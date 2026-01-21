/*
 * Unit tests for keyword extraction
 */

#include "../test_framework.h"
#include "../../src/session/keywords.h"

#include <stdlib.h>
#include <string.h>

/*
 * TEST: Basic keyword extraction
 */
TEST(keywords_basic_extraction) {
    extraction_result_t result;

    const char* text = "The OAuth authentication system handles token refresh and validation";

    ASSERT_OK(extract_keywords(NULL, text, strlen(text), &result));

    ASSERT_GT(result.keyword_count, 0);

    /* Should have OAuth as a keyword */
    bool found_oauth = false;
    for (size_t i = 0; i < result.keyword_count; i++) {
        if (strcmp(result.keywords[i].word, "oauth") == 0) {
            found_oauth = true;
            break;
        }
    }
    ASSERT_TRUE(found_oauth);
}

/*
 * TEST: Stop words are filtered
 */
TEST(keywords_stop_words_filtered) {
    extraction_result_t result;

    const char* text = "the and or but in on at to for of with is are was were";

    ASSERT_OK(extract_keywords(NULL, text, strlen(text), &result));

    /* Should not have any of the stop words */
    for (size_t i = 0; i < result.keyword_count; i++) {
        ASSERT_FALSE(is_stop_word(result.keywords[i].word));
    }
}

/*
 * TEST: Identifier extraction - functions
 */
TEST(keywords_function_identifiers) {
    identifier_t idents[MAX_IDENTIFIERS];

    const char* code = "func handleAuth(token string) { validateToken(t) }";

    size_t count = extract_identifiers(code, strlen(code), idents, MAX_IDENTIFIERS);

    ASSERT_GT(count, 0);

    /* Should have handleAuth or validateToken as function */
    bool found_func = false;
    for (size_t i = 0; i < count; i++) {
        if (idents[i].kind == IDENT_FUNCTION &&
            (strcmp(idents[i].name, "handleAuth") == 0 ||
             strcmp(idents[i].name, "validateToken") == 0)) {
            found_func = true;
            break;
        }
    }
    ASSERT_TRUE(found_func);
}

/*
 * TEST: Identifier extraction - types
 */
TEST(keywords_type_identifiers) {
    identifier_t idents[MAX_IDENTIFIERS];

    const char* code = "struct AuthRequest { UserCredentials creds; }";

    size_t count = extract_identifiers(code, strlen(code), idents, MAX_IDENTIFIERS);

    ASSERT_GT(count, 0);

    /* Should have AuthRequest or UserCredentials as type */
    bool found_type = false;
    for (size_t i = 0; i < count; i++) {
        if (idents[i].kind == IDENT_TYPE &&
            (strcmp(idents[i].name, "AuthRequest") == 0 ||
             strcmp(idents[i].name, "UserCredentials") == 0)) {
            found_type = true;
            break;
        }
    }
    ASSERT_TRUE(found_type);
}

/*
 * TEST: Identifier extraction - constants
 */
TEST(keywords_constant_identifiers) {
    identifier_t idents[MAX_IDENTIFIERS];

    const char* code = "const MAX_RETRY = 3; const DEFAULT_TIMEOUT = 5000;";

    size_t count = extract_identifiers(code, strlen(code), idents, MAX_IDENTIFIERS);

    /* Should have MAX_RETRY or DEFAULT_TIMEOUT as constant */
    bool found_const = false;
    for (size_t i = 0; i < count; i++) {
        if (idents[i].kind == IDENT_CONSTANT &&
            (strcmp(idents[i].name, "MAX_RETRY") == 0 ||
             strcmp(idents[i].name, "DEFAULT_TIMEOUT") == 0)) {
            found_const = true;
            break;
        }
    }
    ASSERT_TRUE(found_const);
}

/*
 * TEST: File path extraction - absolute paths
 */
TEST(keywords_file_paths_absolute) {
    char paths[MAX_FILE_PATHS][MAX_FILE_PATH_LEN];

    const char* text = "Edit the file /home/user/project/src/main.c to fix the bug";

    size_t count = extract_file_paths(text, strlen(text), paths, MAX_FILE_PATHS);

    ASSERT_GT(count, 0);

    bool found = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(paths[i], "/home/user/project") != NULL) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

/*
 * TEST: File path extraction - relative paths
 */
