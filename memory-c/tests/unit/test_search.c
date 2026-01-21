/*
 * Unit tests for unified search module
 */

#include "../test_framework.h"
#include "../../src/search/search.h"
#include "../../src/core/hierarchy.h"
#include "../../src/embedding/embedding.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_search"

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

/* Helper: Create a normalized random vector */
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

/* Test basic creation and destruction */
TEST(search_create_destroy) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    search_config_t config = SEARCH_CONFIG_DEFAULT;

    ASSERT_OK(search_engine_create(&engine, h, &config));
    ASSERT_NOT_NULL(engine);
    ASSERT_EQ(search_engine_node_count(engine), 0);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test creation with default config */
TEST(search_create_default) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));
    ASSERT_NOT_NULL(engine);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test indexing nodes */
TEST(search_index_nodes) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));

    /* Create hierarchy */
    node_id_t session, message, block, stmt;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt));

    /* Index statement */
    float vec[EMBEDDING_DIM];
    random_vector(vec, 42);
    const char* tokens[] = {"hello", "world"};

    ASSERT_OK(search_engine_index(engine, stmt, vec, tokens, 2, 1000));
    ASSERT_EQ(search_engine_node_count(engine), 1);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test semantic search */
TEST(search_semantic) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));

    /* Create statements with different vectors */
    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    float vecs[5][EMBEDDING_DIM];
    node_id_t stmts[5];

    for (int i = 0; i < 5; i++) {
        random_vector(vecs[i], i);
        ASSERT_OK(hierarchy_create_statement(h, block, &stmts[i]));
        ASSERT_OK(search_engine_index(engine, stmts[i], vecs[i], NULL, 0, 1000 + i));
    }

    /* Search for vector similar to stmt 2 */
    float query[EMBEDDING_DIM];
    memcpy(query, vecs[2], sizeof(query));

    search_match_t results[10];
    size_t count = 0;

    ASSERT_OK(search_engine_semantic(engine, query, 5, results, &count));
    ASSERT_GT(count, 0);

    /* First result should be stmt 2 (exact match) */
    ASSERT_EQ(results[0].node_id, stmts[2]);
    ASSERT_GT(results[0].semantic_score, 0.99f);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test exact match search */
TEST(search_exact) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Index statements with different tokens */
    node_id_t stmt1, stmt2, stmt3;
    float vec[EMBEDDING_DIM];
    random_vector(vec, 42);

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt1));
    const char* tokens1[] = {"hello", "world"};
    ASSERT_OK(search_engine_index(engine, stmt1, vec, tokens1, 2, 1000));

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt2));
    const char* tokens2[] = {"hello", "everyone"};
    ASSERT_OK(search_engine_index(engine, stmt2, vec, tokens2, 2, 1001));

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt3));
    const char* tokens3[] = {"goodbye", "world"};
    ASSERT_OK(search_engine_index(engine, stmt3, vec, tokens3, 2, 1002));

    /* Search for "hello" */
    const char* query[] = {"hello"};
    search_match_t results[10];
    size_t count = 0;

    ASSERT_OK(search_engine_exact(engine, query, 1, 10, results, &count));
    ASSERT_EQ(count, 2);  /* stmt1 and stmt2 contain "hello" */

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test combined search */
TEST(search_combined) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Create statements */
    node_id_t stmt1, stmt2;
    float vec1[EMBEDDING_DIM], vec2[EMBEDDING_DIM];
    random_vector(vec1, 1);
    random_vector(vec2, 2);

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt1));
    const char* tokens1[] = {"machine", "learning"};
    ASSERT_OK(search_engine_index(engine, stmt1, vec1, tokens1, 2, 1000));

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt2));
    const char* tokens2[] = {"deep", "learning"};
    ASSERT_OK(search_engine_index(engine, stmt2, vec2, tokens2, 2, 1001));

    /* Combined search */
    const char* query_tokens[] = {"learning"};
    search_query_t query = {
        .embedding = vec1,  /* Similar to stmt1 */
        .tokens = query_tokens,
        .token_count = 1,
        .k = 10,
        .min_level = LEVEL_STATEMENT,
        .max_level = LEVEL_SESSION
    };

    search_match_t results[10];
    size_t count = 0;

    ASSERT_OK(search_engine_search(engine, &query, results, &count));
    ASSERT_EQ(count, 2);

    /* Both should have scores (semantic + exact) */
    ASSERT_GT(results[0].score, 0.0f);
    ASSERT_GT(results[1].score, 0.0f);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test ranking formula */
