// Package core provides the hierarchy manager for the memory tree structure.
package core

import (
	"sync"
	"time"

	"github.com/anthropics/memory-go/internal/storage"
	"github.com/anthropics/memory-go/pkg/types"
)

// HierarchyManager manages the hierarchical node structure.
type HierarchyManager struct {
	store *storage.Store
	mu    sync.RWMutex

	// In-memory caches for fast traversal
	children map[types.NodeID][]types.NodeID // parent -> children
	parents  map[types.NodeID]types.NodeID   // child -> parent
}

// NewHierarchyManager creates a new hierarchy manager.
func NewHierarchyManager(store *storage.Store) (*HierarchyManager, error) {
	hm := &HierarchyManager{
		store:    store,
		children: make(map[types.NodeID][]types.NodeID),
		parents:  make(map[types.NodeID]types.NodeID),
	}

	// Build in-memory caches from storage
	if err := hm.rebuildCaches(); err != nil {
		return nil, err
	}

	return hm, nil
}

// rebuildCaches reconstructs the in-memory relationship caches from storage.
func (hm *HierarchyManager) rebuildCaches() error {
	hm.mu.Lock()
	defer hm.mu.Unlock()

	hm.children = make(map[types.NodeID][]types.NodeID)
	hm.parents = make(map[types.NodeID]types.NodeID)

	return hm.store.IterateNodes(func(node *types.Node) error {
		if node.ParentID != 0 {
			hm.parents[node.ID] = node.ParentID
			hm.children[node.ParentID] = append(hm.children[node.ParentID], node.ID)
		}
		return nil
	})
}

// CreateNode creates a new node in the hierarchy.
func (hm *HierarchyManager) CreateNode(req *types.StoreRequest) (*types.Node, error) {
	hm.mu.Lock()
	defer hm.mu.Unlock()

	// Validate level based on parent
	level := req.Level
	if req.ParentID != 0 {
		parent, err := hm.store.GetNode(req.ParentID)
		if err != nil {
			return nil, types.WrapError("hierarchy.CreateNode", types.ErrNotFound, err)
		}
		// Child must be at a lower level than parent
		if level >= parent.Level {
			level = parent.Level - 1
		}
		if level < types.LevelStatement {
			return nil, types.Errorf("hierarchy.CreateNode", types.ErrInvalidLevel,
				"cannot create child below statement level")
		}
	}

	node := &types.Node{
		ID:          hm.store.NextNodeID(),
		Level:       level,
		ParentID:    req.ParentID,
		AgentID:     req.AgentID,
		SessionID:   req.SessionID,
		Content:     req.Content,
		Role:        req.Role,
		CreatedAt:   time.Now(),
		SequenceNum: hm.store.NextSequence(),
	}

	// Persist the node
	if err := hm.store.SaveNode(node); err != nil {
		return nil, err
	}

	// Update parent's first child if needed
	if req.ParentID != 0 {
		hm.parents[node.ID] = req.ParentID
		hm.children[req.ParentID] = append(hm.children[req.ParentID], node.ID)

		// Update parent node's first child pointer if this is the first child
		parent, _ := hm.store.GetNode(req.ParentID)
		if parent != nil && parent.FirstChildID == 0 {
			parent.FirstChildID = node.ID
			hm.store.SaveNode(parent)
		}
	}

	return node, nil
}

// GetNode retrieves a node by ID.
func (hm *HierarchyManager) GetNode(id types.NodeID) (*types.Node, error) {
	return hm.store.GetNode(id)
}

// UpdateNode updates an existing node.
func (hm *HierarchyManager) UpdateNode(node *types.Node) error {
	hm.mu.Lock()
	defer hm.mu.Unlock()

	existing, err := hm.store.GetNode(node.ID)
	if err != nil {
		return err
	}

	// Don't allow changing parent (would require re-linking)
	if node.ParentID != existing.ParentID {
		return types.Errorf("hierarchy.UpdateNode", types.ErrInvalidArg,
			"cannot change parent ID")
	}

	return hm.store.SaveNode(node)
}

// DeleteNode removes a node and all its descendants.
func (hm *HierarchyManager) DeleteNode(id types.NodeID) error {
	hm.mu.Lock()
	defer hm.mu.Unlock()

	// Get all descendant IDs first
	toDelete := hm.collectDescendants(id)
	toDelete = append([]types.NodeID{id}, toDelete...)

	// Delete all nodes
	for _, nodeID := range toDelete {
		if err := hm.store.DeleteNode(nodeID); err != nil {
			return err
		}

		// Clean up caches
		parentID := hm.parents[nodeID]
		delete(hm.parents, nodeID)
		delete(hm.children, nodeID)

		// Remove from parent's children list
		if parentID != 0 {
			children := hm.children[parentID]
			for i, childID := range children {
				if childID == nodeID {
					hm.children[parentID] = append(children[:i], children[i+1:]...)
					break
				}
			}
		}
	}

	return nil
}

