/*
 * Unit tests for session metadata management
 */

#include "../test_framework.h"
#include "../../src/session/session.h"

#include <stdlib.h>
#include <string.h>

/*
 * TEST: Create session manager
 */
TEST(session_manager_create_destroy) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));
    ASSERT_NOT_NULL(manager);

    ASSERT_EQ(session_count(manager), 0);

    session_manager_destroy(manager);
}

/*
 * TEST: Register session
 */
TEST(session_register_basic) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess-001", "agent-1", 100));
    ASSERT_EQ(session_count(manager), 1);

    /* Duplicate registration should fail */
    ASSERT_EQ(session_register(manager, "sess-001", "agent-1", 101), MEM_ERR_EXISTS);

    /* Can register different session */
    ASSERT_OK(session_register(manager, "sess-002", "agent-2", 200));
    ASSERT_EQ(session_count(manager), 2);

    session_manager_destroy(manager);
}

/*
 * TEST: Get session metadata
 */
TEST(session_get_metadata) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "test-session", "test-agent", 42));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "test-session", &meta));

    ASSERT_STR_EQ(meta.session_id, "test-session");
    ASSERT_STR_EQ(meta.agent_id, "test-agent");
    ASSERT_EQ(meta.root_node_id, 42);
    ASSERT_GT(meta.created_at, 0);
    ASSERT_GT(meta.sequence_num, 0);

    /* Non-existent session */
    ASSERT_EQ(session_get_metadata(manager, "no-such-session", &meta), MEM_ERR_NOT_FOUND);

    session_manager_destroy(manager);
}

/*
 * TEST: Update content extracts keywords
 */
TEST(session_update_content_keywords) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess", "agent", 1));

    /* Update with code content */
    const char* content =
        "Implementing OAuth token refresh in src/auth/oauth.go.\n"
        "The handleTokenRefresh() function validates and refreshes tokens.";

    ASSERT_OK(session_update_content(manager, "sess", content, strlen(content)));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "sess", &meta));

    /* Should have extracted keywords */
    ASSERT_GT(meta.keyword_count, 0);

    /* Should have "oauth" as keyword */
    bool found = false;
    for (size_t i = 0; i < meta.keyword_count; i++) {
        if (strstr(meta.keywords[i].word, "oauth") != NULL ||
            strstr(meta.keywords[i].word, "token") != NULL) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    session_manager_destroy(manager);
}

/*
 * TEST: Update content extracts file paths
 */
TEST(session_update_content_files) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess", "agent", 1));

    const char* content = "Modified src/api/handler.c and tests/test_api.c";
    ASSERT_OK(session_update_content(manager, "sess", content, strlen(content)));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "sess", &meta));

    ASSERT_GT(meta.file_count, 0);

    session_manager_destroy(manager);
}

/*
 * TEST: Set session title
 */
TEST(session_set_title) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess", "agent", 1));

    ASSERT_OK(session_set_title(manager, "sess", "OAuth Token Implementation"));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "sess", &meta));

    ASSERT_TRUE(meta.title_generated);
    ASSERT_STR_EQ(meta.title, "OAuth Token Implementation");

    session_manager_destroy(manager);
}

/*
 * TEST: List sessions by agent
 */
TEST(session_list_by_agent) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess-a1", "agent-A", 1));
    ASSERT_OK(session_register(manager, "sess-a2", "agent-A", 2));
    ASSERT_OK(session_register(manager, "sess-b1", "agent-B", 3));

    char results[10][MAX_SESSION_ID_LEN];
    size_t count;

    /* List all for agent-A */
    count = session_list(manager, "agent-A", NULL, 0, results, 10);
    ASSERT_EQ(count, 2);

    /* List all for agent-B */
    count = session_list(manager, "agent-B", NULL, 0, results, 10);
    ASSERT_EQ(count, 1);

    /* List all */
    count = session_list(manager, NULL, NULL, 0, results, 10);
    ASSERT_EQ(count, 3);

    session_manager_destroy(manager);
}

/*
 * TEST: Find sessions by keyword
 */
TEST(session_find_by_keyword) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess-1", "agent", 1));
    ASSERT_OK(session_update_content(manager, "sess-1",
        "OAuth authentication implementation", 34));

    ASSERT_OK(session_register(manager, "sess-2", "agent", 2));
    ASSERT_OK(session_update_content(manager, "sess-2",
        "Database query optimization", 27));

    char results[10][MAX_SESSION_ID_LEN];
    size_t count;

    /* Find by OAuth keyword */
    count = session_find_by_keyword(manager, "oauth", results, 10);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(results[0], "sess-1");

    session_manager_destroy(manager);
}

/*
 * TEST: Find sessions by file
 */
TEST(session_find_by_file) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess-1", "agent", 1));
    ASSERT_OK(session_update_content(manager, "sess-1",
        "Edit src/auth/handler.go", 25));

    ASSERT_OK(session_register(manager, "sess-2", "agent", 2));
    ASSERT_OK(session_update_content(manager, "sess-2",
        "Edit src/db/query.go", 21));

    char results[10][MAX_SESSION_ID_LEN];
    size_t count;

    count = session_find_by_file(manager, "auth", results, 10);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(results[0], "sess-1");

    session_manager_destroy(manager);
}

/*
 * TEST: Update stats
 */
TEST(session_update_stats) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess", "agent", 1));

    ASSERT_OK(session_update_stats(manager, "sess", 1, 0, 0));
    ASSERT_OK(session_update_stats(manager, "sess", 0, 2, 0));
    ASSERT_OK(session_update_stats(manager, "sess", 0, 0, 5));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "sess", &meta));

    ASSERT_EQ(meta.message_count, 1);
    ASSERT_EQ(meta.block_count, 2);
    ASSERT_EQ(meta.statement_count, 5);

    session_manager_destroy(manager);
}

/*
 * TEST: Sequence numbers increment
 */
TEST(session_sequence_numbers) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    uint64_t seq1 = session_get_next_sequence(manager);
    uint64_t seq2 = session_get_next_sequence(manager);
    uint64_t seq3 = session_get_next_sequence(manager);

    ASSERT_GT(seq2, seq1);
    ASSERT_GT(seq3, seq2);

    session_manager_destroy(manager);
}

/*
 * TEST: Invalid arguments
 */
TEST(session_invalid_args) {
    session_manager_t* manager = NULL;
    ASSERT_EQ(session_manager_create(NULL), MEM_ERR_INVALID_ARG);

    ASSERT_OK(session_manager_create(&manager));

    ASSERT_EQ(session_register(NULL, "s", "a", 1), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(session_register(manager, NULL, "a", 1), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(session_register(manager, "s", NULL, 1), MEM_ERR_INVALID_ARG);

    session_metadata_t meta;
    ASSERT_EQ(session_get_metadata(NULL, "s", &meta), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(session_get_metadata(manager, NULL, &meta), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(session_get_metadata(manager, "s", NULL), MEM_ERR_INVALID_ARG);

    session_manager_destroy(manager);
}

/*
 * TEST: Multiple content updates merge keywords
 */
TEST(session_multiple_updates_merge) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sess", "agent", 1));

    ASSERT_OK(session_update_content(manager, "sess",
        "OAuth authentication", 20));
    ASSERT_OK(session_update_content(manager, "sess",
        "Token validation", 16));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "sess", &meta));

    /* Should have keywords from both updates */
    ASSERT_GT(meta.keyword_count, 1);

    session_manager_destroy(manager);
}

TEST_MAIN()
