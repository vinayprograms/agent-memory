/*
 * SVC_MEM_TEST_0008 - Verify HTTP endpoints
 *
 * Test specification:
 * - Verify /health endpoint returns correct health status
 * - Verify /metrics endpoint returns Prometheus format
 * - Verify /rpc endpoint handles JSON-RPC requests
 * - Verify proper content types and response codes
 *
 * Note: This test verifies endpoint behavior through the API functions
 * since libmicrohttpd may not be available at test time.
 */

#include "../test_framework.h"
#include "../../src/api/api.h"
#include "../../src/core/hierarchy.h"
#include "../../src/search/search.h"
#include "../../third_party/yyjson/yyjson.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_http_endpoints"

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
 * TEST: Verify health endpoint returns healthy status
 */
TEST(http_health_endpoint) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Create some nodes */
    node_id_t session, message;
    ASSERT_OK(hierarchy_create_session(h, "agent1", "session1", &session));
    ASSERT_OK(hierarchy_create_message(h, session, &message));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    /* Get health status */
    health_result_t health;
    ASSERT_OK(api_get_health(server, &health));

    ASSERT_TRUE(health.healthy);
    ASSERT_EQ(health.node_count, 2);  /* session + message */
    /* uptime_ms is valid uint64_t calculated from timestamps */

    /* Format as JSON */
    char* json = NULL;
    size_t len = 0;
    ASSERT_OK(api_format_health(&health, &json, &len));
    ASSERT_NOT_NULL(json);
    ASSERT_GT(len, 0);

    /* Parse and verify JSON structure */
    yyjson_doc* doc = yyjson_read(json, len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* status = yyjson_obj_get(root, "status");
    ASSERT_NOT_NULL(status);
    ASSERT_STR_EQ(yyjson_get_str(status), "healthy");

    yyjson_val* node_count = yyjson_obj_get(root, "node_count");
    ASSERT_NOT_NULL(node_count);
    ASSERT_EQ(yyjson_get_uint(node_count), 2);

    yyjson_doc_free(doc);
    free(json);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify health endpoint for uninitialized server
 */
TEST(http_health_endpoint_null) {
    health_result_t health;
    ASSERT_OK(api_get_health(NULL, &health));

    ASSERT_FALSE(health.healthy);
    ASSERT_STR_EQ(health.status, "not initialized");
}

/*
 * TEST: Verify metrics endpoint returns Prometheus format
 */
TEST(http_metrics_endpoint) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, search, NULL, NULL));

    /* Make some requests to increment counters */
    const char* request1 = "{\"jsonrpc\":\"2.0\",\"method\":\"list_sessions\",\"id\":1}";
    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request1, strlen(request1),
                              &response, &response_len));
    free(response);

    const char* request2 = "{invalid}";
    ASSERT_OK(api_process_rpc(server, request2, strlen(request2),
                              &response, &response_len));
    free(response);

    /* Get metrics */
    metrics_result_t metrics;
    ASSERT_OK(api_get_metrics(server, &metrics));

    ASSERT_EQ(metrics.requests_total, 2);
    ASSERT_EQ(metrics.requests_success, 1);
    ASSERT_EQ(metrics.requests_error, 1);
    /* nodes_indexed is size_t, just verify it's accessible */
    (void)metrics.nodes_indexed;

    /* Format as Prometheus exposition format */
    char* text = NULL;
    size_t len = 0;
    ASSERT_OK(api_format_metrics(&metrics, &text, &len));
    ASSERT_NOT_NULL(text);
    ASSERT_GT(len, 0);

    /* Verify Prometheus format */
    ASSERT_NOT_NULL(strstr(text, "# HELP"));
    ASSERT_NOT_NULL(strstr(text, "# TYPE"));
    ASSERT_NOT_NULL(strstr(text, "memory_service_requests_total 2"));
    ASSERT_NOT_NULL(strstr(text, "memory_service_requests_success 1"));
    ASSERT_NOT_NULL(strstr(text, "memory_service_requests_error 1"));

    free(text);

    api_server_destroy(server);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify RPC endpoint handles valid request
 */
