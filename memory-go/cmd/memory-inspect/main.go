// Package main provides a CLI tool to inspect and navigate the memory hierarchy.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"
)

var httpClient = &http.Client{Timeout: 10 * time.Second}

func main() {
	serverURL := flag.String("url", "http://localhost:8080", "Memory server URL")
	flag.StringVar(serverURL, "u", "http://localhost:8080", "Memory server URL (shorthand)")

	// Commands
	listSessions := flag.Bool("sessions", false, "List all sessions")
	sessionID := flag.String("session", "", "Show session tree")
	nodeID := flag.Uint64("node", 0, "Show node and its context")
	query := flag.String("query", "", "Search for content")

	// Options
	depth := flag.Int("depth", 3, "Tree depth to show")
	jsonOutput := flag.Bool("json", false, "Output as JSON")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `Memory Inspector - Navigate the memory hierarchy

Usage: memory-inspect [OPTIONS] COMMAND

Commands:
  --sessions              List all sessions
  --session ID            Show session with message tree
  --node ID               Show node with ancestors and children
  --query "text"          Search for content

Options:
  -u, --url URL           Server URL (default: http://localhost:8080)
  --depth N               Tree depth (default: 3)
  --json                  Output as JSON

Examples:
  memory-inspect --sessions
  memory-inspect --session abc123
  memory-inspect --node 42 --depth 5
  memory-inspect --query "authentication"
`)
	}

	flag.Parse()

	if !*listSessions && *sessionID == "" && *nodeID == 0 && *query == "" {
		flag.Usage()
		os.Exit(1)
	}

	client := &apiClient{baseURL: *serverURL}

	if *listSessions {
		client.listSessions(*jsonOutput)
	}
	if *sessionID != "" {
		client.showSessionTree(*sessionID, *depth, *jsonOutput)
	}
	if *nodeID != 0 {
		client.showNodeContext(*nodeID, *depth, *jsonOutput)
	}
	if *query != "" {
		client.search(*query, *jsonOutput)
	}
}

type apiClient struct {
	baseURL string
}

func (c *apiClient) listSessions(asJSON bool) {
	data, err := c.get("/sessions")
	if err != nil {
		fatal("Error: %v", err)
	}

	if asJSON {
		fmt.Println(string(data))
		return
	}

	var result struct {
		Sessions []Session `json:"sessions"`
	}
	json.Unmarshal(data, &result)

	if len(result.Sessions) == 0 {
		fmt.Println("No sessions found.")
		return
	}

	fmt.Printf("Sessions (%d):\n\n", len(result.Sessions))
	for _, s := range result.Sessions {
		fmt.Printf("  %s\n", s.ID)
		fmt.Printf("    Agent: %s\n", s.AgentID)
		fmt.Printf("    Created: %s\n", s.CreatedAt.Format("2006-01-02 15:04:05"))
		if len(s.Keywords) > 0 {
			fmt.Printf("    Keywords: %s\n", strings.Join(s.Keywords, ", "))
		}
		fmt.Println()
	}
}

func (c *apiClient) showSessionTree(sessionID string, depth int, asJSON bool) {
	// Get session info
	sessData, err := c.get("/sessions/" + sessionID)
	if err != nil {
		fatal("Session not found: %v", err)
	}

	var sess Session
	json.Unmarshal(sessData, &sess)

	if asJSON {
		// Get all children recursively
		tree := c.buildTree(sess.RootNodeID, depth)
		result := map[string]any{"session": sess, "tree": tree}
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		enc.Encode(result)
		return
	}

	// Print session header
	fmt.Printf("Session: %s\n", sess.ID)
	fmt.Printf("Agent: %s\n", sess.AgentID)
	fmt.Printf("Created: %s\n", sess.CreatedAt.Format("2006-01-02 15:04:05"))
	if len(sess.Keywords) > 0 {
		fmt.Printf("Keywords: %s\n", strings.Join(sess.Keywords, ", "))
	}
	if len(sess.FilesTouched) > 0 {
		fmt.Printf("Files: %s\n", strings.Join(sess.FilesTouched, ", "))
	}
	fmt.Println()
	fmt.Println("Messages:")
	fmt.Println(strings.Repeat("─", 60))

	// Get children of root (messages)
	c.printTree(sess.RootNodeID, depth, 0, "")
}

func (c *apiClient) showNodeContext(nodeID uint64, depth int, asJSON bool) {
	// Get the node
	nodeData, err := c.get(fmt.Sprintf("/nodes/%d", nodeID))
	if err != nil {
		fatal("Node not found: %v", err)
	}

	var node Node
	json.Unmarshal(nodeData, &node)

	// Get ancestors
	ancestorData, _ := c.post("/zoom_out", map[string]uint64{"id": nodeID})
	var ancestorResult struct {
		Ancestors []Node `json:"ancestors"`
	}
	json.Unmarshal(ancestorData, &ancestorResult)

	if asJSON {
		children := c.buildTree(nodeID, depth)
		result := map[string]any{
			"node":      node,
			"ancestors": ancestorResult.Ancestors,
			"children":  children,
		}
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		enc.Encode(result)
		return
	}

	// Print breadcrumb (ancestors)
	if len(ancestorResult.Ancestors) > 0 {
		fmt.Println("Path:")
		for i, a := range ancestorResult.Ancestors {
			indent := strings.Repeat("  ", i)
			preview := truncate(a.Content, 50)
			fmt.Printf("%s└─ [%s #%d] %s\n", indent, a.Level, a.ID, preview)
		}
		fmt.Println()
	}

	// Print current node
	fmt.Println(strings.Repeat("═", 60))
	fmt.Printf("[%s #%d] %s\n", node.Level, node.ID, node.SessionID)
	fmt.Println(strings.Repeat("─", 60))
	fmt.Println(node.Content)
	fmt.Println(strings.Repeat("═", 60))

	// Print children tree
	children := c.getChildren(nodeID)
	if len(children) > 0 {
		fmt.Println("\nChildren:")
		c.printTree(nodeID, depth, 0, "")
	}
}

