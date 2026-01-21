/*
 * Memory Service - Hierarchy Management Implementation
 */

#include "hierarchy.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* Internal node metadata stored in memory */
typedef struct {
    timestamp_ns_t  created_at;
    uint32_t        embedding_idx;
    char            agent_id[MAX_AGENT_ID_LEN];
    char            session_id[MAX_SESSION_ID_LEN];
} node_meta_t;

/* Text content entry */
typedef struct {
    char*   text;
    size_t  len;
} text_entry_t;

struct hierarchy {
    char* base_dir;
    relations_store_t* relations;
    embeddings_store_t* embeddings;

    /* Node metadata array (parallel to relations) */
    node_meta_t* node_meta;
    size_t node_meta_capacity;

    /* Text content array (parallel to relations) */
    text_entry_t* text_content;
    size_t text_content_capacity;
};

/* Metadata file magic and version for validation */
#define METADATA_MAGIC 0x4D454D4F  /* 'MEMO' */
#define METADATA_VERSION 1

/* Save node metadata to file */
static mem_error_t save_metadata(hierarchy_t* h) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/metadata.dat", h->base_dir);

    FILE* f = fopen(path, "wb");
    if (!f) {
        MEM_RETURN_ERROR(MEM_ERR_IO, "failed to open metadata file for write");
    }

    /* Write header: magic, version, count */
    size_t count = relations_count(h->relations);
    uint32_t magic = METADATA_MAGIC;
    uint32_t version = METADATA_VERSION;
    uint32_t node_count = (uint32_t)count;

    if (fwrite(&magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&node_count, sizeof(node_count), 1, f) != 1) {
        fclose(f);
        MEM_RETURN_ERROR(MEM_ERR_IO, "failed to write metadata header");
    }

    /* Write node metadata entries */
    for (size_t i = 0; i < count && i < h->node_meta_capacity; i++) {
        if (fwrite(&h->node_meta[i], sizeof(node_meta_t), 1, f) != 1) {
            fclose(f);
            MEM_RETURN_ERROR(MEM_ERR_IO, "failed to write node metadata");
        }
    }

    fclose(f);
    return MEM_OK;
}

/* Load node metadata from file */
static mem_error_t load_metadata(hierarchy_t* h) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/metadata.dat", h->base_dir);

    FILE* f = fopen(path, "rb");
    if (!f) {
        /* File doesn't exist - not an error for fresh databases */
        return MEM_OK;
    }

    /* Read and validate header */
    uint32_t magic, version, node_count;
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&node_count, sizeof(node_count), 1, f) != 1) {
        fclose(f);
        MEM_RETURN_ERROR(MEM_ERR_IO, "failed to read metadata header");
    }

    if (magic != METADATA_MAGIC) {
        fclose(f);
        MEM_RETURN_ERROR(MEM_ERR_IO, "invalid metadata magic - file corrupted");
    }

    if (version != METADATA_VERSION) {
        fclose(f);
        MEM_RETURN_ERROR(MEM_ERR_IO, "unsupported metadata version");
    }

    /* Read node metadata entries */
    for (size_t i = 0; i < node_count && i < h->node_meta_capacity; i++) {
        if (fread(&h->node_meta[i], sizeof(node_meta_t), 1, f) != 1) {
            fclose(f);
            MEM_RETURN_ERROR(MEM_ERR_IO, "failed to read node metadata");
        }
    }

    fclose(f);
    LOG_INFO("Loaded metadata for %u nodes", node_count);
    return MEM_OK;
}

/* Ensure node metadata array has capacity */
static mem_error_t ensure_meta_capacity(hierarchy_t* h, size_t needed) {
    if (needed <= h->node_meta_capacity) {
        return MEM_OK;
    }

    size_t new_capacity = h->node_meta_capacity * 2;
    if (new_capacity < needed) new_capacity = needed;
    if (new_capacity < 1024) new_capacity = 1024;

    node_meta_t* new_meta = realloc(h->node_meta, new_capacity * sizeof(node_meta_t));
    if (!new_meta) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to grow node metadata array");
    }

    /* Zero-initialize new entries */
    memset(new_meta + h->node_meta_capacity, 0,
           (new_capacity - h->node_meta_capacity) * sizeof(node_meta_t));

    h->node_meta = new_meta;
    h->node_meta_capacity = new_capacity;

    return MEM_OK;
}

