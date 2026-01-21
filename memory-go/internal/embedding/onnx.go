package embedding

import (
	"fmt"
	"sync"

	"github.com/anthropics/memory-go/pkg/types"
	"github.com/knights-analytics/hugot"
	"github.com/knights-analytics/hugot/pipelines"
)

// ONNXEngine provides embeddings using ONNX Runtime via hugot.
type ONNXEngine struct {
	config    types.EmbeddingConfig
	dim       int
	mu        sync.Mutex
	modelPath string
	provider  string

	session  *hugot.Session
	pipeline *pipelines.FeatureExtractionPipeline
}

// NewONNXEngine creates a new ONNX-based embedding engine.
func NewONNXEngine(config types.EmbeddingConfig) (*ONNXEngine, error) {
	provider := config.Provider
	if provider == "" {
		provider = "cpu"
	}

	e := &ONNXEngine{
		config:    config,
		dim:       types.EmbeddingDim,
		modelPath: config.ModelPath,
		provider:  provider,
	}

	// Initialize hugot session
	// Use pure Go session by default (no C dependencies, works with small models like MiniLM)
	// For better performance with larger models, use ORT or XLA backends
	// which require build tags and external libraries
	var session *hugot.Session
	var err error

	// NewGoSession() works without any external dependencies
	// For CUDA/CoreML/etc., user needs to compile with appropriate build tags
	// and use NewORTSession() or NewXLASession()
	session, err = hugot.NewGoSession()
	if err != nil {
		return nil, fmt.Errorf("failed to create hugot session: %w", err)
	}
	e.session = session

	// Create feature extraction pipeline config
	pipelineConfig := hugot.FeatureExtractionConfig{
		ModelPath: config.ModelPath,
		Name:      "embedding",
	}

	// Add normalization option for L2 normalized embeddings
	pipelineConfig.Options = append(pipelineConfig.Options, pipelines.WithNormalization())

	// Create the pipeline
	pipeline, err := hugot.NewPipeline(session, pipelineConfig)
	if err != nil {
		session.Destroy()
		return nil, fmt.Errorf("failed to create embedding pipeline: %w", err)
	}
	e.pipeline = pipeline

	return e, nil
}

// Embed generates an embedding for a single text.
func (e *ONNXEngine) Embed(text string) (types.Embedding, error) {
	embeddings, err := e.EmbedBatch([]string{text})
	if err != nil {
		return nil, err
	}
	if len(embeddings) == 0 {
		return nil, fmt.Errorf("no embedding generated")
	}
	return embeddings[0], nil
}

// EmbedBatch generates embeddings for multiple texts.
func (e *ONNXEngine) EmbedBatch(texts []string) ([]types.Embedding, error) {
	if len(texts) == 0 {
		return nil, nil
	}

	e.mu.Lock()
	defer e.mu.Unlock()

	// Run the pipeline
	result, err := e.pipeline.RunPipeline(texts)
	if err != nil {
		return nil, fmt.Errorf("embedding inference failed: %w", err)
	}

	// Convert results to our embedding format
	// result.Embeddings is [][]float32
	embeddings := make([]types.Embedding, len(result.Embeddings))
	for i, emb := range result.Embeddings {
		embeddings[i] = types.Embedding(emb)
	}

	return embeddings, nil
}

// Dimension returns the embedding dimension.
func (e *ONNXEngine) Dimension() int {
	return e.dim
}

// Provider returns the execution provider name.
func (e *ONNXEngine) Provider() string {
	return e.provider
}

// Close releases ONNX resources.
func (e *ONNXEngine) Close() error {
	e.mu.Lock()
	defer e.mu.Unlock()

	if e.session != nil {
		return e.session.Destroy()
	}
	return nil
}

// Ensure ONNXEngine implements Engine.
var _ Engine = (*ONNXEngine)(nil)