TEST(search_ranking) {
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

    /* Create two statements with same embedding but different timestamps */
    float vec[EMBEDDING_DIM];
    random_vector(vec, 42);

    node_id_t stmt_old, stmt_new;
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt_old));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt_new));

    const char* tokens[] = {"test"};
    ASSERT_OK(search_engine_index(engine, stmt_old, vec, tokens, 1, 1000));
    ASSERT_OK(search_engine_index(engine, stmt_new, vec, tokens, 1, 9999999999ULL));  /* Very recent */

    /* Search */
    search_match_t results[10];
    size_t count = 0;
    ASSERT_OK(search_engine_semantic(engine, vec, 10, results, &count));
    ASSERT_EQ(count, 2);

    /* Newer statement should rank higher due to recency */
    ASSERT_EQ(results[0].node_id, stmt_new);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test token budget */
TEST(search_token_budget) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Create results array */
    search_match_t results[10];
    for (int i = 0; i < 10; i++) {
        results[i].level = LEVEL_STATEMENT;  /* 50 tokens each */
    }

    size_t final_count = 0;

    /* Budget of 150 should allow 3 statements */
    ASSERT_OK(search_apply_budget(h, results, 10, 150, &final_count));
    ASSERT_EQ(final_count, 3);

    /* Budget of 50 should allow 1 statement */
    ASSERT_OK(search_apply_budget(h, results, 10, 50, &final_count));
    ASSERT_EQ(final_count, 1);

    /* Budget of 0 should allow 0 statements */
    ASSERT_OK(search_apply_budget(h, results, 10, 0, &final_count));
    ASSERT_EQ(final_count, 0);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test remove operation */
TEST(search_remove) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));

    node_id_t session, message, block, stmt;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt));

    float vec[EMBEDDING_DIM];
    random_vector(vec, 42);
    const char* tokens[] = {"test"};

    ASSERT_OK(search_engine_index(engine, stmt, vec, tokens, 1, 1000));
    ASSERT_EQ(search_engine_node_count(engine), 1);

    /* Remove */
    ASSERT_OK(search_engine_remove(engine, stmt));

    /* Search should find nothing */
    search_match_t results[10];
    size_t count = 99;
    ASSERT_OK(search_engine_semantic(engine, vec, 10, results, &count));
    ASSERT_EQ(count, 0);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test invalid arguments */
TEST(search_invalid_args) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    search_match_t results[10];
    size_t count;

    /* NULL pointer */
    ASSERT_NE(search_engine_create(NULL, h, NULL), MEM_OK);
    ASSERT_NE(search_engine_create(&engine, NULL, NULL), MEM_OK);

    ASSERT_OK(search_engine_create(&engine, h, NULL));

    float vec[EMBEDDING_DIM];
    const char* tokens[] = {"test"};

    /* NULL arguments */
    ASSERT_NE(search_engine_index(NULL, 0, vec, tokens, 1, 0), MEM_OK);
    ASSERT_NE(search_engine_index(engine, 0, NULL, tokens, 1, 0), MEM_OK);

    ASSERT_NE(search_engine_semantic(NULL, vec, 10, results, &count), MEM_OK);
    ASSERT_NE(search_engine_semantic(engine, NULL, 10, results, &count), MEM_OK);
    ASSERT_NE(search_engine_semantic(engine, vec, 10, NULL, &count), MEM_OK);
    ASSERT_NE(search_engine_semantic(engine, vec, 10, results, NULL), MEM_OK);

    /* NULL graceful handling */
    ASSERT_EQ(search_engine_node_count(NULL), 0);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
