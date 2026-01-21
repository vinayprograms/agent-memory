/*
 * Memory Service - JSON-RPC 2.0 Handler
 *
 * Implements JSON-RPC 2.0 request parsing and response serialization.
 */

#include "api.h"
#include "../util/log.h"
#include "../util/text.h"
#include "../../third_party/yyjson/yyjson.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* RPC context */
struct rpc_context {
    hierarchy_t*        hierarchy;
    search_engine_t*    search;
    embedding_engine_t* embedding;
};

/* Response metadata for logging (no PII) */
typedef struct {
    size_t  match_count;        /* Number of matches returned */
    char    levels[128];        /* Levels returned (comma-separated) */
    /* Store operation metadata */
    char    agent_id[64];       /* Agent identifier for store ops */
    size_t  blocks_count;       /* Blocks created */
    size_t  statements_count;   /* Statements created */
    /* Timing breakdown (in milliseconds) */
    double  parse_ms;           /* Parameter parsing/validation */
    double  embed_ms;           /* Embedding generation */
    double  search_ms;          /* Search/filter operation */
    double  build_ms;           /* Response building */
    double  hierarchy_ms;       /* Hierarchy operations (create node, etc.) */
    double  index_ms;           /* Search index update */
} rpc_result_metadata_t;

/* Helper to mark a timing checkpoint and return elapsed */
static inline double checkpoint_ms(struct timespec* checkpoint) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (now.tv_sec - checkpoint->tv_sec) * 1000.0 +
                     (now.tv_nsec - checkpoint->tv_nsec) / 1000000.0;
    *checkpoint = now;
    return elapsed;
}

/* Extended response for internal use - stores the mutable doc */
typedef struct {
    rpc_response_t       base;
    yyjson_mut_doc*      result_doc;   /* Mutable doc owning the result */
    rpc_result_metadata_t metadata;    /* Logging metadata */
} rpc_response_internal_t;

/* Method handler function type */
typedef mem_error_t (*method_handler_t)(rpc_context_t* ctx,
                                        yyjson_val* params,
                                        rpc_response_internal_t* response);

/* Method registry entry */
typedef struct {
    const char* name;
    method_handler_t handler;
} method_entry_t;

/* Forward declarations for method handlers */
static mem_error_t handle_store(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);
static mem_error_t handle_store_block(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);
static mem_error_t handle_store_statement(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);
static mem_error_t handle_query(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);
static mem_error_t handle_get_session(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);
static mem_error_t handle_list_sessions(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);
static mem_error_t handle_get_context(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);
static mem_error_t handle_drill_down(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);
static mem_error_t handle_zoom_out(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp);

/* Method registry */
static const method_entry_t g_methods[] = {
    {"store",           handle_store},
    {"store_block",     handle_store_block},
    {"store_statement", handle_store_statement},
    {"query",           handle_query},
    {"get_session",   handle_get_session},
    {"list_sessions", handle_list_sessions},
    {"get_context",   handle_get_context},
    {"drill_down",    handle_drill_down},
    {"zoom_out",      handle_zoom_out},
    {NULL, NULL}
};

/*
 * Create RPC context
 */
mem_error_t rpc_context_create(rpc_context_t** ctx,
                               hierarchy_t* hierarchy,
                               search_engine_t* search,
                               embedding_engine_t* embedding) {
    if (!ctx) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "ctx is NULL");
    }

    rpc_context_t* c = calloc(1, sizeof(rpc_context_t));
    MEM_CHECK_ALLOC(c);

    c->hierarchy = hierarchy;
    c->search = search;
    c->embedding = embedding;

    *ctx = c;
    return MEM_OK;
}

/*
 * Destroy RPC context
 */
void rpc_context_destroy(rpc_context_t* ctx) {
    if (ctx) {
        free(ctx);
    }
}

/*
 * Parse JSON-RPC request
 */
mem_error_t rpc_parse_request(const char* json, size_t len,
                              rpc_request_t* request, void** doc_out) {
    if (!json || !request || !doc_out) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "NULL argument");
    }

    /* Parse JSON */
    yyjson_doc* doc = yyjson_read(json, len, 0);
    if (!doc) {
        MEM_RETURN_ERROR(MEM_ERR_PARSE, "invalid JSON");
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        MEM_RETURN_ERROR(MEM_ERR_RPC, "request must be an object");
    }

    /* Extract jsonrpc version */
    yyjson_val* jsonrpc = yyjson_obj_get(root, "jsonrpc");
    if (!jsonrpc || !yyjson_is_str(jsonrpc)) {
        yyjson_doc_free(doc);
        MEM_RETURN_ERROR(MEM_ERR_RPC, "missing or invalid jsonrpc field");
    }
    request->jsonrpc = yyjson_get_str(jsonrpc);
    if (strcmp(request->jsonrpc, "2.0") != 0) {
        yyjson_doc_free(doc);
        MEM_RETURN_ERROR(MEM_ERR_RPC, "jsonrpc must be \"2.0\"");
    }

    /* Extract method */
    yyjson_val* method = yyjson_obj_get(root, "method");
    if (!method || !yyjson_is_str(method)) {
        yyjson_doc_free(doc);
        MEM_RETURN_ERROR(MEM_ERR_RPC, "missing or invalid method field");
    }
    request->method = yyjson_get_str(method);

    /* Extract params (optional) */
    yyjson_val* params = yyjson_obj_get(root, "params");
    request->params = params;  /* Can be NULL */

    /* Extract id (optional for notifications) */
    yyjson_val* id = yyjson_obj_get(root, "id");
    request->id = id;

    *doc_out = doc;
    return MEM_OK;
}

/*
 * Free parsed request
 */
void rpc_request_free(void* doc) {
    if (doc) {
        yyjson_doc_free((yyjson_doc*)doc);
    }
}

/*
 * Execute RPC method
 */
