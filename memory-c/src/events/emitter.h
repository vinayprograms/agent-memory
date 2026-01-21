/*
 * Memory Service - Event Emission
 *
 * Emits events for memory operations to the event bus.
 * Events are written as JSON Lines to topic directories.
 */

#ifndef MEMORY_SERVICE_EMITTER_H
#define MEMORY_SERVICE_EMITTER_H

#include "../../include/types.h"
#include "../../include/error.h"

#include <stddef.h>
#include <stdbool.h>

/* Event types */
typedef enum {
    EVENT_MEMORY_STORED,
    EVENT_MEMORY_DELETED,
    EVENT_SESSION_CREATED,
    EVENT_SESSION_UPDATED,
    EVENT_QUERY_PERFORMED,
} event_type_t;

/* Event data */
typedef struct {
    event_type_t    type;
    char            session_id[MAX_SESSION_ID_LEN];
    char            agent_id[MAX_AGENT_ID_LEN];
    node_id_t       node_id;
    timestamp_ns_t  timestamp;
    char            trace_id[64];

    /* Event-specific data */
    union {
        struct {
            size_t content_len;
            size_t keyword_count;
        } stored;
        struct {
            size_t nodes_deleted;
        } deleted;
        struct {
            char title[256];
        } session;
        struct {
            char query[256];
            size_t result_count;
            uint64_t latency_us;
        } query;
    } data;
} event_t;

/* Event emitter context */
typedef struct event_emitter event_emitter_t;

/* Event callback for subscribers */
typedef void (*event_callback_t)(const event_t* event, void* user_data);

/*
 * Create an event emitter
 *
 * @param emitter       Output emitter pointer
 * @param events_dir    Directory for event files (NULL to disable file output)
 * @return              MEM_OK on success
 */
mem_error_t event_emitter_create(event_emitter_t** emitter, const char* events_dir);

/*
 * Destroy event emitter
 */
void event_emitter_destroy(event_emitter_t* emitter);

/*
 * Emit an event
 *
 * Writes event to file and notifies subscribers.
 *
 * @param emitter   Event emitter
 * @param event     Event to emit
 * @return          MEM_OK on success
 */
mem_error_t event_emit(event_emitter_t* emitter, const event_t* event);

/*
 * Emit memory stored event
 */
mem_error_t event_emit_stored(event_emitter_t* emitter,
                              const char* session_id,
                              const char* agent_id,
                              node_id_t node_id,
                              size_t content_len,
                              size_t keyword_count);

/*
 * Emit memory deleted event
 */
mem_error_t event_emit_deleted(event_emitter_t* emitter,
                               const char* session_id,
                               const char* agent_id,
                               size_t nodes_deleted);

/*
 * Emit session created event
 */
mem_error_t event_emit_session_created(event_emitter_t* emitter,
                                       const char* session_id,
                                       const char* agent_id,
                                       node_id_t root_node_id);

/*
 * Emit session updated event (e.g., title generated)
 */
mem_error_t event_emit_session_updated(event_emitter_t* emitter,
                                       const char* session_id,
                                       const char* agent_id,
                                       const char* title);

/*
 * Emit query performed event
 */
mem_error_t event_emit_query(event_emitter_t* emitter,
                            const char* session_id,
                            const char* agent_id,
                            const char* query_text,
                            size_t result_count,
                            uint64_t latency_us);

/*
 * Subscribe to events
 *
 * @param emitter       Event emitter
 * @param callback      Callback function
 * @param user_data     User data passed to callback
 * @return              Subscription ID (0 on error)
 */
uint32_t event_subscribe(event_emitter_t* emitter,
                        event_callback_t callback,
                        void* user_data);

/*
 * Unsubscribe from events
 *
 * @param emitter           Event emitter
 * @param subscription_id   ID from event_subscribe
 */
void event_unsubscribe(event_emitter_t* emitter, uint32_t subscription_id);

/*
 * Set trace ID for subsequent events
 *
 * @param emitter   Event emitter
 * @param trace_id  Trace ID string
 */
void event_set_trace_id(event_emitter_t* emitter, const char* trace_id);

/*
 * Generate a new trace ID
 *
 * @param buf       Output buffer
 * @param buf_len   Buffer length
 */
void event_generate_trace_id(char* buf, size_t buf_len);

/*
 * Get event count
 */
uint64_t event_get_count(const event_emitter_t* emitter);

/*
 * Get event type name
 */
const char* event_type_name(event_type_t type);

#endif /* MEMORY_SERVICE_EMITTER_H */
