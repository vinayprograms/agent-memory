/*
 * SVC_MEM_TEST_0007 - Verify JSON-RPC interface
 *
 * Test specification:
 * - Send valid JSON-RPC 2.0 requests
 * - Verify responses follow JSON-RPC 2.0 spec
 * - Test all methods: store, query, get_session, list_sessions, get_context
 * - Verify error handling for invalid requests
 */

#include "../test_framework.h"
#include "../../src/api/api.h"
#include "../../src/core/hierarchy.h"
#include "../../src/search/search.h"
#include "../../third_party/yyjson/yyjson.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_jsonrpc_interface"

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

/* Helper: Validate JSON-RPC 2.0 response structure */
static bool validate_jsonrpc_response(const char* json, size_t len) {
    yyjson_doc* doc = yyjson_read(json, len, 0);
    if (!doc) return false;

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    /* Must have "jsonrpc": "2.0" */
    yyjson_val* jsonrpc = yyjson_obj_get(root, "jsonrpc");
    if (!jsonrpc || !yyjson_is_str(jsonrpc) ||
        strcmp(yyjson_get_str(jsonrpc), "2.0") != 0) {
        yyjson_doc_free(doc);
        return false;
    }

    /* Must have id */
    yyjson_val* id = yyjson_obj_get(root, "id");
    if (!id) {
        yyjson_doc_free(doc);
        return false;
    }

    /* Must have either result or error, but not both */
    yyjson_val* result = yyjson_obj_get(root, "result");
    yyjson_val* error = yyjson_obj_get(root, "error");

    if ((result && error) || (!result && !error)) {
        yyjson_doc_free(doc);
        return false;
    }

    /* If error, must have code and message */
    if (error) {
        yyjson_val* code = yyjson_obj_get(error, "code");
        yyjson_val* message = yyjson_obj_get(error, "message");
        if (!code || !yyjson_is_int(code) ||
            !message || !yyjson_is_str(message)) {
            yyjson_doc_free(doc);
            return false;
        }
    }

    yyjson_doc_free(doc);
    return true;
}

/*
 * TEST: Verify JSON-RPC 2.0 store method
 */
TEST(jsonrpc_store_method) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, search, NULL, NULL));

    /* Send store request */
    const char* request =
        "{\"jsonrpc\":\"2.0\",\"method\":\"store\","
        "\"params\":{\"session_id\":\"test-sess\",\"agent_id\":\"test-agent\","
        "\"content\":\"Hello, this is a test message.\"},"
        "\"id\":1}";

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request, strlen(request),
                              &response, &response_len));

    ASSERT_NOT_NULL(response);
    ASSERT_GT(response_len, 0);

    /* Validate response structure */
    ASSERT_TRUE(validate_jsonrpc_response(response, response_len));

    /* Parse response and verify it's a success with message_id */
    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_val* message_id = yyjson_obj_get(result, "message_id");
    ASSERT_NOT_NULL(message_id);
    ASSERT_TRUE(yyjson_is_uint(message_id));

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify JSON-RPC 2.0 query method
 */
TEST(jsonrpc_query_method) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, search, NULL, NULL));

    /* Send query request */
    const char* request =
        "{\"jsonrpc\":\"2.0\",\"method\":\"query\","
        "\"params\":{\"query\":\"test search\",\"max_results\":10},"
        "\"id\":2}";

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request, strlen(request),
                              &response, &response_len));

    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(validate_jsonrpc_response(response, response_len));

    /* Parse and verify result structure */
    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_val* results = yyjson_obj_get(result, "results");
    ASSERT_NOT_NULL(results);
    ASSERT_TRUE(yyjson_is_arr(results));

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify JSON-RPC 2.0 get_context method
 */
TEST(jsonrpc_get_context_method) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create a session to get context for */
    node_id_t session;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    /* Send get_context request */
    char request[256];
    snprintf(request, sizeof(request),
        "{\"jsonrpc\":\"2.0\",\"method\":\"get_context\","
        "\"params\":{\"node_id\":%u,\"include_children\":true},"
        "\"id\":3}", session);

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request, strlen(request),
                              &response, &response_len));

    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(validate_jsonrpc_response(response, response_len));

    /* Parse and verify result structure */
    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_val* node_id = yyjson_obj_get(result, "node_id");
    ASSERT_NOT_NULL(node_id);
    ASSERT_EQ((node_id_t)yyjson_get_uint(node_id), session);

    yyjson_val* level = yyjson_obj_get(result, "level");
    ASSERT_NOT_NULL(level);
    ASSERT_STR_EQ(yyjson_get_str(level), "session");

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify JSON-RPC 2.0 error handling
 */