mem_error_t rpc_execute(rpc_context_t* ctx,
                        const rpc_request_t* request,
                        rpc_response_t* response) {
    if (!ctx || !request || !response) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "NULL argument");
    }

    /* Start timing */
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    /* Log incoming request (no params to avoid PII) */
    LOG_INFO("RPC request: method=%s", request->method);

    /* Use internal response type */
    rpc_response_internal_t int_resp;
    memset(&int_resp, 0, sizeof(int_resp));

    /* Find method handler */
    method_handler_t handler = NULL;
    for (const method_entry_t* m = g_methods; m->name; m++) {
        if (strcmp(m->name, request->method) == 0) {
            handler = m->handler;
            break;
        }
    }

    if (!handler) {
        int_resp.base.is_error = true;
        int_resp.base.error_code = RPC_ERROR_METHOD_NOT_FOUND;
        int_resp.base.error_message = "method not found";
    } else {
        /* Execute handler */
        mem_error_t err = handler(ctx, (yyjson_val*)request->params, &int_resp);
        if (err != MEM_OK) {
            int_resp.base.is_error = true;
            int_resp.base.error_code = RPC_ERROR_INTERNAL;
            int_resp.base.error_message = mem_error_str(err);
            /* Clean up any partial result */
            if (int_resp.result_doc) {
                yyjson_mut_doc_free(int_resp.result_doc);
                int_resp.result_doc = NULL;
            }
        }
    }

    /* Calculate elapsed time in milliseconds */
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                      (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;

    /* Build timing breakdown string */
    rpc_result_metadata_t* m = &int_resp.metadata;
    char timing_buf[256] = "";
    size_t pos = 0;
    size_t remaining;
#define TIMING_SNPRINTF(fmt, val) do { \
    if (pos < sizeof(timing_buf)) { \
        remaining = sizeof(timing_buf) - pos; \
        int written = snprintf(timing_buf + pos, remaining, fmt, val); \
        if (written > 0 && (size_t)written < remaining) pos += written; \
        else if (written > 0) pos = sizeof(timing_buf); \
    } \
} while(0)

    if (m->parse_ms > 0.01) TIMING_SNPRINTF("parse=%.2f ", m->parse_ms);
    if (m->hierarchy_ms > 0.01) TIMING_SNPRINTF("hierarchy=%.2f ", m->hierarchy_ms);
    if (m->embed_ms > 0.01) TIMING_SNPRINTF("embed=%.2f ", m->embed_ms);
    if (m->index_ms > 0.01) TIMING_SNPRINTF("index=%.2f ", m->index_ms);
    if (m->search_ms > 0.01) TIMING_SNPRINTF("search=%.2f ", m->search_ms);
    if (m->build_ms > 0.01) TIMING_SNPRINTF("build=%.2f ", m->build_ms);
#undef TIMING_SNPRINTF
    /* Remove trailing space */
    if (pos > 0 && pos < sizeof(timing_buf) && timing_buf[pos-1] == ' ') timing_buf[pos-1] = '\0';

    /* Log response (no content to avoid PII) */
    if (int_resp.base.is_error) {
        LOG_INFO("RPC response: method=%s status=error code=%d elapsed=%.2fms",
                 request->method, int_resp.base.error_code, total_ms);
    } else if (strcmp(request->method, "store") == 0 && int_resp.metadata.agent_id[0]) {
        /* Store operation - show agent and counts */
        LOG_INFO("RPC response: method=%s status=ok agent=%s msgs=1 blocks=%zu stmts=%zu elapsed=%.2fms [%s]",
                 request->method, int_resp.metadata.agent_id,
                 int_resp.metadata.blocks_count, int_resp.metadata.statements_count,
                 total_ms, timing_buf[0] ? timing_buf : "n/a");
    } else if (int_resp.metadata.match_count > 0) {
        LOG_INFO("RPC response: method=%s status=ok matches=%zu levels=%s elapsed=%.2fms [%s]",
                 request->method, int_resp.metadata.match_count,
                 int_resp.metadata.levels[0] ? int_resp.metadata.levels : "n/a", total_ms,
                 timing_buf[0] ? timing_buf : "n/a");
    } else {
        LOG_INFO("RPC response: method=%s status=ok elapsed=%.2fms [%s]",
                 request->method, total_ms, timing_buf[0] ? timing_buf : "n/a");
    }

    /* Copy base response and store doc pointer */
    *response = int_resp.base;
    response->result = int_resp.result_doc;  /* Store doc pointer for serialization */

    return MEM_OK;
}

/*
 * Serialize RPC response
 */
mem_error_t rpc_serialize_response(const rpc_response_t* response,
                                   void* request_id,
                                   char** json_out, size_t* len_out) {
    if (!response || !json_out || !len_out) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_ARG, "NULL argument");
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to create JSON document");
    }

    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    /* Always include jsonrpc version */
    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");

    /* Include id (copy from request) */
    if (request_id) {
        yyjson_val* id = (yyjson_val*)request_id;
        if (yyjson_is_str(id)) {
            yyjson_mut_obj_add_strcpy(doc, root, "id", yyjson_get_str(id));
        } else if (yyjson_is_int(id)) {
            yyjson_mut_obj_add_int(doc, root, "id", yyjson_get_int(id));
        } else if (yyjson_is_null(id)) {
            yyjson_mut_obj_add_null(doc, root, "id");
        }
    } else {
        yyjson_mut_obj_add_null(doc, root, "id");
    }

    if (response->is_error) {
        /* Error response */
        yyjson_mut_val* error = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, error, "code", response->error_code);
        yyjson_mut_obj_add_str(doc, error, "message",
                               response->error_message ? response->error_message : "unknown error");
        yyjson_mut_obj_add_val(doc, root, "error", error);
    } else {
        /* Success response - copy result from handler's doc */
        yyjson_mut_doc* result_doc = (yyjson_mut_doc*)response->result;
        if (result_doc) {
            yyjson_mut_val* result = yyjson_mut_doc_get_root(result_doc);
            if (result) {
                /* Deep copy the result into our response doc */
                yyjson_mut_val* result_copy = yyjson_mut_val_mut_copy(doc, result);
                yyjson_mut_obj_add_val(doc, root, "result", result_copy);
            } else {
                yyjson_mut_obj_add_null(doc, root, "result");
            }
        } else {
            yyjson_mut_obj_add_null(doc, root, "result");
        }
    }

    /* Serialize */
    size_t len;
    char* json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
    yyjson_mut_doc_free(doc);

    if (!json) {
        MEM_RETURN_ERROR(MEM_ERR_IO, "failed to serialize JSON");
    }

    *json_out = json;
    *len_out = len;
    return MEM_OK;
}

/*
 * Free response resources
 */
void rpc_response_free(rpc_response_t* response) {
    if (response && response->result) {
        yyjson_mut_doc_free((yyjson_mut_doc*)response->result);
        response->result = NULL;
    }
}

/*
 * Method handlers
 */

/* Helper to create and set result document */
static yyjson_mut_val* create_result(rpc_response_internal_t* resp) {
    resp->result_doc = yyjson_mut_doc_new(NULL);
    if (!resp->result_doc) return NULL;

    yyjson_mut_val* result = yyjson_mut_obj(resp->result_doc);
    yyjson_mut_doc_set_root(resp->result_doc, result);
    return result;
}

