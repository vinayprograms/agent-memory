/*
 * Unit tests for hierarchical embedding pooling
 */

#include "../test_framework.h"
#include "../../src/embedding/pooling.h"
#include "../../src/core/hierarchy.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_pooling"

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

/* Test aggregating children embeddings into parent */
TEST(pooling_aggregate_children) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create hierarchy: session -> message -> block -> 3 statements */
    node_id_t session, message, block;
    node_id_t stmts[3];

    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    for (int i = 0; i < 3; i++) {
        ASSERT_OK(hierarchy_create_statement(h, block, &stmts[i]));
    }

    /* Set statement embeddings (simple orthogonal vectors) */
    float emb1[EMBEDDING_DIM] = {0};
    float emb2[EMBEDDING_DIM] = {0};
    float emb3[EMBEDDING_DIM] = {0};

    emb1[0] = 1.0f;
    emb2[1] = 1.0f;
    emb3[2] = 1.0f;

    ASSERT_OK(hierarchy_set_embedding(h, stmts[0], emb1));
    ASSERT_OK(hierarchy_set_embedding(h, stmts[1], emb2));
    ASSERT_OK(hierarchy_set_embedding(h, stmts[2], emb3));

    /* Aggregate into block */
    ASSERT_OK(pooling_aggregate_children(h, block));

    /* Check block embedding is mean of children (normalized) */
    const float* block_emb = hierarchy_get_embedding(h, block);
    ASSERT_NOT_NULL(block_emb);

    /* Expected: (1/sqrt(3), 1/sqrt(3), 1/sqrt(3), 0, ...) */
    float expected = 1.0f / sqrtf(3.0f);
    ASSERT_FLOAT_EQ(block_emb[0], expected, 0.01f);
    ASSERT_FLOAT_EQ(block_emb[1], expected, 0.01f);
    ASSERT_FLOAT_EQ(block_emb[2], expected, 0.01f);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test propagating embeddings up through hierarchy */
TEST(pooling_propagate_session) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create hierarchy:
     * session
     *   └── message
     *         ├── block1
     *         │     ├── stmt1 (1,0,0,...)
     *         │     └── stmt2 (0,1,0,...)
     *         └── block2
     *               └── stmt3 (0,0,1,...)
     */
    node_id_t session, message, block1, block2;
    node_id_t stmt1, stmt2, stmt3;

    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block1));
    ASSERT_OK(hierarchy_create_block(h, message, &block2));
    ASSERT_OK(hierarchy_create_statement(h, block1, &stmt1));
    ASSERT_OK(hierarchy_create_statement(h, block1, &stmt2));
    ASSERT_OK(hierarchy_create_statement(h, block2, &stmt3));

    /* Set leaf embeddings */
    float emb1[EMBEDDING_DIM] = {0}; emb1[0] = 1.0f;
    float emb2[EMBEDDING_DIM] = {0}; emb2[1] = 1.0f;
    float emb3[EMBEDDING_DIM] = {0}; emb3[2] = 1.0f;

    ASSERT_OK(hierarchy_set_embedding(h, stmt1, emb1));
    ASSERT_OK(hierarchy_set_embedding(h, stmt2, emb2));
    ASSERT_OK(hierarchy_set_embedding(h, stmt3, emb3));

    /* Propagate up to session */
    ASSERT_OK(pooling_propagate_session(h, session));

    /* Check block1 embedding = mean(stmt1, stmt2) */
    const float* b1_emb = hierarchy_get_embedding(h, block1);
    ASSERT_NOT_NULL(b1_emb);
    float expected_b1 = 1.0f / sqrtf(2.0f);  /* (1,1,0,...) normalized */
    ASSERT_FLOAT_EQ(b1_emb[0], expected_b1, 0.01f);
    ASSERT_FLOAT_EQ(b1_emb[1], expected_b1, 0.01f);

    /* Check block2 embedding = stmt3 (only child) */
    const float* b2_emb = hierarchy_get_embedding(h, block2);
    ASSERT_NOT_NULL(b2_emb);
    ASSERT_FLOAT_EQ(b2_emb[2], 1.0f, 0.01f);

    /* Check message embedding = mean(block1, block2) */
    const float* msg_emb = hierarchy_get_embedding(h, message);
    ASSERT_NOT_NULL(msg_emb);

    /* Message has pooled from 2 blocks:
     * block1 = (0.707, 0.707, 0, ...)
     * block2 = (0, 0, 1, ...)
     * mean = (0.354, 0.354, 0.5, ...) then normalized
     */
    float mag = sqrtf(msg_emb[0]*msg_emb[0] + msg_emb[1]*msg_emb[1] + msg_emb[2]*msg_emb[2]);
    ASSERT_FLOAT_EQ(mag, 1.0f, 0.01f);  /* Should be normalized */

    /* Session should also be pooled */
    const float* sess_emb = hierarchy_get_embedding(h, session);
    ASSERT_NOT_NULL(sess_emb);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test embedding a complete message */
TEST(pooling_embed_message) {
    setup_dir();

    embedding_engine_t* engine = NULL;
    ASSERT_OK(embedding_engine_create(&engine, NULL));

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create hierarchy */
    node_id_t session, message, block1, block2;
    node_id_t stmt1, stmt2, stmt3;

    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block1));
    ASSERT_OK(hierarchy_create_block(h, message, &block2));
    ASSERT_OK(hierarchy_create_statement(h, block1, &stmt1));
    ASSERT_OK(hierarchy_create_statement(h, block1, &stmt2));
    ASSERT_OK(hierarchy_create_statement(h, block2, &stmt3));

    /* Text for each statement */
    const char* texts[] = {
        "First statement in block one.",
        "Second statement in block one.",
        "Statement in block two."
    };
    size_t lens[] = {
        strlen(texts[0]),
        strlen(texts[1]),
        strlen(texts[2])
    };

    /* Generate and pool embeddings */
    ASSERT_OK(pooling_embed_message(engine, h, message, texts, lens, 3));

    /* Verify all nodes have embeddings */
    ASSERT_NOT_NULL(hierarchy_get_embedding(h, stmt1));
    ASSERT_NOT_NULL(hierarchy_get_embedding(h, stmt2));
    ASSERT_NOT_NULL(hierarchy_get_embedding(h, stmt3));
    ASSERT_NOT_NULL(hierarchy_get_embedding(h, block1));
    ASSERT_NOT_NULL(hierarchy_get_embedding(h, block2));
    ASSERT_NOT_NULL(hierarchy_get_embedding(h, message));

    /* Verify embeddings are normalized */
    const float* msg_emb = hierarchy_get_embedding(h, message);
    float mag = 0.0f;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        mag += msg_emb[i] * msg_emb[i];
    }
    mag = sqrtf(mag);
    ASSERT_FLOAT_EQ(mag, 1.0f, 0.01f);

    embedding_engine_destroy(engine);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test pooling with no children */
TEST(pooling_no_children) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));

    /* Aggregating with no children should not fail */
    ASSERT_OK(pooling_aggregate_children(h, session));

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test invalid arguments */
TEST(pooling_invalid_args) {
    ASSERT_NE(pooling_aggregate_children(NULL, 0), MEM_OK);
    ASSERT_NE(pooling_propagate_session(NULL, 0), MEM_OK);
}

TEST_MAIN()
