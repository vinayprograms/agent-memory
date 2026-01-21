/*
 * Memory Service - WordPiece Tokenizer
 *
 * Implements tokenization for BERT-style models like all-MiniLM-L6-v2.
 * Supports vocabulary loading, WordPiece tokenization, and special tokens.
 */

#ifndef MEMORY_SERVICE_TOKENIZER_H
#define MEMORY_SERVICE_TOKENIZER_H

#include "../../include/types.h"
#include "../../include/error.h"

#include <stdint.h>
#include <stdbool.h>

/* Forward declaration */
typedef struct tokenizer tokenizer_t;

/* Special token IDs (standard BERT vocabulary) */
#define TOKEN_PAD   0       /* [PAD] */
#define TOKEN_UNK   100     /* [UNK] */
#define TOKEN_CLS   101     /* [CLS] */
#define TOKEN_SEP   102     /* [SEP] */
#define TOKEN_MASK  103     /* [MASK] */

/* Maximum vocabulary size */
#define MAX_VOCAB_SIZE 32000

/* Maximum token length */
#define MAX_TOKEN_LEN 128

/* Tokenization result */
typedef struct {
    int32_t* input_ids;         /* Token IDs */
    int32_t* attention_mask;    /* 1 for real tokens, 0 for padding */
    int32_t* token_type_ids;    /* Segment IDs (all 0 for single sequence) */
    size_t length;              /* Actual sequence length (including special tokens) */
    size_t max_length;          /* Allocated length */
} tokenizer_output_t;

/*
 * Create tokenizer from vocabulary file
 *
 * @param tokenizer  Output tokenizer handle
 * @param vocab_path Path to vocab.txt file
 * @return MEM_OK on success
 */
mem_error_t tokenizer_create(tokenizer_t** tokenizer, const char* vocab_path);

/*
 * Create tokenizer from embedded vocabulary
 * Uses a built-in minimal vocabulary for testing when no vocab file is available.
 */
mem_error_t tokenizer_create_default(tokenizer_t** tokenizer);

/*
 * Destroy tokenizer
 */
void tokenizer_destroy(tokenizer_t* tokenizer);

/*
 * Tokenize text
 *
 * Adds [CLS] at start, [SEP] at end, pads to max_length.
 *
 * @param tokenizer    The tokenizer
 * @param text         Input text
 * @param text_len     Length of input text
 * @param max_length   Maximum sequence length (including special tokens)
 * @param output       Output structure (caller must free with tokenizer_output_free)
 * @return MEM_OK on success
 */
mem_error_t tokenizer_encode(tokenizer_t* tokenizer,
                             const char* text, size_t text_len,
                             size_t max_length,
                             tokenizer_output_t* output);

/*
 * Tokenize batch of texts
 *
 * @param tokenizer    The tokenizer
 * @param texts        Array of text pointers
 * @param lengths      Array of text lengths
 * @param count        Number of texts
 * @param max_length   Maximum sequence length
 * @param outputs      Array of output structures
 * @return MEM_OK on success
 */
mem_error_t tokenizer_encode_batch(tokenizer_t* tokenizer,
                                   const char** texts,
                                   const size_t* lengths,
                                   size_t count,
                                   size_t max_length,
                                   tokenizer_output_t* outputs);

/*
 * Free tokenizer output
 */
void tokenizer_output_free(tokenizer_output_t* output);

/*
 * Get vocabulary size
 */
size_t tokenizer_vocab_size(const tokenizer_t* tokenizer);

/*
 * Look up token ID for a word
 */
int32_t tokenizer_token_to_id(const tokenizer_t* tokenizer, const char* token);

/*
 * Check if tokenizer has a vocabulary loaded
 */
bool tokenizer_has_vocab(const tokenizer_t* tokenizer);

#endif /* MEMORY_SERVICE_TOKENIZER_H */
