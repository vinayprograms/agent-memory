/*
 * Memory Service - Relations Storage Implementation
 */

#include "relations.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* File names */
#define PARENT_FILE "parent.bin"
#define FIRST_CHILD_FILE "first_child.bin"
#define NEXT_SIBLING_FILE "next_sibling.bin"
#define LEVEL_FILE "level.bin"

/* Header at start of each file */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t capacity;
} relations_header_t;

#define RELATIONS_MAGIC 0x52454C30  /* "REL0" */
#define RELATIONS_VERSION 1
#define HEADER_SIZE sizeof(relations_header_t)

/* Calculate file size */
static size_t calc_file_size(size_t capacity, size_t element_size) {
    return HEADER_SIZE + capacity * element_size;
}

/* Open or create arena for relation type */
static mem_error_t open_relation_arena(arena_t** arena, const char* dir,
                                       const char* filename, size_t capacity,
                                       size_t element_size, bool create) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);

    if (create) {
        size_t file_size = calc_file_size(capacity, element_size);
        MEM_CHECK(arena_create_mmap(arena, path, file_size, 0));

        /* Write header */
        relations_header_t* hdr = arena_alloc(*arena, HEADER_SIZE);
        MEM_CHECK_ALLOC(hdr);

        hdr->magic = RELATIONS_MAGIC;
        hdr->version = RELATIONS_VERSION;
        hdr->count = 0;
        hdr->capacity = (uint32_t)capacity;

        /* Initialize data to invalid */
        void* data = arena_alloc(*arena, capacity * element_size);
        MEM_CHECK_ALLOC(data);

        if (element_size == sizeof(node_id_t)) {
            /* Initialize node_id arrays to NODE_ID_INVALID */
            node_id_t* arr = (node_id_t*)data;
            for (size_t i = 0; i < capacity; i++) {
                arr[i] = NODE_ID_INVALID;
            }
        } else {
            /* Initialize level array to 0 */
            memset(data, 0, capacity * element_size);
        }
    } else {
        MEM_CHECK(arena_open_mmap(arena, path, 0));

        /* Validate header */
        relations_header_t* hdr = arena_get_ptr(*arena, 0);
        if (!hdr || hdr->magic != RELATIONS_MAGIC) {
            arena_destroy(*arena);
            *arena = NULL;
            MEM_RETURN_ERROR(MEM_ERR_INDEX_CORRUPT, "invalid relations file %s", filename);
        }
    }

    return MEM_OK;
}

mem_error_t relations_create(relations_store_t** store, const char* dir,
                             size_t initial_capacity) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(dir != NULL, MEM_ERR_INVALID_ARG, "dir is NULL");
    MEM_CHECK_ERR(initial_capacity > 0, MEM_ERR_INVALID_ARG, "capacity must be > 0");

    relations_store_t* s = calloc(1, sizeof(relations_store_t));
    MEM_CHECK_ALLOC(s);

    s->base_dir = strdup(dir);
    if (!s->base_dir) {
        free(s);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate dir path");
    }

    /* Create each relation file */
    mem_error_t err;

    err = open_relation_arena(&s->parent_arena, dir, PARENT_FILE,
                              initial_capacity, sizeof(node_id_t), true);
    if (err != MEM_OK) goto cleanup;

    err = open_relation_arena(&s->first_child_arena, dir, FIRST_CHILD_FILE,
                              initial_capacity, sizeof(node_id_t), true);
    if (err != MEM_OK) goto cleanup;

    err = open_relation_arena(&s->next_sibling_arena, dir, NEXT_SIBLING_FILE,
                              initial_capacity, sizeof(node_id_t), true);
    if (err != MEM_OK) goto cleanup;

    err = open_relation_arena(&s->level_arena, dir, LEVEL_FILE,
                              initial_capacity, sizeof(uint8_t), true);
    if (err != MEM_OK) goto cleanup;

    s->count = 0;
    s->capacity = initial_capacity;

    *store = s;
    LOG_INFO("Relations store created at %s with capacity %zu", dir, initial_capacity);
    return MEM_OK;

cleanup:
    if (s->parent_arena) arena_destroy(s->parent_arena);
    if (s->first_child_arena) arena_destroy(s->first_child_arena);
    if (s->next_sibling_arena) arena_destroy(s->next_sibling_arena);
    if (s->level_arena) arena_destroy(s->level_arena);
    free(s->base_dir);
    free(s);
    return err;
}

