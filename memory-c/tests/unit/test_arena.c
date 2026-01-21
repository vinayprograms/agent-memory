/*
 * Memory Service - Arena Allocator Tests
 */

#include "../test_framework.h"
#include "../../src/core/arena.h"
#include "../../include/error.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* Test basic arena creation */
TEST(arena_create_basic) {
    arena_t* arena = NULL;
    mem_error_t err = arena_create(&arena, 1024);

    ASSERT_OK(err);
    ASSERT_NOT_NULL(arena);
    ASSERT_EQ(arena_size(arena), 1024);
    ASSERT_EQ(arena_used(arena), 0);
    ASSERT_EQ(arena_available(arena), 1024);
    ASSERT(!arena_is_mmap(arena));

    arena_destroy(arena);
}

/* Test arena allocation */
TEST(arena_alloc_basic) {
    arena_t* arena = NULL;
    ASSERT_OK(arena_create(&arena, 1024));

    void* p1 = arena_alloc(arena, 100);
    ASSERT_NOT_NULL(p1);
    ASSERT_GE(arena_used(arena), 100);

    void* p2 = arena_alloc(arena, 200);
    ASSERT_NOT_NULL(p2);
    ASSERT_GE(arena_used(arena), 300);

    /* Pointers should be different and in order */
    ASSERT_NE(p1, p2);
    ASSERT_LT(p1, p2);

    arena_destroy(arena);
}

/* Test arena aligned allocation */
TEST(arena_alloc_aligned) {
    arena_t* arena = NULL;
    ASSERT_OK(arena_create(&arena, 16384));  /* Larger arena for alignment tests */

    /* Allocate with various alignments */
    void* p1 = arena_alloc_aligned(arena, 1, 1);
    ASSERT_NOT_NULL(p1);

    void* p2 = arena_alloc_aligned(arena, 1, 16);
    ASSERT_NOT_NULL(p2);
    ASSERT_EQ((uintptr_t)p2 % 16, 0);

    void* p3 = arena_alloc_aligned(arena, 1, 64);
    ASSERT_NOT_NULL(p3);
    ASSERT_EQ((uintptr_t)p3 % 64, 0);

    /* 256-byte alignment needs extra padding */
    void* p4 = arena_alloc_aligned(arena, 256, 256);
    ASSERT_NOT_NULL(p4);
    ASSERT_EQ((uintptr_t)p4 % 256, 0);

    arena_destroy(arena);
}

/* Test arena overflow */
TEST(arena_overflow) {
    arena_t* arena = NULL;
    ASSERT_OK(arena_create(&arena, 256));

    /* With 16-byte alignment, 100 bytes rounds up to 112 */
    void* p1 = arena_alloc(arena, 100);
    ASSERT_NOT_NULL(p1);

    void* p2 = arena_alloc(arena, 100);
    ASSERT_NOT_NULL(p2);

    /* This should fail - no space left (would need 336+ bytes total) */
    void* p3 = arena_alloc(arena, 100);
    ASSERT_NULL(p3);

    arena_destroy(arena);
}

/* Test arena reset */
TEST(arena_reset) {
    arena_t* arena = NULL;
    ASSERT_OK(arena_create(&arena, 1024));

    arena_alloc(arena, 500);
    ASSERT_GE(arena_used(arena), 500);

    arena_reset(arena);
    ASSERT_EQ(arena_used(arena), 0);
    ASSERT_EQ(arena_available(arena), 1024);

    /* Should be able to allocate again */
    void* p = arena_alloc(arena, 500);
    ASSERT_NOT_NULL(p);

    arena_destroy(arena);
}

/* Test arena grow */
TEST(arena_grow) {
    arena_t* arena = NULL;
    ASSERT_OK(arena_create(&arena, 256));

    arena_alloc(arena, 200);
    ASSERT_EQ(arena_size(arena), 256);

    ASSERT_OK(arena_grow(arena, 512));
    ASSERT_EQ(arena_size(arena), 512);

    /* Should be able to allocate more now */
    void* p = arena_alloc(arena, 250);
    ASSERT_NOT_NULL(p);

    arena_destroy(arena);
}

/* Test mmap'd arena creation */
TEST(arena_mmap_create) {
    const char* path = "/tmp/test_arena_mmap.bin";
    arena_t* arena = NULL;

    mem_error_t err = arena_create_mmap(&arena, path, 4096, 0);
    ASSERT_OK(err);
    ASSERT_NOT_NULL(arena);
    ASSERT(arena_is_mmap(arena));
    ASSERT_EQ(arena_size(arena), 4096);

    /* Verify file exists */
    struct stat st;
    ASSERT_EQ(stat(path, &st), 0);
    ASSERT_EQ(st.st_size, 4096);

    arena_destroy(arena);
    unlink(path);
}

