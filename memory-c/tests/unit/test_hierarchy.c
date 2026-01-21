/*
 * Unit tests for hierarchy management
 */

#include "../test_framework.h"
#include "../../src/core/hierarchy.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_hierarchy"

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

/* Test basic creation */
TEST(hierarchy_create_basic) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 1000));
    ASSERT_NOT_NULL(h);
    ASSERT_EQ(hierarchy_count(h), 0);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test session creation */
TEST(hierarchy_create_session) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session_id;
    ASSERT_OK(hierarchy_create_session(h, "agent-1", "session-1", &session_id));
    ASSERT_EQ(session_id, 0);
    ASSERT_EQ(hierarchy_count(h), 1);

    /* Verify node info */
    node_info_t info;
    ASSERT_OK(hierarchy_get_node(h, session_id, &info));
    ASSERT_EQ(info.level, LEVEL_SESSION);
    ASSERT_EQ(info.parent_id, NODE_ID_INVALID);
    ASSERT_STR_EQ(info.agent_id, "agent-1");
    ASSERT_STR_EQ(info.session_id, "session-1");

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test full hierarchy creation */
TEST(hierarchy_full_tree) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create session */
    node_id_t session;
    ASSERT_OK(hierarchy_create_session(h, "agent-1", "session-1", &session));

    /* Create message under session */
    node_id_t message;
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_EQ(hierarchy_get_level(h, message), LEVEL_MESSAGE);
    ASSERT_EQ(hierarchy_get_parent(h, message), session);

    /* Create block under message */
    node_id_t block;
    ASSERT_OK(hierarchy_create_block(h, message, &block));
    ASSERT_EQ(hierarchy_get_level(h, block), LEVEL_BLOCK);
    ASSERT_EQ(hierarchy_get_parent(h, block), message);

    /* Create statement under block */
    node_id_t stmt;
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt));
    ASSERT_EQ(hierarchy_get_level(h, stmt), LEVEL_STATEMENT);
    ASSERT_EQ(hierarchy_get_parent(h, stmt), block);

    ASSERT_EQ(hierarchy_count(h), 4);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test parent-child relationships */
TEST(hierarchy_parent_child) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create session with message and two blocks */
    node_id_t session, message, block1, block2;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block1));
    ASSERT_OK(hierarchy_create_block(h, message, &block2));

    /* Verify parent relationships */
    ASSERT_EQ(hierarchy_get_parent(h, block1), message);
    ASSERT_EQ(hierarchy_get_parent(h, block2), message);

    /* Verify first child */
    ASSERT_EQ(hierarchy_get_first_child(h, message), block1);

    /* Verify sibling relationship */
    ASSERT_EQ(hierarchy_get_next_sibling(h, block1), block2);
    ASSERT_EQ(hierarchy_get_next_sibling(h, block2), NODE_ID_INVALID);

    /* Verify get_children */
    node_id_t children[10];
    size_t count = hierarchy_get_children(h, message, children, 10);
    ASSERT_EQ(count, 2);
    ASSERT_EQ(children[0], block1);
    ASSERT_EQ(children[1], block2);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test sibling relationships */
TEST(hierarchy_siblings) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Create 3 statements */
    node_id_t stmts[3];
    for (int i = 0; i < 3; i++) {
        ASSERT_OK(hierarchy_create_statement(h, block, &stmts[i]));
    }

    /* Verify sibling chain */
    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[0]), stmts[1]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[1]), stmts[2]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[2]), NODE_ID_INVALID);

    /* Verify get_siblings (excludes self) */
    node_id_t siblings[10];
    size_t count = hierarchy_get_siblings(h, stmts[1], siblings, 10);
    ASSERT_EQ(count, 2);  /* Should get stmt0 and stmt2 */

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test ancestor traversal */
TEST(hierarchy_ancestors) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message, block, stmt;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt));

    /* Get ancestors of statement */
    node_id_t ancestors[10];
    size_t count = hierarchy_get_ancestors(h, stmt, ancestors, 10);
    ASSERT_EQ(count, 3);  /* block, message, session */
    ASSERT_EQ(ancestors[0], block);
    ASSERT_EQ(ancestors[1], message);
    ASSERT_EQ(ancestors[2], session);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test descendant counting */
