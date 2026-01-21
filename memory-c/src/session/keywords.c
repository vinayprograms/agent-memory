/*
 * Memory Service - Keyword Extraction Implementation
 */

#include "keywords.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* Stop words list */
static const char* STOP_WORDS[] = {
    "a", "an", "the", "and", "or", "but", "in", "on", "at", "to", "for",
    "of", "with", "by", "from", "is", "are", "was", "were", "be", "been",
    "being", "have", "has", "had", "do", "does", "did", "will", "would",
    "could", "should", "may", "might", "must", "shall", "can", "need",
    "this", "that", "these", "those", "it", "its", "i", "me", "my",
    "we", "us", "our", "you", "your", "he", "him", "his", "she", "her",
    "they", "them", "their", "what", "which", "who", "whom", "when",
    "where", "why", "how", "all", "each", "every", "both", "few", "more",
    "most", "other", "some", "such", "no", "not", "only", "same", "so",
    "than", "too", "very", "just", "also", "now", "here", "there", "then",
    "if", "else", "as", "until", "while", "during", "before", "after",
    NULL
};

/* Hash table entry for word counts */
typedef struct word_count_entry {
    char word[MAX_KEYWORD_LEN];
    size_t doc_count;       /* Number of documents containing word */
    struct word_count_entry* next;
} word_count_entry_t;

#define HASH_TABLE_SIZE 1024

struct keyword_extractor {
    word_count_entry_t* idf_table[HASH_TABLE_SIZE];
    size_t doc_count;       /* Total documents seen */
};

/* Simple hash function for strings */
static uint32_t hash_string(const char* s) {
    uint32_t h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (uint8_t)*s++;
    }
    return h;
}

