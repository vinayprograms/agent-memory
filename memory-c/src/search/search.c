/*
 * Memory Service - Unified Search Implementation
 *
 * Combines semantic and exact match search with ranking:
 * final_score = 0.6 * relevance + 0.3 * recency + 0.1 * level_boost
 */

#include "search.h"
#include "../util/log.h"
#include "../util/time.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Node metadata for scoring */
typedef struct {
    node_id_t node_id;
    uint64_t timestamp;
    hierarchy_level_t level;
    size_t token_count;
} node_meta_t;

/* Search engine structure */
struct search_engine {
    search_config_t config;
    hierarchy_t* hierarchy;

    /* HNSW indices per level */
    hnsw_index_t* hnsw[LEVEL_COUNT];

    /* Single inverted index */
    inverted_index_t* inverted;

    /* Node metadata for scoring */
    node_meta_t* metas;
    size_t meta_count;
    size_t meta_capacity;

    /* ID to meta index mapping */
    size_t* id_to_meta;
    size_t id_map_size;
};

/* ========== Helper Functions ========== */

static size_t find_meta_index(search_engine_t* engine, node_id_t node_id) {
    if (node_id >= engine->id_map_size) return SIZE_MAX;
    return engine->id_to_meta[node_id];
}

static node_meta_t* get_meta(search_engine_t* engine, node_id_t node_id) {
    size_t idx = find_meta_index(engine, node_id);
    if (idx == SIZE_MAX) return NULL;
    return &engine->metas[idx];
}

/* Level boost: higher levels get slight boost */
static float level_boost(hierarchy_level_t level) {
    switch (level) {
        case LEVEL_SESSION:   return 1.0f;
        case LEVEL_MESSAGE:   return 0.9f;
        case LEVEL_BLOCK:     return 0.8f;
        case LEVEL_STATEMENT: return 0.7f;
        default:              return 0.5f;
    }
}

/* Recency score: exponential decay over time */
static float recency_score(uint64_t timestamp, uint64_t now) {
    if (timestamp >= now) return 1.0f;

    /* Decay constant: half-life of 1 hour */
    const float half_life_ms = 3600000.0f;
    float age_ms = (float)(now - timestamp);
    return expf(-0.693f * age_ms / half_life_ms);
}

/* Convert distance to similarity score */
static float distance_to_score(float distance) {
    return 1.0f - distance;
}

