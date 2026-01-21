/*
 * Memory Service - HNSW Index Implementation
 *
 * Hierarchical Navigable Small World graph for approximate
 * nearest neighbor search on embedding vectors.
 *
 * Algorithm based on: "Efficient and robust approximate nearest
 * neighbor search using Hierarchical Navigable Small World graphs"
 * by Malkov and Yashunin (2016)
 */

#include "hnsw.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* Maximum number of layers (log scale, 16 layers = 2^16 elements) */
#define MAX_LAYERS 16

/* Node in the HNSW graph */
typedef struct hnsw_node {
    node_id_t id;
    float vector[EMBEDDING_DIM];
    int top_layer;            /* Highest layer this node exists in */
    bool deleted;             /* Soft delete flag */

    /* Neighbors per layer (variable size based on layer) */
    /* Layer 0 has M * 2 neighbors, other layers have M neighbors */
    node_id_t** neighbors;    /* neighbors[layer] = array of neighbor indices */
    size_t* neighbor_counts;  /* Number of neighbors per layer */
} hnsw_node_t;

/* Priority queue element for search */
typedef struct {
    size_t node_idx;
    float distance;
} pq_elem_t;

/* Min-heap priority queue */
typedef struct {
    pq_elem_t* data;
    size_t size;
    size_t capacity;
} pq_t;

/* HNSW index structure */
struct hnsw_index {
    hnsw_config_t config;

    /* Nodes array */
    hnsw_node_t* nodes;
    size_t node_count;
    size_t node_capacity;

    /* Entry point (highest layer node) */
    size_t entry_point;
    int max_layer;

    /* ID to index mapping */
    node_id_t* id_to_idx;     /* Sparse array: id_to_idx[id] = node index */
    size_t id_map_size;

    /* Random state for layer selection */
    uint32_t rand_state;

    /* Level multiplier for layer assignment */
    float level_mult;
};

/* ========== Priority Queue Implementation ========== */

static bool pq_init(pq_t* pq, size_t capacity) {
    pq->data = malloc(capacity * sizeof(pq_elem_t));
    pq->size = 0;
    pq->capacity = capacity;
    return pq->data != NULL;
}

static void pq_destroy(pq_t* pq) {
    free(pq->data);
    pq->data = NULL;
    pq->size = 0;
}

static bool pq_push(pq_t* pq, size_t node_idx, float distance) {
    if (pq->size >= pq->capacity) {
        size_t new_capacity = pq->capacity * 2;
        pq_elem_t* new_data = realloc(pq->data, new_capacity * sizeof(pq_elem_t));
        if (!new_data) {
            return false;  /* Allocation failed, caller should handle */
        }
        pq->data = new_data;
        pq->capacity = new_capacity;
    }

    /* Add at end */
    size_t i = pq->size++;
    pq->data[i].node_idx = node_idx;
    pq->data[i].distance = distance;

    /* Bubble up (min-heap) */
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (pq->data[parent].distance <= pq->data[i].distance) break;

        pq_elem_t tmp = pq->data[parent];
        pq->data[parent] = pq->data[i];
        pq->data[i] = tmp;
        i = parent;
    }
    return true;
}

static pq_elem_t pq_pop(pq_t* pq) {
    /* Return invalid element if queue is empty */
    if (pq->size == 0) {
        pq_elem_t empty = { .node_idx = SIZE_MAX, .distance = FLT_MAX };
        return empty;
    }

    pq_elem_t result = pq->data[0];

    /* Move last to root */
    pq->data[0] = pq->data[--pq->size];

    /* Bubble down */
    size_t i = 0;
    while (true) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t smallest = i;

        if (left < pq->size && pq->data[left].distance < pq->data[smallest].distance) {
            smallest = left;
        }
        if (right < pq->size && pq->data[right].distance < pq->data[smallest].distance) {
            smallest = right;
        }

        if (smallest == i) break;

        pq_elem_t tmp = pq->data[i];
        pq->data[i] = pq->data[smallest];
        pq->data[smallest] = tmp;
        i = smallest;
    }

    return result;
}

static bool pq_empty(const pq_t* pq) {
    return pq->size == 0;
}

/* ========== Distance Functions ========== */

/* Compute distance (1 - cosine_similarity) for normalized vectors */
static float compute_distance(const float* a, const float* b) {
    float dot = 0.0f;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        dot += a[i] * b[i];
    }
    /* For normalized vectors: distance = 1 - cos_sim */
    return 1.0f - dot;
}

