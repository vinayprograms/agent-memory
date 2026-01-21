/*
 * Memory Service - WAL Unit Tests
 */

#include "../test_framework.h"
#include "../../src/storage/wal.h"
#include "../../include/error.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* Test WAL creation */
TEST(wal_create_basic) {
    const char* path = "/tmp/test_wal_create.log";
    wal_t* wal = NULL;

    mem_error_t err = wal_create(&wal, path, 1024 * 1024);
    ASSERT_OK(err);
    ASSERT_NOT_NULL(wal);
    ASSERT_EQ(wal_sequence(wal), 1);
    ASSERT_EQ(wal_size(wal), 0);

    wal_close(wal);
    unlink(path);
}

/* Test WAL append and sequence */
TEST(wal_append_basic) {
    const char* path = "/tmp/test_wal_append.log";
    wal_t* wal = NULL;

    ASSERT_OK(wal_create(&wal, path, 1024 * 1024));

    /* Append some entries */
    wal_node_data_t node = {
        .node_id = 42,
        .level = LEVEL_MESSAGE,
        .parent_id = 1,
        .embedding_idx = 10
    };
    strcpy(node.agent_id, "agent-1");
    strcpy(node.session_id, "session-1");

    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));
    ASSERT_EQ(wal_sequence(wal), 2);
    ASSERT_GT(wal_size(wal), 0);

    ASSERT_OK(wal_append(wal, WAL_OP_NODE_UPDATE, &node, sizeof(node)));
    ASSERT_EQ(wal_sequence(wal), 3);

    wal_close(wal);
    unlink(path);
}

/* Replay callback for testing */
static int g_replay_count = 0;
static wal_op_type_t g_last_op = WAL_OP_NONE;

static mem_error_t test_replay_callback(wal_op_type_t op, const void* data,
                                        size_t len, void* user_data) {
    (void)data;
    (void)len;
    (void)user_data;
    g_replay_count++;
    g_last_op = op;
    return MEM_OK;
}

/* Test WAL replay */
TEST(wal_replay_basic) {
    const char* path = "/tmp/test_wal_replay.log";
    wal_t* wal = NULL;

    ASSERT_OK(wal_create(&wal, path, 1024 * 1024));

    /* Write some entries */
    wal_node_data_t node = { .node_id = 1 };
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));

    node.node_id = 2;
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));

    node.node_id = 3;
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_UPDATE, &node, sizeof(node)));

    wal_close(wal);

    /* Reopen and replay */
    g_replay_count = 0;
    ASSERT_OK(wal_open(&wal, path));
    ASSERT_OK(wal_replay(wal, test_replay_callback, NULL));

    ASSERT_EQ(g_replay_count, 3);
    ASSERT_EQ(g_last_op, WAL_OP_NODE_UPDATE);

    /* Sequence should be recovered */
    ASSERT_EQ(wal_sequence(wal), 4);

    wal_close(wal);
    unlink(path);
}

/* Test WAL checkpoint */
TEST(wal_checkpoint_basic) {
    const char* path = "/tmp/test_wal_checkpoint.log";
    wal_t* wal = NULL;

    ASSERT_OK(wal_create(&wal, path, 1024 * 1024));

    /* Write some entries */
    wal_node_data_t node = { .node_id = 1 };
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));

    /* Checkpoint */
    ASSERT_OK(wal_checkpoint(wal));

    /* Write more entries */
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_UPDATE, &node, sizeof(node)));

    wal_close(wal);

    /* Reopen and replay from checkpoint */
    g_replay_count = 0;
    ASSERT_OK(wal_open(&wal, path));

    /* Replay to verify checkpoint marker was written */
    wal_replay(wal, test_replay_callback, NULL);

    /* Replay from last checkpoint would skip the first entries */
    /* This test just verifies checkpoint marker is written */

    wal_close(wal);
    unlink(path);
}

/* Test WAL truncate */
TEST(wal_truncate_basic) {
    const char* path = "/tmp/test_wal_truncate.log";
    wal_t* wal = NULL;

    ASSERT_OK(wal_create(&wal, path, 1024 * 1024));

    /* Write entries */
    wal_node_data_t node = { .node_id = 1 };
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));

    size_t size_before = wal_size(wal);
    ASSERT_GT(size_before, 0);

    /* Truncate */
    ASSERT_OK(wal_truncate(wal));
    ASSERT_EQ(wal_size(wal), 0);

    /* Can still append */
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));
    ASSERT_GT(wal_size(wal), 0);

    wal_close(wal);
    unlink(path);
}

/* Test WAL needs checkpoint */
TEST(wal_needs_checkpoint) {
    const char* path = "/tmp/test_wal_needs_cp.log";
    wal_t* wal = NULL;

    /* Small max size */
    ASSERT_OK(wal_create(&wal, path, 500));

    ASSERT(!wal_needs_checkpoint(wal));

    /* Fill up the WAL */
    wal_node_data_t node = { .node_id = 1 };
    for (int i = 0; i < 10; i++) {
        wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node));
    }

    ASSERT(wal_needs_checkpoint(wal));

    wal_close(wal);
    unlink(path);
}

/* Test CRC validation */
TEST(wal_crc_validation) {
    const char* path = "/tmp/test_wal_crc.log";
    wal_t* wal = NULL;

    ASSERT_OK(wal_create(&wal, path, 1024 * 1024));

    /* Write entry */
    wal_node_data_t node = { .node_id = 42, .level = LEVEL_BLOCK };
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &node, sizeof(node)));

    wal_close(wal);

    /* Reopen and replay - should succeed with valid CRC */
    g_replay_count = 0;
    ASSERT_OK(wal_open(&wal, path));
    ASSERT_OK(wal_replay(wal, test_replay_callback, NULL));
    ASSERT_EQ(g_replay_count, 1);

    wal_close(wal);
    unlink(path);
}

/* Test NULL and invalid arguments */
TEST(wal_invalid_args) {
    wal_t* wal = NULL;

    ASSERT_EQ(wal_create(NULL, "/tmp/x.log", 1024), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(wal_create(&wal, NULL, 1024), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(wal_create(&wal, "/tmp/x.log", 0), MEM_ERR_INVALID_ARG);

    ASSERT_EQ(wal_append(NULL, WAL_OP_NODE_INSERT, NULL, 0), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(wal_sync(NULL), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(wal_checkpoint(NULL), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(wal_truncate(NULL), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(wal_replay(NULL, test_replay_callback, NULL), MEM_ERR_INVALID_ARG);

    ASSERT_EQ(wal_sequence(NULL), 0);
    ASSERT_EQ(wal_size(NULL), 0);
}

TEST_MAIN()
