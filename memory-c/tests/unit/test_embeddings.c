/*
 * Memory Service - Embeddings Storage Unit Tests
 */

#include "../test_framework.h"
#include "../../src/storage/embeddings.h"
#include "../../include/error.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>

static void cleanup_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

/* Test embeddings creation */
TEST(embeddings_create_basic) {
    const char* dir = "/tmp/test_embeddings_create";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    embeddings_store_t* store = NULL;
    mem_error_t err = embeddings_create(&store, dir, 1000);

    ASSERT_OK(err);
    ASSERT_NOT_NULL(store);

    /* All levels should start empty */
    for (int i = 0; i < LEVEL_COUNT; i++) {
        ASSERT_EQ(embeddings_count(store, (hierarchy_level_t)i), 0);
    }

    embeddings_close(store);
    cleanup_dir(dir);
}

/* Test embeddings alloc and set */
TEST(embeddings_alloc_and_set) {
    const char* dir = "/tmp/test_embeddings_alloc";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    embeddings_store_t* store = NULL;
    ASSERT_OK(embeddings_create(&store, dir, 100));

    /* Allocate embedding */
    uint32_t idx;
    ASSERT_OK(embeddings_alloc(store, LEVEL_STATEMENT, &idx));
    ASSERT_EQ(idx, 0);
    ASSERT_EQ(embeddings_count(store, LEVEL_STATEMENT), 1);

    /* Set embedding values */
    float values[EMBEDDING_DIM];
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        values[i] = (float)i / EMBEDDING_DIM;
    }
    ASSERT_OK(embeddings_set(store, LEVEL_STATEMENT, idx, values));

    /* Get and verify */
    const float* retrieved = embeddings_get(store, LEVEL_STATEMENT, idx);
    ASSERT_NOT_NULL(retrieved);
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        ASSERT_FLOAT_EQ(retrieved[i], values[i], 0.0001f);
    }

    embeddings_close(store);
    cleanup_dir(dir);
}

/* Test embeddings persistence */
TEST(embeddings_persistence) {
    const char* dir = "/tmp/test_embeddings_persist";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    float values[EMBEDDING_DIM];
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        values[i] = (float)i * 0.01f;
    }

    /* Create and store */
    {
        embeddings_store_t* store = NULL;
        ASSERT_OK(embeddings_create(&store, dir, 100));

        uint32_t idx;
        ASSERT_OK(embeddings_alloc(store, LEVEL_MESSAGE, &idx));
        ASSERT_OK(embeddings_set(store, LEVEL_MESSAGE, idx, values));

        ASSERT_OK(embeddings_sync(store));
        embeddings_close(store);
    }

    /* Reopen and verify */
    {
        embeddings_store_t* store = NULL;
        ASSERT_OK(embeddings_open(&store, dir));

        ASSERT_EQ(embeddings_count(store, LEVEL_MESSAGE), 1);

        const float* retrieved = embeddings_get(store, LEVEL_MESSAGE, 0);
        ASSERT_NOT_NULL(retrieved);
        for (int i = 0; i < EMBEDDING_DIM; i++) {
            ASSERT_FLOAT_EQ(retrieved[i], values[i], 0.0001f);
        }

        embeddings_close(store);
    }

    cleanup_dir(dir);
}

/* Test cosine similarity */
TEST(embeddings_similarity) {
    const char* dir = "/tmp/test_embeddings_sim";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    embeddings_store_t* store = NULL;
    ASSERT_OK(embeddings_create(&store, dir, 100));

    /* Create two embeddings */
    uint32_t idx1, idx2;
    ASSERT_OK(embeddings_alloc(store, LEVEL_STATEMENT, &idx1));
    ASSERT_OK(embeddings_alloc(store, LEVEL_STATEMENT, &idx2));

    /* Same vector -> similarity = 1.0 */
    float v1[EMBEDDING_DIM];
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        v1[i] = 1.0f;
    }
    ASSERT_OK(embeddings_set(store, LEVEL_STATEMENT, idx1, v1));
    ASSERT_OK(embeddings_set(store, LEVEL_STATEMENT, idx2, v1));

    float sim = embeddings_similarity(store, LEVEL_STATEMENT, idx1, idx2);
    ASSERT_FLOAT_EQ(sim, 1.0f, 0.001f);

    /* Orthogonal vectors */
    float v2[EMBEDDING_DIM];
    memset(v2, 0, sizeof(v2));
    v2[0] = 1.0f;

    float v3[EMBEDDING_DIM];
    memset(v3, 0, sizeof(v3));
    v3[1] = 1.0f;

    ASSERT_OK(embeddings_set(store, LEVEL_STATEMENT, idx1, v2));
    ASSERT_OK(embeddings_set(store, LEVEL_STATEMENT, idx2, v3));

    sim = embeddings_similarity(store, LEVEL_STATEMENT, idx1, idx2);
    ASSERT_FLOAT_EQ(sim, 0.0f, 0.001f);

    embeddings_close(store);
    cleanup_dir(dir);
}

