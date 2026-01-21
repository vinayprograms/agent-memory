package types

import (
	"time"
)

// Config holds all configuration for the memory service.
type Config struct {
	// Server configuration
	Server ServerConfig `json:"server"`

	// Storage configuration
	Storage StorageConfig `json:"storage"`

	// Embedding configuration
	Embedding EmbeddingConfig `json:"embedding"`

	// Search configuration
	Search SearchConfig `json:"search"`

	// Logging configuration
	Log LogConfig `json:"log"`
}

// ServerConfig holds HTTP server configuration.
type ServerConfig struct {
	Port            int           `json:"port"`
	ReadTimeout     time.Duration `json:"read_timeout"`
	WriteTimeout    time.Duration `json:"write_timeout"`
	ShutdownTimeout time.Duration `json:"shutdown_timeout"`
}

// StorageConfig holds storage configuration.
type StorageConfig struct {
	DataDir       string `json:"data_dir"`
	MaxNodeCount  uint64 `json:"max_node_count"`
	SyncWrites    bool   `json:"sync_writes"`
	CacheSize     int64  `json:"cache_size"` // Pebble cache size in bytes
}

// EmbeddingConfig holds embedding model configuration.
type EmbeddingConfig struct {
	ModelPath     string `json:"model_path"`
	VocabPath     string `json:"vocab_path"`
	BatchSize     int    `json:"batch_size"`
	MaxSeqLength  int    `json:"max_seq_length"`
	UseGPU        bool   `json:"use_gpu"`
	DeviceID      int    `json:"device_id"`
	Provider      string `json:"provider"` // cpu, cuda, coreml, directml, migraphx
}

// SearchConfig holds search configuration.
type SearchConfig struct {
	// HNSW parameters
	HNSWM            int `json:"hnsw_m"`              // Max connections per layer
	HNSWEfConstruct  int `json:"hnsw_ef_construct"`   // Construction search width
	HNSWEfSearch     int `json:"hnsw_ef_search"`      // Query search width

	// Ranking weights
	RelevanceWeight float32 `json:"relevance_weight"`
	RecencyWeight   float32 `json:"recency_weight"`
	LevelBoostWeight float32 `json:"level_boost_weight"`

	// Default limits
	DefaultMaxResults int `json:"default_max_results"`
	MaxTokenBudget    int `json:"max_token_budget"`
}

// LogConfig holds logging configuration.
type LogConfig struct {
	Level  string `json:"level"`  // trace, debug, info, warn, error
	Format string `json:"format"` // text, json
	Output string `json:"output"` // stdout, stderr, file path
}

// DefaultConfig returns the default configuration.
func DefaultConfig() *Config {
	return &Config{
		Server: ServerConfig{
			Port:            8080,
			ReadTimeout:     30 * time.Second,
			WriteTimeout:    30 * time.Second,
			ShutdownTimeout: 10 * time.Second,
		},
		Storage: StorageConfig{
			DataDir:      "./data",
			MaxNodeCount: 10_000_000,
			SyncWrites:   false,
			CacheSize:    256 << 20, // 256 MB
		},
		Embedding: EmbeddingConfig{
			ModelPath:    "./models/all-MiniLM-L6-v2.onnx",
			VocabPath:    "./models/vocab.txt",
			BatchSize:    32,
			MaxSeqLength: 512,
			UseGPU:       false,
			DeviceID:     0,
			Provider:     "cpu",
		},
		Search: SearchConfig{
			HNSWM:            16,
			HNSWEfConstruct:  200,
			HNSWEfSearch:     50,
			RelevanceWeight:  0.6,
			RecencyWeight:    0.3,
			LevelBoostWeight: 0.1,
			DefaultMaxResults: 10,
			MaxTokenBudget:   4096,
		},
		Log: LogConfig{
			Level:  "info",
			Format: "text",
			Output: "stdout",
		},
	}
}