mem_error_t hierarchy_create(hierarchy_t** h, const char* dir, size_t capacity) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy ptr is NULL");
    MEM_CHECK_ERR(dir != NULL, MEM_ERR_INVALID_ARG, "dir is NULL");
    MEM_CHECK_ERR(capacity > 0, MEM_ERR_INVALID_ARG, "capacity must be > 0");

    hierarchy_t* hier = calloc(1, sizeof(hierarchy_t));
    MEM_CHECK_ALLOC(hier);

    hier->base_dir = strdup(dir);
    if (!hier->base_dir) {
        free(hier);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate dir path");
    }

    /* Create subdirectories */
    char path[PATH_MAX];
    mem_error_t err;

    snprintf(path, sizeof(path), "%s/relations", dir);
    err = relations_create(&hier->relations, path, capacity);
    if (err != MEM_OK) goto cleanup;

    snprintf(path, sizeof(path), "%s/embeddings", dir);
    err = embeddings_create(&hier->embeddings, path, capacity);
    if (err != MEM_OK) goto cleanup;

    /* Initialize node metadata */
    hier->node_meta = calloc(capacity, sizeof(node_meta_t));
    if (!hier->node_meta) {
        err = MEM_ERR_NOMEM;
        goto cleanup;
    }
    hier->node_meta_capacity = capacity;

    /* Initialize text content storage */
    hier->text_content = calloc(capacity, sizeof(text_entry_t));
    if (!hier->text_content) {
        err = MEM_ERR_NOMEM;
        goto cleanup;
    }
    hier->text_content_capacity = capacity;

    *h = hier;
    LOG_INFO("Hierarchy created at %s with capacity %zu", dir, capacity);
    return MEM_OK;

cleanup:
    if (hier->relations) relations_close(hier->relations);
    if (hier->embeddings) embeddings_close(hier->embeddings);
    free(hier->node_meta);
    free(hier->text_content);
    free(hier->base_dir);
    free(hier);
    return err;
}

mem_error_t hierarchy_open(hierarchy_t** h, const char* dir) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy ptr is NULL");
    MEM_CHECK_ERR(dir != NULL, MEM_ERR_INVALID_ARG, "dir is NULL");

    hierarchy_t* hier = calloc(1, sizeof(hierarchy_t));
    MEM_CHECK_ALLOC(hier);

    hier->base_dir = strdup(dir);
    if (!hier->base_dir) {
        free(hier);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate dir path");
    }

    char path[PATH_MAX];
    mem_error_t err;

    snprintf(path, sizeof(path), "%s/relations", dir);
    err = relations_open(&hier->relations, path);
    if (err != MEM_OK) goto cleanup;

    snprintf(path, sizeof(path), "%s/embeddings", dir);
    err = embeddings_open(&hier->embeddings, path);
    if (err != MEM_OK) goto cleanup;

    /* Initialize node metadata array based on existing node count */
    size_t count = relations_count(hier->relations);
    size_t capacity = count > 0 ? count * 2 : 1024;

    hier->node_meta = calloc(capacity, sizeof(node_meta_t));
    if (!hier->node_meta) {
        err = MEM_ERR_NOMEM;
        goto cleanup;
    }
    hier->node_meta_capacity = capacity;

    /* Initialize text content storage */
    hier->text_content = calloc(capacity, sizeof(text_entry_t));
    if (!hier->text_content) {
        err = MEM_ERR_NOMEM;
        goto cleanup;
    }
    hier->text_content_capacity = capacity;

    /* Load persisted metadata */
    err = load_metadata(hier);
    if (err != MEM_OK) goto cleanup;

    *h = hier;
    LOG_INFO("Hierarchy opened at %s with %zu nodes", dir, count);
    return MEM_OK;

cleanup:
    if (hier->relations) relations_close(hier->relations);
    if (hier->embeddings) embeddings_close(hier->embeddings);
    free(hier->node_meta);
    free(hier->text_content);
    free(hier->base_dir);
    free(hier);
    return err;
}