func (c *apiClient) search(query string, asJSON bool) {
	data, err := c.post("/query", map[string]any{
		"query":       query,
		"max_results": 20,
	})
	if err != nil {
		fatal("Search failed: %v", err)
	}

	if asJSON {
		fmt.Println(string(data))
		return
	}

	var result struct {
		Results []SearchResult `json:"results"`
	}
	json.Unmarshal(data, &result)

	if len(result.Results) == 0 {
		fmt.Println("No results found.")
		return
	}

	fmt.Printf("Found %d results for \"%s\":\n\n", len(result.Results), query)
	for i, r := range result.Results {
		preview := truncate(r.Content, 100)
		fmt.Printf("%d. [%s #%d] score=%.2f\n", i+1, r.Level, r.NodeID, r.CombinedScore)
		fmt.Printf("   Session: %s\n", r.SessionID)
		fmt.Printf("   %s\n\n", preview)
	}
}

func (c *apiClient) printTree(parentID uint64, maxDepth, currentDepth int, prefix string) {
	if currentDepth >= maxDepth {
		return
	}

	children := c.getChildren(parentID)
	for i, child := range children {
		isLast := i == len(children)-1

		// Choose connector
		connector := "├─"
		if isLast {
			connector = "└─"
		}

		// Format level badge
		levelBadge := levelIcon(child.Level)

		// Print node
		preview := truncate(child.Content, 60-currentDepth*2)
		fmt.Printf("%s%s %s [#%d] %s\n", prefix, connector, levelBadge, child.ID, preview)

		// Recurse with updated prefix
		newPrefix := prefix
		if isLast {
			newPrefix += "   "
		} else {
			newPrefix += "│  "
		}
		c.printTree(child.ID, maxDepth, currentDepth+1, newPrefix)
	}
}

func (c *apiClient) buildTree(parentID uint64, maxDepth int) []map[string]any {
	if maxDepth <= 0 {
		return nil
	}

	children := c.getChildren(parentID)
	result := make([]map[string]any, 0, len(children))

	for _, child := range children {
		node := map[string]any{
			"id":       child.ID,
			"level":    child.Level,
			"content":  child.Content,
			"children": c.buildTree(child.ID, maxDepth-1),
		}
		result = append(result, node)
	}

	return result
}

func (c *apiClient) getChildren(parentID uint64) []Node {
	data, err := c.post("/drill_down", map[string]any{
		"id":          parentID,
		"max_results": 100,
	})
	if err != nil {
		return nil
	}

	var result struct {
		Children []Node `json:"children"`
	}
	json.Unmarshal(data, &result)
	return result.Children
}

func (c *apiClient) get(path string) ([]byte, error) {
	resp, err := httpClient.Get(c.baseURL + path)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	data, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 400 {
		return nil, fmt.Errorf("HTTP %d: %s", resp.StatusCode, string(data))
	}
	return data, nil
}

func (c *apiClient) post(path string, body any) ([]byte, error) {
	bodyData, _ := json.Marshal(body)
	resp, err := httpClient.Post(c.baseURL+path, "application/json", strings.NewReader(string(bodyData)))
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	data, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 400 {
		return nil, fmt.Errorf("HTTP %d: %s", resp.StatusCode, string(data))
	}
	return data, nil
}

// Types

type Session struct {
	ID           string    `json:"id"`
	AgentID      string    `json:"agent_id"`
	RootNodeID   uint64    `json:"root_node_id"`
	CreatedAt    time.Time `json:"created_at"`
	LastActiveAt time.Time `json:"last_active_at"`
	Keywords     []string  `json:"keywords"`
	Identifiers  []string  `json:"identifiers"`
	FilesTouched []string  `json:"files_touched"`
}

type Node struct {
	ID        uint64 `json:"id"`
	Level     string `json:"level"`
	ParentID  uint64 `json:"parent_id"`
	SessionID string `json:"session_id"`
	Content   string `json:"content"`
}

type SearchResult struct {
	NodeID        uint64  `json:"node_id"`
	Level         string  `json:"level"`
	SessionID     string  `json:"session_id"`
	Content       string  `json:"content"`
	CombinedScore float64 `json:"combined_score"`
}

// Helpers

func truncate(s string, max int) string {
	// Remove newlines for preview
	s = strings.ReplaceAll(s, "\n", " ")
	s = strings.ReplaceAll(s, "\r", "")
	// Collapse multiple spaces
	for strings.Contains(s, "  ") {
		s = strings.ReplaceAll(s, "  ", " ")
	}
	s = strings.TrimSpace(s)

	if len(s) <= max {
		return s
	}
	return s[:max-3] + "..."
}

func levelIcon(level string) string {
	switch level {
	case "session":
		return "[SESSION]"
	case "message":
		return "[MSG]"
	case "block":
		return "[BLK]"
	case "statement":
		return "[STM]"
	default:
		return "[" + strings.ToUpper(level) + "]"
	}
}

func fatal(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}
