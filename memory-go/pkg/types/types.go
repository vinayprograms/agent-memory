// Package types defines the core data types for the memory service.
package types

import (
	"time"
)

// EmbeddingDim is the dimension of embedding vectors (all-MiniLM-L6-v2).
const EmbeddingDim = 384

// HierarchyLevel represents the level in the memory hierarchy.
type HierarchyLevel uint8

const (
	LevelStatement HierarchyLevel = iota // Individual sentence or code line
	LevelBlock                           // Logical section (code, explanation, tool output)
	LevelMessage                         // Single turn in conversation
	LevelSession                         // Entire agent work session
	LevelAgent                           // Agent instance (optional)
)

func (l HierarchyLevel) String() string {
	switch l {
	case LevelStatement:
		return "statement"
	case LevelBlock:
		return "block"
	case LevelMessage:
		return "message"
	case LevelSession:
		return "session"
	case LevelAgent:
		return "agent"
	default:
		return "unknown"
	}
}

// NodeID is a unique identifier for a node in the hierarchy.
type NodeID uint64

// Embedding represents a vector embedding.
type Embedding []float32

// Node represents a node in the memory hierarchy.
type Node struct {
	ID             NodeID         `json:"id"`
	Level          HierarchyLevel `json:"level"`
	ParentID       NodeID         `json:"parent_id,omitempty"`
	FirstChildID   NodeID         `json:"first_child_id,omitempty"`
	NextSiblingID  NodeID         `json:"next_sibling_id,omitempty"`
	AgentID        string         `json:"agent_id,omitempty"`
	SessionID      string         `json:"session_id,omitempty"`
	Content        string         `json:"content,omitempty"`
	Role           string         `json:"role,omitempty"` // user, assistant, tool
	CreatedAt      time.Time      `json:"created_at"`
	SequenceNum    uint64         `json:"sequence_num"`
	EmbeddingIndex uint64         `json:"embedding_index,omitempty"`
}

// Session represents a conversation session.
type Session struct {
	ID           string    `json:"id"`
	AgentID      string    `json:"agent_id"`
	Title        string    `json:"title,omitempty"`
	Keywords     []string  `json:"keywords,omitempty"`
	Identifiers  []string  `json:"identifiers,omitempty"`
	FilesTouched []string  `json:"files_touched,omitempty"`
	RootNodeID   NodeID    `json:"root_node_id"`
	CreatedAt    time.Time `json:"created_at"`
	LastActiveAt time.Time `json:"last_active_at"`
	SequenceNum  uint64    `json:"sequence_num"`
}

// SearchResult represents a single search result.
type SearchResult struct {
	NodeID         NodeID         `json:"node_id"`
	Level          HierarchyLevel `json:"level"`
	Content        string         `json:"content"`
	AgentID        string         `json:"agent_id,omitempty"`
	SessionID      string         `json:"session_id,omitempty"`
	CreatedAt      time.Time      `json:"created_at"`
	RelevanceScore float32        `json:"relevance_score"`
	RecencyScore   float32        `json:"recency_score"`
	CombinedScore  float32        `json:"combined_score"`
	TokenCount     int            `json:"token_count,omitempty"` // Estimated token count
}

// SearchResponse wraps search results with metadata.
type SearchResponse struct {
	Results      []SearchResult `json:"results"`
	TotalResults int            `json:"total_results"`
	Truncated    bool           `json:"truncated"`
	TokensUsed   int            `json:"tokens_used,omitempty"`
}

// SearchOptions configures a search query.
type SearchOptions struct {
	Query       string         `json:"query"`
	TopLevel    HierarchyLevel `json:"top_level,omitempty"`
	BottomLevel HierarchyLevel `json:"bottom_level,omitempty"`
	MaxResults  int            `json:"max_results,omitempty"`
	MaxTokens   int            `json:"max_tokens,omitempty"`   // Token budget for results
	SessionID   string         `json:"session_id,omitempty"`
	AgentID     string         `json:"agent_id,omitempty"`
	AfterTime   int64          `json:"after_time,omitempty"`  // Unix timestamp (nanoseconds)
	BeforeTime  int64          `json:"before_time,omitempty"` // Unix timestamp (nanoseconds)
}

// ContextOptions configures context expansion for get_context method.
type ContextOptions struct {
	NodeID          NodeID `json:"node_id"`
	IncludeParent   bool   `json:"include_parent,omitempty"`
	IncludeSiblings bool   `json:"include_siblings,omitempty"`
	IncludeChildren bool   `json:"include_children,omitempty"`
	MaxDepth        int    `json:"max_depth,omitempty"`
}

// ContextResult contains the expanded context for a node.
type ContextResult struct {
	Node     *Node   `json:"node"`
	Parent   *Node   `json:"parent,omitempty"`
	Siblings []*Node `json:"siblings,omitempty"`
	Children []*Node `json:"children,omitempty"`
}

// StoreRequest represents a request to store content.
type StoreRequest struct {
	SessionID string `json:"session_id"`
	AgentID   string `json:"agent_id"`
	Content   string `json:"content"`
	Role      string `json:"role,omitempty"`
	ParentID  NodeID `json:"parent_id,omitempty"`
	Level     HierarchyLevel `json:"level,omitempty"`
}

// StoreResponse represents the response from a store operation.
type StoreResponse struct {
	NodeID     NodeID `json:"node_id"`
	SequenceNum uint64 `json:"sequence_num"`
	NewSession bool   `json:"new_session,omitempty"`
}

// DrillDownRequest represents a request to get children of a node.
type DrillDownRequest struct {
	NodeID     NodeID `json:"node_id"`
	Filter     string `json:"filter,omitempty"`
	MaxResults int    `json:"max_results,omitempty"`
}

// ZoomOutRequest represents a request to get ancestors of a node.
type ZoomOutRequest struct {
	NodeID NodeID `json:"node_id"`
}

// MaxKeywords is the maximum number of keywords per session.
const MaxKeywords = 32

// MaxIdentifiers is the maximum number of identifiers per session.
const MaxIdentifiers = 128

// MaxFilesTouched is the maximum number of files tracked per session.
const MaxFilesTouched = 64

// MaxSessionIDLen is the maximum length of a session ID.
const MaxSessionIDLen = 64

// MaxAgentIDLen is the maximum length of an agent ID.
const MaxAgentIDLen = 64

// MaxContentLen is the maximum length of content.
const MaxContentLen = 65536
