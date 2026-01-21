/*
 * Memory Service - MCP Server
 *
 * Model Context Protocol server for integration with AI coding agents.
 * Communicates via JSON-RPC 2.0 over stdin/stdout.
 *
 * Usage:
 *   ./memory-mcp [OPTIONS]
 *
 * MCP Integration (Claude Code, etc.):
 *   {
 *     "memory": {
 *       "command": "./memory-mcp",
 *       "args": ["--model", "./models/all-MiniLM-L6-v2/model.onnx"]
 *     }
 *   }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#include "config.h"
#include "util/log.h"
#include "util/time.h"
#include "types.h"

#include "core/hierarchy.h"
#include "embedding/embedding.h"
#include "search/search.h"
#include "api/api.h"

/* yyjson for MCP protocol handling */
#include "../third_party/yyjson/yyjson.h"

/* Global shutdown flag */
static volatile sig_atomic_t g_shutdown = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_shutdown = 1;
}

/* Ensure directory exists, create if needed */
static int ensure_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/* MCP Tool Definitions */
static const char* MCP_TOOLS_JSON =
"["
  "{"
    "\"name\": \"memory_store\","
    "\"description\": \"Store a message in the memory hierarchy. Creates a session if needed.\","
    "\"inputSchema\": {"
      "\"type\": \"object\","
      "\"properties\": {"
        "\"session_id\": {\"type\": \"string\", \"description\": \"Session identifier\"},"
        "\"agent_id\": {\"type\": \"string\", \"description\": \"Agent identifier\"},"
        "\"content\": {\"type\": \"string\", \"description\": \"Message content to store\"}"
      "},"
      "\"required\": [\"session_id\", \"agent_id\", \"content\"]"
    "}"
  "},"
  "{"
    "\"name\": \"memory_store_block\","
    "\"description\": \"Store a block (code, paragraph) under a parent message.\","
    "\"inputSchema\": {"
      "\"type\": \"object\","
      "\"properties\": {"
        "\"parent_id\": {\"type\": \"integer\", \"description\": \"Parent node ID\"},"
        "\"content\": {\"type\": \"string\", \"description\": \"Block content\"}"
      "},"
      "\"required\": [\"parent_id\", \"content\"]"
    "}"
  "},"
  "{"
    "\"name\": \"memory_store_statement\","
    "\"description\": \"Store a statement (sentence, line) under a parent block.\","
    "\"inputSchema\": {"
      "\"type\": \"object\","
      "\"properties\": {"
        "\"parent_id\": {\"type\": \"integer\", \"description\": \"Parent node ID\"},"
        "\"content\": {\"type\": \"string\", \"description\": \"Statement content\"}"
      "},"
      "\"required\": [\"parent_id\", \"content\"]"
    "}"
  "},"
  "{"
    "\"name\": \"memory_query\","
    "\"description\": \"Semantic search across the memory hierarchy.\","
    "\"inputSchema\": {"
      "\"type\": \"object\","
      "\"properties\": {"
        "\"query\": {\"type\": \"string\", \"description\": \"Search query text\"},"
        "\"level\": {\"type\": \"string\", \"enum\": [\"session\", \"message\", \"block\", \"statement\"], \"description\": \"Filter to specific level\"},"
        "\"max_results\": {\"type\": \"integer\", \"description\": \"Maximum results (default 10, max 100)\"}"
      "},"
      "\"required\": [\"query\"]"
    "}"
  "},"
  "{"
    "\"name\": \"memory_drill_down\","
    "\"description\": \"Get children of a node for navigation.\","
    "\"inputSchema\": {"
      "\"type\": \"object\","
      "\"properties\": {"
        "\"id\": {\"type\": \"integer\", \"description\": \"Node ID to drill down from\"},"
        "\"filter\": {\"type\": \"string\", \"description\": \"Optional text filter for children\"},"
        "\"max_results\": {\"type\": \"integer\", \"description\": \"Maximum children to return\"}"
      "},"
      "\"required\": [\"id\"]"
    "}"
  "},"
  "{"
    "\"name\": \"memory_zoom_out\","
    "\"description\": \"Get parent chain and siblings of a node for context.\","
    "\"inputSchema\": {"
      "\"type\": \"object\","
      "\"properties\": {"
        "\"id\": {\"type\": \"integer\", \"description\": \"Node ID to zoom out from\"}"
      "},"
      "\"required\": [\"id\"]"
    "}"
  "},"
  "{"
    "\"name\": \"memory_list_sessions\","
    "\"description\": \"List all sessions in the memory store.\","
    "\"inputSchema\": {"
      "\"type\": \"object\","
      "\"properties\": {},"
      "\"required\": []"
    "}"
  "},"
  "{"
    "\"name\": \"memory_get_session\","
    "\"description\": \"Get details of a specific session.\","
    "\"inputSchema\": {"
      "\"type\": \"object\","
      "\"properties\": {"
        "\"session_id\": {\"type\": \"string\", \"description\": \"Session identifier\"}"
      "},"
      "\"required\": [\"session_id\"]"
    "}"
  "}"
