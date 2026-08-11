/**
 * @file event_store.c
 * @brief Encrypted event storage on LittleFS flash
 *
 * Manages daily event files, a flat index, and encrypted writes/reads.
 * All file I/O uses POSIX APIs (NuttX VFS compatible).
 */

#include "event_store.h"
#include "event_crypto.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/** Build the filename for today's (or a given) date. */
static void make_filename(char *buf, size_t buflen, const struct tm *tm)
{
    snprintf(buf, buflen, "%s/%s%04d%02d%02d%s",
             EVT_STORE_BASE_DIR,
             EVT_STORE_FILE_PREFIX,
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             EVT_STORE_FILE_SUFFIX);
}

/** Return the byte size of one daily file. */
static off_t file_record_count(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_size / EVT_STORE_RECORD_LEN;
}

/** Ensure the base directory exists. */
static int ensure_dir(void)
{
    struct stat st;
    if (stat(EVT_STORE_BASE_DIR, &st) == 0) return 0;
    if (mkdir(EVT_STORE_BASE_DIR, 0700) != 0 && errno != EEXIST) {
        syslog(LOG_ERR, "event_store: mkdir %s: %d\n",
               EVT_STORE_BASE_DIR, errno);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Index management                                                   */
/* ------------------------------------------------------------------ */

/*
 * The index is a simple flat binary file of event_index_entry structs.
 * It is rebuilt from the actual files on startup (rebuild_index) and
 * appended to on each write.  On a clean shutdown it is compacted.
 *
 * Index header:  { magic(4), version(4), count(4), next_seq(4) }
 * Followed by:   count * event_index_entry
 */

#define INDEX_MAGIC    0x564C5345  /* "VLSE" */
#define INDEX_VERSION  1

struct index_header {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t next_seq;
};

static uint32_t g_next_seq    = 1;
static uint32_t g_event_count = 0;
static bool     g_initialised = false;

/**
 * Write the index header + entry to the end of the index file.
 */
static evt_store_err_t index_append(uint32_t seq, const char *file,
                                    uint32_t offset)
{
    int fd = open(EVT_STORE_INDEX_FILE, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return EVT_STORE_ERR_IO;

    struct event_index_entry entry;
    memset(&entry, 0, sizeof(entry));
    entry.seq    = seq;
    strncpy(entry.file, file, sizeof(entry.file) - 1);
    entry.offset = offset;

    ssize_t n = write(fd, &entry, sizeof(entry));
    close(fd);
    return (n == sizeof(entry)) ? EVT_STORE_OK : EVT_STORE_ERR_IO;
}

/**
 * Rewrite the full index from the stored count and next_seq.
 */
static evt_store_err_t index_save(void)
{
    int fd = open(EVT_STORE_INDEX_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return EVT_STORE_ERR_IO;

    struct index_header hdr;
    hdr.magic    = INDEX_MAGIC;
    hdr.version  = INDEX_VERSION;
    hdr.count    = g_event_count;
    hdr.next_seq = g_next_seq;

    ssize_t n = write(fd, &hdr, sizeof(hdr));
    close(fd);
    return (n == sizeof(hdr)) ? EVT_STORE_OK : EVT_STORE_ERR_IO;
}

/**
 * Load the index header.  If the file is corrupt or missing,
 * rebuild from the actual event files.
 */
static evt_store_err_t index_load(void)
{
    int fd = open(EVT_STORE_INDEX_FILE, O_RDONLY);
    if (fd < 0) {
        /* No index file — rebuild */
        return evt_store_rebuild_index();
    }

    struct index_header hdr;
    ssize_t n = read(fd, &hdr, sizeof(hdr));
    close(fd);

    if (n != sizeof(hdr) || hdr.magic != INDEX_MAGIC ||
        hdr.version != INDEX_VERSION) {
        syslog(LOG_WARNING, "event_store: index corrupt, rebuilding\n");
        return evt_store_rebuild_index();
    }

    g_event_count = hdr.count;
    g_next_seq    = hdr.next_seq;
    return EVT_STORE_OK;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

evt_store_err_t evt_store_init(void)
{
    if (g_initialised) return EVT_STORE_OK;

    if (!evt_crypto_is_ready()) {
        syslog(LOG_ERR, "event_store: crypto not ready\n");
        return EVT_STORE_ERR_INIT;
    }

    if (ensure_dir() != 0) {
        return EVT_STORE_ERR_IO;
    }

    evt_store_err_t rc = index_load();
    if (rc != EVT_STORE_OK) return rc;

    g_initialised = true;
    syslog(LOG_INFO, "event_store: init ok, %u events, next_seq=%u\n",
           g_event_count, g_next_seq);
    return EVT_STORE_OK;
}

/* ------------------------------------------------------------------ */
/* Write                                                              */
/* ------------------------------------------------------------------ */

evt_store_err_t evt_store_write(const struct event_plain *event)
{
    if (!g_initialised) return EVT_STORE_ERR_INIT;
    if (!event)         return EVT_STORE_ERR_PARAM;

    /* Determine today's filename */
    time_t now = (time_t)event->timestamp;
    struct tm tm;
    gmtime_r(&now, &tm);
    char filename[64];
    make_filename(filename, sizeof(filename), &tm);

    /* Check if today's file is full */
    off_t current_count = file_record_count(filename);
    if (current_count >= EVT_STORE_MAX_PER_FILE) {
        /* Need to rotate — find next available name (minute granularity) */
        /* Simple approach: append a suffix */
        char rotated[80];
        int suffix = 1;
        do {
            snprintf(rotated, sizeof(rotated), "%s_%d", filename, suffix++);
        } while (file_record_count(rotated) >= EVT_STORE_MAX_PER_FILE &&
                 suffix < 1000);
        strncpy(filename, rotated, sizeof(filename));
        current_count = file_record_count(filename);
    }

    /* Enforce total file limit */
    {
        DIR *d = opendir(EVT_STORE_BASE_DIR);
        if (d) {
            int file_count = 0;
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (strncmp(ent->d_name, EVT_STORE_FILE_PREFIX,
                            strlen(EVT_STORE_FILE_PREFIX)) == 0) {
                    file_count++;
                }
            }
            closedir(d);

            if (file_count >= EVT_STORE_MAX_FILES) {
                /* Delete the oldest file */
                /* Simple heuristic: first file lexically (YYYYMMDD order) */
                char oldest[128] = {0};
                d = opendir(EVT_STORE_BASE_DIR);
                if (d) {
                    while ((ent = readdir(d)) != NULL) {
                        if (strncmp(ent->d_name, EVT_STORE_FILE_PREFIX,
                                    strlen(EVT_STORE_FILE_PREFIX)) == 0) {
                            if (oldest[0] == '\0' ||
                                strcmp(ent->d_name, oldest) < 0) {
                                strncpy(oldest, ent->d_name, sizeof(oldest) - 1);
                            }
                        }
                    }
                    closedir(d);
                    if (oldest[0] != '\0') {
                        char path[256];
                        snprintf(path, sizeof(path), "%s/%s",
                                 EVT_STORE_BASE_DIR, oldest);
                        unlink(path);
                        syslog(LOG_INFO, "event_store: rotated out %s\n", oldest);
                        if (g_event_count > EVT_STORE_MAX_PER_FILE) {
                            g_event_count -= EVT_STORE_MAX_PER_FILE;
                        } else {
                            g_event_count = 0;
                        }
                    }
                }
            }
        }
    }

    /* Build the 48-byte plaintext */
    uint8_t plain[EVT_CRYPTO_PLAIN_LEN];
    memset(plain, 0, sizeof(plain));
    memcpy(plain, event, sizeof(struct event_plain));

    /* Encrypt: produces iv(12) + cipher(48) + tag(16) = 76 bytes */
    uint8_t crypto_out[EVT_CRYPTO_IV_LEN + EVT_CRYPTO_CIPHER_LEN + EVT_CRYPTO_TAG_LEN];
    size_t  crypto_len = 0;
    evt_crypto_err_t crc = evt_crypto_encrypt(plain, crypto_out, &crypto_len);
    if (crc != EVT_CRYPTO_OK) {
        syslog(LOG_ERR, "event_store: encrypt failed: %d\n", crc);
        return EVT_STORE_ERR_CRYPTO;
    }

    /* Assemble the 84-byte on-disk record */
    struct event_record rec;
    memset(&rec, 0, sizeof(rec));
    rec.seq = g_next_seq;
    memcpy(rec.iv,            crypto_out,                        12);
    memcpy(rec.encrypted_data, crypto_out + EVT_CRYPTO_IV_LEN,  48);
    memcpy(rec.tag,            crypto_out + EVT_CRYPTO_IV_LEN
                                                + EVT_CRYPTO_CIPHER_LEN, 16);

    /* CRC over iv + encrypted_data + tag (bytes 4..83 of the record) */
    rec.crc32 = evt_crypto_crc32(
        (const uint8_t *)&rec.iv, 12 + 48 + 16);

    /* Append to the daily file */
    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        syslog(LOG_ERR, "event_store: open %s: %d\n", filename, errno);
        return EVT_STORE_ERR_IO;
    }
    ssize_t n = write(fd, &rec, sizeof(rec));
    close(fd);
    if (n != sizeof(rec)) {
        syslog(LOG_ERR, "event_store: short write\n");
        return EVT_STORE_ERR_IO;
    }

    /* Update index */
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    uint32_t offset = (uint32_t)(current_count * EVT_STORE_RECORD_LEN);
    index_append(rec.seq, base, offset);

    g_next_seq++;
    g_event_count++;
    index_save();

    return EVT_STORE_OK;
}

/* ------------------------------------------------------------------ */
/* Read                                                               */
/* ------------------------------------------------------------------ */

evt_store_err_t evt_store_read(uint32_t seq, struct event_plain *event)
{
    if (!g_initialised) return EVT_STORE_ERR_INIT;
    if (!event)         return EVT_STORE_ERR_PARAM;
    if (seq == 0 || seq >= g_next_seq) return EVT_STORE_ERR_NOTFOUND;

    /*
     * Scan all event files for the requested sequence number.
     * For a production system the index would map seq->file+offset
     * for O(1) lookup; here we do a linear scan which is acceptable
     * for ~500 records.
     */
    DIR *d = opendir(EVT_STORE_BASE_DIR);
    if (!d) return EVT_STORE_ERR_IO;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, EVT_STORE_FILE_PREFIX,
                    strlen(EVT_STORE_FILE_PREFIX)) != 0) {
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", EVT_STORE_BASE_DIR, ent->d_name);

        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        struct event_record rec;
        while (read(fd, &rec, sizeof(rec)) == sizeof(rec)) {
            if (rec.seq != seq) continue;

            /* Verify CRC */
            uint32_t crc = evt_crypto_crc32(
                (const uint8_t *)&rec.iv, 12 + 48 + 16);
            if (crc != rec.crc32) {
                syslog(LOG_WARNING, "event_store: CRC mismatch seq=%u\n", seq);
                close(fd);
                closedir(d);
                return EVT_STORE_ERR_IO;
            }

            /* Decrypt */
            uint8_t crypto_in[EVT_CRYPTO_IV_LEN + EVT_CRYPTO_CIPHER_LEN + EVT_CRYPTO_TAG_LEN];
            memcpy(crypto_in,                        rec.iv,            12);
            memcpy(crypto_in + 12,                   rec.encrypted_data, 48);
            memcpy(crypto_in + 12 + 48,              rec.tag,            16);

            uint8_t plain[EVT_CRYPTO_PLAIN_LEN];
            evt_crypto_err_t rc = evt_crypto_decrypt(crypto_in, sizeof(crypto_in), plain);
            if (rc != EVT_CRYPTO_OK) {
                close(fd);
                closedir(d);
                return EVT_STORE_ERR_CRYPTO;
            }

            memcpy(event, plain, sizeof(struct event_plain));
            close(fd);
            closedir(d);
            return EVT_STORE_OK;
        }
        close(fd);
    }
    closedir(d);
    return EVT_STORE_ERR_NOTFOUND;
}

evt_store_err_t evt_store_read_range(uint32_t start_seq,
                                     struct event_plain *events,
                                     uint32_t *count)
{
    if (!g_initialised || !events || !count) return EVT_STORE_ERR_PARAM;

    uint32_t max   = *count;
    uint32_t readn = 0;

    for (uint32_t seq = start_seq;
         seq < g_next_seq && readn < max;
         seq++) {
        evt_store_err_t rc = evt_store_read(seq, &events[readn]);
        if (rc == EVT_STORE_OK) {
            readn++;
        } else if (rc == EVT_STORE_ERR_NOTFOUND) {
            /* Gap in sequence — skip */
            continue;
        } else {
            break;
        }
    }

    *count = readn;
    return EVT_STORE_OK;
}

/* ------------------------------------------------------------------ */
/* Queries                                                            */
/* ------------------------------------------------------------------ */

uint32_t evt_store_count(void)
{
    return g_event_count;
}

uint32_t evt_store_latest_seq(void)
{
    return (g_next_seq > 1) ? g_next_seq - 1 : 0;
}

uint32_t evt_store_oldest_seq(void)
{
    if (g_event_count == 0) return 0;

    /* Scan first event file for the lowest seq */
    DIR *d = opendir(EVT_STORE_BASE_DIR);
    if (!d) return 0;

    uint32_t oldest = UINT32_MAX;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, EVT_STORE_FILE_PREFIX,
                    strlen(EVT_STORE_FILE_PREFIX)) != 0) {
            continue;
        }
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", EVT_STORE_BASE_DIR, ent->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        struct event_record rec;
        if (read(fd, &rec, sizeof(rec)) == sizeof(rec)) {
            if (rec.seq < oldest) oldest = rec.seq;
        }
        close(fd);
    }
    closedir(d);

    return (oldest != UINT32_MAX) ? oldest : 0;
}

