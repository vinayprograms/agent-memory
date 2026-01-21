package api

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	"github.com/anthropics/memory-go/internal/core"
	"github.com/anthropics/memory-go/internal/embedding"
	"github.com/anthropics/memory-go/internal/events"
	"github.com/anthropics/memory-go/internal/parser"
	"github.com/anthropics/memory-go/internal/search"
	"github.com/anthropics/memory-go/internal/session"
	"github.com/anthropics/memory-go/internal/storage"
	"github.com/anthropics/memory-go/pkg/types"
)

// Server is the HTTP server for the memory service.
type Server struct {
	config    types.ServerConfig
	store     *storage.Store
	hierarchy *core.HierarchyManager
	search    *search.Engine
	sessions  *session.Manager
	embedder  embedding.Engine
	emitter   *events.Emitter
	extractor *session.Extractor

	httpServer   *http.Server
	startTime    time.Time
	requestCount atomic.Uint64
}

// NewServer creates a new HTTP server.
func NewServer(
	config types.ServerConfig,
	store *storage.Store,
	hierarchy *core.HierarchyManager,
	searchEngine *search.Engine,
	sessions *session.Manager,
	embedder embedding.Engine,
	emitter *events.Emitter,
) *Server {
	return &Server{
		config:    config,
		store:     store,
		hierarchy: hierarchy,
		search:    searchEngine,
		sessions:  sessions,
		embedder:  embedder,
		emitter:   emitter,
		extractor: session.NewExtractor(),
		startTime: time.Now(),
	}
}

// Start starts the HTTP server.
func (s *Server) Start() error {
	mux := http.NewServeMux()

	// JSON-RPC endpoint
	mux.HandleFunc("/rpc", s.handleRPC)

	// REST endpoints for MCP proxy
	mux.HandleFunc("/store", s.handleRESTStore)
	mux.HandleFunc("/query", s.handleRESTQuery)
	mux.HandleFunc("/drill_down", s.handleRESTDrillDown)
	mux.HandleFunc("/zoom_out", s.handleRESTZoomOut)
	mux.HandleFunc("/get_context", s.handleRESTGetContext)
	mux.HandleFunc("/sessions", s.handleRESTSessions)
	mux.HandleFunc("/sessions/", s.handleRESTSessionByID)
	mux.HandleFunc("/nodes/", s.handleRESTNodeByID)

	// Health and metrics
	mux.HandleFunc("/health", s.handleHealth)
	mux.HandleFunc("/metrics", s.handleMetrics)

	// Wrap with logging middleware
	handler := s.loggingMiddleware(mux)

	s.httpServer = &http.Server{
		Addr:         fmt.Sprintf(":%d", s.config.Port),
		Handler:      handler,
		ReadTimeout:  s.config.ReadTimeout,
		WriteTimeout: s.config.WriteTimeout,
	}

	return s.httpServer.ListenAndServe()
}

// loggingMiddleware logs all HTTP requests
func (s *Server) loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()

		// Wrap response writer to capture status code
		lrw := &loggingResponseWriter{ResponseWriter: w, statusCode: http.StatusOK}

		next.ServeHTTP(lrw, r)

		log.Printf("%s %s %d %s", r.Method, r.URL.Path, lrw.statusCode, time.Since(start))
	})
}

type loggingResponseWriter struct {
	http.ResponseWriter
	statusCode int
}

func (lrw *loggingResponseWriter) WriteHeader(code int) {
	lrw.statusCode = code
	lrw.ResponseWriter.WriteHeader(code)
}

// Shutdown gracefully shuts down the server.
func (s *Server) Shutdown(ctx context.Context) error {
	if s.httpServer == nil {
		return nil
	}
	return s.httpServer.Shutdown(ctx)
}

// handleRPC handles JSON-RPC requests.
func (s *Server) handleRPC(w http.ResponseWriter, r *http.Request) {
	s.requestCount.Add(1)

	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	body, err := io.ReadAll(r.Body)
	if err != nil {
		s.writeError(w, nil, types.RPCParseError, "failed to read request body")
		return
	}

	var req Request
	if err := json.Unmarshal(body, &req); err != nil {
		s.writeError(w, nil, types.RPCParseError, "invalid JSON")
		return
	}

	if rpcErr := req.Validate(); rpcErr != nil {
		s.writeError(w, req.ID, rpcErr.Code, rpcErr.Message)
		return
	}

	result, rpcErr := s.dispatch(&req)
	if rpcErr != nil {
		s.writeError(w, req.ID, rpcErr.Code, rpcErr.Message)
		return
	}

	s.writeResult(w, req.ID, result)
}

