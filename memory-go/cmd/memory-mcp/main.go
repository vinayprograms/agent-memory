// Package main provides an MCP server that wraps the HTTP memory service.
// This is a thin client that proxies requests to the HTTP server.
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

const version = "0.1.0"

var httpClient = &http.Client{Timeout: 30 * time.Second}

func main() {
	baseURL := flag.String("url", "http://localhost:8080", "Memory service HTTP URL")
	flag.StringVar(baseURL, "u", "http://localhost:8080", "Memory service HTTP URL (shorthand)")

	help := flag.Bool("help", false, "Show help")
	flag.BoolVar(help, "h", false, "Show help (shorthand)")

	flag.Parse()

	if *help {
		fmt.Fprintf(os.Stderr, `Memory Service MCP Client v%s

MCP server that proxies requests to the memory HTTP service.
Requires memory-server to be running.

Usage: memory-mcp [OPTIONS]

Options:
  -u, --url URL    Memory service URL (default: http://localhost:8080)
  -h, --help       Show this help

Claude Code MCP Configuration:
  "mcpServers": {
    "memory": {
      "command": "memory-mcp",
      "args": ["-u", "http://localhost:8080"]
    }
  }

The HTTP server must be running:
  memory-server -d ./data -p 8080
`, version)
		os.Exit(0)
	}

	// Create MCP server
	server := mcp.NewServer(&mcp.Implementation{
		Name:    "memory-mcp",
		Version: version,
	}, nil)

	// Create proxy client
	proxy := &proxyClient{baseURL: *baseURL}

	// Register tools
	registerTools(server, proxy)

	// Handle shutdown
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-sigChan
		cancel()
	}()

	// Run MCP server over stdio
	if err := server.Run(ctx, &mcp.StdioTransport{}); err != nil && ctx.Err() == nil {
		log.Fatalf("Server error: %v", err)
	}
}

type proxyClient struct {
	baseURL string
}

func (p *proxyClient) post(endpoint string, body any) (map[string]any, error) {
	data, err := json.Marshal(body)
	if err != nil {
		return nil, err
	}

	resp, err := httpClient.Post(p.baseURL+endpoint, "application/json", bytes.NewReader(data))
	if err != nil {
		return nil, fmt.Errorf("HTTP request failed: %w", err)
	}
	defer resp.Body.Close()

	respData, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read response: %w", err)
	}

	if resp.StatusCode >= 400 {
		return nil, fmt.Errorf("HTTP %d: %s", resp.StatusCode, string(respData))
	}

	var result map[string]any
	if err := json.Unmarshal(respData, &result); err != nil {
		return nil, fmt.Errorf("parse response: %w", err)
	}

	return result, nil
}

func (p *proxyClient) get(endpoint string) (map[string]any, error) {
	resp, err := httpClient.Get(p.baseURL + endpoint)
	if err != nil {
		return nil, fmt.Errorf("HTTP request failed: %w", err)
	}
	defer resp.Body.Close()

	respData, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read response: %w", err)
	}

	if resp.StatusCode >= 400 {
		return nil, fmt.Errorf("HTTP %d: %s", resp.StatusCode, string(respData))
	}

	var result map[string]any
	if err := json.Unmarshal(respData, &result); err != nil {
		return nil, fmt.Errorf("parse response: %w", err)
	}

	return result, nil
}

// Tool argument types

type QueryArgs struct {
	Query      string `json:"query" jsonschema:"Search query text"`
	Level      string `json:"level,omitempty" jsonschema:"Filter by level (session/message/block/statement)"`
	MaxResults int    `json:"max_results,omitempty" jsonschema:"Maximum results (default 10)"`
	MaxTokens  int    `json:"max_tokens,omitempty" jsonschema:"Maximum tokens in results (budget constraint)"`
	AfterTime  int64  `json:"after_time,omitempty" jsonschema:"Filter to results after this Unix timestamp (nanoseconds)"`
	BeforeTime int64  `json:"before_time,omitempty" jsonschema:"Filter to results before this Unix timestamp (nanoseconds)"`
}

type DrillDownArgs struct {
	ID         uint64 `json:"id" jsonschema:"Node ID to drill down from"`
	Filter     string `json:"filter,omitempty" jsonschema:"Text filter for children"`
	MaxResults int    `json:"max_results,omitempty" jsonschema:"Maximum children to return"`
}

type ZoomOutArgs struct {
	ID uint64 `json:"id" jsonschema:"Node ID to zoom out from"`
}

type GetSessionArgs struct {
	SessionID string `json:"session_id" jsonschema:"Session identifier"`
}

type GetNodeArgs struct {
	ID uint64 `json:"id" jsonschema:"Node ID to retrieve"`
}

type GetContextArgs struct {
	ID              uint64 `json:"id" jsonschema:"Node ID to get context for"`
	IncludeParent   bool   `json:"include_parent,omitempty" jsonschema:"Include parent node"`
	IncludeSiblings bool   `json:"include_siblings,omitempty" jsonschema:"Include sibling nodes"`
	IncludeChildren bool   `json:"include_children,omitempty" jsonschema:"Include child nodes"`
	MaxDepth        int    `json:"max_depth,omitempty" jsonschema:"Maximum depth for children (default 1)"`
}