void hierarchy_close(hierarchy_t* h) {
    if (!h) return;

    hierarchy_sync(h);

    if (h->relations) relations_close(h->relations);
    if (h->embeddings) embeddings_close(h->embeddings);

    /* Free text content */
    if (h->text_content) {
        for (size_t i = 0; i < h->text_content_capacity; i++) {
            free(h->text_content[i].text);
        }
        free(h->text_content);
    }

    free(h->node_meta);
    free(h->base_dir);
    free(h);
}

mem_error_t hierarchy_sync(hierarchy_t* h) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");

    MEM_CHECK(relations_sync(h->relations));
    MEM_CHECK(embeddings_sync(h->embeddings));
    MEM_CHECK(save_metadata(h));

    return MEM_OK;
}

/* Internal: Create a node at specified level under parent */
static mem_error_t create_node_internal(hierarchy_t* h,
                                        node_id_t parent_id,
                                        hierarchy_level_t level,
                                        const char* agent_id,
                                        const char* session_id,
                                        node_id_t* out_id) {
    /* Allocate node in relations store */
    node_id_t id;
    MEM_CHECK(relations_alloc_node(h->relations, &id));

    /* Ensure metadata capacity */
    MEM_CHECK(ensure_meta_capacity(h, id + 1));

    /* Set level */
    MEM_CHECK(relations_set_level(h->relations, id, level));

    /* Set parent relationship */
    if (parent_id != NODE_ID_INVALID) {
        MEM_CHECK(relations_set_parent(h->relations, id, parent_id));

        /* Link as child of parent */
        node_id_t first_child = relations_get_first_child(h->relations, parent_id);
        if (first_child == NODE_ID_INVALID) {
            /* First child */
            MEM_CHECK(relations_set_first_child(h->relations, parent_id, id));
        } else {
            /* Find last sibling and link */
            node_id_t last = first_child;
            node_id_t next = relations_get_next_sibling(h->relations, last);
            while (next != NODE_ID_INVALID) {
                last = next;
                next = relations_get_next_sibling(h->relations, last);
            }
            MEM_CHECK(relations_set_next_sibling(h->relations, last, id));
        }
    }

    /* Allocate embedding slot */
    uint32_t emb_idx;
    MEM_CHECK(embeddings_alloc(h->embeddings, level, &emb_idx));

    /* Store metadata */
    node_meta_t* meta = &h->node_meta[id];
    meta->created_at = timestamp_now_ns();
    meta->embedding_idx = emb_idx;

    if (agent_id) {
        snprintf(meta->agent_id, MAX_AGENT_ID_LEN, "%s", agent_id);
        meta->agent_id[MAX_AGENT_ID_LEN - 1] = '\0';
    }

    if (session_id) {
        snprintf(meta->session_id, MAX_SESSION_ID_LEN, "%s", session_id);
        meta->session_id[MAX_SESSION_ID_LEN - 1] = '\0';
    }

    *out_id = id;
    return MEM_OK;
}

/* Find existing agent by agent_id string */
static node_id_t find_agent_by_id(const hierarchy_t* h, const char* agent_id) {
    if (!h || !agent_id) return NODE_ID_INVALID;

    size_t count = relations_count(h->relations);
    for (node_id_t id = 0; id < count; id++) {
        hierarchy_level_t level = relations_get_level(h->relations, id);
        if (level == LEVEL_AGENT && id < h->node_meta_capacity) {
            if (strcmp(h->node_meta[id].agent_id, agent_id) == 0) {
                return id;
            }
        }
    }
    return NODE_ID_INVALID;
}

/* Find existing session by session_id string under a specific agent */
static node_id_t find_session_by_id(const hierarchy_t* h, node_id_t agent_node_id,
                                    const char* session_id) {
    if (!h || !session_id) return NODE_ID_INVALID;

    /* Iterate children of agent to find matching session */
    node_id_t child = relations_get_first_child(h->relations, agent_node_id);
    while (child != NODE_ID_INVALID) {
        hierarchy_level_t level = relations_get_level(h->relations, child);
        if (level == LEVEL_SESSION && child < h->node_meta_capacity) {
            if (strcmp(h->node_meta[child].session_id, session_id) == 0) {
                return child;
            }
        }
        child = relations_get_next_sibling(h->relations, child);
    }
    return NODE_ID_INVALID;
}

