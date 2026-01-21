/*
 * Memory Service - WordPiece Tokenizer Implementation
 *
 * Implements BERT-style WordPiece tokenization.
 */

#include "tokenizer.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Hash table entry for vocabulary */
typedef struct vocab_entry {
    char* token;
    int32_t id;
    struct vocab_entry* next;
} vocab_entry_t;

/* Hash table size (prime for better distribution) */
#define VOCAB_HASH_SIZE 16381

struct tokenizer {
    vocab_entry_t* vocab_table[VOCAB_HASH_SIZE];
    size_t vocab_size;
    bool has_vocab;
};

/* Simple string hash function (djb2) */
static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/* Add token to vocabulary */
static mem_error_t vocab_add(tokenizer_t* tok, const char* token, int32_t id) {
    uint32_t hash = hash_string(token) % VOCAB_HASH_SIZE;

    vocab_entry_t* entry = malloc(sizeof(vocab_entry_t));
    if (!entry) return MEM_ERR_NOMEM;

    entry->token = strdup(token);
    if (!entry->token) {
        free(entry);
        return MEM_ERR_NOMEM;
    }

    entry->id = id;
    entry->next = tok->vocab_table[hash];
    tok->vocab_table[hash] = entry;
    tok->vocab_size++;

    return MEM_OK;
}

/* Look up token ID */
int32_t tokenizer_token_to_id(const tokenizer_t* tok, const char* token) {
    if (!tok || !token) return TOKEN_UNK;

    uint32_t hash = hash_string(token) % VOCAB_HASH_SIZE;
    vocab_entry_t* entry = tok->vocab_table[hash];

    while (entry) {
        if (strcmp(entry->token, token) == 0) {
            return entry->id;
        }
        entry = entry->next;
    }

    return TOKEN_UNK;
}

mem_error_t tokenizer_create(tokenizer_t** tokenizer, const char* vocab_path) {
    MEM_CHECK_ERR(tokenizer != NULL, MEM_ERR_INVALID_ARG, "tokenizer is NULL");

    tokenizer_t* tok = calloc(1, sizeof(tokenizer_t));
    MEM_CHECK_ALLOC(tok);

    if (!vocab_path) {
        tok->has_vocab = false;
        *tokenizer = tok;
        LOG_WARN("No vocabulary path - tokenizer will use character-level fallback");
        return MEM_OK;
    }

    FILE* f = fopen(vocab_path, "r");
    if (!f) {
        tok->has_vocab = false;
        *tokenizer = tok;
        LOG_WARN("Could not open vocabulary file %s - using fallback", vocab_path);
        return MEM_OK;
    }

    /* Read vocabulary line by line */
    char line[MAX_TOKEN_LEN];
    int32_t id = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        if (len > 0) {
            mem_error_t err = vocab_add(tok, line, id);
            if (err != MEM_OK) {
                fclose(f);
                tokenizer_destroy(tok);
                return err;
            }
        }
        id++;
    }

    fclose(f);
    tok->has_vocab = true;

    *tokenizer = tok;
    LOG_INFO("Tokenizer loaded vocabulary with %zu tokens", tok->vocab_size);
    return MEM_OK;
}