/* Simple tokenizer - splits on whitespace and lowercases */
static size_t tokenize_query(const char* query, size_t len, char tokens[][64], size_t max_tokens) {
    size_t count = 0;
    size_t i = 0;

    while (i < len && count < max_tokens) {
        /* Skip whitespace */
        while (i < len && (query[i] == ' ' || query[i] == '\t' || query[i] == '\n')) i++;
        if (i >= len) break;

        /* Extract token */
        size_t start = i;
        while (i < len && query[i] != ' ' && query[i] != '\t' && query[i] != '\n') i++;

        size_t tok_len = i - start;
        if (tok_len > 0 && tok_len < 64) {
            for (size_t j = 0; j < tok_len; j++) {
                char c = query[start + j];
                tokens[count][j] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
            }
            tokens[count][tok_len] = '\0';
            count++;
        }
    }
    return count;
}

/* Helper to embed and index a node */
static void embed_and_index_node(rpc_context_t* ctx, node_id_t node_id,
                                  const char* text, size_t text_len) {
    if (!ctx->embedding) return;

    float embedding[EMBEDDING_DIM];
    mem_error_t err = embedding_generate(ctx->embedding, text, text_len, embedding);
    if (err != MEM_OK) return;

    hierarchy_set_embedding(ctx->hierarchy, node_id, embedding);

    if (ctx->search) {
        char index_tokens[64][64];
        size_t token_count = tokenize_query(text, text_len, index_tokens, 64);

        const char* token_ptrs[64];
        for (size_t i = 0; i < token_count; i++) {
            token_ptrs[i] = index_tokens[i];
        }

        search_engine_index(ctx->search, node_id, embedding,
                           token_ptrs, token_count, timestamp_now_ns());
    }
}

/* store: Ingest a message with automatic decomposition into blocks and statements */
static mem_error_t handle_store(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    if (!ctx->hierarchy) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "hierarchy not initialized";
        return MEM_OK;
    }

    if (!params || !yyjson_is_obj(params)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "params must be an object";
        return MEM_OK;
    }

    /* Extract required fields */
    yyjson_val* agent_id = yyjson_obj_get(params, "agent_id");
    yyjson_val* session_id = yyjson_obj_get(params, "session_id");
    yyjson_val* content = yyjson_obj_get(params, "content");
    yyjson_val* role = yyjson_obj_get(params, "role");  /* Optional: user/assistant/tool */

    if (!agent_id || !yyjson_is_str(agent_id)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid agent_id";
        return MEM_OK;
    }

    if (!session_id || !yyjson_is_str(session_id)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid session_id";
        return MEM_OK;
    }

    if (!content || !yyjson_is_str(content)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid content";
        return MEM_OK;
    }

    const char* content_str = yyjson_get_str(content);
    size_t content_len = yyjson_get_len(content);
    const char* role_str = role ? yyjson_get_str(role) : "user";
    (void)role_str;  /* TODO: store role in message metadata */
    resp->metadata.parse_ms = checkpoint_ms(&ts);

    /* Create or find agent */
    node_id_t agent_node_id;
    bool new_agent = true;
    mem_error_t err = hierarchy_create_agent(ctx->hierarchy,
                                             yyjson_get_str(agent_id),
                                             &agent_node_id);
    if (err == MEM_ERR_EXISTS) {
        new_agent = false;
    } else if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create agent";
        return MEM_OK;
    }

    /* Create or find session under agent */
    node_id_t session_node_id;
    bool new_session = true;
    err = hierarchy_create_session(ctx->hierarchy,
                                   agent_node_id,
                                   yyjson_get_str(session_id),
                                   &session_node_id);
    if (err == MEM_ERR_EXISTS) {
        new_session = false;
    } else if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create session";
        return MEM_OK;
    }
    (void)new_agent;  /* Suppress unused warning */

    /* Create message node */
    node_id_t message_id;
    err = hierarchy_create_message(ctx->hierarchy, session_node_id, &message_id);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create message";
        return MEM_OK;
    }

    /* Store text content for message */
    err = hierarchy_set_text(ctx->hierarchy, message_id, content_str, content_len);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to store message content";
        return MEM_OK;
    }
    resp->metadata.hierarchy_ms = checkpoint_ms(&ts);

    /* Embed and index the message */
    embed_and_index_node(ctx, message_id, content_str, content_len);

    /* Decompose content into blocks and statements */
    text_block_t blocks[MAX_BLOCKS];
    size_t block_count = text_split_blocks(content_str, content_len, blocks, MAX_BLOCKS);

    size_t total_blocks = 0;
    size_t total_statements = 0;

    for (size_t i = 0; i < block_count; i++) {
        text_block_t* block = &blocks[i];

        /* Skip empty blocks */
        if (text_is_empty(block->span)) continue;

        /* Create block node */
        node_id_t block_id;
        err = hierarchy_create_block(ctx->hierarchy, message_id, &block_id);
        if (err != MEM_OK) continue;

        /* Store block text */
        err = hierarchy_set_text(ctx->hierarchy, block_id, block->span.start, block->span.len);
        if (err != MEM_OK) continue;

        /* Embed and index the block */
        embed_and_index_node(ctx, block_id, block->span.start, block->span.len);
        total_blocks++;

        /* Split block into statements */
        text_span_t statements[MAX_STATEMENTS];
        size_t stmt_count = text_split_statements(block, statements, MAX_STATEMENTS);

        for (size_t j = 0; j < stmt_count; j++) {
            text_span_t* stmt = &statements[j];

            /* Skip empty statements */
            if (text_is_empty(*stmt)) continue;

            /* Create statement node */
            node_id_t stmt_id;
            err = hierarchy_create_statement(ctx->hierarchy, block_id, &stmt_id);
            if (err != MEM_OK) continue;

            /* Store statement text */
            err = hierarchy_set_text(ctx->hierarchy, stmt_id, stmt->start, stmt->len);
            if (err != MEM_OK) continue;

            /* Embed and index the statement */
            embed_and_index_node(ctx, stmt_id, stmt->start, stmt->len);
            total_statements++;
        }
    }

    resp->metadata.embed_ms = checkpoint_ms(&ts);
    resp->metadata.index_ms = checkpoint_ms(&ts);

    /* Populate logging metadata */
    snprintf(resp->metadata.agent_id, sizeof(resp->metadata.agent_id), "%s", yyjson_get_str(agent_id));
    resp->metadata.blocks_count = total_blocks;
    resp->metadata.statements_count = total_statements;
    resp->metadata.match_count = 1;  /* 1 message stored */

    /* Build result */
    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    yyjson_mut_obj_add_uint(resp->result_doc, result, "agent_id", agent_node_id);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "session_id", session_node_id);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "message_id", message_id);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "blocks_created", total_blocks);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "statements_created", total_statements);
    yyjson_mut_obj_add_bool(resp->result_doc, result, "new_session", new_session);
    resp->metadata.build_ms = checkpoint_ms(&ts);

    resp->base.is_error = false;
    return MEM_OK;
}

