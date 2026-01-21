/*
 * SVC_MEM_TEST_0006 - Verify inverted index exact match
 *
 * Test specification:
 * - Store code with identifiers
 * - Query for exact identifier
 * - Inverted index MUST return exact match
 * - Semantic search MUST also be performed in parallel
 * - Results MUST be merged and deduplicated
 */

#include "../test_framework.h"
#include "../../src/core/hierarchy.h"
#include "../../src/search/search.h"
#include "../../src/embedding/embedding.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_inverted_index_match"

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
 * TEST: Exact identifier match via inverted index
 */
TEST(inverted_index_exact_match) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* engine = NULL;
    ASSERT_OK(search_engine_create(&engine, h, NULL));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Store statements with different identifiers */
    node_id_t stmt1, stmt2, stmt3;
    float emb1[EMBEDDING_DIM], emb2[EMBEDDING_DIM], emb3[EMBEDDING_DIM];
    random_vector(emb1, 1);
    random_vector(emb2, 2);
    random_vector(emb3, 3);

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt1));
    const char* tokens1[] = {"func", "handleauth", "token", "string", "error"};
    ASSERT_OK(search_engine_index(engine, stmt1, emb1, tokens1, 5, 1000));

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt2));
    const char* tokens2[] = {"func", "processrequest", "req", "response"};
    ASSERT_OK(search_engine_index(engine, stmt2, emb2, tokens2, 4, 1001));

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt3));
    const char* tokens3[] = {"func", "validatetoken", "token", "bool"};
    ASSERT_OK(search_engine_index(engine, stmt3, emb3, tokens3, 4, 1002));

    /* Query for exact identifier "handleauth" */
    const char* query_tokens[] = {"handleauth"};
    search_match_t results[10];
    size_t count = 0;

    ASSERT_OK(search_engine_exact(engine, query_tokens, 1, 10, results, &count));

    /* Should find exactly one match: stmt1 */
    ASSERT_EQ(count, 1);
    ASSERT_EQ(results[0].node_id, stmt1);
    ASSERT_GT(results[0].exact_score, 0.0f);

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Merge semantic and exact search results
 */
TEST(merged_search_results) {
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
    node_id_t stmt1, stmt2, stmt3;
    float emb1[EMBEDDING_DIM], emb2[EMBEDDING_DIM], emb3[EMBEDDING_DIM];
    random_vector(emb1, 1);
    random_vector(emb2, 2);
    random_vector(emb3, 3);

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt1));
    const char* tokens1[] = {"authentication", "oauth", "token"};
    ASSERT_OK(search_engine_index(engine, stmt1, emb1, tokens1, 3, 1000));

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt2));
    const char* tokens2[] = {"authorization", "oauth", "refresh"};
    ASSERT_OK(search_engine_index(engine, stmt2, emb2, tokens2, 3, 1001));

    ASSERT_OK(hierarchy_create_statement(h, block, &stmt3));
    const char* tokens3[] = {"database", "connection", "pool"};
    ASSERT_OK(search_engine_index(engine, stmt3, emb3, tokens3, 3, 1002));

    /* Combined query: semantic (embedding similar to stmt1) + exact ("oauth") */
    const char* query_tokens[] = {"oauth"};
    search_query_t query = {
        .embedding = emb1,  /* Similar to stmt1 */
        .tokens = query_tokens,
        .token_count = 1,
        .k = 10,
        .min_level = LEVEL_STATEMENT,
        .max_level = LEVEL_SESSION
    };

    search_match_t results[10];
    size_t count = 0;

    ASSERT_OK(search_engine_search(engine, &query, results, &count));

    /* Should find stmt1 and stmt2 (both have "oauth" token) */
    ASSERT_GE(count, 2);

    /* stmt1 should rank first (high semantic + high exact match) */
    ASSERT_EQ(results[0].node_id, stmt1);

    /* Both semantic and exact scores should be non-zero for stmt1 */
    ASSERT_GT(results[0].semantic_score, 0.0f);
    ASSERT_GT(results[0].exact_score, 0.0f);

    /* Verify no duplicates in results */
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            ASSERT_NE(results[i].node_id, results[j].node_id);
        }
    }

    search_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
