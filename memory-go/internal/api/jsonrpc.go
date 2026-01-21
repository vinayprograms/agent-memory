// Package api provides the HTTP server and JSON-RPC API.
package api

import (
	"encoding/json"

	"github.com/anthropics/memory-go/pkg/types"
)

// JSON-RPC 2.0 request and response structures

// Request represents a JSON-RPC 2.0 request.
type Request struct {
	JSONRPC string          `json:"jsonrpc"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params,omitempty"`
	ID      interface{}     `json:"id,omitempty"`
}

// Response represents a JSON-RPC 2.0 response.
type Response struct {
	JSONRPC string           `json:"jsonrpc"`
	Result  interface{}      `json:"result,omitempty"`
	Error   *types.RPCError  `json:"error,omitempty"`
	ID      interface{}      `json:"id"`
}

// NewResponse creates a successful response.
func NewResponse(id interface{}, result interface{}) *Response {
	return &Response{
		JSONRPC: "2.0",
		Result:  result,
		ID:      id,
	}
}

// NewErrorResponse creates an error response.
func NewErrorResponse(id interface{}, code int, message string, data interface{}) *Response {
	return &Response{
		JSONRPC: "2.0",
		Error: &types.RPCError{
			Code:    code,
			Message: message,
			Data:    data,
		},
		ID: id,
	}
}

// Method parameter types

// StoreParams contains parameters for the store method.
type StoreParams struct {
	SessionID string `json:"session_id"`
	AgentID   string `json:"agent_id"`
	Content   string `json:"content"`
	Role      string `json:"role,omitempty"`
}

// StoreBlockParams contains parameters for the store_block method.
type StoreBlockParams struct {
	ParentID types.NodeID `json:"parent_id"`
	Content  string       `json:"content"`
}

// StoreStatementParams contains parameters for the store_statement method.
type StoreStatementParams struct {
	ParentID types.NodeID `json:"parent_id"`
	Content  string       `json:"content"`
}

// QueryParams contains parameters for the query method.
type QueryParams struct {
	Query       string                `json:"query"`
	Level       *types.HierarchyLevel `json:"level,omitempty"`
	TopLevel    *types.HierarchyLevel `json:"top_level,omitempty"`
	BottomLevel *types.HierarchyLevel `json:"bottom_level,omitempty"`
	MaxResults  int                   `json:"max_results,omitempty"`
	MaxTokens   int                   `json:"max_tokens,omitempty"`
	SessionID   string                `json:"session_id,omitempty"`
	AgentID     string                `json:"agent_id,omitempty"`
	AfterTime   int64                 `json:"after_time,omitempty"`  // Unix timestamp (nanoseconds)
	BeforeTime  int64                 `json:"before_time,omitempty"` // Unix timestamp (nanoseconds)
}

// GetContextParams contains parameters for the get_context method.
type GetContextParams struct {
	ID              types.NodeID `json:"id"`
	IncludeParent   bool         `json:"include_parent,omitempty"`
	IncludeSiblings bool         `json:"include_siblings,omitempty"`
	IncludeChildren bool         `json:"include_children,omitempty"`
	MaxDepth        int          `json:"max_depth,omitempty"`
}

// DrillDownParams contains parameters for the drill_down method.
type DrillDownParams struct {
	ID         types.NodeID `json:"id"`
	Filter     string       `json:"filter,omitempty"`
	MaxResults int          `json:"max_results,omitempty"`
}

// ZoomOutParams contains parameters for the zoom_out method.
type ZoomOutParams struct {
	ID types.NodeID `json:"id"`
}

// GetSessionParams contains parameters for the get_session method.
type GetSessionParams struct {
	SessionID string `json:"session_id"`
}

// Method result types

// StoreResult is returned by the store method.
type StoreResult struct {
	NodeID      types.NodeID `json:"node_id"`
	SequenceNum uint64       `json:"sequence_num"`
	NewSession  bool         `json:"new_session,omitempty"`
}

// StoreBlockResult is returned by the store_block method.
type StoreBlockResult struct {
	BlockID types.NodeID `json:"block_id"`
}

// StoreStatementResult is returned by the store_statement method.
type StoreStatementResult struct {
	StatementID types.NodeID `json:"statement_id"`
}

// QueryResult is returned by the query method.
type QueryResult struct {
	Results      []types.SearchResult `json:"results"`
	TotalResults int                  `json:"total_results,omitempty"`
	Truncated    bool                 `json:"truncated,omitempty"`
	TokensUsed   int                  `json:"tokens_used,omitempty"`
}

// GetContextResult is returned by the get_context method.
type GetContextResult struct {
	Node     *types.Node   `json:"node"`
	Parent   *types.Node   `json:"parent,omitempty"`
	Siblings []*types.Node `json:"siblings,omitempty"`
	Children []*types.Node `json:"children,omitempty"`
}

// DrillDownResult is returned by the drill_down method.
type DrillDownResult struct {
	Children []*types.Node `json:"children"`
}

// ZoomOutResult is returned by the zoom_out method.
type ZoomOutResult struct {
	Ancestors []*types.Node `json:"ancestors"`
}

// ListSessionsResult is returned by the list_sessions method.
type ListSessionsResult struct {
	Sessions []*types.Session `json:"sessions"`
}

// GetSessionResult is returned by the get_session method.
type GetSessionResult struct {
	Session *types.Session `json:"session"`
}

// HealthResult is returned by the health endpoint.
type HealthResult struct {
	Healthy      bool   `json:"healthy"`
	Status       string `json:"status"`
	NodeCount    uint64 `json:"node_count"`
	UptimeMs     int64  `json:"uptime_ms"`
	RequestCount uint64 `json:"request_count"`
}

// Validate validates a JSON-RPC request.
func (r *Request) Validate() *types.RPCError {
	if r.JSONRPC != "2.0" {
		return types.NewRPCError(types.RPCInvalidRequest, "invalid JSON-RPC version", nil)
	}
	if r.Method == "" {
		return types.NewRPCError(types.RPCInvalidRequest, "method is required", nil)
	}
	return nil
}
