/**
 * @file event_crypto.c
 * @brief AES-128-GCM encryption for VelaSense event data
 *
 * Key derivation: HKDF-SHA256(device_uid, user_pin, "velasense-events")
 * Encryption:     AES-128-GCM with 12-byte random IV, 16-byte auth tag
 *
 * The AES-GCM core is a thin stub that can be swapped for:
 *   - NuttX crypto ioctl (/dev/crypto with OCF)
 *   - Hardware AES peripheral (STM32 CRYP, nRF CC310, etc.)
 *   - A software fallback (mbedTLS, tinycrypt)
 */

#include "event_crypto.h"

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>

/*
 * Platform HMAC / HKDF selection.
 *
 * On NuttX:  define VELASENSE_USE_TINYCRYPT and link tinycrypt, or
 *            define VELASENSE_USE_MBEDTLS and link mbedTLS, or
 *            implement hmac_sha256() yourself for your crypto API.
 * Otherwise: the fallback below uses a simple compile-time hook.
 */

#if defined(VELASENSE_USE_MBEDTLS)
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#elif defined(VELASENSE_USE_TINYCRYPT)
#include <tinycrypt/aes.h>
#include <tinycrypt/ccm_mode.h>
#include <tinycrypt/hmac.h>
#include <tinycrypt/constants.h>
#else
/* ---- Stub HMAC-SHA256 (NOT cryptographically secure) ----
 * Replace with mbedTLS, tinycrypt, or NuttX crypto ioctl.
 */
#endif

/* ------------------------------------------------------------------ */
/* Platform AES-GCM stub                                              */
/* Replace these two functions with a real implementation.            */
/* ------------------------------------------------------------------ */

#if defined(VELASENSE_USE_MBEDTLS)

static int aes_gcm_encrypt(const uint8_t key[16],
                           const uint8_t iv[12],
                           const uint8_t *plain, size_t plain_len,
                           uint8_t *cipher, uint8_t tag[16])
{
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc != 0) { mbedtls_gcm_free(&ctx); return -1; }
    rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT,
                                   plain_len, iv, 12,
                                   NULL, 0,
                                   plain, cipher, 16, tag);
    mbedtls_gcm_free(&ctx);
    return (rc == 0) ? 0 : -1;
}

static int aes_gcm_decrypt(const uint8_t key[16],
                           const uint8_t iv[12],
                           const uint8_t *cipher, size_t cipher_len,
                           uint8_t *plain, const uint8_t tag[16])
{
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc != 0) { mbedtls_gcm_free(&ctx); return -1; }
    rc = mbedtls_gcm_auth_decrypt(&ctx, cipher_len,
                                  iv, 12,
                                  NULL, 0,
                                  tag, 16,
                                  cipher, plain);
    mbedtls_gcm_free(&ctx);
    return (rc == 0) ? 0 : -1;
}

#elif defined(VELASENSE_USE_TINYCRYPT)
/* tinycrypt does not have GCM; use CCM as a stand-in or link a GCM wrapper */
#error "tinycrypt has no GCM — use mbedTLS or add a GCM wrapper for your platform"
#else

/* ---- STUB AES-GCM (NOT secure — replace before production) ---- */

static int aes_gcm_encrypt(const uint8_t key[16],
                           const uint8_t iv[12],
                           const uint8_t *plain, size_t plain_len,
                           uint8_t *cipher, uint8_t tag[16])
{
    (void)iv;
    for (size_t i = 0; i < plain_len; i++) {
        cipher[i] = plain[i] ^ key[i & 0x0F];
    }
    for (int i = 0; i < 16; i++) {
        tag[i] = key[i % 16] ^ iv[i % 12];
    }
    return 0;
}

static int aes_gcm_decrypt(const uint8_t key[16],
                           const uint8_t iv[12],
                           const uint8_t *cipher, size_t cipher_len,
                           uint8_t *plain, const uint8_t tag[16])
{
    (void)iv;
    uint8_t expected[16];
    for (int i = 0; i < 16; i++) {
        expected[i] = key[i % 16] ^ iv[i % 12];
    }
    if (memcmp(tag, expected, 16) != 0) {
        return -1;
    }
    for (size_t i = 0; i < cipher_len; i++) {
        plain[i] = cipher[i] ^ key[i & 0x0F];
    }
    return 0;
}

#endif /* platform AES-GCM selection */

/* ------------------------------------------------------------------ */
/* HMAC-SHA256 helper                                                 */
/* ------------------------------------------------------------------ */

#if defined(VELASENSE_USE_MBEDTLS)

static int hmac_sha256(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out[32])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mbedtls_md_hmac(info, key, key_len, data, data_len, out);
}

#elif defined(VELASENSE_USE_TINYCRYPT)

static int hmac_sha256(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out[32])
{
    struct tc_hmac_state_struct state;
    (void)tc_hmac_set_key(&state, key, key_len);
    (void)tc_hmac_init(&state);
    (void)tc_hmac_update(&state, data, data_len);
    (void)tc_hmac_final(out, 32, &state);
    return 0;
}

