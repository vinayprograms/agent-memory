/*
 * Memory Service - Arena Allocator
 *
 * Fast, contiguous memory allocation with mmap support for persistence.
 */

#ifndef MEMORY_SERVICE_ARENA_H
#define MEMORY_SERVICE_ARENA_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "error.h"

/* Arena flags */
#define ARENA_FLAG_MMAP     (1 << 0)    /* Use memory-mapped file */
#define ARENA_FLAG_SHARED   (1 << 1)    /* Shared between processes */
#define ARENA_FLAG_READONLY (1 << 2)    /* Read-only mapping */

/* Arena structure */
typedef struct arena {
    void*       base;           /* Base address */
    size_t      size;           /* Total size */
    size_t      used;           /* Bytes used */
    size_t      alignment;      /* Default alignment */
    uint32_t    flags;
    int         fd;             /* File descriptor for mmap */
    char*       path;           /* File path for mmap */
} arena_t;

/* Create memory arena (heap-backed) */
mem_error_t arena_create(arena_t** arena, size_t size);

/* Create memory-mapped arena (file-backed) */
mem_error_t arena_create_mmap(arena_t** arena, const char* path, size_t size, uint32_t flags);

/* Open existing mmap'd arena */
mem_error_t arena_open_mmap(arena_t** arena, const char* path, uint32_t flags);

/* Allocate from arena */
void* arena_alloc(arena_t* arena, size_t size);

/* Allocate with specific alignment */
void* arena_alloc_aligned(arena_t* arena, size_t size, size_t alignment);

/* Get pointer by offset (for persistent arenas) */
void* arena_get_ptr(arena_t* arena, size_t offset);

/* Get offset from pointer */
size_t arena_get_offset(arena_t* arena, const void* ptr);

/* Reset arena (free all allocations) */
void arena_reset(arena_t* arena);

/* Reset arena and securely clear memory (prevents information disclosure) */
void arena_reset_secure(arena_t* arena);

/* Sync to disk (for mmap'd arenas) */
mem_error_t arena_sync(arena_t* arena);

/* Grow arena */
mem_error_t arena_grow(arena_t* arena, size_t new_size);

/* Destroy arena */
void arena_destroy(arena_t* arena);

/* Get stats */
size_t arena_used(const arena_t* arena);
size_t arena_available(const arena_t* arena);
size_t arena_size(const arena_t* arena);
bool arena_is_mmap(const arena_t* arena);

/* Allocate typed */
#define ARENA_ALLOC(arena, type) \
    ((type*)arena_alloc_aligned((arena), sizeof(type), _Alignof(type)))

#define ARENA_ALLOC_ARRAY(arena, type, count) \
    ((type*)arena_alloc_aligned((arena), sizeof(type) * (count), _Alignof(type)))

#endif /* MEMORY_SERVICE_ARENA_H */
