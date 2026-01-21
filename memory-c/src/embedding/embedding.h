/*
 * Memory Service - Embedding Generation
 *
 * Handles text embedding generation using ONNX Runtime.
 * When ONNX Runtime is not available, provides stub implementation.
 */

#ifndef MEMORY_SERVICE_EMBEDDING_H
#define MEMORY_SERVICE_EMBEDDING_H

#include "../../include/types.h"
#include "../../include/error.h"

/* Forward declaration */
typedef struct embedding_engine embedding_engine_t;

/* Configuration */
typedef struct {
    const char* model_path;     /* Path to ONNX model file */
    size_t batch_size;          /* Batch size for inference (default: 32) */
    size_t max_seq_len;         /* Max sequence length (default: 512) */
} embedding_config_t;

/* Default configuration */
#define EMBEDDING_CONFIG_DEFAULT { \
    .model_path = NULL, \
    .batch_size = BATCH_SIZE, \
    .max_seq_len = 512 \
}

/*
 * Create embedding engine
 *
 * If model_path is NULL or ONNX Runtime is not available,
 * uses stub implementation that returns zero vectors.
 */
mem_error_t embedding_engine_create(embedding_engine_t** engine,
                                    const embedding_config_t* config);

/* Destroy embedding engine */
void embedding_engine_destroy(embedding_engine_t* engine);

/*
 * Generate embedding for single text
 *
 * @param engine  The embedding engine
 * @param text    Input text
 * @param text_len Length of input text
 * @param output  Output buffer (must be EMBEDDING_DIM floats)
 * @return MEM_OK on success
 */
mem_error_t embedding_generate(embedding_engine_t* engine,
                               const char* text, size_t text_len,
                               float* output);

/*
 * Generate embeddings for batch of texts
 *
 * @param engine   The embedding engine
 * @param texts    Array of text pointers
 * @param lengths  Array of text lengths
 * @param count    Number of texts in batch
 * @param outputs  Output buffer (must be count * EMBEDDING_DIM floats)
 * @return MEM_OK on success
 */
mem_error_t embedding_generate_batch(embedding_engine_t* engine,
                                     const char** texts,
                                     const size_t* lengths,
                                     size_t count,
                                     float* outputs);

/*
 * Mean pooling of embeddings
 *
 * Used to create parent embeddings from child embeddings.
 * Block embedding = mean(statement embeddings)
 * Message embedding = mean(block embeddings)
 *
 * @param embeddings  Array of embedding vectors
 * @param count       Number of embeddings to pool
 * @param output      Output buffer (must be EMBEDDING_DIM floats)
 */
void embedding_mean_pool(const float** embeddings, size_t count, float* output);

/*
 * Compute cosine similarity between two embeddings
 */
float embedding_cosine_similarity(const float* a, const float* b);

/*
 * L2 normalize an embedding vector in place
 */
void embedding_normalize(float* embedding);

/*
 * Check if ONNX Runtime is available
 */
bool embedding_onnx_available(void);

#endif /* MEMORY_SERVICE_EMBEDDING_H */
