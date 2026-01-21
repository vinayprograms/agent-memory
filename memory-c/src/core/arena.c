/*
 * Memory Service - Arena Allocator Implementation
 */

#include "arena.h"
#include "../util/log.h"
#include "../platform/platform.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* Thread-local error context */
_Thread_local error_context_t g_last_error = {0};

/* Default alignment */
#define DEFAULT_ALIGNMENT 16

/* Error string table */
const char* mem_error_str(mem_error_t err) {
    static const char* error_strings[] = {
        [MEM_OK] = "success",
        [MEM_ERR_NOMEM] = "out of memory",
        [MEM_ERR_INVALID_ARG] = "invalid argument",
        [MEM_ERR_NOT_FOUND] = "not found",
        [MEM_ERR_EXISTS] = "already exists",
        [MEM_ERR_FULL] = "container full",
        [MEM_ERR_EMPTY] = "container empty",
        [MEM_ERR_OVERFLOW] = "buffer overflow",
        [MEM_ERR_TIMEOUT] = "timeout",
        [MEM_ERR_IO] = "I/O error",
        [MEM_ERR_OPEN] = "open failed",
        [MEM_ERR_READ] = "read failed",
        [MEM_ERR_WRITE] = "write failed",
        [MEM_ERR_SEEK] = "seek failed",
        [MEM_ERR_MMAP] = "mmap failed",
        [MEM_ERR_MUNMAP] = "munmap failed",
        [MEM_ERR_SYNC] = "sync failed",
        [MEM_ERR_TRUNCATE] = "truncate failed",
        [MEM_ERR_WAL] = "WAL error",
        [MEM_ERR_WAL_CORRUPT] = "WAL corrupt",
        [MEM_ERR_LMDB] = "LMDB error",
        [MEM_ERR_LMDB_FULL] = "LMDB map full",
        [MEM_ERR_INDEX] = "index error",
        [MEM_ERR_INDEX_CORRUPT] = "index corrupt",
        [MEM_ERR_HIERARCHY] = "hierarchy error",
        [MEM_ERR_INVALID_LEVEL] = "invalid level",
        [MEM_ERR_ORPHAN] = "orphan node",
        [MEM_ERR_CYCLE] = "cycle detected",
        [MEM_ERR_SEARCH] = "search error",
        [MEM_ERR_EMBEDDING] = "embedding error",
        [MEM_ERR_HNSW] = "HNSW error",
        [MEM_ERR_API] = "API error",
        [MEM_ERR_PARSE] = "parse error",
        [MEM_ERR_RPC] = "RPC error",
        [MEM_ERR_METHOD] = "unknown method",
        [MEM_ERR_PARAMS] = "invalid params",
        [MEM_ERR_ONNX] = "ONNX error",
        [MEM_ERR_ONNX_LOAD] = "model load failed",
        [MEM_ERR_ONNX_INFER] = "inference failed",
        [MEM_ERR_TOKENIZE] = "tokenize error",
        [MEM_ERR_THREAD] = "thread error",
        [MEM_ERR_MUTEX] = "mutex error",
        [MEM_ERR_COND] = "condition error",
    };

    if (err >= 0 && err < (mem_error_t)(sizeof(error_strings) / sizeof(error_strings[0]))) {
        const char* str = error_strings[err];
        if (str) return str;
    }
    return "unknown error";
}

mem_error_t arena_create(arena_t** arena, size_t size) {
    MEM_CHECK_ERR(arena != NULL, MEM_ERR_INVALID_ARG, "arena pointer is NULL");
    MEM_CHECK_ERR(size > 0, MEM_ERR_INVALID_ARG, "size must be > 0");

    arena_t* a = malloc(sizeof(arena_t));
    MEM_CHECK_ALLOC(a);

    a->base = malloc(size);
    if (!a->base) {
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate %zu bytes", size);
    }

    a->size = size;
    a->used = 0;
    a->alignment = DEFAULT_ALIGNMENT;
    a->flags = 0;
    a->fd = -1;
    a->path = NULL;

    *arena = a;
    return MEM_OK;
}

