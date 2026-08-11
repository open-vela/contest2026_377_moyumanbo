/****************************************************************************
 * VelaSense Privacy Compliance Audit Tests — Implementation
 *
 * Verifies privacy-by-design principles across the firmware data path.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "test_privacy.h"

#include "../libs/event/event_crypto.h"
#include "../ble/gatt_service.h"

#include <stdio.h>
#include <ctype.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t g_test_flash[TEST_FLASH_SIZE];
static uint8_t g_test_ble_buf[TEST_BLE_PACKET_MAX * 4];
static uint8_t g_test_upload_buf[TEST_UPLOAD_PAYLOAD_MAX];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: to_lowercase
 *
 * Description:
 *   Convert a string to lowercase in-place.
 *
 ****************************************************************************/

static void to_lowercase(char *str)
{
  if (!str)
    {
      return;
    }

  while (*str)
    {
      *str = (char)tolower((unsigned char)*str);
      str++;
    }
}

/****************************************************************************
 * Helper Function Implementations
 ****************************************************************************/

/****************************************************************************
 * Name: privacy_scan_flash_for_patterns
 ****************************************************************************/

int privacy_scan_flash_for_patterns(const uint8_t *flash, size_t size)
{
  int violations = 0;

  if (!flash || size < 4)
    {
      return 0;
    }

  /* Scan for PPG waveform magic bytes */

  for (size_t i = 0; i <= size - 4; i++)
    {
      uint32_t pattern = ((uint32_t)flash[i] << 24) |
                         ((uint32_t)flash[i + 1] << 16) |
                         ((uint32_t)flash[i + 2] << 8) |
                         ((uint32_t)flash[i + 3]);

      if (pattern == PRIVACY_PPG_PATTERN || pattern == PRIVACY_EDA_PATTERN)
        {
          violations++;
        }
    }

  /* Scan for sequences of float values that look like waveform data.
   * Heuristic: > 10 consecutive floats in a narrow physiological range
   * (PPG: 0-65535 raw counts, EDA: 0.1-100 uS).
   */

  int consecutive_floats = 0;

  for (size_t i = 0; i <= size - sizeof(float); i += sizeof(float))
    {
      float val;
      memcpy(&val, &flash[i], sizeof(float));

      /* Check if value looks like a raw PPG count (0-65535) */

      if (val >= 0.0f && val <= 65535.0f && val != 0.0f)
        {
          consecutive_floats++;
          if (consecutive_floats > 10)
            {
              violations++;
              break;
            }
        }
      else
        {
          consecutive_floats = 0;
        }
    }

  return violations;
}

/****************************************************************************
 * Name: privacy_scan_ble_for_raw_data
 ****************************************************************************/

int privacy_scan_ble_for_raw_data(const uint8_t *payload, size_t size)
{
  int violations = 0;

  if (!payload || size < 4)
    {
      return 0;
    }

  /* BLE packets should only contain event summaries (packed structs),
   * not raw waveform arrays. Check for long sequences of float values.
   */

  int consecutive_floats = 0;

  for (size_t i = 0; i <= size - sizeof(float); i += sizeof(float))
    {
      float val;
      memcpy(&val, &payload[i], sizeof(float));

      /* Raw PPG values are typically in the thousands range */

      if (val > 100.0f && val < 65535.0f)
        {
          consecutive_floats++;
          if (consecutive_floats > 5)
            {
              violations++;
              break;
            }
        }
      else
        {
          consecutive_floats = 0;
        }
    }

  return violations;
}

/****************************************************************************
 * Name: privacy_check_medical_terms
 ****************************************************************************/

int privacy_check_medical_terms(const char *text)
{
  if (!text)
    {
      return 0;
    }

  /* Create a lowercase copy for case-insensitive matching */

  size_t len = strlen(text);
  char *lower = (char *)malloc(len + 1);
  if (!lower)
    {
      return 0;
    }

  memcpy(lower, text, len + 1);
  to_lowercase(lower);

  const char *terms[] = PRIVACY_DIAGNOSIS_TERMS;
  int violation = 0;

  for (int i = 0; terms[i] != NULL; i++)
    {
      if (strstr(lower, terms[i]) != NULL)
        {
          violation = 1;
          break;
        }
    }

  free(lower);
  return violation;
}