/* ------------------------------------------------------------------ */
/* Housekeeping                                                       */
/* ------------------------------------------------------------------ */

evt_store_err_t evt_store_purge(uint32_t older_than_seq)
{
    if (!g_initialised) return EVT_STORE_ERR_INIT;

    DIR *d = opendir(EVT_STORE_BASE_DIR);
    if (!d) return EVT_STORE_ERR_IO;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, EVT_STORE_FILE_PREFIX,
                    strlen(EVT_STORE_FILE_PREFIX)) != 0) {
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", EVT_STORE_BASE_DIR, ent->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        /* Check if every record in this file is older than the cutoff */
        bool all_old = true;
        struct event_record rec;
        while (read(fd, &rec, sizeof(rec)) == sizeof(rec)) {
            if (rec.seq >= older_than_seq) {
                all_old = false;
                break;
            }
        }
        close(fd);

        if (all_old) {
            syslog(LOG_INFO, "event_store: purging %s\n", ent->d_name);
            unlink(path);
        }
    }
    closedir(d);

    /* Rebuild index to reflect deleted files */
    return evt_store_rebuild_index();
}

evt_store_err_t evt_store_secure_erase(void)
{
    if (!g_initialised) return EVT_STORE_ERR_INIT;

    DIR *d = opendir(EVT_STORE_BASE_DIR);
    if (!d) return EVT_STORE_ERR_IO;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, EVT_STORE_FILE_PREFIX,
                    strlen(EVT_STORE_FILE_PREFIX)) != 0) {
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", EVT_STORE_BASE_DIR, ent->d_name);

        /* Overwrite with random data before unlinking */
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
            int fd = open(path, O_WRONLY);
            if (fd >= 0) {
                uint8_t buf[256];
                size_t remaining = (size_t)st.st_size;
                while (remaining > 0) {
                    size_t chunk = (remaining < sizeof(buf)) ?
                                    remaining : sizeof(buf);
                    evt_crypto_random(buf, chunk);
                    write(fd, buf, chunk);
                    remaining -= chunk;
                }
                fsync(fd);
                close(fd);
            }
        }
        unlink(path);
    }
    closedir(d);

    /* Wipe index */
    unlink(EVT_STORE_INDEX_FILE);
    g_event_count = 0;
    g_next_seq    = 1;

    syslog(LOG_INFO, "event_store: secure erase complete\n");
    return EVT_STORE_OK;
}

