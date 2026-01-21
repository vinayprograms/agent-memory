/*
 * Memory Service - Inverted Index Implementation
 *
 * Simple hash-based inverted index for exact match search.
 * Uses BM25 scoring for ranking results.
 */

#include "inverted_index.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* BM25 parameters */
#define BM25_K1 1.2f
#define BM25_B  0.75f

/* Hash table entry for token -> posting list mapping */
typedef struct token_entry {
    char* token;
    posting_t* postings;
    size_t posting_count;
    size_t posting_capacity;
    struct token_entry* next;  /* For hash collision chaining */
} token_entry_t;

/* Document info for length normalization */
typedef struct doc_info {
    node_id_t doc_id;
    size_t token_count;
    bool deleted;
} doc_info_t;

/* Inverted index structure */
struct inverted_index {
    inverted_index_config_t config;

    /* Hash table for token -> postings */
    token_entry_t** buckets;
    size_t bucket_count;
    size_t token_count;

    /* Document info array */
    doc_info_t* docs;
    size_t doc_count;
    size_t doc_capacity;

    /* Average document length (for BM25) */
    float avg_doc_len;
    size_t total_tokens;
};

/* ========== Hash Functions ========== */

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    }
    return hash;
}

/* ========== Token Entry Management ========== */

static token_entry_t* token_entry_create(const char* token) {
    token_entry_t* entry = calloc(1, sizeof(token_entry_t));
    if (!entry) return NULL;

    entry->token = strdup(token);
    if (!entry->token) {
        free(entry);
        return NULL;
    }

    entry->posting_capacity = 8;
    entry->postings = calloc(entry->posting_capacity, sizeof(posting_t));
    if (!entry->postings) {
        free(entry->token);
        free(entry);
        return NULL;
    }

    return entry;
}

static void token_entry_destroy(token_entry_t* entry) {
    if (!entry) return;
    free(entry->token);
    free(entry->postings);
    free(entry);
}

static mem_error_t token_entry_add_posting(token_entry_t* entry, node_id_t doc_id,
                                           uint16_t position) {
    /* Check if document already in postings */
    for (size_t i = 0; i < entry->posting_count; i++) {
        if (entry->postings[i].doc_id == doc_id) {
            entry->postings[i].term_freq++;
            return MEM_OK;
        }
    }

    /* Add new posting */
    if (entry->posting_count >= entry->posting_capacity) {
        size_t new_cap = entry->posting_capacity * 2;
        posting_t* new_postings = realloc(entry->postings, new_cap * sizeof(posting_t));
        if (!new_postings) {
            MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to expand postings");
        }
        entry->postings = new_postings;
        entry->posting_capacity = new_cap;
    }

    posting_t* p = &entry->postings[entry->posting_count++];
    p->doc_id = doc_id;
    p->term_freq = 1;
    p->position = position;

    return MEM_OK;
}

static void token_entry_remove_doc(token_entry_t* entry, node_id_t doc_id) {
    for (size_t i = 0; i < entry->posting_count; i++) {
        if (entry->postings[i].doc_id == doc_id) {
            /* Shift remaining postings */
            memmove(&entry->postings[i], &entry->postings[i + 1],
                    (entry->posting_count - i - 1) * sizeof(posting_t));
            entry->posting_count--;
            return;
        }
    }
}

/* ========== Index Operations ========== */

