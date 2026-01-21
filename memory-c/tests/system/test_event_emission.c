/*
 * SVC_MEM_TEST_0017 - Verify event emission
 * SVC_MEM_TEST_0018 - Verify session lifecycle events
 *
 * Test specification:
 * - Subscribe to memory.* events
 * - Store new message -> memory.stored event MUST arrive within 100ms
 * - Event MUST contain session_id, agent_id, timestamp
 * - Session create/update -> appropriate lifecycle events emitted
 */

#include "../test_framework.h"
#include "../../src/events/emitter.h"
#include "../../src/session/session.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/test_event_emission"

static void cleanup_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

/* Callback state for testing */
static int event_count = 0;
static event_type_t last_event_type;
static char last_session_id[MAX_SESSION_ID_LEN];
static char last_agent_id[MAX_AGENT_ID_LEN];
static timestamp_ns_t last_timestamp;

static void test_event_callback(const event_t* event, void* user_data) {
    (void)user_data;
    event_count++;
    last_event_type = event->type;
    strncpy(last_session_id, event->session_id, MAX_SESSION_ID_LEN - 1);
    strncpy(last_agent_id, event->agent_id, MAX_AGENT_ID_LEN - 1);
    last_timestamp = event->timestamp;
}

/*
 * TEST: Memory stored event emitted
 */
TEST(event_memory_stored) {
    event_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    /* Subscribe to events */
    uint32_t sub_id = event_subscribe(emitter, test_event_callback, NULL);
    ASSERT_GT(sub_id, 0);

    /* Emit stored event */
    ASSERT_OK(event_emit_stored(emitter, "test-session", "test-agent", 42, 1024, 5));

    /* Verify callback was called */
    ASSERT_EQ(event_count, 1);
    ASSERT_EQ(last_event_type, EVENT_MEMORY_STORED);
    ASSERT_STR_EQ(last_session_id, "test-session");
    ASSERT_STR_EQ(last_agent_id, "test-agent");
    ASSERT_GT(last_timestamp, 0);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Session created event emitted
 */
TEST(event_session_created) {
    event_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    uint32_t sub_id = event_subscribe(emitter, test_event_callback, NULL);
    ASSERT_GT(sub_id, 0);

    /* Emit session created event */
    ASSERT_OK(event_emit_session_created(emitter, "new-session", "creating-agent", 100));

    ASSERT_EQ(event_count, 1);
    ASSERT_EQ(last_event_type, EVENT_SESSION_CREATED);
    ASSERT_STR_EQ(last_session_id, "new-session");
    ASSERT_STR_EQ(last_agent_id, "creating-agent");

    event_emitter_destroy(emitter);
}

/*
 * TEST: Session updated event emitted
 */
TEST(event_session_updated) {
    event_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    uint32_t sub_id = event_subscribe(emitter, test_event_callback, NULL);
    ASSERT_GT(sub_id, 0);

    /* Emit session updated event (e.g., title generated) */
    ASSERT_OK(event_emit_session_updated(emitter, "updated-session", "agent", "OAuth Implementation"));

    ASSERT_EQ(event_count, 1);
    ASSERT_EQ(last_event_type, EVENT_SESSION_UPDATED);
    ASSERT_STR_EQ(last_session_id, "updated-session");

    event_emitter_destroy(emitter);
}

/*
 * TEST: Query event includes latency
 */
TEST(event_query_performed) {
    event_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    uint32_t sub_id = event_subscribe(emitter, test_event_callback, NULL);
    ASSERT_GT(sub_id, 0);

    /* Emit query event with latency */
    ASSERT_OK(event_emit_query(emitter, "query-session", "query-agent",
                              "authentication flow", 10, 5000));

    ASSERT_EQ(event_count, 1);
    ASSERT_EQ(last_event_type, EVENT_QUERY_PERFORMED);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Event contains trace ID
 */
TEST(event_trace_id) {
    cleanup_dir(TEST_DIR);
    mkdir(TEST_DIR, 0755);

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, TEST_DIR));

    /* Set custom trace ID */
    event_set_trace_id(emitter, "custom-trace-12345");

    /* Emit event */
    ASSERT_OK(event_emit_stored(emitter, "traced-session", "agent", 1, 100, 2));

    event_emitter_destroy(emitter);

    /* Read event file and verify trace_id */
    char path[256];
    snprintf(path, sizeof(path), "%s/memory/events.jsonl", TEST_DIR);

    FILE* f = fopen(path, "r");
    ASSERT_NOT_NULL(f);

    char line[4096];
    if (fgets(line, sizeof(line), f)) {
        ASSERT_NOT_NULL(strstr(line, "custom-trace-12345"));
    }

    fclose(f);
    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Events written to file in JSON Lines format
 */
TEST(event_file_output) {
    cleanup_dir(TEST_DIR);
    mkdir(TEST_DIR, 0755);

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, TEST_DIR));

    /* Emit several events */
    ASSERT_OK(event_emit_session_created(emitter, "s1", "a1", 1));
    ASSERT_OK(event_emit_stored(emitter, "s1", "a1", 2, 256, 3));
    ASSERT_OK(event_emit_query(emitter, "s1", "a1", "test", 5, 1000));
    ASSERT_OK(event_emit_session_updated(emitter, "s1", "a1", "Test Title"));

    event_emitter_destroy(emitter);

    /* Verify file contents */
    char path[256];
    snprintf(path, sizeof(path), "%s/memory/events.jsonl", TEST_DIR);

    FILE* f = fopen(path, "r");
    ASSERT_NOT_NULL(f);

    int line_count = 0;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        /* Each line should be valid JSON starting with { */
        ASSERT_EQ(line[0], '{');
        /* Should contain standard event fields */
        ASSERT_NOT_NULL(strstr(line, "\"ts\":"));
        ASSERT_NOT_NULL(strstr(line, "\"event\":"));
        ASSERT_NOT_NULL(strstr(line, "\"trace_id\":"));
        line_count++;
    }

    fclose(f);
    ASSERT_EQ(line_count, 4);

    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Session lifecycle events in sequence
 */
