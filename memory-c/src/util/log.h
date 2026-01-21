/*
 * Memory Service - Logging
 *
 * Simple, fast logging with level filtering.
 */

#ifndef MEMORY_SERVICE_LOG_H
#define MEMORY_SERVICE_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

/* Log levels */
typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_WARN = 3,
    LOG_ERROR = 4,
    LOG_FATAL = 5,
    LOG_OFF = 6
} log_level_t;

/* Log output format */
typedef enum {
    LOG_FORMAT_TEXT = 0,   /* Human-readable text (default) */
    LOG_FORMAT_JSON = 1    /* JSON Lines for analytics pipelines */
} log_format_t;

/* Log configuration */
typedef struct {
    log_level_t     level;
    log_format_t    format;
    FILE*           output;
    bool            include_timestamp;
    bool            include_location;
    bool            colorize;
} log_config_t;

/* Initialize logging */
void log_init(const log_config_t* config);

/* Set log level */
void log_set_level(log_level_t level);

/* Get current log level */
log_level_t log_get_level(void);

/* Check if level is enabled */
bool log_level_enabled(log_level_t level);

/* Core logging function */
void log_write(log_level_t level, const char* file, int line,
               const char* func, const char* fmt, ...);

/* Logging macros */
#define LOG_TRACE(...) \
    do { if (log_level_enabled(LOG_TRACE)) \
        log_write(LOG_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

#define LOG_DEBUG(...) \
    do { if (log_level_enabled(LOG_DEBUG)) \
        log_write(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

#define LOG_INFO(...) \
    do { if (log_level_enabled(LOG_INFO)) \
        log_write(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

#define LOG_WARN(...) \
    do { if (log_level_enabled(LOG_WARN)) \
        log_write(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

#define LOG_ERROR(...) \
    do { if (log_level_enabled(LOG_ERROR)) \
        log_write(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

#define LOG_FATAL(...) \
    do { if (log_level_enabled(LOG_FATAL)) \
        log_write(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

/* Get level name */
const char* log_level_name(log_level_t level);

/* Parse level from string */
log_level_t log_level_parse(const char* name);

#endif /* MEMORY_SERVICE_LOG_H */