mem_error_t relations_open(relations_store_t** store, const char* dir) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(dir != NULL, MEM_ERR_INVALID_ARG, "dir is NULL");

    relations_store_t* s = calloc(1, sizeof(relations_store_t));
    MEM_CHECK_ALLOC(s);

    s->base_dir = strdup(dir);
    if (!s->base_dir) {
        free(s);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate dir path");
    }

    mem_error_t err;

    err = open_relation_arena(&s->parent_arena, dir, PARENT_FILE, 0, sizeof(node_id_t), false);
    if (err != MEM_OK) goto cleanup;

    err = open_relation_arena(&s->first_child_arena, dir, FIRST_CHILD_FILE, 0, sizeof(node_id_t), false);
    if (err != MEM_OK) goto cleanup;

    err = open_relation_arena(&s->next_sibling_arena, dir, NEXT_SIBLING_FILE, 0, sizeof(node_id_t), false);
    if (err != MEM_OK) goto cleanup;

    err = open_relation_arena(&s->level_arena, dir, LEVEL_FILE, 0, sizeof(uint8_t), false);
    if (err != MEM_OK) goto cleanup;

    /* Read count and capacity from parent file header */
    relations_header_t* hdr = arena_get_ptr(s->parent_arena, 0);
    s->count = hdr->count;
    s->capacity = hdr->capacity;

    *store = s;
    LOG_INFO("Relations store opened at %s with %zu nodes", dir, s->count);
    return MEM_OK;

cleanup:
    if (s->parent_arena) arena_destroy(s->parent_arena);
    if (s->first_child_arena) arena_destroy(s->first_child_arena);
    if (s->next_sibling_arena) arena_destroy(s->next_sibling_arena);
    if (s->level_arena) arena_destroy(s->level_arena);
    free(s->base_dir);
    free(s);
    return err;
}

mem_error_t relations_alloc_node(relations_store_t* store, node_id_t* id) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(id != NULL, MEM_ERR_INVALID_ARG, "id is NULL");

    if (store->count >= store->capacity) {
        MEM_RETURN_ERROR(MEM_ERR_FULL, "relations store at capacity");
    }

    *id = (node_id_t)store->count;
    store->count++;

    /* Update header count in all files */
    relations_header_t* hdr = arena_get_ptr(store->parent_arena, 0);
    if (hdr) hdr->count = (uint32_t)store->count;

    return MEM_OK;
}

/* Helper to get pointer into relation array */
static inline node_id_t* get_node_ptr(arena_t* arena, node_id_t id) {
    size_t offset = HEADER_SIZE + id * sizeof(node_id_t);
    return arena_get_ptr(arena, offset);
}

static inline uint8_t* get_level_ptr(arena_t* arena, node_id_t id) {
    size_t offset = HEADER_SIZE + id * sizeof(uint8_t);
    return arena_get_ptr(arena, offset);
}

mem_error_t relations_set_parent(relations_store_t* store, node_id_t node_id,
                                 node_id_t parent_id) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(node_id < store->count, MEM_ERR_NOT_FOUND, "node not found");

    node_id_t* ptr = get_node_ptr(store->parent_arena, node_id);
    if (!ptr) MEM_RETURN_ERROR(MEM_ERR_INDEX, "failed to get parent pointer");

    *ptr = parent_id;
    return MEM_OK;
}

mem_error_t relations_set_first_child(relations_store_t* store, node_id_t node_id,
                                      node_id_t child_id) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(node_id < store->count, MEM_ERR_NOT_FOUND, "node not found");

    node_id_t* ptr = get_node_ptr(store->first_child_arena, node_id);
    if (!ptr) MEM_RETURN_ERROR(MEM_ERR_INDEX, "failed to get first_child pointer");

    *ptr = child_id;
    return MEM_OK;
}

mem_error_t relations_set_next_sibling(relations_store_t* store, node_id_t node_id,
                                       node_id_t sibling_id) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(node_id < store->count, MEM_ERR_NOT_FOUND, "node not found");

    node_id_t* ptr = get_node_ptr(store->next_sibling_arena, node_id);
    if (!ptr) MEM_RETURN_ERROR(MEM_ERR_INDEX, "failed to get next_sibling pointer");

    *ptr = sibling_id;
    return MEM_OK;
}

mem_error_t relations_set_level(relations_store_t* store, node_id_t node_id,
                                hierarchy_level_t level) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");
    MEM_CHECK_ERR(node_id < store->count, MEM_ERR_NOT_FOUND, "node not found");
    MEM_CHECK_ERR(level < LEVEL_COUNT, MEM_ERR_INVALID_LEVEL, "invalid level");

    uint8_t* ptr = get_level_ptr(store->level_arena, node_id);
    if (!ptr) MEM_RETURN_ERROR(MEM_ERR_INDEX, "failed to get level pointer");

    *ptr = (uint8_t)level;
    return MEM_OK;
}

