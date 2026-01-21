/*
 * Tokenizer Unit Tests
 */

#include "../test_framework.h"
#include "../../src/embedding/tokenizer.h"

#include <stdlib.h>
#include <string.h>

/*
 * TEST: Create and destroy default tokenizer
 */
TEST(tokenizer_create_default) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));
    ASSERT_NOT_NULL(tok);
    ASSERT_TRUE(tokenizer_has_vocab(tok));
    ASSERT_GT(tokenizer_vocab_size(tok), 0);
    tokenizer_destroy(tok);
}

/*
 * TEST: Create tokenizer with NULL path
 */
TEST(tokenizer_create_null_path) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create(&tok, NULL));
    ASSERT_NOT_NULL(tok);
    ASSERT_FALSE(tokenizer_has_vocab(tok));
    tokenizer_destroy(tok);
}

/*
 * TEST: Create tokenizer with non-existent path
 */
TEST(tokenizer_create_missing_file) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create(&tok, "/nonexistent/path/vocab.txt"));
    ASSERT_NOT_NULL(tok);
    ASSERT_FALSE(tokenizer_has_vocab(tok));  /* Falls back gracefully */
    tokenizer_destroy(tok);
}

/*
 * TEST: Encode simple text
 */
TEST(tokenizer_encode_simple) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    const char* text = "hello world";
    tokenizer_output_t output;

    ASSERT_OK(tokenizer_encode(tok, text, strlen(text), 128, &output));

    /* Should have at least [CLS], some tokens, [SEP] */
    ASSERT_GE(output.length, 3);
    ASSERT_EQ(output.input_ids[0], TOKEN_CLS);
    ASSERT_EQ(output.input_ids[output.length - 1], TOKEN_SEP);

    /* Attention mask should be 1 for real tokens, 0 for padding */
    for (size_t i = 0; i < output.length; i++) {
        ASSERT_EQ(output.attention_mask[i], 1);
    }
    for (size_t i = output.length; i < output.max_length; i++) {
        ASSERT_EQ(output.attention_mask[i], 0);
    }

    tokenizer_output_free(&output);
    tokenizer_destroy(tok);
}

/*
 * TEST: Encode empty text
 */
TEST(tokenizer_encode_empty) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    tokenizer_output_t output;
    ASSERT_OK(tokenizer_encode(tok, "", 0, 128, &output));

    /* Should have [CLS] [SEP] */
    ASSERT_EQ(output.length, 2);
    ASSERT_EQ(output.input_ids[0], TOKEN_CLS);
    ASSERT_EQ(output.input_ids[1], TOKEN_SEP);

    tokenizer_output_free(&output);
    tokenizer_destroy(tok);
}

/*
 * TEST: Encode long text with truncation
 */
TEST(tokenizer_encode_truncation) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    /* Create a long text */
    char long_text[1000];
    for (int i = 0; i < 999; i++) {
        long_text[i] = 'a' + (i % 26);
    }
    long_text[999] = '\0';

    tokenizer_output_t output;
    size_t max_len = 32;
    ASSERT_OK(tokenizer_encode(tok, long_text, strlen(long_text), max_len, &output));

    /* Output should be exactly max_length */
    ASSERT_LE(output.length, max_len);
    ASSERT_EQ(output.max_length, max_len);

    /* First token should be [CLS] */
    ASSERT_EQ(output.input_ids[0], TOKEN_CLS);

    tokenizer_output_free(&output);
    tokenizer_destroy(tok);
}

/*
 * TEST: Encode batch
 */
TEST(tokenizer_encode_batch) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    const char* texts[] = {"hello world", "foo bar", "test"};
    size_t lengths[] = {11, 7, 4};
    size_t count = 3;

    tokenizer_output_t outputs[3];
    ASSERT_OK(tokenizer_encode_batch(tok, texts, lengths, count, 64, outputs));

    /* All should have [CLS] at start and [SEP] at end */
    for (size_t i = 0; i < count; i++) {
        ASSERT_GE(outputs[i].length, 2);
        ASSERT_EQ(outputs[i].input_ids[0], TOKEN_CLS);
        ASSERT_EQ(outputs[i].input_ids[outputs[i].length - 1], TOKEN_SEP);
        tokenizer_output_free(&outputs[i]);
    }

    tokenizer_destroy(tok);
}

/*
 * TEST: Token to ID lookup
 */
TEST(tokenizer_token_to_id) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    /* Special tokens should be found */
    ASSERT_EQ(tokenizer_token_to_id(tok, "[PAD]"), TOKEN_PAD);
    ASSERT_EQ(tokenizer_token_to_id(tok, "[CLS]"), TOKEN_CLS);
    ASSERT_EQ(tokenizer_token_to_id(tok, "[SEP]"), TOKEN_SEP);
    ASSERT_EQ(tokenizer_token_to_id(tok, "[UNK]"), TOKEN_UNK);

    /* Unknown tokens return UNK */
    ASSERT_EQ(tokenizer_token_to_id(tok, "nonexistent_xyz_123"), TOKEN_UNK);

    tokenizer_destroy(tok);
}

/*
 * TEST: Common words are in vocabulary
 */
TEST(tokenizer_common_words) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    /* These common words should be in the default vocabulary */
    int32_t the_id = tokenizer_token_to_id(tok, "the");
    int32_t and_id = tokenizer_token_to_id(tok, "and");
    int32_t is_id = tokenizer_token_to_id(tok, "is");

    ASSERT_NE(the_id, TOKEN_UNK);
    ASSERT_NE(and_id, TOKEN_UNK);
    ASSERT_NE(is_id, TOKEN_UNK);

    tokenizer_destroy(tok);
}

/*
 * TEST: Token type IDs are all zero for single sequence
 */
TEST(tokenizer_token_type_ids) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    const char* text = "test sentence";
    tokenizer_output_t output;

    ASSERT_OK(tokenizer_encode(tok, text, strlen(text), 128, &output));

    /* All token_type_ids should be 0 for single sequence */
    for (size_t i = 0; i < output.max_length; i++) {
        ASSERT_EQ(output.token_type_ids[i], 0);
    }

    tokenizer_output_free(&output);
    tokenizer_destroy(tok);
}

/*
 * TEST: Output free is idempotent
 */
TEST(tokenizer_output_free_idempotent) {
    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    tokenizer_output_t output;
    ASSERT_OK(tokenizer_encode(tok, "test", 4, 32, &output));

    tokenizer_output_free(&output);
    tokenizer_output_free(&output);  /* Should not crash */

    tokenizer_destroy(tok);
}

/*
 * TEST: Invalid arguments
 */
TEST(tokenizer_invalid_args) {
    ASSERT_EQ(tokenizer_create(NULL, NULL), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(tokenizer_create_default(NULL), MEM_ERR_INVALID_ARG);

    tokenizer_t* tok = NULL;
    ASSERT_OK(tokenizer_create_default(&tok));

    tokenizer_output_t output;
    ASSERT_EQ(tokenizer_encode(NULL, "test", 4, 32, &output), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(tokenizer_encode(tok, "test", 4, 32, NULL), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(tokenizer_encode(tok, "test", 4, 2, &output), MEM_ERR_INVALID_ARG);  /* max_len too small */

    tokenizer_destroy(tok);
}

TEST_MAIN()
