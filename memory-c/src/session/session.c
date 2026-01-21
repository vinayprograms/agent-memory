/*
 * Memory Service - Session Metadata Management Implementation
 */

#include "session.h"
#include "../util/log.h"
#include "../util/time.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Session entry in hash table */
typedef struct session_entry {
    session_metadata_t metadata;
    struct session_entry* next;
} session_entry_t;

#define SESSION_HASH_SIZE 1024

struct session_manager {
    session_entry_t* sessions[SESSION_HASH_SIZE];
    size_t session_count;
    uint64_t sequence_counter;
    keyword_extractor_t* extractor;
    pthread_mutex_t lock;
};

/* Simple hash for session ID */
static uint32_t hash_session_id(const char* id) {
    uint32_t h = 5381;
    while (*id) {
        h = ((h << 5) + h) + (uint8_t)*id++;
    }
    return h % SESSION_HASH_SIZE;
}

mem_error_t session_manager_create(session_manager_t** manager) {
    if (!manager) return MEM_ERR_INVALID_ARG;

    session_manager_t* m = calloc(1, sizeof(session_manager_t));
    if (!m) return MEM_ERR_NOMEM;

    /* Create keyword extractor */
    mem_error_t err = keyword_extractor_create(&m->extractor);
    if (err != MEM_OK) {
        free(m);
        return err;
    }

    pthread_mutex_init(&m->lock, NULL);
    *manager = m;
    return MEM_OK;
}

void session_manager_destroy(session_manager_t* manager) {
    if (!manager) return;

    /* Free all session entries */
    for (size_t i = 0; i < SESSION_HASH_SIZE; i++) {
        session_entry_t* entry = manager->sessions[i];
        while (entry) {
            session_entry_t* next = entry->next;
            free(entry);
            entry = next;
        }
    }

    keyword_extractor_destroy(manager->extractor);
    pthread_mutex_destroy(&manager->lock);
    free(manager);
}