mem_error_t arena_create_mmap(arena_t** arena, const char* path, size_t size, uint32_t flags) {
    MEM_CHECK_ERR(arena != NULL, MEM_ERR_INVALID_ARG, "arena pointer is NULL");
    MEM_CHECK_ERR(path != NULL, MEM_ERR_INVALID_ARG, "path is NULL");
    MEM_CHECK_ERR(size > 0, MEM_ERR_INVALID_ARG, "size must be > 0");

    arena_t* a = malloc(sizeof(arena_t));
    MEM_CHECK_ALLOC(a);

    /* Open or create file */
    int open_flags = O_RDWR | O_CREAT;
    int fd = open(path, open_flags, 0644);
    if (fd < 0) {
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_OPEN, "failed to open %s", path);
    }

    /* Extend file to requested size */
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_TRUNCATE, "failed to truncate %s to %zu", path, size);
    }

    /* Map file */
    int prot = PROT_READ | PROT_WRITE;
    int mflags = MAP_SHARED;
    if (flags & ARENA_FLAG_SHARED) {
        mflags = MAP_SHARED;
    }

    void* base = mmap(NULL, size, prot, mflags, fd, 0);
    if (base == MAP_FAILED) {
        close(fd);
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_MMAP, "mmap failed for %s", path);
    }

    a->base = base;
    a->size = size;
    a->used = 0;
    a->alignment = DEFAULT_ALIGNMENT;
    a->flags = flags | ARENA_FLAG_MMAP;
    a->fd = fd;
    a->path = strdup(path);
    if (!a->path) {
        munmap(base, size);
        close(fd);
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate path string");
    }

    *arena = a;
    return MEM_OK;
}

mem_error_t arena_open_mmap(arena_t** arena, const char* path, uint32_t flags) {
    MEM_CHECK_ERR(arena != NULL, MEM_ERR_INVALID_ARG, "arena pointer is NULL");
    MEM_CHECK_ERR(path != NULL, MEM_ERR_INVALID_ARG, "path is NULL");

    arena_t* a = malloc(sizeof(arena_t));
    MEM_CHECK_ALLOC(a);

    /* Get file size */
    struct stat st;
    if (stat(path, &st) < 0) {
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_OPEN, "failed to stat %s", path);
    }

    /* Open file */
    int open_flags = (flags & ARENA_FLAG_READONLY) ? O_RDONLY : O_RDWR;
    int fd = open(path, open_flags);
    if (fd < 0) {
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_OPEN, "failed to open %s", path);
    }

    /* Map file */
    int prot = PROT_READ;
    if (!(flags & ARENA_FLAG_READONLY)) {
        prot |= PROT_WRITE;
    }
    int mflags = MAP_SHARED;

    void* base = mmap(NULL, (size_t)st.st_size, prot, mflags, fd, 0);
    if (base == MAP_FAILED) {
        close(fd);
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_MMAP, "mmap failed for %s", path);
    }

    a->base = base;
    a->size = (size_t)st.st_size;
    a->used = 0;  /* Note: caller must track used separately for persistent arenas */
    a->alignment = DEFAULT_ALIGNMENT;
    a->flags = flags | ARENA_FLAG_MMAP;
    a->fd = fd;
    a->path = strdup(path);
    if (!a->path) {
        munmap(base, (size_t)st.st_size);
        close(fd);
        free(a);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate path string");
    }

    *arena = a;
    return MEM_OK;
}

void* arena_alloc(arena_t* arena, size_t size) {
    return arena_alloc_aligned(arena, size, arena->alignment);
}

