package search

import (
	"math"
	"testing"

	"github.com/anthropics/memory-go/pkg/types"
)

// Helper: create a unit vector along dimension d
func unitVector(dim int) types.Embedding {
	vec := make(types.Embedding, types.EmbeddingDim)
	if dim < types.EmbeddingDim {
		vec[dim] = 1.0
	}
	return vec
}

// Helper: create a random-ish deterministic vector
func randomVector(seed int) types.Embedding {
	vec := make(types.Embedding, types.EmbeddingDim)
	var mag float32
	for i := 0; i < types.EmbeddingDim; i++ {
		// Simple deterministic "random" based on seed and index
		val := float32(((seed*31+i*17)%1000)-500) / 1000.0
		vec[i] = val
		mag += val * val
	}
	// Normalize
	mag = float32(math.Sqrt(float64(mag)))
	if mag > 0 {
		for i := range vec {
			vec[i] /= mag
		}
	}
	return vec
}

func TestVectorIndex_CreateDestroy(t *testing.T) {
	config := types.SearchConfig{
		HNSWM:           16,
		HNSWEfConstruct: 200,
		HNSWEfSearch:    50,
	}

	index := NewVectorIndex(config)
	if index == nil {
		t.Fatal("NewVectorIndex() returned nil")
	}

	if index.TotalSize() != 0 {
		t.Errorf("TotalSize() = %d, want 0", index.TotalSize())
	}
}

func TestVectorIndex_AddSingle(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	vec := randomVector(42)
	err := index.Add(types.LevelMessage, 100, vec)
	if err != nil {
		t.Fatalf("Add() error = %v", err)
	}

	if index.Size(types.LevelMessage) != 1 {
		t.Errorf("Size(LevelMessage) = %d, want 1", index.Size(types.LevelMessage))
	}
}

func TestVectorIndex_AddMultiple(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	for i := 0; i < 50; i++ {
		vec := randomVector(i)
		err := index.Add(types.LevelMessage, types.NodeID(i), vec)
		if err != nil {
			t.Fatalf("Add(%d) error = %v", i, err)
		}
	}

	if index.Size(types.LevelMessage) != 50 {
		t.Errorf("Size() = %d, want 50", index.Size(types.LevelMessage))
	}
}

func TestVectorIndex_SearchBasic(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	// Add orthogonal vectors
	for i := 0; i < 10; i++ {
		vec := unitVector(i)
		err := index.Add(types.LevelMessage, types.NodeID(i), vec)
		if err != nil {
			t.Fatalf("Add(%d) error = %v", i, err)
		}
	}

	// Search for vector along dimension 5
	query := unitVector(5)
	results, err := index.Search(types.LevelMessage, query, 5)
	if err != nil {
		t.Fatalf("Search() error = %v", err)
	}

	if len(results) == 0 {
		t.Fatal("Search returned no results")
	}

	// First result should be exact match (id=5)
	if results[0].NodeID != 5 {
		t.Errorf("First result NodeID = %d, want 5", results[0].NodeID)
	}

	// Distance should be close to 0
	if results[0].Distance > 0.1 {
		t.Errorf("First result distance = %f, want ~0", results[0].Distance)
	}
}

func TestVectorIndex_SearchSorted(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	// Add 20 random vectors
	for i := 0; i < 20; i++ {
		vec := randomVector(i)
		index.Add(types.LevelMessage, types.NodeID(i), vec)
	}

	// Search
	query := randomVector(100)
	results, err := index.Search(types.LevelMessage, query, 10)
	if err != nil {
		t.Fatalf("Search() error = %v", err)
	}

	if len(results) <= 1 {
		t.Skip("Not enough results to verify sorting")
	}

	// Verify sorted by distance
	for i := 1; i < len(results); i++ {
		if results[i-1].Distance > results[i].Distance+0.001 {
			t.Errorf("Results not sorted: [%d].Distance=%f > [%d].Distance=%f",
				i-1, results[i-1].Distance, i, results[i].Distance)
		}
	}
}

func TestVectorIndex_SearchEmpty(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	query := randomVector(42)
	results, err := index.Search(types.LevelMessage, query, 5)
	if err != nil {
		t.Fatalf("Search() error = %v", err)
	}

	if len(results) != 0 {
		t.Errorf("Search on empty index returned %d results, want 0", len(results))
	}
}

