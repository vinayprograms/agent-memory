/*
 * Memory Service - Metadata Storage (LMDB)
 *
 * LMDB-based storage for:
 * - Session metadata (keywords, title, timestamps)
 * - Message text content
 * - Node metadata
 */

#ifndef MEMORY_SERVICE_METADATA_H
#define MEMORY_SERVICE_METADATA_H

#include "../../include/types.h"
#include "../../include/error.h"

#ifdef HAVE_LMDB
#include <lmdb.h>
#endif

/* Metadata store */
typedef struct {
#ifdef HAVE_LMDB
    MDB_env*        env;
    MDB_dbi         sessions_db;    /* session_id -> session_meta_t */
    MDB_dbi         nodes_db;       /* node_id -> node_t */
    MDB_dbi         text_db;        /* node_id -> text content */
#endif
    char*           path;
    size_t          map_size;
    bool            read_only;
} metadata_store_t;

/* Create metadata store */
mem_error_t metadata_create(metadata_store_t** store, const char* path,
                            size_t map_size);

/* Open existing metadata store */
mem_error_t metadata_open(metadata_store_t** store, const char* path,
                          bool read_only);

/* Session operations */
mem_error_t metadata_put_session(metadata_store_t* store, const session_meta_t* session);
mem_error_t metadata_get_session(metadata_store_t* store, const char* session_id,
                                 session_meta_t* session);
mem_error_t metadata_delete_session(metadata_store_t* store, const char* session_id);
mem_error_t metadata_update_session_title(metadata_store_t* store,
                                          const char* session_id, const char* title);

/* Node operations */
mem_error_t metadata_put_node(metadata_store_t* store, const node_t* node);
mem_error_t metadata_get_node(metadata_store_t* store, node_id_t id, node_t* node);
mem_error_t metadata_delete_node(metadata_store_t* store, node_id_t id);

/* Text content operations */
mem_error_t metadata_put_text(metadata_store_t* store, node_id_t id,
                              const char* text, size_t len);
mem_error_t metadata_get_text(metadata_store_t* store, node_id_t id,
                              char** text, size_t* len);
mem_error_t metadata_delete_text(metadata_store_t* store, node_id_t id);

/* Session listing/searching */
typedef bool (*session_iter_fn)(const session_meta_t* session, void* user_data);
mem_error_t metadata_iter_sessions(metadata_store_t* store,
                                   session_iter_fn callback, void* user_data);

/* Stats */
size_t metadata_session_count(metadata_store_t* store);
size_t metadata_node_count(metadata_store_t* store);

/* Sync to disk */
mem_error_t metadata_sync(metadata_store_t* store);

/* Close store */
void metadata_close(metadata_store_t* store);

#endif /* MEMORY_SERVICE_METADATA_H */
