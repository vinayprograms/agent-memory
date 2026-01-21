/*
 * Memory Service - Hierarchical Embedding Pooling Implementation
 */

#include "pooling.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>

/* Maximum children per node for pooling */
#define MAX_CHILDREN_POOL 256

mem_error_t pooling_aggregate_children(hierarchy_t* h, node_id_t parent_id) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");

    /* Get all children */
    node_id_t children[MAX_CHILDREN_POOL];
    size_t count = hierarchy_get_children(h, parent_id, children, MAX_CHILDREN_POOL);

    if (count == 0) {
        /* No children to pool - keep existing embedding or return error */
        return MEM_OK;  /* Not an error, just nothing to do */
    }

    /* Collect child embeddings */
    const float* child_embeddings[MAX_CHILDREN_POOL];
    size_t valid_count = 0;

    for (size_t i = 0; i < count; i++) {
        const float* emb = hierarchy_get_embedding(h, children[i]);
        if (emb) {
            child_embeddings[valid_count++] = emb;
        }
    }

    if (valid_count == 0) {
        /* No valid child embeddings */
        return MEM_OK;
    }

    /* Pool embeddings */
    float pooled[EMBEDDING_DIM];
    embedding_mean_pool(child_embeddings, valid_count, pooled);

    /* Store in parent */
    MEM_CHECK(hierarchy_set_embedding(h, parent_id, pooled));

    return MEM_OK;
}

/* Recursive helper to propagate embeddings bottom-up */
static mem_error_t propagate_node(hierarchy_t* h, node_id_t node_id) {
    /* First, process all children recursively */
    node_id_t children[MAX_CHILDREN_POOL];
    size_t count = hierarchy_get_children(h, node_id, children, MAX_CHILDREN_POOL);

    for (size_t i = 0; i < count; i++) {
        MEM_CHECK(propagate_node(h, children[i]));
    }

    /* Then, if we have children, aggregate their embeddings */
    if (count > 0) {
        MEM_CHECK(pooling_aggregate_children(h, node_id));
    }

    return MEM_OK;
}

mem_error_t pooling_propagate_session(hierarchy_t* h, node_id_t session_id) {
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");

    /* Verify it's a session */
    hierarchy_level_t level = hierarchy_get_level(h, session_id);
    if (level != LEVEL_SESSION) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_LEVEL,
                        "expected session level, got %d", level);
    }

    return propagate_node(h, session_id);
}

mem_error_t pooling_embed_message(embedding_engine_t* engine,
                                  hierarchy_t* h,
                                  node_id_t message_id,
                                  const char** texts,
                                  const size_t* text_lens,
                                  size_t text_count) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");
    MEM_CHECK_ERR(h != NULL, MEM_ERR_INVALID_ARG, "hierarchy is NULL");

    /* Verify it's a message */
    hierarchy_level_t level = hierarchy_get_level(h, message_id);
    if (level != LEVEL_MESSAGE) {
        MEM_RETURN_ERROR(MEM_ERR_INVALID_LEVEL,
                        "expected message level, got %d", level);
    }

    /* Get all blocks under this message */
    node_id_t blocks[MAX_CHILDREN_POOL];
    size_t block_count = hierarchy_get_children(h, message_id, blocks, MAX_CHILDREN_POOL);

    if (block_count == 0 || text_count == 0) {
        /* No content to embed - create zero embedding */
        float zero_emb[EMBEDDING_DIM] = {0};
        return hierarchy_set_embedding(h, message_id, zero_emb);
    }

    /* Collect all statements across all blocks */
    node_id_t all_statements[MAX_CHILDREN_POOL * MAX_CHILDREN_POOL];
    size_t statement_count = 0;

    for (size_t b = 0; b < block_count && statement_count < text_count; b++) {
        node_id_t stmts[MAX_CHILDREN_POOL];
        size_t stmt_count = hierarchy_get_children(h, blocks[b], stmts, MAX_CHILDREN_POOL);

        for (size_t s = 0; s < stmt_count && statement_count < text_count; s++) {
            all_statements[statement_count++] = stmts[s];
        }
    }

    /* Clamp to actual text count */
    if (statement_count > text_count) {
        statement_count = text_count;
    }

    /* Generate embeddings for all statements */
    float* stmt_embeddings = malloc(statement_count * EMBEDDING_DIM * sizeof(float));
    if (!stmt_embeddings) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate embedding buffer");
    }

    mem_error_t err = embedding_generate_batch(engine, texts, text_lens,
                                               statement_count, stmt_embeddings);
    if (err != MEM_OK) {
        free(stmt_embeddings);
        return err;
    }

    /* Store statement embeddings */
    for (size_t i = 0; i < statement_count; i++) {
        err = hierarchy_set_embedding(h, all_statements[i],
                                     stmt_embeddings + i * EMBEDDING_DIM);
        if (err != MEM_OK) {
            free(stmt_embeddings);
            return err;
        }
    }

    free(stmt_embeddings);

    /* Pool block embeddings from statements */
    for (size_t b = 0; b < block_count; b++) {
        err = pooling_aggregate_children(h, blocks[b]);
        if (err != MEM_OK) return err;
    }

    /* Pool message embedding from blocks */
    err = pooling_aggregate_children(h, message_id);

    return err;
}
