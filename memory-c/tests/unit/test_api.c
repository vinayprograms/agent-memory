/*
 * Unit tests for API module (JSON-RPC and HTTP server)
 */

#include "../test_framework.h"
#include "../../src/api/api.h"
#include "../../src/core/hierarchy.h"
#include "../../src/search/search.h"
#include "../../third_party/yyjson/yyjson.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_api"

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

/* Test RPC context creation */
TEST(rpc_context_create_destroy) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    rpc_context_t* ctx = NULL;
    ASSERT_OK(rpc_context_create(&ctx, h, search, NULL));
    ASSERT_NOT_NULL(ctx);

    rpc_context_destroy(ctx);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test RPC context with NULL arguments */
TEST(rpc_context_null_args) {
    rpc_context_t* ctx = NULL;

    /* NULL output pointer should fail */
    ASSERT_NE(rpc_context_create(NULL, NULL, NULL, NULL), MEM_OK);

    /* NULL hierarchy and search are allowed */
    ASSERT_OK(rpc_context_create(&ctx, NULL, NULL, NULL));
    ASSERT_NOT_NULL(ctx);

    rpc_context_destroy(ctx);
}

/* Test JSON-RPC request parsing */
TEST(rpc_parse_valid_request) {
    const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"query\",\"params\":{\"query\":\"test\"},\"id\":1}";

    rpc_request_t request;
    void* doc = NULL;

    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    ASSERT_STR_EQ(request.jsonrpc, "2.0");
    ASSERT_STR_EQ(request.method, "query");
    ASSERT_NOT_NULL(request.params);
    ASSERT_NOT_NULL(request.id);

    rpc_request_free(doc);
}

/* Test parsing request without params */
TEST(rpc_parse_no_params) {
    const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"list_sessions\",\"id\":\"abc\"}";

    rpc_request_t request;
    void* doc = NULL;

    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    ASSERT_STR_EQ(request.jsonrpc, "2.0");
    ASSERT_STR_EQ(request.method, "list_sessions");
    ASSERT_NULL(request.params);
    ASSERT_NOT_NULL(request.id);

    rpc_request_free(doc);
}

/* Test parsing notification (no id) */
TEST(rpc_parse_notification) {
    const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}";

    rpc_request_t request;
    void* doc = NULL;

    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    ASSERT_STR_EQ(request.jsonrpc, "2.0");
    ASSERT_STR_EQ(request.method, "ping");
    ASSERT_NULL(request.id);

    rpc_request_free(doc);
}

/* Test parsing invalid JSON */
TEST(rpc_parse_invalid_json) {
    const char* json = "{not valid json}";

    rpc_request_t request;
    void* doc = NULL;

    ASSERT_NE(rpc_parse_request(json, strlen(json), &request, &doc), MEM_OK);
}

/* Test parsing missing jsonrpc field */
TEST(rpc_parse_missing_jsonrpc) {
    const char* json = "{\"method\":\"test\",\"id\":1}";

    rpc_request_t request;
    void* doc = NULL;

    ASSERT_NE(rpc_parse_request(json, strlen(json), &request, &doc), MEM_OK);
}

/* Test parsing wrong jsonrpc version */
TEST(rpc_parse_wrong_version) {
    const char* json = "{\"jsonrpc\":\"1.0\",\"method\":\"test\",\"id\":1}";

    rpc_request_t request;
    void* doc = NULL;

    ASSERT_NE(rpc_parse_request(json, strlen(json), &request, &doc), MEM_OK);
}

/* Test parsing missing method */
TEST(rpc_parse_missing_method) {
    const char* json = "{\"jsonrpc\":\"2.0\",\"id\":1}";

    rpc_request_t request;
    void* doc = NULL;

    ASSERT_NE(rpc_parse_request(json, strlen(json), &request, &doc), MEM_OK);
}

/* Test RPC execute with unknown method */
TEST(rpc_execute_unknown_method) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    rpc_context_t* ctx = NULL;
    ASSERT_OK(rpc_context_create(&ctx, h, NULL, NULL));

    const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"unknown_method\",\"id\":1}";
    rpc_request_t request;
    void* doc = NULL;
    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    rpc_response_t response;
    ASSERT_OK(rpc_execute(ctx, &request, &response));

    ASSERT_TRUE(response.is_error);
    ASSERT_EQ(response.error_code, RPC_ERROR_METHOD_NOT_FOUND);

    rpc_request_free(doc);
    rpc_context_destroy(ctx);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test RPC response serialization */
