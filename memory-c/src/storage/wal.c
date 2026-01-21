/*
 * Memory Service - Write-Ahead Log Implementation
 */

#include "wal.h"
#include "../util/log.h"
#include "../util/time.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>

/* Default write buffer size */
#define DEFAULT_WRITE_BUF_SIZE (64 * 1024)

/* Maximum allowed WAL data length to prevent DoS from corrupted/malicious files */
#define MAX_WAL_DATA_LEN (64 * 1024 * 1024)  /* 64 MB */

/* CRC32 lookup table - thread-safe initialization using pthread_once */
static uint32_t crc32_table[256];
static pthread_once_t crc32_init_once = PTHREAD_ONCE_INIT;

static void init_crc32_table_impl(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
}

static void init_crc32_table(void) {
    pthread_once(&crc32_init_once, init_crc32_table_impl);
}

static uint32_t compute_crc32(const void* data, size_t len) {
    init_crc32_table();

    const uint8_t* buf = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

mem_error_t wal_create(wal_t** wal, const char* path, size_t max_size) {
    MEM_CHECK_ERR(wal != NULL, MEM_ERR_INVALID_ARG, "wal pointer is NULL");
    MEM_CHECK_ERR(path != NULL, MEM_ERR_INVALID_ARG, "path is NULL");
    MEM_CHECK_ERR(max_size > 0, MEM_ERR_INVALID_ARG, "max_size must be > 0");

    wal_t* w = calloc(1, sizeof(wal_t));
    MEM_CHECK_ALLOC(w);

    /* Open or create file */
    int fd = open(path, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        free(w);
        MEM_RETURN_ERROR(MEM_ERR_OPEN, "failed to open WAL %s", path);
    }

    /* Get current size */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        free(w);
        MEM_RETURN_ERROR(MEM_ERR_IO, "failed to stat WAL");
    }

    /* Allocate write buffer */
    void* buf = malloc(DEFAULT_WRITE_BUF_SIZE);
    if (!buf) {
        close(fd);
        free(w);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate write buffer");
    }

    w->fd = fd;
    w->path = strdup(path);
    if (!w->path) {
        close(fd);
        free(buf);
        free(w);
        MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate WAL path");
    }
    w->size = (size_t)st.st_size;
    w->max_size = max_size;
    w->sequence = 1;  /* Will be updated on replay */
    w->checkpoint_seq = 0;
    w->sync_on_write = true;
    w->write_buf = buf;
    w->write_buf_size = DEFAULT_WRITE_BUF_SIZE;

    *wal = w;
    return MEM_OK;
}

mem_error_t wal_open(wal_t** wal, const char* path) {
    return wal_create(wal, path, SIZE_MAX);
}

mem_error_t wal_append(wal_t* wal, wal_op_type_t op,
                       const void* data, size_t len) {
    MEM_CHECK_ERR(wal != NULL, MEM_ERR_INVALID_ARG, "wal is NULL");

    /* Prepare header */
    wal_entry_header_t header = {
        .magic = WAL_MAGIC,
        .crc32 = data ? compute_crc32(data, len) : 0,
        .sequence = wal->sequence,
        .timestamp_ns = time_wallclock_ns(),
        .op_type = op,
        .data_len = (uint32_t)len
    };

    /* Write header */
    ssize_t written = write(wal->fd, &header, sizeof(header));
    if (written != sizeof(header)) {
        MEM_RETURN_ERROR(MEM_ERR_WRITE, "failed to write WAL header");
    }
    wal->size += sizeof(header);

    /* Write data if present */
    if (data && len > 0) {
        written = write(wal->fd, data, len);
        if (written != (ssize_t)len) {
            MEM_RETURN_ERROR(MEM_ERR_WRITE, "failed to write WAL data");
        }
        wal->size += len;
    }

    /* Sync if enabled */
    if (wal->sync_on_write) {
        if (fdatasync(wal->fd) < 0) {
            MEM_RETURN_ERROR(MEM_ERR_SYNC, "failed to sync WAL");
        }
    }

    wal->sequence++;
    return MEM_OK;
}

mem_error_t wal_sync(wal_t* wal) {
    MEM_CHECK_ERR(wal != NULL, MEM_ERR_INVALID_ARG, "wal is NULL");

    if (fsync(wal->fd) < 0) {
        MEM_RETURN_ERROR(MEM_ERR_SYNC, "failed to sync WAL");
    }

    return MEM_OK;
}

mem_error_t wal_checkpoint(wal_t* wal) {
    MEM_CHECK_ERR(wal != NULL, MEM_ERR_INVALID_ARG, "wal is NULL");

    /* Write checkpoint marker */
    MEM_CHECK(wal_append(wal, WAL_OP_CHECKPOINT, NULL, 0));

    /* Record checkpoint sequence */
    wal->checkpoint_seq = wal->sequence - 1;

    LOG_INFO("WAL checkpoint at sequence %lu", wal->checkpoint_seq);

    return MEM_OK;
}

