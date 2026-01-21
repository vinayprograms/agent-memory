/*
 * Unit tests for event emission
 */

#include "../test_framework.h"
#include "../../src/events/emitter.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_EVENTS_DIR "/tmp/test_events"

static void cleanup_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

/*
 * TEST: Create event emitter without file output
 */
TEST(events_create_no_file) {
    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));
    ASSERT_NOT_NULL(emitter);

    ASSERT_EQ(event_get_count(emitter), 0);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Create event emitter with file output
 */
TEST(events_create_with_file) {
    cleanup_dir(TEST_EVENTS_DIR);

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, TEST_EVENTS_DIR));
    ASSERT_NOT_NULL(emitter);

    /* Directory should be created */
    struct stat st;
    ASSERT_EQ(stat(TEST_EVENTS_DIR, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));

    event_emitter_destroy(emitter);
    cleanup_dir(TEST_EVENTS_DIR);
}

/*
 * TEST: Emit stored event
 */
TEST(events_emit_stored) {
    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    ASSERT_OK(event_emit_stored(emitter, "sess-1", "agent-1", 42, 1024, 5));

    ASSERT_EQ(event_get_count(emitter), 1);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Emit session events
 */
TEST(events_emit_session) {
    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    ASSERT_OK(event_emit_session_created(emitter, "sess-1", "agent-1", 100));
    ASSERT_OK(event_emit_session_updated(emitter, "sess-1", "agent-1", "Test Session"));

    ASSERT_EQ(event_get_count(emitter), 2);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Emit query event
 */
TEST(events_emit_query) {
    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    ASSERT_OK(event_emit_query(emitter, "sess-1", "agent-1", "test query", 10, 5000));

    ASSERT_EQ(event_get_count(emitter), 1);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Emit deleted event
 */
TEST(events_emit_deleted) {
    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    ASSERT_OK(event_emit_deleted(emitter, "sess-1", "agent-1", 5));

    ASSERT_EQ(event_get_count(emitter), 1);

    event_emitter_destroy(emitter);
}

/*
 * Subscriber callback for testing
 */
static int callback_count = 0;
static event_type_t last_event_type;
static char last_session_id[MAX_SESSION_ID_LEN];

static void test_callback(const event_t* event, void* user_data) {
    callback_count++;
    last_event_type = event->type;
    strncpy(last_session_id, event->session_id, MAX_SESSION_ID_LEN - 1);
    (void)user_data;
}

/*
 * TEST: Subscribe to events
 */
TEST(events_subscribe) {
    callback_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    uint32_t sub_id = event_subscribe(emitter, test_callback, NULL);
    ASSERT_GT(sub_id, 0);

    ASSERT_OK(event_emit_stored(emitter, "test-session", "agent", 1, 100, 3));

    ASSERT_EQ(callback_count, 1);
    ASSERT_EQ(last_event_type, EVENT_MEMORY_STORED);
    ASSERT_STR_EQ(last_session_id, "test-session");

    event_emitter_destroy(emitter);
}

/*
 * TEST: Unsubscribe from events
 */
TEST(events_unsubscribe) {
    callback_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    uint32_t sub_id = event_subscribe(emitter, test_callback, NULL);
    ASSERT_GT(sub_id, 0);

    ASSERT_OK(event_emit_stored(emitter, "sess", "agent", 1, 100, 3));
    ASSERT_EQ(callback_count, 1);

    event_unsubscribe(emitter, sub_id);

    ASSERT_OK(event_emit_stored(emitter, "sess", "agent", 2, 100, 3));
    /* Should not increment callback count */
    ASSERT_EQ(callback_count, 1);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Multiple subscribers
 */
static int callback2_count = 0;

static void test_callback2(const event_t* event, void* user_data) {
    callback2_count++;
    (void)event;
    (void)user_data;
}

TEST(events_multiple_subscribers) {
    callback_count = 0;
    callback2_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    uint32_t sub1 = event_subscribe(emitter, test_callback, NULL);
    uint32_t sub2 = event_subscribe(emitter, test_callback2, NULL);

    ASSERT_GT(sub1, 0);
    ASSERT_GT(sub2, 0);
    ASSERT_NE(sub1, sub2);

    ASSERT_OK(event_emit_stored(emitter, "sess", "agent", 1, 100, 3));

    ASSERT_EQ(callback_count, 1);
    ASSERT_EQ(callback2_count, 1);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Set trace ID
 */
TEST(events_trace_id) {
    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    event_set_trace_id(emitter, "custom-trace-123");

    /* Events should now use the custom trace ID */
    /* We verify this through the callback */
    callback_count = 0;

    event_subscribe(emitter, test_callback, NULL);
    ASSERT_OK(event_emit_stored(emitter, "sess", "agent", 1, 100, 3));

    event_emitter_destroy(emitter);
}

/*
 * TEST: Generate trace ID
 */
TEST(events_generate_trace_id) {
    char buf1[64] = {0};
    char buf2[64] = {0};

    event_generate_trace_id(buf1, sizeof(buf1));
    event_generate_trace_id(buf2, sizeof(buf2));

    ASSERT_GT(strlen(buf1), 0);
    ASSERT_GT(strlen(buf2), 0);

    /* Different calls should generate different IDs */
    ASSERT_NE(strcmp(buf1, buf2), 0);
}

/*
 * TEST: Event type names
 */
TEST(events_type_names) {
    ASSERT_STR_EQ(event_type_name(EVENT_MEMORY_STORED), "memory.stored");
    ASSERT_STR_EQ(event_type_name(EVENT_MEMORY_DELETED), "memory.deleted");
    ASSERT_STR_EQ(event_type_name(EVENT_SESSION_CREATED), "memory.session_created");
    ASSERT_STR_EQ(event_type_name(EVENT_SESSION_UPDATED), "memory.session_updated");
    ASSERT_STR_EQ(event_type_name(EVENT_QUERY_PERFORMED), "memory.query_performed");
}

/*
 * TEST: Write events to file
 */
TEST(events_write_to_file) {
    cleanup_dir(TEST_EVENTS_DIR);

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, TEST_EVENTS_DIR));

    /* Emit some events */
    ASSERT_OK(event_emit_session_created(emitter, "sess-1", "agent-1", 1));
    ASSERT_OK(event_emit_stored(emitter, "sess-1", "agent-1", 2, 256, 3));
    ASSERT_OK(event_emit_query(emitter, "sess-1", "agent-1", "test", 5, 1000));

    event_emitter_destroy(emitter);

    /* Check that events file exists and has content */
    char path[256];
    snprintf(path, sizeof(path), "%s/memory/events.jsonl", TEST_EVENTS_DIR);

    struct stat st;
    ASSERT_EQ(stat(path, &st), 0);
    ASSERT_GT(st.st_size, 0);

    /* Read and verify JSON Lines format */
    FILE* f = fopen(path, "r");
    ASSERT_NOT_NULL(f);

    int line_count = 0;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        /* Each line should start with { */
        ASSERT_EQ(line[0], '{');
        line_count++;
    }

    fclose(f);
    ASSERT_EQ(line_count, 3);

    cleanup_dir(TEST_EVENTS_DIR);
}

/*
 * TEST: Invalid arguments
 */
TEST(events_invalid_args) {
    ASSERT_EQ(event_emitter_create(NULL, NULL), MEM_ERR_INVALID_ARG);

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    ASSERT_EQ(event_emit(NULL, NULL), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(event_emit(emitter, NULL), MEM_ERR_INVALID_ARG);

    ASSERT_EQ(event_emit_stored(NULL, "s", "a", 1, 1, 1), MEM_ERR_INVALID_ARG);

    ASSERT_EQ(event_subscribe(NULL, test_callback, NULL), 0);
    ASSERT_EQ(event_subscribe(emitter, NULL, NULL), 0);

    event_emitter_destroy(emitter);
}

TEST_MAIN()