mem_error_t tokenizer_create_default(tokenizer_t** tokenizer) {
    MEM_CHECK_ERR(tokenizer != NULL, MEM_ERR_INVALID_ARG, "tokenizer is NULL");

    tokenizer_t* tok = calloc(1, sizeof(tokenizer_t));
    MEM_CHECK_ALLOC(tok);

    mem_error_t err;

    /* Add essential special tokens - check all return values */
    err = vocab_add(tok, "[PAD]", TOKEN_PAD);
    if (err != MEM_OK) goto cleanup_error;
    err = vocab_add(tok, "[UNK]", TOKEN_UNK);
    if (err != MEM_OK) goto cleanup_error;
    err = vocab_add(tok, "[CLS]", TOKEN_CLS);
    if (err != MEM_OK) goto cleanup_error;
    err = vocab_add(tok, "[SEP]", TOKEN_SEP);
    if (err != MEM_OK) goto cleanup_error;
    err = vocab_add(tok, "[MASK]", TOKEN_MASK);
    if (err != MEM_OK) goto cleanup_error;

    /* Add basic ASCII characters (IDs 1000+) */
    for (int c = 32; c < 127; c++) {
        char token[2] = {(char)c, '\0'};
        err = vocab_add(tok, token, 1000 + (c - 32));
        if (err != MEM_OK) goto cleanup_error;
    }

    /* Add common words */
    const char* common_words[] = {
        "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "do", "does", "did", "will", "would", "could",
        "should", "may", "might", "must", "shall", "can", "need", "dare",
        "ought", "used", "to", "of", "in", "for", "on", "with", "at", "by",
        "from", "as", "into", "through", "during", "before", "after", "above",
        "below", "between", "under", "again", "further", "then", "once",
        "here", "there", "when", "where", "why", "how", "all", "each",
        "every", "both", "few", "more", "most", "other", "some", "such",
        "no", "nor", "not", "only", "own", "same", "so", "than", "too",
        "very", "just", "also", "now", "new", "first", "last", "long",
        "great", "little", "own", "other", "old", "right", "big", "high",
        "different", "small", "large", "next", "early", "young", "important",
        "public", "bad", "same", "able", "and", "or", "but", "if", "because",
        "until", "while", "although", "though", "after", "before", "since",
        NULL
    };

    int32_t word_id = 2000;
    for (int i = 0; common_words[i] != NULL; i++) {
        err = vocab_add(tok, common_words[i], word_id++);
        if (err != MEM_OK) goto cleanup_error;
    }

    tok->has_vocab = true;
    *tokenizer = tok;

    LOG_INFO("Created default tokenizer with %zu tokens", tok->vocab_size);
    return MEM_OK;

cleanup_error:
    tokenizer_destroy(tok);
    return err;
}

void tokenizer_destroy(tokenizer_t* tok) {
    if (!tok) return;

    for (size_t i = 0; i < VOCAB_HASH_SIZE; i++) {
        vocab_entry_t* entry = tok->vocab_table[i];
        while (entry) {
            vocab_entry_t* next = entry->next;
            free(entry->token);
            free(entry);
            entry = next;
        }
    }

    free(tok);
}

/* Basic word tokenization (split on whitespace and punctuation) */
static size_t basic_tokenize(const char* text, size_t text_len,
                             char tokens[][MAX_TOKEN_LEN], size_t max_tokens) {
    size_t token_count = 0;
    size_t i = 0;

    while (i < text_len && token_count < max_tokens) {
        /* Skip whitespace */
        while (i < text_len && isspace((unsigned char)text[i])) {
            i++;
        }

        if (i >= text_len) break;

        /* Check for punctuation (single character token) */
        if (ispunct((unsigned char)text[i])) {
            tokens[token_count][0] = text[i];
            tokens[token_count][1] = '\0';
            token_count++;
            i++;
            continue;
        }

        /* Collect word characters */
        size_t start = i;
        while (i < text_len && !isspace((unsigned char)text[i]) &&
               !ispunct((unsigned char)text[i])) {
            i++;
        }

        size_t word_len = i - start;
        if (word_len > 0 && word_len < MAX_TOKEN_LEN - 1) {
            /* Convert to lowercase */
            for (size_t j = 0; j < word_len; j++) {
                tokens[token_count][j] = tolower((unsigned char)text[start + j]);
            }
            tokens[token_count][word_len] = '\0';
            token_count++;
        }
    }

    return token_count;
}

/* WordPiece tokenization of a single word */
static size_t wordpiece_tokenize(const tokenizer_t* tok, const char* word,
                                 int32_t* output_ids, size_t max_ids) {
    size_t word_len = strlen(word);
    size_t output_count = 0;
    size_t start = 0;

    while (start < word_len && output_count < max_ids) {
        size_t end = word_len;
        bool found = false;

        /* Try to find the longest matching subword */
        while (start < end) {
            char subword[MAX_TOKEN_LEN];

            if (start == 0) {
                /* First subword - no prefix */
                size_t len = end - start;
                if (len >= MAX_TOKEN_LEN) len = MAX_TOKEN_LEN - 1;
                memcpy(subword, word + start, len);
                subword[len] = '\0';
            } else {
                /* Continuation - add ## prefix */
                subword[0] = '#';
                subword[1] = '#';
                size_t len = end - start;
                if (len >= MAX_TOKEN_LEN - 2) len = MAX_TOKEN_LEN - 3;
                memcpy(subword + 2, word + start, len);
                subword[2 + len] = '\0';
            }

            int32_t id = tokenizer_token_to_id(tok, subword);
            if (id != TOKEN_UNK) {
                output_ids[output_count++] = id;
                found = true;
                start = end;
                break;
            }

            end--;
        }

        if (!found) {
            /* Character not in vocab, use [UNK] */
            output_ids[output_count++] = TOKEN_UNK;
            start++;
        }
    }

    return output_count;
}