/* store_block: Create a block under a parent node */
static mem_error_t handle_store_block(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    if (!ctx->hierarchy) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "hierarchy not initialized";
        return MEM_OK;
    }

    if (!params || !yyjson_is_obj(params)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "params must be an object";
        return MEM_OK;
    }

    yyjson_val* parent_id_val = yyjson_obj_get(params, "parent_id");
    yyjson_val* content = yyjson_obj_get(params, "content");

    if (!parent_id_val || !yyjson_is_uint(parent_id_val)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid parent_id";
        return MEM_OK;
    }

    if (!content || !yyjson_is_str(content)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid content";
        return MEM_OK;
    }

    node_id_t parent_id = (node_id_t)yyjson_get_uint(parent_id_val);
    const char* content_str = yyjson_get_str(content);
    size_t content_len = yyjson_get_len(content);
    resp->metadata.parse_ms = checkpoint_ms(&ts);

    /* Create block node */
    node_id_t block_id;
    mem_error_t err = hierarchy_create_block(ctx->hierarchy, parent_id, &block_id);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create block";
        return MEM_OK;
    }

    /* Store text content */
    err = hierarchy_set_text(ctx->hierarchy, block_id, content_str, content_len);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to store block content";
        return MEM_OK;
    }
    resp->metadata.hierarchy_ms = checkpoint_ms(&ts);

    /* Generate embedding if embedding engine available */
    if (ctx->embedding) {
        float embedding[EMBEDDING_DIM];
        err = embedding_generate(ctx->embedding, content_str, content_len, embedding);
        resp->metadata.embed_ms = checkpoint_ms(&ts);

        if (err == MEM_OK) {
            hierarchy_set_embedding(ctx->hierarchy, block_id, embedding);

            if (ctx->search) {
                char index_tokens[64][64];
                size_t token_count = tokenize_query(content_str, content_len, index_tokens, 64);

                const char* token_ptrs[64];
                for (size_t i = 0; i < token_count; i++) {
                    token_ptrs[i] = index_tokens[i];
                }

                search_engine_index(ctx->search, block_id, embedding,
                                   token_ptrs, token_count, timestamp_now_ns());
                resp->metadata.index_ms = checkpoint_ms(&ts);
            }
        }
    }

    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    yyjson_mut_obj_add_uint(resp->result_doc, result, "block_id", block_id);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "parent_id", parent_id);
    resp->metadata.build_ms = checkpoint_ms(&ts);

    resp->base.is_error = false;
    return MEM_OK;
}

/* store_statement: Create a statement node under a block */
static mem_error_t handle_store_statement(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    if (!ctx->hierarchy) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "hierarchy not initialized";
        return MEM_OK;
    }

    if (!params || !yyjson_is_obj(params)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "params must be an object";
        return MEM_OK;
    }

    yyjson_val* parent_id_val = yyjson_obj_get(params, "parent_id");
    yyjson_val* content = yyjson_obj_get(params, "content");

    if (!parent_id_val || !yyjson_is_uint(parent_id_val)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid parent_id";
        return MEM_OK;
    }

    if (!content || !yyjson_is_str(content)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid content";
        return MEM_OK;
    }

    node_id_t parent_id = (node_id_t)yyjson_get_uint(parent_id_val);
    const char* content_str = yyjson_get_str(content);
    size_t content_len = yyjson_get_len(content);
    resp->metadata.parse_ms = checkpoint_ms(&ts);

    /* Create statement node */
    node_id_t stmt_id;
    mem_error_t err = hierarchy_create_statement(ctx->hierarchy, parent_id, &stmt_id);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create statement";
        return MEM_OK;
    }

    /* Store text content */
    err = hierarchy_set_text(ctx->hierarchy, stmt_id, content_str, content_len);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to store statement content";
        return MEM_OK;
    }
    resp->metadata.hierarchy_ms = checkpoint_ms(&ts);

    /* Generate embedding if embedding engine available */
    if (ctx->embedding) {
        float embedding[EMBEDDING_DIM];
        err = embedding_generate(ctx->embedding, content_str, content_len, embedding);
        resp->metadata.embed_ms = checkpoint_ms(&ts);

        if (err == MEM_OK) {
            hierarchy_set_embedding(ctx->hierarchy, stmt_id, embedding);

            if (ctx->search) {
                char index_tokens[64][64];
                size_t token_count = tokenize_query(content_str, content_len, index_tokens, 64);

                const char* token_ptrs[64];
                for (size_t i = 0; i < token_count; i++) {
                    token_ptrs[i] = index_tokens[i];
                }

                search_engine_index(ctx->search, stmt_id, embedding,
                                   token_ptrs, token_count, timestamp_now_ns());
                resp->metadata.index_ms = checkpoint_ms(&ts);
            }
        }
    }

    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    yyjson_mut_obj_add_uint(resp->result_doc, result, "statement_id", stmt_id);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "parent_id", parent_id);
    resp->metadata.build_ms = checkpoint_ms(&ts);

    resp->base.is_error = false;
    return MEM_OK;
}

/* Parse level name to hierarchy_level_t */
static hierarchy_level_t parse_level(const char* name) {
    if (!name) return LEVEL_COUNT;
    if (strcmp(name, "session") == 0) return LEVEL_SESSION;
    if (strcmp(name, "message") == 0) return LEVEL_MESSAGE;
    if (strcmp(name, "block") == 0) return LEVEL_BLOCK;
    if (strcmp(name, "statement") == 0) return LEVEL_STATEMENT;
    return LEVEL_COUNT;
}

/* query: Search across the memory hierarchy
 *
 * Parameters:
 *   query: search text (required)
 *   max_results: optional limit (default: 10, max: 100)
 *   level: optional single level to search (e.g., "block")
 *   top_level: highest level in hierarchy to search (default: "session")
 *   bottom_level: lowest level in hierarchy to search (default: "statement")
 *
 * Level hierarchy (top to bottom):
 *   session -> message -> block -> statement
 */
