package embedding

import (
	"hash/fnv"

	"github.com/anthropics/memory-go/pkg/types"
)

// StubEngine provides deterministic embeddings for testing.
// It generates embeddings based on text content hash, ensuring
// similar texts produce similar embeddings.
type StubEngine struct {
	dim int
}

// NewStubEngine creates a new stub embedding engine.
func NewStubEngine() *StubEngine {
	return &StubEngine{
		dim: types.EmbeddingDim,
	}
}

// Embed generates a deterministic embedding based on text hash.
func (e *StubEngine) Embed(text string) (types.Embedding, error) {
	return e.generateEmbedding(text), nil
}

// EmbedBatch generates embeddings for multiple texts.
func (e *StubEngine) EmbedBatch(texts []string) ([]types.Embedding, error) {
	results := make([]types.Embedding, len(texts))
	for i, text := range texts {
		results[i] = e.generateEmbedding(text)
	}
	return results, nil
}

// Dimension returns the embedding dimension.
func (e *StubEngine) Dimension() int {
	return e.dim
}

// Provider returns "stub".
func (e *StubEngine) Provider() string {
	return "stub"
}

// Close is a no-op for the stub engine.
func (e *StubEngine) Close() error {
	return nil
}

// generateEmbedding creates a deterministic embedding from text.
// Uses multiple hash positions to create a pseudo-random but deterministic vector.
func (e *StubEngine) generateEmbedding(text string) types.Embedding {
	embedding := make(types.Embedding, e.dim)

	if len(text) == 0 {
		return embedding
	}

	// Use FNV hash with different seeds to fill the embedding
	h := fnv.New64a()

	for i := 0; i < e.dim; i++ {
		h.Reset()
		// Add position to create different hashes for each dimension
		h.Write([]byte{byte(i), byte(i >> 8)})
		h.Write([]byte(text))

		// Convert hash to float in range [-1, 1]
		hashVal := h.Sum64()
		embedding[i] = float32(int64(hashVal)>>32) / float32(1<<31)
	}

	// Normalize to unit length
	return Normalize(embedding)
}

// Ensure StubEngine implements Engine.
var _ Engine = (*StubEngine)(nil)
