/*
 * SVC_MEM_TEST_0003 - Verify hierarchical relationships
 *
 * Test specification:
 * - Store message with 2 code blocks, each with 3 statements
 * - Query relationships: message -> blocks -> statements
 * - All parent-child relationships MUST be correct
 * - Sibling relationships (next block, next statement) MUST be correct
 */

#include "../test_framework.h"
#include "../../src/core/hierarchy.h"
#include "../../include/types.h"
#include "../../include/error.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_hierarchical_relationships"

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

/*
 * TEST: Verify complete hierarchical structure
 *
 * Creates:
 *   Session
 *     └── Message
 *           ├── Block 1
 *           │     ├── Statement 1.1
 *           │     ├── Statement 1.2
 *           │     └── Statement 1.3
 *           └── Block 2
 *                 ├── Statement 2.1
 *                 ├── Statement 2.2
 *                 └── Statement 2.3
 */
TEST(hierarchical_relationships) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create session */
    node_id_t session;
    ASSERT_OK(hierarchy_create_session(h, "test-agent", "test-session", &session));
    ASSERT_EQ(hierarchy_get_level(h, session), LEVEL_SESSION);

    /* Create message under session */
    node_id_t message;
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_EQ(hierarchy_get_level(h, message), LEVEL_MESSAGE);

    /* Create 2 blocks under message */
    node_id_t blocks[2];
    ASSERT_OK(hierarchy_create_block(h, message, &blocks[0]));
    ASSERT_OK(hierarchy_create_block(h, message, &blocks[1]));

    /* Create 3 statements under each block */
    node_id_t stmts[2][3];
    for (int b = 0; b < 2; b++) {
        for (int s = 0; s < 3; s++) {
            ASSERT_OK(hierarchy_create_statement(h, blocks[b], &stmts[b][s]));
        }
    }

    /* Total nodes: 1 session + 1 message + 2 blocks + 6 statements = 10 */
    ASSERT_EQ(hierarchy_count(h), 10);

    /*
     * Verify parent-child relationships: message -> blocks -> statements
     */

    /* Message's parent is session */
    ASSERT_EQ(hierarchy_get_parent(h, message), session);

    /* Blocks' parent is message */
    ASSERT_EQ(hierarchy_get_parent(h, blocks[0]), message);
    ASSERT_EQ(hierarchy_get_parent(h, blocks[1]), message);

    /* Statements' parents are their respective blocks */
    for (int s = 0; s < 3; s++) {
        ASSERT_EQ(hierarchy_get_parent(h, stmts[0][s]), blocks[0]);
        ASSERT_EQ(hierarchy_get_parent(h, stmts[1][s]), blocks[1]);
    }

    /* Verify first child relationships */
    ASSERT_EQ(hierarchy_get_first_child(h, session), message);
    ASSERT_EQ(hierarchy_get_first_child(h, message), blocks[0]);
    ASSERT_EQ(hierarchy_get_first_child(h, blocks[0]), stmts[0][0]);
    ASSERT_EQ(hierarchy_get_first_child(h, blocks[1]), stmts[1][0]);

    /* Verify get_children returns all children */
    node_id_t children[10];
    size_t count;

    count = hierarchy_get_children(h, message, children, 10);
    ASSERT_EQ(count, 2);
    ASSERT_EQ(children[0], blocks[0]);
    ASSERT_EQ(children[1], blocks[1]);

    count = hierarchy_get_children(h, blocks[0], children, 10);
    ASSERT_EQ(count, 3);
    ASSERT_EQ(children[0], stmts[0][0]);
    ASSERT_EQ(children[1], stmts[0][1]);
    ASSERT_EQ(children[2], stmts[0][2]);

    /*
     * Verify sibling relationships: next block, next statement
     */

    /* Block sibling chain */
    ASSERT_EQ(hierarchy_get_next_sibling(h, blocks[0]), blocks[1]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, blocks[1]), NODE_ID_INVALID);

    /* Statement sibling chains */
    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[0][0]), stmts[0][1]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[0][1]), stmts[0][2]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[0][2]), NODE_ID_INVALID);

    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[1][0]), stmts[1][1]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[1][1]), stmts[1][2]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[1][2]), NODE_ID_INVALID);

    /*
     * Verify ancestor traversal: statement -> block -> message -> session
     */
    node_id_t ancestors[10];
    count = hierarchy_get_ancestors(h, stmts[0][2], ancestors, 10);
    ASSERT_EQ(count, 3);
    ASSERT_EQ(ancestors[0], blocks[0]);    /* Immediate parent */
    ASSERT_EQ(ancestors[1], message);       /* Grandparent */
    ASSERT_EQ(ancestors[2], session);       /* Great-grandparent */

    /*
     * Verify descendant counting
     */
    ASSERT_EQ(hierarchy_count_descendants(h, session), 9);   /* message + 2 blocks + 6 stmts */
    ASSERT_EQ(hierarchy_count_descendants(h, message), 8);   /* 2 blocks + 6 stmts */
    ASSERT_EQ(hierarchy_count_descendants(h, blocks[0]), 3); /* 3 stmts */
    ASSERT_EQ(hierarchy_count_descendants(h, blocks[1]), 3); /* 3 stmts */
    ASSERT_EQ(hierarchy_count_descendants(h, stmts[0][0]), 0); /* Leaf node */

    /*
     * Verify level assignments
     */
    ASSERT_EQ(hierarchy_get_level(h, session), LEVEL_SESSION);
    ASSERT_EQ(hierarchy_get_level(h, message), LEVEL_MESSAGE);
    ASSERT_EQ(hierarchy_get_level(h, blocks[0]), LEVEL_BLOCK);
    ASSERT_EQ(hierarchy_get_level(h, blocks[1]), LEVEL_BLOCK);
    for (int b = 0; b < 2; b++) {
        for (int s = 0; s < 3; s++) {
            ASSERT_EQ(hierarchy_get_level(h, stmts[b][s]), LEVEL_STATEMENT);
        }
    }

    /*
     * Verify node info contains correct metadata
     */
    node_info_t info;
    ASSERT_OK(hierarchy_get_node(h, stmts[1][1], &info));
    ASSERT_EQ(info.id, stmts[1][1]);
    ASSERT_EQ(info.level, LEVEL_STATEMENT);
    ASSERT_EQ(info.parent_id, blocks[1]);
    ASSERT_EQ(info.first_child_id, NODE_ID_INVALID);  /* Leaf node */
    ASSERT_EQ(info.next_sibling_id, stmts[1][2]);
    ASSERT_STR_EQ(info.session_id, "test-session");
    ASSERT_STR_EQ(info.agent_id, "test-agent");

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify relationships persist across restart
 */