/* ========== Random Layer Selection ========== */

static uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int random_layer(hnsw_index_t* idx) {
    float r = (float)xorshift32(&idx->rand_state) / (float)UINT32_MAX;
    return (int)(-log(r) * idx->level_mult);
}

/* ========== Core HNSW Operations ========== */

/* Search layer for nearest neighbors */
static void search_layer(hnsw_index_t* idx, const float* query, size_t entry,
                         int layer, size_t ef, pq_t* result) {
    /* Bounds check on entry parameter to prevent out-of-bounds access */
    if (entry >= idx->node_count) {
        return;
    }

    /* Early exit if entry doesn't exist at this layer */
    if (layer > idx->nodes[entry].top_layer) {
        return;
    }

    /* Visited set (bitmap for efficiency) */
    size_t visited_size = (idx->node_count + 63) / 64;
    if (visited_size == 0) visited_size = 1;
    uint64_t* visited = calloc(visited_size, sizeof(uint64_t));
    if (!visited) return;

    /* Mark entry as visited */
    visited[entry / 64] |= (1ULL << (entry % 64));

    /* Candidate queue (min-heap by distance) */
    pq_t candidates;
    if (!pq_init(&candidates, ef * 2)) {
        free(visited);
        return;
    }

    float entry_dist = compute_distance(query, idx->nodes[entry].vector);
    pq_push(&candidates, entry, entry_dist);
    pq_push(result, entry, entry_dist);

    float worst_dist = entry_dist;

    while (!pq_empty(&candidates)) {
        pq_elem_t curr = pq_pop(&candidates);

        /* If current is worse than worst result, stop */
        if (curr.distance > worst_dist && result->size >= ef) {
            break;
        }

        hnsw_node_t* node = &idx->nodes[curr.node_idx];
        if (node->deleted || layer > node->top_layer) continue;

        /* Explore neighbors at this layer */
        if (!node->neighbors || !node->neighbor_counts) continue;
        if (layer > node->top_layer) continue;

        size_t neighbor_count = node->neighbor_counts[layer];
        node_id_t* neighbors = node->neighbors[layer];
        if (!neighbors) continue;

        for (size_t i = 0; i < neighbor_count; i++) {
            size_t neighbor_idx = neighbors[i];
            if (neighbor_idx >= idx->node_count) continue;

            /* Check if visited */
            if (visited[neighbor_idx / 64] & (1ULL << (neighbor_idx % 64))) {
                continue;
            }
            visited[neighbor_idx / 64] |= (1ULL << (neighbor_idx % 64));

            hnsw_node_t* neighbor = &idx->nodes[neighbor_idx];
            if (neighbor->deleted) continue;

            float dist = compute_distance(query, neighbor->vector);

            if (result->size < ef || dist < worst_dist) {
                pq_push(&candidates, neighbor_idx, dist);
                pq_push(result, neighbor_idx, dist);

                /* Maintain result size at ef */
                if (result->size > ef) {
                    /* Find and remove worst (we need max-heap behavior for results) */
                    /* For simplicity, just track worst distance */
                }

                if (dist > worst_dist) {
                    worst_dist = dist;
                }
            }
        }
    }

    pq_destroy(&candidates);
    free(visited);
}

/* Select M best neighbors from candidates */
static void select_neighbors(hnsw_index_t* idx, size_t node_idx, pq_t* candidates,
                            int layer, size_t M, node_id_t* out, size_t* out_count) {
    (void)layer;  /* Could be used for layer-specific selection heuristics */

    /* Simple heuristic: take M closest */
    *out_count = 0;

    if (!candidates || candidates->size == 0) {
        return;
    }

    /* Sort by distance (extract all, sort, take M) */
    size_t n = candidates->size;
    pq_elem_t* sorted = malloc(n * sizeof(pq_elem_t));
    if (!sorted) return;

    size_t sorted_count = 0;

    while (!pq_empty(candidates) && sorted_count < n) {
        sorted[sorted_count++] = pq_pop(candidates);
    }

    /* Take up to M closest */
    for (size_t i = 0; i < sorted_count && *out_count < M; i++) {
        size_t neighbor_idx = sorted[i].node_idx;
        if (neighbor_idx < idx->node_count &&
            neighbor_idx != node_idx &&
            !idx->nodes[neighbor_idx].deleted) {
            out[(*out_count)++] = (node_id_t)neighbor_idx;
        }
    }

    free(sorted);
}

