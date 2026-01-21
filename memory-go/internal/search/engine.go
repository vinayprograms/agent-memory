package search

import (
	"sort"
	"time"

	"github.com/anthropics/memory-go/internal/embedding"
	"github.com/anthropics/memory-go/internal/storage"
	"github.com/anthropics/memory-go/pkg/types"
)

// Engine provides unified search combining semantic and keyword search.
type Engine struct {
	store         *storage.Store
	vectorIndex   *VectorIndex
	invertedIndex *InvertedIndex
	embedder      embedding.Engine
	config        types.SearchConfig
}

// NewEngine creates a new search engine.
func NewEngine(store *storage.Store, embedder embedding.Engine, config types.SearchConfig) (*Engine, error) {
	e := &Engine{
		store:         store,
		vectorIndex:   NewVectorIndex(config),
		invertedIndex: NewInvertedIndex(),
		embedder:      embedder,
		config:        config,
	}

	// Rebuild indices from storage
	if err := e.rebuildIndices(); err != nil {
		return nil, err
	}

	return e, nil
}

// rebuildIndices reconstructs search indices from storage.
func (e *Engine) rebuildIndices() error {
	return e.store.IterateNodes(func(node *types.Node) error {
		// Add to inverted index
		e.invertedIndex.Add(node.ID, node.Content)

		// Try to get embedding from storage and add to vector index
		emb, err := e.store.GetEmbedding(node.Level, node.ID)
		if err == nil && len(emb) > 0 {
			e.vectorIndex.Add(node.Level, node.ID, emb)
		}

		return nil
	})
}

// IndexNode adds a node to all search indices.
func (e *Engine) IndexNode(node *types.Node, emb types.Embedding) error {
	// Add to inverted index
	e.invertedIndex.Add(node.ID, node.Content)

	// Add to vector index
	if len(emb) > 0 {
		if err := e.vectorIndex.Add(node.Level, node.ID, emb); err != nil {
			return err
		}
	}

	return nil
}

// RemoveNode removes a node from all search indices.
func (e *Engine) RemoveNode(id types.NodeID, level types.HierarchyLevel) error {
	e.invertedIndex.Remove(id)
	return e.vectorIndex.Remove(level, id)
}

// Search performs a hybrid search combining semantic and keyword matching.
func (e *Engine) Search(opts types.SearchOptions) ([]types.SearchResult, error) {
	if opts.MaxResults <= 0 {
		opts.MaxResults = e.config.DefaultMaxResults
	}
	if opts.TopLevel == 0 && opts.BottomLevel == 0 {
		opts.TopLevel = types.LevelSession
		opts.BottomLevel = types.LevelStatement
	}

	// Generate query embedding
	queryEmb, err := e.embedder.Embed(opts.Query)
	if err != nil {
		return nil, types.WrapError("search.Search", types.ErrEmbeddingFailed, err)
	}

	// Semantic search across levels
	semanticMatches, err := e.vectorIndex.SearchMultiLevel(
		queryEmb,
		opts.TopLevel,
		opts.BottomLevel,
		opts.MaxResults*2, // Get extra for filtering
	)
	if err != nil {
		return nil, err
	}

	// Keyword search
	keywordMatches := e.invertedIndex.SearchWithScores(opts.Query, opts.MaxResults*2)

	// Combine results
	results := e.combineResults(semanticMatches, keywordMatches, queryEmb, opts)

	// Sort by combined score
	sort.Slice(results, func(i, j int) bool {
		return results[i].CombinedScore > results[j].CombinedScore
	})

	// Apply max results limit
	if len(results) > opts.MaxResults {
		results = results[:opts.MaxResults]
	}

	// Apply token budget if specified
	if opts.MaxTokens > 0 {
		results = e.applyTokenBudget(results, opts.MaxTokens)
	}

	return results, nil
}