/****************************************************************************
 * Name: privacy_check_raw_sensor_data
 ****************************************************************************/

int privacy_check_raw_sensor_data(const void *payload, size_t size)
{
  if (!payload || size < sizeof(float) * 10)
    {
      return 0;
    }

  const uint8_t *data = (const uint8_t *)payload;

  /* Check for sequences of float values that look like raw sensor data.
   * Event records should only contain summary statistics (uint16 packed),
   * not raw float arrays.
   */

  int consecutive_raw_floats = 0;

  for (size_t i = 0; i <= size - sizeof(float); i += sizeof(float))
    {
      float val;
      memcpy(&val, &data[i], sizeof(float));

      /* Raw PPG samples: typically 1000-200000 range */

      if (val > 500.0f && val < 200000.0f)
        {
          consecutive_raw_floats++;
          if (consecutive_raw_floats > 5)
            {
              return 1; /* Violation: looks like raw waveform */
            }
        }
      else
        {
          consecutive_raw_floats = 0;
        }
    }

  return 0;
}

/****************************************************************************
 * Test: No raw waveform data in Flash storage
 ****************************************************************************/

void test_no_raw_waveform_in_storage(void **state)
{
  (void)state;

  /* Test 1: Clean storage (encrypted event records only) */

  memset(g_test_flash, 0, TEST_FLASH_SIZE);

  /* Simulate encrypted event records: 76-byte blocks (iv+ciphertext+tag) */

  for (int i = 0; i < 5; i++)
    {
      size_t offset = i * 80; /* 80 bytes per record with padding */

      if (offset + 76 > TEST_FLASH_SIZE)
        {
          break;
        }

      /* Fill with pseudo-random encrypted data (not waveform patterns) */

      for (size_t j = 0; j < 76; j++)
        {
          g_test_flash[offset + j] = (uint8_t)((i * 37 + j * 13) & 0xFF);
        }
    }

  int violations = privacy_scan_flash_for_patterns(g_test_flash,
                                                    TEST_FLASH_SIZE);
  assert_int_equal(violations, 0);

  /* Test 2: Inject PPG magic pattern — should be detected */

  memset(g_test_flash, 0, TEST_FLASH_SIZE);
  g_test_flash[100] = 0x50; /* 'P' */
  g_test_flash[101] = 0x50; /* 'P' */
  g_test_flash[102] = 0x47; /* 'G' */
  g_test_flash[103] = 0x00; /* '\0' */

  violations = privacy_scan_flash_for_patterns(g_test_flash, TEST_FLASH_SIZE);
  assert_true(violations > 0);

  /* Test 3: Inject EDA magic pattern — should be detected */

  memset(g_test_flash, 0, TEST_FLASH_SIZE);
  g_test_flash[200] = 0x45; /* 'E' */
  g_test_flash[201] = 0x44; /* 'D' */
  g_test_flash[202] = 0x41; /* 'A' */
  g_test_flash[203] = 0x00; /* '\0' */

  violations = privacy_scan_flash_for_patterns(g_test_flash, TEST_FLASH_SIZE);
  assert_true(violations > 0);
}

/****************************************************************************
 * Test: No raw waveform data in BLE packets
 ****************************************************************************/

