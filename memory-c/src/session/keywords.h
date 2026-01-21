/*
 * Memory Service - Keyword Extraction
 *
 * Extracts keywords and identifiers from content for session metadata.
 * Supports:
 * - TF-IDF style keyword extraction
 * - Code identifier parsing (functions, variables, types)
 * - File path extraction
 */

#ifndef MEMORY_SERVICE_KEYWORDS_H
#define MEMORY_SERVICE_KEYWORDS_H

#include "../../include/types.h"
#include "../../include/error.h"

#include <stddef.h>
#include <stdbool.h>

/* Maximum keywords per extraction */
#define MAX_KEYWORDS 32
#define MAX_KEYWORD_LEN 64
/* MAX_IDENTIFIERS is defined in types.h */
#define MAX_IDENTIFIER_LEN 128
#define MAX_FILE_PATHS 32
#define MAX_FILE_PATH_LEN 256

/* Keyword with score */
typedef struct {
    char    word[MAX_KEYWORD_LEN];
    float   score;      /* TF-IDF or similar score */
} keyword_t;

/* Code identifier */
typedef struct {
    char    name[MAX_IDENTIFIER_LEN];
    enum {
        IDENT_FUNCTION,
        IDENT_VARIABLE,
        IDENT_TYPE,
        IDENT_CONSTANT,
        IDENT_UNKNOWN
    } kind;
} identifier_t;

/* Extraction result */
typedef struct {
    keyword_t       keywords[MAX_KEYWORDS];
    size_t          keyword_count;

    identifier_t    identifiers[MAX_IDENTIFIERS];
    size_t          identifier_count;

    char            file_paths[MAX_FILE_PATHS][MAX_FILE_PATH_LEN];
    size_t          file_path_count;
} extraction_result_t;

/* Keyword extractor context */
typedef struct keyword_extractor keyword_extractor_t;

/*
 * Create a new keyword extractor
 *
 * Initializes internal state for TF-IDF calculations.
 */
mem_error_t keyword_extractor_create(keyword_extractor_t** extractor);

/*
 * Destroy keyword extractor
 */
void keyword_extractor_destroy(keyword_extractor_t* extractor);

/*
 * Extract keywords, identifiers, and file paths from text
 *
 * @param extractor     Extractor context (can be NULL for simple extraction)
 * @param text          Input text to analyze
 * @param text_len      Length of text
 * @param result        Output result structure
 * @return              MEM_OK on success
 */
mem_error_t extract_keywords(keyword_extractor_t* extractor,
                            const char* text, size_t text_len,
                            extraction_result_t* result);

/*
 * Extract code identifiers from text
 *
 * Parses function names, variable names, type names from code.
 *
 * @param text          Input text (code)
 * @param text_len      Length of text
 * @param identifiers   Output array of identifiers
 * @param max_idents    Maximum identifiers to extract
 * @return              Number of identifiers extracted
 */
size_t extract_identifiers(const char* text, size_t text_len,
                          identifier_t* identifiers, size_t max_idents);

/*
 * Extract file paths from text
 *
 * Looks for patterns like:
 * - /path/to/file.ext
 * - ./relative/path
 * - src/component/file.c
 *
 * @param text          Input text
 * @param text_len      Length of text
 * @param paths         Output array of paths
 * @param max_paths     Maximum paths to extract
 * @return              Number of paths extracted
 */
size_t extract_file_paths(const char* text, size_t text_len,
                         char paths[][MAX_FILE_PATH_LEN], size_t max_paths);

/*
 * Update IDF statistics with new document
 *
 * Call this for each document to build IDF statistics for better TF-IDF scoring.
 *
 * @param extractor     Extractor context
 * @param text          Document text
 * @param text_len      Length of text
 * @return              MEM_OK on success
 */
mem_error_t keyword_extractor_update_idf(keyword_extractor_t* extractor,
                                         const char* text, size_t text_len);

/*
 * Simple tokenization helper
 *
 * Splits text into words, normalizing to lowercase.
 *
 * @param text          Input text
 * @param text_len      Length of text
 * @param tokens        Output token array
 * @param max_tokens    Maximum tokens
 * @param token_len     Maximum length per token
 * @return              Number of tokens
 */
size_t tokenize_text(const char* text, size_t text_len,
                    char** tokens, size_t max_tokens, size_t token_len);

/*
 * Check if word is a common stop word
 *
 * @param word          Word to check (lowercase)
 * @return              true if stop word
 */
bool is_stop_word(const char* word);

#endif /* MEMORY_SERVICE_KEYWORDS_H */