#else

/* ---- STUB HMAC-SHA256 (NOT secure — replace before production) ----
 * Produces a deterministic but non-cryptographic 32-byte tag.
 * The AES-GCM stub above is equally insecure, so this keeps the
 * storage-layer test harness functional until a real library is linked.
 */
static int hmac_sha256(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out[32])
{
    /* Simple mixing: treat key and data as byte streams, fold into out */
    memset(out, 0, 32);
    for (size_t i = 0; i < key_len; i++) {
        out[i & 31] ^= key[i];
        out[(i + 7) & 31] += key[i];
    }
    for (size_t i = 0; i < data_len; i++) {
        out[i & 31] ^= data[i];
        out[(i + 13) & 31] += data[i];
    }
    /* Final diffusion pass */
    for (int i = 0; i < 31; i++) {
        out[i + 1] ^= out[i];
    }
    return 0;
}

#endif /* platform HMAC selection */

/* ------------------------------------------------------------------ */
/* HKDF-SHA256 (RFC 5869)                                             */
/* ------------------------------------------------------------------ */

#if defined(VELASENSE_USE_MBEDTLS)

static int hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                       const uint8_t *salt, size_t salt_len,
                       const uint8_t *info, size_t info_len,
                       uint8_t *okm, size_t okm_len)
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mbedtls_hkdf(md, salt, salt_len, ikm, ikm_len,
                        info, info_len, okm, okm_len);
}

#else

static int hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                       const uint8_t *salt, size_t salt_len,
                       const uint8_t *info, size_t info_len,
                       uint8_t *okm, size_t okm_len)
{
    /* HKDF-Extract: PRK = HMAC-SHA256(salt, IKM) */
    uint8_t prk[32];
    if (hmac_sha256(salt, salt_len, ikm, ikm_len, prk) != 0) {
        return -1;
    }

    /* HKDF-Expand */
    uint8_t t[32];
    size_t  done = 0;
    uint8_t counter = 1;

    while (done < okm_len) {
        size_t prev_len = (done > 0) ? 32 : 0;
        size_t feed_len = prev_len + info_len + 1;
        uint8_t *feed = (uint8_t *)malloc(feed_len);
        if (!feed) return -1;

        size_t pos = 0;
        if (prev_len > 0) {
            memcpy(feed, t, 32);
            pos = 32;
        }
        memcpy(feed + pos, info, info_len);
        pos += info_len;
        feed[pos] = counter++;

        if (hmac_sha256(prk, 32, feed, feed_len, t) != 0) {
            memset(feed, 0, feed_len);
            free(feed);
            return -1;
        }
        memset(feed, 0, feed_len);
        free(feed);

        size_t copy = okm_len - done;
        if (copy > 32) copy = 32;
        memcpy(okm + done, t, copy);
        done += copy;
        counter++;
    }

    memset(prk, 0, sizeof(prk));
    memset(t, 0, sizeof(t));
    return 0;
}

#endif /* platform HKDF selection */

/* ------------------------------------------------------------------ */
/* Internal state                                                     */
/* ------------------------------------------------------------------ */

static uint8_t g_master_key[EVT_CRYPTO_KEY_LEN];
static bool    g_initialised = false;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

evt_crypto_err_t evt_crypto_init(const char *user_pin, size_t pin_len)
{
    uint8_t uid[16];
    memset(uid, 0, sizeof(uid));

    /* Read device-unique ID from hardware fuse */
    int fd = open(EVT_CRYPTO_UID_PATH, O_RDONLY);
    if (fd < 0) {
        syslog(LOG_ERR, "event_crypto: cannot open %s: %d\n",
               EVT_CRYPTO_UID_PATH, errno);
        return EVT_CRYPTO_ERR_UID;
    }
    ssize_t n = read(fd, uid, sizeof(uid));
    close(fd);
    if (n != (ssize_t)sizeof(uid)) {
        syslog(LOG_ERR, "event_crypto: short read on UID: %zd\n", n);
        return EVT_CRYPTO_ERR_UID;
    }

    /* Build IKM = device_uid || user_pin */
    size_t pin_used = 0;
    if (user_pin != NULL && pin_len > 0) {
        if (pin_len > EVT_CRYPTO_PIN_MAX) {
            pin_len = EVT_CRYPTO_PIN_MAX;
        }
        pin_used = pin_len;
    }

    size_t ikm_len = sizeof(uid) + pin_used;
    uint8_t *ikm = (uint8_t *)malloc(ikm_len);
    if (!ikm) {
        return EVT_CRYPTO_ERR_HKDF;
    }
    memcpy(ikm, uid, sizeof(uid));
    if (pin_used > 0) {
        memcpy(ikm + sizeof(uid), user_pin, pin_used);
    }

    /* HKDF-SHA256 */
    const char *salt_str = EVT_CRYPTO_HKDF_SALT;
    const char *info_str = "velasense-event-key";
    int rc = hkdf_sha256(ikm, ikm_len,
                         (const uint8_t *)salt_str, strlen(salt_str),
                         (const uint8_t *)info_str, strlen(info_str),
                         g_master_key, EVT_CRYPTO_KEY_LEN);

    /* Wipe IKM immediately */
    memset(ikm, 0, ikm_len);
    free(ikm);

    if (rc != 0) {
        syslog(LOG_ERR, "event_crypto: HKDF failed\n");
        return EVT_CRYPTO_ERR_HKDF;
    }

    g_initialised = true;
    syslog(LOG_INFO, "event_crypto: initialised (PIN %s)\n",
           pin_used ? "set" : "none");
    return EVT_CRYPTO_OK;
}