// dispatch routes a request to the appropriate handler.
func (s *Server) dispatch(req *Request) (interface{}, *types.RPCError) {
	switch req.Method {
	case "store":
		return s.handleStore(req.Params)
	case "store_block":
		return s.handleStoreBlock(req.Params)
	case "store_statement":
		return s.handleStoreStatement(req.Params)
	case "query":
		return s.handleQuery(req.Params)
	case "drill_down":
		return s.handleDrillDown(req.Params)
	case "zoom_out":
		return s.handleZoomOut(req.Params)
	case "get_context":
		return s.handleGetContext(req.Params)
	case "list_sessions":
		return s.handleListSessions()
	case "get_session":
		return s.handleGetSession(req.Params)
	default:
		return nil, types.NewRPCError(types.RPCMethodNotFound, "method not found: "+req.Method, nil)
	}
}

// handleStore handles the "store" method.
// It parses content into a hierarchy: Message -> Block -> Statement
func (s *Server) handleStore(params json.RawMessage) (interface{}, *types.RPCError) {
	var p StoreParams
	if err := json.Unmarshal(params, &p); err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "invalid params: "+err.Error(), nil)
	}

	// Debug: log content length and newline count
	newlineCount := 0
	for _, c := range p.Content {
		if c == '\n' {
			newlineCount++
		}
	}
	log.Printf("[store] session=%s content_len=%d newlines=%d", p.SessionID, len(p.Content), newlineCount)

	if p.SessionID == "" || p.Content == "" {
		return nil, types.NewRPCError(types.RPCInvalidParams, "session_id and content are required", nil)
	}

	// Get or create session
	sess, isNew, err := s.sessions.GetOrCreate(p.SessionID, p.AgentID)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInternalError, err.Error(), nil)
	}

	// Parse content into hierarchy
	parsed := parser.Parse(p.Content)
	log.Printf("[store] parsed %d blocks", len(parsed.Blocks))

	// Create message node (Level 2) - stores the full content
	messageReq := &types.StoreRequest{
		SessionID: p.SessionID,
		AgentID:   p.AgentID,
		Content:   p.Content,
		Role:      p.Role,
		ParentID:  sess.RootNodeID,
		Level:     types.LevelMessage,
	}

	messageNode, err := s.hierarchy.CreateNode(messageReq)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInternalError, err.Error(), nil)
	}

	// Generate and store embedding for message
	if emb, err := s.embedder.Embed(p.Content); err == nil && len(emb) > 0 {
		s.store.SaveEmbedding(messageNode.Level, messageNode.ID, emb)
		s.search.IndexNode(messageNode, emb)
	}

	// Create block nodes (Level 1) under the message
	for _, block := range parsed.Blocks {
		blockReq := &types.StoreRequest{
			SessionID: p.SessionID,
			AgentID:   p.AgentID,
			Content:   block.Content,
			ParentID:  messageNode.ID,
			Level:     types.LevelBlock,
		}

		blockNode, err := s.hierarchy.CreateNode(blockReq)
		if err != nil {
			log.Printf("[store] failed to create block node: %v", err)
			continue
		}

		// Generate and store embedding for block
		if emb, err := s.embedder.Embed(block.Content); err == nil && len(emb) > 0 {
			s.store.SaveEmbedding(blockNode.Level, blockNode.ID, emb)
			s.search.IndexNode(blockNode, emb)
		}

		// Create statement nodes (Level 0) under each block
		for _, stmt := range block.Statements {
			if stmt.Content == "" {
				continue
			}

			stmtReq := &types.StoreRequest{
				SessionID: p.SessionID,
				AgentID:   p.AgentID,
				Content:   stmt.Content,
				ParentID:  blockNode.ID,
				Level:     types.LevelStatement,
			}

			stmtNode, err := s.hierarchy.CreateNode(stmtReq)
			if err != nil {
				log.Printf("[store] failed to create statement node: %v", err)
				continue
			}

			// Generate and store embedding for statement
			if emb, err := s.embedder.Embed(stmt.Content); err == nil && len(emb) > 0 {
				s.store.SaveEmbedding(stmtNode.Level, stmtNode.ID, emb)
				s.search.IndexNode(stmtNode, emb)
			}
		}
	}

	// Extract and add keywords
	keywords, identifiers, files := s.extractor.Extract(p.Content)
	s.sessions.AddKeywords(p.SessionID, keywords)
	s.sessions.AddIdentifiers(p.SessionID, identifiers)
	s.sessions.AddFilesTouched(p.SessionID, files)

	// Emit event
	if s.emitter != nil {
		s.emitter.Emit(events.Event{
			Type:      events.MemoryStored,
			NodeID:    messageNode.ID,
			SessionID: p.SessionID,
			AgentID:   p.AgentID,
			Timestamp: time.Now(),
		})
	}

	return &StoreResult{
		NodeID:      messageNode.ID,
		SequenceNum: messageNode.SequenceNum,
		NewSession:  isNew,
	}, nil
}