func TestVectorIndex_Remove(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	vec := randomVector(42)
	index.Add(types.LevelMessage, 100, vec)

	if index.Size(types.LevelMessage) != 1 {
		t.Fatalf("Size() = %d, want 1", index.Size(types.LevelMessage))
	}

	err := index.Remove(types.LevelMessage, 100)
	if err != nil {
		t.Fatalf("Remove() error = %v", err)
	}

	if index.Size(types.LevelMessage) != 0 {
		t.Errorf("Size() after remove = %d, want 0", index.Size(types.LevelMessage))
	}
}

func TestVectorIndex_SearchAfterRemove(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	// Add vectors along first 3 dimensions
	index.Add(types.LevelMessage, 0, unitVector(0))
	index.Add(types.LevelMessage, 1, unitVector(1))
	index.Add(types.LevelMessage, 2, unitVector(2))

	// Remove id=1
	index.Remove(types.LevelMessage, 1)

	// Search for vector along dimension 1
	query := unitVector(1)
	results, err := index.Search(types.LevelMessage, query, 3)
	if err != nil {
		t.Fatalf("Search() error = %v", err)
	}

	// id=1 should not be in results
	for _, r := range results {
		if r.NodeID == 1 {
			t.Error("Removed node should not appear in search results")
		}
	}
}

func TestVectorIndex_SearchMultiLevel(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	// Add to different levels
	index.Add(types.LevelStatement, 1, randomVector(1))
	index.Add(types.LevelBlock, 2, randomVector(2))
	index.Add(types.LevelMessage, 3, randomVector(3))

	query := randomVector(1) // Similar to node 1
	results, err := index.SearchMultiLevel(query, types.LevelMessage, types.LevelStatement, 10)
	if err != nil {
		t.Fatalf("SearchMultiLevel() error = %v", err)
	}

	if len(results) != 3 {
		t.Errorf("SearchMultiLevel returned %d results, want 3", len(results))
	}
}

func TestVectorIndex_InvalidLevel(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	vec := randomVector(1)
	err := index.Add(types.HierarchyLevel(99), 1, vec)
	if err == nil {
		t.Error("Add with invalid level should return error")
	}

	_, err = index.Search(types.HierarchyLevel(99), vec, 5)
	if err == nil {
		t.Error("Search with invalid level should return error")
	}
}

func TestVectorIndex_Clear(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	for i := 0; i < 10; i++ {
		index.Add(types.LevelMessage, types.NodeID(i), randomVector(i))
	}

	index.Clear()

	if index.TotalSize() != 0 {
		t.Errorf("TotalSize() after clear = %d, want 0", index.TotalSize())
	}
}

func TestVectorIndex_Stats(t *testing.T) {
	index := NewVectorIndex(types.SearchConfig{})

	index.Add(types.LevelStatement, 1, randomVector(1))
	index.Add(types.LevelStatement, 2, randomVector(2))
	index.Add(types.LevelMessage, 3, randomVector(3))

	stats := index.Stats()

	if stats["total_vectors"] != 3 {
		t.Errorf("stats[total_vectors] = %v, want 3", stats["total_vectors"])
	}
}

func TestEuclideanDistance(t *testing.T) {
	a := []float32{0, 0, 0}
	b := []float32{3, 4, 0}

	dist := euclideanDistance(a, b)
	expected := float32(5.0) // 3-4-5 triangle

	if math.Abs(float64(dist-expected)) > 0.1 {
		t.Errorf("euclideanDistance() = %f, want %f", dist, expected)
	}
}

func BenchmarkVectorIndex_Add(b *testing.B) {
	index := NewVectorIndex(types.SearchConfig{})
	vec := randomVector(42)

	for i := 0; i < b.N; i++ {
		index.Add(types.LevelMessage, types.NodeID(i), vec)
	}
}

func BenchmarkVectorIndex_Search(b *testing.B) {
	index := NewVectorIndex(types.SearchConfig{})

	// Add 1000 vectors
	for i := 0; i < 1000; i++ {
		index.Add(types.LevelMessage, types.NodeID(i), randomVector(i))
	}

	query := randomVector(9999)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		index.Search(types.LevelMessage, query, 10)
	}
}
