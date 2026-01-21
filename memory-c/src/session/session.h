/*
 * Memory Service - Session Metadata Management
 *
 * Manages session metadata including:
 * - Keywords (auto-extracted)
 * - Identifiers (parsed from code)
 * - File paths touched
 * - Timestamps and sequence numbers
 * - LLM-generated title (async)
 */

#ifndef MEMORY_SERVICE_SESSION_H
#define MEMORY_SERVICE_SESSION_H

#include "../../include/types.h"
#include "../../include/error.h"
#include "keywords.h"

#include <stddef.h>
#include <stdbool.h>

/* Maximum sessions tracked */
#define MAX_SESSIONS 10000
#define MAX_TITLE_LEN 256

/* Session metadata */
typedef struct {
    char            session_id[MAX_SESSION_ID_LEN];
    char            agent_id[MAX_AGENT_ID_LEN];
    node_id_t       root_node_id;

    /* Timestamps */
    timestamp_ns_t  created_at;
    timestamp_ns_t  last_active_at;
    uint64_t        sequence_num;       /* Monotonic counter for ordering */

    /* Extracted metadata */
    keyword_t       keywords[MAX_KEYWORDS];
    size_t          keyword_count;

    identifier_t    identifiers[MAX_IDENTIFIERS];
    size_t          identifier_count;

    char            files_touched[MAX_FILE_PATHS][MAX_FILE_PATH_LEN];
    size_t          file_count;

    /* Title (may be NULL if not yet generated) */
    char            title[MAX_TITLE_LEN];
    bool            title_generated;

    /* Statistics */
    size_t          message_count;
    size_t          block_count;
    size_t          statement_count;
} session_metadata_t;

/* Session manager context */
typedef struct session_manager session_manager_t;

/*
 * Create a session manager
 *
 * @param manager   Output manager pointer
 * @return          MEM_OK on success
 */
mem_error_t session_manager_create(session_manager_t** manager);

/*
 * Destroy session manager
 */
void session_manager_destroy(session_manager_t* manager);

/*
 * Register a new session
 *
 * Creates session metadata entry with initial timestamps.
 *
 * @param manager       Session manager
 * @param session_id    Session identifier string
 * @param agent_id      Agent identifier string
 * @param root_node_id  Root node ID in hierarchy
 * @return              MEM_OK on success
 */
mem_error_t session_register(session_manager_t* manager,
                            const char* session_id,
                            const char* agent_id,
                            node_id_t root_node_id);

/*
 * Update session with new content
 *
 * Extracts keywords and updates timestamps.
 *
 * @param manager       Session manager
 * @param session_id    Session identifier
 * @param content       New content text
 * @param content_len   Content length
 * @return              MEM_OK on success
 */
mem_error_t session_update_content(session_manager_t* manager,
                                   const char* session_id,
                                   const char* content, size_t content_len);

/*
 * Set session title
 *
 * @param manager       Session manager
 * @param session_id    Session identifier
 * @param title         Generated title
 * @return              MEM_OK on success
 */
mem_error_t session_set_title(session_manager_t* manager,
                             const char* session_id,
                             const char* title);

/*
 * Get session metadata
 *
 * Note: Takes non-const manager as it acquires internal lock for thread safety.
 *
 * @param manager       Session manager
 * @param session_id    Session identifier
 * @param metadata      Output metadata (copied)
 * @return              MEM_OK on success, MEM_ERROR_NOT_FOUND if not found
 */
mem_error_t session_get_metadata(session_manager_t* manager,
                                const char* session_id,
                                session_metadata_t* metadata);

/*
 * List sessions matching criteria
 *
 * Note: Takes non-const manager as it acquires internal lock for thread safety.
 *
 * @param manager       Session manager
 * @param agent_id      Filter by agent ID (NULL for all)
 * @param keyword       Filter by keyword (NULL for all)
 * @param since         Filter by created_at >= since (0 for all)
 * @param results       Output array of session IDs
 * @param max_results   Maximum results
 * @return              Number of matching sessions
 */
size_t session_list(session_manager_t* manager,
                   const char* agent_id,
                   const char* keyword,
                   timestamp_ns_t since,
                   char results[][MAX_SESSION_ID_LEN],
                   size_t max_results);

/*
 * Find sessions by keyword
 *
 * Note: Takes non-const manager as it acquires internal lock for thread safety.
 *
 * @param manager       Session manager
 * @param keyword       Keyword to search
 * @param results       Output array of session IDs
 * @param max_results   Maximum results
 * @return              Number of matching sessions
 */
size_t session_find_by_keyword(session_manager_t* manager,
                              const char* keyword,
                              char results[][MAX_SESSION_ID_LEN],
                              size_t max_results);

/*
 * Find sessions by file path
 *
 * Note: Takes non-const manager as it acquires internal lock for thread safety.
 *
 * @param manager       Session manager
 * @param file_path     File path (partial match)
 * @param results       Output array of session IDs
 * @param max_results   Maximum results
 * @return              Number of matching sessions
 */
size_t session_find_by_file(session_manager_t* manager,
                           const char* file_path,
                           char results[][MAX_SESSION_ID_LEN],
                           size_t max_results);

/*
 * Update session statistics
 *
 * @param manager           Session manager
 * @param session_id        Session identifier
 * @param messages_delta    Change in message count
 * @param blocks_delta      Change in block count
 * @param statements_delta  Change in statement count
 */
mem_error_t session_update_stats(session_manager_t* manager,
                                const char* session_id,
                                int messages_delta,
                                int blocks_delta,
                                int statements_delta);

/*
 * Get session count
 */
size_t session_count(const session_manager_t* manager);

/*
 * Get total sequence number (for ordering)
 */
uint64_t session_get_next_sequence(session_manager_t* manager);

#endif /* MEMORY_SERVICE_SESSION_H */