void* arena_alloc_aligned(arena_t* arena, size_t size, size_t alignment) {
    if (!arena || size == 0) return NULL;

    /* Validate alignment: must be non-zero and power of 2 */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    /* Check for potential overflow in alignment calculation */
    uintptr_t current_addr = (uintptr_t)arena->base + arena->used;
    if (current_addr > UINTPTR_MAX - alignment + 1) {
        return NULL;  /* Would overflow in alignment calculation */
    }

    /* Calculate the actual address and align it */
    uintptr_t aligned_addr = (current_addr + alignment - 1) & ~(alignment - 1);
    size_t padding = aligned_addr - current_addr;

    /* Check for overflow: padding + size must not overflow */
    if (padding > SIZE_MAX - size) {
        return NULL;  /* Addition would overflow */
    }
    size_t alloc_total = padding + size;

    /* Check for overflow: arena->used + alloc_total must not overflow */
    if (arena->used > SIZE_MAX - alloc_total) {
        return NULL;  /* Addition would overflow */
    }
    size_t new_used = arena->used + alloc_total;

    /* Check space */
    if (new_used > arena->size) {
        return NULL;
    }

    arena->used = new_used;
    return (void*)aligned_addr;
}

void* arena_get_ptr(arena_t* arena, size_t offset) {
    if (!arena || offset >= arena->size) return NULL;
    return (char*)arena->base + offset;
}

size_t arena_get_offset(arena_t* arena, const void* ptr) {
    if (!arena || !ptr) return (size_t)-1;

    const char* p = (const char*)ptr;
    const char* base = (const char*)arena->base;

    if (p < base || p >= base + arena->size) {
        return (size_t)-1;
    }

    return (size_t)(p - base);
}

void arena_reset(arena_t* arena) {
    if (arena) {
        arena->used = 0;
    }
}

void arena_reset_secure(arena_t* arena) {
    if (arena && arena->base) {
        /* Zero memory to prevent information disclosure */
        memset(arena->base, 0, arena->size);
        arena->used = 0;
    }
}

mem_error_t arena_sync(arena_t* arena) {
    MEM_CHECK_ERR(arena != NULL, MEM_ERR_INVALID_ARG, "arena is NULL");

    if (!(arena->flags & ARENA_FLAG_MMAP)) {
        return MEM_OK;  /* Nothing to sync for heap arenas */
    }

    if (msync(arena->base, arena->size, MS_SYNC) < 0) {
        MEM_RETURN_ERROR(MEM_ERR_SYNC, "msync failed");
    }

    return MEM_OK;
}

mem_error_t arena_grow(arena_t* arena, size_t new_size) {
    MEM_CHECK_ERR(arena != NULL, MEM_ERR_INVALID_ARG, "arena is NULL");
    MEM_CHECK_ERR(new_size > arena->size, MEM_ERR_INVALID_ARG, "new size must be larger");

    if (arena->flags & ARENA_FLAG_MMAP) {
        /* For mmap'd arenas, we need to remap */
        if (ftruncate(arena->fd, (off_t)new_size) < 0) {
            MEM_RETURN_ERROR(MEM_ERR_TRUNCATE, "failed to grow file");
        }

        /* Use platform-specific remap implementation */
        void* new_base = platform_mremap(arena->base, arena->size, new_size, arena->fd);
        if (new_base == MAP_FAILED) {
            MEM_RETURN_ERROR(MEM_ERR_MMAP, "remap failed");
        }
        arena->base = new_base;
        arena->size = new_size;
    } else {
        /* For heap arenas, realloc */
        void* new_base = realloc(arena->base, new_size);
        MEM_CHECK_ALLOC(new_base);

        arena->base = new_base;
        arena->size = new_size;
    }

    return MEM_OK;
}

void arena_destroy(arena_t* arena) {
    if (!arena) return;

    if (arena->flags & ARENA_FLAG_MMAP) {
        if (arena->base && arena->base != MAP_FAILED) {
            msync(arena->base, arena->size, MS_SYNC);
            munmap(arena->base, arena->size);
        }
        if (arena->fd >= 0) {
            close(arena->fd);
        }
        free(arena->path);
    } else {
        free(arena->base);
    }

    free(arena);
}

size_t arena_used(const arena_t* arena) {
    return arena ? arena->used : 0;
}

size_t arena_available(const arena_t* arena) {
    return arena ? arena->size - arena->used : 0;
}

size_t arena_size(const arena_t* arena) {
    return arena ? arena->size : 0;
}

bool arena_is_mmap(const arena_t* arena) {
    return arena && (arena->flags & ARENA_FLAG_MMAP);
}