/* Add bidirectional connection */
static void add_connection(hnsw_index_t* idx, size_t from_idx, size_t to_idx, int layer) {
    if (from_idx >= idx->node_count || to_idx >= idx->node_count) return;

    hnsw_node_t* from_node = &idx->nodes[from_idx];
    if (!from_node->neighbors || layer > from_node->top_layer) return;
    if (!from_node->neighbors[layer]) return;

    size_t max_neighbors = (layer == 0) ? idx->config.M * 2 : idx->config.M;

    /* Check if already connected */
    for (size_t i = 0; i < from_node->neighbor_counts[layer]; i++) {
        if (from_node->neighbors[layer][i] == to_idx) return;
    }

    /* Add connection */
    if (from_node->neighbor_counts[layer] < max_neighbors) {
        from_node->neighbors[layer][from_node->neighbor_counts[layer]++] = (node_id_t)to_idx;
    } else {
        /* Need to prune - replace worst neighbor if new one is better */
        /* For simplicity, find the farthest neighbor and replace */
        float worst_dist = 0;
        size_t worst_idx = 0;

        for (size_t i = 0; i < from_node->neighbor_counts[layer]; i++) {
            size_t neighbor_idx = from_node->neighbors[layer][i];
            float dist = compute_distance(from_node->vector, idx->nodes[neighbor_idx].vector);
            if (dist > worst_dist) {
                worst_dist = dist;
                worst_idx = i;
            }
        }

        float new_dist = compute_distance(from_node->vector, idx->nodes[to_idx].vector);
        if (new_dist < worst_dist) {
            from_node->neighbors[layer][worst_idx] = (node_id_t)to_idx;
        }
    }
}

/* ========== Public API ========== */

mem_error_t hnsw_create(hnsw_index_t** index, const hnsw_config_t* config) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index pointer is NULL");

    hnsw_index_t* idx = calloc(1, sizeof(hnsw_index_t));
    if (!idx) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate HNSW index");
    }

    /* Use default config if not provided */
    if (config) {
        idx->config = *config;
    } else {
        idx->config = (hnsw_config_t)HNSW_CONFIG_DEFAULT;
    }

    /* Allocate nodes array */
    idx->node_capacity = 1024;
    idx->nodes = calloc(idx->node_capacity, sizeof(hnsw_node_t));
    if (!idx->nodes) {
        free(idx);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate nodes");
    }

    idx->node_count = 0;
    idx->entry_point = 0;
    idx->max_layer = -1;

    /* ID mapping */
    idx->id_map_size = idx->config.max_elements;
    idx->id_to_idx = malloc(idx->id_map_size * sizeof(node_id_t));
    if (!idx->id_to_idx) {
        free(idx->nodes);
        free(idx);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate ID map");
    }
    for (size_t i = 0; i < idx->id_map_size; i++) {
        idx->id_to_idx[i] = NODE_ID_INVALID;
    }

    /* Random state */
    idx->rand_state = 12345;  /* Fixed seed for reproducibility */

    /* Level multiplier: 1/ln(M) */
    idx->level_mult = 1.0f / logf((float)idx->config.M);

    *index = idx;
    return MEM_OK;
}

void hnsw_destroy(hnsw_index_t* index) {
    if (!index) return;

    /* Free each node's neighbor lists */
    for (size_t i = 0; i < index->node_count; i++) {
        hnsw_node_t* node = &index->nodes[i];
        if (node->neighbors) {
            for (int layer = 0; layer <= node->top_layer; layer++) {
                free(node->neighbors[layer]);
            }
            free(node->neighbors);
            free(node->neighbor_counts);
        }
    }

    free(index->nodes);
    free(index->id_to_idx);
    free(index);
}

