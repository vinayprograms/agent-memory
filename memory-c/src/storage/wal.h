/*
 * Memory Service - Write-Ahead Log
 *
 * WAL provides durability for mmap'd data structures.
 * Operations are first written to the log, then applied.
 * On crash, the WAL is replayed to recover consistent state.
 */

#ifndef MEMORY_SERVICE_WAL_H
#define MEMORY_SERVICE_WAL_H

#include "../core/arena.h"
#include "../../include/types.h"
#include "../../include/error.h"
#include <stdint.h>
#include <stdbool.h>

/* WAL operation types */
typedef enum {
    WAL_OP_NONE = 0,
    WAL_OP_NODE_INSERT,         /* Insert new node */
    WAL_OP_NODE_UPDATE,         /* Update existing node */
    WAL_OP_NODE_DELETE,         /* Delete node */
    WAL_OP_EMBEDDING_SET,       /* Set embedding vector */
    WAL_OP_RELATION_SET,        /* Set relationship */
    WAL_OP_INDEX_INSERT,        /* Insert into index */
    WAL_OP_INDEX_DELETE,        /* Delete from index */
    WAL_OP_SESSION_CREATE,      /* Create session */
    WAL_OP_SESSION_UPDATE,      /* Update session metadata */
    WAL_OP_CHECKPOINT,          /* Checkpoint marker */
    WAL_OP_COMMIT,              /* Transaction commit */
} wal_op_type_t;

/* WAL entry header */
typedef struct {
    uint32_t        magic;          /* Magic number for validation */
    uint32_t        crc32;          /* CRC32 of data */
    uint64_t        sequence;       /* Monotonic sequence number */
    uint64_t        timestamp_ns;   /* Wall-clock timestamp */
    wal_op_type_t   op_type;        /* Operation type */
    uint32_t        data_len;       /* Length of data following header */
} wal_entry_header_t;

#define WAL_MAGIC 0x57414C30        /* "WAL0" */
#define WAL_HEADER_SIZE sizeof(wal_entry_header_t)

/* WAL state */
typedef struct wal {
    int             fd;             /* File descriptor */
    char*           path;           /* File path */
    size_t          size;           /* Current file size */
    size_t          max_size;       /* Max size before checkpoint */
    uint64_t        sequence;       /* Next sequence number */
    uint64_t        checkpoint_seq; /* Last checkpoint sequence */
    bool            sync_on_write;  /* fsync after each write */
    void*           write_buf;      /* Write buffer */
    size_t          write_buf_size;
} wal_t;

/* WAL replay callback */
typedef mem_error_t (*wal_replay_fn)(wal_op_type_t op, const void* data,
                                      size_t len, void* user_data);

/* Create or open WAL */
mem_error_t wal_create(wal_t** wal, const char* path, size_t max_size);

/* Open existing WAL for replay */
mem_error_t wal_open(wal_t** wal, const char* path);

/* Append entry to WAL */
mem_error_t wal_append(wal_t* wal, wal_op_type_t op,
                       const void* data, size_t len);

/* Sync WAL to disk */
mem_error_t wal_sync(wal_t* wal);

/* Write checkpoint marker */
mem_error_t wal_checkpoint(wal_t* wal);

/* Truncate WAL (after checkpoint) */
mem_error_t wal_truncate(wal_t* wal);

/* Replay WAL from last checkpoint */
mem_error_t wal_replay(wal_t* wal, wal_replay_fn callback, void* user_data);

/* Replay WAL from specific sequence */
mem_error_t wal_replay_from(wal_t* wal, uint64_t from_seq,
                            wal_replay_fn callback, void* user_data);

/* Get current sequence number */
uint64_t wal_sequence(const wal_t* wal);

/* Get size */
size_t wal_size(const wal_t* wal);

/* Check if checkpoint needed */
bool wal_needs_checkpoint(const wal_t* wal);

/* Close WAL */
void wal_close(wal_t* wal);

/* Operation data structures */

/* Node insert/update data */
typedef struct {
    node_id_t       node_id;
    hierarchy_level_t level;
    node_id_t       parent_id;
    uint32_t        embedding_idx;
    char            agent_id[MAX_AGENT_ID_LEN];
    char            session_id[MAX_SESSION_ID_LEN];
} wal_node_data_t;

/* Embedding set data */
typedef struct {
    hierarchy_level_t level;
    uint32_t        embedding_idx;
    float           values[EMBEDDING_DIM];
} wal_embedding_data_t;

/* Relation set data */
typedef struct {
    node_id_t       node_id;
    node_id_t       parent_id;
    node_id_t       first_child_id;
    node_id_t       next_sibling_id;
} wal_relation_data_t;

/* Session create data */
typedef struct {
    char            session_id[MAX_SESSION_ID_LEN];
    char            agent_id[MAX_AGENT_ID_LEN];
    node_id_t       root_node_id;
    timestamp_ns_t  created_at;
} wal_session_data_t;

#endif /* MEMORY_SERVICE_WAL_H */
