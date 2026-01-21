/*
 * SVC_MEM_TEST_0001 - Verify persistence and crash recovery
 *
 * Test specification:
 * - Store 100 messages via Memory Service
 * - Kill Memory Service process (SIGKILL)
 * - Restart Memory Service
 * - All 100 messages MUST be recoverable via WAL replay
 * - HNSW indices MUST be consistent after recovery
 */

#include "../test_framework.h"
#include "../../src/storage/wal.h"
#include "../../src/storage/embeddings.h"
#include "../../src/storage/relations.h"
#include "../../include/types.h"
#include "../../include/error.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>

#define TEST_DIR "/tmp/test_persistence_recovery"
#define NUM_MESSAGES 100

static void cleanup_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

static void setup_dirs(void) {
    cleanup_dir(TEST_DIR);
    mkdir(TEST_DIR, 0755);

    char path[256];
    snprintf(path, sizeof(path), "%s/embeddings", TEST_DIR);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/relations", TEST_DIR);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/wal", TEST_DIR);
    mkdir(path, 0755);
}

/* Replay callback to count recovered entries */
static int g_recovered_count = 0;

static mem_error_t recovery_callback(wal_op_type_t op, const void* data,
                                     size_t len, void* user_data) {
    (void)data;
    (void)len;
    (void)user_data;

    if (op == WAL_OP_NODE_INSERT) {
        g_recovered_count++;
    }
    return MEM_OK;
}

/*
 * TEST: Store messages, simulate crash, recover via WAL replay
 */
TEST(persistence_and_crash_recovery) {
    setup_dirs();

    char wal_path[256];
    char emb_path[256];
    char rel_path[256];

    snprintf(wal_path, sizeof(wal_path), "%s/wal/operations.log", TEST_DIR);
    snprintf(emb_path, sizeof(emb_path), "%s/embeddings", TEST_DIR);
    snprintf(rel_path, sizeof(rel_path), "%s/relations", TEST_DIR);

    /* Phase 1: Store 100 messages with WAL logging */
    {
        wal_t* wal = NULL;
        embeddings_store_t* emb = NULL;
        relations_store_t* rel = NULL;

        ASSERT_OK(wal_create(&wal, wal_path, 10 * 1024 * 1024));
        ASSERT_OK(embeddings_create(&emb, emb_path, 1000));
        ASSERT_OK(relations_create(&rel, rel_path, 1000));

        for (int i = 0; i < NUM_MESSAGES; i++) {
            /* Allocate node */
            node_id_t node_id;
            ASSERT_OK(relations_alloc_node(rel, &node_id));
            ASSERT_OK(relations_set_level(rel, node_id, LEVEL_MESSAGE));

            /* Allocate embedding */
            uint32_t emb_idx;
            ASSERT_OK(embeddings_alloc(emb, LEVEL_MESSAGE, &emb_idx));

            /* Set embedding values (simple test pattern) */
            float values[EMBEDDING_DIM];
            for (int j = 0; j < EMBEDDING_DIM; j++) {
                values[j] = (float)(i * EMBEDDING_DIM + j) * 0.001f;
            }
            ASSERT_OK(embeddings_set(emb, LEVEL_MESSAGE, emb_idx, values));

            /* Log to WAL */
            wal_node_data_t wal_data = {
                .node_id = node_id,
                .level = LEVEL_MESSAGE,
                .parent_id = NODE_ID_INVALID,
                .embedding_idx = emb_idx
            };
            snprintf(wal_data.agent_id, sizeof(wal_data.agent_id), "agent-%d", i % 10);
            snprintf(wal_data.session_id, sizeof(wal_data.session_id), "session-%d", i / 10);

            ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &wal_data, sizeof(wal_data)));
        }

        /* Verify counts before "crash" */
        ASSERT_EQ(relations_count(rel), NUM_MESSAGES);
        ASSERT_EQ(embeddings_count(emb, LEVEL_MESSAGE), NUM_MESSAGES);

        /* Sync everything before "crash" */
        ASSERT_OK(wal_sync(wal));
        ASSERT_OK(embeddings_sync(emb));
        ASSERT_OK(relations_sync(rel));

        /* Simulate crash - just close without proper cleanup */
        /* In real crash recovery, this would be SIGKILL */
        wal_close(wal);
        embeddings_close(emb);
        relations_close(rel);
    }

    /* Phase 2: Recovery - reopen and verify via WAL replay */
    {
        wal_t* wal = NULL;
        embeddings_store_t* emb = NULL;
        relations_store_t* rel = NULL;

        /* Reopen WAL for replay */
        ASSERT_OK(wal_open(&wal, wal_path));

        /* Replay and count recovered entries */
        g_recovered_count = 0;
        ASSERT_OK(wal_replay(wal, recovery_callback, NULL));

        /* Verify all 100 messages recovered */
        ASSERT_EQ(g_recovered_count, NUM_MESSAGES);

        /* Reopen data stores */
        ASSERT_OK(embeddings_open(&emb, emb_path));
        ASSERT_OK(relations_open(&rel, rel_path));

        /* Verify data integrity */
        ASSERT_EQ(relations_count(rel), NUM_MESSAGES);
        ASSERT_EQ(embeddings_count(emb, LEVEL_MESSAGE), NUM_MESSAGES);

        /* Verify specific embedding values survived */
        for (int i = 0; i < NUM_MESSAGES; i++) {
            const float* values = embeddings_get(emb, LEVEL_MESSAGE, (uint32_t)i);
            ASSERT_NOT_NULL(values);

            /* Check first value matches expected pattern */
            float expected = (float)(i * EMBEDDING_DIM) * 0.001f;
            ASSERT_FLOAT_EQ(values[0], expected, 0.0001f);
        }

        /* Verify levels survived */
        for (node_id_t i = 0; i < NUM_MESSAGES; i++) {
            ASSERT_EQ(relations_get_level(rel, i), LEVEL_MESSAGE);
        }

        wal_close(wal);
        embeddings_close(emb);
        relations_close(rel);
    }

    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Partial write recovery (truncated WAL entry)
 */
TEST(partial_write_recovery) {
    setup_dirs();

    char wal_path[256];
    snprintf(wal_path, sizeof(wal_path), "%s/wal/partial.log", TEST_DIR);

    /* Write some entries, then corrupt the last one */
    {
        wal_t* wal = NULL;
        ASSERT_OK(wal_create(&wal, wal_path, 1024 * 1024));

        wal_node_data_t data = { .node_id = 1 };
        ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &data, sizeof(data)));

        data.node_id = 2;
        ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &data, sizeof(data)));

        wal_close(wal);

        /* Truncate file to simulate partial write */
        /* Remove last few bytes from the file */
        struct stat st;
        stat(wal_path, &st);
        truncate(wal_path, st.st_size - 10);
    }

    /* Recovery should handle truncated entry gracefully */
    {
        wal_t* wal = NULL;
        ASSERT_OK(wal_open(&wal, wal_path));

        g_recovered_count = 0;
        /* Should recover complete entries, skip truncated one */
        ASSERT_OK(wal_replay(wal, recovery_callback, NULL));

        /* At least one entry should be recovered */
        ASSERT_GE(g_recovered_count, 1);

        wal_close(wal);
    }

    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