// handleStoreBlock handles the "store_block" method.
func (s *Server) handleStoreBlock(params json.RawMessage) (interface{}, *types.RPCError) {
	var p StoreBlockParams
	if err := json.Unmarshal(params, &p); err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "invalid params: "+err.Error(), nil)
	}

	if p.ParentID == 0 || p.Content == "" {
		return nil, types.NewRPCError(types.RPCInvalidParams, "parent_id and content are required", nil)
	}

	// Get parent to inherit session/agent info
	parent, err := s.hierarchy.GetNode(p.ParentID)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "parent not found", nil)
	}

	req := &types.StoreRequest{
		SessionID: parent.SessionID,
		AgentID:   parent.AgentID,
		Content:   p.Content,
		ParentID:  p.ParentID,
		Level:     types.LevelBlock,
	}

	node, err := s.hierarchy.CreateNode(req)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInternalError, err.Error(), nil)
	}

	// Generate and store embedding
	emb, err := s.embedder.Embed(p.Content)
	if err == nil && len(emb) > 0 {
		s.store.SaveEmbedding(node.Level, node.ID, emb)
		s.search.IndexNode(node, emb)
	}

	return &StoreBlockResult{BlockID: node.ID}, nil
}

// handleStoreStatement handles the "store_statement" method.
func (s *Server) handleStoreStatement(params json.RawMessage) (interface{}, *types.RPCError) {
	var p StoreStatementParams
	if err := json.Unmarshal(params, &p); err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "invalid params: "+err.Error(), nil)
	}

	if p.ParentID == 0 || p.Content == "" {
		return nil, types.NewRPCError(types.RPCInvalidParams, "parent_id and content are required", nil)
	}

	parent, err := s.hierarchy.GetNode(p.ParentID)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "parent not found", nil)
	}

	req := &types.StoreRequest{
		SessionID: parent.SessionID,
		AgentID:   parent.AgentID,
		Content:   p.Content,
		ParentID:  p.ParentID,
		Level:     types.LevelStatement,
	}

	node, err := s.hierarchy.CreateNode(req)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInternalError, err.Error(), nil)
	}

	// Generate and store embedding
	emb, err := s.embedder.Embed(p.Content)
	if err == nil && len(emb) > 0 {
		s.store.SaveEmbedding(node.Level, node.ID, emb)
		s.search.IndexNode(node, emb)
	}

	return &StoreStatementResult{StatementID: node.ID}, nil
}

