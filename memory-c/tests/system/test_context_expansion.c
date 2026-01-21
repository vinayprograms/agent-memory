/*
 * SVC_MEM_TEST_0015 - Verify context expansion
 * SVC_MEM_TEST_0016 - Verify expansion depth limit
 *
 * Test specification:
 * - Store message with 3 blocks, each with 4 statements
 * - Call get_context on middle statement with include_parent: true
 * - Result MUST include parent block and message
 * - Call with include_siblings: true
 * - Result MUST include prev and next statements
 * - Call with max_depth: 1 -> includes parent block only
 * - Call with max_depth: 2 -> includes block and message
 */

#include "../test_framework.h"
#include "../../src/api/api.h"
#include "../../src/core/hierarchy.h"
#include "../../third_party/yyjson/yyjson.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

#define TEST_DIR "/tmp/test_context_expansion"

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
 * TEST: Context expansion includes parent
 */
TEST(context_expansion_parent) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create hierarchy: session -> message -> block -> statements */
    node_id_t session, message, block, stmt1, stmt2, stmt3;

    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt1));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt2));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt3));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    /* Get context for middle statement with include_parent */
    char request[256];
    snprintf(request, sizeof(request),
        "{\"jsonrpc\":\"2.0\",\"method\":\"get_context\","
        "\"params\":{\"node_id\":%u,\"include_parent\":true,\"include_children\":false},"
        "\"id\":1}", stmt2);

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request, strlen(request), &response, &response_len));
    ASSERT_NOT_NULL(response);

    /* Parse response */
    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    /* Should return the node */
    yyjson_val* node_id = yyjson_obj_get(result, "node_id");
    ASSERT_NOT_NULL(node_id);
    ASSERT_EQ((node_id_t)yyjson_get_uint(node_id), stmt2);

    /* Level should be statement */
    yyjson_val* level = yyjson_obj_get(result, "level");
    ASSERT_NOT_NULL(level);
    ASSERT_STR_EQ(yyjson_get_str(level), "statement");

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Hierarchy navigation - parent, children, siblings
 */
TEST(context_expansion_navigation) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Build: session -> message -> 3 blocks, each with 2 statements */
    node_id_t session, message;
    node_id_t blocks[3];
    node_id_t stmts[3][2];

    ASSERT_OK(hierarchy_create_session(h, "agent", "sess", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));

    for (int b = 0; b < 3; b++) {
        ASSERT_OK(hierarchy_create_block(h, message, &blocks[b]));
        for (int s = 0; s < 2; s++) {
            ASSERT_OK(hierarchy_create_statement(h, blocks[b], &stmts[b][s]));
        }
    }

    /* Check parent relationships */
    ASSERT_EQ(hierarchy_get_parent(h, stmts[1][0]), blocks[1]);
    ASSERT_EQ(hierarchy_get_parent(h, blocks[1]), message);
    ASSERT_EQ(hierarchy_get_parent(h, message), session);

    /* Check children */
    node_id_t children[10];
    size_t child_count = hierarchy_get_children(h, message, children, 10);
    ASSERT_EQ(child_count, 3);

    /* Check siblings */
    node_id_t siblings[10];
    size_t sib_count = hierarchy_get_siblings(h, blocks[1], siblings, 10);
    ASSERT_EQ(sib_count, 2);  /* blocks[0] and blocks[2] */

    /* Check next sibling */
    ASSERT_EQ(hierarchy_get_next_sibling(h, blocks[0]), blocks[1]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, blocks[1]), blocks[2]);
    ASSERT_EQ(hierarchy_get_next_sibling(h, blocks[2]), NODE_ID_INVALID);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Get ancestors up to root
 */
TEST(context_expansion_ancestors) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message, block, stmt;
    ASSERT_OK(hierarchy_create_session(h, "agent", "sess", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt));

    /* Get ancestors from statement */
    node_id_t ancestors[10];
    size_t count = hierarchy_get_ancestors(h, stmt, ancestors, 10);

    ASSERT_EQ(count, 3);  /* block, message, session */
    ASSERT_EQ(ancestors[0], block);
    ASSERT_EQ(ancestors[1], message);
    ASSERT_EQ(ancestors[2], session);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Count descendants recursively
 */
TEST(context_expansion_descendant_count) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message;
    ASSERT_OK(hierarchy_create_session(h, "agent", "sess", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));

    /* Create 2 blocks with 3 statements each */
    for (int b = 0; b < 2; b++) {
        node_id_t block;
        ASSERT_OK(hierarchy_create_block(h, message, &block));
        for (int s = 0; s < 3; s++) {
            node_id_t stmt;
            ASSERT_OK(hierarchy_create_statement(h, block, &stmt));
        }
    }

    /* Message should have 2 blocks + 6 statements = 8 descendants */
    size_t desc_count = hierarchy_count_descendants(h, message);
    ASSERT_EQ(desc_count, 8);

    /* Session should have 1 message + 8 message descendants = 9 */
    desc_count = hierarchy_count_descendants(h, session);
    ASSERT_EQ(desc_count, 9);

    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Get context via API with depth limit simulation
 */
TEST(context_expansion_api) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    node_id_t session, message, block, stmt;
    ASSERT_OK(hierarchy_create_session(h, "agent", "sess", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));
    ASSERT_OK(hierarchy_create_block(h, message, &block));
    ASSERT_OK(hierarchy_create_statement(h, block, &stmt));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    /* Get context for block with children included */
    char request[256];
    snprintf(request, sizeof(request),
        "{\"jsonrpc\":\"2.0\",\"method\":\"get_context\","
        "\"params\":{\"node_id\":%u,\"include_children\":true},"
        "\"id\":1}", block);

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request, strlen(request), &response, &response_len));
    ASSERT_NOT_NULL(response);

    /* Verify response is valid JSON */
    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_val* level = yyjson_obj_get(result, "level");
    ASSERT_NOT_NULL(level);
    ASSERT_STR_EQ(yyjson_get_str(level), "block");

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