node_id_t relations_get_parent(const relations_store_t* store, node_id_t node_id) {
    if (!store || node_id >= store->count) return NODE_ID_INVALID;

    node_id_t* ptr = get_node_ptr(store->parent_arena, node_id);
    return ptr ? *ptr : NODE_ID_INVALID;
}

node_id_t relations_get_first_child(const relations_store_t* store, node_id_t node_id) {
    if (!store || node_id >= store->count) return NODE_ID_INVALID;

    node_id_t* ptr = get_node_ptr(store->first_child_arena, node_id);
    return ptr ? *ptr : NODE_ID_INVALID;
}

node_id_t relations_get_next_sibling(const relations_store_t* store, node_id_t node_id) {
    if (!store || node_id >= store->count) return NODE_ID_INVALID;

    node_id_t* ptr = get_node_ptr(store->next_sibling_arena, node_id);
    return ptr ? *ptr : NODE_ID_INVALID;
}

hierarchy_level_t relations_get_level(const relations_store_t* store, node_id_t node_id) {
    if (!store || node_id >= store->count) return LEVEL_STATEMENT;

    uint8_t* ptr = get_level_ptr(store->level_arena, node_id);
    return ptr ? (hierarchy_level_t)*ptr : LEVEL_STATEMENT;
}

size_t relations_get_children(const relations_store_t* store, node_id_t node_id,
                              node_id_t* children, size_t max_children) {
    if (!store || !children || max_children == 0) return 0;

    size_t count = 0;
    node_id_t child = relations_get_first_child(store, node_id);

    while (child != NODE_ID_INVALID && count < max_children) {
        children[count++] = child;
        child = relations_get_next_sibling(store, child);
    }

    return count;
}

size_t relations_get_siblings(const relations_store_t* store, node_id_t node_id,
                              node_id_t* siblings, size_t max_siblings) {
    if (!store || !siblings || max_siblings == 0) return 0;

    /* Find first sibling by going to parent, then first child */
    node_id_t parent = relations_get_parent(store, node_id);
    if (parent == NODE_ID_INVALID) return 0;

    node_id_t first = relations_get_first_child(store, parent);
    size_t count = 0;

    while (first != NODE_ID_INVALID && count < max_siblings) {
        if (first != node_id) {  /* Don't include self */
            siblings[count++] = first;
        }
        first = relations_get_next_sibling(store, first);
    }

    return count;
}

size_t relations_get_ancestors(const relations_store_t* store, node_id_t node_id,
                               node_id_t* ancestors, size_t max_ancestors) {
    if (!store || !ancestors || max_ancestors == 0) return 0;

    size_t count = 0;
    node_id_t current = relations_get_parent(store, node_id);

    while (current != NODE_ID_INVALID && count < max_ancestors) {
        ancestors[count++] = current;
        current = relations_get_parent(store, current);
    }

    return count;
}

size_t relations_count_descendants(const relations_store_t* store, node_id_t node_id) {
    if (!store || node_id >= store->count) return 0;

    size_t count = 0;
    node_id_t child = relations_get_first_child(store, node_id);

    while (child != NODE_ID_INVALID) {
        count++;  /* Count this child */
        count += relations_count_descendants(store, child);  /* Count its descendants */
        child = relations_get_next_sibling(store, child);
    }

    return count;
}

size_t relations_count(const relations_store_t* store) {
    return store ? store->count : 0;
}

mem_error_t relations_sync(relations_store_t* store) {
    MEM_CHECK_ERR(store != NULL, MEM_ERR_INVALID_ARG, "store is NULL");

    MEM_CHECK(arena_sync(store->parent_arena));
    MEM_CHECK(arena_sync(store->first_child_arena));
    MEM_CHECK(arena_sync(store->next_sibling_arena));
    MEM_CHECK(arena_sync(store->level_arena));

    return MEM_OK;
}

void relations_close(relations_store_t* store) {
    if (!store) return;

    relations_sync(store);

    if (store->parent_arena) arena_destroy(store->parent_arena);
    if (store->first_child_arena) arena_destroy(store->first_child_arena);
    if (store->next_sibling_arena) arena_destroy(store->next_sibling_arena);
    if (store->level_arena) arena_destroy(store->level_arena);

    free(store->base_dir);
    free(store);
}