// handleQuery handles the "query" method.
func (s *Server) handleQuery(params json.RawMessage) (interface{}, *types.RPCError) {
	var p QueryParams
	if err := json.Unmarshal(params, &p); err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "invalid params: "+err.Error(), nil)
	}

	if p.Query == "" {
		return nil, types.NewRPCError(types.RPCInvalidParams, "query is required", nil)
	}

	opts := types.SearchOptions{
		Query:      p.Query,
		MaxResults: p.MaxResults,
		MaxTokens:  p.MaxTokens,
		SessionID:  p.SessionID,
		AgentID:    p.AgentID,
		AfterTime:  p.AfterTime,
		BeforeTime: p.BeforeTime,
	}

	if p.TopLevel != nil {
		opts.TopLevel = *p.TopLevel
	} else {
		opts.TopLevel = types.LevelSession
	}

	if p.BottomLevel != nil {
		opts.BottomLevel = *p.BottomLevel
	} else {
		opts.BottomLevel = types.LevelStatement
	}

	// For single level search
	if p.Level != nil {
		opts.TopLevel = *p.Level
		opts.BottomLevel = *p.Level
	}

	// Use SearchWithResponse if max_tokens is specified for truncation info
	var result *QueryResult
	if p.MaxTokens > 0 {
		resp, err := s.search.SearchWithResponse(opts)
		if err != nil {
			return nil, types.NewRPCError(types.RPCInternalError, err.Error(), nil)
		}
		result = &QueryResult{
			Results:      resp.Results,
			TotalResults: resp.TotalResults,
			Truncated:    resp.Truncated,
			TokensUsed:   resp.TokensUsed,
		}
	} else {
		results, err := s.search.Search(opts)
		if err != nil {
			return nil, types.NewRPCError(types.RPCInternalError, err.Error(), nil)
		}
		result = &QueryResult{Results: results}
	}

	// Emit event
	if s.emitter != nil {
		s.emitter.Emit(events.Event{
			Type:      events.QueryPerformed,
			SessionID: p.SessionID,
			AgentID:   p.AgentID,
			Timestamp: time.Now(),
			Data:      map[string]interface{}{"query": p.Query, "result_count": len(result.Results)},
		})
	}

	return result, nil
}

// handleGetContext handles the "get_context" method.
// It returns a node with its parent, siblings, and/or children based on options.
func (s *Server) handleGetContext(params json.RawMessage) (interface{}, *types.RPCError) {
	var p GetContextParams
	if err := json.Unmarshal(params, &p); err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "invalid params: "+err.Error(), nil)
	}

	if p.ID == 0 {
		return nil, types.NewRPCError(types.RPCInvalidParams, "id is required", nil)
	}

	// Get the main node
	node, err := s.hierarchy.GetNode(p.ID)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInternalError, "node not found", nil)
	}

	result := &GetContextResult{Node: node}

	// Include parent if requested
	if p.IncludeParent {
		parent, err := s.hierarchy.GetParent(p.ID)
		if err == nil {
			result.Parent = parent
		}
	}

	// Include siblings if requested
	if p.IncludeSiblings {
		siblings, err := s.hierarchy.GetSiblings(p.ID)
		if err == nil {
			// Filter out the node itself from siblings
			filteredSiblings := make([]*types.Node, 0, len(siblings))
			for _, sib := range siblings {
				if sib.ID != p.ID {
					filteredSiblings = append(filteredSiblings, sib)
				}
			}
			result.Siblings = filteredSiblings
		}
	}

	// Include children if requested
	if p.IncludeChildren {
		children, err := s.hierarchy.GetChildren(p.ID)
		if err == nil {
			// Apply max depth if specified (1 = immediate children only)
			if p.MaxDepth <= 0 || p.MaxDepth >= 1 {
				result.Children = children
			}
			// For deeper traversal (max_depth > 1), we'd recursively get descendants
			// but that's handled by the drill_down method for efficiency
		}
	}

	return result, nil
}

// handleDrillDown handles the "drill_down" method.
func (s *Server) handleDrillDown(params json.RawMessage) (interface{}, *types.RPCError) {
	var p DrillDownParams
	if err := json.Unmarshal(params, &p); err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "invalid params: "+err.Error(), nil)
	}

	if p.ID == 0 {
		return nil, types.NewRPCError(types.RPCInvalidParams, "id is required", nil)
	}

	children, err := s.hierarchy.GetChildren(p.ID)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInternalError, err.Error(), nil)
	}

	// Apply filter if specified
	if p.Filter != "" {
		filtered := make([]*types.Node, 0)
		for _, child := range children {
			if containsIgnoreCase(child.Content, p.Filter) {
				filtered = append(filtered, child)
			}
		}
		children = filtered
	}

	// Apply max results
	if p.MaxResults > 0 && len(children) > p.MaxResults {
		children = children[:p.MaxResults]
	}

	return &DrillDownResult{Children: children}, nil
}

// handleZoomOut handles the "zoom_out" method.
func (s *Server) handleZoomOut(params json.RawMessage) (interface{}, *types.RPCError) {
	var p ZoomOutParams
	if err := json.Unmarshal(params, &p); err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "invalid params: "+err.Error(), nil)
	}

	if p.ID == 0 {
		return nil, types.NewRPCError(types.RPCInvalidParams, "id is required", nil)
	}

	ancestors, err := s.hierarchy.GetAncestors(p.ID)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInternalError, err.Error(), nil)
	}

	return &ZoomOutResult{Ancestors: ancestors}, nil
}

