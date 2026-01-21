/*
 * SVC_MEM_TEST_0020 - Verify token budget truncation
 *
 * Test specification:
 * - Store many statements
 * - Query with max_tokens budget
 * - Response MUST fit within token budget
 * - Response MUST truncate results to fit
 */

#include "../test_framework.h"
#include "../../src/core/hierarchy.h"
#include "../../src/search/search.h"
#include "../../src/embedding/embedding.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_token_budget"

static void cleanup_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

static void setup_dir(void) {
    cleanup_dir(TEST_DIR);
    mkdir(TEST_DIR, 0755);

    char path[256];
    snprintf(path, sizeof(path), "%s/relations", TEST_DIR);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/embeddings", TEST_DIR);
    mkdir(path, 0755);
}

static void random_vector(float* vec, unsigned int seed) {
    srand(seed);
    float mag = 0.0f;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        vec[i] = (float)rand() / RAND_MAX - 0.5f;
        mag += vec[i] * vec[i];
    }
    mag = sqrtf(mag);
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        vec[i] /= mag;
    }
}

/*
 * TEST: Token budget truncates results
 */
TEST(token_budget_truncation) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 1000));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Store 100 statements */
    float query_vec[EMBEDDING_DIM];
    random_vector(query_vec, 42);

    for (int i = 0; i < 100; i++) {
        node_id_t stmt;
        ASSERT_OK(hierarchy_create_statement(h, block, &stmt));

        float vec[EMBEDDING_DIM];
        memcpy(vec, query_vec, sizeof(vec));
        /* Slightly perturb vector to create varied results */
        vec[i % EMBEDDING_DIM] += 0.01f * (i - 50);
        embedding_normalize(vec);

        const char* tokens[] = {"common", "token"};
        ASSERT_OK(search_engine_index(engine, stmt, vec, tokens, 2, 1000 + i));
    }

    /* Search should return many results */
    search_match_t results[100];
    size_t count = 0;

    ASSERT_OK(search_engine_semantic(engine, query_vec, 100, results, &count));
    ASSERT_GT(count, 10);  /* Many results */

    /* Apply budget: 150 tokens should allow ~3 statements (50 each) */
    size_t budget_count = 0;
    ASSERT_OK(search_apply_budget(h, results, count, 150, &budget_count));
    ASSERT_EQ(budget_count, 3);

    /* Apply budget: 1000 tokens should allow ~20 statements */
    ASSERT_OK(search_apply_budget(h, results, count, 1000, &budget_count));
    ASSERT_EQ(budget_count, 20);

    /* Apply budget: 0 tokens should allow 0 results */
    ASSERT_OK(search_apply_budget(h, results, count, 0, &budget_count));
    ASSERT_EQ(budget_count, 0);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Budget handles different hierarchy levels
 */
TEST(token_budget_levels) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message, block, stmt;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt));

    /* Create results with different levels */
    search_match_t results[4];
    results[0].level = LEVEL_STATEMENT;  /* 50 tokens */
    results[1].level = LEVEL_BLOCK;      /* 200 tokens */
    results[2].level = LEVEL_MESSAGE;    /* 500 tokens */
    results[3].level = LEVEL_SESSION;    /* 1000 tokens */

    size_t budget_count;

    /* Budget of 250 should fit statement + block */
    ASSERT_OK(search_apply_budget(h, results, 4, 250, &budget_count));
    ASSERT_EQ(budget_count, 2);

    /* Budget of 750 should fit statement + block + message */
    ASSERT_OK(search_apply_budget(h, results, 4, 750, &budget_count));
    ASSERT_EQ(budget_count, 3);

    /* Budget of 1750 should fit all */
    ASSERT_OK(search_apply_budget(h, results, 4, 1750, &budget_count));
    ASSERT_EQ(budget_count, 4);

    /* Budget of 49 should fit nothing (statement is 50) */
    ASSERT_OK(search_apply_budget(h, results, 4, 49, &budget_count));
    ASSERT_EQ(budget_count, 0);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
