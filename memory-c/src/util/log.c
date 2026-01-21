/*
 * Memory Service - Logging Implementation
 */

#include "log.h"
#include <time.h>
#include <string.h>
#include <pthread.h>

/* Global log configuration */
static log_config_t g_log_config = {
    .level = LOG_INFO,
    .format = LOG_FORMAT_TEXT,
    .output = NULL,  /* Will default to stderr */
    .include_timestamp = true,
    .include_location = false,
    .colorize = true
};

/* Mutex for thread-safe logging */
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ANSI color codes */
static const char* level_colors[] = {
    [LOG_TRACE] = "\033[90m",       /* Gray */
    [LOG_DEBUG] = "\033[36m",       /* Cyan */
    [LOG_INFO]  = "\033[32m",       /* Green */
    [LOG_WARN]  = "\033[33m",       /* Yellow */
    [LOG_ERROR] = "\033[31m",       /* Red */
    [LOG_FATAL] = "\033[35;1m",     /* Magenta bold */
};
static const char* color_reset = "\033[0m";

/* Level names */
static const char* level_names[] = {
    [LOG_TRACE] = "TRACE",
    [LOG_DEBUG] = "DEBUG",
    [LOG_INFO]  = "INFO",
    [LOG_WARN]  = "WARN",
    [LOG_ERROR] = "ERROR",
    [LOG_FATAL] = "FATAL",
};

void log_init(const log_config_t* config) {
    pthread_mutex_lock(&g_log_mutex);
    if (config) {
        g_log_config = *config;
    }
    if (!g_log_config.output) {
        g_log_config.output = stderr;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void log_set_level(log_level_t level) {
    pthread_mutex_lock(&g_log_mutex);
    g_log_config.level = level;
    pthread_mutex_unlock(&g_log_mutex);
}

log_level_t log_get_level(void) {
    pthread_mutex_lock(&g_log_mutex);
    log_level_t level = g_log_config.level;
    pthread_mutex_unlock(&g_log_mutex);
    return level;
}

bool log_level_enabled(log_level_t level) {
    pthread_mutex_lock(&g_log_mutex);
    bool enabled = level >= g_log_config.level;
    pthread_mutex_unlock(&g_log_mutex);
    return enabled;
}

const char* log_level_name(log_level_t level) {
    if (level < LOG_OFF) {
        return level_names[level];
    }
    return "UNKNOWN";
}

log_level_t log_level_parse(const char* name) {
    if (!name) return LOG_INFO;

    if (strcasecmp(name, "trace") == 0) return LOG_TRACE;
    if (strcasecmp(name, "debug") == 0) return LOG_DEBUG;
    if (strcasecmp(name, "info") == 0) return LOG_INFO;
    if (strcasecmp(name, "warn") == 0 || strcasecmp(name, "warning") == 0) return LOG_WARN;
    if (strcasecmp(name, "error") == 0) return LOG_ERROR;
    if (strcasecmp(name, "fatal") == 0) return LOG_FATAL;
    if (strcasecmp(name, "off") == 0) return LOG_OFF;

    return LOG_INFO;
}

/* Write JSON-escaped string to file */
static void write_json_escaped(FILE* out, const char* str) {
    while (*str) {
        switch (*str) {
            case '"':  fprintf(out, "\\\""); break;
            case '\\': fprintf(out, "\\\\"); break;
            case '\b': fprintf(out, "\\b"); break;
            case '\f': fprintf(out, "\\f"); break;
            case '\n': fprintf(out, "\\n"); break;
            case '\r': fprintf(out, "\\r"); break;
            case '\t': fprintf(out, "\\t"); break;
            default:
                if ((unsigned char)*str < 0x20) {
                    fprintf(out, "\\u%04x", (unsigned char)*str);
                } else {
                    fputc(*str, out);
                }
        }
        str++;
    }
}

void log_write(log_level_t level, const char* file, int line,
               const char* func, const char* fmt, ...) {
    if (level < g_log_config.level) return;

    FILE* out = g_log_config.output ? g_log_config.output : stderr;

    pthread_mutex_lock(&g_log_mutex);

    /* Get timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    /* Format message into buffer */
    char msg_buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    if (g_log_config.format == LOG_FORMAT_JSON) {
        /* JSON Lines output */
        fprintf(out, "{\"ts\":\"%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ\",\"level\":\"%s\",\"msg\":\"",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                ts.tv_nsec / 1000000,
                level_names[level]);
        write_json_escaped(out, msg_buf);
        fprintf(out, "\"");

        if (g_log_config.include_location) {
            const char* filename = strrchr(file, '/');
            filename = filename ? filename + 1 : file;
            fprintf(out, ",\"file\":\"%s\",\"line\":%d,\"func\":\"%s\"",
                    filename, line, func);
        }

        fprintf(out, "}\n");
    } else {
        /* Text format output */
        if (g_log_config.include_timestamp) {
            fprintf(out, "%04d-%02d-%02d %02d:%02d:%02d.%03ld ",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    ts.tv_nsec / 1000000);
        }

        /* Level with color */
        if (g_log_config.colorize && level < LOG_OFF) {
            fprintf(out, "%s%-5s%s ", level_colors[level], level_names[level], color_reset);
        } else if (level < LOG_OFF) {
            fprintf(out, "%-5s ", level_names[level]);
        }

        /* Location */
        if (g_log_config.include_location) {
            const char* filename = strrchr(file, '/');
            filename = filename ? filename + 1 : file;
            fprintf(out, "[%s:%d %s] ", filename, line, func);
        }

        /* Message */
        fprintf(out, "%s\n", msg_buf);
    }

    fflush(out);
    pthread_mutex_unlock(&g_log_mutex);
}