mem_error_t hierarchy_create_agent(hierarchy_t* h,
                                   const char* agent_id,
                                   node_id_t* out_id) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");
    MEM_CHECK_ERR(agent_id != NULL, MEM_ERR_INVALID_ARG, "agent_id is NULL");
    MEM_CHECK_ERR(out_id != NULL, MEM_ERR_INVALID_ARG, "out_id is NULL");

    /* Check if agent already exists */
    node_id_t existing = find_agent_by_id(h, agent_id);
    if (existing != NODE_ID_INVALID) {
        *out_id = existing;
        return MEM_ERR_EXISTS;
    }

    return create_node_internal(h, NODE_ID_INVALID, LEVEL_AGENT,
                               agent_id, NULL, out_id);
}

mem_error_t hierarchy_create_session(hierarchy_t* h,
                                     node_id_t agent_node_id,
                                     const char* session_id,
                                     node_id_t* out_id) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");
    MEM_CHECK_ERR(session_id != NULL, MEM_ERR_INVALID_ARG, "session_id is NULL");
    MEM_CHECK_ERR(out_id != NULL, MEM_ERR_INVALID_ARG, "out_id is NULL");

    /* Verify parent is an agent */
    hierarchy_level_t parent_level = relations_get_level(h->relations, agent_node_id);
    if (parent_level != LEVEL_AGENT) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_LEVEL,
                        "session parent must be agent, got level %d", parent_level);
    }

    /* Check if session already exists under this agent */
    node_id_t existing = find_session_by_id(h, agent_node_id, session_id);
    if (existing != NODE_ID_INVALID) {
        *out_id = existing;
        return MEM_ERR_EXISTS;
    }

    /* Get agent_id from parent for inheritance */
    const char* agent_id_str = NULL;
    if (agent_node_id < h->node_meta_capacity) {
        agent_id_str = h->node_meta[agent_node_id].agent_id;
    }

    return create_node_internal(h, agent_node_id, LEVEL_SESSION,
                               agent_id_str, session_id, out_id);
}

mem_error_t hierarchy_create_child(hierarchy_t* h,
                                   node_id_t parent_id,
                                   hierarchy_level_t level,
                                   node_id_t* out_id) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");
    MEM_CHECK_ERR(parent_id != NODE_ID_INVALID, MEM_ERR_INVALID_ARG, "invalid parent");
    MEM_CHECK_ERR(level < LEVEL_COUNT, MEM_ERR_INVALID_LEVEL, "invalid level");
    MEM_CHECK_ERR(out_id != NULL, MEM_ERR_INVALID_ARG, "out_id is NULL");

    /* Validate level hierarchy */
    hierarchy_level_t parent_level = relations_get_level(h->relations, parent_id);
    if (level >= parent_level) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_LEVEL,
                        "child level %d must be < parent level %d", level, parent_level);
    }

    /* Inherit session info from parent */
    const char* agent_id = NULL;
    const char* session_id = NULL;
    if (parent_id < h->node_meta_capacity) {
        agent_id = h->node_meta[parent_id].agent_id;
        session_id = h->node_meta[parent_id].session_id;
    }

    return create_node_internal(h, parent_id, level, agent_id, session_id, out_id);
}

mem_error_t hierarchy_create_message(hierarchy_t* h,
                                     node_id_t session_id,
                                     node_id_t* out_id) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");

    /* Verify parent is a session */
    hierarchy_level_t parent_level = relations_get_level(h->relations, session_id);
    if (parent_level != LEVEL_SESSION) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_LEVEL,
                        "message parent must be session, got level %d", parent_level);
    }

    return hierarchy_create_child(h, session_id, LEVEL_MESSAGE, out_id);
}

mem_error_t hierarchy_create_block(hierarchy_t* h,
                                   node_id_t message_id,
                                   node_id_t* out_id) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");

    /* Verify parent is a message */
    hierarchy_level_t parent_level = relations_get_level(h->relations, message_id);
    if (parent_level != LEVEL_MESSAGE) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_LEVEL,
                        "block parent must be message, got level %d", parent_level);
    }

    return hierarchy_create_child(h, message_id, LEVEL_BLOCK, out_id);
}

