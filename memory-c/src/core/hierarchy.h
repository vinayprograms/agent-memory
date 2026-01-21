/*
 * Memory Service - Hierarchy Management
 *
 * Manages the hierarchical data model:
 * - Agent -> Session -> Message -> Block -> Statement
 * - Parent/child/sibling relationships
 * - Level-specific operations
 */

#ifndef MEMORY_SERVICE_HIERARCHY_H
#define MEMORY_SERVICE_HIERARCHY_H

#include "../../include/types.h"
#include "../../include/error.h"
#include "../storage/relations.h"
#include "../storage/embeddings.h"

/* Forward declaration */
typedef struct hierarchy hierarchy_t;

/* Node info returned from queries */
typedef struct {
    node_id_t       id;
    hierarchy_level_t level;
    node_id_t       parent_id;
    node_id_t       first_child_id;
    node_id_t       next_sibling_id;
    uint32_t        embedding_idx;
    timestamp_ns_t  created_at;
    char            agent_id[MAX_AGENT_ID_LEN];
    char            session_id[MAX_SESSION_ID_LEN];
} node_info_t;

/* Create a new hierarchy manager */
mem_error_t hierarchy_create(hierarchy_t** h, const char* dir, size_t capacity);

/* Open existing hierarchy */
mem_error_t hierarchy_open(hierarchy_t** h, const char* dir);

/* Close hierarchy manager */
void hierarchy_close(hierarchy_t* h);

/* Sync to disk */
mem_error_t hierarchy_sync(hierarchy_t* h);

/*
 * Node creation functions
 */

/* Create or find an agent node by agent_id string.
 * Returns MEM_ERR_EXISTS if agent already exists (out_id set to existing).
 */
mem_error_t hierarchy_create_agent(hierarchy_t* h,
                                   const char* agent_id,
                                   node_id_t* out_id);

/* Create or find a session node under an agent.
 * Returns MEM_ERR_EXISTS if session already exists (out_id set to existing).
 */
mem_error_t hierarchy_create_session(hierarchy_t* h,
                                     node_id_t agent_node_id,
                                     const char* session_id,
                                     node_id_t* out_id);

/* Create a child node under a parent */
mem_error_t hierarchy_create_child(hierarchy_t* h,
                                   node_id_t parent_id,
                                   hierarchy_level_t level,
                                   node_id_t* out_id);

/* Create a message under a session */
mem_error_t hierarchy_create_message(hierarchy_t* h,
                                     node_id_t session_id,
                                     node_id_t* out_id);

/* Create a block under a message */
mem_error_t hierarchy_create_block(hierarchy_t* h,
                                   node_id_t message_id,
                                   node_id_t* out_id);

/* Create a statement under a block */
mem_error_t hierarchy_create_statement(hierarchy_t* h,
                                       node_id_t block_id,
                                       node_id_t* out_id);

/*
 * Node query functions
 */

/* Get node info */
mem_error_t hierarchy_get_node(const hierarchy_t* h, node_id_t id, node_info_t* info);

/* Get parent of a node */
node_id_t hierarchy_get_parent(const hierarchy_t* h, node_id_t id);

/* Get first child of a node */
node_id_t hierarchy_get_first_child(const hierarchy_t* h, node_id_t id);

/* Get next sibling of a node */
node_id_t hierarchy_get_next_sibling(const hierarchy_t* h, node_id_t id);

/* Get level of a node */
hierarchy_level_t hierarchy_get_level(const hierarchy_t* h, node_id_t id);

/* Get all children of a node */
size_t hierarchy_get_children(const hierarchy_t* h, node_id_t id,
                              node_id_t* children, size_t max_count);

/* Get all siblings of a node (excluding self) */
size_t hierarchy_get_siblings(const hierarchy_t* h, node_id_t id,
                              node_id_t* siblings, size_t max_count);

/* Get ancestors up to root */
size_t hierarchy_get_ancestors(const hierarchy_t* h, node_id_t id,
                               node_id_t* ancestors, size_t max_count);

/* Count descendants (recursive) */
size_t hierarchy_count_descendants(const hierarchy_t* h, node_id_t id);

/* Total node count */
size_t hierarchy_count(const hierarchy_t* h);

/*
 * Embedding functions
 */

/* Set embedding for a node */
mem_error_t hierarchy_set_embedding(hierarchy_t* h, node_id_t id,
                                    const float* values);

/* Get embedding for a node */
const float* hierarchy_get_embedding(const hierarchy_t* h, node_id_t id);

/* Compute similarity between two nodes */
float hierarchy_similarity(const hierarchy_t* h, node_id_t id1, node_id_t id2);

/*
 * Text content storage
 */

/* Set text content for a node */
mem_error_t hierarchy_set_text(hierarchy_t* h, node_id_t id,
                               const char* text, size_t len);

/* Get text content for a node (returns pointer to internal storage) */
const char* hierarchy_get_text(const hierarchy_t* h, node_id_t id, size_t* len);

/*
 * Session iteration
 */

/* Callback for session iteration */
typedef bool (*session_iter_fn)(node_id_t session_id, const char* agent_id,
                                const char* session_str, void* user_data);

/* Iterate all sessions */
size_t hierarchy_iter_sessions(const hierarchy_t* h, session_iter_fn callback, void* user_data);

/*
 * Access underlying stores (for advanced operations)
 */

relations_store_t* hierarchy_get_relations(hierarchy_t* h);
embeddings_store_t* hierarchy_get_embeddings(hierarchy_t* h);

#endif /* MEMORY_SERVICE_HIERARCHY_H */
