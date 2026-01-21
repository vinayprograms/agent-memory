/*
 * Memory Service - Embedding Generation Implementation
 *
 * When ONNX Runtime is available (HAVE_ONNXRUNTIME defined),
 * uses real model inference. Otherwise, uses stub implementation.
 */

#include "embedding.h"
#include "tokenizer.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_c_api.h>
#ifdef __APPLE__
/* CoreML provider header - may not exist in all ONNX Runtime builds */
#if __has_include(<coreml_provider_factory.h>)
#include <coreml_provider_factory.h>
#define HAVE_COREML_PROVIDER 1
#endif
#endif
#endif

struct embedding_engine {
    embedding_config_t config;
    bool onnx_available;
    tokenizer_t* tokenizer;

#ifdef HAVE_ONNXRUNTIME
    const OrtApi* api;
    OrtEnv* env;
    OrtSession* session;
    OrtSessionOptions* session_options;
    OrtAllocator* allocator;
    OrtMemoryInfo* memory_info;

    /* Detected model inputs */
    bool has_input_ids;
    bool has_attention_mask;
    bool has_token_type_ids;
    size_t input_count;

    /* Detected model outputs */
    char* output_name;  /* Dynamically detected output name */
#endif
};

/*
 * SIMD-optimized dot product
 * TODO: Add AVX2/NEON implementations for better performance
 */
static float dot_product_scalar(const float* a, const float* b, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}

static float magnitude_scalar(const float* v, size_t n) {
    return sqrtf(dot_product_scalar(v, v, n));
}

bool embedding_onnx_available(void) {
#ifdef HAVE_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

#ifdef HAVE_ONNXRUNTIME

/* ONNX Runtime implementation */

static void check_ort_status(OrtStatus* status, const OrtApi* api) {
    if (status != NULL) {
        const char* msg = api->GetErrorMessage(status);
        LOG_ERROR("ONNX Runtime error: %s", msg);
        api->ReleaseStatus(status);
    }
}

/* Get vocabulary path from model path (replace .onnx with vocab.txt) */
static char* get_vocab_path(const char* model_path) {
    if (!model_path) return NULL;

    size_t len = strlen(model_path);
    char* vocab_path = malloc(len + 16);
    if (!vocab_path) return NULL;

    /* Find directory */
    const char* last_slash = strrchr(model_path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - model_path + 1;
        memcpy(vocab_path, model_path, dir_len);
        strcpy(vocab_path + dir_len, "vocab.txt");
    } else {
        strcpy(vocab_path, "vocab.txt");
    }

    return vocab_path;
}

mem_error_t embedding_engine_create(embedding_engine_t** engine,
                                    const embedding_config_t* config) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");

    embedding_engine_t* e = calloc(1, sizeof(embedding_engine_t));
    MEM_CHECK_ALLOC(e);

    if (config) {
        e->config = *config;
    } else {
        e->config = (embedding_config_t)EMBEDDING_CONFIG_DEFAULT;
    }

    /* Create tokenizer */
    char* vocab_path = get_vocab_path(e->config.model_path);
    mem_error_t tok_err = tokenizer_create(&e->tokenizer, vocab_path);
    free(vocab_path);

    if (tok_err != MEM_OK) {
        /* Fall back to default tokenizer */
        tok_err = tokenizer_create_default(&e->tokenizer);
        if (tok_err != MEM_OK) {
            free(e);
            return tok_err;
        }
    }

    if (!e->config.model_path) {
        /* No model path - use stub */
        e->onnx_available = false;
        *engine = e;
        LOG_WARN("No ONNX model path provided - using stub embeddings");
        return MEM_OK;
    }

    e->api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    const OrtApi* api = e->api;
    OrtStatus* status;

    /* Create environment */
    status = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "memory-service", &e->env);
    if (status) {
        check_ort_status(status, api);
        tokenizer_destroy(e->tokenizer);
        free(e);
        MEM_RETURN_ERROR(MEM_ERR_ONNX, "failed to create ONNX environment");
    }

    /* Create session options */
    status = api->CreateSessionOptions(&e->session_options);
    if (status) {
        check_ort_status(status, api);
        api->ReleaseEnv(e->env);
        tokenizer_destroy(e->tokenizer);
        free(e);
        MEM_RETURN_ERROR(MEM_ERR_ONNX, "failed to create session options");
    }

    /* Set thread count and optimization */
    status = api->SetIntraOpNumThreads(e->session_options, 4);
    if (status) {
        check_ort_status(status, api);
        /* Non-fatal, continue */
    }
    status = api->SetSessionGraphOptimizationLevel(e->session_options, ORT_ENABLE_ALL);
    if (status) {
        check_ort_status(status, api);
        /* Non-fatal, continue */
    }

    /* Try to enable hardware acceleration */
    const char* provider_name = "CPU";
