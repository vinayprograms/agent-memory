/*
 * Memory Service - Hierarchical Embedding Pooling
 *
 * Handles the aggregation of embeddings from child nodes to parents:
 * - Block embedding = mean_pool(statement embeddings)
 * - Message embedding = mean_pool(block embeddings)
 * - Session embedding = mean_pool(message embeddings)
 */

#ifndef MEMORY_SERVICE_POOLING_H
#define MEMORY_SERVICE_POOLING_H

#include "../../include/types.h"
#include "../../include/error.h"
#include "../core/hierarchy.h"
#include "embedding.h"

/*
 * Pool child embeddings into parent embedding
 *
 * Retrieves all child embeddings for a node and computes
 * the mean-pooled embedding, storing it in the parent.
 *
 * @param h         The hierarchy
 * @param parent_id The parent node to update
 * @return MEM_OK if at least one child exists, error otherwise
 */
mem_error_t pooling_aggregate_children(hierarchy_t* h, node_id_t parent_id);

/*
 * Propagate embeddings up the hierarchy
 *
 * After generating statement-level embeddings, call this to
 * aggregate all embeddings up to the session level.
 *
 * For each node from the bottom up:
 * - If node has children: pool child embeddings
 * - If node is leaf: keep existing embedding
 *
 * @param h          The hierarchy
 * @param session_id The session to propagate
 * @return MEM_OK on success
 */
mem_error_t pooling_propagate_session(hierarchy_t* h, node_id_t session_id);

/*
 * Generate embeddings for a message and all descendants
 *
 * 1. Generates embeddings for all statements using the engine
 * 2. Pools statement embeddings into block embeddings
 * 3. Pools block embeddings into message embedding
 *
 * @param engine     Embedding engine
 * @param h          The hierarchy
 * @param message_id The message to process
 * @param texts      Array of text for each statement (parallel to statement IDs)
 * @param text_count Number of statements
 * @return MEM_OK on success
 */
mem_error_t pooling_embed_message(embedding_engine_t* engine,
                                  hierarchy_t* h,
                                  node_id_t message_id,
                                  const char** texts,
                                  const size_t* text_lens,
                                  size_t text_count);

#endif /* MEMORY_SERVICE_POOLING_H */
