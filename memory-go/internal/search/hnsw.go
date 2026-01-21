// Package search provides semantic and keyword search capabilities.
package search

import (
	"sync"

	"github.com/anthropics/memory-go/pkg/types"
	"github.com/coder/hnsw"
)

// VectorIndex provides approximate nearest neighbor search using HNSW.
type VectorIndex struct {
	// Per-level HNSW graphs
	graphs map[types.HierarchyLevel]*hnsw.Graph[types.NodeID]
	// Per-level vector storage (for distance calculation)
	vectors map[types.HierarchyLevel]map[types.NodeID]types.Embedding
	config  types.SearchConfig
	mu      sync.RWMutex
}

// NewVectorIndex creates a new HNSW-based vector index.
func NewVectorIndex(config types.SearchConfig) *VectorIndex {
	vi := &VectorIndex{
		graphs:  make(map[types.HierarchyLevel]*hnsw.Graph[types.NodeID]),
		vectors: make(map[types.HierarchyLevel]map[types.NodeID]types.Embedding),
		config:  config,
	}

	// Initialize graphs for each hierarchy level
	for level := types.LevelStatement; level <= types.LevelAgent; level++ {
		vi.graphs[level] = hnsw.NewGraph[types.NodeID]()
		vi.vectors[level] = make(map[types.NodeID]types.Embedding)
	}

	return vi
}

// Add adds a vector to the index at the specified level.
func (vi *VectorIndex) Add(level types.HierarchyLevel, id types.NodeID, embedding types.Embedding) error {
	vi.mu.Lock()
	defer vi.mu.Unlock()

	graph, ok := vi.graphs[level]
	if !ok {
		return types.Errorf("search.Add", types.ErrInvalidLevel, "invalid level: %d", level)
	}

	// Store the vector for later distance calculations
	vi.vectors[level][id] = embedding

	// Add to HNSW graph
	node := hnsw.MakeNode(id, embedding)
	graph.Add(node)
	return nil
}

// Remove removes a vector from the index.
func (vi *VectorIndex) Remove(level types.HierarchyLevel, id types.NodeID) error {
	vi.mu.Lock()
	defer vi.mu.Unlock()

	graph, ok := vi.graphs[level]
	if !ok {
		return types.Errorf("search.Remove", types.ErrInvalidLevel, "invalid level: %d", level)
	}

	delete(vi.vectors[level], id)
	graph.Delete(id)
	return nil
}

// Search finds the k nearest neighbors to the query vector.
func (vi *VectorIndex) Search(level types.HierarchyLevel, query types.Embedding, k int) ([]SearchMatch, error) {
	vi.mu.RLock()
	defer vi.mu.RUnlock()

	graph, ok := vi.graphs[level]
	if !ok {
		return nil, types.Errorf("search.Search", types.ErrInvalidLevel, "invalid level: %d", level)
	}

	vectors := vi.vectors[level]

	// Search the graph - returns []Node[K] (just the keys)
	neighbors := graph.Search(query, k)

	// Convert to SearchMatch results with distances
	results := make([]SearchMatch, 0, len(neighbors))
	for _, n := range neighbors {
		nodeID := n.Key
		if vec, exists := vectors[nodeID]; exists {
			dist := euclideanDistance(query, vec)
			results = append(results, SearchMatch{
				NodeID:   nodeID,
				Distance: dist,
			})
		}
	}

	// Sort by distance (should already be sorted, but ensure it)
	sortByDistance(results)

	return results, nil
}

// euclideanDistance computes the Euclidean distance between two vectors.
func euclideanDistance(a, b []float32) float32 {
	if len(a) != len(b) {
		return 1e10
	}
	var sum float32
	for i := range a {
		diff := a[i] - b[i]
		sum += diff * diff
	}
	return sqrt32(sum)
}

// sqrt32 computes square root for float32 using Newton's method.
func sqrt32(x float32) float32 {
	if x <= 0 {
		return 0
	}
	z := x
	for i := 0; i < 10; i++ {
		z = (z + x/z) / 2
	}
	return z
}

// SearchMultiLevel searches across multiple hierarchy levels.
func (vi *VectorIndex) SearchMultiLevel(query types.Embedding, topLevel, bottomLevel types.HierarchyLevel, k int) ([]SearchMatch, error) {
	vi.mu.RLock()
	defer vi.mu.RUnlock()

	var allResults []SearchMatch

	for level := bottomLevel; level <= topLevel; level++ {
		graph, ok := vi.graphs[level]
		if !ok {
			continue
		}

		vectors := vi.vectors[level]
		neighbors := graph.Search(query, k)

		for _, n := range neighbors {
			nodeID := n.Key
			if vec, exists := vectors[nodeID]; exists {
				dist := euclideanDistance(query, vec)
				allResults = append(allResults, SearchMatch{
					NodeID:   nodeID,
					Level:    level,
					Distance: dist,
				})
			}
		}
	}

	// Sort by distance and take top k
	sortByDistance(allResults)
	if len(allResults) > k {
		allResults = allResults[:k]
	}

	return allResults, nil
}

// SearchMatch represents a search result from the vector index.
type SearchMatch struct {
	NodeID   types.NodeID
	Level    types.HierarchyLevel
	Distance float32
}

// sortByDistance sorts matches by distance (ascending).
func sortByDistance(matches []SearchMatch) {
	// Simple insertion sort for small slices
	for i := 1; i < len(matches); i++ {
		j := i
		for j > 0 && matches[j].Distance < matches[j-1].Distance {
			matches[j], matches[j-1] = matches[j-1], matches[j]
			j--
		}
	}
}

// Size returns the number of vectors in a level's index.
func (vi *VectorIndex) Size(level types.HierarchyLevel) int {
	vi.mu.RLock()
	defer vi.mu.RUnlock()

	graph, ok := vi.graphs[level]
	if !ok {
		return 0
	}

	return graph.Len()
}

// TotalSize returns the total number of vectors across all levels.
func (vi *VectorIndex) TotalSize() int {
	vi.mu.RLock()
	defer vi.mu.RUnlock()

	total := 0
	for _, graph := range vi.graphs {
		total += graph.Len()
	}
	return total
}

// Stats returns index statistics.
func (vi *VectorIndex) Stats() map[string]interface{} {
	vi.mu.RLock()
	defer vi.mu.RUnlock()

	stats := map[string]interface{}{
		"total_vectors": vi.TotalSize(),
	}

	for level := types.LevelStatement; level <= types.LevelAgent; level++ {
		if graph, ok := vi.graphs[level]; ok {
			stats[level.String()+"_count"] = graph.Len()
		}
	}

	return stats
}

// Clear removes all vectors from all indices.
func (vi *VectorIndex) Clear() {
	vi.mu.Lock()
	defer vi.mu.Unlock()

	for level := types.LevelStatement; level <= types.LevelAgent; level++ {
		vi.graphs[level] = hnsw.NewGraph[types.NodeID]()
		vi.vectors[level] = make(map[types.NodeID]types.Embedding)
	}
}