static token_entry_t* find_token(inverted_index_t* idx, const char* token) {
    uint32_t hash = hash_string(token);
    size_t bucket = hash % idx->bucket_count;

    token_entry_t* entry = idx->buckets[bucket];
    while (entry) {
        if (strcmp(entry->token, token) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static token_entry_t* find_or_create_token(inverted_index_t* idx, const char* token) {
    uint32_t hash = hash_string(token);
    size_t bucket = hash % idx->bucket_count;

    /* Search existing */
    token_entry_t* entry = idx->buckets[bucket];
    while (entry) {
        if (strcmp(entry->token, token) == 0) {
            return entry;
        }
        entry = entry->next;
    }

    /* Create new */
    entry = token_entry_create(token);
    if (!entry) return NULL;

    entry->next = idx->buckets[bucket];
    idx->buckets[bucket] = entry;
    idx->token_count++;

    return entry;
}

static size_t find_doc_index(inverted_index_t* idx, node_id_t doc_id) {
    for (size_t i = 0; i < idx->doc_count; i++) {
        if (idx->docs[i].doc_id == doc_id && !idx->docs[i].deleted) {
            return i;
        }
    }
    return SIZE_MAX;
}

/* ========== BM25 Scoring ========== */

static float bm25_score(float tf, float df, float doc_len, float avg_doc_len,
                        size_t total_docs) {
    float idf = logf((total_docs - df + 0.5f) / (df + 0.5f) + 1.0f);
    float tf_component = (tf * (BM25_K1 + 1.0f)) /
                         (tf + BM25_K1 * (1.0f - BM25_B + BM25_B * (doc_len / avg_doc_len)));
    return idf * tf_component;
}

/* ========== Public API ========== */

mem_error_t inverted_index_create(inverted_index_t** index,
                                  const inverted_index_config_t* config) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index pointer is NULL");

    inverted_index_t* idx = calloc(1, sizeof(inverted_index_t));
    if (!idx) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate inverted index");
    }

    if (config) {
        idx->config = *config;
    } else {
        idx->config = (inverted_index_config_t)INVERTED_INDEX_CONFIG_DEFAULT;
    }

    /* Allocate hash buckets */
    idx->bucket_count = 4096;  /* Power of 2 for efficient modulo */
    idx->buckets = calloc(idx->bucket_count, sizeof(token_entry_t*));
    if (!idx->buckets) {
        free(idx);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate buckets");
    }

    /* Allocate document info */
    idx->doc_capacity = 1024;
    idx->docs = calloc(idx->doc_capacity, sizeof(doc_info_t));
    if (!idx->docs) {
        free(idx->buckets);
        free(idx);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate docs");
    }

    idx->avg_doc_len = 0.0f;

    *index = idx;
    return MEM_OK;
}

void inverted_index_destroy(inverted_index_t* index) {
    if (!index) return;

    /* Free all token entries */
    for (size_t i = 0; i < index->bucket_count; i++) {
        token_entry_t* entry = index->buckets[i];
        while (entry) {
            token_entry_t* next = entry->next;
            token_entry_destroy(entry);
            entry = next;
        }
    }

    free(index->buckets);
    free(index->docs);
    free(index);
}

mem_error_t inverted_index_add(inverted_index_t* index, node_id_t doc_id,
                               const char** tokens, size_t count) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index is NULL");

    if (count == 0) return MEM_OK;

    /* Check if document already exists */
    if (find_doc_index(index, doc_id) != SIZE_MAX) {
        MEM_RETURN_ERROR(MEM_ERR_EXISTS, "document %u already in index", doc_id);
    }

    /* Expand document array if needed */
    if (index->doc_count >= index->doc_capacity) {
        size_t new_cap = index->doc_capacity * 2;
        doc_info_t* new_docs = realloc(index->docs, new_cap * sizeof(doc_info_t));
        if (!new_docs) {
            MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to expand docs");
        }
        index->docs = new_docs;
        index->doc_capacity = new_cap;
    }

    /* Add document info */
    doc_info_t* doc = &index->docs[index->doc_count++];
    doc->doc_id = doc_id;
    doc->token_count = count;
    doc->deleted = false;

    /* Update average document length */
    index->total_tokens += count;
    index->avg_doc_len = (float)index->total_tokens / (float)index->doc_count;

    /* Add tokens to index */
    for (size_t i = 0; i < count; i++) {
        if (!tokens[i] || strlen(tokens[i]) == 0) continue;

        token_entry_t* entry = find_or_create_token(index, tokens[i]);
        if (!entry) {
            MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to create token entry");
        }

        MEM_CHECK(token_entry_add_posting(entry, doc_id, (uint16_t)i));
    }

    return MEM_OK;
}

mem_error_t inverted_index_remove(inverted_index_t* index, node_id_t doc_id) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index is NULL");

    size_t doc_idx = find_doc_index(index, doc_id);
    if (doc_idx == SIZE_MAX) {
        MEM_RETURN_ERROR(MEM_ERR_NOT_FOUND, "document %u not in index", doc_id);
    }

    /* Mark document as deleted */
    index->docs[doc_idx].deleted = true;

    /* Update stats */
    index->total_tokens -= index->docs[doc_idx].token_count;
    size_t active_docs = 0;
    for (size_t i = 0; i < index->doc_count; i++) {
        if (!index->docs[i].deleted) active_docs++;
    }
    if (active_docs > 0) {
        index->avg_doc_len = (float)index->total_tokens / (float)active_docs;
    } else {
        index->avg_doc_len = 0.0f;
    }

    /* Remove from all posting lists */
    for (size_t i = 0; i < index->bucket_count; i++) {
        token_entry_t* entry = index->buckets[i];
        while (entry) {
            token_entry_remove_doc(entry, doc_id);
            entry = entry->next;
        }
    }

    return MEM_OK;
}

