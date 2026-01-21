package search

import (
	"testing"

	"github.com/anthropics/memory-go/pkg/types"
)

func TestInvertedIndex_CreateDestroy(t *testing.T) {
	index := NewInvertedIndex()
	if index == nil {
		t.Fatal("NewInvertedIndex() returned nil")
	}

	if index.NodeCount() != 0 {
		t.Errorf("NodeCount() = %d, want 0", index.NodeCount())
	}
	if index.Size() != 0 {
		t.Errorf("Size() = %d, want 0", index.Size())
	}
}

func TestInvertedIndex_AddSingle(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(100, "hello world test")

	if index.NodeCount() != 1 {
		t.Errorf("NodeCount() = %d, want 1", index.NodeCount())
	}
	if index.Size() != 3 {
		t.Errorf("Size() = %d, want 3", index.Size())
	}
}

func TestInvertedIndex_AddMultiple(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(1, "hello world")
	index.Add(2, "hello everyone")
	index.Add(3, "goodbye world")

	if index.NodeCount() != 3 {
		t.Errorf("NodeCount() = %d, want 3", index.NodeCount())
	}
	// Tokens: hello, world, everyone, goodbye = 4
	if index.Size() != 4 {
		t.Errorf("Size() = %d, want 4", index.Size())
	}
}

func TestInvertedIndex_SearchAND(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(1, "hello world test")
	index.Add(2, "hello everyone test")
	index.Add(3, "goodbye world test")

	// Search for "hello world" - should find doc1 only
	results := index.SearchAND("hello world")

	if len(results) != 1 {
		t.Fatalf("SearchAND() returned %d results, want 1", len(results))
	}
	if results[0] != 1 {
		t.Errorf("result = %d, want 1", results[0])
	}

	// Search for "test" - should find all 3
	results = index.SearchAND("test")
	if len(results) != 3 {
		t.Errorf("SearchAND('test') returned %d results, want 3", len(results))
	}
}

func TestInvertedIndex_SearchOR(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(1, "hello")
	index.Add(2, "world")
	index.Add(3, "test")

	// Search for "hello world" - should find doc1 and doc2
	results := index.SearchOR("hello world")

	if len(results) != 2 {
		t.Errorf("SearchOR() returned %d results, want 2", len(results))
	}
}

func TestInvertedIndex_SearchEmpty(t *testing.T) {
	index := NewInvertedIndex()

	results := index.SearchAND("hello")

	if results != nil && len(results) != 0 {
		t.Errorf("SearchAND on empty index should return empty")
	}
}

func TestInvertedIndex_SearchNoMatch(t *testing.T) {
	index := NewInvertedIndex()
	index.Add(1, "hello world")

	results := index.SearchAND("foo bar")

	if results != nil && len(results) != 0 {
		t.Errorf("SearchAND with no matches should return empty")
	}
}

func TestInvertedIndex_Remove(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(100, "hello world")
	if index.NodeCount() != 1 {
		t.Fatalf("NodeCount() = %d, want 1", index.NodeCount())
	}

	index.Remove(100)

	if index.NodeCount() != 0 {
		t.Errorf("NodeCount() after remove = %d, want 0", index.NodeCount())
	}
}

func TestInvertedIndex_SearchAfterRemove(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(1, "test")
	index.Add(2, "test")

	index.Remove(1)

	results := index.SearchAND("test")

	if len(results) != 1 {
		t.Fatalf("SearchAND returned %d results, want 1", len(results))
	}
	if results[0] != 2 {
		t.Errorf("result = %d, want 2", results[0])
	}
}

func TestInvertedIndex_SearchWithScores(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(1, "test")
	index.Add(2, "test hello world")

	results := index.SearchWithScores("test hello", 10)

	if len(results) < 1 {
		t.Fatal("SearchWithScores returned no results")
	}

	// Doc2 should score higher as it matches both terms
	if len(results) >= 2 {
		if results[0].NodeID != 2 {
			t.Errorf("Expected doc 2 to rank first, got %d", results[0].NodeID)
		}
	}
}

func TestInvertedIndex_Contains(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(1, "hello world")

	if !index.Contains("hello") {
		t.Error("Contains('hello') should be true")
	}
	if !index.Contains("hello world") {
		t.Error("Contains('hello world') should be true")
	}
	if index.Contains("foo bar") {
		t.Error("Contains('foo bar') should be false")
	}
}

func TestInvertedIndex_Clear(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(1, "hello world")
	index.Add(2, "foo bar")

	index.Clear()

	if index.NodeCount() != 0 {
		t.Errorf("NodeCount() after clear = %d, want 0", index.NodeCount())
	}
	if index.Size() != 0 {
		t.Errorf("Size() after clear = %d, want 0", index.Size())
	}
}

func TestInvertedIndex_UpdateDocument(t *testing.T) {
	index := NewInvertedIndex()

	index.Add(1, "hello world")
	index.Add(1, "goodbye everyone") // Should replace

	results := index.SearchAND("hello")
	if len(results) != 0 {
		t.Error("Old content should be removed")
	}

	results = index.SearchAND("goodbye")
	if len(results) != 1 {
		t.Error("New content should be searchable")
	}
}

func TestTokenize(t *testing.T) {
	tests := []struct {
		input    string
		expected []string
	}{
		{"hello world", []string{"hello", "world"}},
		{"Hello, World!", []string{"hello", "world"}},
		{"test_function", []string{"test_function"}},
		{"a b c", []string{}}, // Single chars filtered out
		{"CamelCase", []string{"camelcase"}},
		{"", []string{}},
	}

	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			result := tokenize(tt.input)

			if len(result) != len(tt.expected) {
				t.Errorf("tokenize(%q) = %v (len %d), want len %d",
					tt.input, result, len(result), len(tt.expected))
				return
			}

			for i, token := range result {
				if token != tt.expected[i] {
					t.Errorf("tokenize(%q)[%d] = %q, want %q",
						tt.input, i, token, tt.expected[i])
				}
			}
		})
	}
}

func BenchmarkInvertedIndex_Add(b *testing.B) {
	index := NewInvertedIndex()
	content := "The quick brown fox jumps over the lazy dog"

	for i := 0; i < b.N; i++ {
		index.Add(types.NodeID(i), content)
	}
}

func BenchmarkInvertedIndex_SearchAND(b *testing.B) {
	index := NewInvertedIndex()

	// Add 1000 documents
	for i := 0; i < 1000; i++ {
		index.Add(types.NodeID(i), "hello world test document number")
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		index.SearchAND("hello world")
	}
}
