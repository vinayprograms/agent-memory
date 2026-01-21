/*
 * Memory Service - Core Type Definitions
 *
 * Fundamental types used throughout the memory service.
 */

#ifndef MEMORY_SERVICE_TYPES_H
#define MEMORY_SERVICE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* Version info */
#define MEMORY_SERVICE_VERSION_MAJOR 0
#define MEMORY_SERVICE_VERSION_MINOR 1
#define MEMORY_SERVICE_VERSION_PATCH 0

/* Configuration constants */
#define EMBEDDING_DIM 384           /* all-MiniLM-L6-v2 dimension */
#define MAX_AGENT_ID_LEN 64
#define MAX_SESSION_ID_LEN 64
#define MAX_TRACE_ID_LEN 64
#define MAX_TEXT_LEN 65536
#define MAX_KEYWORDS 32
#define MAX_IDENTIFIERS 128
#define MAX_FILES_TOUCHED 64
#define BATCH_SIZE 32               /* ONNX batch size */

/* Hierarchy levels */
typedef enum {
    LEVEL_STATEMENT = 0,    /* Lowest: code statement, sentence */
    LEVEL_BLOCK = 1,        /* Code block, paragraph, tool output */
    LEVEL_MESSAGE = 2,      /* One turn: user/assistant/tool */
    LEVEL_SESSION = 3,      /* Entire conversation */
    LEVEL_AGENT = 4,        /* Agent instance (Claude Code instance) */
    LEVEL_COUNT = 5
} hierarchy_level_t;

/* Node identifier - 32-bit for memory efficiency */
typedef uint32_t node_id_t;
#define NODE_ID_INVALID UINT32_MAX

/* Embedding vector */
typedef struct {
    float values[EMBEDDING_DIM];
} embedding_t;

/* Timestamp in nanoseconds since epoch */
typedef uint64_t timestamp_ns_t;

/* Sequence number for ordering */
typedef uint64_t sequence_t;

/* Node structure - represents any item in hierarchy */
typedef struct {
    node_id_t       id;
    hierarchy_level_t level;
    node_id_t       parent_id;
    node_id_t       first_child_id;
    node_id_t       next_sibling_id;
    uint32_t        embedding_idx;      /* Index into level's embedding array */
    timestamp_ns_t  created_at;
    sequence_t      sequence_num;
    char            agent_id[MAX_AGENT_ID_LEN];
    char            session_id[MAX_SESSION_ID_LEN];
} node_t;

/* Session metadata */
typedef struct {
    char            session_id[MAX_SESSION_ID_LEN];
    char            agent_id[MAX_AGENT_ID_LEN];
    char*           keywords[MAX_KEYWORDS];
    size_t          keyword_count;
    char*           identifiers[MAX_IDENTIFIERS];
    size_t          identifier_count;
    char*           files_touched[MAX_FILES_TOUCHED];
    size_t          files_count;
    char*           title;              /* LLM-generated, may be NULL */
    timestamp_ns_t  created_at;
    timestamp_ns_t  last_active_at;
    sequence_t      sequence_num;
    node_id_t       root_node_id;       /* Session's root in hierarchy */
} session_meta_t;

/* Message role */
typedef enum {
    ROLE_USER = 0,
    ROLE_ASSISTANT = 1,
    ROLE_TOOL = 2,
    ROLE_SYSTEM = 3
} message_role_t;

/* Query parameters */
typedef struct {
    const char*     query_text;
    const char*     agent_id;           /* NULL for all agents */
    const char*     session_id;         /* NULL for all sessions */
    timestamp_ns_t  after_time;         /* 0 for no filter */
    timestamp_ns_t  before_time;        /* 0 for no filter */
    uint32_t        max_results;        /* Default: 10 */
    uint32_t        max_tokens;         /* Token budget, 0 for unlimited */
    bool            include_parent;
    bool            include_siblings;
    bool            include_children;
    uint32_t        max_depth;          /* Expansion depth limit */
} query_params_t;

/* Search result item */
typedef struct {
    node_id_t       node_id;
    hierarchy_level_t level;
    float           relevance_score;    /* 0.0 - 1.0 */
    float           recency_score;      /* 0.0 - 1.0 */
    float           combined_score;     /* Weighted combination */
    const char*     text;
    const char*     agent_id;
    const char*     session_id;
    timestamp_ns_t  created_at;
} search_result_t;

/* Search results container */
typedef struct {
    search_result_t* results;
    size_t          count;
    size_t          total_matches;      /* Before truncation */
    bool            truncated;
    uint32_t        tokens_used;
} search_results_t;

/* Store request */
typedef struct {
    const char*     session_id;
    const char*     agent_id;
    const char*     trace_id;           /* Optional */
    message_role_t  role;
    const char*     content;
    size_t          content_len;
} store_request_t;

/* Store response */
typedef struct {
    node_id_t       message_id;
    sequence_t      sequence_num;
    bool            new_session;
} store_response_t;

/* Context expansion result */
typedef struct {
    node_t*         nodes;
    size_t          count;
    char**          texts;              /* Parallel array of text content */
} context_result_t;

/* Inline helper functions */
static inline timestamp_ns_t timestamp_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (timestamp_ns_t)ts.tv_sec * 1000000000ULL + (timestamp_ns_t)ts.tv_nsec;
}

static inline const char* level_name(hierarchy_level_t level) {
    static const char* names[] = {"statement", "block", "message", "session", "agent"};
    return (level < LEVEL_COUNT) ? names[level] : "unknown";
}

static inline const char* role_name(message_role_t role) {
    static const char* names[] = {"user", "assistant", "tool", "system"};
    return (role <= ROLE_SYSTEM) ? names[role] : "unknown";
}

#endif /* MEMORY_SERVICE_TYPES_H */
