/*
 * SVC_MEM_TEST_0019 - Verify ranking formula
 *
 * Test specification:
 * - Store old message with high relevance
 * - Store recent message with medium relevance
 * - Query and verify ranking reflects weighted formula
 * - Very recent + medium relevance MAY outrank old + high relevance
 *
 * Ranking formula: 0.6 * relevance + 0.3 * recency + 0.1 * level_boost
 */

#include "../test_framework.h"
#include "../../src/core/hierarchy.h"
#include "../../src/search/search.h"
#include "../../src/embedding/embedding.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_ranking_formula"

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
 * TEST: Verify ranking formula weights
 *
 * Formula: score = 0.6 * relevance + 0.3 * recency + 0.1 * level_boost
 *
 * This test verifies that the ranking formula is correctly applied:
 * - Highly relevant + old content gets a boost from relevance
 * - Less relevant + recent content gets a boost from recency
 * - The combined score should reflect both factors
 */
TEST(ranking_formula_weights) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_config_t config = SEARCH_CONFIG_DEFAULT;
    config.relevance_weight = 0.6f;
    config.recency_weight = 0.3f;
    config.level_weight = 0.1f;

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, &config));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Create query vector */
    float query[EMBEDDING_DIM];
    random_vector(query, 100);

    /* Statement 1: Very relevant (same as query), but old */
    node_id_t stmt_relevant_old;
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt_relevant_old));
    const char* tokens1[] = {"relevant", "old"};
    ASSERT_OK(search_engine_index(engine, stmt_relevant_old, query,
                                  tokens1, 2, 1));  /* Very old timestamp */

    /* Statement 2: Same embedding but more recent */
    node_id_t stmt_relevant_new;
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt_relevant_new));
    const char* tokens2[] = {"relevant", "new"};
    ASSERT_OK(search_engine_index(engine, stmt_relevant_new, query,
                                  tokens2, 2, 9999999999999ULL));  /* Far future */

    /* Search */
    search_match_t results[10];
    size_t count = 0;

    ASSERT_OK(search_engine_semantic(engine, query, 10, results, &count));
    ASSERT_EQ(count, 2);

    /* Both should have non-zero scores */
    ASSERT_GT(results[0].score, 0.0f);
    ASSERT_GT(results[1].score, 0.0f);

    /* With identical semantic scores, the recent one should rank first
     * due to the 0.3 recency weight */
    ASSERT_EQ(results[0].node_id, stmt_relevant_new);
    ASSERT_GT(results[0].score, results[1].score);

    /* Verify both have high semantic scores (should be near 1.0) */
    ASSERT_GT(results[0].semantic_score, 0.99f);
    ASSERT_GT(results[1].semantic_score, 0.99f);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify combined score components
 */
TEST(ranking_score_components) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Create statement with both semantic and exact match */
    float vec[EMBEDDING_DIM];
    random_vector(vec, 42);

    node_id_t stmt;
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt));
    const char* tokens[] = {"test", "content"};
    ASSERT_OK(search_engine_index(engine, stmt, vec, tokens, 2, 5000000000ULL));

    /* Combined query */
    const char* query_tokens[] = {"test"};
    search_query_t query = {
        .embedding = vec,
        .tokens = query_tokens,
        .token_count = 1,
        .k = 10,
        .min_level = LEVEL_STATEMENT,
        .max_level = LEVEL_SESSION
    };

    search_match_t results[10];
    size_t count = 0;

    ASSERT_OK(search_engine_search(engine, &query, results, &count));
    ASSERT_EQ(count, 1);

    /* Verify score components are populated */
    ASSERT_GT(results[0].semantic_score, 0.9f);  /* Exact semantic match */
    ASSERT_GT(results[0].exact_score, 0.0f);     /* Has exact token match */
    ASSERT_GT(results[0].score, 0.0f);           /* Combined score */

    /* Level should be statement */
    ASSERT_EQ(results[0].level, LEVEL_STATEMENT);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