func registerTools(server *mcp.Server, proxy *proxyClient) {
	// memory_query - semantic search
	mcp.AddTool(server, &mcp.Tool{
		Name:        "memory_query",
		Description: "Semantic search across past interactions. Returns relevant conversations and code.",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args QueryArgs) (*mcp.CallToolResult, any, error) {
		result, err := proxy.post("/query", args)
		if err != nil {
			return nil, nil, err
		}
		return formatQueryResult(result)
	})

	// memory_drill_down - navigate to children
	mcp.AddTool(server, &mcp.Tool{
		Name:        "memory_drill_down",
		Description: "Get children of a node to explore deeper into a conversation.",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args DrillDownArgs) (*mcp.CallToolResult, any, error) {
		result, err := proxy.post("/drill_down", args)
		if err != nil {
			return nil, nil, err
		}
		return formatDrillDownResult(result)
	})

	// memory_zoom_out - navigate to parent
	mcp.AddTool(server, &mcp.Tool{
		Name:        "memory_zoom_out",
		Description: "Get ancestor chain of a node to see broader context.",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args ZoomOutArgs) (*mcp.CallToolResult, any, error) {
		result, err := proxy.post("/zoom_out", args)
		if err != nil {
			return nil, nil, err
		}
		return formatZoomOutResult(result)
	})

	// memory_list_sessions - list all sessions
	mcp.AddTool(server, &mcp.Tool{
		Name:        "memory_list_sessions",
		Description: "List all conversation sessions in memory.",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args struct{}) (*mcp.CallToolResult, any, error) {
		result, err := proxy.get("/sessions")
		if err != nil {
			return nil, nil, err
		}
		return formatSessionsResult(result)
	})

	// memory_get_session - get session details
	mcp.AddTool(server, &mcp.Tool{
		Name:        "memory_get_session",
		Description: "Get details and metadata about a specific session.",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args GetSessionArgs) (*mcp.CallToolResult, any, error) {
		result, err := proxy.get("/sessions/" + args.SessionID)
		if err != nil {
			return nil, nil, err
		}
		return formatSessionResult(result)
	})

	// memory_get_node - get a specific node
	mcp.AddTool(server, &mcp.Tool{
		Name:        "memory_get_node",
		Description: "Get full content of a specific node by ID.",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args GetNodeArgs) (*mcp.CallToolResult, any, error) {
		result, err := proxy.get(fmt.Sprintf("/nodes/%d", args.ID))
		if err != nil {
			return nil, nil, err
		}
		return formatNodeResult(result)
	})

	// memory_get_context - get node with context expansion
	mcp.AddTool(server, &mcp.Tool{
		Name:        "memory_get_context",
		Description: "Get a node with optional context (parent, siblings, children). Use this to expand context around a search result.",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args GetContextArgs) (*mcp.CallToolResult, any, error) {
		result, err := proxy.post("/get_context", args)
		if err != nil {
			return nil, nil, err
		}
		return formatGetContextResult(result)
	})
}

// Result formatters

func formatQueryResult(result map[string]any) (*mcp.CallToolResult, any, error) {
	results, _ := result["results"].([]any)
	count := len(results)
	totalResults, _ := result["total_results"].(float64)
	truncated, _ := result["truncated"].(bool)
	tokensUsed, _ := result["tokens_used"].(float64)

	var text string
	if count == 0 {
		text = "No results found."
	} else {
		text = fmt.Sprintf("Found %d results", count)
		if totalResults > 0 && int(totalResults) > count {
			text += fmt.Sprintf(" (total: %.0f)", totalResults)
		}
		if truncated {
			text += " [truncated]"
		}
		if tokensUsed > 0 {
			text += fmt.Sprintf(" [%.0f tokens]", tokensUsed)
		}
		text += ":\n"

		for i, r := range results {
			rm, _ := r.(map[string]any)
			content, _ := rm["content"].(string)
			level, _ := rm["level"].(string)
			nodeID, _ := rm["node_id"].(float64)
			score, _ := rm["combined_score"].(float64)

			preview := content
			if len(preview) > 100 {
				preview = preview[:100] + "..."
			}
			text += fmt.Sprintf("\n%d. [%s] node_id=%.0f score=%.2f\n   %s",
				i+1, level, nodeID, score, preview)
		}
	}

	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: text}},
	}, result, nil
}

func formatDrillDownResult(result map[string]any) (*mcp.CallToolResult, any, error) {
	children, _ := result["children"].([]any)
	count := len(children)

	var text string
	if count == 0 {
		text = "No children found."
	} else {
		text = fmt.Sprintf("Found %d children:\n", count)
		for _, c := range children {
			cm, _ := c.(map[string]any)
			content, _ := cm["content"].(string)
			level, _ := cm["level"].(string)
			nodeID, _ := cm["id"].(float64)

			preview := content
			if len(preview) > 80 {
				preview = preview[:80] + "..."
			}
			text += fmt.Sprintf("\n- [%s] node_id=%.0f: %s", level, nodeID, preview)
		}
	}

	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: text}},
	}, result, nil
}

