/*
 * Memory Service - Text Decomposition Implementation
 */

#include "text.h"
#include <string.h>
#include <ctype.h>

/* Check if position starts a code fence (``` or ~~~) */
static bool is_code_fence(const char* p, size_t remaining) {
    if (remaining < 3) return false;
    return (p[0] == '`' && p[1] == '`' && p[2] == '`') ||
           (p[0] == '~' && p[1] == '~' && p[2] == '~');
}

/* Find end of code fence, returns pointer past closing fence or end of content */
static const char* find_code_fence_end(const char* start, const char* end, char fence_char) {
    const char* p = start;

    /* Skip to end of opening fence line */
    while (p < end && *p != '\n') p++;
    if (p < end) p++;  /* Skip newline */

    /* Find closing fence */
    while (p < end) {
        /* Check for closing fence at start of line */
        if (p[0] == fence_char && p + 2 < end &&
            p[1] == fence_char && p[2] == fence_char) {
            /* Skip past closing fence */
            p += 3;
            while (p < end && *p != '\n') p++;
            if (p < end) p++;  /* Skip newline */
            return p;
        }
        /* Move to next line */
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }

    return end;  /* Unclosed fence - take rest of content */
}

/* Extract language hint from code fence opening */
static void extract_lang(const char* fence_start, size_t fence_len, char* lang, size_t lang_size) {
    lang[0] = '\0';

    /* Skip fence characters */
    const char* p = fence_start + 3;
    const char* end = fence_start + fence_len;

    /* Skip whitespace */
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    /* Extract language identifier */
    size_t i = 0;
    while (p < end && i < lang_size - 1 && *p != '\n' && *p != ' ' && *p != '\t') {
        lang[i++] = *p++;
    }
    lang[i] = '\0';
}

/* Check if we're at a paragraph break (double newline) */
static bool is_para_break(const char* p, size_t remaining) {
    if (remaining < 2) return false;

    /* Look for \n\n or \n followed by whitespace-only line */
    if (p[0] == '\n') {
        if (p[1] == '\n') return true;

        /* Check for blank line (whitespace only) */
        const char* scan = p + 1;
        const char* end = p + remaining;
        while (scan < end && (*scan == ' ' || *scan == '\t')) scan++;
        if (scan < end && *scan == '\n') return true;
    }

    return false;
}

/* Skip paragraph break, return pointer to start of next content */
static const char* skip_para_break(const char* p, const char* end) {
    while (p < end && (*p == '\n' || *p == ' ' || *p == '\t')) {
        /* Stop at start of non-blank content */
        if (*p == '\n') {
            const char* line_start = p + 1;
            const char* scan = line_start;
            while (scan < end && (*scan == ' ' || *scan == '\t')) scan++;
            if (scan < end && *scan != '\n') {
                return line_start;
            }
        }
        p++;
    }
    return p;
}

/*
 * Split content into blocks
 */
size_t text_split_blocks(const char* content, size_t len,
                         text_block_t* blocks, size_t max_blocks) {
    if (!content || len == 0 || !blocks || max_blocks == 0) {
        return 0;
    }

    size_t count = 0;
    const char* p = content;
    const char* end = content + len;
    const char* block_start = content;

    /* Skip leading whitespace */
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
    block_start = p;

    while (p < end && count < max_blocks) {
        /* Check for code fence */
        if (is_code_fence(p, end - p)) {
            /* Save any text before the fence as a text block */
            if (p > block_start) {
                text_span_t span = { block_start, (size_t)(p - block_start) };
                span = text_trim(span);
                if (!text_is_empty(span)) {
                    blocks[count].type = BLOCK_TEXT;
                    blocks[count].span = span;
                    blocks[count].lang[0] = '\0';
                    count++;
                    if (count >= max_blocks) break;
                }
            }

            /* Parse code block */
            char fence_char = *p;
            const char* fence_start = p;
            const char* fence_end = find_code_fence_end(p, end, fence_char);

            /* Extract content between fences */
            const char* code_start = p;
            while (code_start < fence_end && *code_start != '\n') code_start++;
            if (code_start < fence_end) code_start++;  /* Skip opening fence line */

            const char* code_end = fence_end;
            /* Back up past closing fence */
            if (code_end > code_start) {
                code_end--;
                while (code_end > code_start && *code_end != '\n') code_end--;
            }

            blocks[count].type = BLOCK_CODE;
            blocks[count].span.start = code_start;
            blocks[count].span.len = (size_t)(code_end - code_start);
            extract_lang(fence_start, fence_end - fence_start,
                        blocks[count].lang, sizeof(blocks[count].lang));
            count++;

            p = fence_end;
            block_start = p;
            continue;
        }

        /* Check for paragraph break */
        if (is_para_break(p, end - p)) {
            /* Save current block */
            if (p > block_start) {
                text_span_t span = { block_start, (size_t)(p - block_start) };
                span = text_trim(span);
                if (!text_is_empty(span)) {
                    blocks[count].type = BLOCK_TEXT;
                    blocks[count].span = span;
                    blocks[count].lang[0] = '\0';
                    count++;
                    if (count >= max_blocks) break;
                }
            }

            p = skip_para_break(p, end);
            block_start = p;
            continue;
        }

        p++;
    }

    /* Save final block */
    if (p > block_start && count < max_blocks) {
        text_span_t span = { block_start, (size_t)(p - block_start) };
        span = text_trim(span);
        if (!text_is_empty(span)) {
            blocks[count].type = BLOCK_TEXT;
            blocks[count].span = span;
            blocks[count].lang[0] = '\0';
            count++;
        }
    }

    return count;
}