TEST(hierarchy_descendants) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message, block1, block2;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block1));
    ASSERT_OK(hierarchy_create_block(h, message, &block2));

    /* Add statements to blocks */
    node_id_t stmt;
    ASSERT_OK(hierarchy_create_statement(h, block1, &stmt));
    ASSERT_OK(hierarchy_create_statement(h, block1, &stmt));
    ASSERT_OK(hierarchy_create_statement(h, block2, &stmt));

    /* Count descendants */
    ASSERT_EQ(hierarchy_count_descendants(h, session), 6);  /* 1 msg + 2 blocks + 3 stmts */
    ASSERT_EQ(hierarchy_count_descendants(h, message), 5);  /* 2 blocks + 3 stmts */
    ASSERT_EQ(hierarchy_count_descendants(h, block1), 2);   /* 2 stmts */
    ASSERT_EQ(hierarchy_count_descendants(h, block2), 1);   /* 1 stmt */

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test embeddings integration */
TEST(hierarchy_embeddings) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));

    /* Set embedding for message */
    float values[EMBEDDING_DIM];
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        values[i] = (float)i * 0.01f;
    }
    ASSERT_OK(hierarchy_set_embedding(h, message, values));

    /* Get embedding back */
    const float* retrieved = hierarchy_get_embedding(h, message);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_FLOAT_EQ(retrieved[0], 0.0f, 0.001f);
    ASSERT_FLOAT_EQ(retrieved[1], 0.01f, 0.001f);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test level validation */
TEST(hierarchy_level_validation) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message, block;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));

    /* Try to create message under block (should fail) */
    node_id_t bad_msg;
    ASSERT_NE(hierarchy_create_message(h, block, &bad_msg), MEM_OK);

    /* Try to create block under session (should fail) */
    node_id_t bad_block;
    ASSERT_NE(hierarchy_create_block(h, session, &bad_block), MEM_OK);

    /* Try to create statement under session (should fail) */
    node_id_t bad_stmt;
    ASSERT_NE(hierarchy_create_statement(h, session, &bad_stmt), MEM_OK);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test persistence */
TEST(hierarchy_persistence) {
    setup_dir();

    node_id_t session, message, block, stmt;

    /* Create and populate */
    {
        hierarchy_t* h = NULL;
        ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

        ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
        ASSERT_OK(hierarchy_create_message(h, session, &message));
        ASSERT_OK(hierarchy_create_block(h, message, &block));
        ASSERT_OK(hierarchy_create_statement(h, block, &stmt));

        ASSERT_OK(hierarchy_sync(h));
        hierarchy_close(h);
    }

    /* Reopen and verify */
    {
        hierarchy_t* h = NULL;
        ASSERT_OK(hierarchy_open(&h, TEST_DIR));

        ASSERT_EQ(hierarchy_count(h), 4);
        ASSERT_EQ(hierarchy_get_level(h, session), LEVEL_SESSION);
        ASSERT_EQ(hierarchy_get_level(h, message), LEVEL_MESSAGE);
        ASSERT_EQ(hierarchy_get_level(h, block), LEVEL_BLOCK);
        ASSERT_EQ(hierarchy_get_level(h, stmt), LEVEL_STATEMENT);

        /* Verify relationships survived */
        ASSERT_EQ(hierarchy_get_parent(h, stmt), block);
        ASSERT_EQ(hierarchy_get_parent(h, block), message);
        ASSERT_EQ(hierarchy_get_parent(h, message), session);

        hierarchy_close(h);
    }

    cleanup_dir(TEST_DIR);
}

/* Test invalid arguments */
TEST(hierarchy_invalid_args) {
    ASSERT_NE(hierarchy_create(NULL, "/tmp/test", 100), MEM_OK);

    hierarchy_t* h = NULL;
    ASSERT_NE(hierarchy_create(&h, NULL, 100), MEM_OK);
    ASSERT_NE(hierarchy_create(&h, "/tmp/test", 0), MEM_OK);

    /* NULL hierarchy should return safe defaults */
    ASSERT_EQ(hierarchy_get_parent(NULL, 0), NODE_ID_INVALID);
    ASSERT_EQ(hierarchy_get_first_child(NULL, 0), NODE_ID_INVALID);
    ASSERT_EQ(hierarchy_count(NULL), 0);
}

TEST_MAIN()
