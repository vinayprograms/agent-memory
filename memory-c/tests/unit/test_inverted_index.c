/*
 * Unit tests for inverted index
 */

#include "../test_framework.h"
#include "../../src/search/inverted_index.h"

#include <stdlib.h>
#include <string.h>

/* Test basic creation and destruction */
TEST(inverted_index_create_destroy) {
    inverted_index_t* index = NULL;
    inverted_index_config_t config = INVERTED_INDEX_CONFIG_DEFAULT;

    ASSERT_OK(inverted_index_create(&index, &config));
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(inverted_index_doc_count(index), 0);
    ASSERT_EQ(inverted_index_token_count(index), 0);

    inverted_index_destroy(index);
}

/* Test creation with default config */
TEST(inverted_index_create_default) {
    inverted_index_t* index = NULL;

    ASSERT_OK(inverted_index_create(&index, NULL));
    ASSERT_NOT_NULL(index);

    inverted_index_destroy(index);
}

/* Test adding a single document */
TEST(inverted_index_add_single) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* tokens[] = {"hello", "world", "test"};
    ASSERT_OK(inverted_index_add(index, 100, tokens, 3));

    ASSERT_EQ(inverted_index_doc_count(index), 1);
    ASSERT_EQ(inverted_index_token_count(index), 3);
    ASSERT_TRUE(inverted_index_contains(index, 100));
    ASSERT_FALSE(inverted_index_contains(index, 101));

    inverted_index_destroy(index);
}

/* Test adding multiple documents */
TEST(inverted_index_add_multiple) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* doc1[] = {"hello", "world"};
    const char* doc2[] = {"hello", "everyone"};
    const char* doc3[] = {"goodbye", "world"};

    ASSERT_OK(inverted_index_add(index, 1, doc1, 2));
    ASSERT_OK(inverted_index_add(index, 2, doc2, 2));
    ASSERT_OK(inverted_index_add(index, 3, doc3, 2));

    ASSERT_EQ(inverted_index_doc_count(index), 3);
    ASSERT_EQ(inverted_index_token_count(index), 4);  /* hello, world, everyone, goodbye */

    inverted_index_destroy(index);
}

/* Test duplicate document rejection */
TEST(inverted_index_add_duplicate) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* tokens[] = {"test"};
    ASSERT_OK(inverted_index_add(index, 100, tokens, 1));
    ASSERT_NE(inverted_index_add(index, 100, tokens, 1), MEM_OK);  /* Should fail */

    ASSERT_EQ(inverted_index_doc_count(index), 1);

    inverted_index_destroy(index);
}

/* Test AND search */
TEST(inverted_index_search_and) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* doc1[] = {"hello", "world", "test"};
    const char* doc2[] = {"hello", "everyone", "test"};
    const char* doc3[] = {"goodbye", "world", "test"};

    ASSERT_OK(inverted_index_add(index, 1, doc1, 3));
    ASSERT_OK(inverted_index_add(index, 2, doc2, 3));
    ASSERT_OK(inverted_index_add(index, 3, doc3, 3));

    /* Search for "hello" AND "world" - should find doc1 only */
    const char* query[] = {"hello", "world"};
    inverted_result_t results[10];
    size_t count = 0;

    ASSERT_OK(inverted_index_search(index, query, 2, 10, results, &count));
    ASSERT_EQ(count, 1);
    ASSERT_EQ(results[0].doc_id, 1);

    /* Search for "test" - should find all 3 docs */
    const char* query2[] = {"test"};
    ASSERT_OK(inverted_index_search(index, query2, 1, 10, results, &count));
    ASSERT_EQ(count, 3);

    inverted_index_destroy(index);
}

/* Test OR search */
TEST(inverted_index_search_or) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* doc1[] = {"hello"};
    const char* doc2[] = {"world"};
    const char* doc3[] = {"test"};

    ASSERT_OK(inverted_index_add(index, 1, doc1, 1));
    ASSERT_OK(inverted_index_add(index, 2, doc2, 1));
    ASSERT_OK(inverted_index_add(index, 3, doc3, 1));

    /* Search for "hello" OR "world" - should find doc1 and doc2 */
    const char* query[] = {"hello", "world"};
    inverted_result_t results[10];
    size_t count = 0;

    ASSERT_OK(inverted_index_search_any(index, query, 2, 10, results, &count));
    ASSERT_EQ(count, 2);

    inverted_index_destroy(index);
}

/* Test search on empty index */
TEST(inverted_index_search_empty) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* query[] = {"hello"};
    inverted_result_t results[10];
    size_t count = 99;

    ASSERT_OK(inverted_index_search(index, query, 1, 10, results, &count));
    ASSERT_EQ(count, 0);

    inverted_index_destroy(index);
}