mem_error_t hierarchy_create_statement(hierarchy_t* h,
                                       node_id_t block_id,
                                       node_id_t* out_id) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");

    /* Verify parent is a block */
    hierarchy_level_t parent_level = relations_get_level(h->relations, block_id);
    if (parent_level != LEVEL_BLOCK) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_LEVEL,
                        "statement parent must be block, got level %d", parent_level);
    }

    return hierarchy_create_child(h, block_id, LEVEL_STATEMENT, out_id);
}

mem_error_t hierarchy_get_node(const hierarchy_t* h, node_id_t id, node_info_t* info) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");
    MEM_CHECK_ERR(info != NULL, MEM_ERR_INVALID_ARG, "info is NULL");

    size_t count = relations_count(h->relations);
    if (id >= count) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "node %u not found", id);
    }

    info->id = id;
    info->level = relations_get_level(h->relations, id);
    info->parent_id = relations_get_parent(h->relations, id);
    info->first_child_id = relations_get_first_child(h->relations, id);
    info->next_sibling_id = relations_get_next_sibling(h->relations, id);

    if (id < h->node_meta_capacity) {
        info->created_at = h->node_meta[id].created_at;
        info->embedding_idx = h->node_meta[id].embedding_idx;
        snprintf(info->agent_id, MAX_AGENT_ID_LEN, "%s", h->node_meta[id].agent_id);
        snprintf(info->session_id, MAX_SESSION_ID_LEN, "%s", h->node_meta[id].session_id);
    } else {
        info->created_at = 0;
        info->embedding_idx = 0;
        info->agent_id[0] = '\0';
        info->session_id[0] = '\0';
    }

    return MEM_OK;
}

node_id_t hierarchy_get_parent(const hierarchy_t* h, node_id_t id) {
    if (!h) return NODE_ID_INVALID;
    return relations_get_parent(h->relations, id);
}

node_id_t hierarchy_get_first_child(const hierarchy_t* h, node_id_t id) {
    if (!h) return NODE_ID_INVALID;
    return relations_get_first_child(h->relations, id);
}

node_id_t hierarchy_get_next_sibling(const hierarchy_t* h, node_id_t id) {
    if (!h) return NODE_ID_INVALID;
    return relations_get_next_sibling(h->relations, id);
}

hierarchy_level_t hierarchy_get_level(const hierarchy_t* h, node_id_t id) {
    if (!h) return LEVEL_STATEMENT;
    return relations_get_level(h->relations, id);
}

size_t hierarchy_get_children(const hierarchy_t* h, node_id_t id,
                              node_id_t* children, size_t max_count) {
    if (!h) return 0;
    return relations_get_children(h->relations, id, children, max_count);
}

size_t hierarchy_get_siblings(const hierarchy_t* h, node_id_t id,
                              node_id_t* siblings, size_t max_count) {
    if (!h) return 0;
    return relations_get_siblings(h->relations, id, siblings, max_count);
}

size_t hierarchy_get_ancestors(const hierarchy_t* h, node_id_t id,
                               node_id_t* ancestors, size_t max_count) {
    if (!h) return 0;
    return relations_get_ancestors(h->relations, id, ancestors, max_count);
}

size_t hierarchy_count_descendants(const hierarchy_t* h, node_id_t id) {
    if (!h) return 0;
    return relations_count_descendants(h->relations, id);
}

size_t hierarchy_count(const hierarchy_t* h) {
    if (!h) return 0;
    return relations_count(h->relations);
}

mem_error_t hierarchy_set_embedding(hierarchy_t* h, node_id_t id,
                                    const float* values) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");
    MEM_CHECK_ERR(values != NULL, MEM_ERR_INVALID_ARG, "values is NULL");

    size_t count = relations_count(h->relations);
    if (id >= count) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "node %u not found", id);
    }

    if (id >= h->node_meta_capacity) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "node %u metadata not found", id);
    }

    hierarchy_level_t level = relations_get_level(h->relations, id);
    uint32_t emb_idx = h->node_meta[id].embedding_idx;

    return embeddings_set(h->embeddings, level, emb_idx, values);
}

