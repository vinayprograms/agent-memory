/*
 * Memory Service - Time Utilities Implementation
 */

#include "time.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void time_format_iso8601(uint64_t ns, char* buf, size_t buflen) {
    if (!buf || buflen < 25) return;

    time_t sec = (time_t)(ns / 1000000000ULL);
    long msec = (long)((ns % 1000000000ULL) / 1000000);

    struct tm tm;
    gmtime_r(&sec, &tm);

    snprintf(buf, buflen, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
}

uint64_t time_parse_iso8601(const char* str) {
    if (!str) return 0;

    struct tm tm = {0};
    int msec = 0;

    /* Parse: YYYY-MM-DDTHH:MM:SS.mmmZ or YYYY-MM-DDTHH:MM:SSZ */
    int matched = sscanf(str, "%d-%d-%dT%d:%d:%d.%dZ",
                         &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                         &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &msec);

    if (matched < 6) {
        /* Try without milliseconds */
        matched = sscanf(str, "%d-%d-%dT%d:%d:%dZ",
                         &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                         &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
        if (matched < 6) return 0;
    }

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;

    time_t sec = timegm(&tm);
    if (sec == (time_t)-1) return 0;

    return (uint64_t)sec * 1000000000ULL + (uint64_t)msec * 1000000;
}

void latency_log(const latency_tracker_t* lt) {
    if (!lt->enabled || lt->checkpoint_count == 0) return;

    uint64_t total = latency_total_us(lt);
    LOG_DEBUG("Latency breakdown (total: %lu us):", total);

    uint64_t prev = lt->start_ns;
    for (int i = 0; i < lt->checkpoint_count; i++) {
        uint64_t delta = (lt->checkpoints[i] - prev) / 1000;
        uint64_t cumulative = (lt->checkpoints[i] - lt->start_ns) / 1000;
        LOG_DEBUG("  [%d] %-20s: +%6lu us (cumulative: %lu us)",
                  i, lt->checkpoint_names[i], delta, cumulative);
        prev = lt->checkpoints[i];
    }
}
