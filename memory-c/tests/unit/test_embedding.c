/*
 * Unit tests for embedding generation
 */

#include "../test_framework.h"
#include "../../src/embedding/embedding.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Test basic creation */
TEST(embedding_engine_create_basic) {
    embedding_engine_t* engine = NULL;
    embedding_config_t config = EMBEDDING_CONFIG_DEFAULT;

    ASSERT_OK(embedding_engine_create(&engine, &config));
    ASSERT_NOT_NULL(engine);

    embedding_engine_destroy(engine);
}

/* Test single embedding generation */
TEST(embedding_generate_single) {
    embedding_engine_t* engine = NULL;
    ASSERT_OK(embedding_engine_create(&engine, NULL));

    const char* text = "Hello world";
    float output[EMBEDDING_DIM];

    ASSERT_OK(embedding_generate(engine, text, strlen(text), output));

    /* Verify output is normalized (magnitude ~= 1.0) */
    float mag = 0.0f;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        mag += output[i] * output[i];
    }
    mag = sqrtf(mag);
    ASSERT_FLOAT_EQ(mag, 1.0f, 0.01f);

    embedding_engine_destroy(engine);
}

/* Test batch embedding generation */
TEST(embedding_generate_batch) {
    embedding_engine_t* engine = NULL;
    ASSERT_OK(embedding_engine_create(&engine, NULL));

    const char* texts[] = {
        "First sentence.",
        "Second sentence.",
        "Third sentence."
    };
    size_t lengths[] = {
        strlen(texts[0]),
        strlen(texts[1]),
        strlen(texts[2])
    };

    float outputs[3 * EMBEDDING_DIM];
    ASSERT_OK(embedding_generate_batch(engine, texts, lengths, 3, outputs));

    /* Verify all outputs are normalized */
    for (int t = 0; t < 3; t++) {
        float mag = 0.0f;
        for (int i = 0; i < EMBEDDING_DIM; i++) {
            mag += outputs[t * EMBEDDING_DIM + i] * outputs[t * EMBEDDING_DIM + i];
        }
        mag = sqrtf(mag);
        ASSERT_FLOAT_EQ(mag, 1.0f, 0.01f);
    }

    embedding_engine_destroy(engine);
}

/* Test deterministic embedding */
TEST(embedding_deterministic) {
    embedding_engine_t* engine = NULL;
    ASSERT_OK(embedding_engine_create(&engine, NULL));

    const char* text = "Test determinism";
    float output1[EMBEDDING_DIM];
    float output2[EMBEDDING_DIM];

    ASSERT_OK(embedding_generate(engine, text, strlen(text), output1));
    ASSERT_OK(embedding_generate(engine, text, strlen(text), output2));

    /* Same text should produce same embedding */
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        ASSERT_FLOAT_EQ(output1[i], output2[i], 0.0001f);
    }

    embedding_engine_destroy(engine);
}

/* Test different texts produce different embeddings */
TEST(embedding_different_texts) {
    embedding_engine_t* engine = NULL;
    ASSERT_OK(embedding_engine_create(&engine, NULL));

    const char* text1 = "The quick brown fox";
    const char* text2 = "completely different words";
    float output1[EMBEDDING_DIM];
    float output2[EMBEDDING_DIM];

    ASSERT_OK(embedding_generate(engine, text1, strlen(text1), output1));
    ASSERT_OK(embedding_generate(engine, text2, strlen(text2), output2));

    /* Different texts should produce different embeddings */
    int different_count = 0;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        if (fabsf(output1[i] - output2[i]) > 0.0001f) {
            different_count++;
        }
    }
    ASSERT_GT(different_count, 0);

    embedding_engine_destroy(engine);
}

/* Test cosine similarity */
TEST(embedding_cosine_similarity) {
    /* Identical vectors should have similarity 1.0 */
    float vec1[EMBEDDING_DIM];
    float vec2[EMBEDDING_DIM];

    for (int i = 0; i < EMBEDDING_DIM; i++) {
        vec1[i] = (float)i / EMBEDDING_DIM;
        vec2[i] = (float)i / EMBEDDING_DIM;
    }

    embedding_normalize(vec1);
    embedding_normalize(vec2);

    float sim = embedding_cosine_similarity(vec1, vec2);
    ASSERT_FLOAT_EQ(sim, 1.0f, 0.0001f);

    /* Orthogonal vectors should have similarity ~0.0 */
    /* Create orthogonal by negating every other element */
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        if (i % 2 == 0) {
            vec2[i] = vec1[i];
        } else {
            vec2[i] = -vec1[i];
        }
    }
    embedding_normalize(vec2);

    sim = embedding_cosine_similarity(vec1, vec2);
    /* Should be close to 0, but not exactly due to construction */
    ASSERT_LT(fabsf(sim), 0.2f);
}

/* Test mean pooling */
TEST(embedding_mean_pool) {
    const float emb1[EMBEDDING_DIM] = {1.0f, 0.0f, 0.0f};  /* First 3 dims, rest 0 */
    const float emb2[EMBEDDING_DIM] = {0.0f, 1.0f, 0.0f};
    const float emb3[EMBEDDING_DIM] = {0.0f, 0.0f, 1.0f};

    const float* embeddings[] = {emb1, emb2, emb3};
    float pooled[EMBEDDING_DIM];

    embedding_mean_pool(embeddings, 3, pooled);

    /* Mean should be ~(0.333, 0.333, 0.333) but normalized */
    /* After normalization: (0.577, 0.577, 0.577, 0, 0, ...) */
    float expected = 1.0f / sqrtf(3.0f);  /* ~0.577 */

    ASSERT_FLOAT_EQ(pooled[0], expected, 0.01f);
    ASSERT_FLOAT_EQ(pooled[1], expected, 0.01f);
    ASSERT_FLOAT_EQ(pooled[2], expected, 0.01f);
}

/* Test normalization */
TEST(embedding_normalize) {
    float vec[EMBEDDING_DIM];

    /* Create unnormalized vector */
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        vec[i] = (float)(i + 1);
    }

    /* Calculate original magnitude */
    float orig_mag = 0.0f;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        orig_mag += vec[i] * vec[i];
    }
    orig_mag = sqrtf(orig_mag);
    ASSERT_GT(orig_mag, 1.0f);  /* Should be large */

    /* Normalize */
    embedding_normalize(vec);

    /* Check magnitude is now 1.0 */
    float new_mag = 0.0f;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        new_mag += vec[i] * vec[i];
    }
    new_mag = sqrtf(new_mag);
    ASSERT_FLOAT_EQ(new_mag, 1.0f, 0.0001f);
}

/* Test ONNX availability check */
TEST(embedding_onnx_check) {
    /* Just verify the function doesn't crash */
    bool available = embedding_onnx_available();
    /* Either true or false is valid */
    (void)available;
}

/* Test invalid arguments */
TEST(embedding_invalid_args) {
    ASSERT_NE(embedding_engine_create(NULL, NULL), MEM_OK);

    embedding_engine_t* engine = NULL;
    ASSERT_OK(embedding_engine_create(&engine, NULL));

    float output[EMBEDDING_DIM];
    ASSERT_NE(embedding_generate(NULL, "test", 4, output), MEM_OK);
    ASSERT_NE(embedding_generate(engine, "test", 4, NULL), MEM_OK);

    embedding_engine_destroy(engine);
}

TEST_MAIN()
