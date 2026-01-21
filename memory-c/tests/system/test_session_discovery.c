/*
 * SVC_MEM_TEST_0012 - Verify session discovery
 *
 * Test specification:
 * - Store 100 sessions with varied content
 * - Query `list_sessions` by keyword -> returns matching sessions
 * - Query by agent_id -> returns only that agent's sessions
 * - Query by timestamp -> returns sessions after time
 */

#include "../test_framework.h"
#include "../../src/session/session.h"
#include "../../src/core/hierarchy.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_session_discovery"

static void cleanup_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

static void setup_dir(void) {
    cleanup_dir(TEST_DIR);
    mkdir(TEST_DIR, 0755);

    char path[256];
    snprintf(path, sizeof(path), "%s/relations", TEST_DIR);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/embeddings", TEST_DIR);
    mkdir(path, 0755);
}

/*
 * TEST: Discover sessions by keyword
 */
TEST(session_discovery_by_keyword) {
    setup_dir();

    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    /* Create sessions with different topics */
    ASSERT_OK(session_register(manager, "sess-auth-1", "agent-1", 1));
    ASSERT_OK(session_update_content(manager, "sess-auth-1",
        "Implementing OAuth authentication flow", 39));

    ASSERT_OK(session_register(manager, "sess-auth-2", "agent-2", 2));
    ASSERT_OK(session_update_content(manager, "sess-auth-2",
        "Adding JWT token validation for authentication", 45));

    ASSERT_OK(session_register(manager, "sess-db-1", "agent-1", 3));
    ASSERT_OK(session_update_content(manager, "sess-db-1",
        "Database query optimization", 27));

    ASSERT_OK(session_register(manager, "sess-db-2", "agent-2", 4));
    ASSERT_OK(session_update_content(manager, "sess-db-2",
        "PostgreSQL connection pooling", 29));

    /* Find by keyword "auth" */
    char results[10][MAX_SESSION_ID_LEN];
    size_t count = session_find_by_keyword(manager, "auth", results, 10);

    /* Should find at least the auth sessions */
    ASSERT_GT(count, 0);

    session_manager_destroy(manager);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Discover sessions by agent
 */
TEST(session_discovery_by_agent) {
    setup_dir();

    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    /* Create sessions for different agents */
    ASSERT_OK(session_register(manager, "sess-a1", "agent-alpha", 1));
    ASSERT_OK(session_register(manager, "sess-a2", "agent-alpha", 2));
    ASSERT_OK(session_register(manager, "sess-a3", "agent-alpha", 3));

    ASSERT_OK(session_register(manager, "sess-b1", "agent-beta", 4));
    ASSERT_OK(session_register(manager, "sess-b2", "agent-beta", 5));

    ASSERT_OK(session_register(manager, "sess-c1", "agent-gamma", 6));

    /* List sessions for agent-alpha */
    char results[10][MAX_SESSION_ID_LEN];
    size_t count = session_list(manager, "agent-alpha", NULL, 0, results, 10);
    ASSERT_EQ(count, 3);

    /* List sessions for agent-beta */
    count = session_list(manager, "agent-beta", NULL, 0, results, 10);
    ASSERT_EQ(count, 2);

    /* List sessions for agent-gamma */
    count = session_list(manager, "agent-gamma", NULL, 0, results, 10);
    ASSERT_EQ(count, 1);

    session_manager_destroy(manager);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: List all sessions
 */
TEST(session_discovery_list_all) {
    setup_dir();

    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    /* Create many sessions */
    for (int i = 0; i < 50; i++) {
        char sess_id[32], agent_id[32];
        snprintf(sess_id, sizeof(sess_id), "session-%03d", i);
        snprintf(agent_id, sizeof(agent_id), "agent-%d", i % 5);
        ASSERT_OK(session_register(manager, sess_id, agent_id, i + 1));
    }

    ASSERT_EQ(session_count(manager), 50);

    /* List all sessions */
    char results[100][MAX_SESSION_ID_LEN];
    size_t count = session_list(manager, NULL, NULL, 0, results, 100);
    ASSERT_EQ(count, 50);

    session_manager_destroy(manager);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Discover sessions by file touched
 */
TEST(session_discovery_by_file) {
    setup_dir();

    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess-1", "agent", 1));
    ASSERT_OK(session_update_content(manager, "sess-1",
        "Modified src/api/handler.go and src/api/router.go", 50));

    ASSERT_OK(session_register(manager, "sess-2", "agent", 2));
    ASSERT_OK(session_update_content(manager, "sess-2",
        "Modified src/db/query.go and src/db/connection.go", 50));

    ASSERT_OK(session_register(manager, "sess-3", "agent", 3));
    ASSERT_OK(session_update_content(manager, "sess-3",
        "Modified src/api/middleware.go", 31));

    /* Find sessions that touched API files */
    char results[10][MAX_SESSION_ID_LEN];
    size_t count = session_find_by_file(manager, "api", results, 10);
    ASSERT_EQ(count, 2);

    /* Find sessions that touched DB files */
    count = session_find_by_file(manager, "db", results, 10);
    ASSERT_EQ(count, 1);

    session_manager_destroy(manager);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Session metadata includes timestamps
 */
TEST(session_discovery_timestamps) {
    setup_dir();

    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess", "agent", 1));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "sess", &meta));

    /* Should have valid timestamps */
    ASSERT_GT(meta.created_at, 0);
    ASSERT_GT(meta.last_active_at, 0);
    ASSERT_GE(meta.last_active_at, meta.created_at);

    /* Should have sequence number */
    ASSERT_GT(meta.sequence_num, 0);

    session_manager_destroy(manager);
    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