TEST(jsonrpc_error_handling) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    /* Test: Invalid JSON */
    {
        const char* request = "{not valid json}";
        char* response = NULL;
        size_t response_len = 0;
        ASSERT_OK(api_process_rpc(server, request, strlen(request),
                                  &response, &response_len));

        yyjson_doc* doc = yyjson_read(response, response_len, 0);
        ASSERT_NOT_NULL(doc);

        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* error = yyjson_obj_get(root, "error");
        ASSERT_NOT_NULL(error);

        yyjson_val* code = yyjson_obj_get(error, "code");
        ASSERT_EQ(yyjson_get_int(code), RPC_ERROR_PARSE);

        yyjson_doc_free(doc);
        free(response);
    }

    /* Test: Unknown method */
    {
        const char* request =
            "{\"jsonrpc\":\"2.0\",\"method\":\"unknown_method\",\"id\":1}";
        char* response = NULL;
        size_t response_len = 0;
        ASSERT_OK(api_process_rpc(server, request, strlen(request),
                                  &response, &response_len));

        yyjson_doc* doc = yyjson_read(response, response_len, 0);
        ASSERT_NOT_NULL(doc);

        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* error = yyjson_obj_get(root, "error");
        ASSERT_NOT_NULL(error);

        yyjson_val* code = yyjson_obj_get(error, "code");
        ASSERT_EQ(yyjson_get_int(code), RPC_ERROR_METHOD_NOT_FOUND);

        yyjson_doc_free(doc);
        free(response);
    }

    /* Test: Invalid params */
    {
        const char* request =
            "{\"jsonrpc\":\"2.0\",\"method\":\"store\","
            "\"params\":{\"session_id\":\"test\"},\"id\":1}";  /* Missing required params */
        char* response = NULL;
        size_t response_len = 0;
        ASSERT_OK(api_process_rpc(server, request, strlen(request),
                                  &response, &response_len));

        yyjson_doc* doc = yyjson_read(response, response_len, 0);
        ASSERT_NOT_NULL(doc);

        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* error = yyjson_obj_get(root, "error");
        ASSERT_NOT_NULL(error);

        yyjson_val* code = yyjson_obj_get(error, "code");
        ASSERT_EQ(yyjson_get_int(code), RPC_ERROR_INVALID_PARAMS);

        yyjson_doc_free(doc);
        free(response);
    }

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify JSON-RPC 2.0 request ID handling
 */
TEST(jsonrpc_id_handling) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    /* Test: Numeric ID */
    {
        const char* request =
            "{\"jsonrpc\":\"2.0\",\"method\":\"list_sessions\",\"id\":42}";
        char* response = NULL;
        size_t response_len = 0;
        ASSERT_OK(api_process_rpc(server, request, strlen(request),
                                  &response, &response_len));

        yyjson_doc* doc = yyjson_read(response, response_len, 0);
        ASSERT_NOT_NULL(doc);

        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* id = yyjson_obj_get(root, "id");
        ASSERT_NOT_NULL(id);
        ASSERT_TRUE(yyjson_is_int(id));
        ASSERT_EQ(yyjson_get_int(id), 42);

        yyjson_doc_free(doc);
        free(response);
    }

    /* Test: String ID */
    {
        const char* request =
            "{\"jsonrpc\":\"2.0\",\"method\":\"list_sessions\",\"id\":\"abc-123\"}";
        char* response = NULL;
        size_t response_len = 0;
        ASSERT_OK(api_process_rpc(server, request, strlen(request),
                                  &response, &response_len));

        yyjson_doc* doc = yyjson_read(response, response_len, 0);
        ASSERT_NOT_NULL(doc);

        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* id = yyjson_obj_get(root, "id");
        ASSERT_NOT_NULL(id);
        ASSERT_TRUE(yyjson_is_str(id));
        ASSERT_STR_EQ(yyjson_get_str(id), "abc-123");

        yyjson_doc_free(doc);
        free(response);
    }

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify JSON-RPC 2.0 list_sessions method
 */
TEST(jsonrpc_list_sessions_method) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    /* Send list_sessions request */
    const char* request =
        "{\"jsonrpc\":\"2.0\",\"method\":\"list_sessions\",\"id\":4}";

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request, strlen(request),
                              &response, &response_len));

    ASSERT_NOT_NULL(response);
    ASSERT_TRUE(validate_jsonrpc_response(response, response_len));

    /* Parse and verify result structure */
    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_val* sessions = yyjson_obj_get(result, "sessions");
    ASSERT_NOT_NULL(sessions);
    ASSERT_TRUE(yyjson_is_arr(sessions));

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