/* Find session entry (internal, must hold lock) */
static session_entry_t* find_session(const session_manager_t* manager,
                                     const char* session_id) {
    uint32_t h = hash_session_id(session_id);
    session_entry_t* entry = manager->sessions[h];

    while (entry) {
        if (strcmp(entry->metadata.session_id, session_id) == 0) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

mem_error_t session_register(session_manager_t* manager,
                            const char* session_id,
                            const char* agent_id,
                            node_id_t root_node_id) {
    if (!manager || !session_id || !agent_id) return MEM_ERR_INVALID_ARG;

    pthread_mutex_lock(&manager->lock);

    /* Check if already exists */
    if (find_session(manager, session_id)) {
        pthread_mutex_unlock(&manager->lock);
        return MEM_ERR_EXISTS;
    }

    /* Create new entry */
    session_entry_t* entry = calloc(1, sizeof(session_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&manager->lock);
        return MEM_ERR_NOMEM;
    }

    /* Initialize metadata */
    snprintf(entry->metadata.session_id, MAX_SESSION_ID_LEN, "%s", session_id);
    snprintf(entry->metadata.agent_id, MAX_AGENT_ID_LEN, "%s", agent_id);
    entry->metadata.root_node_id = root_node_id;

    timestamp_ns_t now = time_now_ns();
    entry->metadata.created_at = now;
    entry->metadata.last_active_at = now;
    entry->metadata.sequence_num = ++manager->sequence_counter;

    /* Insert into hash table */
    uint32_t h = hash_session_id(session_id);
    entry->next = manager->sessions[h];
    manager->sessions[h] = entry;
    manager->session_count++;

    LOG_DEBUG("Session registered: %s (agent=%s, root=%u)",
              session_id, agent_id, root_node_id);

    pthread_mutex_unlock(&manager->lock);
    return MEM_OK;
}

mem_error_t session_update_content(session_manager_t* manager,
                                   const char* session_id,
                                   const char* content, size_t content_len) {
    if (!manager || !session_id || !content) return MEM_ERR_INVALID_ARG;

    pthread_mutex_lock(&manager->lock);

    session_entry_t* entry = find_session(manager, session_id);
    if (!entry) {
        pthread_mutex_unlock(&manager->lock);
        return MEM_ERR_NOT_FOUND;
    }

    /* Extract keywords from new content */
    extraction_result_t result;
    mem_error_t err = extract_keywords(manager->extractor, content, content_len, &result);
    if (err != MEM_OK) {
        pthread_mutex_unlock(&manager->lock);
        return err;
    }

    /* Merge new keywords with existing (keep top scoring) */
    /* Simple approach: add new keywords if they score higher */
    for (size_t i = 0; i < result.keyword_count && i < MAX_KEYWORDS; i++) {
        bool found = false;
        for (size_t j = 0; j < entry->metadata.keyword_count; j++) {
            if (strcmp(entry->metadata.keywords[j].word, result.keywords[i].word) == 0) {
                /* Update score if higher */
                if (result.keywords[i].score > entry->metadata.keywords[j].score) {
                    entry->metadata.keywords[j].score = result.keywords[i].score;
                }
                found = true;
                break;
            }
        }
        if (!found && entry->metadata.keyword_count < MAX_KEYWORDS) {
            entry->metadata.keywords[entry->metadata.keyword_count++] = result.keywords[i];
        }
    }

    /* Merge identifiers */
    for (size_t i = 0; i < result.identifier_count && entry->metadata.identifier_count < MAX_IDENTIFIERS; i++) {
        bool found = false;
        for (size_t j = 0; j < entry->metadata.identifier_count; j++) {
            if (strcmp(entry->metadata.identifiers[j].name, result.identifiers[i].name) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            entry->metadata.identifiers[entry->metadata.identifier_count++] = result.identifiers[i];
        }
    }

    /* Merge file paths */
    for (size_t i = 0; i < result.file_path_count && entry->metadata.file_count < MAX_FILE_PATHS; i++) {
        bool found = false;
        for (size_t j = 0; j < entry->metadata.file_count; j++) {
            if (strcmp(entry->metadata.files_touched[j], result.file_paths[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            snprintf(entry->metadata.files_touched[entry->metadata.file_count++],
                    MAX_FILE_PATH_LEN, "%s", result.file_paths[i]);
        }
    }

    /* Update IDF statistics */
    keyword_extractor_update_idf(manager->extractor, content, content_len);

    /* Update timestamps */
    entry->metadata.last_active_at = time_now_ns();
    entry->metadata.sequence_num = ++manager->sequence_counter;

    pthread_mutex_unlock(&manager->lock);
    return MEM_OK;
}

mem_error_t session_set_title(session_manager_t* manager,
                             const char* session_id,
                             const char* title) {
    if (!manager || !session_id || !title) return MEM_ERR_INVALID_ARG;

    pthread_mutex_lock(&manager->lock);

    session_entry_t* entry = find_session(manager, session_id);
    if (!entry) {
        pthread_mutex_unlock(&manager->lock);
        return MEM_ERR_NOT_FOUND;
    }

    snprintf(entry->metadata.title, MAX_TITLE_LEN, "%s", title);
    entry->metadata.title_generated = true;
    entry->metadata.last_active_at = time_now_ns();

    pthread_mutex_unlock(&manager->lock);
    return MEM_OK;
}

mem_error_t session_get_metadata(session_manager_t* manager,
                                const char* session_id,
                                session_metadata_t* metadata) {
    if (!manager || !session_id || !metadata) return MEM_ERR_INVALID_ARG;

    pthread_mutex_lock(&manager->lock);

    session_entry_t* entry = find_session(manager, session_id);
    if (!entry) {
        pthread_mutex_unlock(&manager->lock);
        return MEM_ERR_NOT_FOUND;
    }

    *metadata = entry->metadata;

    pthread_mutex_unlock(&manager->lock);
    return MEM_OK;
}

size_t session_list(session_manager_t* manager,
                   const char* agent_id,
                   const char* keyword,
                   timestamp_ns_t since,
                   char results[][MAX_SESSION_ID_LEN],
                   size_t max_results) {
    if (!manager || !results || max_results == 0) return 0;

    pthread_mutex_lock(&manager->lock);

    size_t count = 0;

    for (size_t i = 0; i < SESSION_HASH_SIZE && count < max_results; i++) {
        session_entry_t* entry = manager->sessions[i];
        while (entry && count < max_results) {
            bool match = true;

            /* Filter by agent */
            if (agent_id && strcmp(entry->metadata.agent_id, agent_id) != 0) {
                match = false;
            }

            /* Filter by timestamp */
            if (since > 0 && entry->metadata.created_at < since) {
                match = false;
            }

            /* Filter by keyword */
            if (keyword && match) {
                bool has_keyword = false;
                for (size_t j = 0; j < entry->metadata.keyword_count; j++) {
                    if (strstr(entry->metadata.keywords[j].word, keyword)) {
                        has_keyword = true;
                        break;
                    }
                }
                if (!has_keyword) match = false;
            }

            if (match) {
                snprintf(results[count++], MAX_SESSION_ID_LEN, "%s", entry->metadata.session_id);
            }

            entry = entry->next;
        }
    }

    pthread_mutex_unlock(&manager->lock);
    return count;
}

size_t session_find_by_keyword(session_manager_t* manager,
                              const char* keyword,
                              char results[][MAX_SESSION_ID_LEN],
                              size_t max_results) {
    return session_list(manager, NULL, keyword, 0, results, max_results);
}

size_t session_find_by_file(session_manager_t* manager,
                           const char* file_path,
                           char results[][MAX_SESSION_ID_LEN],
                           size_t max_results) {
    if (!manager || !file_path || !results || max_results == 0) return 0;

    pthread_mutex_lock(&manager->lock);

    size_t count = 0;

    for (size_t i = 0; i < SESSION_HASH_SIZE && count < max_results; i++) {
        session_entry_t* entry = manager->sessions[i];
        while (entry && count < max_results) {
            for (size_t j = 0; j < entry->metadata.file_count; j++) {
                if (strstr(entry->metadata.files_touched[j], file_path)) {
                    snprintf(results[count++], MAX_SESSION_ID_LEN, "%s", entry->metadata.session_id);
                    break;
                }
            }
            entry = entry->next;
        }
    }

    pthread_mutex_unlock(&manager->lock);
    return count;
}

mem_error_t session_update_stats(session_manager_t* manager,
                                const char* session_id,
                                int messages_delta,
                                int blocks_delta,
                                int statements_delta) {
    if (!manager || !session_id) return MEM_ERR_INVALID_ARG;

    pthread_mutex_lock(&manager->lock);

    session_entry_t* entry = find_session(manager, session_id);
    if (!entry) {
        pthread_mutex_unlock(&manager->lock);
        return MEM_ERR_NOT_FOUND;
    }

    entry->metadata.message_count += messages_delta;
    entry->metadata.block_count += blocks_delta;
    entry->metadata.statement_count += statements_delta;
    entry->metadata.last_active_at = time_now_ns();

    pthread_mutex_unlock(&manager->lock);
    return MEM_OK;
}

size_t session_count(const session_manager_t* manager) {
    if (!manager) return 0;
    return manager->session_count;
}

uint64_t session_get_next_sequence(session_manager_t* manager) {
    if (!manager) return 0;

    pthread_mutex_lock(&manager->lock);
    uint64_t seq = ++manager->sequence_counter;
    pthread_mutex_unlock(&manager->lock);

    return seq;
}