static mem_error_t handle_query(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    if (!ctx->search) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "search engine not initialized";
        return MEM_OK;
    }

    if (!params || !yyjson_is_obj(params)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "params must be an object";
        return MEM_OK;
    }

    yyjson_val* query_text = yyjson_obj_get(params, "query");
    if (!query_text || !yyjson_is_str(query_text)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid query";
        return MEM_OK;
    }

    const char* query_str = yyjson_get_str(query_text);
    size_t query_len = yyjson_get_len(query_text);

    /* Get optional params */
    yyjson_val* max_results_val = yyjson_obj_get(params, "max_results");
    size_t max_results = 10;
    if (max_results_val && yyjson_is_int(max_results_val)) {
        max_results = (size_t)yyjson_get_int(max_results_val);
        if (max_results > 100) max_results = 100;
    }

    /* Parse level constraints
     * Hierarchy (top to bottom): SESSION(0) -> MESSAGE(1) -> BLOCK(2) -> STATEMENT(3)
     * top_level = highest in tree (lower enum value)
     * bottom_level = lowest in tree (higher enum value)
     */
    hierarchy_level_t top_level = LEVEL_SESSION;     /* Highest in hierarchy */
    hierarchy_level_t bottom_level = LEVEL_STATEMENT; /* Lowest in hierarchy */

    /* Single level parameter overrides top/bottom */
    yyjson_val* level_val = yyjson_obj_get(params, "level");
    if (level_val && yyjson_is_str(level_val)) {
        hierarchy_level_t lvl = parse_level(yyjson_get_str(level_val));
        if (lvl < LEVEL_COUNT) {
            top_level = lvl;
            bottom_level = lvl;
        }
    } else {
        /* Check explicit top/bottom level params */
        yyjson_val* top_val = yyjson_obj_get(params, "top_level");
        if (top_val && yyjson_is_str(top_val)) {
            hierarchy_level_t lvl = parse_level(yyjson_get_str(top_val));
            if (lvl < LEVEL_COUNT) top_level = lvl;
        }

        yyjson_val* bottom_val = yyjson_obj_get(params, "bottom_level");
        if (bottom_val && yyjson_is_str(bottom_val)) {
            hierarchy_level_t lvl = parse_level(yyjson_get_str(bottom_val));
            if (lvl < LEVEL_COUNT) bottom_level = lvl;
        }
    }
    resp->metadata.parse_ms = checkpoint_ms(&ts);

    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    yyjson_mut_val* results_arr = yyjson_mut_arr(resp->result_doc);

    /* Max content length for query results (search results, not full retrieval) */
    const size_t MAX_CONTENT_LEN = 1000;

    /* Track unique levels for logging */
    bool seen_levels[4] = {false};  /* SESSION, MESSAGE, BLOCK, STATEMENT */
    size_t total_matches = 0;

    /* Semantic search using embeddings with level constraints */
    if (ctx->embedding) {
        float query_embedding[EMBEDDING_DIM];
        mem_error_t err = embedding_generate(ctx->embedding, query_str, query_len, query_embedding);
        resp->metadata.embed_ms = checkpoint_ms(&ts);

        if (err == MEM_OK) {
            search_match_t* matches = calloc(max_results, sizeof(search_match_t));
            if (matches) {
                size_t match_count = 0;

                /* Use full search API with level constraints
                 * Note: search API uses min/max where min=bottom (most granular),
                 * max=top (least granular) - opposite of tree visualization
                 */
                search_query_t sq = {
                    .embedding = query_embedding,
                    .tokens = NULL,
                    .token_count = 0,
                    .k = max_results,
                    .min_level = bottom_level,  /* Most granular = bottom of tree */
                    .max_level = top_level      /* Least granular = top of tree */
                };

                err = search_engine_search(ctx->search, &sq, matches, &match_count);
                resp->metadata.search_ms = checkpoint_ms(&ts);

                if (err == MEM_OK) {
                    total_matches = match_count;
                    for (size_t i = 0; i < match_count; i++) {
                        yyjson_mut_val* match_obj = yyjson_mut_obj(resp->result_doc);
                        yyjson_mut_obj_add_uint(resp->result_doc, match_obj, "node_id", matches[i].node_id);
                        yyjson_mut_obj_add_str(resp->result_doc, match_obj, "level",
                                              level_name(matches[i].level));
                        /* Guard against NaN/Inf scores which break JSON serialization */
                        float score = matches[i].score;
                        if (isnan(score) || isinf(score)) score = 0.0f;
                        yyjson_mut_obj_add_real(resp->result_doc, match_obj, "score", score);

                        /* Track level for logging */
                        if (matches[i].level < 4) seen_levels[matches[i].level] = true;

                        /* Include truncated text content if available */
                        size_t text_len;
                        const char* text = hierarchy_get_text(ctx->hierarchy, matches[i].node_id, &text_len);
                        if (text) {
                            size_t content_len = text_len > MAX_CONTENT_LEN ? MAX_CONTENT_LEN : text_len;
                            yyjson_mut_obj_add_strncpy(resp->result_doc, match_obj, "content", text, content_len);
                        }

                        /* Include children count for agent navigation */
                        node_id_t child_ids[1];
                        size_t child_count = hierarchy_get_children(ctx->hierarchy, matches[i].node_id, child_ids, 1);
                        yyjson_mut_obj_add_uint(resp->result_doc, match_obj, "children_count", child_count);

                        yyjson_mut_arr_add_val(results_arr, match_obj);
                    }
                }
                free(matches);
            }
        }
    }

    yyjson_mut_obj_add_val(resp->result_doc, result, "results", results_arr);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "total_matches", yyjson_mut_arr_size(results_arr));
    yyjson_mut_obj_add_str(resp->result_doc, result, "top_level", level_name(top_level));
    yyjson_mut_obj_add_str(resp->result_doc, result, "bottom_level", level_name(bottom_level));
    yyjson_mut_obj_add_bool(resp->result_doc, result, "truncated", false);

    /* Populate logging metadata */
    resp->metadata.match_count = total_matches;
    /* Must match enum order: STATEMENT=0, BLOCK=1, MESSAGE=2, SESSION=3 */
    const char* level_names_short[] = {"statement", "block", "message", "session"};
    resp->metadata.levels[0] = '\0';
    for (int i = 0; i < 4; i++) {
        if (seen_levels[i]) {
            if (resp->metadata.levels[0] != '\0') {
                strncat(resp->metadata.levels, ",", sizeof(resp->metadata.levels) - strlen(resp->metadata.levels) - 1);
            }
            strncat(resp->metadata.levels, level_names_short[i], sizeof(resp->metadata.levels) - strlen(resp->metadata.levels) - 1);
        }
    }
    resp->metadata.build_ms = checkpoint_ms(&ts);

    resp->base.is_error = false;
    return MEM_OK;
}