mem_error_t hnsw_add(hnsw_index_t* index, node_id_t id, const float* vector) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index is NULL");
    MEM_CHECK_ERR(vector != NULL, MEM_ERR_INVALID_ARG, "vector is NULL");

    /* Check capacity */
    if (index->node_count >= index->config.max_elements) {
        MEM_RETURN_ERROR(MEM_ERR_FULL, "HNSW index is full");
    }

    /* Check if ID already exists */
    if (id < index->id_map_size && index->id_to_idx[id] != NODE_ID_INVALID) {
        MEM_RETURN_ERROR(MEM_ERR_EXISTS, "ID %u already in index", id);
    }

    /* Expand nodes array if needed */
    if (index->node_count >= index->node_capacity) {
        size_t new_cap = index->node_capacity * 2;
        hnsw_node_t* new_nodes = realloc(index->nodes, new_cap * sizeof(hnsw_node_t));
        if (!new_nodes) {
            MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to expand nodes");
        }
        memset(new_nodes + index->node_capacity, 0,
               (new_cap - index->node_capacity) * sizeof(hnsw_node_t));
        index->nodes = new_nodes;
        index->node_capacity = new_cap;
    }

    /* Expand ID map if needed */
    if (id >= index->id_map_size) {
        size_t new_size = id + 1;
        if (new_size < index->id_map_size * 2) {
            new_size = index->id_map_size * 2;
        }
        node_id_t* new_map = realloc(index->id_to_idx, new_size * sizeof(node_id_t));
        if (!new_map) {
            MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to expand ID map");
        }
        for (size_t i = index->id_map_size; i < new_size; i++) {
            new_map[i] = NODE_ID_INVALID;
        }
        index->id_to_idx = new_map;
        index->id_map_size = new_size;
    }

    /* Assign layer */
    int node_layer = random_layer(index);
    if (node_layer > MAX_LAYERS - 1) node_layer = MAX_LAYERS - 1;

    /* Create node */
    size_t node_idx = index->node_count++;
    hnsw_node_t* node = &index->nodes[node_idx];
    node->id = id;
    memcpy(node->vector, vector, EMBEDDING_DIM * sizeof(float));
    node->top_layer = node_layer;
    node->deleted = false;

    /* Allocate neighbor lists */
    node->neighbors = calloc(node_layer + 1, sizeof(node_id_t*));
    if (!node->neighbors) {
        index->node_count--;  /* Rollback node allocation */
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate neighbor lists");
    }

    node->neighbor_counts = calloc(node_layer + 1, sizeof(size_t));
    if (!node->neighbor_counts) {
        free(node->neighbors);
        node->neighbors = NULL;
        index->node_count--;  /* Rollback node allocation */
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate neighbor counts");
    }

    for (int layer = 0; layer <= node_layer; layer++) {
        size_t max_neighbors = (layer == 0) ? index->config.M * 2 : index->config.M;
        node->neighbors[layer] = calloc(max_neighbors, sizeof(node_id_t));
        if (!node->neighbors[layer]) {
            /* Clean up previously allocated layers */
            for (int j = 0; j < layer; j++) {
                free(node->neighbors[j]);
            }
            free(node->neighbors);
            free(node->neighbor_counts);
            node->neighbors = NULL;
            node->neighbor_counts = NULL;
            index->node_count--;  /* Rollback node allocation */
            MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate layer neighbors");
        }
    }

    /* Update ID mapping */
    index->id_to_idx[id] = (node_id_t)node_idx;

    /* If first node, set as entry point */
    if (index->node_count == 1) {
        index->entry_point = 0;
        index->max_layer = node_layer;
        return MEM_OK;
    }

    /* Search for neighbors and connect */
    size_t curr_entry = index->entry_point;
    float curr_dist = compute_distance(vector, index->nodes[curr_entry].vector);

    /* Greedy search from top layer down to node_layer + 1 */
    for (int layer = index->max_layer; layer > node_layer; layer--) {
        bool changed = true;
        while (changed) {
            changed = false;
            hnsw_node_t* entry_node = &index->nodes[curr_entry];

            if (layer > entry_node->top_layer) continue;

            for (size_t i = 0; i < entry_node->neighbor_counts[layer]; i++) {
                size_t neighbor_idx = entry_node->neighbors[layer][i];
                if (neighbor_idx >= index->node_count) continue;

                float dist = compute_distance(vector, index->nodes[neighbor_idx].vector);
                if (dist < curr_dist) {
                    curr_dist = dist;
                    curr_entry = neighbor_idx;
                    changed = true;
                }
            }
        }
    }

    /* Search and connect at each layer from node_layer down to 0 */
    for (int layer = node_layer; layer >= 0; layer--) {
        pq_t candidates;
        if (!pq_init(&candidates, index->config.ef_construction)) {
            continue;  /* Skip this layer on allocation failure */
        }

        search_layer(index, vector, curr_entry, layer, index->config.ef_construction, &candidates);

        /* Select neighbors */
        size_t M = (layer == 0) ? index->config.M * 2 : index->config.M;
        node_id_t selected[256];
        size_t selected_count = 0;

        select_neighbors(index, node_idx, &candidates, layer, M, selected, &selected_count);

        /* Connect */
        for (size_t i = 0; i < selected_count; i++) {
            add_connection(index, node_idx, selected[i], layer);
            add_connection(index, selected[i], node_idx, layer);
        }

        /* Update entry for next layer */
        if (selected_count > 0) {
            curr_entry = selected[0];
        }

        pq_destroy(&candidates);
    }

    /* Update entry point if new node is at higher layer */
    if (node_layer > index->max_layer) {
        index->entry_point = node_idx;
        index->max_layer = node_layer;
    }

    return MEM_OK;
}

