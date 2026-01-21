/*
 * Unit tests for HNSW index
 */

#include "../test_framework.h"
#include "../../src/search/hnsw.h"
#include "../../src/embedding/embedding.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Helper: Create a normalized random vector */
static void random_vector(float* vec, unsigned int seed) {
    srand(seed);
    float mag = 0.0f;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        vec[i] = (float)rand() / RAND_MAX - 0.5f;
        mag += vec[i] * vec[i];
    }
    mag = sqrtf(mag);
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        vec[i] /= mag;
    }
}

/* Helper: Create a unit vector along dimension d */
static void unit_vector(float* vec, int dim) {
    memset(vec, 0, EMBEDDING_DIM * sizeof(float));
    if (dim < EMBEDDING_DIM) {
        vec[dim] = 1.0f;
    }
}

/* Test basic creation and destruction */
TEST(hnsw_create_destroy) {
    hnsw_index_t* index = NULL;
    hnsw_config_t config = HNSW_CONFIG_DEFAULT;
    config.max_elements = 1000;

    ASSERT_OK(hnsw_create(&index, &config));
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(hnsw_size(index), 0);

    hnsw_destroy(index);
}

/* Test creation with default config */
TEST(hnsw_create_default_config) {
    hnsw_index_t* index = NULL;

    ASSERT_OK(hnsw_create(&index, NULL));
    ASSERT_NOT_NULL(index);

    hnsw_destroy(index);
}

/* Test adding a single element */
TEST(hnsw_add_single) {
    hnsw_index_t* index = NULL;
    ASSERT_OK(hnsw_create(&index, NULL));

    float vec[EMBEDDING_DIM];
    random_vector(vec, 42);

    ASSERT_OK(hnsw_add(index, 100, vec));
    ASSERT_EQ(hnsw_size(index), 1);
    ASSERT_TRUE(hnsw_contains(index, 100));
    ASSERT_FALSE(hnsw_contains(index, 101));

    hnsw_destroy(index);
}

/* Test adding multiple elements */
TEST(hnsw_add_multiple) {
    hnsw_index_t* index = NULL;
    hnsw_config_t config = HNSW_CONFIG_DEFAULT;
    config.max_elements = 100;
    ASSERT_OK(hnsw_create(&index, &config));

    float vec[EMBEDDING_DIM];

    for (int i = 0; i < 50; i++) {
        random_vector(vec, i);
        ASSERT_OK(hnsw_add(index, (node_id_t)i, vec));
    }

    ASSERT_EQ(hnsw_size(index), 50);

    for (int i = 0; i < 50; i++) {
        ASSERT_TRUE(hnsw_contains(index, (node_id_t)i));
    }

    hnsw_destroy(index);
}

/* Test duplicate ID rejection */
TEST(hnsw_add_duplicate) {
    hnsw_index_t* index = NULL;
    ASSERT_OK(hnsw_create(&index, NULL));

    float vec[EMBEDDING_DIM];
    random_vector(vec, 42);

    ASSERT_OK(hnsw_add(index, 100, vec));
    ASSERT_NE(hnsw_add(index, 100, vec), MEM_OK);  /* Should fail */

    ASSERT_EQ(hnsw_size(index), 1);

    hnsw_destroy(index);
}

/* Test basic search */
TEST(hnsw_search_basic) {
    hnsw_index_t* index = NULL;
    hnsw_config_t config = HNSW_CONFIG_DEFAULT;
    config.max_elements = 100;
    ASSERT_OK(hnsw_create(&index, &config));

    /* Add orthogonal vectors */
    float vec[EMBEDDING_DIM];
    for (int i = 0; i < 10; i++) {
        unit_vector(vec, i);
        ASSERT_OK(hnsw_add(index, (node_id_t)i, vec));
    }

    /* Search for vector along dimension 5 */
    float query[EMBEDDING_DIM];
    unit_vector(query, 5);

    hnsw_result_t results[5];
    size_t count = 0;

    ASSERT_OK(hnsw_search(index, query, 5, results, &count));
    ASSERT_GT(count, 0);

    /* First result should be exact match (id=5, distance=0) */
    ASSERT_EQ(results[0].id, 5);
    ASSERT_FLOAT_EQ(results[0].distance, 0.0f, 0.01f);

    hnsw_destroy(index);
}