// SearchWithResponse performs search and returns full response with metadata.
func (e *Engine) SearchWithResponse(opts types.SearchOptions) (*types.SearchResponse, error) {
	// Get all matching results first
	allResults, err := e.Search(types.SearchOptions{
		Query:       opts.Query,
		TopLevel:    opts.TopLevel,
		BottomLevel: opts.BottomLevel,
		MaxResults:  opts.MaxResults * 2, // Get more for accurate total count
		SessionID:   opts.SessionID,
		AgentID:     opts.AgentID,
		AfterTime:   opts.AfterTime,
		BeforeTime:  opts.BeforeTime,
		MaxTokens:   0, // Don't apply token budget yet
	})
	if err != nil {
		return nil, err
	}

	totalResults := len(allResults)
	truncated := false
	tokensUsed := 0

	// Apply max results limit
	if len(allResults) > opts.MaxResults {
		allResults = allResults[:opts.MaxResults]
		truncated = true
	}

	// Apply token budget if specified
	if opts.MaxTokens > 0 {
		before := len(allResults)
		allResults = e.applyTokenBudget(allResults, opts.MaxTokens)
		for _, r := range allResults {
			tokensUsed += r.TokenCount
		}
		if len(allResults) < before {
			truncated = true
		}
	}

	return &types.SearchResponse{
		Results:      allResults,
		TotalResults: totalResults,
		Truncated:    truncated,
		TokensUsed:   tokensUsed,
	}, nil
}

// applyTokenBudget truncates results to fit within token budget.
func (e *Engine) applyTokenBudget(results []types.SearchResult, maxTokens int) []types.SearchResult {
	tokenCount := 0
	for i := range results {
		// Estimate tokens as ~4 characters per token (rough approximation)
		results[i].TokenCount = (len(results[i].Content) + 3) / 4
		tokenCount += results[i].TokenCount

		if tokenCount > maxTokens {
			return results[:i]
		}
	}
	return results
}

// combineResults merges semantic and keyword results with scoring.
func (e *Engine) combineResults(
	semantic []SearchMatch,
	keyword []KeywordMatch,
	queryEmb types.Embedding,
	opts types.SearchOptions,
) []types.SearchResult {
	// Build score maps
	semanticScores := make(map[types.NodeID]float32)
	levelMap := make(map[types.NodeID]types.HierarchyLevel)

	for _, m := range semantic {
		// Convert distance to similarity (1 - normalized distance)
		similarity := 1.0 - m.Distance
		if similarity < 0 {
			similarity = 0
		}
		semanticScores[m.NodeID] = similarity
		levelMap[m.NodeID] = m.Level
	}

	keywordScores := make(map[types.NodeID]float32)
	for _, m := range keyword {
		keywordScores[m.NodeID] = m.Score
	}

	// Merge all node IDs
	allNodes := make(map[types.NodeID]struct{})
	for id := range semanticScores {
		allNodes[id] = struct{}{}
	}
	for id := range keywordScores {
		allNodes[id] = struct{}{}
	}

	// Calculate combined scores
	now := time.Now()
	results := make([]types.SearchResult, 0, len(allNodes))

	for id := range allNodes {
		node, err := e.store.GetNode(id)
		if err != nil {
			continue
		}

		// Filter by session/agent if specified
		if opts.SessionID != "" && node.SessionID != opts.SessionID {
			continue
		}
		if opts.AgentID != "" && node.AgentID != opts.AgentID {
			continue
		}

		// Filter by level range
		if node.Level < opts.BottomLevel || node.Level > opts.TopLevel {
			continue
		}

		// Filter by time range
		nodeTime := node.CreatedAt.UnixNano()
		if opts.AfterTime > 0 && nodeTime < opts.AfterTime {
			continue
		}
		if opts.BeforeTime > 0 && nodeTime > opts.BeforeTime {
			continue
		}

		// Get scores (default to 0 if not present)
		semScore := semanticScores[id]
		kwScore := keywordScores[id]

		// Calculate recency score (exponential decay)
		age := now.Sub(node.CreatedAt)
		recencyScore := float32(1.0 / (1.0 + age.Hours()/24.0)) // Decay over days

		// Calculate level boost
		// Per spec: statement=1.0, block=0.6, message=0.3, session=0.0
		var levelBoost float32
		switch node.Level {
		case types.LevelStatement:
			levelBoost = 1.0
		case types.LevelBlock:
			levelBoost = 0.6
		case types.LevelMessage:
			levelBoost = 0.3
		case types.LevelSession:
			levelBoost = 0.0
		default:
			levelBoost = 0.0
		}

		// Combine with semantic getting more weight if both are present
		var relevanceScore float32
		if semScore > 0 && kwScore > 0 {
			relevanceScore = 0.7*semScore + 0.3*kwScore
		} else if semScore > 0 {
			relevanceScore = semScore
		} else {
			relevanceScore = kwScore
		}

		// Final combined score using config weights
		// Per spec: 0.6×relevance + 0.3×recency + 0.1×level_boost
		combinedScore := e.config.RelevanceWeight*relevanceScore +
			e.config.RecencyWeight*recencyScore +
			e.config.LevelBoostWeight*levelBoost

		results = append(results, types.SearchResult{
			NodeID:         id,
			Level:          node.Level,
			Content:        node.Content,
			AgentID:        node.AgentID,
			SessionID:      node.SessionID,
			CreatedAt:      node.CreatedAt,
			RelevanceScore: relevanceScore,
			RecencyScore:   recencyScore,
			CombinedScore:  combinedScore,
		})
	}

	return results
}

