/*
 * Memory Service - Error Handling
 *
 * Error codes and error handling macros.
 */

#ifndef MEMORY_SERVICE_ERROR_H
#define MEMORY_SERVICE_ERROR_H

#include <stdio.h>
#include <errno.h>

/* Error codes */
typedef enum {
    MEM_OK = 0,

    /* General errors (1-99) */
    MEM_ERR_NOMEM = 1,          /* Out of memory */
    MEM_ERR_INVALID_ARG = 2,    /* Invalid argument */
    MEM_ERR_NOT_FOUND = 3,      /* Item not found */
    MEM_ERR_EXISTS = 4,         /* Item already exists */
    MEM_ERR_FULL = 5,           /* Container is full */
    MEM_ERR_EMPTY = 6,          /* Container is empty */
    MEM_ERR_OVERFLOW = 7,       /* Buffer overflow */
    MEM_ERR_TIMEOUT = 8,        /* Operation timed out */

    /* I/O errors (100-199) */
    MEM_ERR_IO = 100,           /* General I/O error */
    MEM_ERR_OPEN = 101,         /* Failed to open file */
    MEM_ERR_READ = 102,         /* Failed to read */
    MEM_ERR_WRITE = 103,        /* Failed to write */
    MEM_ERR_SEEK = 104,         /* Failed to seek */
    MEM_ERR_MMAP = 105,         /* mmap failed */
    MEM_ERR_MUNMAP = 106,       /* munmap failed */
    MEM_ERR_SYNC = 107,         /* fsync/msync failed */
    MEM_ERR_TRUNCATE = 108,     /* ftruncate failed */

    /* Storage errors (200-299) */
    MEM_ERR_WAL = 200,          /* WAL operation failed */
    MEM_ERR_WAL_CORRUPT = 201,  /* WAL corruption detected */
    MEM_ERR_LMDB = 210,         /* LMDB error */
    MEM_ERR_LMDB_FULL = 211,    /* LMDB map full */
    MEM_ERR_INDEX = 220,        /* Index operation failed */
    MEM_ERR_INDEX_CORRUPT = 221,/* Index corruption detected */

    /* Hierarchy errors (300-399) */
    MEM_ERR_HIERARCHY = 300,    /* Hierarchy operation failed */
    MEM_ERR_INVALID_LEVEL = 301,/* Invalid hierarchy level */
    MEM_ERR_ORPHAN = 302,       /* Orphaned node detected */
    MEM_ERR_CYCLE = 303,        /* Cycle in hierarchy detected */

    /* Search errors (400-499) */
    MEM_ERR_SEARCH = 400,       /* Search operation failed */
    MEM_ERR_EMBEDDING = 401,    /* Embedding generation failed */
    MEM_ERR_HNSW = 402,         /* HNSW operation failed */

    /* API errors (500-599) */
    MEM_ERR_API = 500,          /* API error */
    MEM_ERR_PARSE = 501,        /* JSON parse error */
    MEM_ERR_RPC = 502,          /* JSON-RPC error */
    MEM_ERR_METHOD = 503,       /* Unknown method */
    MEM_ERR_PARAMS = 504,       /* Invalid parameters */

    /* ONNX errors (600-699) */
    MEM_ERR_ONNX = 600,         /* ONNX Runtime error */
    MEM_ERR_ONNX_LOAD = 601,    /* Model load failed */
    MEM_ERR_ONNX_INFER = 602,   /* Inference failed */
    MEM_ERR_TOKENIZE = 610,     /* Tokenization error */

    /* Threading errors (700-799) */
    MEM_ERR_THREAD = 700,       /* Thread operation failed */
    MEM_ERR_MUTEX = 701,        /* Mutex operation failed */
    MEM_ERR_COND = 702,         /* Condition variable failed */

} mem_error_t;

/* Error context for detailed error messages */
typedef struct {
    mem_error_t     code;
    int             sys_errno;      /* System errno if applicable */
    const char*     file;
    int             line;
    const char*     func;
    char            message[256];
} error_context_t;

/* Thread-local error context */
extern _Thread_local error_context_t g_last_error;

/* Get error string */
const char* mem_error_str(mem_error_t err);

/* Set error with context */
#define MEM_SET_ERROR(err_code, fmt, ...) do { \
    g_last_error.code = (err_code); \
    g_last_error.sys_errno = errno; \
    g_last_error.file = __FILE__; \
    g_last_error.line = __LINE__; \
    g_last_error.func = __func__; \
    snprintf(g_last_error.message, sizeof(g_last_error.message), fmt, ##__VA_ARGS__); \
} while(0)

/* Return error with logging */
#define MEM_RETURN_ERROR(err_code, fmt, ...) do { \
    MEM_SET_ERROR(err_code, fmt, ##__VA_ARGS__); \
    return (err_code); \
} while(0)

/* Check and propagate error */
#define MEM_CHECK(expr) do { \
    mem_error_t _err = (expr); \
    if (_err != MEM_OK) return _err; \
} while(0)

/* Check with custom error */
#define MEM_CHECK_ERR(expr, code, fmt, ...) do { \
    if (!(expr)) { \
        MEM_RETURN_ERROR(code, fmt, ##__VA_ARGS__); \
    } \
} while(0)

/* Check allocation */
#define MEM_CHECK_ALLOC(ptr) do { \
    if ((ptr) == NULL) { \
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "allocation failed"); \
    } \
} while(0)

/* Check system call with errno */
#define MEM_CHECK_SYS(expr, code, fmt, ...) do { \
    if ((expr) < 0) { \
        MEM_RETURN_ERROR(code, fmt ": %s", ##__VA_ARGS__, strerror(errno)); \
    } \
} while(0)

/* Assert for debug builds */
#ifdef DEBUG
#define MEM_ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "ASSERT FAILED: %s at %s:%d in %s\n", \
                #expr, __FILE__, __LINE__, __func__); \
        __builtin_trap(); \
    } \
} while(0)
#else
#define MEM_ASSERT(expr) ((void)0)
#endif

/* Get last error */
static inline const error_context_t* mem_get_last_error(void) {
    return &g_last_error;
}

/* Clear last error */
static inline void mem_clear_error(void) {
    g_last_error.code = MEM_OK;
    g_last_error.message[0] = '\0';
}

#endif /* MEMORY_SERVICE_ERROR_H */