/* Test mmap'd arena persistence */
TEST(arena_mmap_persistence) {
    const char* path = "/tmp/test_arena_persist.bin";

    /* Create and write data */
    {
        arena_t* arena = NULL;
        ASSERT_OK(arena_create_mmap(&arena, path, 4096, 0));

        uint32_t* data = arena_alloc(arena, sizeof(uint32_t) * 4);
        ASSERT_NOT_NULL(data);
        data[0] = 0xDEADBEEF;
        data[1] = 0xCAFEBABE;
        data[2] = 0x12345678;
        data[3] = 0x87654321;

        ASSERT_OK(arena_sync(arena));
        arena_destroy(arena);
    }

    /* Reopen and verify */
    {
        arena_t* arena = NULL;
        ASSERT_OK(arena_open_mmap(&arena, path, 0));

        uint32_t* data = arena_get_ptr(arena, 0);
        ASSERT_NOT_NULL(data);
        ASSERT_EQ(data[0], 0xDEADBEEF);
        ASSERT_EQ(data[1], 0xCAFEBABE);
        ASSERT_EQ(data[2], 0x12345678);
        ASSERT_EQ(data[3], 0x87654321);

        arena_destroy(arena);
    }

    unlink(path);
}

/* Test arena offset operations */
TEST(arena_offset_operations) {
    arena_t* arena = NULL;
    ASSERT_OK(arena_create(&arena, 1024));

    void* p1 = arena_alloc(arena, 100);
    void* p2 = arena_alloc(arena, 100);

    size_t off1 = arena_get_offset(arena, p1);
    size_t off2 = arena_get_offset(arena, p2);

    ASSERT_NE(off1, (size_t)-1);
    ASSERT_NE(off2, (size_t)-1);
    ASSERT_LT(off1, off2);

    /* Convert back to pointers */
    ASSERT_EQ(arena_get_ptr(arena, off1), p1);
    ASSERT_EQ(arena_get_ptr(arena, off2), p2);

    /* Invalid offset should return NULL */
    ASSERT_NULL(arena_get_ptr(arena, 999999));

    /* Pointer outside arena should return -1 */
    int x;
    ASSERT_EQ(arena_get_offset(arena, &x), (size_t)-1);

    arena_destroy(arena);
}

/* Test NULL and invalid arguments */
TEST(arena_invalid_args) {
    arena_t* arena = NULL;

    /* NULL arena pointer */
    ASSERT_EQ(arena_create(NULL, 1024), MEM_ERR_INVALID_ARG);

    /* Zero size */
    ASSERT_EQ(arena_create(&arena, 0), MEM_ERR_INVALID_ARG);

    /* NULL path for mmap */
    ASSERT_EQ(arena_create_mmap(&arena, NULL, 1024, 0), MEM_ERR_INVALID_ARG);
}

/* Test typed allocation macros */
TEST(arena_typed_alloc) {
    arena_t* arena = NULL;
    ASSERT_OK(arena_create(&arena, 4096));

    typedef struct {
        int32_t x;
        int32_t y;
        double z;
    } point_t;

    point_t* p = ARENA_ALLOC(arena, point_t);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((uintptr_t)p % _Alignof(point_t), 0);

    p->x = 10;
    p->y = 20;
    p->z = 3.14159;

    point_t* arr = ARENA_ALLOC_ARRAY(arena, point_t, 10);
    ASSERT_NOT_NULL(arr);
    ASSERT_EQ((uintptr_t)arr % _Alignof(point_t), 0);

    for (int i = 0; i < 10; i++) {
        arr[i].x = i;
        arr[i].y = i * 2;
        arr[i].z = (double)i * 0.5;
    }

    ASSERT_EQ(arr[5].x, 5);
    ASSERT_EQ(arr[5].y, 10);

    arena_destroy(arena);
}

/* Test large allocation */
TEST(arena_large_alloc) {
    arena_t* arena = NULL;
    size_t size = 16 * 1024 * 1024;  /* 16 MB */
    ASSERT_OK(arena_create(&arena, size));

    /* Allocate large chunk */
    void* p = arena_alloc(arena, size - 1024);
    ASSERT_NOT_NULL(p);

    /* Fill with pattern */
    memset(p, 0xAA, size - 1024);

    arena_destroy(arena);
}

TEST_MAIN()
