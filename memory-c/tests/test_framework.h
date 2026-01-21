/*
 * Memory Service - Test Framework
 *
 * Minimal test framework for unit and integration testing.
 */

#ifndef MEMORY_SERVICE_TEST_FRAMEWORK_H
#define MEMORY_SERVICE_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* Test result tracking */
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static const char* g_current_test = NULL;

/* ANSI colors */
#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET  "\033[0m"

/* Test registration and running */
typedef void (*test_fn)(void);

typedef struct {
    const char* name;
    test_fn     fn;
} test_case_t;

#define MAX_TESTS 256
static test_case_t g_tests[MAX_TESTS];
static int g_test_count = 0;

/* Register a test */
#define TEST(test_name) \
    static void test_##test_name(void); \
    __attribute__((constructor)) static void register_test_##test_name(void) { \
        if (g_test_count < MAX_TESTS) { \
            g_tests[g_test_count].name = #test_name; \
            g_tests[g_test_count].fn = test_##test_name; \
            g_test_count++; \
        } \
    } \
    static void test_##test_name(void)

/* Assertion macros */
#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_MSG(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s - %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #cond, msg); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s == %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s != %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_LT(a, b) do { \
    if ((a) >= (b)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s < %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_LE(a, b) do { \
    if ((a) > (b)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s <= %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_GT(a, b) do { \
    if ((a) <= (b)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s > %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_GE(a, b) do { \
    if ((a) < (b)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s >= %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s should be true" COLOR_RESET "\n", \
                __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s should be false" COLOR_RESET "\n", \
                __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s should be NULL" COLOR_RESET "\n", \
                __FILE__, __LINE__, #ptr); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %s should not be NULL" COLOR_RESET "\n", \
                __FILE__, __LINE__, #ptr); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: \"%s\" != \"%s\"" COLOR_RESET "\n", \
                __FILE__, __LINE__, (a), (b)); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_MEM_EQ(a, b, len) do { \
    if (memcmp((a), (b), (len)) != 0) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: memory mismatch" COLOR_RESET "\n", \
                __FILE__, __LINE__); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabs((a) - (b)) > (eps)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: %f != %f (eps=%f)" COLOR_RESET "\n", \
                __FILE__, __LINE__, (double)(a), (double)(b), (double)(eps)); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_OK(err) do { \
    if ((err) != MEM_OK) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: expected MEM_OK, got %d" COLOR_RESET "\n", \
                __FILE__, __LINE__, (err)); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_ERR(err, expected) do { \
    if ((err) != (expected)) { \
        fprintf(stderr, COLOR_RED "  FAIL: %s:%d: expected error %d, got %d" COLOR_RESET "\n", \
                __FILE__, __LINE__, (expected), (err)); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

/* Skip test */
#define SKIP_TEST(reason) do { \
    fprintf(stderr, COLOR_YELLOW "  SKIP: %s" COLOR_RESET "\n", reason); \
    return; \
} while(0)

/* Run all registered tests */
static inline int run_tests(void) {
    printf("\n========================================\n");
    printf("Running %d tests\n", g_test_count);
    printf("========================================\n\n");

    for (int i = 0; i < g_test_count; i++) {
        g_current_test = g_tests[i].name;
        int failed_before = g_tests_failed;

        printf("%-50s ", g_tests[i].name);
        fflush(stdout);

        g_tests[i].fn();
        g_tests_run++;

        if (g_tests_failed == failed_before) {
            g_tests_passed++;
            printf(COLOR_GREEN "[PASS]" COLOR_RESET "\n");
        } else {
            printf(COLOR_RED "[FAIL]" COLOR_RESET "\n");
        }
    }

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           g_tests_passed, g_tests_failed, g_tests_run);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}

/* Main entry for test executables */
#define TEST_MAIN() \
    int main(int argc, char** argv) { \
        (void)argc; (void)argv; \
        return run_tests(); \
    }

#endif /* MEMORY_SERVICE_TEST_FRAMEWORK_H */
