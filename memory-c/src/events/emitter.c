/*
 * Memory Service - Event Emission Implementation
 */

#include "emitter.h"
#include "../util/log.h"
#include "../util/time.h"
#include "../../third_party/yyjson/yyjson.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_SUBSCRIBERS 32

/* Subscriber entry */
typedef struct {
    uint32_t id;
    event_callback_t callback;
    void* user_data;
    bool active;
} subscriber_t;

struct event_emitter {
    char events_dir[256];
    bool file_output_enabled;
    FILE* event_file;

    subscriber_t subscribers[MAX_SUBSCRIBERS];
    size_t subscriber_count;
    uint32_t next_subscriber_id;

    char current_trace_id[64];
    uint64_t event_count;

    pthread_mutex_t lock;
};

/* Event type names */
static const char* EVENT_TYPE_NAMES[] = {
    "memory.stored",
    "memory.deleted",
    "memory.session_created",
    "memory.session_updated",
    "memory.query_performed",
};

const char* event_type_name(event_type_t type) {
    if (type >= 0 && type < sizeof(EVENT_TYPE_NAMES) / sizeof(EVENT_TYPE_NAMES[0])) {
        return EVENT_TYPE_NAMES[type];
    }
    return "unknown";
}

void event_generate_trace_id(char* buf, size_t buf_len) {
    /* Simple UUID-like generation using timestamp and random */
    timestamp_ns_t ts = time_now_ns();
    uint32_t r1 = (uint32_t)(ts >> 32);
    uint32_t r2 = (uint32_t)ts;
    uint32_t r3 = (uint32_t)rand();
    uint32_t r4 = (uint32_t)rand();

    snprintf(buf, buf_len, "%08x-%04x-%04x-%04x-%08x%04x",
             r1, (r2 >> 16) & 0xFFFF, r2 & 0xFFFF,
             (r3 >> 16) & 0xFFFF, r3 & 0xFFFF, r4 & 0xFFFF);
}

mem_error_t event_emitter_create(event_emitter_t** emitter, const char* events_dir) {
    if (!emitter) return MEM_ERR_INVALID_ARG;

    event_emitter_t* e = calloc(1, sizeof(event_emitter_t));
    if (!e) return MEM_ERR_NOMEM;

    pthread_mutex_init(&e->lock, NULL);
    e->next_subscriber_id = 1;

    /* Generate initial trace ID */
    event_generate_trace_id(e->current_trace_id, sizeof(e->current_trace_id));

    if (events_dir) {
        snprintf(e->events_dir, sizeof(e->events_dir), "%s", events_dir);
        e->file_output_enabled = true;

        /* Create events directory if needed */
        mkdir(events_dir, 0755);

        /* Create memory topic directory */
        char topic_dir[512];
        snprintf(topic_dir, sizeof(topic_dir), "%s/memory", events_dir);
        mkdir(topic_dir, 0755);

        /* Open event file for appending */
        char event_path[512];
        snprintf(event_path, sizeof(event_path), "%s/memory/events.jsonl", events_dir);
        e->event_file = fopen(event_path, "a");
        if (!e->event_file) {
            LOG_WARN("Could not open event file: %s", event_path);
            e->file_output_enabled = false;
        }
    }

    *emitter = e;
    return MEM_OK;
}

void event_emitter_destroy(event_emitter_t* emitter) {
    if (!emitter) return;

    if (emitter->event_file) {
        fclose(emitter->event_file);
    }

    pthread_mutex_destroy(&emitter->lock);
    free(emitter);
}