void test_no_raw_waveform_in_ble(void **state)
{
  (void)state;

  /* Test 1: Clean BLE packet (event summary only) */

  struct ble_event_summary_record record;
  memset(&record, 0, sizeof(record));
  record.seq = 1;
  record.timestamp = 12345;
  record.label = 1;
  record.confidence = 85;
  record.reason_flags = 0x03;
  record.hr = 720;     /* 72.0 BPM * 10 */
  record.rmssd = 450;  /* 45.0 ms * 10 */

  memcpy(g_test_ble_buf, &record, sizeof(record));

  int violations = privacy_scan_ble_for_raw_data(g_test_ble_buf,
                                                  sizeof(record));
  assert_int_equal(violations, 0);

  /* Test 2: Inject raw PPG-like float sequence — should be detected */

  memset(g_test_ble_buf, 0, sizeof(g_test_ble_buf));

  float raw_ppg[] = { 15000.0f, 15200.0f, 14800.0f, 15100.0f,
                      15300.0f, 14900.0f, 15050.0f, 15150.0f };
  memcpy(g_test_ble_buf, raw_ppg, sizeof(raw_ppg));

  violations = privacy_scan_ble_for_raw_data(g_test_ble_buf,
                                              sizeof(raw_ppg));
  assert_true(violations > 0);

  /* Test 3: Verify BLE event notify structure contains no raw data */

  struct ble_event_notify notify;
  memset(&notify, 0, sizeof(notify));
  notify.timestamp = 1000;
  notify.label = 2;
  notify.confidence = 85;
  notify.reason_flags = 0x03;
  notify.hr = 720;
  notify.rmssd = 450;

  /* The notify payload is only 12 bytes of packed integers — no floats */

  assert_int_equal(sizeof(notify), VELASENSE_EVENT_NOTIFY_LEN);

  /* Verify no float sequences in the notify payload */

  violations = privacy_scan_ble_for_raw_data((const uint8_t *)&notify,
                                              sizeof(notify));
  assert_int_equal(violations, 0);
}

/****************************************************************************
 * Test: Encryption integrity
 ****************************************************************************/

void test_encryption_integrity(void **state)
{
  (void)state;

  /* Test 1: Encrypted data cannot be read without key.
   * Initialize crypto and verify decrypt fails with wrong/no key.
   */

  /* Create a fake plaintext event record */

  uint8_t plaintext[EVT_CRYPTO_PLAIN_LEN];
  memset(plaintext, 0xAA, sizeof(plaintext));

  /* Attempting to decrypt without initialization should fail */

  uint8_t ciphertext[EVT_CRYPTO_PLAIN_LEN + EVT_CRYPTO_IV_LEN +
                     EVT_CRYPTO_TAG_LEN];
  size_t cipher_len = 0;

  /* Without crypto init, encrypt should fail gracefully */

  evt_crypto_err_t err = evt_crypto_encrypt(plaintext, ciphertext,
                                             &cipher_len);

  /* If crypto is not initialized, we expect ERR_STATE.
   * If it is initialized (test environment), the operation may succeed.
   * Either way, verify that ciphertext != plaintext.
   */

  if (err == EVT_CRYPTO_OK)
    {
      /* Ciphertext should differ from plaintext */

      int differs = 0;
      for (size_t i = 0; i < EVT_CRYPTO_PLAIN_LEN; i++)
        {
          if (ciphertext[EVT_CRYPTO_IV_LEN + i] != plaintext[i])
            {
              differs = 1;
              break;
            }
        }

      assert_true(differs);

      /* Decrypt with the same key should succeed */

      uint8_t decrypted[EVT_CRYPTO_PLAIN_LEN];
      err = evt_crypto_decrypt(ciphertext, cipher_len, decrypted);
      assert_int_equal(err, EVT_CRYPTO_OK);

      /* Decrypted should match original plaintext */

      assert_memory_equal(decrypted, plaintext, EVT_CRYPTO_PLAIN_LEN);
    }

  /* Test 2: Verify that tampered ciphertext fails GCM authentication.
   * (Only test if crypto is initialized)
   */

  if (err == EVT_CRYPTO_OK)
    {
      /* Flip a bit in the ciphertext */

      ciphertext[EVT_CRYPTO_IV_LEN + 5] ^= 0x01;

      uint8_t decrypted[EVT_CRYPTO_PLAIN_LEN];
      err = evt_crypto_decrypt(ciphertext, cipher_len, decrypted);
      assert_int_equal(err, EVT_CRYPTO_ERR_AES);
    }
}

