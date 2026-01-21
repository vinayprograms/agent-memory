/*
 * Memory Service - Text Decomposition
 *
 * Splits text content into hierarchical chunks:
 * - Blocks: code fences, paragraphs (double newline separated)
 * - Statements: sentences within blocks
 *
 * Used by the store handler to automatically decompose messages
 * into searchable blocks and statements.
 */

#ifndef MEMORY_SERVICE_TEXT_H
#define MEMORY_SERVICE_TEXT_H

#include <stddef.h>
#include <stdbool.h>

/* Maximum blocks per message */
#define MAX_BLOCKS 64

/* Maximum statements per block */
#define MAX_STATEMENTS 128

/* Block types */
typedef enum {
    BLOCK_TEXT,      /* Regular paragraph text */
    BLOCK_CODE,      /* Code fence (```...```) */
    BLOCK_LIST,      /* List items */
} block_type_t;

/* A text span (pointer into original content) */
typedef struct {
    const char* start;
    size_t      len;
} text_span_t;

/* A block with its type and content */
typedef struct {
    block_type_t type;
    text_span_t  span;
    char         lang[32];  /* Language hint for code blocks */
} text_block_t;

/* Split content into blocks
 *
 * Splits on:
 * - Code fences (```lang ... ```)
 * - Double newlines (paragraph breaks)
 *
 * Returns number of blocks found (up to max_blocks).
 * Blocks array must be pre-allocated.
 */
size_t text_split_blocks(const char* content, size_t len,
                         text_block_t* blocks, size_t max_blocks);

/* Split a block into statements (sentences)
 *
 * For text blocks:
 * - Splits on sentence boundaries (. ! ?)
 * - Handles common abbreviations (Dr., Mr., etc., e.g., i.e.)
 * - Preserves sentence-ending punctuation with the sentence
 *
 * For code blocks:
 * - Splits on newlines (each line is a statement)
 *
 * Returns number of statements found (up to max_stmts).
 * Statements array must be pre-allocated.
 */
size_t text_split_statements(const text_block_t* block,
                             text_span_t* statements, size_t max_stmts);

/* Trim whitespace from span (returns new span, doesn't modify original) */
text_span_t text_trim(text_span_t span);

/* Check if span is empty or whitespace-only */
bool text_is_empty(text_span_t span);

#endif /* MEMORY_SERVICE_TEXT_H */