"]";

/* Map MCP tool name to internal JSON-RPC method */
static const char* map_tool_to_method(const char* tool_name) {
    if (strcmp(tool_name, "memory_store") == 0) return "store";
    if (strcmp(tool_name, "memory_store_block") == 0) return "store_block";
    if (strcmp(tool_name, "memory_store_statement") == 0) return "store_statement";
    if (strcmp(tool_name, "memory_query") == 0) return "query";
    if (strcmp(tool_name, "memory_drill_down") == 0) return "drill_down";
    if (strcmp(tool_name, "memory_zoom_out") == 0) return "zoom_out";
    if (strcmp(tool_name, "memory_list_sessions") == 0) return "list_sessions";
    if (strcmp(tool_name, "memory_get_session") == 0) return "get_session";
    return NULL;
}

/* Handle MCP initialize request */
static char* handle_mcp_initialize(yyjson_val* id) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");

    /* Copy request ID */
    if (yyjson_is_int(id)) {
        yyjson_mut_obj_add_int(doc, root, "id", yyjson_get_int(id));
    } else if (yyjson_is_str(id)) {
        yyjson_mut_obj_add_str(doc, root, "id", yyjson_get_str(id));
    }

    /* Result */
    yyjson_mut_val* result = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "result", result);

    /* Protocol version */
    yyjson_mut_obj_add_str(doc, result, "protocolVersion", "2024-11-05");

    /* Server info */
    yyjson_mut_val* server_info = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, server_info, "name", "memory-service");
    char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             MEMORY_SERVICE_VERSION_MAJOR,
             MEMORY_SERVICE_VERSION_MINOR,
             MEMORY_SERVICE_VERSION_PATCH);
    yyjson_mut_obj_add_str(doc, server_info, "version", version);
    yyjson_mut_obj_add_val(doc, result, "serverInfo", server_info);

    /* Capabilities */
    yyjson_mut_val* capabilities = yyjson_mut_obj(doc);
    yyjson_mut_val* tools_cap = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, capabilities, "tools", tools_cap);
    yyjson_mut_obj_add_val(doc, result, "capabilities", capabilities);

    char* response = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return response;
}

/* Handle MCP tools/list request */
static char* handle_mcp_tools_list(yyjson_val* id) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");

    if (yyjson_is_int(id)) {
        yyjson_mut_obj_add_int(doc, root, "id", yyjson_get_int(id));
    } else if (yyjson_is_str(id)) {
        yyjson_mut_obj_add_str(doc, root, "id", yyjson_get_str(id));
    }

    /* Parse tools JSON and add to result */
    yyjson_mut_val* result = yyjson_mut_obj(doc);

    yyjson_doc* tools_doc = yyjson_read(MCP_TOOLS_JSON, strlen(MCP_TOOLS_JSON), 0);
    if (tools_doc) {
        yyjson_val* tools_arr = yyjson_doc_get_root(tools_doc);
        yyjson_mut_val* mut_tools = yyjson_val_mut_copy(doc, tools_arr);
        yyjson_mut_obj_add_val(doc, result, "tools", mut_tools);
        yyjson_doc_free(tools_doc);
    }

    yyjson_mut_obj_add_val(doc, root, "result", result);

    char* response = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return response;
}