/* Serialize event to JSON */
static char* event_to_json(const event_t* event, size_t* len) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    if (!doc) return NULL;

    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    /* Standard fields */
    char ts_buf[32];
    time_format_iso8601(event->timestamp, ts_buf, sizeof(ts_buf));
    yyjson_mut_obj_add_str(doc, root, "ts", ts_buf);
    yyjson_mut_obj_add_str(doc, root, "component_id", "memory-service");
    yyjson_mut_obj_add_str(doc, root, "level", "info");
    yyjson_mut_obj_add_str(doc, root, "event", event_type_name(event->type));
    yyjson_mut_obj_add_str(doc, root, "trace_id", event->trace_id);

    /* Data object */
    yyjson_mut_val* data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "data", data);

    yyjson_mut_obj_add_str(doc, data, "session_id", event->session_id);
    yyjson_mut_obj_add_str(doc, data, "agent_id", event->agent_id);
    yyjson_mut_obj_add_uint(doc, data, "node_id", event->node_id);

    /* Event-specific data */
    switch (event->type) {
    case EVENT_MEMORY_STORED:
        yyjson_mut_obj_add_uint(doc, data, "content_len", event->data.stored.content_len);
        yyjson_mut_obj_add_uint(doc, data, "keyword_count", event->data.stored.keyword_count);
        break;

    case EVENT_MEMORY_DELETED:
        yyjson_mut_obj_add_uint(doc, data, "nodes_deleted", event->data.deleted.nodes_deleted);
        break;

    case EVENT_SESSION_CREATED:
        /* Node ID is the root node */
        break;

    case EVENT_SESSION_UPDATED:
        if (event->data.session.title[0]) {
            yyjson_mut_obj_add_str(doc, data, "title", event->data.session.title);
        }
        break;

    case EVENT_QUERY_PERFORMED:
        yyjson_mut_obj_add_str(doc, data, "query", event->data.query.query);
        yyjson_mut_obj_add_uint(doc, data, "result_count", event->data.query.result_count);
        yyjson_mut_obj_add_uint(doc, data, "latency_us", event->data.query.latency_us);
        break;
    }

    char* json = yyjson_mut_write(doc, 0, len);
    yyjson_mut_doc_free(doc);

    return json;
}

mem_error_t event_emit(event_emitter_t* emitter, const event_t* event) {
    if (!emitter || !event) return MEM_ERR_INVALID_ARG;

    pthread_mutex_lock(&emitter->lock);

    /* Serialize event */
    size_t json_len;
    char* json = event_to_json(event, &json_len);
    if (!json) {
        pthread_mutex_unlock(&emitter->lock);
        return MEM_ERR_NOMEM;
    }

    /* Write to file if enabled */
    if (emitter->file_output_enabled && emitter->event_file) {
        fprintf(emitter->event_file, "%s\n", json);
        fflush(emitter->event_file);
    }

    /* Notify subscribers */
    for (size_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (emitter->subscribers[i].active && emitter->subscribers[i].callback) {
            emitter->subscribers[i].callback(event, emitter->subscribers[i].user_data);
        }
    }

    emitter->event_count++;

    LOG_DEBUG("Event emitted: %s (session=%s, trace=%s)",
              event_type_name(event->type), event->session_id, event->trace_id);

    free(json);
    pthread_mutex_unlock(&emitter->lock);
    return MEM_OK;
}

/* Helper to create and emit event */
static event_t create_base_event(event_emitter_t* emitter,
                                 event_type_t type,
                                 const char* session_id,
                                 const char* agent_id) {
    event_t event = {0};
    event.type = type;
    event.timestamp = time_now_ns();

    if (session_id) {
        snprintf(event.session_id, MAX_SESSION_ID_LEN, "%s", session_id);
    }
    if (agent_id) {
        snprintf(event.agent_id, MAX_AGENT_ID_LEN, "%s", agent_id);
    }
    snprintf(event.trace_id, sizeof(event.trace_id), "%s", emitter->current_trace_id);

    return event;
}

mem_error_t event_emit_stored(event_emitter_t* emitter,
                              const char* session_id,
                              const char* agent_id,
                              node_id_t node_id,
                              size_t content_len,
                              size_t keyword_count) {
    if (!emitter) return MEM_ERR_INVALID_ARG;

    event_t event = create_base_event(emitter, EVENT_MEMORY_STORED, session_id, agent_id);
    event.node_id = node_id;
    event.data.stored.content_len = content_len;
    event.data.stored.keyword_count = keyword_count;

    return event_emit(emitter, &event);
}