func formatZoomOutResult(result map[string]any) (*mcp.CallToolResult, any, error) {
	ancestors, _ := result["ancestors"].([]any)
	count := len(ancestors)

	var text string
	if count == 0 {
		text = "No ancestors found (root node)."
	} else {
		text = fmt.Sprintf("Ancestor chain (%d nodes):\n", count)
		for i, a := range ancestors {
			am, _ := a.(map[string]any)
			content, _ := am["content"].(string)
			level, _ := am["level"].(string)
			nodeID, _ := am["id"].(float64)

			preview := content
			if len(preview) > 60 {
				preview = preview[:60] + "..."
			}
			indent := ""
			for j := 0; j < i; j++ {
				indent += "  "
			}
			text += fmt.Sprintf("\n%s[%s] node_id=%.0f: %s", indent, level, nodeID, preview)
		}
	}

	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: text}},
	}, result, nil
}

func formatSessionsResult(result map[string]any) (*mcp.CallToolResult, any, error) {
	sessions, _ := result["sessions"].([]any)
	count := len(sessions)

	var text string
	if count == 0 {
		text = "No sessions found."
	} else {
		text = fmt.Sprintf("Found %d sessions:\n", count)
		for _, s := range sessions {
			sm, _ := s.(map[string]any)
			id, _ := sm["id"].(string)
			agent, _ := sm["agent_id"].(string)
			created, _ := sm["created_at"].(string)
			text += fmt.Sprintf("\n- %s (agent: %s, created: %s)", id, agent, created)
		}
	}

	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: text}},
	}, result, nil
}

func formatSessionResult(result map[string]any) (*mcp.CallToolResult, any, error) {
	id, _ := result["id"].(string)
	agent, _ := result["agent_id"].(string)
	created, _ := result["created_at"].(string)
	lastActive, _ := result["last_active_at"].(string)
	keywords, _ := result["keywords"].([]any)
	files, _ := result["files_touched"].([]any)

	text := fmt.Sprintf(`Session: %s
Agent: %s
Created: %s
Last Active: %s
Keywords: %v
Files: %v`, id, agent, created, lastActive, keywords, files)

	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: text}},
	}, result, nil
}

func formatNodeResult(result map[string]any) (*mcp.CallToolResult, any, error) {
	nodeID, _ := result["id"].(float64)
	level, _ := result["level"].(string)
	session, _ := result["session_id"].(string)
	created, _ := result["created_at"].(string)
	content, _ := result["content"].(string)

	text := fmt.Sprintf(`Node: %.0f
Level: %s
Session: %s
Created: %s

%s`, nodeID, level, session, created, content)

	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: text}},
	}, result, nil
}

func formatGetContextResult(result map[string]any) (*mcp.CallToolResult, any, error) {
	var text string

	// Format main node
	if node, ok := result["node"].(map[string]any); ok {
		nodeID, _ := node["id"].(float64)
		level, _ := node["level"].(string)
		content, _ := node["content"].(string)
		preview := content
		if len(preview) > 200 {
			preview = preview[:200] + "..."
		}
		text = fmt.Sprintf("Node: %.0f [%s]\n%s\n", nodeID, level, preview)
	}

	// Format parent
	if parent, ok := result["parent"].(map[string]any); ok {
		nodeID, _ := parent["id"].(float64)
		level, _ := parent["level"].(string)
		content, _ := parent["content"].(string)
		preview := content
		if len(preview) > 100 {
			preview = preview[:100] + "..."
		}
		text += fmt.Sprintf("\nParent: %.0f [%s]\n  %s\n", nodeID, level, preview)
	}

	// Format siblings
	if siblings, ok := result["siblings"].([]any); ok && len(siblings) > 0 {
		text += fmt.Sprintf("\nSiblings (%d):\n", len(siblings))
		for _, s := range siblings {
			sm, _ := s.(map[string]any)
			nodeID, _ := sm["id"].(float64)
			level, _ := sm["level"].(string)
			content, _ := sm["content"].(string)
			preview := content
			if len(preview) > 60 {
				preview = preview[:60] + "..."
			}
			text += fmt.Sprintf("  - %.0f [%s]: %s\n", nodeID, level, preview)
		}
	}

	// Format children
	if children, ok := result["children"].([]any); ok && len(children) > 0 {
		text += fmt.Sprintf("\nChildren (%d):\n", len(children))
		for _, c := range children {
			cm, _ := c.(map[string]any)
			nodeID, _ := cm["id"].(float64)
			level, _ := cm["level"].(string)
			content, _ := cm["content"].(string)
			preview := content
			if len(preview) > 60 {
				preview = preview[:60] + "..."
			}
			text += fmt.Sprintf("  - %.0f [%s]: %s\n", nodeID, level, preview)
		}
	}

	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: text}},
	}, result, nil
}
