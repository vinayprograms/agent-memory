package embedding

import (
	"testing"

	"github.com/anthropics/memory-go/pkg/types"
)

func TestStubEngine_Embed(t *testing.T) {
	engine := NewStubEngine()
	defer engine.Close()

	emb, err := engine.Embed("hello world")
	if err != nil {
		t.Fatalf("Embed() error = %v", err)
	}

	if len(emb) != types.EmbeddingDim {
		t.Errorf("len(embedding) = %d, want %d", len(emb), types.EmbeddingDim)
	}
}

func TestStubEngine_Deterministic(t *testing.T) {
	engine := NewStubEngine()

	emb1, _ := engine.Embed("test input")
	emb2, _ := engine.Embed("test input")

	for i := range emb1 {
		if emb1[i] != emb2[i] {
			t.Errorf("Embeddings should be deterministic, differ at index %d", i)
			break
		}
	}
}

func TestStubEngine_DifferentInputs(t *testing.T) {
	engine := NewStubEngine()

	emb1, _ := engine.Embed("hello")
	emb2, _ := engine.Embed("world")

	same := true
	for i := range emb1 {
		if emb1[i] != emb2[i] {
			same = false
			break
		}
	}

	if same {
		t.Error("Different inputs should produce different embeddings")
	}
}

func TestStubEngine_EmbedBatch(t *testing.T) {
	engine := NewStubEngine()

	texts := []string{"one", "two", "three"}
	embeddings, err := engine.EmbedBatch(texts)
	if err != nil {
		t.Fatalf("EmbedBatch() error = %v", err)
	}

	if len(embeddings) != len(texts) {
		t.Errorf("len(embeddings) = %d, want %d", len(embeddings), len(texts))
	}

	for i, emb := range embeddings {
		if len(emb) != types.EmbeddingDim {
			t.Errorf("embedding[%d] length = %d, want %d", i, len(emb), types.EmbeddingDim)
		}
	}
}

func TestStubEngine_Dimension(t *testing.T) {
	engine := NewStubEngine()

	if engine.Dimension() != types.EmbeddingDim {
		t.Errorf("Dimension() = %d, want %d", engine.Dimension(), types.EmbeddingDim)
	}
}

func TestStubEngine_Provider(t *testing.T) {
	engine := NewStubEngine()

	if engine.Provider() != "stub" {
		t.Errorf("Provider() = %s, want stub", engine.Provider())
	}
}

func TestStubEngine_EmptyInput(t *testing.T) {
	engine := NewStubEngine()

	emb, err := engine.Embed("")
	if err != nil {
		t.Fatalf("Embed() error = %v", err)
	}

	if len(emb) != types.EmbeddingDim {
		t.Errorf("Empty input should still return embedding of correct dimension")
	}
}

func TestStubEngine_Normalized(t *testing.T) {
	engine := NewStubEngine()

	emb, _ := engine.Embed("test")

	var norm float32
	for _, v := range emb {
		norm += v * v
	}

	// Should be approximately 1.0 (unit vector)
	if norm < 0.99 || norm > 1.01 {
		t.Errorf("Embedding should be normalized, got norm² = %v", norm)
	}
}
