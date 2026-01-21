/*
 * Memory Service - Relations Storage
 *
 * mmap'd storage for hierarchical relationships:
 * - parent[node_id] = parent_id
 * - first_child[node_id] = child_id
 * - next_sibling[node_id] = sibling_id
 * - level[node_id] = hierarchy_level
 */

#ifndef MEMORY_SERVICE_RELATIONS_H
#define MEMORY_SERVICE_RELATIONS_H

#include "../core/arena.h"
#include "../../include/types.h"
#include "../../include/error.h"

/* Relations store */
typedef struct {
    arena_t*        parent_arena;       /* parent[id] = parent_id */
    arena_t*        first_child_arena;  /* first_child[id] = child_id */
    arena_t*        next_sibling_arena; /* next_sibling[id] = sibling_id */
    arena_t*        level_arena;        /* level[id] = hierarchy_level */
    char*           base_dir;
    size_t          count;              /* Number of nodes */
    size_t          capacity;           /* Max nodes before grow */
} relations_store_t;

/* Create relations store */
mem_error_t relations_create(relations_store_t** store, const char* dir,
                             size_t initial_capacity);

/* Open existing relations store */
mem_error_t relations_open(relations_store_t** store, const char* dir);

/* Allocate new node slot, returns node_id */
mem_error_t relations_alloc_node(relations_store_t* store, node_id_t* id);

/* Set parent relationship */
mem_error_t relations_set_parent(relations_store_t* store, node_id_t node_id,
                                 node_id_t parent_id);

/* Set first child relationship */
mem_error_t relations_set_first_child(relations_store_t* store, node_id_t node_id,
                                      node_id_t child_id);

/* Set next sibling relationship */
mem_error_t relations_set_next_sibling(relations_store_t* store, node_id_t node_id,
                                       node_id_t sibling_id);

/* Set node level */
mem_error_t relations_set_level(relations_store_t* store, node_id_t node_id,
                                hierarchy_level_t level);

/* Get parent */
node_id_t relations_get_parent(const relations_store_t* store, node_id_t node_id);

/* Get first child */
node_id_t relations_get_first_child(const relations_store_t* store, node_id_t node_id);

/* Get next sibling */
node_id_t relations_get_next_sibling(const relations_store_t* store, node_id_t node_id);

/* Get level */
hierarchy_level_t relations_get_level(const relations_store_t* store, node_id_t node_id);

/* Get all children (fills array, returns count) */
size_t relations_get_children(const relations_store_t* store, node_id_t node_id,
                              node_id_t* children, size_t max_children);

/* Get all siblings (fills array, returns count) */
size_t relations_get_siblings(const relations_store_t* store, node_id_t node_id,
                              node_id_t* siblings, size_t max_siblings);

/* Get ancestors up to root (fills array, returns count) */
size_t relations_get_ancestors(const relations_store_t* store, node_id_t node_id,
                               node_id_t* ancestors, size_t max_ancestors);

/* Count descendants recursively */
size_t relations_count_descendants(const relations_store_t* store, node_id_t node_id);

/* Get node count */
size_t relations_count(const relations_store_t* store);

/* Sync to disk */
mem_error_t relations_sync(relations_store_t* store);

/* Close store */
void relations_close(relations_store_t* store);

#endif /* MEMORY_SERVICE_RELATIONS_H */
