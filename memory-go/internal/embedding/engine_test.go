package embedding

import (
	"math"
	"testing"

	"github.com/anthropics/memory-go/pkg/types"
)

func TestSimilarity(t *testing.T) {
	tests := []struct {
		name     string
		a, b     types.Embedding
		expected float32
		delta    float32
	}{
		{
			name:     "identical vectors",
			a:        types.Embedding{1, 0, 0},
			b:        types.Embedding{1, 0, 0},
			expected: 1.0,
			delta:    0.01,
		},
		{
			name:     "orthogonal vectors",
			a:        types.Embedding{1, 0, 0},
			b:        types.Embedding{0, 1, 0},
			expected: 0.0,
			delta:    0.01,
		},
		{
			name:     "opposite vectors",
			a:        types.Embedding{1, 0, 0},
			b:        types.Embedding{-1, 0, 0},
			expected: -1.0,
			delta:    0.01,
		},
		{
			name:     "empty vectors",
			a:        types.Embedding{},
			b:        types.Embedding{},
			expected: 0.0,
			delta:    0.01,
		},
		{
			name:     "different lengths",
			a:        types.Embedding{1, 2},
			b:        types.Embedding{1, 2, 3},
			expected: 0.0,
			delta:    0.01,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Similarity(tt.a, tt.b)
			if math.Abs(float64(got-tt.expected)) > float64(tt.delta) {
				t.Errorf("Similarity() = %v, want %v (±%v)", got, tt.expected, tt.delta)
			}
		})
	}
}

func TestNormalize(t *testing.T) {
	v := types.Embedding{3, 4, 0}
	normalized := Normalize(v)

	// Check unit length
	var norm float32
	for _, val := range normalized {
		norm += val * val
	}
	norm = float32(math.Sqrt(float64(norm)))

	if math.Abs(float64(norm-1.0)) > 0.01 {
		t.Errorf("Normalized vector length = %v, want 1.0", norm)
	}

	// Check direction preserved (3,4,0 -> 0.6, 0.8, 0)
	if math.Abs(float64(normalized[0]-0.6)) > 0.01 {
		t.Errorf("normalized[0] = %v, want 0.6", normalized[0])
	}
	if math.Abs(float64(normalized[1]-0.8)) > 0.01 {
		t.Errorf("normalized[1] = %v, want 0.8", normalized[1])
	}
}

func TestMeanPool(t *testing.T) {
	embeddings := []types.Embedding{
		{1, 0, 0},
		{0, 1, 0},
		{0, 0, 1},
	}

	result := MeanPool(embeddings)

	if result == nil {
		t.Fatal("MeanPool returned nil")
	}

	if len(result) != 3 {
		t.Errorf("len(result) = %d, want 3", len(result))
	}

	// Mean of unit vectors should be normalized
	var norm float32
	for _, v := range result {
		norm += v * v
	}
	norm = float32(math.Sqrt(float64(norm)))

	if math.Abs(float64(norm-1.0)) > 0.01 {
		t.Errorf("MeanPool result should be normalized, got length %v", norm)
	}
}

func TestMeanPool_Empty(t *testing.T) {
	result := MeanPool(nil)
	if result != nil {
		t.Error("MeanPool(nil) should return nil")
	}

	result = MeanPool([]types.Embedding{})
	if result != nil {
		t.Error("MeanPool([]) should return nil")
	}
}

func TestDistance(t *testing.T) {
	a := types.Embedding{0, 0, 0}
	b := types.Embedding{3, 4, 0}

	dist := Distance(a, b)
	expected := float32(5.0) // 3-4-5 triangle

	if math.Abs(float64(dist-expected)) > 0.01 {
		t.Errorf("Distance() = %v, want %v", dist, expected)
	}
}
