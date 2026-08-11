/**
 * @file event_crypto.h
 * @brief AES-128-GCM encryption helpers for VelaSense event data
 *
 * Provides authenticated encryption (AES-128-GCM) for event records
 * stored in Flash.  The master key is derived at boot from the
 * device-unique ID fuse and an optional user PIN via HKDF.
 *
 * Key hierarchy:
 *   device_uid (fuse) + user_pin --> HKDF --> master_key (128-bit)
 *   master_key + random_iv        --> AES-128-GCM --> ciphertext + tag
 *
 * The master key is kept only in a protected RAM region and is
 * zeroed on every power-down or secure-erase request.
 */

#ifndef VELASENSE_EVENT_CRYPTO_H
#define VELASENSE_EVENT_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define EVT_CRYPTO_KEY_LEN      16   /* AES-128 key size              */
#define EVT_CRYPTO_IV_LEN       12   /* GCM recommended IV size       */
#define EVT_CRYPTO_TAG_LEN      16   /* GCM authentication tag        */
#define EVT_CRYPTO_PLAIN_LEN    48   /* Plaintext event payload       */
#define EVT_CRYPTO_CIPHER_LEN   48   /* Same as plain for GCM         */

/* HKDF parameters */
#define EVT_CRYPTO_HKDF_SALT    "velasense-events"
#define EVT_CRYPTO_HKDF_SALTLEN 17   /* including NUL for clarity     */

/* Device UID source (platform-specific) */
#ifndef EVT_CRYPTO_UID_PATH
#define EVT_CRYPTO_UID_PATH     "/dev/uid0"
#endif

/* Max user PIN length (UTF-8 bytes) */
#define EVT_CRYPTO_PIN_MAX      64

/* ------------------------------------------------------------------ */
/* Return codes                                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    EVT_CRYPTO_OK          =  0,
    EVT_CRYPTO_ERR_PARAM   = -1,  /* NULL pointer or bad length       */
    EVT_CRYPTO_ERR_RNG     = -2,  /* Hardware RNG failure             */
    EVT_CRYPTO_ERR_AES     = -3,  /* AES-GCM operation failed         */
    EVT_CRYPTO_ERR_HKDF    = -4,  /* Key derivation failed            */
    EVT_CRYPTO_ERR_UID     = -5,  /* Cannot read device UID           */
    EVT_CRYPTO_ERR_STATE   = -6,  /* Module not initialised           */
} evt_crypto_err_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/**
 * Initialise the crypto subsystem.
 *
 * Reads the device UID from the hardware fuse, optionally mixes in
 * a user PIN, and derives the master key via HKDF-SHA256.
 *
 * @param user_pin      Optional PIN string (NULL or "" to skip).
 * @param pin_len       Length of user_pin in bytes (0 if NULL).
 * @return EVT_CRYPTO_OK on success.
 */
evt_crypto_err_t evt_crypto_init(const char *user_pin, size_t pin_len);

/**
 * Re-key the master key with a new or changed user PIN.
 *
 * Derives a fresh master key from the stored device UID and the
 * supplied PIN.  The previous key is securely wiped before the
 * new one is written.
 *
 * @param new_pin       New PIN string.
 * @param pin_len       Length of new_pin.
 * @return EVT_CRYPTO_OK on success.
 */
evt_crypto_err_t evt_crypto_rekey(const char *new_pin, size_t pin_len);

/**
 * Securely wipe the master key from RAM.
 *
 * Must be called before deep sleep or when the user requests
 * a secure erase of all events.
 */
void evt_crypto_deinit(void);

/**
 * Return true if the crypto subsystem is initialised and keyed.
 */
bool evt_crypto_is_ready(void);

/* ------------------------------------------------------------------ */
/* Encrypt / Decrypt                                                  */
/* ------------------------------------------------------------------ */

/**
 * Encrypt a 48-byte plaintext event record.
 *
 * Generates a random 12-byte IV (hardware RNG preferred),
 * encrypts with AES-128-GCM, and writes:
 *   iv(12) || ciphertext(48) || tag(16)   = 76 bytes
 *
 * @param plain         48-byte plaintext input.
 * @param out           Buffer of at least 76 bytes.
 * @param out_len       Written byte count (always 76 on success).
 * @return EVT_CRYPTO_OK on success.
 */
evt_crypto_err_t evt_crypto_encrypt(const uint8_t plain[EVT_CRYPTO_PLAIN_LEN],
                                    uint8_t *out, size_t *out_len);

/**
 * Decrypt a 76-byte (iv+ciphertext+tag) record back to 48 bytes.
 *
 * Verifies the GCM authentication tag; returns EVT_CRYPTO_ERR_AES
 * if tampering is detected.
 *
 * @param in            76-byte encrypted record (iv || cipher || tag).
 * @param in_len        Must be 76.
 * @param plain         48-byte output buffer.
 * @return EVT_CRYPTO_OK on success, EVT_CRYPTO_ERR_AES on tag mismatch.
 */
evt_crypto_err_t evt_crypto_decrypt(const uint8_t *in, size_t in_len,
                                    uint8_t plain[EVT_CRYPTO_PLAIN_LEN]);

/* ------------------------------------------------------------------ */
/* RNG helper                                                         */
/* ------------------------------------------------------------------ */

/**
 * Fill buffer with cryptographically random bytes.
 *
 * Tries /dev/hwrng first, falls back to /dev/urandom.
 *
 * @param buf   Output buffer.
 * @param len   Number of bytes.
 * @return EVT_CRYPTO_OK on success.
 */
evt_crypto_err_t evt_crypto_random(uint8_t *buf, size_t len);

/* ------------------------------------------------------------------ */
/* Utility                                                            */
/* ------------------------------------------------------------------ */

/**
 * CRC-32 (IEEE 802.3) over an arbitrary buffer.
 * Used for the on-disk record trailer.
 */
uint32_t evt_crypto_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* VELASENSE_EVENT_CRYPTO_H */
