/**
 * @file event_store.h
 * @brief Encrypted event storage API for VelaSense
 *
 * Stores confirmed classification events in encrypted form on a
 * LittleFS partition mounted at /data/events.
 *
 * Storage layout:
 *   /data/events/evt_YYYYMMDD.bin   — daily event files (max 100 each)
 *   /data/events/index.bin          — sequence-number index
 *
 * Each on-disk record is 84 bytes:
 *   seq(4) | iv(12) | encrypted_data(48) | tag(16) | crc32(4)
 *
 * The encrypted_data (48 bytes) contains a struct event_plain:
 *   timestamp(4) | label(1) | confidence(1) | reason_flags(2) |
 *   hr(2) | rmssd(2) | activity(2) | reserved(34)
 *
 * Total capacity: ~500 events before oldest file is overwritten.
 * Rotation: when a daily file reaches 100 records a new file is
 * created; when total files exceed the limit the oldest is deleted.
 */

#ifndef VELASENSE_EVENT_STORE_H
#define VELASENSE_EVENT_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Limits and constants                                               */
/* ------------------------------------------------------------------ */

#define EVT_STORE_BASE_DIR      "/data/events"
#define EVT_STORE_INDEX_FILE    "/data/events/index.bin"
#define EVT_STORE_MAX_PER_FILE  100
#define EVT_STORE_MAX_FILES     6       /* ~600 records, keeps ~500 */
#define EVT_STORE_RECORD_LEN    84
#define EVT_STORE_FILE_PREFIX   "evt_"
#define EVT_STORE_FILE_SUFFIX   ".bin"

/* ------------------------------------------------------------------ */
/* On-disk plaintext event (48 bytes, encrypted before write)         */
/* ------------------------------------------------------------------ */

struct event_plain {
    uint32_t timestamp;       /* Unix epoch seconds                   */
    uint8_t  label;           /* User-confirmed label (enum)          */
    uint8_t  confidence;      /* 0-100                                */
    uint16_t reason_flags;    /* Bitmask of reason codes              */
    uint16_t hr;              /* Heart rate x10 (e.g. 725 = 72.5)    */
    uint16_t rmssd;           /* RMSSD x10                            */
    uint16_t activity;        /* Activity level x10                   */
    uint8_t  reserved[34];    /* Padding / future fields              */
};

/* ------------------------------------------------------------------ */
/* On-disk record (84 bytes total)                                    */
/* ------------------------------------------------------------------ */

struct event_record {
    uint32_t seq;                          /* Monotonic sequence #     */
    uint8_t  iv[12];                       /* AES-GCM IV              */
    uint8_t  encrypted_data[48];           /* Ciphertext              */
    uint8_t  tag[16];                      /* GCM auth tag            */
    uint32_t crc32;                        /* CRC-32 over iv+ciph+tag */
};

/* ------------------------------------------------------------------ */
/* Index entry                                                        */
/* ------------------------------------------------------------------ */

struct event_index_entry {
    uint32_t seq;          /* Event sequence number                   */
    char     file[32];     /* Filename (relative to base dir)         */
    uint32_t offset;       /* Byte offset within the file             */
};

/* ------------------------------------------------------------------ */
/* Return codes                                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    EVT_STORE_OK           =  0,
    EVT_STORE_ERR_PARAM    = -1,
    EVT_STORE_ERR_IO       = -2,
    EVT_STORE_ERR_CRYPTO   = -3,
    EVT_STORE_ERR_FULL     = -4,
    EVT_STORE_ERR_NOTFOUND = -5,
    EVT_STORE_ERR_INDEX    = -6,
    EVT_STORE_ERR_INIT     = -7,
} evt_store_err_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/**
 * Initialise the event store.
 *
 * Creates /data/events if missing, loads the index, and verifies
 * the crypto subsystem is ready.  Safe to call multiple times
 * (idempotent after first success).
 *
 * @return EVT_STORE_OK on success.
 */
evt_store_err_t evt_store_init(void);

/* ------------------------------------------------------------------ */
/* Write                                                              */
/* ------------------------------------------------------------------ */

/**
 * Store a confirmed event.
 *
 * Encrypts the event, appends it to the current day's file, and
 * updates the index.  If the current file has reached
 * EVT_STORE_MAX_PER_FILE a new file is started.  If total file
 * count exceeds EVT_STORE_MAX_FILES the oldest file is deleted.
 *
 * @param event   Plaintext event to store (48 bytes of fields).
 * @return EVT_STORE_OK on success.
 */
evt_store_err_t evt_store_write(const struct event_plain *event);

/* ------------------------------------------------------------------ */
/* Read                                                               */
/* ------------------------------------------------------------------ */

/**
 * Read a single event by sequence number.
 *
 * Looks up the index, reads the 84-byte record, verifies CRC,
 * and decrypts.
 *
 * @param seq       Sequence number to look up.
 * @param event     Output: decrypted plaintext event.
 * @return EVT_STORE_OK, EVT_STORE_ERR_NOTFOUND, or error.
 */
evt_store_err_t evt_store_read(uint32_t seq, struct event_plain *event);

/**
 * Read a range of events by sequence number.
 *
 * Reads up to *count events starting at start_seq.  On return
 * *count is set to the number actually read.
 *
 * @param start_seq   First sequence number.
 * @param events      Output array.
 * @param count       [in] max entries, [out] entries read.
 * @return EVT_STORE_OK on success.
 */
evt_store_err_t evt_store_read_range(uint32_t start_seq,
                                     struct event_plain *events,
                                     uint32_t *count);

/**
 * Return the total number of events currently stored.
 */
uint32_t evt_store_count(void);

/**
 * Return the highest sequence number in the store (0 if empty).
 */
uint32_t evt_store_latest_seq(void);

/**
 * Return the lowest (oldest) sequence number in the store (0 if empty).
 */
uint32_t evt_store_oldest_seq(void);

/* ------------------------------------------------------------------ */
/* Housekeeping                                                       */
/* ------------------------------------------------------------------ */

/**
 * Purge events older than the given sequence number.
 *
 * Deletes entire daily files when all their records are older
 * than the cutoff.
 *
 * @param older_than_seq   Delete events with seq < this value.
 * @return EVT_STORE_OK on success.
 */
evt_store_err_t evt_store_purge(uint32_t older_than_seq);

/**
 * Secure-erase all stored events.
 *
 * Overwrites each file with random bytes, then deletes.
 * Zeroes the index.
 *
 * @return EVT_STORE_OK on success.
 */
evt_store_err_t evt_store_secure_erase(void);

/**
 * Compact the store: rewrite the index from the actual files.
 *
 * Useful after an unclean shutdown or power loss during write.
 */
evt_store_err_t evt_store_rebuild_index(void);

#ifdef __cplusplus
}
#endif

#endif /* VELASENSE_EVENT_STORE_H */