#ifdef HAVE_COREML_PROVIDER
    /* CoreML provides Metal/ANE acceleration on Apple Silicon */
    uint32_t coreml_flags = 0;  /* 0 = default, use ANE if available */
    status = OrtSessionOptionsAppendExecutionProvider_CoreML(e->session_options, coreml_flags);
    if (status) {
        LOG_DEBUG("CoreML not available: %s", api->GetErrorMessage(status));
        api->ReleaseStatus(status);
        /* Fall back to CPU - not an error */
    } else {
        provider_name = "CoreML";
    }
#endif
    /* Future: Could add CUDA provider for Linux GPU support */

    /* Create session */
    const char* model_path = e->config.model_path;  /* Save before potential free */
    status = api->CreateSession(e->env, model_path,
                                e->session_options, &e->session);
    if (status) {
        check_ort_status(status, api);
        api->ReleaseSessionOptions(e->session_options);
        api->ReleaseEnv(e->env);
        tokenizer_destroy(e->tokenizer);
        free(e);
        MEM_RETURN_ERROR(MEM_ERR_ONNX_LOAD, "failed to load ONNX model from %s",
                        model_path);
    }

    /* Get allocator (needed for introspection) */
    status = api->GetAllocatorWithDefaultOptions(&e->allocator);
    if (status) {
        check_ort_status(status, api);
        api->ReleaseSession(e->session);
        api->ReleaseSessionOptions(e->session_options);
        api->ReleaseEnv(e->env);
        tokenizer_destroy(e->tokenizer);
        free(e);
        MEM_RETURN_ERROR(MEM_ERR_ONNX, "failed to get ONNX allocator");
    }

    /* Introspect model inputs */
    e->has_input_ids = false;
    e->has_attention_mask = false;
    e->has_token_type_ids = false;
    e->input_count = 0;

    size_t num_inputs;
    status = api->SessionGetInputCount(e->session, &num_inputs);
    if (status) {
        check_ort_status(status, api);
    } else {
        for (size_t i = 0; i < num_inputs; i++) {
            char* input_name = NULL;
            status = api->SessionGetInputName(e->session, i, e->allocator, &input_name);
            if (status) {
                check_ort_status(status, api);
                continue;
            }
            if (input_name) {
                if (strcmp(input_name, "input_ids") == 0) {
                    e->has_input_ids = true;
                } else if (strcmp(input_name, "attention_mask") == 0) {
                    e->has_attention_mask = true;
                } else if (strcmp(input_name, "token_type_ids") == 0) {
                    e->has_token_type_ids = true;
                }
                (void)api->AllocatorFree(e->allocator, input_name);
            }
        }
        e->input_count = num_inputs;
    }

    LOG_DEBUG("Model inputs detected: input_ids=%d attention_mask=%d token_type_ids=%d",
              e->has_input_ids, e->has_attention_mask, e->has_token_type_ids);

    /* Introspect model outputs */
    e->output_name = NULL;
    size_t num_outputs;
    status = api->SessionGetOutputCount(e->session, &num_outputs);
    if (status) {
        check_ort_status(status, api);
    } else if (num_outputs > 0) {
        char* output_name = NULL;
        status = api->SessionGetOutputName(e->session, 0, e->allocator, &output_name);
        if (!status && output_name) {
            e->output_name = strdup(output_name);
            (void)api->AllocatorFree(e->allocator, output_name);
            if (!e->output_name) {
                api->ReleaseSession(e->session);
                api->ReleaseSessionOptions(e->session_options);
                api->ReleaseEnv(e->env);
                tokenizer_destroy(e->tokenizer);
                free(e);
                MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate output name string");
            }
            LOG_DEBUG("Model output detected: %s", e->output_name);
        }
    }

    /* Validate required inputs */
    if (!e->has_input_ids || !e->has_attention_mask) {
        LOG_WARN("Model missing required inputs (input_ids=%d, attention_mask=%d)",
                 e->has_input_ids, e->has_attention_mask);
    }

    /* Create CPU memory info */
    status = api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &e->memory_info);
    if (status) {
        check_ort_status(status, api);
        api->ReleaseSession(e->session);
        api->ReleaseSessionOptions(e->session_options);
        api->ReleaseEnv(e->env);
        tokenizer_destroy(e->tokenizer);
        free(e->output_name);  /* May have been allocated during output introspection */
        free(e);
        MEM_RETURN_ERROR(MEM_ERR_ONNX, "failed to create memory info");
    }

    e->onnx_available = true;
    *engine = e;
    LOG_INFO("ONNX embedding engine initialized (provider=%s, model=%s)",
             provider_name, e->config.model_path);
    return MEM_OK;
}