/* Callback data for finding a session by session_id string */
typedef struct {
    const char* target_session_id;
    node_id_t found_node_id;
    const char* found_agent_id;
    bool found;
} session_find_data_t;

static bool find_session_callback(node_id_t session_node_id, const char* agent_id,
                                  const char* session_str, void* user_data) {
    session_find_data_t* data = (session_find_data_t*)user_data;
    if (strcmp(session_str, data->target_session_id) == 0) {
        data->found_node_id = session_node_id;
        data->found_agent_id = agent_id;
        data->found = true;
        return false;  /* Stop iteration */
    }
    return true;  /* Continue */
}

/* get_session: Retrieve session metadata */
static mem_error_t handle_get_session(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    if (!ctx->hierarchy) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "hierarchy not initialized";
        return MEM_OK;
    }

    if (!params || !yyjson_is_obj(params)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "params must be an object";
        return MEM_OK;
    }

    yyjson_val* session_id = yyjson_obj_get(params, "session_id");
    if (!session_id || !yyjson_is_str(session_id)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid session_id";
        return MEM_OK;
    }

    /* Find session by session_id string */
    session_find_data_t find_data = {
        .target_session_id = yyjson_get_str(session_id),
        .found = false
    };
    hierarchy_iter_sessions(ctx->hierarchy, find_session_callback, &find_data);

    if (!find_data.found) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_SERVER;
        resp->base.error_message = "session not found";
        return MEM_OK;
    }

    /* Build result */
    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    yyjson_mut_obj_add_uint(resp->result_doc, result, "node_id", find_data.found_node_id);
    yyjson_mut_obj_add_str(resp->result_doc, result, "session_id", find_data.target_session_id);
    yyjson_mut_obj_add_str(resp->result_doc, result, "agent_id",
                          find_data.found_agent_id ? find_data.found_agent_id : "");

    /* Get message count */
    node_id_t children[100];
    size_t message_count = hierarchy_get_children(ctx->hierarchy, find_data.found_node_id, children, 100);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "message_count", message_count);

    resp->base.is_error = false;
    return MEM_OK;
}

/* Callback data for listing sessions */
typedef struct {
    rpc_response_internal_t* resp;
    yyjson_mut_val* sessions_arr;
    hierarchy_t* hierarchy;
} session_list_data_t;

static bool list_session_callback(node_id_t session_node_id, const char* agent_id,
                                  const char* session_str, void* user_data) {
    session_list_data_t* data = (session_list_data_t*)user_data;

    yyjson_mut_val* session_obj = yyjson_mut_obj(data->resp->result_doc);
    yyjson_mut_obj_add_uint(data->resp->result_doc, session_obj, "node_id", session_node_id);
    yyjson_mut_obj_add_str(data->resp->result_doc, session_obj, "session_id", session_str);
    yyjson_mut_obj_add_str(data->resp->result_doc, session_obj, "agent_id", agent_id);

    /* Get message count */
    node_id_t children[100];
    size_t message_count = hierarchy_get_children(data->hierarchy, session_node_id, children, 100);
    yyjson_mut_obj_add_uint(data->resp->result_doc, session_obj, "message_count", message_count);

    yyjson_mut_arr_add_val(data->sessions_arr, session_obj);
    return true;  /* Continue iteration */
}

/* list_sessions: List sessions by keywords/time */
static mem_error_t handle_list_sessions(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    (void)params;

    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    yyjson_mut_val* sessions = yyjson_mut_arr(resp->result_doc);
    size_t count = 0;

    /* Iterate all sessions if hierarchy is available */
    if (ctx->hierarchy) {
        session_list_data_t list_data = {
            .resp = resp,
            .sessions_arr = sessions,
            .hierarchy = ctx->hierarchy
        };
        count = hierarchy_iter_sessions(ctx->hierarchy, list_session_callback, &list_data);
    }

    yyjson_mut_obj_add_val(resp->result_doc, result, "sessions", sessions);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "total", count);

    resp->base.is_error = false;
    return MEM_OK;
}

/* get_context: Get node with parent/child expansion */
static mem_error_t handle_get_context(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    if (!ctx->hierarchy) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "hierarchy not initialized";
        return MEM_OK;
    }

    if (!params || !yyjson_is_obj(params)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "params must be an object";
        return MEM_OK;
    }

    yyjson_val* node_id_val = yyjson_obj_get(params, "node_id");
    if (!node_id_val || !yyjson_is_int(node_id_val)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid node_id";
        return MEM_OK;
    }

    node_id_t node_id = (node_id_t)yyjson_get_int(node_id_val);

    /* Get node info */
    node_info_t info;
    mem_error_t err = hierarchy_get_node(ctx->hierarchy, node_id, &info);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_SERVER;
        resp->base.error_message = "node not found";
        return MEM_OK;
    }

    /* Build result */
    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    yyjson_mut_obj_add_uint(resp->result_doc, result, "node_id", info.id);
    yyjson_mut_obj_add_str(resp->result_doc, result, "level", level_name(info.level));
    yyjson_mut_obj_add_uint(resp->result_doc, result, "parent_id", info.parent_id);
    yyjson_mut_obj_add_str(resp->result_doc, result, "agent_id", info.agent_id);
    yyjson_mut_obj_add_str(resp->result_doc, result, "session_id", info.session_id);

    /* Include text content if available */
    size_t text_len;
    const char* text = hierarchy_get_text(ctx->hierarchy, node_id, &text_len);
    if (text) {
        yyjson_mut_obj_add_strncpy(resp->result_doc, result, "content", text, text_len);
    }

    /* Get optional expansion params */
    yyjson_val* include_parent = yyjson_obj_get(params, "include_parent");
    yyjson_val* include_children = yyjson_obj_get(params, "include_children");
    yyjson_val* include_siblings = yyjson_obj_get(params, "include_siblings");

    if (include_parent && yyjson_is_true(include_parent) && info.parent_id != NODE_ID_INVALID) {
        node_info_t parent_info;
        if (hierarchy_get_node(ctx->hierarchy, info.parent_id, &parent_info) == MEM_OK) {
            yyjson_mut_val* parent = yyjson_mut_obj(resp->result_doc);
            yyjson_mut_obj_add_uint(resp->result_doc, parent, "node_id", parent_info.id);
            yyjson_mut_obj_add_str(resp->result_doc, parent, "level", level_name(parent_info.level));
            yyjson_mut_obj_add_val(resp->result_doc, result, "parent", parent);
        }
    }

    if (include_children && yyjson_is_true(include_children)) {
        yyjson_mut_val* children = yyjson_mut_arr(resp->result_doc);
        node_id_t child_ids[100];
        size_t count = hierarchy_get_children(ctx->hierarchy, node_id, child_ids, 100);
        for (size_t i = 0; i < count; i++) {
            node_info_t child_info;
            if (hierarchy_get_node(ctx->hierarchy, child_ids[i], &child_info) == MEM_OK) {
                yyjson_mut_val* child = yyjson_mut_obj(resp->result_doc);
                yyjson_mut_obj_add_uint(resp->result_doc, child, "node_id", child_info.id);
                yyjson_mut_obj_add_str(resp->result_doc, child, "level", level_name(child_info.level));
                yyjson_mut_arr_add_val(children, child);
            }
        }
        yyjson_mut_obj_add_val(resp->result_doc, result, "children", children);
    }

    if (include_siblings && yyjson_is_true(include_siblings)) {
        yyjson_mut_val* siblings = yyjson_mut_arr(resp->result_doc);
        node_id_t sibling_ids[100];
        size_t count = hierarchy_get_siblings(ctx->hierarchy, node_id, sibling_ids, 100);
        for (size_t i = 0; i < count; i++) {
            node_info_t sibling_info;
            if (hierarchy_get_node(ctx->hierarchy, sibling_ids[i], &sibling_info) == MEM_OK) {
                yyjson_mut_val* sibling = yyjson_mut_obj(resp->result_doc);
                yyjson_mut_obj_add_uint(resp->result_doc, sibling, "node_id", sibling_info.id);
                yyjson_mut_obj_add_str(resp->result_doc, sibling, "level", level_name(sibling_info.level));
                yyjson_mut_arr_add_val(siblings, sibling);
            }
        }
        yyjson_mut_obj_add_val(resp->result_doc, result, "siblings", siblings);
    }

    resp->base.is_error = false;
    return MEM_OK;
}

