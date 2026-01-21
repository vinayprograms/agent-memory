/*
 * Memory Service - Configuration
 *
 * Configuration structures and defaults.
 */

#ifndef MEMORY_SERVICE_CONFIG_H
#define MEMORY_SERVICE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* Default paths (relative to $GRID_HOME/data/memory/) */
#define DEFAULT_DATA_DIR        "data"
#define DEFAULT_EMBEDDINGS_DIR  "embeddings"
#define DEFAULT_INDEX_DIR       "index"
#define DEFAULT_RELATIONS_DIR   "relations"
#define DEFAULT_METADATA_DIR    "metadata"
#define DEFAULT_WAL_DIR         "wal"
#define DEFAULT_MODEL_PATH      "models/all-MiniLM-L6-v2.onnx"

/* Default server settings */
#define DEFAULT_HOST            "127.0.0.1"
#define DEFAULT_PORT            8080
#define DEFAULT_MAX_CONNECTIONS 64

/* Default storage settings */
#define DEFAULT_LMDB_MAP_SIZE   (1ULL << 30)    /* 1 GB */
#define DEFAULT_WAL_MAX_SIZE    (64 << 20)      /* 64 MB before checkpoint */
#define DEFAULT_ARENA_SIZE      (256 << 20)     /* 256 MB per arena */

/* Default HNSW parameters */
#define DEFAULT_HNSW_M          16              /* Max connections per node */
#define DEFAULT_HNSW_EF_CONSTRUCT 200           /* Construction search width */
#define DEFAULT_HNSW_EF_SEARCH  50              /* Query search width */

/* Default search parameters */
#define DEFAULT_SEARCH_K_STATEMENT  30
#define DEFAULT_SEARCH_K_BLOCK      20
#define DEFAULT_SEARCH_K_MESSAGE    10
#define DEFAULT_SEARCH_K_SESSION    5
#define DEFAULT_MAX_RESULTS         10

/* Ranking weights */
#define DEFAULT_WEIGHT_RELEVANCE    0.6f
#define DEFAULT_WEIGHT_RECENCY      0.3f
#define DEFAULT_WEIGHT_LEVEL        0.1f

/* Latency targets (microseconds) */
#define TARGET_QUERY_LATENCY_US     10000       /* 10ms */
#define TARGET_INGEST_LATENCY_US    10000       /* 10ms */

/* Event bus settings */
#define DEFAULT_EVENTS_DIR          "events"
#define DEFAULT_EVENT_TOPIC         "memory"

/* Configuration structure */
typedef struct {
    /* Paths */
    char*       data_dir;
    char*       model_path;

    /* Server */
    char*       host;
    uint16_t    port;
    uint32_t    max_connections;

    /* Storage */
    size_t      lmdb_map_size;
    size_t      wal_max_size;
    size_t      arena_size;

    /* HNSW */
    uint32_t    hnsw_m;
    uint32_t    hnsw_ef_construct;
    uint32_t    hnsw_ef_search;

    /* Search */
    uint32_t    search_k[4];            /* Per level */
    uint32_t    max_results;

    /* Ranking */
    float       weight_relevance;
    float       weight_recency;
    float       weight_level;

    /* Threading */
    uint32_t    num_workers;            /* 0 = auto-detect */

    /* Events */
    char*       events_dir;
    bool        emit_events;

    /* Debug */
    bool        debug;
    bool        trace_latency;
} config_t;

/* Load from file */
int config_load(config_t* cfg, const char* path);

/* Free allocated strings */
void config_cleanup(config_t* cfg);

/* Validate configuration */
int config_validate(const config_t* cfg);

/* Print configuration */
void config_print(const config_t* cfg);

/* Implementation of inline defaults */
static inline void config_init_defaults(config_t* cfg) {
    cfg->data_dir = NULL;           /* Must be set */
    cfg->model_path = NULL;         /* Must be set */
    cfg->host = NULL;
    cfg->port = DEFAULT_PORT;
    cfg->max_connections = DEFAULT_MAX_CONNECTIONS;
    cfg->lmdb_map_size = DEFAULT_LMDB_MAP_SIZE;
    cfg->wal_max_size = DEFAULT_WAL_MAX_SIZE;
    cfg->arena_size = DEFAULT_ARENA_SIZE;
    cfg->hnsw_m = DEFAULT_HNSW_M;
    cfg->hnsw_ef_construct = DEFAULT_HNSW_EF_CONSTRUCT;
    cfg->hnsw_ef_search = DEFAULT_HNSW_EF_SEARCH;
    cfg->search_k[0] = DEFAULT_SEARCH_K_STATEMENT;
    cfg->search_k[1] = DEFAULT_SEARCH_K_BLOCK;
    cfg->search_k[2] = DEFAULT_SEARCH_K_MESSAGE;
    cfg->search_k[3] = DEFAULT_SEARCH_K_SESSION;
    cfg->max_results = DEFAULT_MAX_RESULTS;
    cfg->weight_relevance = DEFAULT_WEIGHT_RELEVANCE;
    cfg->weight_recency = DEFAULT_WEIGHT_RECENCY;
    cfg->weight_level = DEFAULT_WEIGHT_LEVEL;
    cfg->num_workers = 0;
    cfg->events_dir = NULL;
    cfg->emit_events = true;
    cfg->debug = false;
    cfg->trace_latency = false;
}

#endif /* MEMORY_SERVICE_CONFIG_H */