TEST(event_session_lifecycle) {
    event_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    uint32_t sub_id = event_subscribe(emitter, test_event_callback, NULL);
    ASSERT_GT(sub_id, 0);

    /* Simulate session lifecycle */
    ASSERT_OK(event_emit_session_created(emitter, "lifecycle-session", "agent", 1));
    ASSERT_EQ(event_count, 1);
    ASSERT_EQ(last_event_type, EVENT_SESSION_CREATED);

    ASSERT_OK(event_emit_stored(emitter, "lifecycle-session", "agent", 2, 100, 3));
    ASSERT_EQ(event_count, 2);
    ASSERT_EQ(last_event_type, EVENT_MEMORY_STORED);

    ASSERT_OK(event_emit_stored(emitter, "lifecycle-session", "agent", 3, 200, 4));
    ASSERT_EQ(event_count, 3);

    ASSERT_OK(event_emit_session_updated(emitter, "lifecycle-session", "agent", "Generated Title"));
    ASSERT_EQ(event_count, 4);
    ASSERT_EQ(last_event_type, EVENT_SESSION_UPDATED);

    ASSERT_EQ(event_get_count(emitter), 4);

    event_emitter_destroy(emitter);
}

/*
 * TEST: Multiple subscribers receive events
 */
static int callback2_count = 0;

static void test_callback2(const event_t* event, void* user_data) {
    (void)event;
    (void)user_data;
    callback2_count++;
}

TEST(event_multiple_subscribers) {
    event_count = 0;
    callback2_count = 0;

    event_emitter_t* emitter = NULL;
    ASSERT_OK(event_emitter_create(&emitter, NULL));

    uint32_t sub1 = event_subscribe(emitter, test_event_callback, NULL);
    uint32_t sub2 = event_subscribe(emitter, test_callback2, NULL);

    ASSERT_GT(sub1, 0);
    ASSERT_GT(sub2, 0);

    ASSERT_OK(event_emit_stored(emitter, "multi-sub-session", "agent", 1, 100, 2));

    ASSERT_EQ(event_count, 1);
    ASSERT_EQ(callback2_count, 1);

    event_emitter_destroy(emitter);
}

TEST_MAIN()