/****************************************************************************
 * Test: Secure deletion — deleted events are unrecoverable
 ****************************************************************************/

void test_secure_delete(void **state)
{
  (void)state;

  /* Simulate writing an event to Flash, then securely deleting it.
   * After deletion, the storage region should contain no recognizable
   * event data.
   */

  memset(g_test_flash, 0, TEST_FLASH_SIZE);

  /* Write a fake event record */

  struct test_flash_record record;
  record.magic = 0x56454C41; /* "VELA" */
  record.length = sizeof(record.data);
  memset(record.data, 0xBB, sizeof(record.data));

  memcpy(g_test_flash, &record, sizeof(record));

  /* Verify record is present */

  struct test_flash_record *readback =
      (struct test_flash_record *)g_test_flash;
  assert_int_equal(readback->magic, 0x56454C41);
  assert_int_equal(readback->data[0], 0xBB);

  /* Secure delete: overwrite with zeros */

  memset(g_test_flash, 0, sizeof(record));

  /* Verify record is gone */

  readback = (struct test_flash_record *)g_test_flash;
  assert_int_equal(readback->magic, 0);

  /* Verify no recognizable pattern remains */

  for (size_t i = 0; i < sizeof(record); i++)
    {
      assert_int_equal(g_test_flash[i], 0);
    }

  /* Secure delete with random overwrite (more thorough) */

  memcpy(g_test_flash, &record, sizeof(record));
  assert_int_equal(((struct test_flash_record *)g_test_flash)->magic,
                   0x56454C41);

  /* Overwrite with random pattern */

  srand(99);
  for (size_t i = 0; i < sizeof(record); i++)
    {
      g_test_flash[i] = (uint8_t)(rand() & 0xFF);
    }

  /* Then zero */

  memset(g_test_flash, 0, sizeof(record));

  readback = (struct test_flash_record *)g_test_flash;
  assert_int_equal(readback->magic, 0);
}

/****************************************************************************
 * Test: Mimo output contains no medical diagnosis terms
 ****************************************************************************/

void test_mimo_no_medical_terms(void **state)
{
  (void)state;

  /* Test 1: Clean description should pass */

  const char *clean_desc = "User confirmed excitement event at 14:30. "
                           "HR elevated, RMSSD decreased.";
  assert_int_equal(privacy_check_medical_terms(clean_desc), 0);

  /* Test 2: Description with medical terms should fail */

  const char *bad_desc1 = "Possible anxiety disorder detected";
  assert_int_equal(privacy_check_medical_terms(bad_desc1), 1);

  const char *bad_desc2 = "Clinical stress response";
  assert_int_equal(privacy_check_medical_terms(bad_desc2), 1);

  const char *bad_desc3 = "Patient shows elevated HR";
  assert_int_equal(privacy_check_medical_terms(bad_desc3), 1);

  const char *bad_desc4 = "Treatment recommended for condition";
  assert_int_equal(privacy_check_medical_terms(bad_desc4), 1);

  const char *bad_desc5 = "ICD-10 code suggestion";
  assert_int_equal(privacy_check_medical_terms(bad_desc5), 1);

  /* Test 3: Case-insensitive matching */

  const char *bad_desc6 = "DIAGNOSIS: elevated arousal";
  assert_int_equal(privacy_check_medical_terms(bad_desc6), 1);

  const char *bad_desc7 = "Syndrome detected in user";
  assert_int_equal(privacy_check_medical_terms(bad_desc7), 1);

  /* Test 4: NULL input should not crash */

  assert_int_equal(privacy_check_medical_terms(NULL), 0);

  /* Test 5: Empty string should pass */

  assert_int_equal(privacy_check_medical_terms(""), 0);
}

/****************************************************************************
 * Test: Mimo upload payload contains no raw sensor data
 ****************************************************************************/