/* Test similarity with query vector */
TEST(embeddings_similarity_vec) {
    const char* dir = "/tmp/test_embeddings_sim_vec";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    embeddings_store_t* store = NULL;
    ASSERT_OK(embeddings_create(&store, dir, 100));

    uint32_t idx;
    ASSERT_OK(embeddings_alloc(store, LEVEL_STATEMENT, &idx));

    float stored[EMBEDDING_DIM];
    float query[EMBEDDING_DIM];
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        stored[i] = (float)i;
        query[i] = (float)i;
    }
    ASSERT_OK(embeddings_set(store, LEVEL_STATEMENT, idx, stored));

    float sim = embeddings_similarity_vec(store, LEVEL_STATEMENT, idx, query);
    ASSERT_FLOAT_EQ(sim, 1.0f, 0.001f);

    embeddings_close(store);
    cleanup_dir(dir);
}

/* Test multiple levels */
TEST(embeddings_multiple_levels) {
    const char* dir = "/tmp/test_embeddings_levels";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    embeddings_store_t* store = NULL;
    ASSERT_OK(embeddings_create(&store, dir, 100));

    /* Allocate in different levels */
    uint32_t stmt_idx, block_idx, msg_idx, sess_idx;
    ASSERT_OK(embeddings_alloc(store, LEVEL_STATEMENT, &stmt_idx));
    ASSERT_OK(embeddings_alloc(store, LEVEL_BLOCK, &block_idx));
    ASSERT_OK(embeddings_alloc(store, LEVEL_MESSAGE, &msg_idx));
    ASSERT_OK(embeddings_alloc(store, LEVEL_SESSION, &sess_idx));

    ASSERT_EQ(embeddings_count(store, LEVEL_STATEMENT), 1);
    ASSERT_EQ(embeddings_count(store, LEVEL_BLOCK), 1);
    ASSERT_EQ(embeddings_count(store, LEVEL_MESSAGE), 1);
    ASSERT_EQ(embeddings_count(store, LEVEL_SESSION), 1);

    embeddings_close(store);
    cleanup_dir(dir);
}

/* Test copy function */
TEST(embeddings_copy) {
    const char* dir = "/tmp/test_embeddings_copy";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    embeddings_store_t* store = NULL;
    ASSERT_OK(embeddings_create(&store, dir, 100));

    uint32_t idx;
    ASSERT_OK(embeddings_alloc(store, LEVEL_STATEMENT, &idx));

    float original[EMBEDDING_DIM];
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        original[i] = (float)i * 0.5f;
    }
    ASSERT_OK(embeddings_set(store, LEVEL_STATEMENT, idx, original));

    float buf[EMBEDDING_DIM];
    ASSERT_OK(embeddings_copy(store, LEVEL_STATEMENT, idx, buf));

    for (int i = 0; i < EMBEDDING_DIM; i++) {
        ASSERT_FLOAT_EQ(buf[i], original[i], 0.0001f);
    }

    embeddings_close(store);
    cleanup_dir(dir);
}

/* Test invalid arguments */
TEST(embeddings_invalid_args) {
    embeddings_store_t* store = NULL;

    ASSERT_EQ(embeddings_create(NULL, "/tmp/x", 100), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(embeddings_create(&store, NULL, 100), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(embeddings_create(&store, "/tmp/x", 0), MEM_ERR_INVALID_ARG);

    uint32_t idx;
    ASSERT_EQ(embeddings_alloc(NULL, LEVEL_STATEMENT, &idx), MEM_ERR_INVALID_ARG);

    ASSERT_NULL(embeddings_get(NULL, LEVEL_STATEMENT, 0));
    ASSERT_EQ(embeddings_count(NULL, LEVEL_STATEMENT), 0);
}

TEST_MAIN()