TEST(rpc_serialize_success_response) {
    rpc_response_t response = {
        .is_error = false,
        .result = NULL
    };

    char* json = NULL;
    size_t len = 0;

    ASSERT_OK(rpc_serialize_response(&response, NULL, &json, &len));
    ASSERT_NOT_NULL(json);
    ASSERT_GT(len, 0);

    /* Parse and verify */
    yyjson_doc* doc = yyjson_read(json, len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* jsonrpc = yyjson_obj_get(root, "jsonrpc");
    ASSERT_STR_EQ(yyjson_get_str(jsonrpc), "2.0");

    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_TRUE(yyjson_is_null(result));

    yyjson_doc_free(doc);
    free(json);
}

/* Test RPC response serialization with error */
TEST(rpc_serialize_error_response) {
    rpc_response_t response = {
        .is_error = true,
        .error_code = RPC_ERROR_INVALID_PARAMS,
        .error_message = "missing required parameter"
    };

    char* json = NULL;
    size_t len = 0;

    ASSERT_OK(rpc_serialize_response(&response, NULL, &json, &len));
    ASSERT_NOT_NULL(json);

    /* Parse and verify */
    yyjson_doc* doc = yyjson_read(json, len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* error = yyjson_obj_get(root, "error");
    ASSERT_NOT_NULL(error);

    yyjson_val* code = yyjson_obj_get(error, "code");
    ASSERT_EQ(yyjson_get_int(code), RPC_ERROR_INVALID_PARAMS);

    yyjson_val* message = yyjson_obj_get(error, "message");
    ASSERT_STR_EQ(yyjson_get_str(message), "missing required parameter");

    yyjson_doc_free(doc);
    free(json);
}

/* Test store method */
TEST(rpc_method_store) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    rpc_context_t* ctx = NULL;
    ASSERT_OK(rpc_context_create(&ctx, h, search, NULL));

    const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"store\","
                       "\"params\":{\"session_id\":\"sess1\",\"agent_id\":\"agent1\",\"content\":\"hello\"},"
                       "\"id\":1}";

    rpc_request_t request;
    void* doc = NULL;
    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    rpc_response_t response;
    ASSERT_OK(rpc_execute(ctx, &request, &response));

    ASSERT_FALSE(response.is_error);
    ASSERT_NOT_NULL(response.result);

    rpc_request_free(doc);
    rpc_context_destroy(ctx);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test store method with missing params */
TEST(rpc_method_store_missing_params) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    rpc_context_t* ctx = NULL;
    ASSERT_OK(rpc_context_create(&ctx, h, NULL, NULL));

    /* Missing content */
    const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"store\","
                       "\"params\":{\"session_id\":\"sess1\",\"agent_id\":\"agent1\"},"
                       "\"id\":1}";

    rpc_request_t request;
    void* doc = NULL;
    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    rpc_response_t response;
    ASSERT_OK(rpc_execute(ctx, &request, &response));

    ASSERT_TRUE(response.is_error);
    ASSERT_EQ(response.error_code, RPC_ERROR_INVALID_PARAMS);

    rpc_request_free(doc);
    rpc_context_destroy(ctx);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test query method */
TEST(rpc_method_query) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    rpc_context_t* ctx = NULL;
    ASSERT_OK(rpc_context_create(&ctx, h, search, NULL));

    const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"query\","
                       "\"params\":{\"query\":\"test query\",\"max_results\":10},"
                       "\"id\":1}";

    rpc_request_t request;
    void* doc = NULL;
    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    rpc_response_t response;
    ASSERT_OK(rpc_execute(ctx, &request, &response));

    ASSERT_FALSE(response.is_error);

    rpc_request_free(doc);
    rpc_context_destroy(ctx);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test get_context method */
TEST(rpc_method_get_context) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create a session */
    node_id_t session;
    ASSERT_OK(hierarchy_create_session(h, "agent", "session", &session));

    rpc_context_t* ctx = NULL;
    ASSERT_OK(rpc_context_create(&ctx, h, NULL, NULL));

    char json[256];
    snprintf(json, sizeof(json),
             "{\"jsonrpc\":\"2.0\",\"method\":\"get_context\","
             "\"params\":{\"node_id\":%u},"
             "\"id\":1}", session);

    rpc_request_t request;
    void* doc = NULL;
    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    rpc_response_t response;
    ASSERT_OK(rpc_execute(ctx, &request, &response));

    ASSERT_FALSE(response.is_error);
    ASSERT_NOT_NULL(response.result);

    rpc_request_free(doc);
    rpc_context_destroy(ctx);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test list_sessions method */
TEST(rpc_method_list_sessions) {
    rpc_context_t* ctx = NULL;
    ASSERT_OK(rpc_context_create(&ctx, NULL, NULL, NULL));

    const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"list_sessions\",\"id\":1}";

    rpc_request_t request;
    void* doc = NULL;
    ASSERT_OK(rpc_parse_request(json, strlen(json), &request, &doc));

    rpc_response_t response;
    ASSERT_OK(rpc_execute(ctx, &request, &response));

    ASSERT_FALSE(response.is_error);

    rpc_request_free(doc);
    rpc_context_destroy(ctx);
}

/* Test health formatting */
TEST(api_format_health) {
    health_result_t health = {
        .healthy = true,
        .status = "ok",
        .node_count = 100,
        .uptime_ms = 60000,
        .request_count = 500
    };

    char* json = NULL;
    size_t len = 0;

    ASSERT_OK(api_format_health(&health, &json, &len));
    ASSERT_NOT_NULL(json);
    ASSERT_GT(len, 0);

    /* Parse and verify */
    yyjson_doc* doc = yyjson_read(json, len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* status = yyjson_obj_get(root, "status");
    ASSERT_STR_EQ(yyjson_get_str(status), "healthy");

    yyjson_val* node_count = yyjson_obj_get(root, "node_count");
    ASSERT_EQ(yyjson_get_int(node_count), 100);

    yyjson_doc_free(doc);
    free(json);
}

/* Test metrics formatting */
TEST(api_format_metrics) {
    metrics_result_t metrics = {
        .requests_total = 1000,
        .requests_success = 990,
        .requests_error = 10,
        .latency_avg_ms = 5.5,
        .latency_p99_ms = 9.2,
        .nodes_indexed = 500,
        .memory_bytes = 1024000
    };

    char* text = NULL;
    size_t len = 0;

    ASSERT_OK(api_format_metrics(&metrics, &text, &len));
    ASSERT_NOT_NULL(text);
    ASSERT_GT(len, 0);

    /* Verify Prometheus format */
    ASSERT_NOT_NULL(strstr(text, "memory_service_requests_total 1000"));
    ASSERT_NOT_NULL(strstr(text, "memory_service_requests_success 990"));
    ASSERT_NOT_NULL(strstr(text, "memory_service_requests_error 10"));
    ASSERT_NOT_NULL(strstr(text, "memory_service_nodes_indexed 500"));

    free(text);
}

/* Test API server creation */
TEST(api_server_create_destroy) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    api_config_t config = API_CONFIG_DEFAULT;
    config.port = 18080;  /* Non-privileged port */

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, search, NULL, &config));
    ASSERT_NOT_NULL(server);

    /* Server may or may not be running depending on libmicrohttpd */
    ASSERT_EQ(api_server_port(server), 18080);

    api_server_destroy(server);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test API server with default config */
TEST(api_server_default_config) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));
    ASSERT_NOT_NULL(server);

    ASSERT_EQ(api_server_port(server), 8080);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/* Test NULL argument handling */
TEST(api_null_args) {
    ASSERT_NE(api_server_create(NULL, NULL, NULL, NULL, NULL), MEM_OK);

    ASSERT_FALSE(api_server_running(NULL));
    ASSERT_EQ(api_server_port(NULL), 0);
    ASSERT_EQ(api_server_request_count(NULL), 0);

    api_server_destroy(NULL);  /* Should not crash */
}

TEST_MAIN()