/* Merge and deduplicate results */
static void merge_results(search_match_t* dst, size_t* dst_count, size_t dst_capacity,
                          search_match_t* src, size_t src_count) {
    for (size_t i = 0; i < src_count && *dst_count < dst_capacity; i++) {
        bool found = false;
        for (size_t j = 0; j < *dst_count; j++) {
            if (dst[j].node_id == src[i].node_id) {
                if (src[i].semantic_score > dst[j].semantic_score) {
                    dst[j].semantic_score = src[i].semantic_score;
                }
                if (src[i].exact_score > dst[j].exact_score) {
                    dst[j].exact_score = src[i].exact_score;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            dst[(*dst_count)++] = src[i];
        }
    }
}

static int compare_results(const void* a, const void* b) {
    const search_match_t* ra = a;
    const search_match_t* rb = b;
    if (rb->score > ra->score) return 1;
    if (rb->score < ra->score) return -1;
    return 0;
}

/* ========== Public API ========== */

mem_error_t search_engine_create(search_engine_t** engine,
                                 hierarchy_t* hierarchy,
                                 const search_config_t* config) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine pointer is NULL");
    MEM_CHECK_ERR(hierarchy != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");

    search_engine_t* eng = calloc(1, sizeof(search_engine_t));
    if (!eng) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate search engine");
    }

    if (config) {
        eng->config = *config;
    } else {
        eng->config = (search_config_t)SEARCH_CONFIG_DEFAULT;
    }

    eng->hierarchy = hierarchy;

    /* Create HNSW index for each level */
    hnsw_config_t hnsw_config = HNSW_CONFIG_DEFAULT;
    for (int level = 0; level < LEVEL_COUNT; level++) {
        mem_error_t err = hnsw_create(&eng->hnsw[level], &hnsw_config);
        if (err != MEM_OK) {
            for (int i = 0; i < level; i++) {
                hnsw_destroy(eng->hnsw[i]);
            }
            free(eng);
            return err;
        }
    }

    /* Create inverted index */
    inverted_index_config_t inv_config = INVERTED_INDEX_CONFIG_DEFAULT;
    mem_error_t err = inverted_index_create(&eng->inverted, &inv_config);
    if (err != MEM_OK) {
        for (int i = 0; i < LEVEL_COUNT; i++) {
            hnsw_destroy(eng->hnsw[i]);
        }
        free(eng);
        return err;
    }

    /* Initialize metadata storage */
    eng->meta_capacity = 1024;
    eng->metas = calloc(eng->meta_capacity, sizeof(node_meta_t));
    if (!eng->metas) {
        inverted_index_destroy(eng->inverted);
        for (int i = 0; i < LEVEL_COUNT; i++) {
            hnsw_destroy(eng->hnsw[i]);
        }
        free(eng);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate metas");
    }

    eng->id_map_size = 1024;
    eng->id_to_meta = malloc(eng->id_map_size * sizeof(size_t));
    if (!eng->id_to_meta) {
        free(eng->metas);
        inverted_index_destroy(eng->inverted);
        for (int i = 0; i < LEVEL_COUNT; i++) {
            hnsw_destroy(eng->hnsw[i]);
        }
        free(eng);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate id map");
    }
    for (size_t i = 0; i < eng->id_map_size; i++) {
        eng->id_to_meta[i] = SIZE_MAX;
    }

    LOG_INFO("Search engine created");
    *engine = eng;

    /* Rebuild index from existing hierarchy data */
    size_t node_count = hierarchy_count(hierarchy);
    if (node_count > 0) {
        LOG_INFO("Rebuilding search index from %zu existing nodes...", node_count);
        size_t indexed = 0;
        uint64_t now = timestamp_now_ns();

        for (node_id_t id = 0; id < node_count; id++) {
            const float* embedding = hierarchy_get_embedding(hierarchy, id);
            if (embedding) {
                hierarchy_level_t level = hierarchy_get_level(hierarchy, id);

                /* Index in HNSW */
                if (level < LEVEL_COUNT) {
                    hnsw_add(eng->hnsw[level], id, embedding);
                }

                /* Store metadata */
                if (eng->meta_count >= eng->meta_capacity) {
                    size_t new_cap = eng->meta_capacity * 2;
                    node_meta_t* new_metas = realloc(eng->metas, new_cap * sizeof(node_meta_t));
                    if (new_metas) {
                        eng->metas = new_metas;
                        eng->meta_capacity = new_cap;
                    }
                }

                if (id >= eng->id_map_size) {
                    size_t new_size = id + 1024;
                    size_t* new_map = realloc(eng->id_to_meta, new_size * sizeof(size_t));
                    if (new_map) {
                        for (size_t i = eng->id_map_size; i < new_size; i++) {
                            new_map[i] = SIZE_MAX;
                        }
                        eng->id_to_meta = new_map;
                        eng->id_map_size = new_size;
                    }
                }

                size_t meta_idx = eng->meta_count++;
                eng->metas[meta_idx] = (node_meta_t){
                    .node_id = id,
                    .timestamp = now,
                    .level = level,
                    .token_count = 0
                };
                eng->id_to_meta[id] = meta_idx;

                indexed++;
            }
        }
        LOG_INFO("Search index rebuilt: %zu nodes indexed", indexed);
    }

    return MEM_OK;
}

void search_engine_destroy(search_engine_t* engine) {
    if (!engine) return;

    for (int i = 0; i < LEVEL_COUNT; i++) {
        hnsw_destroy(engine->hnsw[i]);
    }
    inverted_index_destroy(engine->inverted);
    free(engine->metas);
    free(engine->id_to_meta);
    free(engine);
}

mem_error_t search_engine_index(search_engine_t* engine, node_id_t node_id,
                                const float* embedding, const char** tokens,
                                size_t token_count, uint64_t timestamp) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");
    MEM_CHECK_ERR(embedding != NULL, MEM_ERR_INVALID_ARG, "embedding is NULL");

    hierarchy_level_t level = hierarchy_get_level(engine->hierarchy, node_id);

    /* Expand meta array if needed */
    if (engine->meta_count >= engine->meta_capacity) {
        size_t new_cap = engine->meta_capacity * 2;
        node_meta_t* new_metas = realloc(engine->metas, new_cap * sizeof(node_meta_t));
        if (!new_metas) {
            MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to expand metas");
        }
        engine->metas = new_metas;
        engine->meta_capacity = new_cap;
    }

    /* Expand ID map if needed */
    if (node_id >= engine->id_map_size) {
        size_t new_size = node_id + 1;
        if (new_size < engine->id_map_size * 2) {
            new_size = engine->id_map_size * 2;
        }
        size_t* new_map = realloc(engine->id_to_meta, new_size * sizeof(size_t));
        if (!new_map) {
            MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to expand id map");
        }
        for (size_t i = engine->id_map_size; i < new_size; i++) {
            new_map[i] = SIZE_MAX;
        }
        engine->id_to_meta = new_map;
        engine->id_map_size = new_size;
    }

    /* Store metadata */
    size_t meta_idx = engine->meta_count++;
    engine->metas[meta_idx].node_id = node_id;
    engine->metas[meta_idx].timestamp = timestamp;
    engine->metas[meta_idx].level = level;
    engine->metas[meta_idx].token_count = token_count;
    engine->id_to_meta[node_id] = meta_idx;

    /* Add to HNSW for this level */
    MEM_CHECK(hnsw_add(engine->hnsw[level], node_id, embedding));

    /* Add to inverted index */
    if (tokens && token_count > 0) {
        MEM_CHECK(inverted_index_add(engine->inverted, node_id, tokens, token_count));
    }

    return MEM_OK;
}