/* Common abbreviations that don't end sentences */
static const char* ABBREVIATIONS[] = {
    "Dr.", "Mr.", "Mrs.", "Ms.", "Prof.", "Sr.", "Jr.",
    "vs.", "etc.", "e.g.", "i.e.", "viz.", "cf.",
    "Inc.", "Ltd.", "Corp.", "Co.",
    "Jan.", "Feb.", "Mar.", "Apr.", "Jun.", "Jul.", "Aug.", "Sep.", "Sept.", "Oct.", "Nov.", "Dec.",
    "St.", "Ave.", "Blvd.", "Rd.",
    "Fig.", "fig.", "Eq.", "eq.",
    "p.", "pp.", "vol.", "ch.", "sec.",
    NULL
};

/* Check if the text ending at period is an abbreviation */
static bool is_abbreviation(const char* start, const char* period) {
    /* Find start of word before period */
    const char* word_start = period;
    while (word_start > start && isalpha((unsigned char)word_start[-1])) {
        word_start--;
    }

    size_t word_len = period - word_start + 1;  /* Include period */

    for (const char** abbr = ABBREVIATIONS; *abbr; abbr++) {
        size_t abbr_len = strlen(*abbr);
        if (word_len == abbr_len) {
            bool match = true;
            for (size_t i = 0; i < abbr_len && match; i++) {
                char c1 = word_start[i];
                char c2 = (*abbr)[i];
                /* Case-insensitive for first char */
                if (i == 0) {
                    if (tolower((unsigned char)c1) != tolower((unsigned char)c2)) match = false;
                } else {
                    if (c1 != c2) match = false;
                }
            }
            if (match) return true;
        }
    }

    /* Single letter followed by period (likely initial) */
    if (word_len == 2 && isupper((unsigned char)*word_start)) {
        return true;
    }

    return false;
}

/* Check if this looks like end of sentence */
static bool is_sentence_end(const char* start, const char* p, const char* end) {
    char c = *p;

    /* Must be sentence-ending punctuation */
    if (c != '.' && c != '!' && c != '?') return false;

    /* Check what follows */
    const char* next = p + 1;

    /* End of content is sentence end */
    if (next >= end) return true;

    /* Period followed by quote then space/newline */
    if (c == '.' && (*next == '"' || *next == '\'' || *next == ')')) {
        next++;
        if (next >= end) return true;
    }

    /* Must be followed by space, newline, or end */
    if (*next != ' ' && *next != '\n' && *next != '\t') return false;

    /* Skip whitespace */
    while (next < end && (*next == ' ' || *next == '\t')) next++;

    /* Newline or end is sentence end */
    if (next >= end || *next == '\n') return true;

    /* Next char should be uppercase (new sentence) or quote/paren */
    if (isupper((unsigned char)*next) || *next == '"' || *next == '\'' || *next == '(') {
        /* But check for abbreviation */
        if (c == '.' && is_abbreviation(start, p)) {
            return false;
        }
        return true;
    }

    return false;
}

/*
 * Split block into statements
 */
size_t text_split_statements(const text_block_t* block,
                             text_span_t* statements, size_t max_stmts) {
    if (!block || !statements || max_stmts == 0) {
        return 0;
    }

    const char* content = block->span.start;
    size_t len = block->span.len;

    if (len == 0) return 0;

    size_t count = 0;
    const char* p = content;
    const char* end = content + len;
    const char* stmt_start = content;

    /* For code blocks, split on newlines */
    if (block->type == BLOCK_CODE) {
        while (p < end && count < max_stmts) {
            if (*p == '\n') {
                if (p > stmt_start) {
                    text_span_t span = { stmt_start, (size_t)(p - stmt_start) };
                    span = text_trim(span);
                    if (!text_is_empty(span)) {
                        statements[count++] = span;
                    }
                }
                stmt_start = p + 1;
            }
            p++;
        }

        /* Final line */
        if (p > stmt_start && count < max_stmts) {
            text_span_t span = { stmt_start, (size_t)(p - stmt_start) };
            span = text_trim(span);
            if (!text_is_empty(span)) {
                statements[count++] = span;
            }
        }

        return count;
    }

    /* For text blocks, split on sentence boundaries */
    while (p < end && count < max_stmts) {
        if (is_sentence_end(stmt_start, p, end)) {
            /* Include the punctuation in the sentence */
            const char* sent_end = p + 1;

            /* Include closing quote if present */
            if (sent_end < end && (*sent_end == '"' || *sent_end == '\'' || *sent_end == ')')) {
                sent_end++;
            }

            text_span_t span = { stmt_start, (size_t)(sent_end - stmt_start) };
            span = text_trim(span);
            if (!text_is_empty(span)) {
                statements[count++] = span;
            }

            /* Skip whitespace to next sentence */
            p = sent_end;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
            stmt_start = p;
            continue;
        }
        p++;
    }

    /* Final statement (may not end with punctuation) */
    if (stmt_start < end && count < max_stmts) {
        text_span_t span = { stmt_start, (size_t)(end - stmt_start) };
        span = text_trim(span);
        if (!text_is_empty(span)) {
            statements[count++] = span;
        }
    }

    return count;
}

/*
 * Trim whitespace from span
 */
text_span_t text_trim(text_span_t span) {
    if (!span.start || span.len == 0) {
        return span;
    }

    const char* start = span.start;
    const char* end = span.start + span.len;

    /* Trim leading */
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }

    /* Trim trailing */
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }

    text_span_t result = { start, (size_t)(end - start) };
    return result;
}

/*
 * Check if span is empty
 */
bool text_is_empty(text_span_t span) {
    if (!span.start || span.len == 0) return true;

    for (size_t i = 0; i < span.len; i++) {
        char c = span.start[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return false;
        }
    }

    return true;
}