mem_error_t inverted_index_search(const inverted_index_t* index,
                                  const char** tokens, size_t token_count,
                                  size_t k, inverted_result_t* results,
                                  size_t* result_count) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index is NULL");
    MEM_CHECK_ERR(results != NULL, MEM_ERR_INVALID_ARG, "results is NULL");
    MEM_CHECK_ERR(result_count != NULL, MEM_ERR_INVALID_ARG, "result_count is NULL");

    *result_count = 0;

    if (token_count == 0 || index->doc_count == 0) {
        return MEM_OK;
    }

    /* Find postings for each token */
    token_entry_t** entries = calloc(token_count, sizeof(token_entry_t*));
    if (!entries) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate entries");
    }

    size_t valid_tokens = 0;
    for (size_t i = 0; i < token_count; i++) {
        entries[i] = find_token((inverted_index_t*)index, tokens[i]);
        if (entries[i] && entries[i]->posting_count > 0) {
            valid_tokens++;
        }
    }

    if (valid_tokens == 0) {
        free(entries);
        return MEM_OK;
    }

    /* For AND query, find intersection of posting lists */
    /* Start with smallest posting list */
    size_t min_idx = 0;
    size_t min_count = SIZE_MAX;
    for (size_t i = 0; i < token_count; i++) {
        if (entries[i] && entries[i]->posting_count < min_count) {
            min_count = entries[i]->posting_count;
            min_idx = i;
        }
    }

    if (!entries[min_idx]) {
        free(entries);
        return MEM_OK;
    }

    /* Temporary score array */
    size_t max_results = entries[min_idx]->posting_count;
    inverted_result_t* temp_results = calloc(max_results, sizeof(inverted_result_t));
    if (!temp_results) {
        free(entries);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate temp results");
    }

    size_t temp_count = 0;

    /* Count active documents */
    size_t active_docs = 0;
    for (size_t i = 0; i < index->doc_count; i++) {
        if (!index->docs[i].deleted) active_docs++;
    }

    /* For each document in the smallest posting list */
    for (size_t p = 0; p < entries[min_idx]->posting_count; p++) {
        node_id_t doc_id = entries[min_idx]->postings[p].doc_id;

        /* Check if document is in all other posting lists */
        bool in_all = true;
        float total_score = 0.0f;

        /* Find document length */
        float doc_len = 0.0f;
        for (size_t d = 0; d < index->doc_count; d++) {
            if (index->docs[d].doc_id == doc_id && !index->docs[d].deleted) {
                doc_len = (float)index->docs[d].token_count;
                break;
            }
        }

        for (size_t t = 0; t < token_count; t++) {
            if (!entries[t]) {
                in_all = false;
                break;
            }

            /* Find document in this posting list */
            bool found = false;
            for (size_t i = 0; i < entries[t]->posting_count; i++) {
                if (entries[t]->postings[i].doc_id == doc_id) {
                    float tf = (float)entries[t]->postings[i].term_freq;
                    float df = (float)entries[t]->posting_count;
                    total_score += bm25_score(tf, df, doc_len, index->avg_doc_len, active_docs);
                    found = true;
                    break;
                }
            }

            if (!found) {
                in_all = false;
                break;
            }
        }

        if (in_all) {
            temp_results[temp_count].doc_id = doc_id;
            temp_results[temp_count].score = total_score;
            temp_count++;
        }
    }

    /* Sort by score (simple insertion sort for small result sets) */
    for (size_t i = 1; i < temp_count; i++) {
        inverted_result_t key = temp_results[i];
        size_t j = i;
        while (j > 0 && temp_results[j - 1].score < key.score) {
            temp_results[j] = temp_results[j - 1];
            j--;
        }
        temp_results[j] = key;
    }

    /* Copy top k to results */
    for (size_t i = 0; i < temp_count && i < k; i++) {
        results[i] = temp_results[i];
        (*result_count)++;
    }

    free(temp_results);
    free(entries);

    return MEM_OK;
}

