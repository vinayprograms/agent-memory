/*
 * Memory Service - HTTP Server
 *
 * HTTP server using libmicrohttpd for JSON-RPC and health/metrics endpoints.
 * When HAVE_MICROHTTPD is not defined, provides stub implementation.
 */

#include "api.h"
#include "../util/log.h"
#include "../util/time.h"
#include "../../third_party/yyjson/yyjson.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdatomic.h>

#ifdef HAVE_MICROHTTPD
#include <microhttpd.h>
#endif

/* API server structure */
struct api_server {
    api_config_t        config;
    hierarchy_t*        hierarchy;
    search_engine_t*    search;
    embedding_engine_t* embedding;
    rpc_context_t*      rpc_ctx;
    _Atomic bool        running;        /* Thread-safe flag for concurrent access */
    uint64_t            start_time;
    _Atomic uint64_t    request_count;  /* Thread-safe counter for concurrent HTTP handlers */
    _Atomic uint64_t    error_count;    /* Thread-safe counter for concurrent HTTP handlers */

#ifdef HAVE_MICROHTTPD
    struct MHD_Daemon* daemon;
#endif
};

/* Request context for libmicrohttpd */
typedef struct {
    char*   post_data;
    size_t  post_size;
    size_t  post_alloc;
} request_ctx_t;

#ifdef HAVE_MICROHTTPD

/* Build response for request */
static enum MHD_Result handle_request(void* cls,
                                      struct MHD_Connection* connection,
                                      const char* url,
                                      const char* method,
                                      const char* version,
                                      const char* upload_data,
                                      size_t* upload_data_size,
                                      void** con_cls) {
    api_server_t* server = (api_server_t*)cls;
    (void)version;

    /* First call: allocate context */
    if (*con_cls == NULL) {
        request_ctx_t* ctx = calloc(1, sizeof(request_ctx_t));
        if (!ctx) return MHD_NO;
        *con_cls = ctx;
        return MHD_YES;
    }

    request_ctx_t* ctx = (request_ctx_t*)*con_cls;

    /* Handle POST data */
    if (strcmp(method, "POST") == 0 && *upload_data_size > 0) {
        /* Accumulate POST data */
        size_t new_size = ctx->post_size + *upload_data_size;
        if (new_size > ctx->post_alloc) {
            size_t new_alloc = ctx->post_alloc ? ctx->post_alloc * 2 : 4096;
            while (new_alloc < new_size) new_alloc *= 2;

            char* new_data = realloc(ctx->post_data, new_alloc);
            if (!new_data) return MHD_NO;

            ctx->post_data = new_data;
            ctx->post_alloc = new_alloc;
        }

        memcpy(ctx->post_data + ctx->post_size, upload_data, *upload_data_size);
        ctx->post_size = new_size;
        *upload_data_size = 0;
        return MHD_YES;
    }

    atomic_fetch_add(&server->request_count, 1);

    /* Route request */
    struct MHD_Response* response = NULL;
    int status_code = MHD_HTTP_OK;
    char* response_data = NULL;
    size_t response_len = 0;

    if (strcmp(url, "/health") == 0 && strcmp(method, "GET") == 0) {
        /* Health endpoint */
        health_result_t health;
        api_get_health(server, &health);
        api_format_health(&health, &response_data, &response_len);

    } else if (strcmp(url, "/metrics") == 0 && strcmp(method, "GET") == 0) {
        /* Metrics endpoint */
        metrics_result_t metrics;
        api_get_metrics(server, &metrics);
        api_format_metrics(&metrics, &response_data, &response_len);

    } else if (strcmp(url, "/rpc") == 0 && strcmp(method, "POST") == 0) {
        /* JSON-RPC endpoint */
        if (ctx->post_data && ctx->post_size > 0) {
            rpc_request_t request;
            void* doc = NULL;

            mem_error_t err = rpc_parse_request(ctx->post_data, ctx->post_size, &request, &doc);
            if (err == MEM_OK) {
                rpc_response_t rpc_resp;
                rpc_execute(server->rpc_ctx, &request, &rpc_resp);
                rpc_serialize_response(&rpc_resp, request.id, &response_data, &response_len);
                rpc_response_free(&rpc_resp);
                rpc_request_free(doc);
            } else {
                /* Parse error response */
                status_code = MHD_HTTP_BAD_REQUEST;
                const char* error_json = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"},\"id\":null}";
                response_data = strdup(error_json);
                response_len = strlen(error_json);
                atomic_fetch_add(&server->error_count, 1);
            }
        } else {
            status_code = MHD_HTTP_BAD_REQUEST;
            const char* error_json = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"Empty request\"},\"id\":null}";
            response_data = strdup(error_json);
            response_len = strlen(error_json);
            atomic_fetch_add(&server->error_count, 1);
        }

    } else {
        /* Not found */
        status_code = MHD_HTTP_NOT_FOUND;
        response_data = strdup("{\"error\":\"not found\"}");
        response_len = strlen(response_data);
        atomic_fetch_add(&server->error_count, 1);
    }

    /* Create response */
    response = MHD_create_response_from_buffer(response_len,
                                               response_data,
                                               MHD_RESPMEM_MUST_FREE);

    /* Add content type header */
    if (strcmp(url, "/metrics") == 0) {
        MHD_add_response_header(response, "Content-Type", "text/plain; charset=utf-8");
    } else {
        MHD_add_response_header(response, "Content-Type", "application/json");
    }

    enum MHD_Result ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);

    /* Cleanup context */
    if (ctx->post_data) {
        free(ctx->post_data);
    }
    free(ctx);
    *con_cls = NULL;

    return ret;
}

