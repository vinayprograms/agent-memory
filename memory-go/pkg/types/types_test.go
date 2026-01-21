package types

import (
	"testing"
)

func TestHierarchyLevel_String(t *testing.T) {
	tests := []struct {
		level    HierarchyLevel
		expected string
	}{
		{LevelStatement, "statement"},
		{LevelBlock, "block"},
		{LevelMessage, "message"},
		{LevelSession, "session"},
		{LevelAgent, "agent"},
		{HierarchyLevel(99), "unknown"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			if got := tt.level.String(); got != tt.expected {
				t.Errorf("HierarchyLevel.String() = %v, want %v", got, tt.expected)
			}
		})
	}
}

func TestDefaultConfig(t *testing.T) {
	cfg := DefaultConfig()

	if cfg == nil {
		t.Fatal("DefaultConfig() returned nil")
	}

	// Server defaults
	if cfg.Server.Port != 8080 {
		t.Errorf("Server.Port = %d, want 8080", cfg.Server.Port)
	}

	// Storage defaults
	if cfg.Storage.DataDir != "./data" {
		t.Errorf("Storage.DataDir = %s, want ./data", cfg.Storage.DataDir)
	}

	// Embedding defaults
	if cfg.Embedding.BatchSize != 32 {
		t.Errorf("Embedding.BatchSize = %d, want 32", cfg.Embedding.BatchSize)
	}

	// Search defaults
	if cfg.Search.HNSWM != 16 {
		t.Errorf("Search.HNSWM = %d, want 16", cfg.Search.HNSWM)
	}
	if cfg.Search.RelevanceWeight != 0.6 {
		t.Errorf("Search.RelevanceWeight = %f, want 0.6", cfg.Search.RelevanceWeight)
	}
}

func TestEmbeddingDim(t *testing.T) {
	if EmbeddingDim != 384 {
		t.Errorf("EmbeddingDim = %d, want 384", EmbeddingDim)
	}
}
