/*
 * Memory Service - HNSW Index
 *
 * Hierarchical Navigable Small World graph for approximate
 * nearest neighbor search on embedding vectors.
 */

#ifndef MEMORY_SERVICE_HNSW_H
#define MEMORY_SERVICE_HNSW_H

#include "../../include/types.h"
#include "../../include/error.h"

/* Forward declaration */
typedef struct hnsw_index hnsw_index_t;

/* HNSW configuration */
typedef struct {
    size_t max_elements;    /* Maximum number of elements */
    size_t M;               /* Max connections per layer (default: 16) */
    size_t ef_construction; /* Size of dynamic candidate list (default: 200) */
    size_t ef_search;       /* Size of search candidate list (default: 50) */
} hnsw_config_t;

/* Default configuration */
#define HNSW_CONFIG_DEFAULT { \
    .max_elements = 100000, \
    .M = 16, \
    .ef_construction = 200, \
    .ef_search = 50 \
}

/* Search result */
typedef struct {
    node_id_t id;
    float distance;  /* Lower is more similar (1 - cosine_similarity) */
} hnsw_result_t;

/*
 * Create a new HNSW index
 */
mem_error_t hnsw_create(hnsw_index_t** index, const hnsw_config_t* config);

/*
 * Destroy HNSW index
 */
void hnsw_destroy(hnsw_index_t* index);

/*
 * Add a vector to the index
 *
 * @param index   The HNSW index
 * @param id      Unique identifier for this vector
 * @param vector  The embedding vector (EMBEDDING_DIM floats)
 * @return MEM_OK on success
 */
mem_error_t hnsw_add(hnsw_index_t* index, node_id_t id, const float* vector);

/*
 * Search for nearest neighbors
 *
 * @param index       The HNSW index
 * @param query       Query vector (EMBEDDING_DIM floats)
 * @param k           Number of nearest neighbors to find
 * @param results     Output array (must hold k results)
 * @param result_count Output: actual number of results found
 * @return MEM_OK on success
 */
mem_error_t hnsw_search(const hnsw_index_t* index, const float* query,
                        size_t k, hnsw_result_t* results, size_t* result_count);

/*
 * Get number of elements in the index
 */
size_t hnsw_size(const hnsw_index_t* index);

/*
 * Check if index contains an element
 */
bool hnsw_contains(const hnsw_index_t* index, node_id_t id);

/*
 * Remove an element from the index
 * Note: This marks the element as deleted but doesn't reclaim space
 */
mem_error_t hnsw_remove(hnsw_index_t* index, node_id_t id);

#endif /* MEMORY_SERVICE_HNSW_H */