TEST(hierarchical_relationships_persist) {
    setup_dir();

    node_id_t session, message, blocks[2], stmts[2][3];

    /* Create hierarchy */
    {
        hierarchy_t* h = NULL;
        ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

        ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
        ASSERT_OK(hierarchy_create_message(h, session, &message));
        ASSERT_OK(hierarchy_create_block(h, message, &blocks[0]));
        ASSERT_OK(hierarchy_create_block(h, message, &blocks[1]));

        for (int b = 0; b < 2; b++) {
            for (int s = 0; s < 3; s++) {
                ASSERT_OK(hierarchy_create_statement(h, blocks[b], &stmts[b][s]));
            }
        }

        ASSERT_OK(hierarchy_sync(h));
        hierarchy_close(h);
    }

    /* Reopen and verify all relationships */
    {
        hierarchy_t* h = NULL;
        ASSERT_OK(hierarchy_open(&h, TEST_DIR));

        ASSERT_EQ(hierarchy_count(h), 10);

        /* Verify parent-child */
        ASSERT_EQ(hierarchy_get_parent(h, message), session);
        ASSERT_EQ(hierarchy_get_parent(h, blocks[0]), message);
        ASSERT_EQ(hierarchy_get_parent(h, stmts[0][0]), blocks[0]);

        /* Verify siblings */
        ASSERT_EQ(hierarchy_get_next_sibling(h, blocks[0]), blocks[1]);
        ASSERT_EQ(hierarchy_get_next_sibling(h, stmts[0][0]), stmts[0][1]);

        /* Verify children */
        node_id_t children[10];
        size_t count = hierarchy_get_children(h, message, children, 10);
        ASSERT_EQ(count, 2);
        ASSERT_EQ(children[0], blocks[0]);
        ASSERT_EQ(children[1], blocks[1]);

        hierarchy_close(h);
    }

    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