/* Test search returns results sorted by distance */
TEST(hnsw_search_sorted) {
    hnsw_index_t* index = NULL;
    ASSERT_OK(hnsw_create(&index, NULL));

    /* Add 20 random vectors */
    float vecs[20][EMBEDDING_DIM];
    for (int i = 0; i < 20; i++) {
        random_vector(vecs[i], i);
        ASSERT_OK(hnsw_add(index, (node_id_t)i, vecs[i]));
    }

    /* Search */
    float query[EMBEDDING_DIM];
    random_vector(query, 100);

    hnsw_result_t results[10];
    size_t count = 0;

    ASSERT_OK(hnsw_search(index, query, 10, results, &count));
    ASSERT_GT(count, 1);

    /* Verify sorted by distance */
    for (size_t i = 1; i < count; i++) {
        ASSERT_LE(results[i-1].distance, results[i].distance + 0.001f);
    }

    hnsw_destroy(index);
}

/* Test search on empty index */
TEST(hnsw_search_empty) {
    hnsw_index_t* index = NULL;
    ASSERT_OK(hnsw_create(&index, NULL));

    float query[EMBEDDING_DIM];
    random_vector(query, 42);

    hnsw_result_t results[5];
    size_t count = 99;

    ASSERT_OK(hnsw_search(index, query, 5, results, &count));
    ASSERT_EQ(count, 0);

    hnsw_destroy(index);
}

/* Test remove operation */
TEST(hnsw_remove) {
    hnsw_index_t* index = NULL;
    ASSERT_OK(hnsw_create(&index, NULL));

    float vec[EMBEDDING_DIM];
    random_vector(vec, 42);

    ASSERT_OK(hnsw_add(index, 100, vec));
    ASSERT_EQ(hnsw_size(index), 1);
    ASSERT_TRUE(hnsw_contains(index, 100));

    ASSERT_OK(hnsw_remove(index, 100));
    ASSERT_EQ(hnsw_size(index), 0);
    ASSERT_FALSE(hnsw_contains(index, 100));

    hnsw_destroy(index);
}

/* Test remove non-existent ID */
TEST(hnsw_remove_not_found) {
    hnsw_index_t* index = NULL;
    ASSERT_OK(hnsw_create(&index, NULL));

    ASSERT_NE(hnsw_remove(index, 999), MEM_OK);

    hnsw_destroy(index);
}

/* Test search excludes removed elements */
TEST(hnsw_search_after_remove) {
    hnsw_index_t* index = NULL;
    ASSERT_OK(hnsw_create(&index, NULL));

    /* Add vectors along first 3 dimensions */
    float vec[EMBEDDING_DIM];

    unit_vector(vec, 0);
    ASSERT_OK(hnsw_add(index, 0, vec));

    unit_vector(vec, 1);
    ASSERT_OK(hnsw_add(index, 1, vec));

    unit_vector(vec, 2);
    ASSERT_OK(hnsw_add(index, 2, vec));

    /* Remove id=1 */
    ASSERT_OK(hnsw_remove(index, 1));

    /* Search for vector along dimension 1 */
    float query[EMBEDDING_DIM];
    unit_vector(query, 1);

    hnsw_result_t results[3];
    size_t count = 0;

    ASSERT_OK(hnsw_search(index, query, 3, results, &count));

    /* Should get 2 results, and id=1 should not be among them */
    ASSERT_EQ(count, 2);
    for (size_t i = 0; i < count; i++) {
        ASSERT_NE(results[i].id, 1);
    }

    hnsw_destroy(index);
}