TEST(keywords_file_paths_relative) {
    char paths[MAX_FILE_PATHS][MAX_FILE_PATH_LEN];

    const char* text = "Look at src/api/handler.c and tests/test_api.c";

    size_t count = extract_file_paths(text, strlen(text), paths, MAX_FILE_PATHS);

    ASSERT_GT(count, 0);

    bool found_src = false;
    bool found_tests = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(paths[i], "src/api") != NULL) found_src = true;
        if (strstr(paths[i], "tests/") != NULL) found_tests = true;
    }
    ASSERT_TRUE(found_src || found_tests);
}

/*
 * TEST: Keyword extractor with IDF
 */
TEST(keywords_extractor_idf) {
    keyword_extractor_t* extractor = NULL;
    ASSERT_OK(keyword_extractor_create(&extractor));
    ASSERT_NOT_NULL(extractor);

    /* Add some documents to build IDF */
    const char* doc1 = "authentication token validation";
    const char* doc2 = "database query optimization";
    const char* doc3 = "authentication security audit";

    ASSERT_OK(keyword_extractor_update_idf(extractor, doc1, strlen(doc1)));
    ASSERT_OK(keyword_extractor_update_idf(extractor, doc2, strlen(doc2)));
    ASSERT_OK(keyword_extractor_update_idf(extractor, doc3, strlen(doc3)));

    /* Extract from new document - "authentication" should have lower score
       since it appears in multiple docs */
    extraction_result_t result;
    const char* new_doc = "authentication optimization analysis";
    ASSERT_OK(extract_keywords(extractor, new_doc, strlen(new_doc), &result));

    ASSERT_GT(result.keyword_count, 0);

    keyword_extractor_destroy(extractor);
}

/*
 * TEST: Stop word check
 */
TEST(keywords_is_stop_word) {
    ASSERT_TRUE(is_stop_word("the"));
    ASSERT_TRUE(is_stop_word("and"));
    ASSERT_TRUE(is_stop_word("is"));
    ASSERT_TRUE(is_stop_word("for"));

    ASSERT_FALSE(is_stop_word("authentication"));
    ASSERT_FALSE(is_stop_word("database"));
    ASSERT_FALSE(is_stop_word("token"));
}

/*
 * TEST: Empty input handling
 */
TEST(keywords_empty_input) {
    extraction_result_t result;

    ASSERT_OK(extract_keywords(NULL, "", 0, &result));
    ASSERT_EQ(result.keyword_count, 0);
}

/*
 * TEST: Full extraction result
 */
TEST(keywords_full_extraction) {
    extraction_result_t result;

    const char* text =
        "The AuthService in src/auth/service.go implements OAuth token refresh.\n"
        "See handleTokenRefresh() for the main logic.\n"
        "Constants: MAX_TOKEN_AGE, DEFAULT_SCOPE\n"
        "Related: /usr/local/config/auth.yaml";

    ASSERT_OK(extract_keywords(NULL, text, strlen(text), &result));

    /* Should have keywords */
    ASSERT_GT(result.keyword_count, 0);

    /* Should have some identifiers */
    ASSERT_GT(result.identifier_count, 0);

    /* Should have file paths */
    ASSERT_GT(result.file_path_count, 0);
}

/*
 * TEST: Invalid arguments
 */
TEST(keywords_invalid_args) {
    extraction_result_t result;

    ASSERT_EQ(extract_keywords(NULL, NULL, 0, &result), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(extract_keywords(NULL, "test", 4, NULL), MEM_ERR_INVALID_ARG);

    keyword_extractor_t* ext = NULL;
    ASSERT_EQ(keyword_extractor_create(NULL), MEM_ERR_INVALID_ARG);
    ASSERT_OK(keyword_extractor_create(&ext));
    ASSERT_EQ(keyword_extractor_update_idf(ext, NULL, 0), MEM_ERR_INVALID_ARG);
    keyword_extractor_destroy(ext);
}

/*
 * TEST: Keyword scores are sorted
 */
TEST(keywords_sorted_by_score) {
    extraction_result_t result;

    /* Use a document where we can expect ordering */
    const char* text = "authentication authentication authentication token";

    ASSERT_OK(extract_keywords(NULL, text, strlen(text), &result));

    /* Keywords should be sorted by score descending */
    for (size_t i = 1; i < result.keyword_count; i++) {
        ASSERT_GE(result.keywords[i-1].score, result.keywords[i].score);
    }
}

TEST_MAIN()