mem_error_t tokenizer_encode(tokenizer_t* tok,
                             const char* text, size_t text_len,
                             size_t max_length,
                             tokenizer_output_t* output) {
    MEM_CHECK_ERR(tok != NULL, MEM_ERR_INVALID_ARG, "tokenizer is NULL");
    MEM_CHECK_ERR(output != NULL, MEM_ERR_INVALID_ARG, "output is NULL");
    MEM_CHECK_ERR(max_length >= 3, MEM_ERR_INVALID_ARG, "max_length too small");

    /* Allocate output buffers */
    output->input_ids = calloc(max_length, sizeof(int32_t));
    output->attention_mask = calloc(max_length, sizeof(int32_t));
    output->token_type_ids = calloc(max_length, sizeof(int32_t));
    output->max_length = max_length;

    if (!output->input_ids || !output->attention_mask || !output->token_type_ids) {
        tokenizer_output_free(output);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate tokenizer output");
    }

    /* Start with [CLS] token */
    size_t pos = 0;
    output->input_ids[pos] = TOKEN_CLS;
    output->attention_mask[pos] = 1;
    pos++;

    if (text && text_len > 0 && tok->has_vocab) {
        /* Basic tokenization */
        char words[256][MAX_TOKEN_LEN];
        size_t word_count = basic_tokenize(text, text_len, words, 256);

        /* WordPiece tokenization for each word */
        for (size_t i = 0; i < word_count && pos < max_length - 1; i++) {
            int32_t subword_ids[32];
            size_t subword_count = wordpiece_tokenize(tok, words[i],
                                                       subword_ids, 32);

            for (size_t j = 0; j < subword_count && pos < max_length - 1; j++) {
                output->input_ids[pos] = subword_ids[j];
                output->attention_mask[pos] = 1;
                pos++;
            }
        }
    } else if (text && text_len > 0) {
        /* Fallback: character-level tokenization */
        for (size_t i = 0; i < text_len && pos < max_length - 1; i++) {
            output->input_ids[pos] = TOKEN_UNK;  /* All unknown without vocab */
            output->attention_mask[pos] = 1;
            pos++;
        }
    }

    /* Add [SEP] token */
    if (pos < max_length) {
        output->input_ids[pos] = TOKEN_SEP;
        output->attention_mask[pos] = 1;
        pos++;
    }

    output->length = pos;

    /* Rest is padding (already zeroed by calloc) */

    return MEM_OK;
}

mem_error_t tokenizer_encode_batch(tokenizer_t* tok,
                                   const char** texts,
                                   const size_t* lengths,
                                   size_t count,
                                   size_t max_length,
                                   tokenizer_output_t* outputs) {
    MEM_CHECK_ERR(tok != NULL, MEM_ERR_INVALID_ARG, "tokenizer is NULL");
    MEM_CHECK_ERR(outputs != NULL, MEM_ERR_INVALID_ARG, "outputs is NULL");

    for (size_t i = 0; i < count; i++) {
        mem_error_t err = tokenizer_encode(tok, texts[i], lengths[i],
                                           max_length, &outputs[i]);
        if (err != MEM_OK) {
            /* Free already allocated outputs */
            for (size_t j = 0; j < i; j++) {
                tokenizer_output_free(&outputs[j]);
            }
            return err;
        }
    }

    return MEM_OK;
}

void tokenizer_output_free(tokenizer_output_t* output) {
    if (!output) return;

    free(output->input_ids);
    free(output->attention_mask);
    free(output->token_type_ids);

    output->input_ids = NULL;
    output->attention_mask = NULL;
    output->token_type_ids = NULL;
    output->length = 0;
    output->max_length = 0;
}

size_t tokenizer_vocab_size(const tokenizer_t* tok) {
    return tok ? tok->vocab_size : 0;
}

bool tokenizer_has_vocab(const tokenizer_t* tok) {
    return tok ? tok->has_vocab : false;
}