// handleListSessions handles the "list_sessions" method.
func (s *Server) handleListSessions() (interface{}, *types.RPCError) {
	sessions := s.sessions.List()
	return &ListSessionsResult{Sessions: sessions}, nil
}

// handleGetSession handles the "get_session" method.
func (s *Server) handleGetSession(params json.RawMessage) (interface{}, *types.RPCError) {
	var p GetSessionParams
	if err := json.Unmarshal(params, &p); err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "invalid params: "+err.Error(), nil)
	}

	if p.SessionID == "" {
		return nil, types.NewRPCError(types.RPCInvalidParams, "session_id is required", nil)
	}

	sess, err := s.sessions.Get(p.SessionID)
	if err != nil {
		return nil, types.NewRPCError(types.RPCInvalidParams, "session not found", nil)
	}

	return &GetSessionResult{Session: sess}, nil
}

// handleHealth handles health check requests.
func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	stats := s.store.Stats()
	nodeCount, _ := stats["node_count"].(uint64)

	result := HealthResult{
		Healthy:      true,
		Status:       "ok",
		NodeCount:    nodeCount,
		UptimeMs:     time.Since(s.startTime).Milliseconds(),
		RequestCount: s.requestCount.Load(),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(result)
}

// handleMetrics handles Prometheus-style metrics requests.
func (s *Server) handleMetrics(w http.ResponseWriter, r *http.Request) {
	stats := s.store.Stats()
	searchStats := s.search.Stats()
	sessionStats := s.sessions.Stats()

	w.Header().Set("Content-Type", "text/plain")

	// Basic metrics in Prometheus format
	fmt.Fprintf(w, "# HELP memory_requests_total Total number of requests\n")
	fmt.Fprintf(w, "# TYPE memory_requests_total counter\n")
	fmt.Fprintf(w, "memory_requests_total %d\n", s.requestCount.Load())

	fmt.Fprintf(w, "# HELP memory_uptime_seconds Server uptime in seconds\n")
	fmt.Fprintf(w, "# TYPE memory_uptime_seconds gauge\n")
	fmt.Fprintf(w, "memory_uptime_seconds %.2f\n", time.Since(s.startTime).Seconds())

	if nodeCount, ok := stats["node_count"].(uint64); ok {
		fmt.Fprintf(w, "# HELP memory_nodes_total Total number of nodes\n")
		fmt.Fprintf(w, "# TYPE memory_nodes_total gauge\n")
		fmt.Fprintf(w, "memory_nodes_total %d\n", nodeCount)
	}

	if sessionCount, ok := sessionStats["total_sessions"].(int); ok {
		fmt.Fprintf(w, "# HELP memory_sessions_total Total number of sessions\n")
		fmt.Fprintf(w, "# TYPE memory_sessions_total gauge\n")
		fmt.Fprintf(w, "memory_sessions_total %d\n", sessionCount)
	}

	if vectorStats, ok := searchStats["vector_index"].(map[string]interface{}); ok {
		if total, ok := vectorStats["total_vectors"].(int); ok {
			fmt.Fprintf(w, "# HELP memory_vectors_total Total indexed vectors\n")
			fmt.Fprintf(w, "# TYPE memory_vectors_total gauge\n")
			fmt.Fprintf(w, "memory_vectors_total %d\n", total)
		}
	}
}

// Helper functions