#endif /* HAVE_MICROHTTPD */

/*
 * Create and start the API server
 */
mem_error_t api_server_create(api_server_t** server,
                              hierarchy_t* hierarchy,
                              search_engine_t* search,
                              embedding_engine_t* embedding,
                              const api_config_t* config) {
    if (!server) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "server is NULL");
    }

    api_server_t* s = calloc(1, sizeof(api_server_t));
    MEM_CHECK_ALLOC(s);

    s->hierarchy = hierarchy;
    s->search = search;
    s->embedding = embedding;
    s->start_time = time_now_ms();
    s->request_count = 0;
    s->error_count = 0;

    if (config) {
        s->config = *config;
    } else {
        s->config = (api_config_t)API_CONFIG_DEFAULT;
    }

    /* Create RPC context */
    mem_error_t err = rpc_context_create(&s->rpc_ctx, hierarchy, search, embedding);
    if (err != MEM_OK) {
        free(s);
        return err;
    }

#ifdef HAVE_MICROHTTPD
    /* Start HTTP daemon */
    s->daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY | MHD_USE_THREAD_PER_CONNECTION,
        s->config.port,
        NULL, NULL,
        handle_request, s,
        MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)(s->config.timeout_ms / 1000),
        MHD_OPTION_END
    );

    if (!s->daemon) {
        rpc_context_destroy(s->rpc_ctx);
        free(s);
        MEM_RETURN_ERROR(MEM_ERR_IO, "failed to start HTTP daemon");
    }

    atomic_store(&s->running, true);
    LOG_INFO("API server started on port %d", s->config.port);
#else
    /* No HTTP support, just mark as not running */
    atomic_store(&s->running, false);
    LOG_WARN("HTTP server not available (libmicrohttpd not compiled in)");
#endif

    *server = s;
    return MEM_OK;
}

/*
 * Stop and destroy the API server
 */
void api_server_destroy(api_server_t* server) {
    if (!server) return;

#ifdef HAVE_MICROHTTPD
    if (server->daemon) {
        MHD_stop_daemon(server->daemon);
        LOG_INFO("API server stopped");
    }
#endif

    rpc_context_destroy(server->rpc_ctx);
    free(server);
}

/*
 * Check if server is running
 */
bool api_server_running(const api_server_t* server) {
    return server ? atomic_load(&((api_server_t*)server)->running) : false;
}

/*
 * Get server port
 */
uint16_t api_server_port(const api_server_t* server) {
    return server ? server->config.port : 0;
}

/*
 * Get request count
 */
uint64_t api_server_request_count(const api_server_t* server) {
    return server ? server->request_count : 0;
}

/*
 * Get health status
 */
mem_error_t api_get_health(const api_server_t* server, health_result_t* health) {
    if (!health) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "health is NULL");
    }

    memset(health, 0, sizeof(*health));

    if (server) {
        health->healthy = true;
        snprintf(health->status, sizeof(health->status), "ok");
        health->node_count = server->hierarchy ? hierarchy_count(server->hierarchy) : 0;
        health->uptime_ms = time_now_ms() - server->start_time;
        health->request_count = server->request_count;
    } else {
        health->healthy = false;
        snprintf(health->status, sizeof(health->status), "not initialized");
    }

    return MEM_OK;
}

/*
 * Format health as JSON
 */
mem_error_t api_format_health(const health_result_t* health,
                              char** json_out, size_t* len_out) {
    if (!health || !json_out || !len_out) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "NULL argument");
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "status", health->healthy ? "healthy" : "unhealthy");
    yyjson_mut_obj_add_uint(doc, root, "node_count", health->node_count);
    yyjson_mut_obj_add_uint(doc, root, "uptime_ms", health->uptime_ms);
    yyjson_mut_obj_add_uint(doc, root, "request_count", health->request_count);

    size_t len;
    char* json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
    yyjson_mut_doc_free(doc);

    if (!json) {
        MEM_RETURN_ERROR(MEM_ERR_IO, "failed to serialize health JSON");
    }

    *json_out = json;
    *len_out = len;
    return MEM_OK;
}

