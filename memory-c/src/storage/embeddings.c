/*
 * Memory Service - Embeddings Storage Implementation
 */

#include "embeddings.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>

/* File name format */
#define LEVEL_FILE_FMT "%s/level_%d.bin"

/* Header stored at beginning of each level file */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t dim;
    uint32_t count;
    uint32_t capacity;
    uint32_t reserved[3];
} embedding_file_header_t;

#define EMBEDDING_MAGIC 0x454D4230  /* "EMB0" */
#define EMBEDDING_VERSION 1
#define HEADER_SIZE sizeof(embedding_file_header_t)

/* Calculate file size for capacity - returns 0 on overflow */
static size_t calc_file_size(size_t capacity) {
    /* Check for integer overflow before multiplication */
    size_t embedding_bytes = EMBEDDING_DIM * sizeof(float);
    if (capacity > (SIZE_MAX - HEADER_SIZE) / embedding_bytes) {
        return 0;  /* Overflow would occur */
    }
    return HEADER_SIZE + capacity * embedding_bytes;
}

/* Get level file path */
static void get_level_path(char* buf, size_t buflen, const char* dir, int level) {
    snprintf(buf, buflen, LEVEL_FILE_FMT, dir, level);
}

/* Initialize single level */
static mem_error_t init_level(embedding_level_t* lev, const char* dir,
                              hierarchy_level_t level, size_t capacity, bool create) {
    char path[PATH_MAX];
    get_level_path(path, sizeof(path), dir, level);

    if (create) {
        size_t file_size = calc_file_size(capacity);
        if (file_size == 0) {
            MEM_RETURN_ERROR(MEM_ERR_OVERFLOW, "capacity %zu would cause integer overflow", capacity);
        }
        MEM_CHECK(arena_create_mmap(&lev->arena, path, file_size, 0));

        /* Write header */
        embedding_file_header_t* hdr = arena_alloc(lev->arena, HEADER_SIZE);
        MEM_CHECK_ALLOC(hdr);

        hdr->magic = EMBEDDING_MAGIC;
        hdr->version = EMBEDDING_VERSION;
        hdr->dim = EMBEDDING_DIM;
        hdr->count = 0;
        hdr->capacity = (uint32_t)capacity;

        lev->count = 0;
        lev->capacity = capacity;
    } else {
        MEM_CHECK(arena_open_mmap(&lev->arena, path, 0));

        /* Validate header */
        embedding_file_header_t* hdr = arena_get_ptr(lev->arena, 0);
        if (!hdr || hdr->magic != EMBEDDING_MAGIC) {
            arena_destroy(lev->arena);
            lev->arena = NULL;
            MEM_RETURN_ERROR(MEM_ERR_INDEX_CORRUPT, "invalid embedding file level_%d.bin", level);
        }

        if (hdr->dim != EMBEDDING_DIM) {
            arena_destroy(lev->arena);
            lev->arena = NULL;
            MEM_RETURN_ERROR(MEM_ERR_INDEX_CORRUPT,
                           "dimension mismatch: expected %d, got %d",
                           EMBEDDING_DIM, hdr->dim);
        }

        lev->count = hdr->count;
        lev->capacity = hdr->capacity;
    }

    lev->level = level;
    return MEM_OK;
}

mem_error_t embeddings_create(embeddings_store_t** store, const char* dir,
                              size_t initial_capacity) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(dir != NULL, MEM_ERR_INVALID_ARG, "dir is NULL");
    MEM_CHECK_ERR(initial_capacity > 0, MEM_ERR_INVALID_ARG, "capacity must be > 0");

    embeddings_store_t* s = calloc(1, sizeof(embeddings_store_t));
    MEM_CHECK_ALLOC(s);

    s->base_dir = strdup(dir);
    if (!s->base_dir) {
        free(s);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate dir path");
    }

    /* Initialize each level */
    for (int i = 0; i < LEVEL_COUNT; i++) {
        mem_error_t err = init_level(&s->levels[i], dir, (hierarchy_level_t)i,
                                     initial_capacity, true);
        if (err != MEM_OK) {
            /* Cleanup already initialized levels */
            for (int j = 0; j < i; j++) {
                if (s->levels[j].arena) {
                    arena_destroy(s->levels[j].arena);
                }
            }
            free(s->base_dir);
            free(s);
            return err;
        }
    }

    *store = s;
    LOG_INFO("Embeddings store created at %s with capacity %zu per level",
             dir, initial_capacity);
    return MEM_OK;
}

mem_error_t embeddings_open(embeddings_store_t** store, const char* dir) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(dir != NULL, MEM_ERR_INVALID_ARG, "dir is NULL");

    embeddings_store_t* s = calloc(1, sizeof(embeddings_store_t));
    MEM_CHECK_ALLOC(s);

    s->base_dir = strdup(dir);
    if (!s->base_dir) {
        free(s);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate dir path");
    }

    /* Open each level */
    for (int i = 0; i < LEVEL_COUNT; i++) {
        mem_error_t err = init_level(&s->levels[i], dir, (hierarchy_level_t)i, 0, false);
        if (err != MEM_OK) {
            for (int j = 0; j < i; j++) {
                if (s->levels[j].arena) {
                    arena_destroy(s->levels[j].arena);
                }
            }
            free(s->base_dir);
            free(s);
            return err;
        }
    }

    *store = s;
    LOG_INFO("Embeddings store opened at %s", dir);
    return MEM_OK;
}