/* Case-insensitive substring search */
static bool text_contains(const char* haystack, size_t haystack_len,
                          const char* needle, size_t needle_len) {
    if (needle_len == 0) return true;
    if (needle_len > haystack_len) return false;

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        bool match = true;
        for (size_t j = 0; j < needle_len && match; j++) {
            char h = haystack[i + j];
            char n = needle[j];
            /* Simple ASCII lowercase comparison */
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) match = false;
        }
        if (match) return true;
    }
    return false;
}

/* drill_down: Get children of a node (for agent navigation)
 *
 * Parameters:
 *   id: node ID to drill down from (required)
 *   filter: optional search term to filter children by content
 *   max_results: optional limit on returned children (default: 100)
 */
static mem_error_t handle_drill_down(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    if (!ctx->hierarchy) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "hierarchy not initialized";
        return MEM_OK;
    }

    if (!params || !yyjson_is_obj(params)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "params must be an object";
        return MEM_OK;
    }

    yyjson_val* id_val = yyjson_obj_get(params, "id");
    if (!id_val || !yyjson_is_uint(id_val)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid id";
        return MEM_OK;
    }

    node_id_t node_id = (node_id_t)yyjson_get_uint(id_val);

    /* Optional filter term */
    const char* filter_str = NULL;
    size_t filter_len = 0;
    yyjson_val* filter_val = yyjson_obj_get(params, "filter");
    if (filter_val && yyjson_is_str(filter_val)) {
        filter_str = yyjson_get_str(filter_val);
        filter_len = yyjson_get_len(filter_val);
    }

    /* Optional max results */
    size_t max_results = 100;
    yyjson_val* max_val = yyjson_obj_get(params, "max_results");
    if (max_val && yyjson_is_int(max_val)) {
        max_results = (size_t)yyjson_get_int(max_val);
        if (max_results > 100) max_results = 100;
    }
    resp->metadata.parse_ms = checkpoint_ms(&ts);

    /* Get node info */
    node_info_t info;
    mem_error_t err = hierarchy_get_node(ctx->hierarchy, node_id, &info);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_SERVER;
        resp->base.error_message = "node not found";
        return MEM_OK;
    }

    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    /* Max content length for drill_down (exploration, but not full retrieval) */
    const size_t MAX_CONTENT_LEN = 1000;

    /* Current node context */
    yyjson_mut_obj_add_uint(resp->result_doc, result, "node_id", node_id);
    yyjson_mut_obj_add_str(resp->result_doc, result, "level", level_name(info.level));
    if (filter_str) {
        yyjson_mut_obj_add_strncpy(resp->result_doc, result, "filter", filter_str, filter_len);
    }

    /* Get children with content */
    yyjson_mut_val* children = yyjson_mut_arr(resp->result_doc);
    node_id_t child_ids[100];
    size_t total_children = hierarchy_get_children(ctx->hierarchy, node_id, child_ids, 100);
    size_t matched_count = 0;

    for (size_t i = 0; i < total_children && matched_count < max_results; i++) {
        node_info_t child_info;
        if (hierarchy_get_node(ctx->hierarchy, child_ids[i], &child_info) != MEM_OK) {
            continue;
        }

        /* Get content */
        size_t text_len;
        const char* text = hierarchy_get_text(ctx->hierarchy, child_ids[i], &text_len);

        /* Apply filter if specified */
        if (filter_str && filter_len > 0) {
            if (!text || !text_contains(text, text_len, filter_str, filter_len)) {
                continue;  /* Skip non-matching children */
            }
        }

        yyjson_mut_val* child = yyjson_mut_obj(resp->result_doc);
        yyjson_mut_obj_add_uint(resp->result_doc, child, "node_id", child_ids[i]);
        yyjson_mut_obj_add_str(resp->result_doc, child, "level", level_name(child_info.level));

        if (text) {
            size_t content_len = text_len > MAX_CONTENT_LEN ? MAX_CONTENT_LEN : text_len;
            yyjson_mut_obj_add_strncpy(resp->result_doc, child, "content", text, content_len);
        }

        /* Include child count for further drilling */
        node_id_t grandchild_ids[1];
        size_t grandchild_count = hierarchy_get_children(ctx->hierarchy, child_ids[i], grandchild_ids, 1);
        yyjson_mut_obj_add_uint(resp->result_doc, child, "children_count", grandchild_count > 0 ? grandchild_count : 0);

        yyjson_mut_arr_add_val(children, child);
        matched_count++;
    }

    yyjson_mut_obj_add_val(resp->result_doc, result, "children", children);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "children_count", matched_count);
    yyjson_mut_obj_add_uint(resp->result_doc, result, "total_children", total_children);

    /* Populate logging metadata */
    resp->metadata.match_count = matched_count;
    if (matched_count > 0) {
        /* Get level of first child for logging */
        node_info_t first_child_info;
        node_id_t first_matched = 0;
        for (size_t i = 0; i < total_children; i++) {
            size_t tl;
            const char* t = hierarchy_get_text(ctx->hierarchy, child_ids[i], &tl);
            if (!filter_str || !filter_len || (t && text_contains(t, tl, filter_str, filter_len))) {
                first_matched = child_ids[i];
                break;
            }
        }
        if (first_matched && hierarchy_get_node(ctx->hierarchy, first_matched, &first_child_info) == MEM_OK) {
            snprintf(resp->metadata.levels, sizeof(resp->metadata.levels), "%s",
                    level_name(first_child_info.level));
        }
    }
    resp->metadata.build_ms = checkpoint_ms(&ts);

    resp->base.is_error = false;
    return MEM_OK;
}