/* Test search with no matching tokens */
TEST(inverted_index_search_no_match) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* doc1[] = {"hello", "world"};
    ASSERT_OK(inverted_index_add(index, 1, doc1, 2));

    const char* query[] = {"foo", "bar"};
    inverted_result_t results[10];
    size_t count = 99;

    ASSERT_OK(inverted_index_search(index, query, 2, 10, results, &count));
    ASSERT_EQ(count, 0);

    inverted_index_destroy(index);
}

/* Test remove operation */
TEST(inverted_index_remove) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* tokens[] = {"hello", "world"};
    ASSERT_OK(inverted_index_add(index, 100, tokens, 2));
    ASSERT_EQ(inverted_index_doc_count(index), 1);

    ASSERT_OK(inverted_index_remove(index, 100));
    ASSERT_EQ(inverted_index_doc_count(index), 0);
    ASSERT_FALSE(inverted_index_contains(index, 100));

    inverted_index_destroy(index);
}

/* Test remove non-existent document */
TEST(inverted_index_remove_not_found) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    ASSERT_NE(inverted_index_remove(index, 999), MEM_OK);

    inverted_index_destroy(index);
}

/* Test search excludes removed documents */
TEST(inverted_index_search_after_remove) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    const char* doc1[] = {"test"};
    const char* doc2[] = {"test"};

    ASSERT_OK(inverted_index_add(index, 1, doc1, 1));
    ASSERT_OK(inverted_index_add(index, 2, doc2, 1));

    ASSERT_OK(inverted_index_remove(index, 1));

    const char* query[] = {"test"};
    inverted_result_t results[10];
    size_t count = 0;

    ASSERT_OK(inverted_index_search(index, query, 1, 10, results, &count));
    ASSERT_EQ(count, 1);
    ASSERT_EQ(results[0].doc_id, 2);

    inverted_index_destroy(index);
}

/* Test BM25 ranking */
TEST(inverted_index_bm25_ranking) {
    inverted_index_t* index = NULL;
    ASSERT_OK(inverted_index_create(&index, NULL));

    /* Doc with higher term frequency should rank higher */
    const char* doc1[] = {"test"};
    const char* doc2[] = {"test", "test", "test", "test"};

    ASSERT_OK(inverted_index_add(index, 1, doc1, 1));
    ASSERT_OK(inverted_index_add(index, 2, doc2, 4));

    const char* query[] = {"test"};
    inverted_result_t results[10];
    size_t count = 0;

    ASSERT_OK(inverted_index_search(index, query, 1, 10, results, &count));
    ASSERT_EQ(count, 2);
    ASSERT_GT(results[0].score, results[1].score);  /* Higher score first */

    inverted_index_destroy(index);
}

/* Test tokenization */
TEST(inverted_index_tokenize) {
    char** tokens = NULL;
    size_t count = 0;

    const char* text = "Hello, World! This is a TEST.";
    ASSERT_OK(inverted_index_tokenize(text, strlen(text), &tokens, &count, 100));

    ASSERT_GT(count, 0);
    ASSERT_NOT_NULL(tokens);

    /* Verify lowercase conversion */
    bool found_hello = false;
    bool found_world = false;
    bool found_test = false;

    for (size_t i = 0; i < count; i++) {
        if (strcmp(tokens[i], "hello") == 0) found_hello = true;
        if (strcmp(tokens[i], "world") == 0) found_world = true;
        if (strcmp(tokens[i], "test") == 0) found_test = true;
    }

    ASSERT_TRUE(found_hello);
    ASSERT_TRUE(found_world);
    ASSERT_TRUE(found_test);

    inverted_index_free_tokens(tokens, count);
}

/* Test invalid arguments */
TEST(inverted_index_invalid_args) {
    inverted_index_t* index = NULL;
    inverted_result_t results[10];
    size_t count;

    /* NULL index pointer */
    ASSERT_NE(inverted_index_create(NULL, NULL), MEM_OK);

    ASSERT_OK(inverted_index_create(&index, NULL));

    /* NULL arguments to search */
    const char* query[] = {"test"};
    ASSERT_NE(inverted_index_search(NULL, query, 1, 10, results, &count), MEM_OK);
    ASSERT_NE(inverted_index_search(index, query, 1, 10, NULL, &count), MEM_OK);
    ASSERT_NE(inverted_index_search(index, query, 1, 10, results, NULL), MEM_OK);

    /* NULL to contains/counts is handled gracefully */
    ASSERT_EQ(inverted_index_doc_count(NULL), 0);
    ASSERT_EQ(inverted_index_token_count(NULL), 0);
    ASSERT_FALSE(inverted_index_contains(NULL, 0));

    /* NULL to remove */
    ASSERT_NE(inverted_index_remove(NULL, 0), MEM_OK);

    inverted_index_destroy(index);
}

TEST_MAIN()