mem_error_t inverted_index_search_any(const inverted_index_t* index,
                                      const char** tokens, size_t token_count,
                                      size_t k, inverted_result_t* results,
                                      size_t* result_count) {
    MEM_CHECK_ERR(index != NULL, MEM_ERR_INVALID_ARG, "index is NULL");
    MEM_CHECK_ERR(results != NULL, MEM_ERR_INVALID_ARG, "results is NULL");
    MEM_CHECK_ERR(result_count != NULL, MEM_ERR_INVALID_ARG, "result_count is NULL");

    *result_count = 0;

    if (token_count == 0 || index->doc_count == 0) {
        return MEM_OK;
    }

    /* Accumulate scores for all documents */
    size_t score_cap = index->doc_count;
    float* scores = calloc(score_cap, sizeof(float));
    node_id_t* doc_ids = calloc(score_cap, sizeof(node_id_t));
    size_t* score_counts = calloc(score_cap, sizeof(size_t));
    if (!scores || !doc_ids || !score_counts) {
        free(scores);
        free(doc_ids);
        free(score_counts);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate score arrays");
    }

    size_t doc_score_count = 0;

    /* Count active documents */
    size_t active_docs = 0;
    for (size_t i = 0; i < index->doc_count; i++) {
        if (!index->docs[i].deleted) active_docs++;
    }

    /* For each token */
    for (size_t t = 0; t < token_count; t++) {
        token_entry_t* entry = find_token((inverted_index_t*)index, tokens[t]);
        if (!entry) continue;

        /* For each posting */
        for (size_t p = 0; p < entry->posting_count; p++) {
            node_id_t doc_id = entry->postings[p].doc_id;

            /* Find or add document score entry */
            size_t score_idx = SIZE_MAX;
            for (size_t i = 0; i < doc_score_count; i++) {
                if (doc_ids[i] == doc_id) {
                    score_idx = i;
                    break;
                }
            }

            if (score_idx == SIZE_MAX) {
                score_idx = doc_score_count++;
                doc_ids[score_idx] = doc_id;
                scores[score_idx] = 0.0f;
                score_counts[score_idx] = 0;
            }

            /* Find document length */
            float doc_len = 0.0f;
            for (size_t d = 0; d < index->doc_count; d++) {
                if (index->docs[d].doc_id == doc_id && !index->docs[d].deleted) {
                    doc_len = (float)index->docs[d].token_count;
                    break;
                }
            }

            float tf = (float)entry->postings[p].term_freq;
            float df = (float)entry->posting_count;
            scores[score_idx] += bm25_score(tf, df, doc_len, index->avg_doc_len, active_docs);
            score_counts[score_idx]++;
        }
    }

    /* Sort by score */
    for (size_t i = 1; i < doc_score_count; i++) {
        float key_score = scores[i];
        node_id_t key_id = doc_ids[i];
        size_t j = i;
        while (j > 0 && scores[j - 1] < key_score) {
            scores[j] = scores[j - 1];
            doc_ids[j] = doc_ids[j - 1];
            j--;
        }
        scores[j] = key_score;
        doc_ids[j] = key_id;
    }

    /* Copy top k to results */
    for (size_t i = 0; i < doc_score_count && i < k; i++) {
        results[i].doc_id = doc_ids[i];
        results[i].score = scores[i];
        (*result_count)++;
    }

    free(scores);
    free(doc_ids);
    free(score_counts);

    return MEM_OK;
}

size_t inverted_index_doc_count(const inverted_index_t* index) {
    if (!index) return 0;

    size_t count = 0;
    for (size_t i = 0; i < index->doc_count; i++) {
        if (!index->docs[i].deleted) count++;
    }
    return count;
}

size_t inverted_index_token_count(const inverted_index_t* index) {
    if (!index) return 0;
    return index->token_count;
}

bool inverted_index_contains(const inverted_index_t* index, node_id_t doc_id) {
    if (!index) return false;
    return find_doc_index((inverted_index_t*)index, doc_id) != SIZE_MAX;
}

mem_error_t inverted_index_tokenize(const char* text, size_t len,
                                    char*** tokens, size_t* count,
                                    size_t max_tokens) {
    MEM_CHECK_ERR(text != NULL, MEM_ERR_INVALID_ARG, "text is NULL");
    MEM_CHECK_ERR(tokens != NULL, MEM_ERR_INVALID_ARG, "tokens is NULL");
    MEM_CHECK_ERR(count != NULL, MEM_ERR_INVALID_ARG, "count is NULL");

    *tokens = NULL;
    *count = 0;

    if (len == 0) return MEM_OK;

    /* Allocate token array */
    size_t capacity = 64;
    char** result = calloc(capacity, sizeof(char*));
    if (!result) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate tokens");
    }

    /* Tokenize: split on whitespace and punctuation, lowercase */
    size_t token_count = 0;
    char token_buf[256];
    size_t buf_len = 0;

    for (size_t i = 0; i <= len && token_count < max_tokens; i++) {
        char c = (i < len) ? text[i] : ' ';

        if (isalnum(c)) {
            if (buf_len < sizeof(token_buf) - 1) {
                token_buf[buf_len++] = tolower(c);
            }
        } else if (buf_len > 0) {
            /* End of token */
            token_buf[buf_len] = '\0';

            /* Skip very short tokens */
            if (buf_len >= 2) {
                if (token_count >= capacity) {
                    capacity *= 2;
                    char** new_result = realloc(result, capacity * sizeof(char*));
                    if (!new_result) {
                        inverted_index_free_tokens(result, token_count);
                        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to expand tokens");
                    }
                    result = new_result;
                }

                result[token_count] = strdup(token_buf);
                if (!result[token_count]) {
                    inverted_index_free_tokens(result, token_count);
                    MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to copy token");
                }
                token_count++;
            }

            buf_len = 0;
        }
    }

    *tokens = result;
    *count = token_count;

    return MEM_OK;
}

void inverted_index_free_tokens(char** tokens, size_t count) {
    if (!tokens) return;
    for (size_t i = 0; i < count; i++) {
        free(tokens[i]);
    }
    free(tokens);
}