mem_error_t embeddings_alloc(embeddings_store_t* store, hierarchy_level_t level,
                             uint32_t* idx) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(level < LEVEL_COUNT, MEM_ERR_INVALID_LEVEL, "invalid level");
    MEM_CHECK_ERR(idx != NULL, MEM_ERR_INVALID_ARG, "idx is NULL");

    embedding_level_t* lev = &store->levels[level];

    if (lev->count >= lev->capacity) {
        /* TODO: Grow the arena */
        MEM_RETURN_ERROR(MEM_ERR_FULL, "embedding level %d at capacity", level);
    }

    *idx = (uint32_t)lev->count;
    lev->count++;

    /* Update header count */
    embedding_file_header_t* hdr = arena_get_ptr(lev->arena, 0);
    if (hdr) {
        hdr->count = (uint32_t)lev->count;
    }

    return MEM_OK;
}

mem_error_t embeddings_set(embeddings_store_t* store, hierarchy_level_t level,
                           uint32_t idx, const float* values) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(level < LEVEL_COUNT, MEM_ERR_INVALID_LEVEL, "invalid level");
    MEM_CHECK_ERR(values != NULL, MEM_ERR_INVALID_ARG, "values is NULL");

    embedding_level_t* lev = &store->levels[level];

    if (idx >= lev->count) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "embedding index %u not allocated", idx);
    }

    /* Calculate offset */
    size_t offset = HEADER_SIZE + idx * EMBEDDING_DIM * sizeof(float);
    float* dest = arena_get_ptr(lev->arena, offset);

    if (!dest) {
        MEM_RETURN_ERROR(MEM_ERR_INDEX, "failed to get embedding pointer");
    }

    memcpy(dest, values, EMBEDDING_DIM * sizeof(float));
    return MEM_OK;
}

const float* embeddings_get(const embeddings_store_t* store,
                            hierarchy_level_t level, uint32_t idx) {
    if (!store || level >= LEVEL_COUNT) return NULL;

    const embedding_level_t* lev = &store->levels[level];
    if (idx >= lev->count) return NULL;

    size_t offset = HEADER_SIZE + idx * EMBEDDING_DIM * sizeof(float);
    return arena_get_ptr(lev->arena, offset);
}

mem_error_t embeddings_copy(const embeddings_store_t* store,
                            hierarchy_level_t level, uint32_t idx,
                            float* buf) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(buf != NULL, MEM_ERR_INVALID_ARG, "buf is NULL");

    const float* src = embeddings_get(store, level, idx);
    if (!src) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "embedding not found");
    }

    memcpy(buf, src, EMBEDDING_DIM * sizeof(float));
    return MEM_OK;
}

/* SIMD-optimized dot product (fallback for non-SIMD) */
static float dot_product(const float* a, const float* b, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}

/* Vector magnitude */
static float magnitude(const float* v, size_t n) {
    return sqrtf(dot_product(v, v, n));
}

float embeddings_similarity(const embeddings_store_t* store,
                            hierarchy_level_t level,
                            uint32_t idx1, uint32_t idx2) {
    const float* v1 = embeddings_get(store, level, idx1);
    const float* v2 = embeddings_get(store, level, idx2);

    if (!v1 || !v2) return 0.0f;

    float dot = dot_product(v1, v2, EMBEDDING_DIM);
    float mag1 = magnitude(v1, EMBEDDING_DIM);
    float mag2 = magnitude(v2, EMBEDDING_DIM);

    if (mag1 == 0.0f || mag2 == 0.0f) return 0.0f;

    return dot / (mag1 * mag2);
}

float embeddings_similarity_vec(const embeddings_store_t* store,
                                hierarchy_level_t level, uint32_t idx,
                                const float* query) {
    if (!query) return 0.0f;

    const float* v = embeddings_get(store, level, idx);
    if (!v) return 0.0f;

    float dot = dot_product(v, query, EMBEDDING_DIM);
    float mag_v = magnitude(v, EMBEDDING_DIM);
    float mag_q = magnitude(query, EMBEDDING_DIM);

    if (mag_v == 0.0f || mag_q == 0.0f) return 0.0f;

    return dot / (mag_v * mag_q);
}

size_t embeddings_count(const embeddings_store_t* store, hierarchy_level_t level) {
    if (!store || level >= LEVEL_COUNT) return 0;
    return store->levels[level].count;
}

mem_error_t embeddings_sync(embeddings_store_t* store) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");

    for (int i = 0; i < LEVEL_COUNT; i++) {
        if (store->levels[i].arena) {
            MEM_CHECK(arena_sync(store->levels[i].arena));
        }
    }

    return MEM_OK;
}

void embeddings_close(embeddings_store_t* store) {
    if (!store) return;

    for (int i = 0; i < LEVEL_COUNT; i++) {
        if (store->levels[i].arena) {
            arena_sync(store->levels[i].arena);
            arena_destroy(store->levels[i].arena);
        }
    }

    free(store->base_dir);
    free(store);
}