/* zoom_out: Get parent chain of a node (for agent context) */
static mem_error_t handle_zoom_out(rpc_context_t* ctx, yyjson_val* params, rpc_response_internal_t* resp) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!ctx->hierarchy) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "hierarchy not initialized";
        return MEM_OK;
    }

    if (!params || !yyjson_is_obj(params)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "params must be an object";
        return MEM_OK;
    }

    yyjson_val* id_val = yyjson_obj_get(params, "id");
    if (!id_val || !yyjson_is_uint(id_val)) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INVALID_PARAMS;
        resp->base.error_message = "missing or invalid id";
        return MEM_OK;
    }

    node_id_t node_id = (node_id_t)yyjson_get_uint(id_val);
    resp->metadata.parse_ms = checkpoint_ms(&ts);

    /* Get node info */
    node_info_t info;
    mem_error_t err = hierarchy_get_node(ctx->hierarchy, node_id, &info);
    if (err != MEM_OK) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_SERVER;
        resp->base.error_message = "node not found";
        return MEM_OK;
    }

    yyjson_mut_val* result = create_result(resp);
    if (!result) {
        resp->base.is_error = true;
        resp->base.error_code = RPC_ERROR_INTERNAL;
        resp->base.error_message = "failed to create result";
        return MEM_OK;
    }

    /* Max content length for zoom_out (navigation context, not full retrieval) */
    const size_t MAX_CONTENT_LEN = 500;

    /* Current node */
    yyjson_mut_obj_add_uint(resp->result_doc, result, "node_id", node_id);
    yyjson_mut_obj_add_str(resp->result_doc, result, "level", level_name(info.level));

    size_t text_len;
    const char* text = hierarchy_get_text(ctx->hierarchy, node_id, &text_len);
    if (text) {
        size_t content_len = text_len > MAX_CONTENT_LEN ? MAX_CONTENT_LEN : text_len;
        yyjson_mut_obj_add_strncpy(resp->result_doc, result, "content", text, content_len);
    }

    /* Build ancestor chain (from immediate parent to root) */
    yyjson_mut_val* ancestors = yyjson_mut_arr(resp->result_doc);
    node_id_t current = info.parent_id;

    while (current != NODE_ID_INVALID) {
        node_info_t ancestor_info;
        if (hierarchy_get_node(ctx->hierarchy, current, &ancestor_info) != MEM_OK) {
            break;
        }

        yyjson_mut_val* ancestor = yyjson_mut_obj(resp->result_doc);
        yyjson_mut_obj_add_uint(resp->result_doc, ancestor, "node_id", current);
        yyjson_mut_obj_add_str(resp->result_doc, ancestor, "level", level_name(ancestor_info.level));

        /* Include truncated content for context */
        const char* ancestor_text = hierarchy_get_text(ctx->hierarchy, current, &text_len);
        if (ancestor_text) {
            size_t content_len = text_len > MAX_CONTENT_LEN ? MAX_CONTENT_LEN : text_len;
            yyjson_mut_obj_add_strncpy(resp->result_doc, ancestor, "content", ancestor_text, content_len);
        }

        yyjson_mut_arr_add_val(ancestors, ancestor);
        current = ancestor_info.parent_id;
    }

    yyjson_mut_obj_add_val(resp->result_doc, result, "ancestors", ancestors);

    /* Also include siblings for lateral navigation */
    yyjson_mut_val* siblings = yyjson_mut_arr(resp->result_doc);
    node_id_t sibling_ids[20];
    size_t sibling_count = hierarchy_get_siblings(ctx->hierarchy, node_id, sibling_ids, 20);

    for (size_t i = 0; i < sibling_count; i++) {
        if (sibling_ids[i] == node_id) continue;  /* Skip self */

        node_info_t sibling_info;
        if (hierarchy_get_node(ctx->hierarchy, sibling_ids[i], &sibling_info) == MEM_OK) {
            yyjson_mut_val* sibling = yyjson_mut_obj(resp->result_doc);
            yyjson_mut_obj_add_uint(resp->result_doc, sibling, "node_id", sibling_ids[i]);
            yyjson_mut_obj_add_str(resp->result_doc, sibling, "level", level_name(sibling_info.level));

            const char* sibling_text = hierarchy_get_text(ctx->hierarchy, sibling_ids[i], &text_len);
            if (sibling_text) {
                /* Truncate sibling content for overview */
                size_t max_len = text_len > 100 ? 100 : text_len;
                yyjson_mut_obj_add_strncpy(resp->result_doc, sibling, "preview", sibling_text, max_len);
            }

            yyjson_mut_arr_add_val(siblings, sibling);
        }
    }

    yyjson_mut_obj_add_val(resp->result_doc, result, "siblings", siblings);

    /* Populate logging metadata */
    size_t ancestor_count = yyjson_mut_arr_size(ancestors);
    size_t actual_sibling_count = yyjson_mut_arr_size(siblings);
    resp->metadata.match_count = ancestor_count + actual_sibling_count;
    snprintf(resp->metadata.levels, sizeof(resp->metadata.levels), "%s (ancestors=%zu siblings=%zu)",
             level_name(info.level), ancestor_count, actual_sibling_count);
    resp->metadata.build_ms = checkpoint_ms(&ts);

    resp->base.is_error = false;
    return MEM_OK;
}