// SemanticSearch performs pure semantic search.
func (e *Engine) SemanticSearch(query string, level types.HierarchyLevel, k int) ([]types.SearchResult, error) {
	queryEmb, err := e.embedder.Embed(query)
	if err != nil {
		return nil, types.WrapError("search.SemanticSearch", types.ErrEmbeddingFailed, err)
	}

	matches, err := e.vectorIndex.Search(level, queryEmb, k)
	if err != nil {
		return nil, err
	}

	results := make([]types.SearchResult, 0, len(matches))
	for _, m := range matches {
		node, err := e.store.GetNode(m.NodeID)
		if err != nil {
			continue
		}

		similarity := 1.0 - m.Distance
		if similarity < 0 {
			similarity = 0
		}

		results = append(results, types.SearchResult{
			NodeID:         m.NodeID,
			Level:          node.Level,
			Content:        node.Content,
			AgentID:        node.AgentID,
			SessionID:      node.SessionID,
			CreatedAt:      node.CreatedAt,
			RelevanceScore: similarity,
			CombinedScore:  similarity,
		})
	}

	return results, nil
}

// KeywordSearch performs pure keyword search.
func (e *Engine) KeywordSearch(query string, maxResults int) ([]types.SearchResult, error) {
	matches := e.invertedIndex.SearchWithScores(query, maxResults)

	results := make([]types.SearchResult, 0, len(matches))
	for _, m := range matches {
		node, err := e.store.GetNode(m.NodeID)
		if err != nil {
			continue
		}

		results = append(results, types.SearchResult{
			NodeID:         m.NodeID,
			Level:          node.Level,
			Content:        node.Content,
			AgentID:        node.AgentID,
			SessionID:      node.SessionID,
			CreatedAt:      node.CreatedAt,
			RelevanceScore: m.Score,
			CombinedScore:  m.Score,
		})
	}

	return results, nil
}

// Stats returns search engine statistics.
func (e *Engine) Stats() map[string]interface{} {
	vectorStats := e.vectorIndex.Stats()
	invertedStats := e.invertedIndex.Stats()

	stats := map[string]interface{}{
		"vector_index":   vectorStats,
		"inverted_index": invertedStats,
	}

	return stats
}

// Clear removes all entries from all indices.
func (e *Engine) Clear() {
	e.vectorIndex.Clear()
	e.invertedIndex.Clear()
}
