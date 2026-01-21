/*
 * Memory Service - Main Entry Point
 *
 * Fully wired service with:
 * - Hierarchy model (includes storage)
 * - Embedding engine (ONNX or pseudo)
 * - Search engine (HNSW + inverted index)
 * - HTTP API server (JSON-RPC 2.0)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>

#include "config.h"
#include "util/log.h"
#include "util/time.h"
#include "types.h"

#include "core/hierarchy.h"
#include "embedding/embedding.h"
#include "search/search.h"
#include "api/api.h"

/* Global shutdown flag */
static volatile sig_atomic_t g_shutdown = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_shutdown = 1;
}

/* Print usage */
static void print_usage(const char* prog) {
    printf("Memory Service v%d.%d.%d\n\n",
           MEMORY_SERVICE_VERSION_MAJOR,
           MEMORY_SERVICE_VERSION_MINOR,
           MEMORY_SERVICE_VERSION_PATCH);
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  -d, --data-dir DIR       Data directory (default: ./data)\n");
    printf("  -p, --port PORT          HTTP port (default: 8080)\n");
    printf("  -c, --capacity NUM       Max nodes capacity (default: 10000)\n");
    printf("  -m, --model PATH         ONNX model path (optional)\n");
    printf("  -l, --log-format FORMAT  Log format: text or json (default: text)\n");
    printf("  -v, --verbose            Verbose logging\n");
    printf("  -h, --help               Show this help\n");
    printf("\nEndpoints:\n");
    printf("  POST /rpc                JSON-RPC 2.0 API\n");
    printf("  GET  /health             Health check\n");
    printf("  GET  /metrics            Prometheus metrics\n");
    printf("\nJSON-RPC Methods:\n");
    printf("  memory.store             Store a message\n");
    printf("  memory.query             Search memories\n");
    printf("  memory.get_context       Get context for session\n");
    printf("  memory.list_sessions     List all sessions\n");
}

/* Ensure directory exists */
static int ensure_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        return -1;
    }

    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    /* Default configuration */
    const char* data_dir = "./data";
    uint16_t port = 8080;
    size_t capacity = 10000;
    const char* model_path = NULL;
    int verbose = 0;
    log_format_t log_format = LOG_FORMAT_TEXT;

    /* Parse command line options */
    static struct option long_options[] = {
        {"data-dir",   required_argument, 0, 'd'},
        {"port",       required_argument, 0, 'p'},
        {"capacity",   required_argument, 0, 'c'},
        {"model",      required_argument, 0, 'm'},
        {"log-format", required_argument, 0, 'l'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:p:c:m:l:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                data_dir = optarg;
                break;
            case 'p':
                port = (uint16_t)atoi(optarg);
                break;
            case 'c':
                capacity = (size_t)atol(optarg);
                break;
            case 'm':
                model_path = optarg;
                break;
            case 'l':
                if (strcmp(optarg, "json") == 0) {
                    log_format = LOG_FORMAT_JSON;
                } else if (strcmp(optarg, "text") == 0) {
                    log_format = LOG_FORMAT_TEXT;
                } else {
                    fprintf(stderr, "Invalid log format: %s (use 'text' or 'json')\n", optarg);
                    return 1;
                }
                break;
            case 'v':
                verbose = 1;
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

    /* Initialize logging */
    log_config_t log_cfg = {
        .level = verbose ? LOG_DEBUG : LOG_INFO,
        .format = log_format,
        .output = stderr,
        .include_timestamp = true,
        .include_location = verbose,
        .colorize = (log_format == LOG_FORMAT_TEXT)  /* No colors in JSON mode */
    };
    log_init(&log_cfg);

    LOG_INFO("Memory Service v%d.%d.%d starting...",
             MEMORY_SERVICE_VERSION_MAJOR,
             MEMORY_SERVICE_VERSION_MINOR,
             MEMORY_SERVICE_VERSION_PATCH);

    /* Ensure data directory exists */
    if (ensure_dir(data_dir) != 0) {
        LOG_ERROR("Failed to create data directory: %s", data_dir);
        return 1;
    }
    LOG_INFO("Data directory: %s", data_dir);

    /* Initialize components */
    mem_error_t err = MEM_OK;
    hierarchy_t* hierarchy = NULL;
    embedding_engine_t* embedding_engine = NULL;
    search_engine_t* search = NULL;
    api_server_t* api = NULL;

    /* 1. Initialize hierarchy (includes embeddings and relations storage) */
    /* Try to open existing data first, create new if not found */
    err = hierarchy_open(&hierarchy, data_dir);
    if (err != MEM_OK) {
        LOG_INFO("No existing data found, creating new hierarchy");
        err = hierarchy_create(&hierarchy, data_dir, capacity);
        if (err != MEM_OK) {
            LOG_ERROR("Failed to create hierarchy: %d", err);
            goto cleanup;
        }
    }

    /* 2. Initialize embedding engine */
    embedding_config_t emb_cfg = EMBEDDING_CONFIG_DEFAULT;
    emb_cfg.model_path = model_path;
    err = embedding_engine_create(&embedding_engine, &emb_cfg);
    if (err != MEM_OK) {
        LOG_ERROR("Failed to create embedding engine: %d", err);
        goto cleanup;
    }

    /* 3. Initialize search engine */
    search_config_t search_cfg = SEARCH_CONFIG_DEFAULT;
    err = search_engine_create(&search, hierarchy, &search_cfg);
    if (err != MEM_OK) {
        LOG_ERROR("Failed to create search engine: %d", err);
        goto cleanup;
    }

    /* 4. Start API server */
    api_config_t api_cfg = API_CONFIG_DEFAULT;
    api_cfg.port = port;
    err = api_server_create(&api, hierarchy, search, embedding_engine, &api_cfg);
    if (err != MEM_OK) {
        LOG_ERROR("Failed to create API server: %d", err);
        goto cleanup;
    }

    if (api_server_running(api)) {
        LOG_INFO("API server listening on http://localhost:%d", port);
        LOG_INFO("  POST /rpc     - JSON-RPC 2.0 endpoint");
        LOG_INFO("  GET  /health  - Health check");
        LOG_INFO("  GET  /metrics - Prometheus metrics");
    } else {
        LOG_WARN("HTTP server not available - install libmicrohttpd for full functionality");
        LOG_INFO("Service running in embedded mode (API available via api_process_rpc)");
    }

    LOG_INFO("Memory Service ready (capacity: %zu nodes)", capacity);

    /* Main loop */
    while (!g_shutdown) {
        sleep_ms(100);
    }

    LOG_INFO("Memory Service shutting down...");

cleanup:
    /* Cleanup in reverse order */
    if (api) api_server_destroy(api);
    if (search) search_engine_destroy(search);
    if (embedding_engine) embedding_engine_destroy(embedding_engine);
    if (hierarchy) hierarchy_close(hierarchy);

    LOG_INFO("Memory Service stopped");
    return (err == MEM_OK) ? 0 : 1;
}