/*
 * Get metrics
 */
mem_error_t api_get_metrics(const api_server_t* server, metrics_result_t* metrics) {
    if (!metrics) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "metrics is NULL");
    }

    memset(metrics, 0, sizeof(*metrics));

    if (server) {
        metrics->requests_total = server->request_count;
        metrics->requests_success = server->request_count - server->error_count;
        metrics->requests_error = server->error_count;
        metrics->latency_avg_ms = 0.0;  /* TODO: track latency */
        metrics->latency_p99_ms = 0.0;
        metrics->nodes_indexed = server->hierarchy ? hierarchy_count(server->hierarchy) : 0;
        metrics->memory_bytes = 0;  /* TODO: track memory */
    }

    return MEM_OK;
}

/*
 * Format metrics as Prometheus exposition format
 */
mem_error_t api_format_metrics(const metrics_result_t* metrics,
                               char** text_out, size_t* len_out) {
    if (!metrics || !text_out || !len_out) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "NULL argument");
    }

    /* Allocate buffer */
    size_t buf_size = 4096;
    char* buf = malloc(buf_size);
    MEM_CHECK_ALLOC(buf);

    size_t pos = 0;

    /* Write Prometheus metrics */
    pos += snprintf(buf + pos, buf_size - pos,
        "# HELP memory_service_requests_total Total number of requests\n"
        "# TYPE memory_service_requests_total counter\n"
        "memory_service_requests_total %lu\n\n",
        (unsigned long)metrics->requests_total);

    pos += snprintf(buf + pos, buf_size - pos,
        "# HELP memory_service_requests_success Successful requests\n"
        "# TYPE memory_service_requests_success counter\n"
        "memory_service_requests_success %lu\n\n",
        (unsigned long)metrics->requests_success);

    pos += snprintf(buf + pos, buf_size - pos,
        "# HELP memory_service_requests_error Failed requests\n"
        "# TYPE memory_service_requests_error counter\n"
        "memory_service_requests_error %lu\n\n",
        (unsigned long)metrics->requests_error);

    pos += snprintf(buf + pos, buf_size - pos,
        "# HELP memory_service_latency_avg_ms Average request latency\n"
        "# TYPE memory_service_latency_avg_ms gauge\n"
        "memory_service_latency_avg_ms %.3f\n\n",
        metrics->latency_avg_ms);

    pos += snprintf(buf + pos, buf_size - pos,
        "# HELP memory_service_latency_p99_ms 99th percentile request latency\n"
        "# TYPE memory_service_latency_p99_ms gauge\n"
        "memory_service_latency_p99_ms %.3f\n\n",
        metrics->latency_p99_ms);

    pos += snprintf(buf + pos, buf_size - pos,
        "# HELP memory_service_nodes_indexed Number of indexed nodes\n"
        "# TYPE memory_service_nodes_indexed gauge\n"
        "memory_service_nodes_indexed %lu\n\n",
        (unsigned long)metrics->nodes_indexed);

    pos += snprintf(buf + pos, buf_size - pos,
        "# HELP memory_service_memory_bytes Memory usage in bytes\n"
        "# TYPE memory_service_memory_bytes gauge\n"
        "memory_service_memory_bytes %lu\n",
        (unsigned long)metrics->memory_bytes);

    *text_out = buf;
    *len_out = pos;
    return MEM_OK;
}

/*
 * Process a single RPC request (for testing without HTTP server)
 */
mem_error_t api_process_rpc(api_server_t* server,
                            const char* request_json, size_t request_len,
                            char** response_json, size_t* response_len) {
    if (!server || !request_json || !response_json || !response_len) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "NULL argument");
    }

    atomic_fetch_add(&server->request_count, 1);

    rpc_request_t request;
    void* doc = NULL;

    mem_error_t err = rpc_parse_request(request_json, request_len, &request, &doc);
    if (err != MEM_OK) {
        atomic_fetch_add(&server->error_count, 1);
        const char* error_resp = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"},\"id\":null}";
        *response_json = strdup(error_resp);
        *response_len = strlen(error_resp);
        return MEM_OK;
    }

    rpc_response_t rpc_resp;
    rpc_execute(server->rpc_ctx, &request, &rpc_resp);

    if (rpc_resp.is_error) {
        atomic_fetch_add(&server->error_count, 1);
    }

    rpc_serialize_response(&rpc_resp, request.id, response_json, response_len);
    rpc_response_free(&rpc_resp);
    rpc_request_free(doc);

    return MEM_OK;
}