mem_error_t search_engine_remove(search_engine_t* engine, node_id_t node_id) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");

    node_meta_t* meta = get_meta(engine, node_id);
    if (!meta) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "node %u not in index", node_id);
    }

    hnsw_remove(engine->hnsw[meta->level], node_id);
    inverted_index_remove(engine->inverted, node_id);
    engine->id_to_meta[node_id] = SIZE_MAX;

    return MEM_OK;
}

mem_error_t search_engine_search(search_engine_t* engine,
                                 const search_query_t* query,
                                 search_match_t* results,
                                 size_t* result_count) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");
    MEM_CHECK_ERR(query != NULL, MEM_ERR_INVALID_ARG, "query is NULL");
    MEM_CHECK_ERR(results != NULL, MEM_ERR_INVALID_ARG, "results is NULL");
    MEM_CHECK_ERR(result_count != NULL, MEM_ERR_INVALID_ARG, "result_count is NULL");

    *result_count = 0;

    size_t max_candidates = engine->config.max_candidates;
    search_match_t* candidates = calloc(max_candidates * 2, sizeof(search_match_t));
    if (!candidates) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate candidates");
    }
    size_t candidate_count = 0;

    uint64_t now = time_now_ms();

    /* Semantic search across requested levels */
    if (query->embedding) {
        for (hierarchy_level_t level = query->min_level; level <= query->max_level; level++) {
            hnsw_result_t hnsw_results[100];
            size_t hnsw_count = 0;

            mem_error_t err = hnsw_search(engine->hnsw[level], query->embedding,
                                          max_candidates, hnsw_results, &hnsw_count);
            if (err != MEM_OK) continue;

            for (size_t i = 0; i < hnsw_count && candidate_count < max_candidates; i++) {
                node_meta_t* meta = get_meta(engine, hnsw_results[i].id);
                if (!meta) continue;

                search_match_t r = {
                    .node_id = hnsw_results[i].id,
                    .level = meta->level,
                    .semantic_score = distance_to_score(hnsw_results[i].distance),
                    .exact_score = 0.0f,
                    .timestamp = meta->timestamp,
                    .score = 0.0f
                };

                merge_results(candidates, &candidate_count, max_candidates * 2, &r, 1);
            }
        }
    }

    /* Exact match search */
    if (query->tokens && query->token_count > 0) {
        inverted_result_t inv_results[100];
        size_t inv_count = 0;

        mem_error_t err = inverted_index_search_any(engine->inverted,
                                                     query->tokens, query->token_count,
                                                     max_candidates, inv_results, &inv_count);
        if (err == MEM_OK) {
            for (size_t i = 0; i < inv_count; i++) {
                node_meta_t* meta = get_meta(engine, inv_results[i].doc_id);
                if (!meta) continue;

                if (meta->level < query->min_level || meta->level > query->max_level) {
                    continue;
                }

                search_match_t r = {
                    .node_id = inv_results[i].doc_id,
                    .level = meta->level,
                    .semantic_score = 0.0f,
                    .exact_score = inv_results[i].score,
                    .timestamp = meta->timestamp,
                    .score = 0.0f
                };

                merge_results(candidates, &candidate_count, max_candidates * 2, &r, 1);
            }
        }
    }

    /* Normalize exact scores */
    float max_exact = 0.0f;
    for (size_t i = 0; i < candidate_count; i++) {
        if (candidates[i].exact_score > max_exact) {
            max_exact = candidates[i].exact_score;
        }
    }
    if (max_exact > 0) {
        for (size_t i = 0; i < candidate_count; i++) {
            candidates[i].exact_score /= max_exact;
        }
    }

    /* Compute final scores: 0.6 * relevance + 0.3 * recency + 0.1 * level_boost */
    for (size_t i = 0; i < candidate_count; i++) {
        float relevance = engine->config.semantic_weight * candidates[i].semantic_score +
                          engine->config.exact_weight * candidates[i].exact_score;
        float recency = recency_score(candidates[i].timestamp, now);
        float level = level_boost(candidates[i].level);

        candidates[i].score = engine->config.relevance_weight * relevance +
                              engine->config.recency_weight * recency +
                              engine->config.level_weight * level;
    }

    qsort(candidates, candidate_count, sizeof(search_match_t), compare_results);

    size_t copy_count = candidate_count < query->k ? candidate_count : query->k;
    memcpy(results, candidates, copy_count * sizeof(search_match_t));
    *result_count = copy_count;

    free(candidates);
    return MEM_OK;
}

