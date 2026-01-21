/*
 * Memory Service - Inverted Index
 *
 * Token-to-document inverted index for exact match search.
 * Supports term frequency and document frequency tracking.
 */

#ifndef MEMORY_SERVICE_INVERTED_INDEX_H
#define MEMORY_SERVICE_INVERTED_INDEX_H

#include "../../include/types.h"
#include "../../include/error.h"

/* Forward declaration */
typedef struct inverted_index inverted_index_t;

/* Index configuration */
typedef struct {
    size_t max_tokens;       /* Maximum unique tokens (default: 100000) */
    size_t max_documents;    /* Maximum documents (default: 100000) */
    size_t max_token_len;    /* Maximum token length (default: 64) */
} inverted_index_config_t;

/* Default configuration */
#define INVERTED_INDEX_CONFIG_DEFAULT { \
    .max_tokens = 100000, \
    .max_documents = 100000, \
    .max_token_len = 64 \
}

/* Posting entry (document + term frequency) */
typedef struct {
    node_id_t doc_id;
    uint16_t term_freq;      /* Term frequency in document */
    uint16_t position;       /* First position in document */
} posting_t;

/* Search result */
typedef struct {
    node_id_t doc_id;
    float score;             /* TF-IDF or BM25 score */
} inverted_result_t;

/*
 * Create a new inverted index
 */
mem_error_t inverted_index_create(inverted_index_t** index,
                                  const inverted_index_config_t* config);

/*
 * Destroy inverted index
 */
void inverted_index_destroy(inverted_index_t* index);

/*
 * Add a document to the index
 *
 * @param index   The inverted index
 * @param doc_id  Document identifier
 * @param tokens  Array of tokens
 * @param count   Number of tokens
 * @return MEM_OK on success
 */
mem_error_t inverted_index_add(inverted_index_t* index, node_id_t doc_id,
                               const char** tokens, size_t count);

/*
 * Remove a document from the index
 */
mem_error_t inverted_index_remove(inverted_index_t* index, node_id_t doc_id);

/*
 * Search for documents matching all tokens (AND query)
 *
 * @param index        The inverted index
 * @param tokens       Query tokens
 * @param token_count  Number of query tokens
 * @param k            Maximum results to return
 * @param results      Output array (must hold k results)
 * @param result_count Output: actual number of results
 * @return MEM_OK on success
 */
mem_error_t inverted_index_search(const inverted_index_t* index,
                                  const char** tokens, size_t token_count,
                                  size_t k, inverted_result_t* results,
                                  size_t* result_count);

/*
 * Search for documents matching any token (OR query)
 */
mem_error_t inverted_index_search_any(const inverted_index_t* index,
                                      const char** tokens, size_t token_count,
                                      size_t k, inverted_result_t* results,
                                      size_t* result_count);

/*
 * Get number of documents in the index
 */
size_t inverted_index_doc_count(const inverted_index_t* index);

/*
 * Get number of unique tokens
 */
size_t inverted_index_token_count(const inverted_index_t* index);

/*
 * Check if index contains a document
 */
bool inverted_index_contains(const inverted_index_t* index, node_id_t doc_id);

/*
 * Tokenize text into normalized tokens
 *
 * @param text       Input text
 * @param len        Text length
 * @param tokens     Output token array (must free each and the array)
 * @param count      Output: number of tokens
 * @param max_tokens Maximum tokens to extract
 */
mem_error_t inverted_index_tokenize(const char* text, size_t len,
                                    char*** tokens, size_t* count,
                                    size_t max_tokens);

/*
 * Free tokenized output
 */
void inverted_index_free_tokens(char** tokens, size_t count);

#endif /* MEMORY_SERVICE_INVERTED_INDEX_H */