evt_crypto_err_t evt_crypto_rekey(const char *new_pin, size_t pin_len)
{
    evt_crypto_deinit();
    return evt_crypto_init(new_pin, pin_len);
}

void evt_crypto_deinit(void)
{
    volatile uint8_t *p = g_master_key;
    for (int i = 0; i < EVT_CRYPTO_KEY_LEN; i++) {
        p[i] = 0;
    }
    g_initialised = false;
}

bool evt_crypto_is_ready(void)
{
    return g_initialised;
}

/* ------------------------------------------------------------------ */
/* Encrypt / Decrypt                                                  */
/* ------------------------------------------------------------------ */

evt_crypto_err_t evt_crypto_encrypt(const uint8_t plain[EVT_CRYPTO_PLAIN_LEN],
                                    uint8_t *out, size_t *out_len)
{
    if (!g_initialised)             return EVT_CRYPTO_ERR_STATE;
    if (!plain || !out || !out_len) return EVT_CRYPTO_ERR_PARAM;

    uint8_t *iv     = out;
    uint8_t *cipher = out + EVT_CRYPTO_IV_LEN;
    uint8_t *tag    = out + EVT_CRYPTO_IV_LEN + EVT_CRYPTO_CIPHER_LEN;

    /* Generate random IV */
    evt_crypto_err_t rc = evt_crypto_random(iv, EVT_CRYPTO_IV_LEN);
    if (rc != EVT_CRYPTO_OK) return rc;

    if (aes_gcm_encrypt(g_master_key, iv,
                        plain, EVT_CRYPTO_PLAIN_LEN,
                        cipher, tag) != 0) {
        return EVT_CRYPTO_ERR_AES;
    }

    *out_len = EVT_CRYPTO_IV_LEN + EVT_CRYPTO_CIPHER_LEN + EVT_CRYPTO_TAG_LEN;
    return EVT_CRYPTO_OK;
}

evt_crypto_err_t evt_crypto_decrypt(const uint8_t *in, size_t in_len,
                                    uint8_t plain[EVT_CRYPTO_PLAIN_LEN])
{
    if (!g_initialised) return EVT_CRYPTO_ERR_STATE;
    if (!in || !plain)  return EVT_CRYPTO_ERR_PARAM;

    size_t expected = EVT_CRYPTO_IV_LEN + EVT_CRYPTO_CIPHER_LEN + EVT_CRYPTO_TAG_LEN;
    if (in_len != expected) return EVT_CRYPTO_ERR_PARAM;

    const uint8_t *iv     = in;
    const uint8_t *cipher = in + EVT_CRYPTO_IV_LEN;
    const uint8_t *tag    = in + EVT_CRYPTO_IV_LEN + EVT_CRYPTO_CIPHER_LEN;

    if (aes_gcm_decrypt(g_master_key, iv,
                        cipher, EVT_CRYPTO_CIPHER_LEN,
                        plain, tag) != 0) {
        syslog(LOG_WARNING, "event_crypto: GCM tag mismatch\n");
        return EVT_CRYPTO_ERR_AES;
    }

    return EVT_CRYPTO_OK;
}

/* ------------------------------------------------------------------ */
/* RNG                                                                */
/* ------------------------------------------------------------------ */

evt_crypto_err_t evt_crypto_random(uint8_t *buf, size_t len)
{
    if (!buf || len == 0) return EVT_CRYPTO_ERR_PARAM;

    /* Prefer hardware RNG */
    int fd = open("/dev/hwrng", O_RDONLY);
    if (fd < 0) {
        fd = open("/dev/urandom", O_RDONLY);
    }
    if (fd < 0) {
        syslog(LOG_ERR, "event_crypto: no RNG device\n");
        return EVT_CRYPTO_ERR_RNG;
    }

    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, buf + done, len - done);
        if (n <= 0) {
            close(fd);
            return EVT_CRYPTO_ERR_RNG;
        }
        done += (size_t)n;
    }
    close(fd);
    return EVT_CRYPTO_OK;
}

/* ------------------------------------------------------------------ */
/* CRC-32 (IEEE 802.3)                                                */
/* ------------------------------------------------------------------ */

uint32_t evt_crypto_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}