/* Handle MCP tools/call - delegate to internal API */
static char* handle_mcp_tools_call(yyjson_val* id, yyjson_val* params, api_server_t* api) {
    yyjson_val* name = yyjson_obj_get(params, "name");
    yyjson_val* arguments = yyjson_obj_get(params, "arguments");

    if (!name || !yyjson_is_str(name)) {
        /* Error: missing tool name */
        yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
        if (yyjson_is_int(id)) yyjson_mut_obj_add_int(doc, root, "id", yyjson_get_int(id));
        yyjson_mut_val* error = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, error, "code", -32602);
        yyjson_mut_obj_add_str(doc, error, "message", "Missing tool name");
        yyjson_mut_obj_add_val(doc, root, "error", error);
        char* resp = yyjson_mut_write(doc, 0, NULL);
        yyjson_mut_doc_free(doc);
        return resp;
    }

    const char* tool_name = yyjson_get_str(name);
    const char* method = map_tool_to_method(tool_name);

    if (!method) {
        /* Error: unknown tool */
        yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
        if (yyjson_is_int(id)) yyjson_mut_obj_add_int(doc, root, "id", yyjson_get_int(id));
        yyjson_mut_val* error = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, error, "code", -32601);
        yyjson_mut_obj_add_str(doc, error, "message", "Unknown tool");
        yyjson_mut_obj_add_val(doc, root, "error", error);
        char* resp = yyjson_mut_write(doc, 0, NULL);
        yyjson_mut_doc_free(doc);
        return resp;
    }

    /* Build internal JSON-RPC request */
    yyjson_mut_doc* req_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* req_root = yyjson_mut_obj(req_doc);
    yyjson_mut_doc_set_root(req_doc, req_root);

    yyjson_mut_obj_add_str(req_doc, req_root, "jsonrpc", "2.0");
    yyjson_mut_obj_add_str(req_doc, req_root, "method", method);
    yyjson_mut_obj_add_int(req_doc, req_root, "id", 1);

    if (arguments && yyjson_is_obj(arguments)) {
        yyjson_mut_val* mut_args = yyjson_val_mut_copy(req_doc, arguments);
        yyjson_mut_obj_add_val(req_doc, req_root, "params", mut_args);
    } else {
        yyjson_mut_obj_add_obj(req_doc, req_root, "params");
    }

    char* internal_req = yyjson_mut_write(req_doc, 0, NULL);
    yyjson_mut_doc_free(req_doc);

    /* Call internal API */
    char* internal_resp = NULL;
    size_t internal_resp_len = 0;
    api_process_rpc(api, internal_req, strlen(internal_req), &internal_resp, &internal_resp_len);
    free(internal_req);

    if (!internal_resp) {
        return strdup("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"Internal error\"},\"id\":null}");
    }

    /* Parse internal response and wrap in MCP format */
    yyjson_doc* resp_doc = yyjson_read(internal_resp, internal_resp_len, 0);
    free(internal_resp);

    if (!resp_doc) {
        return strdup("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"Parse error\"},\"id\":null}");
    }

    yyjson_val* resp_root = yyjson_doc_get_root(resp_doc);
    yyjson_val* result = yyjson_obj_get(resp_root, "result");
    yyjson_val* error = yyjson_obj_get(resp_root, "error");

    /* Build MCP response */
    yyjson_mut_doc* mcp_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* mcp_root = yyjson_mut_obj(mcp_doc);
    yyjson_mut_doc_set_root(mcp_doc, mcp_root);

    yyjson_mut_obj_add_str(mcp_doc, mcp_root, "jsonrpc", "2.0");
    if (yyjson_is_int(id)) {
        yyjson_mut_obj_add_int(mcp_doc, mcp_root, "id", yyjson_get_int(id));
    } else if (yyjson_is_str(id)) {
        yyjson_mut_obj_add_str(mcp_doc, mcp_root, "id", yyjson_get_str(id));
    }

    if (error) {
        yyjson_mut_val* mut_error = yyjson_val_mut_copy(mcp_doc, error);
        yyjson_mut_obj_add_val(mcp_doc, mcp_root, "error", mut_error);
    } else {
        /* Wrap result in MCP content format */
        yyjson_mut_val* mcp_result = yyjson_mut_obj(mcp_doc);

        /* Convert result to text content */
        /* Use INF_AND_NAN_AS_NULL flag to handle any NaN/Inf that slipped through */
        char* result_text = result ? yyjson_val_write(result, YYJSON_WRITE_INF_AND_NAN_AS_NULL, NULL) : NULL;
        if (!result_text) result_text = strdup("{}");

        yyjson_mut_val* content_arr = yyjson_mut_arr(mcp_doc);
        yyjson_mut_val* content_item = yyjson_mut_obj(mcp_doc);
        yyjson_mut_obj_add_str(mcp_doc, content_item, "type", "text");
        yyjson_mut_obj_add_str(mcp_doc, content_item, "text", result_text);
        yyjson_mut_arr_add_val(content_arr, content_item);
        free(result_text);

        yyjson_mut_obj_add_val(mcp_doc, mcp_result, "content", content_arr);
        yyjson_mut_obj_add_val(mcp_doc, mcp_root, "result", mcp_result);
    }

    yyjson_doc_free(resp_doc);

    char* response = yyjson_mut_write(mcp_doc, 0, NULL);
    yyjson_mut_doc_free(mcp_doc);
    return response;
}