mem_error_t event_emit_deleted(event_emitter_t* emitter,
                               const char* session_id,
                               const char* agent_id,
                               size_t nodes_deleted) {
    if (!emitter) return MEM_ERR_INVALID_ARG;

    event_t event = create_base_event(emitter, EVENT_MEMORY_DELETED, session_id, agent_id);
    event.data.deleted.nodes_deleted = nodes_deleted;

    return event_emit(emitter, &event);
}

mem_error_t event_emit_session_created(event_emitter_t* emitter,
                                       const char* session_id,
                                       const char* agent_id,
                                       node_id_t root_node_id) {
    if (!emitter) return MEM_ERR_INVALID_ARG;

    event_t event = create_base_event(emitter, EVENT_SESSION_CREATED, session_id, agent_id);
    event.node_id = root_node_id;

    return event_emit(emitter, &event);
}

mem_error_t event_emit_session_updated(event_emitter_t* emitter,
                                       const char* session_id,
                                       const char* agent_id,
                                       const char* title) {
    if (!emitter) return MEM_ERR_INVALID_ARG;

    event_t event = create_base_event(emitter, EVENT_SESSION_UPDATED, session_id, agent_id);
    if (title) {
        snprintf(event.data.session.title, sizeof(event.data.session.title), "%s", title);
    }

    return event_emit(emitter, &event);
}

mem_error_t event_emit_query(event_emitter_t* emitter,
                            const char* session_id,
                            const char* agent_id,
                            const char* query_text,
                            size_t result_count,
                            uint64_t latency_us) {
    if (!emitter) return MEM_ERR_INVALID_ARG;

    event_t event = create_base_event(emitter, EVENT_QUERY_PERFORMED, session_id, agent_id);
    if (query_text) {
        snprintf(event.data.query.query, sizeof(event.data.query.query), "%s", query_text);
    }
    event.data.query.result_count = result_count;
    event.data.query.latency_us = latency_us;

    return event_emit(emitter, &event);
}

uint32_t event_subscribe(event_emitter_t* emitter,
                        event_callback_t callback,
                        void* user_data) {
    if (!emitter || !callback) return 0;

    pthread_mutex_lock(&emitter->lock);

    /* Find free slot */
    for (size_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!emitter->subscribers[i].active) {
            emitter->subscribers[i].id = emitter->next_subscriber_id++;
            emitter->subscribers[i].callback = callback;
            emitter->subscribers[i].user_data = user_data;
            emitter->subscribers[i].active = true;
            emitter->subscriber_count++;

            uint32_t id = emitter->subscribers[i].id;
            pthread_mutex_unlock(&emitter->lock);
            return id;
        }
    }

    pthread_mutex_unlock(&emitter->lock);
    return 0;
}

void event_unsubscribe(event_emitter_t* emitter, uint32_t subscription_id) {
    if (!emitter || subscription_id == 0) return;

    pthread_mutex_lock(&emitter->lock);

    for (size_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (emitter->subscribers[i].active &&
            emitter->subscribers[i].id == subscription_id) {
            emitter->subscribers[i].active = false;
            emitter->subscribers[i].callback = NULL;
            emitter->subscribers[i].user_data = NULL;
            emitter->subscriber_count--;
            break;
        }
    }

    pthread_mutex_unlock(&emitter->lock);
}

void event_set_trace_id(event_emitter_t* emitter, const char* trace_id) {
    if (!emitter || !trace_id) return;

    pthread_mutex_lock(&emitter->lock);
    snprintf(emitter->current_trace_id, sizeof(emitter->current_trace_id), "%s", trace_id);
    pthread_mutex_unlock(&emitter->lock);
}

uint64_t event_get_count(const event_emitter_t* emitter) {
    if (!emitter) return 0;
    return emitter->event_count;
}
