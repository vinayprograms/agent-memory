/*
 * Memory Service - Unified Search Module
 *
 * Combines semantic (HNSW) and exact match (inverted index) search
 * with ranking and result merging.
 */

#ifndef MEMORY_SERVICE_SEARCH_H
#define MEMORY_SERVICE_SEARCH_H

#include "../../include/types.h"
#include "../../include/error.h"
#include "../core/hierarchy.h"
#include "hnsw.h"
#include "inverted_index.h"

/* Forward declaration */
typedef struct search_engine search_engine_t;

/* Search configuration */
typedef struct {
    float semantic_weight;    /* Weight for semantic search (default: 0.7) */
    float exact_weight;       /* Weight for exact match (default: 0.3) */
    float recency_weight;     /* Weight for recency (default: 0.3) in final ranking */
    float relevance_weight;   /* Weight for relevance (default: 0.6) */
    float level_weight;       /* Weight for hierarchy level (default: 0.1) */
    size_t max_candidates;    /* Max candidates per search type (default: 100) */
    size_t token_budget;      /* Max tokens in response (default: 4096) */
} search_config_t;

/* Default configuration */
#define SEARCH_CONFIG_DEFAULT { \
    .semantic_weight = 0.7f, \
    .exact_weight = 0.3f, \
    .recency_weight = 0.3f, \
    .relevance_weight = 0.6f, \
    .level_weight = 0.1f, \
    .max_candidates = 100, \
    .token_budget = 4096 \
}

/* Internal search result (different from API search_match_t) */
typedef struct {
    node_id_t node_id;
    hierarchy_level_t level;
    float score;              /* Final combined score */
    float semantic_score;     /* Semantic similarity score */
    float exact_score;        /* Exact match score */
    uint64_t timestamp;       /* For recency calculation */
} search_match_t;

/* Search query */
typedef struct {
    const float* embedding;   /* Query embedding (EMBEDDING_DIM floats) */
    const char** tokens;      /* Query tokens for exact match */
    size_t token_count;       /* Number of tokens */
    size_t k;                 /* Max results to return */
    hierarchy_level_t min_level;  /* Minimum hierarchy level to search */
    hierarchy_level_t max_level;  /* Maximum hierarchy level to search */
} search_query_t;

/*
 * Create a search engine
 */
mem_error_t search_engine_create(search_engine_t** engine,
                                 hierarchy_t* hierarchy,
                                 const search_config_t* config);

/*
 * Destroy search engine
 */
void search_engine_destroy(search_engine_t* engine);

/*
 * Index a node (add to HNSW and inverted index)
 *
 * @param engine    Search engine
 * @param node_id   Node to index
 * @param embedding Embedding vector
 * @param tokens    Text tokens
 * @param token_count Number of tokens
 * @param timestamp Node timestamp for recency
 */
mem_error_t search_engine_index(search_engine_t* engine, node_id_t node_id,
                                const float* embedding, const char** tokens,
                                size_t token_count, uint64_t timestamp);

/*
 * Remove a node from the index
 */
mem_error_t search_engine_remove(search_engine_t* engine, node_id_t node_id);

/*
 * Perform unified search
 *
 * @param engine       Search engine
 * @param query        Search query
 * @param results      Output array (must hold query->k results)
 * @param result_count Output: actual number of results
 */
mem_error_t search_engine_search(search_engine_t* engine,
                                 const search_query_t* query,
                                 search_match_t* results,
                                 size_t* result_count);

/*
 * Perform semantic-only search
 */
mem_error_t search_engine_semantic(search_engine_t* engine,
                                   const float* embedding, size_t k,
                                   search_match_t* results,
                                   size_t* result_count);

/*
 * Perform exact match search
 */
mem_error_t search_engine_exact(search_engine_t* engine,
                                const char** tokens, size_t token_count,
                                size_t k, search_match_t* results,
                                size_t* result_count);

/*
 * Get search engine statistics
 */
size_t search_engine_node_count(const search_engine_t* engine);

/*
 * Apply token budget constraint to results
 *
 * @param hierarchy   Hierarchy for text length lookup
 * @param results     Results to filter
 * @param count       Number of results
 * @param budget      Token budget
 * @param final_count Output: number of results within budget
 */
mem_error_t search_apply_budget(hierarchy_t* hierarchy,
                                search_match_t* results, size_t count,
                                size_t budget, size_t* final_count);

#endif /* MEMORY_SERVICE_SEARCH_H */
