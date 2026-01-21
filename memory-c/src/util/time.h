/*
 * Memory Service - Time Utilities
 *
 * High-precision timing and latency tracking.
 */

#ifndef MEMORY_SERVICE_TIME_H
#define MEMORY_SERVICE_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Maximum checkpoints in latency tracker */
#define MAX_LATENCY_CHECKPOINTS 16

/* Latency tracker for profiling request paths */
typedef struct {
    uint64_t    start_ns;
    uint64_t    checkpoints[MAX_LATENCY_CHECKPOINTS];
    const char* checkpoint_names[MAX_LATENCY_CHECKPOINTS];
    int         checkpoint_count;
    bool        enabled;
} latency_tracker_t;

/* Get current time in nanoseconds */
static inline uint64_t time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Get current time in microseconds */
static inline uint64_t time_now_us(void) {
    return time_now_ns() / 1000;
}

/* Get current time in milliseconds */
static inline uint64_t time_now_ms(void) {
    return time_now_ns() / 1000000;
}

/* Get wall-clock time in nanoseconds (for timestamps) */
static inline uint64_t time_wallclock_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Format timestamp as ISO 8601 */
void time_format_iso8601(uint64_t ns, char* buf, size_t buflen);

/* Parse ISO 8601 timestamp */
uint64_t time_parse_iso8601(const char* str);

/* Initialize latency tracker */
static inline void latency_init(latency_tracker_t* lt, bool enabled) {
    lt->start_ns = 0;
    lt->checkpoint_count = 0;
    lt->enabled = enabled;
}

/* Start timing */
static inline void latency_start(latency_tracker_t* lt) {
    if (lt->enabled) {
        lt->start_ns = time_now_ns();
        lt->checkpoint_count = 0;
    }
}

/* Record checkpoint */
static inline void latency_checkpoint(latency_tracker_t* lt, const char* name) {
    if (lt->enabled && lt->checkpoint_count < MAX_LATENCY_CHECKPOINTS) {
        lt->checkpoints[lt->checkpoint_count] = time_now_ns();
        lt->checkpoint_names[lt->checkpoint_count] = name;
        lt->checkpoint_count++;
    }
}

/* Get total elapsed time in microseconds */
static inline uint64_t latency_total_us(const latency_tracker_t* lt) {
    if (!lt->enabled || lt->start_ns == 0) return 0;
    return (time_now_ns() - lt->start_ns) / 1000;
}

/* Get elapsed time to checkpoint in microseconds */
static inline uint64_t latency_checkpoint_us(const latency_tracker_t* lt, int idx) {
    if (!lt->enabled || idx < 0 || idx >= lt->checkpoint_count) return 0;
    return (lt->checkpoints[idx] - lt->start_ns) / 1000;
}

/* Get time between checkpoints in microseconds */
static inline uint64_t latency_delta_us(const latency_tracker_t* lt, int from, int to) {
    if (!lt->enabled || from < 0 || to >= lt->checkpoint_count || from > to) return 0;
    uint64_t start = (from == 0) ? lt->start_ns : lt->checkpoints[from - 1];
    return (lt->checkpoints[to] - start) / 1000;
}

/* Log latency breakdown */
void latency_log(const latency_tracker_t* lt);

/* Sleep utilities */
static inline void sleep_ns(uint64_t ns) {
    struct timespec ts = {
        .tv_sec = (time_t)(ns / 1000000000ULL),
        .tv_nsec = (long)(ns % 1000000000ULL)
    };
    nanosleep(&ts, NULL);
}

static inline void sleep_us(uint64_t us) {
    sleep_ns(us * 1000);
}

static inline void sleep_ms(uint64_t ms) {
    sleep_ns(ms * 1000000);
}

#endif /* MEMORY_SERVICE_TIME_H */