const float* hierarchy_get_embedding(const hierarchy_t* h, node_id_t id) {
    if (!h) return NULL;

    size_t count = relations_count(h->relations);
    if (id >= count) return NULL;

    if (id >= h->node_meta_capacity) return NULL;

    hierarchy_level_t level = relations_get_level(h->relations, id);
    uint32_t emb_idx = h->node_meta[id].embedding_idx;

    return embeddings_get(h->embeddings, level, emb_idx);
}

float hierarchy_similarity(const hierarchy_t* h, node_id_t id1, node_id_t id2) {
    if (!h) return 0.0f;

    size_t count = relations_count(h->relations);
    if (id1 >= count || id2 >= count) return 0.0f;

    if (id1 >= h->node_meta_capacity || id2 >= h->node_meta_capacity) return 0.0f;

    hierarchy_level_t level1 = relations_get_level(h->relations, id1);
    hierarchy_level_t level2 = relations_get_level(h->relations, id2);

    /* Can only compare nodes at same level */
    if (level1 != level2) return 0.0f;

    uint32_t idx1 = h->node_meta[id1].embedding_idx;
    uint32_t idx2 = h->node_meta[id2].embedding_idx;

    return embeddings_similarity(h->embeddings, level1, idx1, idx2);
}

relations_store_t* hierarchy_get_relations(hierarchy_t* h) {
    return h ? h->relations : NULL;
}

embeddings_store_t* hierarchy_get_embeddings(hierarchy_t* h) {
    return h ? h->embeddings : NULL;
}

/* Ensure text content array has capacity */
static mem_error_t ensure_text_capacity(hierarchy_t* h, size_t needed) {
    if (needed <= h->text_content_capacity) {
        return MEM_OK;
    }

    size_t new_capacity = h->text_content_capacity * 2;
    if (new_capacity < needed) new_capacity = needed;
    if (new_capacity < 1024) new_capacity = 1024;

    text_entry_t* new_text = realloc(h->text_content, new_capacity * sizeof(text_entry_t));
    if (!new_text) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to grow text content array");
    }

    /* Zero-initialize new entries */
    memset(new_text + h->text_content_capacity, 0,
           (new_capacity - h->text_content_capacity) * sizeof(text_entry_t));

    h->text_content = new_text;
    h->text_content_capacity = new_capacity;

    return MEM_OK;
}

mem_error_t hierarchy_set_text(hierarchy_t* h, node_id_t id,
                               const char* text, size_t len) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");
    MEM_CHECK_ERR(text != NULL, MEM_ERR_INVALID_ARG, "text is NULL");

    size_t count = relations_count(h->relations);
    if (id >= count) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "node %u not found", id);
    }

    /* Ensure capacity */
    MEM_CHECK(ensure_text_capacity(h, id + 1));

    /* Free old text if present */
    free(h->text_content[id].text);

    /* Allocate and copy new text */
    h->text_content[id].text = malloc(len + 1);
    if (!h->text_content[id].text) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate text");
    }

    memcpy(h->text_content[id].text, text, len);
    h->text_content[id].text[len] = '\0';
    h->text_content[id].len = len;

    return MEM_OK;
}

const char* hierarchy_get_text(const hierarchy_t* h, node_id_t id, size_t* len) {
    if (!h) return NULL;

    size_t count = relations_count(h->relations);
    if (id >= count) return NULL;

    if (id >= h->text_content_capacity) return NULL;

    if (len) {
        *len = h->text_content[id].len;
    }

    return h->text_content[id].text;
}

size_t hierarchy_iter_sessions(const hierarchy_t* h, session_iter_fn callback, void* user_data) {
    if (!h || !callback) return 0;

    size_t count = relations_count(h->relations);
    size_t session_count = 0;

    for (node_id_t id = 0; id < count; id++) {
        hierarchy_level_t level = relations_get_level(h->relations, id);
        if (level == LEVEL_SESSION) {
            const char* agent_id = "";
            const char* session_str = "";

            if (id < h->node_meta_capacity) {
                agent_id = h->node_meta[id].agent_id;
                session_str = h->node_meta[id].session_id;
            }

            session_count++;
            if (!callback(id, agent_id, session_str, user_data)) {
                break;  /* Callback requested stop */
            }
        }
    }

    return session_count;
}