/* Test larger scale search quality */
TEST(hnsw_search_quality) {
    hnsw_index_t* index = NULL;
    hnsw_config_t config = HNSW_CONFIG_DEFAULT;
    config.max_elements = 500;
    config.ef_construction = 100;
    config.ef_search = 50;
    ASSERT_OK(hnsw_create(&index, &config));

    /* Add 200 random vectors */
    float vecs[200][EMBEDDING_DIM];
    for (int i = 0; i < 200; i++) {
        random_vector(vecs[i], i);
        ASSERT_OK(hnsw_add(index, (node_id_t)i, vecs[i]));
    }

    /* Use one of the stored vectors as query - should find itself */
    float query[EMBEDDING_DIM];
    memcpy(query, vecs[42], sizeof(query));

    hnsw_result_t results[5];
    size_t count = 0;

    ASSERT_OK(hnsw_search(index, query, 5, results, &count));
    ASSERT_GT(count, 0);

    /* First result should be the query vector itself (id=42) */
    ASSERT_EQ(results[0].id, 42);
    ASSERT_FLOAT_EQ(results[0].distance, 0.0f, 0.01f);

    hnsw_destroy(index);
}

/* Test invalid arguments */
TEST(hnsw_invalid_args) {
    hnsw_index_t* index = NULL;
    float vec[EMBEDDING_DIM];
    hnsw_result_t results[5];
    size_t count;

    /* NULL index pointer */
    ASSERT_NE(hnsw_create(NULL, NULL), MEM_OK);

    ASSERT_OK(hnsw_create(&index, NULL));

    /* NULL vector */
    ASSERT_NE(hnsw_add(index, 0, NULL), MEM_OK);

    /* NULL arguments to search */
    ASSERT_NE(hnsw_search(NULL, vec, 5, results, &count), MEM_OK);
    ASSERT_NE(hnsw_search(index, NULL, 5, results, &count), MEM_OK);
    ASSERT_NE(hnsw_search(index, vec, 5, NULL, &count), MEM_OK);
    ASSERT_NE(hnsw_search(index, vec, 5, results, NULL), MEM_OK);

    /* NULL to contains/size is handled gracefully */
    ASSERT_EQ(hnsw_size(NULL), 0);
    ASSERT_FALSE(hnsw_contains(NULL, 0));

    /* NULL to remove */
    ASSERT_NE(hnsw_remove(NULL, 0), MEM_OK);

    hnsw_destroy(index);
}

/* Test cosine similarity via HNSW distance */
TEST(hnsw_cosine_distance) {
    hnsw_index_t* index = NULL;
    ASSERT_OK(hnsw_create(&index, NULL));

    /* Add two orthogonal vectors */
    float vec1[EMBEDDING_DIM];
    float vec2[EMBEDDING_DIM];

    unit_vector(vec1, 0);  /* (1, 0, 0, ...) */
    unit_vector(vec2, 1);  /* (0, 1, 0, ...) */

    ASSERT_OK(hnsw_add(index, 0, vec1));
    ASSERT_OK(hnsw_add(index, 1, vec2));

    /* Search for vec1 */
    hnsw_result_t results[2];
    size_t count = 0;

    ASSERT_OK(hnsw_search(index, vec1, 2, results, &count));
    ASSERT_EQ(count, 2);

    /* First result: vec1 (distance 0) */
    ASSERT_EQ(results[0].id, 0);
    ASSERT_FLOAT_EQ(results[0].distance, 0.0f, 0.01f);

    /* Second result: vec2 (orthogonal, distance = 1 - 0 = 1) */
    ASSERT_EQ(results[1].id, 1);
    ASSERT_FLOAT_EQ(results[1].distance, 1.0f, 0.01f);

    hnsw_destroy(index);
}

TEST_MAIN()