func (s *Server) writeResult(w http.ResponseWriter, id interface{}, result interface{}) {
	resp := NewResponse(id, result)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func (s *Server) writeError(w http.ResponseWriter, id interface{}, code int, message string) {
	resp := NewErrorResponse(id, code, message, nil)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func containsIgnoreCase(s, substr string) bool {
	if len(substr) == 0 {
		return true
	}
	if len(s) < len(substr) {
		return false
	}

	sLower := toLower(s)
	substrLower := toLower(substr)

	for i := 0; i <= len(sLower)-len(substrLower); i++ {
		if sLower[i:i+len(substrLower)] == substrLower {
			return true
		}
	}
	return false
}

func toLower(s string) string {
	b := make([]byte, len(s))
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c >= 'A' && c <= 'Z' {
			c += 'a' - 'A'
		}
		b[i] = c
	}
	return string(b)
}

// REST endpoint handlers (for MCP proxy)

func (s *Server) handleRESTStore(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var p StoreParams
	if err := json.NewDecoder(r.Body).Decode(&p); err != nil {
		s.writeJSONError(w, http.StatusBadRequest, "invalid JSON: "+err.Error())
		return
	}

	result, rpcErr := s.handleStore(mustMarshal(p))
	if rpcErr != nil {
		s.writeJSONError(w, http.StatusBadRequest, rpcErr.Message)
		return
	}

	s.writeJSON(w, result)
}

func (s *Server) handleRESTQuery(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var p QueryParams
	if err := json.NewDecoder(r.Body).Decode(&p); err != nil {
		s.writeJSONError(w, http.StatusBadRequest, "invalid JSON: "+err.Error())
		return
	}

	result, rpcErr := s.handleQuery(mustMarshal(p))
	if rpcErr != nil {
		s.writeJSONError(w, http.StatusBadRequest, rpcErr.Message)
		return
	}

	s.writeJSON(w, result)
}

func (s *Server) handleRESTDrillDown(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var p DrillDownParams
	if err := json.NewDecoder(r.Body).Decode(&p); err != nil {
		s.writeJSONError(w, http.StatusBadRequest, "invalid JSON: "+err.Error())
		return
	}

	result, rpcErr := s.handleDrillDown(mustMarshal(p))
	if rpcErr != nil {
		s.writeJSONError(w, http.StatusBadRequest, rpcErr.Message)
		return
	}

	s.writeJSON(w, result)
}

func (s *Server) handleRESTZoomOut(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var p ZoomOutParams
	if err := json.NewDecoder(r.Body).Decode(&p); err != nil {
		s.writeJSONError(w, http.StatusBadRequest, "invalid JSON: "+err.Error())
		return
	}

	result, rpcErr := s.handleZoomOut(mustMarshal(p))
	if rpcErr != nil {
		s.writeJSONError(w, http.StatusBadRequest, rpcErr.Message)
		return
	}

	s.writeJSON(w, result)
}

func (s *Server) handleRESTGetContext(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var p GetContextParams
	if err := json.NewDecoder(r.Body).Decode(&p); err != nil {
		s.writeJSONError(w, http.StatusBadRequest, "invalid JSON: "+err.Error())
		return
	}

	result, rpcErr := s.handleGetContext(mustMarshal(p))
	if rpcErr != nil {
		s.writeJSONError(w, http.StatusBadRequest, rpcErr.Message)
		return
	}

	s.writeJSON(w, result)
}

func (s *Server) handleRESTSessions(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	result, _ := s.handleListSessions()
	s.writeJSON(w, result)
}

func (s *Server) handleRESTSessionByID(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Extract session ID from path: /sessions/{id}
	sessionID := strings.TrimPrefix(r.URL.Path, "/sessions/")
	if sessionID == "" {
		s.writeJSONError(w, http.StatusBadRequest, "session_id required")
		return
	}

	sess, err := s.sessions.Get(sessionID)
	if err != nil {
		s.writeJSONError(w, http.StatusNotFound, "session not found")
		return
	}

	s.writeJSON(w, sess)
}

func (s *Server) handleRESTNodeByID(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Extract node ID from path: /nodes/{id}
	idStr := strings.TrimPrefix(r.URL.Path, "/nodes/")
	if idStr == "" {
		s.writeJSONError(w, http.StatusBadRequest, "node id required")
		return
	}

	id, err := strconv.ParseUint(idStr, 10, 64)
	if err != nil {
		s.writeJSONError(w, http.StatusBadRequest, "invalid node id")
		return
	}

	node, err := s.hierarchy.GetNode(types.NodeID(id))
	if err != nil {
		s.writeJSONError(w, http.StatusNotFound, "node not found")
		return
	}

	s.writeJSON(w, node)
}

func (s *Server) writeJSON(w http.ResponseWriter, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(data)
}

func (s *Server) writeJSONError(w http.ResponseWriter, status int, message string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(map[string]string{"error": message})
}

func mustMarshal(v interface{}) json.RawMessage {
	data, _ := json.Marshal(v)
	return data
}