evt_store_err_t evt_store_rebuild_index(void)
{
    g_event_count = 0;
    g_next_seq    = 1;

    DIR *d = opendir(EVT_STORE_BASE_DIR);
    if (!d) {
        /* No directory yet — that is fine, just save empty index */
        index_save();
        return EVT_STORE_OK;
    }

    /* Truncate the existing index file */
    int idxfd = open(EVT_STORE_INDEX_FILE,
                     O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (idxfd < 0) {
        closedir(d);
        return EVT_STORE_ERR_IO;
    }

    /* Write a placeholder header; we will rewrite it at the end */
    struct index_header hdr;
    hdr.magic    = INDEX_MAGIC;
    hdr.version  = INDEX_VERSION;
    hdr.count    = 0;
    hdr.next_seq = 1;
    write(idxfd, &hdr, sizeof(hdr));

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, EVT_STORE_FILE_PREFIX,
                    strlen(EVT_STORE_FILE_PREFIX)) != 0) {
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", EVT_STORE_BASE_DIR, ent->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        uint32_t offset = 0;
        struct event_record rec;
        while (read(fd, &rec, sizeof(rec)) == sizeof(rec)) {
            struct event_index_entry entry;
            memset(&entry, 0, sizeof(entry));
            entry.seq    = rec.seq;
            strncpy(entry.file, ent->d_name, sizeof(entry.file) - 1);
            entry.offset = offset;

            write(idxfd, &entry, sizeof(entry));

            if (rec.seq >= g_next_seq) {
                g_next_seq = rec.seq + 1;
            }
            g_event_count++;
            offset += EVT_STORE_RECORD_LEN;
        }
        close(fd);
    }
    closedir(d);

    /* Rewrite header with final counts */
    hdr.count    = g_event_count;
    hdr.next_seq = g_next_seq;
    lseek(idxfd, 0, SEEK_SET);
    write(idxfd, &hdr, sizeof(hdr));
    close(idxfd);

    syslog(LOG_INFO, "event_store: rebuilt index, %u events\n", g_event_count);
    return EVT_STORE_OK;
}