/* Process MCP request */
static char* process_mcp_request(const char* request, size_t len, api_server_t* api) {
    yyjson_doc* doc = yyjson_read(request, len, 0);
    if (!doc) {
        return strdup("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"},\"id\":null}");
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* method = yyjson_obj_get(root, "method");
    yyjson_val* id = yyjson_obj_get(root, "id");
    yyjson_val* params = yyjson_obj_get(root, "params");

    char* response = NULL;

    if (!method || !yyjson_is_str(method)) {
        response = strdup("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"Invalid request\"},\"id\":null}");
    } else {
        const char* method_str = yyjson_get_str(method);

        if (strcmp(method_str, "initialize") == 0) {
            response = handle_mcp_initialize(id);
        } else if (strcmp(method_str, "tools/list") == 0) {
            response = handle_mcp_tools_list(id);
        } else if (strcmp(method_str, "tools/call") == 0) {
            response = handle_mcp_tools_call(id, params, api);
        } else if (strcmp(method_str, "notifications/initialized") == 0) {
            /* Notification - no response needed, but we'll acknowledge */
            response = NULL;
        } else if (strcmp(method_str, "ping") == 0) {
            /* Simple ping response */
            yyjson_mut_doc* resp_doc = yyjson_mut_doc_new(NULL);
            yyjson_mut_val* resp_root = yyjson_mut_obj(resp_doc);
            yyjson_mut_doc_set_root(resp_doc, resp_root);
            yyjson_mut_obj_add_str(resp_doc, resp_root, "jsonrpc", "2.0");
            if (yyjson_is_int(id)) yyjson_mut_obj_add_int(resp_doc, resp_root, "id", yyjson_get_int(id));
            yyjson_mut_obj_add_obj(resp_doc, resp_root, "result");
            response = yyjson_mut_write(resp_doc, 0, NULL);
            yyjson_mut_doc_free(resp_doc);
        } else {
            /* Unknown method */
            yyjson_mut_doc* resp_doc = yyjson_mut_doc_new(NULL);
            yyjson_mut_val* resp_root = yyjson_mut_obj(resp_doc);
            yyjson_mut_doc_set_root(resp_doc, resp_root);
            yyjson_mut_obj_add_str(resp_doc, resp_root, "jsonrpc", "2.0");
            if (yyjson_is_int(id)) yyjson_mut_obj_add_int(resp_doc, resp_root, "id", yyjson_get_int(id));
            yyjson_mut_val* error = yyjson_mut_obj(resp_doc);
            yyjson_mut_obj_add_int(resp_doc, error, "code", -32601);
            yyjson_mut_obj_add_str(resp_doc, error, "message", "Method not found");
            yyjson_mut_obj_add_val(resp_doc, resp_root, "error", error);
            response = yyjson_mut_write(resp_doc, 0, NULL);
            yyjson_mut_doc_free(resp_doc);
        }
    }

    yyjson_doc_free(doc);
    return response;
}

/* Print usage */
static void print_usage(const char* prog) {
    fprintf(stderr, "Memory Service MCP Server v%d.%d.%d\n\n",
           MEMORY_SERVICE_VERSION_MAJOR,
           MEMORY_SERVICE_VERSION_MINOR,
           MEMORY_SERVICE_VERSION_PATCH);
    fprintf(stderr, "Usage: %s [OPTIONS]\n\n", prog);
    fprintf(stderr, "MCP server for AI coding agent integration.\n");
    fprintf(stderr, "Communicates via JSON-RPC 2.0 over stdin/stdout.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d, --data-dir DIR    Data directory (default: ./data)\n");
    fprintf(stderr, "  -m, --model PATH      ONNX model path for embeddings\n");
    fprintf(stderr, "  -c, --capacity N      Max nodes capacity (default: 10000)\n");
    fprintf(stderr, "  -h, --help            Show this help\n");
    fprintf(stderr, "\nMCP Integration (Claude Code settings.json):\n");
    fprintf(stderr, "  \"mcpServers\": {\n");
    fprintf(stderr, "    \"memory\": {\n");
    fprintf(stderr, "      \"command\": \"%s\",\n", prog);
    fprintf(stderr, "      \"args\": [\"--model\", \"./models/all-MiniLM-L6-v2/model.onnx\"]\n");
    fprintf(stderr, "    }\n");
    fprintf(stderr, "  }\n");
}