// collectDescendants returns all descendant node IDs recursively.
func (hm *HierarchyManager) collectDescendants(parentID types.NodeID) []types.NodeID {
	var descendants []types.NodeID

	for _, childID := range hm.children[parentID] {
		descendants = append(descendants, childID)
		descendants = append(descendants, hm.collectDescendants(childID)...)
	}

	return descendants
}

// GetChildren returns all children of a node.
func (hm *HierarchyManager) GetChildren(parentID types.NodeID) ([]*types.Node, error) {
	hm.mu.RLock()
	childIDs := hm.children[parentID]
	hm.mu.RUnlock()

	children := make([]*types.Node, 0, len(childIDs))
	for _, id := range childIDs {
		node, err := hm.store.GetNode(id)
		if err != nil {
			continue // Skip missing nodes
		}
		children = append(children, node)
	}

	return children, nil
}

// GetParent returns the parent of a node.
func (hm *HierarchyManager) GetParent(id types.NodeID) (*types.Node, error) {
	hm.mu.RLock()
	parentID, exists := hm.parents[id]
	hm.mu.RUnlock()

	if !exists || parentID == 0 {
		return nil, types.ErrNotFound
	}

	return hm.store.GetNode(parentID)
}

// GetAncestors returns all ancestors from root to the node (inclusive).
func (hm *HierarchyManager) GetAncestors(id types.NodeID) ([]*types.Node, error) {
	var ancestors []*types.Node

	currentID := id
	for currentID != 0 {
		node, err := hm.store.GetNode(currentID)
		if err != nil {
			break
		}
		ancestors = append([]*types.Node{node}, ancestors...) // Prepend to get root-first order

		hm.mu.RLock()
		currentID = hm.parents[currentID]
		hm.mu.RUnlock()
	}

	return ancestors, nil
}

// GetSiblings returns all siblings of a node (including the node itself).
func (hm *HierarchyManager) GetSiblings(id types.NodeID) ([]*types.Node, error) {
	hm.mu.RLock()
	parentID := hm.parents[id]
	hm.mu.RUnlock()

	if parentID == 0 {
		// Root node, return just itself
		node, err := hm.store.GetNode(id)
		if err != nil {
			return nil, err
		}
		return []*types.Node{node}, nil
	}

	return hm.GetChildren(parentID)
}

// GetDescendants returns all descendants of a node.
func (hm *HierarchyManager) GetDescendants(id types.NodeID) ([]*types.Node, error) {
	hm.mu.RLock()
	descendantIDs := hm.collectDescendants(id)
	hm.mu.RUnlock()

	descendants := make([]*types.Node, 0, len(descendantIDs))
	for _, descID := range descendantIDs {
		node, err := hm.store.GetNode(descID)
		if err != nil {
			continue
		}
		descendants = append(descendants, node)
	}

	return descendants, nil
}

// GetNodesAtLevel returns all nodes at a specific hierarchy level.
func (hm *HierarchyManager) GetNodesAtLevel(level types.HierarchyLevel) ([]*types.Node, error) {
	return hm.store.GetNodesByLevel(level)
}

// GetSessionNodes returns all nodes in a session.
func (hm *HierarchyManager) GetSessionNodes(sessionID string) ([]*types.Node, error) {
	return hm.store.GetNodesBySession(sessionID, 0) // 0 = no limit
}

// GetSubtree returns a node and all its descendants.
func (hm *HierarchyManager) GetSubtree(rootID types.NodeID) ([]*types.Node, error) {
	root, err := hm.store.GetNode(rootID)
	if err != nil {
		return nil, err
	}

	descendants, err := hm.GetDescendants(rootID)
	if err != nil {
		return nil, err
	}

	return append([]*types.Node{root}, descendants...), nil
}

// FindNodesByContent searches for nodes containing the given text.
func (hm *HierarchyManager) FindNodesByContent(query string, level types.HierarchyLevel, maxResults int) ([]*types.Node, error) {
	var results []*types.Node

	err := hm.store.IterateNodes(func(node *types.Node) error {
		if len(results) >= maxResults {
			return nil
		}
		if level != 255 && node.Level != level { // 255 means any level
			return nil
		}
		// Simple substring match - real implementation would use inverted index
		if containsIgnoreCase(node.Content, query) {
			results = append(results, node)
		}
		return nil
	})

	return results, err
}

// containsIgnoreCase checks if s contains substr (case-insensitive).
func containsIgnoreCase(s, substr string) bool {
	if len(substr) == 0 {
		return true
	}
	if len(s) < len(substr) {
		return false
	}

	// Simple implementation - could be optimized
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

// Stats returns hierarchy statistics.
func (hm *HierarchyManager) Stats() map[string]interface{} {
	hm.mu.RLock()
	defer hm.mu.RUnlock()

	return map[string]interface{}{
		"total_relationships": len(hm.parents),
		"parents_with_children": len(hm.children),
	}
}