void embedding_engine_destroy(embedding_engine_t* engine) {
    if (!engine) return;

    if (engine->tokenizer) {
        tokenizer_destroy(engine->tokenizer);
    }

#ifdef HAVE_ONNXRUNTIME
    if (engine->output_name) {
        free(engine->output_name);
    }

    if (engine->onnx_available && engine->api) {
        const OrtApi* api = engine->api;
        if (engine->memory_info) api->ReleaseMemoryInfo(engine->memory_info);
        if (engine->session) api->ReleaseSession(engine->session);
        if (engine->session_options) api->ReleaseSessionOptions(engine->session_options);
        if (engine->env) api->ReleaseEnv(engine->env);
    }
#endif

    free(engine);
}

mem_error_t embedding_generate(embedding_engine_t* engine,
                               const char* text, size_t text_len,
                               float* output) {
    return embedding_generate_batch(engine, &text, &text_len, 1, output);
}

/* Run ONNX inference for a batch of tokenized inputs */
static mem_error_t run_inference(embedding_engine_t* engine,
                                 const tokenizer_output_t* tokens,
                                 size_t batch_size,
                                 size_t seq_len,
                                 float* outputs) {
    const OrtApi* api = engine->api;
    OrtStatus* status;
    mem_error_t result = MEM_OK;

    /* Prepare input tensors */
    size_t input_size = batch_size * seq_len;
    int64_t input_shape[2] = {(int64_t)batch_size, (int64_t)seq_len};

    /* Allocate and fill input arrays */
    int64_t* input_ids_data = malloc(input_size * sizeof(int64_t));
    int64_t* attention_mask_data = malloc(input_size * sizeof(int64_t));
    int64_t* token_type_ids_data = NULL;

    if (engine->has_token_type_ids) {
        token_type_ids_data = malloc(input_size * sizeof(int64_t));
    }

    if (!input_ids_data || !attention_mask_data ||
        (engine->has_token_type_ids && !token_type_ids_data)) {
        free(input_ids_data);
        free(attention_mask_data);
        free(token_type_ids_data);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate input tensors");
    }

    /* Copy tokenized data to contiguous arrays */
    for (size_t b = 0; b < batch_size; b++) {
        for (size_t s = 0; s < seq_len; s++) {
            size_t idx = b * seq_len + s;
            input_ids_data[idx] = tokens[b].input_ids[s];
            attention_mask_data[idx] = tokens[b].attention_mask[s];
            if (engine->has_token_type_ids) {
                token_type_ids_data[idx] = tokens[b].token_type_ids[s];
            }
        }
    }

    /* Create input tensors */
    OrtValue* input_ids_tensor = NULL;
    OrtValue* attention_mask_tensor = NULL;
    OrtValue* token_type_ids_tensor = NULL;
    OrtValue* output_tensor = NULL;

    status = api->CreateTensorWithDataAsOrtValue(
        engine->memory_info, input_ids_data, input_size * sizeof(int64_t),
        input_shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &input_ids_tensor);
    if (status) {
        check_ort_status(status, api);
        result = MEM_ERR_ONNX_INFER;
        goto cleanup;
    }

    status = api->CreateTensorWithDataAsOrtValue(
        engine->memory_info, attention_mask_data, input_size * sizeof(int64_t),
        input_shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &attention_mask_tensor);
    if (status) {
        check_ort_status(status, api);
        result = MEM_ERR_ONNX_INFER;
        goto cleanup;
    }

    if (engine->has_token_type_ids) {
        status = api->CreateTensorWithDataAsOrtValue(
            engine->memory_info, token_type_ids_data, input_size * sizeof(int64_t),
            input_shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &token_type_ids_tensor);
        if (status) {
            check_ort_status(status, api);
            result = MEM_ERR_ONNX_INFER;
            goto cleanup;
        }
    }

    /* Build input arrays dynamically based on detected model inputs */
    const char* input_names[3];
    OrtValue* inputs[3];
    size_t num_inputs = 0;

    if (engine->has_input_ids) {
        input_names[num_inputs] = "input_ids";
        inputs[num_inputs] = input_ids_tensor;
        num_inputs++;
    }
    if (engine->has_attention_mask) {
        input_names[num_inputs] = "attention_mask";
        inputs[num_inputs] = attention_mask_tensor;
        num_inputs++;
    }
    if (engine->has_token_type_ids) {
        input_names[num_inputs] = "token_type_ids";
        inputs[num_inputs] = token_type_ids_tensor;
        num_inputs++;
    }

    /* Use detected output name or fall back to common names */
    const char* output_name = engine->output_name ? engine->output_name : "last_hidden_state";
    const char* output_names[] = {output_name};

    status = api->Run(engine->session, NULL, input_names, (const OrtValue* const*)inputs, num_inputs,
                      output_names, 1, &output_tensor);
    if (status) {
        check_ort_status(status, api);
        result = MEM_ERR_ONNX_INFER;
        goto cleanup;
    }

    /* Extract output - shape is [batch, seq_len, hidden_size] */
    float* output_data;
    status = api->GetTensorMutableData(output_tensor, (void**)&output_data);
    if (status) {
        check_ort_status(status, api);
        result = MEM_ERR_ONNX_INFER;
        goto cleanup;
    }

    /* Mean pooling over sequence dimension with attention mask */
    for (size_t b = 0; b < batch_size; b++) {
        float* batch_output = outputs + b * EMBEDDING_DIM;
        memset(batch_output, 0, EMBEDDING_DIM * sizeof(float));

        float token_count = 0.0f;
        for (size_t s = 0; s < seq_len; s++) {
            if (attention_mask_data[b * seq_len + s] == 1) {
                float* hidden = output_data + (b * seq_len + s) * EMBEDDING_DIM;
                for (size_t d = 0; d < EMBEDDING_DIM; d++) {
                    batch_output[d] += hidden[d];
                }
                token_count += 1.0f;
            }
        }

        /* Average and normalize */
        if (token_count > 0.0f) {
            for (size_t d = 0; d < EMBEDDING_DIM; d++) {
                batch_output[d] /= token_count;
            }
            embedding_normalize(batch_output);
        }
    }

cleanup:
    if (output_tensor) api->ReleaseValue(output_tensor);
    if (token_type_ids_tensor) api->ReleaseValue(token_type_ids_tensor);
    if (attention_mask_tensor) api->ReleaseValue(attention_mask_tensor);
    if (input_ids_tensor) api->ReleaseValue(input_ids_tensor);
    free(input_ids_data);
    free(attention_mask_data);
    free(token_type_ids_data);

    return result;
}

