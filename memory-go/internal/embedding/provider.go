package embedding

import (
	"fmt"

	"github.com/anthropics/memory-go/pkg/types"
)

// ProviderType represents an execution provider type.
type ProviderType string

const (
	ProviderCPU      ProviderType = "cpu"
	ProviderORT      ProviderType = "ort"
	ProviderCUDA     ProviderType = "cuda"
	ProviderGPU      ProviderType = "gpu"
	ProviderCoreML   ProviderType = "coreml"
	ProviderMIGraphX ProviderType = "migraphx"
	ProviderDirectML ProviderType = "directml"
	ProviderTensorRT ProviderType = "tensorrt"
	ProviderStub     ProviderType = "stub"
)

// NewEngine creates an embedding engine based on configuration.
func NewEngine(config types.EmbeddingConfig) (Engine, error) {
	provider := ProviderType(config.Provider)

	switch provider {
	case ProviderStub, "":
		// Use stub for testing or when no provider specified
		if config.ModelPath == "" {
			return NewStubEngine(), nil
		}
		// Fall through to ONNX if model path is specified

	case ProviderCPU, ProviderORT, ProviderCUDA, ProviderGPU, ProviderCoreML, ProviderMIGraphX, ProviderDirectML, ProviderTensorRT:
		// Use ONNX engine for all hardware providers
		return NewONNXEngine(config)
	}

	// Default to stub if ONNX is not available
	if config.ModelPath == "" {
		return NewStubEngine(), nil
	}

	return nil, fmt.Errorf("unsupported embedding provider: %s", provider)
}

// AvailableProviders returns a list of available execution providers.
func AvailableProviders() []ProviderType {
	providers := []ProviderType{ProviderCPU, ProviderStub}

	// TODO: Check for actual availability of each provider
	// This would involve checking for libraries and hardware

	// if coreMLAvailable() {
	//     providers = append(providers, ProviderCoreML)
	// }
	// if cudaAvailable() {
	//     providers = append(providers, ProviderCUDA)
	//     providers = append(providers, ProviderTensorRT)
	// }
	// if rocmAvailable() {
	//     providers = append(providers, ProviderMIGraphX)
	// }
	// if directMLAvailable() {
	//     providers = append(providers, ProviderDirectML)
	// }

	return providers
}

// ProviderInfo returns information about an execution provider.
type ProviderInfo struct {
	Type        ProviderType `json:"type"`
	Name        string       `json:"name"`
	Description string       `json:"description"`
	Available   bool         `json:"available"`
	Hardware    string       `json:"hardware,omitempty"`
}

// GetProviderInfo returns information about all known providers.
func GetProviderInfo() []ProviderInfo {
	return []ProviderInfo{
		{
			Type:        ProviderCPU,
			Name:        "CPU",
			Description: "CPU execution (always available)",
			Available:   true,
			Hardware:    "CPU",
		},
		{
			Type:        ProviderCUDA,
			Name:        "CUDA",
			Description: "NVIDIA GPU acceleration",
			Available:   false, // TODO: detect
			Hardware:    "NVIDIA GPU",
		},
		{
			Type:        ProviderCoreML,
			Name:        "CoreML",
			Description: "Apple Silicon acceleration",
			Available:   false, // TODO: detect
			Hardware:    "Apple Silicon",
		},
		{
			Type:        ProviderMIGraphX,
			Name:        "MIGraphX",
			Description: "AMD GPU acceleration (ROCm)",
			Available:   false, // TODO: detect
			Hardware:    "AMD GPU",
		},
		{
			Type:        ProviderDirectML,
			Name:        "DirectML",
			Description: "Windows GPU acceleration (AMD/Intel/NVIDIA)",
			Available:   false, // TODO: detect
			Hardware:    "Windows GPU",
		},
		{
			Type:        ProviderTensorRT,
			Name:        "TensorRT",
			Description: "NVIDIA TensorRT optimization",
			Available:   false, // TODO: detect
			Hardware:    "NVIDIA GPU",
		},
		{
			Type:        ProviderStub,
			Name:        "Stub",
			Description: "Deterministic stub for testing (no ML model)",
			Available:   true,
			Hardware:    "None",
		},
	}
}