TEST(http_rpc_endpoint_valid) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, search, NULL, NULL));

    /* Send valid store request */
    const char* request =
        "{\"jsonrpc\":\"2.0\",\"method\":\"store\","
        "\"params\":{\"session_id\":\"sess1\",\"agent_id\":\"agent1\","
        "\"content\":\"Hello world\"},"
        "\"id\":1}";

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request, strlen(request),
                              &response, &response_len));

    ASSERT_NOT_NULL(response);
    ASSERT_GT(response_len, 0);

    /* Verify response is valid JSON */
    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* jsonrpc = yyjson_obj_get(root, "jsonrpc");
    ASSERT_STR_EQ(yyjson_get_str(jsonrpc), "2.0");

    /* Should have result, not error */
    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_val* error = yyjson_obj_get(root, "error");
    ASSERT_NULL(error);

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify RPC endpoint handles invalid request
 */
TEST(http_rpc_endpoint_invalid) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    /* Send invalid JSON */
    const char* request = "{broken json";

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, request, strlen(request),
                              &response, &response_len));

    ASSERT_NOT_NULL(response);

    /* Should return parse error */
    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* error = yyjson_obj_get(root, "error");
    ASSERT_NOT_NULL(error);

    yyjson_val* code = yyjson_obj_get(error, "code");
    ASSERT_EQ(yyjson_get_int(code), -32700);  /* Parse error */

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify server configuration
 */
TEST(http_server_configuration) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    /* Custom configuration */
    api_config_t config = {
        .port = 19999,
        .max_connections = 50,
        .thread_pool = 2,
        .timeout_ms = 5000
    };

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, &config));
    ASSERT_NOT_NULL(server);

    ASSERT_EQ(api_server_port(server), 19999);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify request counting
 */
TEST(http_request_counting) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, NULL, NULL, NULL));

    ASSERT_EQ(api_server_request_count(server), 0);

    /* Make some requests */
    const char* request = "{\"jsonrpc\":\"2.0\",\"method\":\"list_sessions\",\"id\":1}";
    char* response = NULL;
    size_t response_len = 0;

    for (int i = 0; i < 5; i++) {
        ASSERT_OK(api_process_rpc(server, request, strlen(request),
                                  &response, &response_len));
        free(response);
    }

    ASSERT_EQ(api_server_request_count(server), 5);

    api_server_destroy(server);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify multiple sequential requests
 */
TEST(http_sequential_requests) {
    setup_dir();

    hierarchy_t* h = NULL;
    ASSERT_OK(hierarchy_create(&h, TEST_DIR, 100));

    search_engine_t* search = NULL;
    ASSERT_OK(search_engine_create(&search, h, NULL));

    api_server_t* server = NULL;
    ASSERT_OK(api_server_create(&server, h, search, NULL, NULL));

    /* First: store a message */
    const char* store_req =
        "{\"jsonrpc\":\"2.0\",\"method\":\"store\","
        "\"params\":{\"session_id\":\"sess\",\"agent_id\":\"agent\","
        "\"content\":\"Test content\"},"
        "\"id\":1}";

    char* response = NULL;
    size_t response_len = 0;
    ASSERT_OK(api_process_rpc(server, store_req, strlen(store_req),
                              &response, &response_len));

    yyjson_doc* doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_doc_free(doc);
    free(response);

    /* Second: query for content */
    const char* query_req =
        "{\"jsonrpc\":\"2.0\",\"method\":\"query\","
        "\"params\":{\"query\":\"test\",\"max_results\":10},"
        "\"id\":2}";

    ASSERT_OK(api_process_rpc(server, query_req, strlen(query_req),
                              &response, &response_len));

    doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_doc_free(doc);
    free(response);

    /* Third: list sessions */
    const char* list_req =
        "{\"jsonrpc\":\"2.0\",\"method\":\"list_sessions\",\"id\":3}";

    ASSERT_OK(api_process_rpc(server, list_req, strlen(list_req),
                              &response, &response_len));

    doc = yyjson_read(response, response_len, 0);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    result = yyjson_obj_get(root, "result");
    ASSERT_NOT_NULL(result);

    yyjson_doc_free(doc);
    free(response);

    api_server_destroy(server);
    search_engine_destroy(search);
    hierarchy_close(h);
    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