mem_error_t embedding_generate_batch(embedding_engine_t* engine,
                                     const char** texts,
                                     const size_t* lengths,
                                     size_t count,
                                     float* outputs) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");
    MEM_CHECK_ERR(outputs != NULL, MEM_ERR_INVALID_ARG, "outputs is NULL");

    if (!engine->onnx_available) {
        /* Fall through to hash-based pseudo-embedding */
        goto pseudo_embedding;
    }

    /* Tokenize all texts */
    size_t max_seq_len = engine->config.max_seq_len;
    tokenizer_output_t* tokens = calloc(count, sizeof(tokenizer_output_t));
    if (!tokens) {
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate token outputs");
    }

    mem_error_t err = tokenizer_encode_batch(engine->tokenizer, texts, lengths,
                                              count, max_seq_len, tokens);
    if (err != MEM_OK) {
        free(tokens);
        return err;
    }

    /* Run inference */
    err = run_inference(engine, tokens, count, max_seq_len, outputs);

    /* Cleanup tokens */
    for (size_t i = 0; i < count; i++) {
        tokenizer_output_free(&tokens[i]);
    }
    free(tokens);

    if (err == MEM_OK) {
        return MEM_OK;
    }

    LOG_WARN("ONNX inference failed, falling back to pseudo-embedding");

pseudo_embedding:
    /* Generate deterministic pseudo-embeddings based on text hash */
    for (size_t i = 0; i < count; i++) {
        const char* text = texts[i];
        size_t len = lengths[i];

        /* Simple hash-based pseudo-embedding */
        uint32_t hash = 0;
        for (size_t j = 0; j < len; j++) {
            hash = hash * 31 + (uint8_t)text[j];
        }

        float* out = outputs + i * EMBEDDING_DIM;
        for (size_t j = 0; j < EMBEDDING_DIM; j++) {
            /* Generate deterministic values from hash */
            hash = hash * 1103515245 + 12345;
            out[j] = ((float)(hash & 0x7FFFFFFF) / (float)0x7FFFFFFF) - 0.5f;
        }

        /* Normalize */
        embedding_normalize(out);
    }

    return MEM_OK;
}

