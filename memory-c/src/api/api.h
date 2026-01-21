/*
 * Memory Service - API Layer
 *
 * JSON-RPC 2.0 over HTTP API for the memory service.
 *
 * Endpoints:
 *   POST /rpc       - JSON-RPC 2.0 methods
 *   GET /health     - Health check
 *   GET /metrics    - Prometheus metrics
 */

#ifndef MEMORY_SERVICE_API_H
#define MEMORY_SERVICE_API_H

#include "../../include/types.h"
#include "../../include/error.h"
#include "../search/search.h"
#include "../embedding/embedding.h"

/* Forward declarations */
typedef struct api_server api_server_t;
typedef struct rpc_context rpc_context_t;

/* API configuration */
typedef struct {
    uint16_t    port;           /* HTTP port (default: 8080) */
    size_t      max_connections;/* Max concurrent connections */
    size_t      thread_pool;    /* Thread pool size for requests */
    uint32_t    timeout_ms;     /* Request timeout in ms */
} api_config_t;

#define API_CONFIG_DEFAULT { \
    .port = 8080, \
    .max_connections = 100, \
    .thread_pool = 4, \
    .timeout_ms = 10000 \
}

/* RPC request (parsed) */
typedef struct {
    const char* jsonrpc;    /* Must be "2.0" */
    const char* method;     /* Method name */
    void*       params;     /* yyjson_val* params object */
    void*       id;         /* Request ID (number or string), NULL for notification */
} rpc_request_t;

/* RPC response */
typedef struct {
    bool        is_error;
    int         error_code;
    const char* error_message;
    void*       result;     /* yyjson_val* for serialization */
} rpc_response_t;

/* JSON-RPC 2.0 error codes */
typedef enum {
    RPC_ERROR_PARSE = -32700,       /* Invalid JSON */
    RPC_ERROR_INVALID_REQUEST = -32600,  /* Not a valid Request */
    RPC_ERROR_METHOD_NOT_FOUND = -32601, /* Method does not exist */
    RPC_ERROR_INVALID_PARAMS = -32602,   /* Invalid method parameters */
    RPC_ERROR_INTERNAL = -32603,         /* Internal error */
    /* Server errors: -32000 to -32099 */
    RPC_ERROR_SERVER = -32000,
} rpc_error_code_t;

/*
 * API Server functions
 */

/* Create and start the API server */
mem_error_t api_server_create(api_server_t** server,
                              hierarchy_t* hierarchy,
                              search_engine_t* search,
                              embedding_engine_t* embedding,
                              const api_config_t* config);

/* Stop and destroy the API server */
void api_server_destroy(api_server_t* server);

/* Check if server is running */
bool api_server_running(const api_server_t* server);

/* Get server port */
uint16_t api_server_port(const api_server_t* server);

/* Get request count */
uint64_t api_server_request_count(const api_server_t* server);

/*
 * JSON-RPC processing functions (also usable standalone)
 */

/* Create RPC context */
mem_error_t rpc_context_create(rpc_context_t** ctx,
                               hierarchy_t* hierarchy,
                               search_engine_t* search,
                               embedding_engine_t* embedding);

/* Destroy RPC context */
void rpc_context_destroy(rpc_context_t* ctx);

/* Parse JSON-RPC request */
mem_error_t rpc_parse_request(const char* json, size_t len,
                              rpc_request_t* request, void** doc);

/* Execute RPC method */
mem_error_t rpc_execute(rpc_context_t* ctx,
                        const rpc_request_t* request,
                        rpc_response_t* response);

/* Serialize RPC response to JSON */
mem_error_t rpc_serialize_response(const rpc_response_t* response,
                                   void* request_id,
                                   char** json_out, size_t* len_out);

/* Free parsed request resources */
void rpc_request_free(void* doc);

/* Free response resources */
void rpc_response_free(rpc_response_t* response);

/*
 * Health check result
 */
typedef struct {
    bool    healthy;
    char    status[64];
    size_t  node_count;
    uint64_t uptime_ms;
    uint64_t request_count;
} health_result_t;

/* Get health status */
mem_error_t api_get_health(const api_server_t* server, health_result_t* health);

/* Format health as JSON */
mem_error_t api_format_health(const health_result_t* health,
                              char** json_out, size_t* len_out);

/*
 * Metrics result
 */
typedef struct {
    uint64_t requests_total;
    uint64_t requests_success;
    uint64_t requests_error;
    double   latency_avg_ms;
    double   latency_p99_ms;
    size_t   nodes_indexed;
    size_t   memory_bytes;
} metrics_result_t;

/* Get metrics */
mem_error_t api_get_metrics(const api_server_t* server, metrics_result_t* metrics);

/* Format metrics as Prometheus exposition format */
mem_error_t api_format_metrics(const metrics_result_t* metrics,
                               char** text_out, size_t* len_out);

/*
 * Process an RPC request directly (for testing without HTTP server)
 */
mem_error_t api_process_rpc(api_server_t* server,
                            const char* request_json, size_t request_len,
                            char** response_json, size_t* response_len);

#endif /* MEMORY_SERVICE_API_H */