mem_error_t hnsw_search(const hnsw_index_t* index, const float* query,
                        size_t k, hnsw_result_t* results, size_t* result_count) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index is NULL");
    MEM_CHECK_ERR(query != NULL, MEM_ERR_INVALID_ARG, "query is NULL");
    MEM_CHECK_ERR(results != NULL, MEM_ERR_INVALID_ARG, "results is NULL");
    MEM_CHECK_ERR(result_count != NULL, MEM_ERR_INVALID_ARG, "result_count is NULL");

    *result_count = 0;

    if (index->node_count == 0) {
        return MEM_OK;
    }

    /* Cast away const for internal operations (search doesn't modify) */
    hnsw_index_t* idx = (hnsw_index_t*)index;

    /* Find entry point at top layer */
    size_t curr_entry = idx->entry_point;
    float curr_dist = compute_distance(query, idx->nodes[curr_entry].vector);

    /* Greedy search from top to layer 1 */
    for (int layer = idx->max_layer; layer > 0; layer--) {
        bool changed = true;
        while (changed) {
            changed = false;
            hnsw_node_t* entry_node = &idx->nodes[curr_entry];

            if (layer > entry_node->top_layer) continue;

            for (size_t i = 0; i < entry_node->neighbor_counts[layer]; i++) {
                size_t neighbor_idx = entry_node->neighbors[layer][i];
                if (neighbor_idx >= idx->node_count) continue;
                if (idx->nodes[neighbor_idx].deleted) continue;

                float dist = compute_distance(query, idx->nodes[neighbor_idx].vector);
                if (dist < curr_dist) {
                    curr_dist = dist;
                    curr_entry = neighbor_idx;
                    changed = true;
                }
            }
        }
    }

    /* Search layer 0 with ef_search candidates */
    pq_t candidates;
    if (!pq_init(&candidates, idx->config.ef_search)) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate search candidates");
    }

    search_layer(idx, query, curr_entry, 0, idx->config.ef_search, &candidates);

    /* Extract k best results */
    pq_elem_t* sorted = malloc(candidates.size * sizeof(pq_elem_t));
    if (!sorted) {
        pq_destroy(&candidates);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate result buffer");
    }
    size_t sorted_count = 0;

    while (!pq_empty(&candidates)) {
        sorted[sorted_count++] = pq_pop(&candidates);
    }

    /* Results are already sorted by distance (min-heap extraction) */
    for (size_t i = 0; i < sorted_count && *result_count < k; i++) {
        size_t node_idx = sorted[i].node_idx;
        if (!idx->nodes[node_idx].deleted) {
            results[*result_count].id = idx->nodes[node_idx].id;
            results[*result_count].distance = sorted[i].distance;
            (*result_count)++;
        }
    }

    free(sorted);
    pq_destroy(&candidates);

    return MEM_OK;
}

size_t hnsw_size(const hnsw_index_t* index) {
    if (!index) return 0;

    size_t count = 0;
    for (size_t i = 0; i < index->node_count; i++) {
        if (!index->nodes[i].deleted) {
            count++;
        }
    }
    return count;
}

bool hnsw_contains(const hnsw_index_t* index, node_id_t id) {
    if (!index) return false;
    if (id >= index->id_map_size) return false;
    if (index->id_to_idx[id] == NODE_ID_INVALID) return false;

    size_t idx = index->id_to_idx[id];
    return !index->nodes[idx].deleted;
}

mem_error_t hnsw_remove(hnsw_index_t* index, node_id_t id) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index is NULL");

    if (id >= index->id_map_size || index->id_to_idx[id] == NODE_ID_INVALID) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "ID %u not in index", id);
    }

    size_t idx = index->id_to_idx[id];
    index->nodes[idx].deleted = true;

    /* Note: We don't actually remove the node or update the graph structure.
     * This is a soft delete that marks the node as deleted but keeps it
     * in place. A more sophisticated implementation would rebuild the
     * graph periodically or implement proper removal. */

    return MEM_OK;
}