mem_error_t wal_truncate(wal_t* wal) {
    MEM_CHECK_ERR(wal != NULL, MEM_ERR_INVALID_ARG, "wal is NULL");

    /* Truncate file */
    if (ftruncate(wal->fd, 0) < 0) {
        MEM_RETURN_ERROR(MEM_ERR_TRUNCATE, "failed to truncate WAL");
    }

    /* Seek to beginning */
    if (lseek(wal->fd, 0, SEEK_SET) < 0) {
        MEM_RETURN_ERROR(MEM_ERR_SEEK, "failed to seek WAL");
    }

    wal->size = 0;
    wal->checkpoint_seq = wal->sequence;

    LOG_INFO("WAL truncated");

    return MEM_OK;
}

mem_error_t wal_replay(wal_t* wal, wal_replay_fn callback, void* user_data) {
    return wal_replay_from(wal, 0, callback, user_data);
}

mem_error_t wal_replay_from(wal_t* wal, uint64_t from_seq,
                            wal_replay_fn callback, void* user_data) {
    MEM_CHECK_ERR(wal != NULL, MEM_ERR_INVALID_ARG, "wal is NULL");
    MEM_CHECK_ERR(callback != NULL, MEM_ERR_INVALID_ARG, "callback is NULL");

    /* Seek to beginning */
    if (lseek(wal->fd, 0, SEEK_SET) < 0) {
        MEM_RETURN_ERROR(MEM_ERR_SEEK, "failed to seek WAL");
    }

    uint64_t max_seq = 0;
    uint64_t entries_replayed = 0;
    uint64_t checkpoint_seq = 0;

    /* Read and replay entries */
    while (1) {
        wal_entry_header_t header;
        ssize_t n = read(wal->fd, &header, sizeof(header));

        if (n == 0) {
            /* EOF */
            break;
        }

        if (n != sizeof(header)) {
            if (n < 0) {
                MEM_RETURN_ERROR(MEM_ERR_READ, "failed to read WAL header");
            }
            /* Partial header - truncated entry, stop here */
            LOG_WARN("WAL truncated entry at offset %lu",
                    (unsigned long)(lseek(wal->fd, 0, SEEK_CUR) - n));
            break;
        }

        /* Validate magic */
        if (header.magic != WAL_MAGIC) {
            MEM_RETURN_ERROR(MEM_ERR_WAL_CORRUPT,
                           "invalid WAL magic at sequence %" PRIu64, header.sequence);
        }

        /* Read data */
        void* data = NULL;
        if (header.data_len > 0) {
            /* Bounds check to prevent DoS from corrupted/malicious WAL files */
            if (header.data_len > MAX_WAL_DATA_LEN) {
                LOG_ERROR("WAL data length %u exceeds maximum %u at sequence %" PRIu64,
                          header.data_len, MAX_WAL_DATA_LEN, header.sequence);
                MEM_RETURN_ERROR(MEM_ERR_WAL_CORRUPT,
                               "WAL data length exceeds maximum allowed");
            }

            /* Use write buffer if large enough, otherwise allocate */
            if (header.data_len <= wal->write_buf_size) {
                data = wal->write_buf;
            } else {
                data = malloc(header.data_len);
                if (!data) {
                    MEM_RETURN_ERROR(MEM_ERR_NOMEM, "failed to allocate WAL data buffer");
                }
            }

            n = read(wal->fd, data, header.data_len);
            if (n != (ssize_t)header.data_len) {
                if (data != wal->write_buf) free(data);
                if (n < 0) {
                    MEM_RETURN_ERROR(MEM_ERR_READ, "failed to read WAL data");
                }
                /* Partial data - truncated entry, stop here gracefully */
                LOG_WARN("WAL truncated data at sequence %lu", header.sequence);
                break;
            }

            /* Verify CRC */
            uint32_t crc = compute_crc32(data, header.data_len);
            if (crc != header.crc32) {
                if (data != wal->write_buf) free(data);
                /* CRC mismatch could be from truncated write - stop gracefully */
                LOG_WARN("WAL CRC mismatch at sequence %lu, treating as truncation",
                        header.sequence);
                break;
            }
        }

        /* Track sequence */
        if (header.sequence > max_seq) {
            max_seq = header.sequence;
        }

        /* Track last checkpoint */
        if (header.op_type == WAL_OP_CHECKPOINT) {
            checkpoint_seq = header.sequence;
        }

        /* Replay if after from_seq */
        if (header.sequence > from_seq && header.op_type != WAL_OP_CHECKPOINT) {
            mem_error_t err = callback(header.op_type, data, header.data_len, user_data);
            if (err != MEM_OK) {
                if (data && data != wal->write_buf) free(data);
                return err;
            }
            entries_replayed++;
        }

        if (data && data != wal->write_buf) {
            free(data);
        }
    }

    /* Update WAL state */
    wal->sequence = max_seq + 1;
    wal->checkpoint_seq = checkpoint_seq;

    LOG_INFO("WAL replay complete: %lu entries replayed, sequence at %lu",
             entries_replayed, wal->sequence);

    return MEM_OK;
}

uint64_t wal_sequence(const wal_t* wal) {
    return wal ? wal->sequence : 0;
}

size_t wal_size(const wal_t* wal) {
    return wal ? wal->size : 0;
}

bool wal_needs_checkpoint(const wal_t* wal) {
    return wal && wal->size >= wal->max_size;
}

void wal_close(wal_t* wal) {
    if (!wal) return;

    if (wal->fd >= 0) {
        fsync(wal->fd);
        close(wal->fd);
    }

    free(wal->path);
    free(wal->write_buf);
    free(wal);
}