#else /* !HAVE_ONNXRUNTIME */

/* Stub implementation when ONNX Runtime is not available */

mem_error_t embedding_engine_create(embedding_engine_t** engine,
                                    const embedding_config_t* config) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");

    embedding_engine_t* e = calloc(1, sizeof(embedding_engine_t));
    MEM_CHECK_ALLOC(e);

    if (config) {
        e->config = *config;
    } else {
        e->config = (embedding_config_t)EMBEDDING_CONFIG_DEFAULT;
    }

    /* Create default tokenizer for text processing */
    mem_error_t tok_err = tokenizer_create_default(&e->tokenizer);
    if (tok_err != MEM_OK) {
        free(e);
        return tok_err;
    }

    e->onnx_available = false;
    *engine = e;

    LOG_WARN("ONNX Runtime not available - using stub embeddings");
    return MEM_OK;
}

void embedding_engine_destroy(embedding_engine_t* engine) {
    if (!engine) return;
    if (engine->tokenizer) {
        tokenizer_destroy(engine->tokenizer);
    }
    free(engine);
}

mem_error_t embedding_generate(embedding_engine_t* engine,
                               const char* text, size_t text_len,
                               float* output) {
    return embedding_generate_batch(engine, &text, &text_len, 1, output);
}

mem_error_t embedding_generate_batch(embedding_engine_t* engine,
                                     const char** texts,
                                     const size_t* lengths,
                                     size_t count,
                                     float* outputs) {
    MEM_CHECK_ERR(engine != NULL, MEM_ERR_INVALID_ARG, "engine is NULL");
    MEM_CHECK_ERR(outputs != NULL, MEM_ERR_INVALID_ARG, "outputs is NULL");
    (void)texts;  /* Suppress unused warning */

    /* Generate deterministic pseudo-embeddings based on text hash */
    for (size_t i = 0; i < count; i++) {
        const char* text = texts[i];
        size_t len = lengths[i];

        /* Simple hash-based pseudo-embedding */
        uint32_t hash = 0;
        for (size_t j = 0; j < len; j++) {
            hash = hash * 31 + (uint8_t)text[j];
        }

        float* out = outputs + i * EMBEDDING_DIM;
        for (size_t j = 0; j < EMBEDDING_DIM; j++) {
            /* Generate deterministic values from hash */
            hash = hash * 1103515245 + 12345;
            out[j] = ((float)(hash & 0x7FFFFFFF) / (float)0x7FFFFFFF) - 0.5f;
        }

        /* Normalize to unit length */
        embedding_normalize(out);
    }

    return MEM_OK;
}

#endif /* HAVE_ONNXRUNTIME */

/* Common implementations */

void embedding_mean_pool(const float** embeddings, size_t count, float* output) {
    if (!embeddings || count == 0 || !output) return;

    /* Initialize output to zeros */
    memset(output, 0, EMBEDDING_DIM * sizeof(float));

    /* Sum all embeddings */
    for (size_t i = 0; i < count; i++) {
        if (!embeddings[i]) continue;
        for (size_t j = 0; j < EMBEDDING_DIM; j++) {
            output[j] += embeddings[i][j];
        }
    }

    /* Divide by count (mean) */
    float inv_count = 1.0f / (float)count;
    for (size_t j = 0; j < EMBEDDING_DIM; j++) {
        output[j] *= inv_count;
    }

    /* Normalize result */
    embedding_normalize(output);
}

float embedding_cosine_similarity(const float* a, const float* b) {
    if (!a || !b) return 0.0f;

    float dot = dot_product_scalar(a, b, EMBEDDING_DIM);
    float mag_a = magnitude_scalar(a, EMBEDDING_DIM);
    float mag_b = magnitude_scalar(b, EMBEDDING_DIM);

    if (mag_a == 0.0f || mag_b == 0.0f) return 0.0f;

    return dot / (mag_a * mag_b);
}

void embedding_normalize(float* embedding) {
    if (!embedding) return;

    float mag = magnitude_scalar(embedding, EMBEDDING_DIM);
    if (mag == 0.0f) return;

    float inv_mag = 1.0f / mag;
    for (size_t i = 0; i < EMBEDDING_DIM; i++) {
        embedding[i] *= inv_mag;
    }
}
