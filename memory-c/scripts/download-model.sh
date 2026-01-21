#!/bin/bash
# Download ONNX embedding model for Memory Service
#
# Usage:
#   ./scripts/download-model.sh                    # Download default model
#   ./scripts/download-model.sh all-mpnet-base-v2  # Download specific model

set -e

MODEL_NAME="${1:-all-MiniLM-L6-v2}"
MODEL_DIR="./models/${MODEL_NAME}"
BASE_URL="https://huggingface.co/sentence-transformers/${MODEL_NAME}/resolve/main"

echo "Downloading model: ${MODEL_NAME}"
echo ""

# Create directory
mkdir -p "${MODEL_DIR}"

# Download ONNX model
echo "Downloading model.onnx..."
if ! curl -L --fail --progress-bar \
    -o "${MODEL_DIR}/model.onnx" \
    "${BASE_URL}/onnx/model.onnx" 2>/dev/null; then

    # Try alternative path (some models use different structure)
    echo "Trying alternative path..."
    if ! curl -L --fail --progress-bar \
        -o "${MODEL_DIR}/model.onnx" \
        "${BASE_URL}/model.onnx" 2>/dev/null; then

        echo ""
        echo "ERROR: Could not download ONNX model."
        echo ""
        echo "This model may not have a pre-exported ONNX version."
        echo "Try one of these models that have ONNX exports:"
        echo "  ./scripts/download-model.sh all-MiniLM-L6-v2"
        echo "  ./scripts/download-model.sh all-MiniLM-L12-v2"
        echo "  ./scripts/download-model.sh paraphrase-MiniLM-L6-v2"
        echo ""
        echo "Or check: https://huggingface.co/sentence-transformers/${MODEL_NAME}/tree/main"
        rm -rf "${MODEL_DIR}"
        exit 1
    fi
fi

# Download tokenizer files
echo "Downloading tokenizer..."
curl -sL -o "${MODEL_DIR}/tokenizer.json" "${BASE_URL}/tokenizer.json" 2>/dev/null || true
curl -sL -o "${MODEL_DIR}/tokenizer_config.json" "${BASE_URL}/tokenizer_config.json" 2>/dev/null || true
curl -sL -o "${MODEL_DIR}/vocab.txt" "${BASE_URL}/vocab.txt" 2>/dev/null || true
curl -sL -o "${MODEL_DIR}/special_tokens_map.json" "${BASE_URL}/special_tokens_map.json" 2>/dev/null || true

# Show result
MODEL_SIZE=$(ls -lh "${MODEL_DIR}/model.onnx" | awk '{print $5}')
echo ""
echo "Done! Model saved to: ${MODEL_DIR}/"
echo "  model.onnx: ${MODEL_SIZE}"
echo ""
echo "Run the service with:"
echo "  ./build/bin/memory-service --model ${MODEL_DIR}/model.onnx"
