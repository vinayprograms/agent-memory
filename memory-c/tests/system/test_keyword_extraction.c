/*
 * SVC_MEM_TEST_0011 - Verify keyword extraction
 *
 * Test specification:
 * - Store message containing code and discussion about OAuth
 * - Session metadata MUST include extracted keywords: "OAuth", "auth", etc.
 * - Keywords MUST be available immediately (sync path)
 * - LLM title MUST appear later (async path)
 */

#include "../test_framework.h"
#include "../../src/session/session.h"
#include "../../src/session/keywords.h"

#include <stdlib.h>
#include <string.h>

/*
 * TEST: Verify keywords extracted from OAuth content
 */
TEST(keyword_extraction_oauth) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "oauth-session", "coding-agent", 1));

    /* Store message with OAuth-related content */
    const char* content =
        "Implementing OAuth 2.0 token refresh flow.\n"
        "The handleTokenRefresh() function in src/auth/oauth.go validates\n"
        "the refresh token and issues a new access token.\n"
        "We need to handle token expiration and implement proper authentication.";

    ASSERT_OK(session_update_content(manager, "oauth-session", content, strlen(content)));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "oauth-session", &meta));

    /* Should have keywords extracted */
    ASSERT_GT(meta.keyword_count, 0);

    /* Should have OAuth-related keywords */
    bool found_oauth = false;
    bool found_token = false;
    bool found_auth = false;

    for (size_t i = 0; i < meta.keyword_count; i++) {
        if (strstr(meta.keywords[i].word, "oauth") != NULL) found_oauth = true;
        if (strstr(meta.keywords[i].word, "token") != NULL) found_token = true;
        if (strstr(meta.keywords[i].word, "auth") != NULL) found_auth = true;
    }

    /* At least some OAuth-related terms should be found */
    ASSERT_TRUE(found_oauth || found_token || found_auth);

    session_manager_destroy(manager);
}

/*
 * TEST: Verify identifiers extracted from code
 */
TEST(keyword_extraction_identifiers) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "code-session", "coding-agent", 1));

    const char* content =
        "func handleTokenRefresh(ctx context.Context, req *RefreshRequest) (*TokenResponse, error) {\n"
        "    token, err := validateRefreshToken(req.RefreshToken)\n"
        "    if err != nil {\n"
        "        return nil, ErrInvalidToken\n"
        "    }\n"
        "    return issueNewToken(token.UserID)\n"
        "}";

    ASSERT_OK(session_update_content(manager, "code-session", content, strlen(content)));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "code-session", &meta));

    /* Should have identifiers */
    ASSERT_GT(meta.identifier_count, 0);

    /* Should include function names */
    bool found_func = false;
    for (size_t i = 0; i < meta.identifier_count; i++) {
        if (meta.identifiers[i].kind == IDENT_FUNCTION) {
            found_func = true;
            break;
        }
    }
    ASSERT_TRUE(found_func);

    session_manager_destroy(manager);
}

/*
 * TEST: Verify file paths extracted
 */
TEST(keyword_extraction_file_paths) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "files-session", "coding-agent", 1));

    const char* content =
        "Modified the following files:\n"
        "- src/auth/oauth.go - Added refresh token handling\n"
        "- src/auth/token.go - Token validation logic\n"
        "- tests/auth_test.go - Unit tests for OAuth flow";

    ASSERT_OK(session_update_content(manager, "files-session", content, strlen(content)));

    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "files-session", &meta));

    /* Should have file paths */
    ASSERT_GT(meta.file_count, 0);

    /* Should include auth directory files */
    bool found_auth_file = false;
    for (size_t i = 0; i < meta.file_count; i++) {
        if (strstr(meta.files_touched[i], "auth") != NULL) {
            found_auth_file = true;
            break;
        }
    }
    ASSERT_TRUE(found_auth_file);

    session_manager_destroy(manager);
}

/*
 * TEST: Keywords available immediately (sync)
 */
TEST(keyword_extraction_sync_availability) {
    session_manager_t* manager = NULL;
    ASSERT_OK(session_manager_create(&manager));

    ASSERT_OK(session_register(manager, "sync-session", "agent", 1));

    const char* content = "Database optimization and query caching";
    ASSERT_OK(session_update_content(manager, "sync-session", content, strlen(content)));

    /* Get metadata immediately - keywords should be available */
    session_metadata_t meta;
    ASSERT_OK(session_get_metadata(manager, "sync-session", &meta));

    /* Keywords extracted synchronously */
    ASSERT_GT(meta.keyword_count, 0);

    /* Title NOT generated yet (would be async via LLM) */
    ASSERT_FALSE(meta.title_generated);

    session_manager_destroy(manager);
}

TEST_MAIN()
