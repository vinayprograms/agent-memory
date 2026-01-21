// Package embedding provides text embedding generation.
package embedding

import (
	"github.com/anthropics/memory-go/pkg/types"
)

// Engine is the interface for embedding generation.
type Engine interface {
	// Embed generates an embedding for a single text.
	Embed(text string) (types.Embedding, error)

	// EmbedBatch generates embeddings for multiple texts.
	EmbedBatch(texts []string) ([]types.Embedding, error)

	// Dimension returns the embedding dimension.
	Dimension() int

	// Provider returns the execution provider name (cpu, cuda, coreml, etc.)
	Provider() string

	// Close releases resources.
	Close() error
}

// Similarity computes cosine similarity between two embeddings.
func Similarity(a, b types.Embedding) float32 {
	if len(a) != len(b) || len(a) == 0 {
		return 0
	}

	var dot, normA, normB float32
	for i := range a {
		dot += a[i] * b[i]
		normA += a[i] * a[i]
		normB += b[i] * b[i]
	}

	if normA == 0 || normB == 0 {
		return 0
	}

	return dot / (sqrt(normA) * sqrt(normB))
}

// sqrt is a fast approximate square root.
func sqrt(x float32) float32 {
	if x <= 0 {
		return 0
	}
	// Newton-Raphson iteration
	z := x
	for i := 0; i < 10; i++ {
		z = (z + x/z) / 2
	}
	return z
}

// Normalize normalizes an embedding to unit length.
func Normalize(e types.Embedding) types.Embedding {
	var norm float32
	for _, v := range e {
		norm += v * v
	}
	if norm == 0 {
		return e
	}
	norm = sqrt(norm)

	result := make(types.Embedding, len(e))
	for i, v := range e {
		result[i] = v / norm
	}
	return result
}

// MeanPool computes the mean of multiple embeddings.
func MeanPool(embeddings []types.Embedding) types.Embedding {
	if len(embeddings) == 0 {
		return nil
	}

	dim := len(embeddings[0])
	result := make(types.Embedding, dim)

	for _, e := range embeddings {
		for i, v := range e {
			result[i] += v
		}
	}

	n := float32(len(embeddings))
	for i := range result {
		result[i] /= n
	}

	return Normalize(result)
}

// Distance computes Euclidean distance between two embeddings.
func Distance(a, b types.Embedding) float32 {
	if len(a) != len(b) {
		return float32(1e10) // Very large distance
	}

	var sum float32
	for i := range a {
		diff := a[i] - b[i]
		sum += diff * diff
	}

	return sqrt(sum)
}