int main(int argc, char** argv) {
    const char* data_dir = "./data";
    const char* model_path = NULL;
    size_t capacity = 10000;

    /* Parse command line options */
    static struct option long_options[] = {
        {"data-dir", required_argument, 0, 'd'},
        {"model",    required_argument, 0, 'm'},
        {"capacity", required_argument, 0, 'c'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:m:c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                data_dir = optarg;
                break;
            case 'm':
                model_path = optarg;
                break;
            case 'c':
                capacity = (size_t)atol(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* MCP servers should be quiet - only JSON on stdout, logs to stderr */
    log_config_t log_cfg = {
        .level = LOG_WARN,
        .output = stderr,
        .include_timestamp = false,
        .include_location = false,
        .colorize = false
    };
    log_init(&log_cfg);

    /* Ensure data directories exist */
    char embeddings_dir[512], relations_dir[512];
    snprintf(embeddings_dir, sizeof(embeddings_dir), "%s/embeddings", data_dir);
    snprintf(relations_dir, sizeof(relations_dir), "%s/relations", data_dir);

    if (ensure_dir(data_dir) != 0 ||
        ensure_dir(embeddings_dir) != 0 ||
        ensure_dir(relations_dir) != 0) {
        fprintf(stderr, "Failed to create data directories\n");
        return 1;
    }

    /* Initialize components */
    mem_error_t err = MEM_OK;
    hierarchy_t* hierarchy = NULL;
    embedding_engine_t* embedding_engine = NULL;
    search_engine_t* search = NULL;
    api_server_t* api = NULL;

    /* 1. Initialize hierarchy - try open existing first */
    err = hierarchy_open(&hierarchy, data_dir);
    if (err != MEM_OK) {
        err = hierarchy_create(&hierarchy, data_dir, capacity);
        if (err != MEM_OK) {
            fprintf(stderr, "Failed to create hierarchy: %d\n", err);
            goto cleanup;
        }
    }

    /* 2. Initialize embedding engine */
    embedding_config_t emb_cfg = EMBEDDING_CONFIG_DEFAULT;
    emb_cfg.model_path = model_path;
    err = embedding_engine_create(&embedding_engine, &emb_cfg);
    if (err != MEM_OK) {
        fprintf(stderr, "Failed to create embedding engine: %d\n", err);
        goto cleanup;
    }

    /* 3. Initialize search engine */
    search_config_t search_cfg = SEARCH_CONFIG_DEFAULT;
    err = search_engine_create(&search, hierarchy, &search_cfg);
    if (err != MEM_OK) {
        fprintf(stderr, "Failed to create search engine: %d\n", err);
        goto cleanup;
    }

    /* 4. Create API handler */
    api_config_t api_cfg = API_CONFIG_DEFAULT;
    err = api_server_create(&api, hierarchy, search, embedding_engine, &api_cfg);
    if (err != MEM_OK) {
        fprintf(stderr, "Failed to create API: %d\n", err);
        goto cleanup;
    }

    /* MCP main loop - read requests from stdin, write responses to stdout */
    char line[65536];
    while (!g_shutdown && fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);

        /* Skip empty lines */
        if (len == 0 || (len == 1 && line[0] == '\n')) {
            continue;
        }

        /* Remove trailing newline */
        if (len > 0 && line[len-1] == '\n') {
            line[--len] = '\0';
        }

        /* Process MCP request */
        char* response = process_mcp_request(line, len, api);

        if (response) {
            printf("%s\n", response);
            fflush(stdout);
            free(response);
        }
        /* Notifications don't require response */
    }

cleanup:
    if (api) api_server_destroy(api);
    if (search) search_engine_destroy(search);
    if (embedding_engine) embedding_engine_destroy(embedding_engine);
    if (hierarchy) hierarchy_close(hierarchy);

    return (err == MEM_OK) ? 0 : 1;
}