mem_error_t search_engine_semantic(search_engine_t* engine,
                                   const float* embedding, size_t k,
                                   search_match_t* results,
                                   size_t* result_count) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");
    MEM_CHECK_ERR(embedding != NULL, MEM_ERR_INVALID_ARG, "embedding is NULL");
    MEM_CHECK_ERR(results != NULL, MEM_ERR_INVALID_ARG, "results is NULL");
    MEM_CHECK_ERR(result_count != NULL, MEM_ERR_INVALID_ARG, "result_count is NULL");

    search_query_t query = {
        .embedding = embedding,
        .tokens = NULL,
        .token_count = 0,
        .k = k,
        .min_level = LEVEL_STATEMENT,
        .max_level = LEVEL_SESSION
    };

    return search_engine_search(engine, &query, results, result_count);
}

mem_error_t search_engine_exact(search_engine_t* engine,
                                const char** tokens, size_t token_count,
                                size_t k, search_match_t* results,
                                size_t* result_count) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");
    MEM_CHECK_ERR(tokens != NULL, MEM_ERR_INVALID_ARG, "tokens is NULL");
    MEM_CHECK_ERR(results != NULL, MEM_ERR_INVALID_ARG, "results is NULL");
    MEM_CHECK_ERR(result_count != NULL, MEM_ERR_INVALID_ARG, "result_count is NULL");

    search_query_t query = {
        .embedding = NULL,
        .tokens = tokens,
        .token_count = token_count,
        .k = k,
        .min_level = LEVEL_STATEMENT,
        .max_level = LEVEL_SESSION
    };

    return search_engine_search(engine, &query, results, result_count);
}

size_t search_engine_node_count(const search_engine_t* engine) {
    if (!engine) return 0;
    return engine->meta_count;
}

mem_error_t search_apply_budget(hierarchy_t* hierarchy,
                                search_match_t* results, size_t count,
                                size_t budget, size_t* final_count) {
    MEM_CHECK_ERR(hierarchy != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");
    MEM_CHECK_ERR(results != NULL, MEM_ERR_INVALID_ARG, "results is NULL");
    MEM_CHECK_ERR(final_count != NULL, MEM_ERR_INVALID_ARG, "final_count is NULL");

    size_t total_tokens = 0;
    *final_count = 0;

    for (size_t i = 0; i < count; i++) {
        size_t est_tokens;
        switch (results[i].level) {
            case LEVEL_STATEMENT: est_tokens = 50;   break;
            case LEVEL_BLOCK:     est_tokens = 200;  break;
            case LEVEL_MESSAGE:   est_tokens = 500;  break;
            case LEVEL_SESSION:   est_tokens = 1000; break;
            default:              est_tokens = 100;  break;
        }

        if (total_tokens + est_tokens > budget) {
            break;
        }

        total_tokens += est_tokens;
        (*final_count)++;
    }

    return MEM_OK;
}