bool is_stop_word(const char* word) {
    for (size_t i = 0; STOP_WORDS[i]; i++) {
        if (strcmp(word, STOP_WORDS[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Check if character is word boundary */
static bool is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/* Convert to lowercase and copy */
static void lowercase_copy(char* dst, const char* src, size_t max_len) {
    size_t i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dst[i] = tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

mem_error_t keyword_extractor_create(keyword_extractor_t** extractor) {
    if (!extractor) return MEM_ERR_INVALID_ARG;

    keyword_extractor_t* ctx = calloc(1, sizeof(keyword_extractor_t));
    if (!ctx) return MEM_ERR_NOMEM;

    *extractor = ctx;
    return MEM_OK;
}

void keyword_extractor_destroy(keyword_extractor_t* extractor) {
    if (!extractor) return;

    /* Free hash table entries */
    for (size_t i = 0; i < HASH_TABLE_SIZE; i++) {
        word_count_entry_t* entry = extractor->idf_table[i];
        while (entry) {
            word_count_entry_t* next = entry->next;
            free(entry);
            entry = next;
        }
    }

    free(extractor);
}

/* Get or create IDF entry */
static word_count_entry_t* get_idf_entry(keyword_extractor_t* extractor,
                                         const char* word, bool create) {
    uint32_t h = hash_string(word) % HASH_TABLE_SIZE;

    word_count_entry_t* entry = extractor->idf_table[h];
    while (entry) {
        if (strcmp(entry->word, word) == 0) {
            return entry;
        }
        entry = entry->next;
    }

    if (!create) return NULL;

    /* Create new entry */
    entry = calloc(1, sizeof(word_count_entry_t));
    if (!entry) return NULL;

    snprintf(entry->word, MAX_KEYWORD_LEN, "%s", word);
    entry->next = extractor->idf_table[h];
    extractor->idf_table[h] = entry;

    return entry;
}

mem_error_t keyword_extractor_update_idf(keyword_extractor_t* extractor,
                                         const char* text, size_t text_len) {
    if (!extractor || !text) return MEM_ERR_INVALID_ARG;

    /* Track which words appear in this document */
    bool seen[HASH_TABLE_SIZE] = {0};

    const char* p = text;
    const char* end = text + text_len;

    while (p < end) {
        /* Skip non-word characters */
        while (p < end && !is_word_char(*p)) p++;
        if (p >= end) break;

        /* Extract word */
        const char* word_start = p;
        while (p < end && is_word_char(*p)) p++;
        size_t word_len = p - word_start;

        if (word_len >= 2 && word_len < MAX_KEYWORD_LEN) {
            char word[MAX_KEYWORD_LEN];
            lowercase_copy(word, word_start, word_len + 1);
            word[word_len] = '\0';

            /* Skip stop words and numbers */
            if (!is_stop_word(word) && !isdigit((unsigned char)word[0])) {
                uint32_t h = hash_string(word) % HASH_TABLE_SIZE;
                if (!seen[h]) {
                    word_count_entry_t* entry = get_idf_entry(extractor, word, true);
                    if (entry) {
                        entry->doc_count++;
                        seen[h] = true;
                    }
                }
            }
        }
    }

    extractor->doc_count++;
    return MEM_OK;
}

/* Term frequency structure */
typedef struct {
    char word[MAX_KEYWORD_LEN];
    size_t count;
    float score;
} term_freq_t;

/* Compare for sorting by score descending */
static int compare_tf_desc(const void* a, const void* b) {
    const term_freq_t* ta = a;
    const term_freq_t* tb = b;
    if (tb->score > ta->score) return 1;
    if (tb->score < ta->score) return -1;
    return 0;
}

mem_error_t extract_keywords(keyword_extractor_t* extractor,
                            const char* text, size_t text_len,
                            extraction_result_t* result) {
    if (!text || !result) return MEM_ERR_INVALID_ARG;

    memset(result, 0, sizeof(*result));

    /* Count term frequencies */
    term_freq_t* terms = calloc(4096, sizeof(term_freq_t));
    if (!terms) return MEM_ERR_NOMEM;
    size_t term_count = 0;
    size_t total_words = 0;

    const char* p = text;
    const char* end = text + text_len;

    while (p < end) {
        /* Skip non-word characters */
        while (p < end && !is_word_char(*p)) p++;
        if (p >= end) break;

        /* Extract word */
        const char* word_start = p;
        while (p < end && is_word_char(*p)) p++;
        size_t word_len = p - word_start;

        if (word_len >= 2 && word_len < MAX_KEYWORD_LEN) {
            char word[MAX_KEYWORD_LEN];
            lowercase_copy(word, word_start, word_len + 1);
            word[word_len] = '\0';

            /* Skip stop words and numbers */
            if (!is_stop_word(word) && !isdigit((unsigned char)word[0])) {
                total_words++;

                /* Find or add term */
                size_t i;
                for (i = 0; i < term_count; i++) {
                    if (strcmp(terms[i].word, word) == 0) {
                        terms[i].count++;
                        break;
                    }
                }
                if (i == term_count && term_count < 4096) {
                    snprintf(terms[term_count].word, MAX_KEYWORD_LEN, "%s", word);
                    terms[term_count].count = 1;
                    term_count++;
                }
            }
        }
    }

    /* Calculate TF-IDF scores */
    for (size_t i = 0; i < term_count; i++) {
        /* TF: normalized by document length */
        float tf = (float)terms[i].count / (total_words > 0 ? total_words : 1);

        /* IDF: log(N/df) or default if no extractor */
        float idf = 1.0f;
        if (extractor && extractor->doc_count > 0) {
            word_count_entry_t* entry = get_idf_entry(extractor, terms[i].word, false);
            if (entry && entry->doc_count > 0) {
                idf = logf((float)extractor->doc_count / entry->doc_count);
            }
        }

        /* Boost longer words slightly */
        float len_boost = 1.0f + 0.1f * (strlen(terms[i].word) - 3);
        if (len_boost < 1.0f) len_boost = 1.0f;
        if (len_boost > 2.0f) len_boost = 2.0f;

        terms[i].score = tf * idf * len_boost;
    }

    /* Sort by score */
    qsort(terms, term_count, sizeof(term_freq_t), compare_tf_desc);

    /* Copy top keywords to result */
    result->keyword_count = term_count > MAX_KEYWORDS ? MAX_KEYWORDS : term_count;
    for (size_t i = 0; i < result->keyword_count; i++) {
        snprintf(result->keywords[i].word, MAX_KEYWORD_LEN, "%s", terms[i].word);
        result->keywords[i].score = terms[i].score;
    }

    free(terms);

    /* Extract identifiers */
    result->identifier_count = extract_identifiers(text, text_len,
                                                   result->identifiers, MAX_IDENTIFIERS);

    /* Extract file paths */
    result->file_path_count = extract_file_paths(text, text_len,
                                                 result->file_paths, MAX_FILE_PATHS);

    return MEM_OK;
}

size_t extract_identifiers(const char* text, size_t text_len,
                          identifier_t* identifiers, size_t max_idents) {
    if (!text || !identifiers || max_idents == 0) return 0;

    size_t count = 0;
    const char* p = text;
    const char* end = text + text_len;

    while (p < end && count < max_idents) {
        /* Look for identifier patterns */

        /* Function pattern: word followed by ( */
        const char* word_start = NULL;
        while (p < end) {
            if (is_word_char(*p) && (p == text || !is_word_char(*(p-1)))) {
                word_start = p;
            }
            if (word_start && !is_word_char(*p)) {
                size_t word_len = p - word_start;

                /* Check what follows */
                const char* after = p;
                while (after < end && isspace((unsigned char)*after)) after++;

                if (word_len >= 2 && word_len < MAX_IDENTIFIER_LEN) {
                    /* Classify identifier */
                    identifier_t* ident = &identifiers[count];
                    memcpy(ident->name, word_start, word_len);
                    ident->name[word_len] = '\0';

                    /* Skip common language keywords */
                    bool is_keyword = false;
                    static const char* keywords[] = {
                        "if", "else", "for", "while", "switch", "case", "return",
                        "break", "continue", "goto", "sizeof", "typedef", "struct",
                        "union", "enum", "static", "const", "volatile", "extern",
                        "inline", "void", "int", "char", "float", "double", "long",
                        "short", "unsigned", "signed", "bool", "true", "false",
                        "NULL", "nullptr", "class", "public", "private", "protected",
                        "virtual", "override", "final", "new", "delete", "this",
                        "func", "var", "let", "def", "fn", "pub", "mut", "impl",
                        NULL
                    };
                    for (size_t i = 0; keywords[i]; i++) {
                        if (strcmp(ident->name, keywords[i]) == 0) {
                            is_keyword = true;
                            break;
                        }
                    }

                    if (!is_keyword) {
                        if (after < end && *after == '(') {
                            ident->kind = IDENT_FUNCTION;
                            count++;
                        } else if (isupper((unsigned char)ident->name[0]) &&
                                   strchr(ident->name, '_') == NULL) {
                            /* CamelCase type name */
                            ident->kind = IDENT_TYPE;
                            count++;
                        } else if (ident->name[0] >= 'A' && ident->name[0] <= 'Z') {
                            /* Check if ALL_CAPS (constant) */
                            bool all_caps = true;
                            for (size_t i = 0; ident->name[i]; i++) {
                                if (ident->name[i] != '_' && !isupper((unsigned char)ident->name[i]) &&
                                    !isdigit((unsigned char)ident->name[i])) {
                                    all_caps = false;
                                    break;
                                }
                            }
                            if (all_caps && word_len >= 3) {
                                ident->kind = IDENT_CONSTANT;
                                count++;
                            }
                        }
                    }
                }
                word_start = NULL;
            }
            p++;
        }
    }

    return count;
}

size_t extract_file_paths(const char* text, size_t text_len,
                         char paths[][MAX_FILE_PATH_LEN], size_t max_paths) {
    if (!text || !paths || max_paths == 0) return 0;

    size_t count = 0;
    const char* p = text;
    const char* end = text + text_len;

    while (p < end && count < max_paths) {
        /* Look for path-like patterns */
        const char* path_start = NULL;

        /* Absolute path starting with / */
        if (*p == '/' && p + 1 < end && (isalnum((unsigned char)p[1]) || p[1] == '_')) {
            path_start = p;
        }
        /* Relative path starting with ./ or ../ */
        else if (*p == '.' && p + 1 < end && (p[1] == '/' || (p[1] == '.' && p + 2 < end && p[2] == '/'))) {
            path_start = p;
        }
        /* Path with directory separator in middle (src/foo/bar.c) */
        else if (isalnum((unsigned char)*p)) {
            const char* look = p;
            bool has_slash = false;
            bool has_ext = false;
            while (look < end && (isalnum((unsigned char)*look) || *look == '_' ||
                                  *look == '/' || *look == '.' || *look == '-')) {
                if (*look == '/') has_slash = true;
                if (*look == '.' && look + 1 < end && isalpha((unsigned char)look[1])) {
                    /* Looks like extension */
                    const char* ext = look + 1;
                    while (ext < end && isalpha((unsigned char)*ext)) ext++;
                    if (ext - look <= 5) has_ext = true;  /* Extension length check */
                }
                look++;
            }
            if (has_slash && has_ext && look - p >= 5) {
                path_start = p;
            }
        }

        if (path_start) {
            /* Find end of path */
            const char* path_end = path_start;
            while (path_end < end && (isalnum((unsigned char)*path_end) || *path_end == '_' ||
                                      *path_end == '/' || *path_end == '.' || *path_end == '-' ||
                                      *path_end == '+')) {
                path_end++;
            }

            size_t path_len = path_end - path_start;
            if (path_len >= 3 && path_len < MAX_FILE_PATH_LEN) {
                /* Must contain a slash or extension to be a path */
                bool valid = false;
                for (size_t i = 0; i < path_len; i++) {
                    if (path_start[i] == '/') {
                        valid = true;
                        break;
                    }
                }

                if (valid) {
                    /* Check for duplicate */
                    bool dup = false;
                    for (size_t i = 0; i < count; i++) {
                        if (strncmp(paths[i], path_start, path_len) == 0 &&
                            paths[i][path_len] == '\0') {
                            dup = true;
                            break;
                        }
                    }

                    if (!dup) {
                        memcpy(paths[count], path_start, path_len);
                        paths[count][path_len] = '\0';
                        count++;
                    }
                }
            }

            p = path_end;
        } else {
            p++;
        }
    }

    return count;
}

size_t tokenize_text(const char* text, size_t text_len,
                    char** tokens, size_t max_tokens, size_t token_len) {
    if (!text || !tokens || max_tokens == 0) return 0;

    size_t count = 0;
    const char* p = text;
    const char* end = text + text_len;

    while (p < end && count < max_tokens) {
        /* Skip non-word characters */
        while (p < end && !is_word_char(*p)) p++;
        if (p >= end) break;

        /* Extract word */
        const char* word_start = p;
        while (p < end && is_word_char(*p)) p++;
        size_t word_len = p - word_start;

        if (word_len >= 1 && word_len < token_len) {
            tokens[count] = malloc(token_len);
            if (tokens[count]) {
                lowercase_copy(tokens[count], word_start, word_len + 1);
                tokens[count][word_len] = '\0';
                count++;
            }
        }
    }

    return count;
}