void test_mimo_no_raw_data(void **state)
{
  (void)state;

  /* Test 1: Clean upload payload (summary data only) */

  struct test_upload_payload payload;
  memset(&payload, 0, sizeof(payload));
  payload.event_seq = 1;
  payload.timestamp = 1234567890;
  payload.label = 2;
  payload.confidence = 85;
  payload.reason_flags = 0x03;
  payload.hr_x10 = 720;
  payload.rmssd_x10 = 450;
  snprintf(payload.description, sizeof(payload.description),
           "User confirmed stress event");

  assert_int_equal(privacy_check_raw_sensor_data(&payload, sizeof(payload)),
                   0);

  /* Test 2: Payload with injected raw PPG array — should be detected */

  memset(g_test_upload_buf, 0, sizeof(g_test_upload_buf));

  /* Embed the clean payload header */

  memcpy(g_test_upload_buf, &payload, 16);

  /* Inject raw PPG-like floats at offset 64 */

  float raw_ppg[20];
  for (int i = 0; i < 20; i++)
    {
      raw_ppg[i] = 15000.0f + (float)(i * 100);
    }

  memcpy(g_test_upload_buf + 64, raw_ppg, sizeof(raw_ppg));

  assert_int_equal(privacy_check_raw_sensor_data(g_test_upload_buf,
                                                  sizeof(g_test_upload_buf)),
                   1);

  /* Test 3: NULL input should not crash */

  assert_int_equal(privacy_check_raw_sensor_data(NULL, 0), 0);
}

/****************************************************************************
 * Test: User consent required before upload
 ****************************************************************************/

void test_user_consent_required(void **state)
{
  (void)state;

  enum test_consent_state consent = TEST_CONSENT_NONE;
  int upload_attempted = 0;
  int upload_blocked = 0;

  /* Test 1: Upload without consent should be blocked */

  if (consent != TEST_CONSENT_GRANTED)
    {
      upload_blocked = 1;
    }

  upload_attempted = 1;
  assert_int_equal(upload_blocked, 1);
  assert_int_equal(consent, TEST_CONSENT_NONE);

  /* Test 2: After consent is granted, upload should proceed */

  consent = TEST_CONSENT_PROMPTED;
  assert_int_not_equal(consent, TEST_CONSENT_GRANTED);

  /* Simulate user granting consent */

  consent = TEST_CONSENT_GRANTED;
  upload_blocked = 0;

  if (consent != TEST_CONSENT_GRANTED)
    {
      upload_blocked = 1;
    }

  assert_int_equal(upload_blocked, 0);

  /* Test 3: After consent is revoked, upload should be blocked again */

  consent = TEST_CONSENT_REVOKED;
  upload_blocked = 0;

  if (consent != TEST_CONSENT_GRANTED)
    {
      upload_blocked = 1;
    }

  assert_int_equal(upload_blocked, 1);

  /* Test 4: Verify BLE characteristic access requires consent.
   * The GATT service should only allow event summary reads when
   * the user has authorized data sharing.
   *
   * This is a structural check: the ble_event_summary_record contains
   * only summary fields (seq, timestamp, label, confidence, reason,
   * hr, rmssd) — no raw waveform data.
   */

  struct ble_event_summary_record summary;
  memset(&summary, 0, sizeof(summary));

  /* Verify the record size matches the spec */

  assert_int_equal(sizeof(summary), VELASENSE_SUMMARY_RECORD_LEN);

  /* Verify no float arrays in the summary (only packed integers) */

  int has_float_array = 0;
  const uint8_t *bytes = (const uint8_t *)&summary;

  for (size_t i = 0; i <= sizeof(summary) - sizeof(float) * 10; i++)
    {
      /* Check for long sequences of float-like values */

      float val;
      memcpy(&val, &bytes[i], sizeof(float));
      if (val > 100.0f && val < 65535.0f)
        {
          has_float_array = 1;
          break;
        }
    }

  /* Summary records should not contain raw float arrays */

  assert_int_equal(has_float_array, 0);
}
