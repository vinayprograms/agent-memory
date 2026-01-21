/*
 * Memory Service - Embeddings Storage
 *
 * mmap'd storage for embedding vectors at each hierarchy level.
 * Each level has its own file with contiguous float32 arrays.
 */

#ifndef MEMORY_SERVICE_EMBEDDINGS_H
#define MEMORY_SERVICE_EMBEDDINGS_H

#include "../core/arena.h"
#include "../../include/types.h"
#include "../../include/error.h"

/* Embedding storage for one level */
typedef struct {
    arena_t*        arena;          /* mmap'd arena */
    size_t          count;          /* Number of embeddings */
    size_t          capacity;       /* Max embeddings before grow */
    hierarchy_level_t level;
} embedding_level_t;

/* Embeddings store (all levels) */
typedef struct {
    embedding_level_t levels[LEVEL_COUNT];
    char*           base_dir;
} embeddings_store_t;

/* Create embeddings store */
mem_error_t embeddings_create(embeddings_store_t** store, const char* dir,
                              size_t initial_capacity);

/* Open existing embeddings store */
mem_error_t embeddings_open(embeddings_store_t** store, const char* dir);

/* Allocate new embedding slot, returns index */
mem_error_t embeddings_alloc(embeddings_store_t* store, hierarchy_level_t level,
                             uint32_t* idx);

/* Set embedding values */
mem_error_t embeddings_set(embeddings_store_t* store, hierarchy_level_t level,
                           uint32_t idx, const float* values);

/* Get embedding values (returns pointer to mmap'd data) */
const float* embeddings_get(const embeddings_store_t* store,
                            hierarchy_level_t level, uint32_t idx);

/* Copy embedding to buffer */
mem_error_t embeddings_copy(const embeddings_store_t* store,
                            hierarchy_level_t level, uint32_t idx,
                            float* buf);

/* Compute cosine similarity between two embeddings */
float embeddings_similarity(const embeddings_store_t* store,
                            hierarchy_level_t level,
                            uint32_t idx1, uint32_t idx2);

/* Compute cosine similarity between embedding and query vector */
float embeddings_similarity_vec(const embeddings_store_t* store,
                                hierarchy_level_t level, uint32_t idx,
                                const float* query);

/* Get count for level */
size_t embeddings_count(const embeddings_store_t* store, hierarchy_level_t level);

/* Sync to disk */
mem_error_t embeddings_sync(embeddings_store_t* store);

/* Close store */
void embeddings_close(embeddings_store_t* store);

#endif /* MEMORY_SERVICE_EMBEDDINGS_H */
