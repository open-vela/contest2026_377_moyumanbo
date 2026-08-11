/****************************************************************************
 * VelaSense Privacy Compliance Audit Tests
 *
 * Verifies that the firmware enforces privacy-by-design principles:
 *   - No raw waveform data in persistent storage
 *   - No raw sensor data in BLE packets
 *   - Encryption integrity for stored events
 *   - Secure deletion of event data
 *   - No medical diagnosis terms in Mimo output
 *   - No raw sensor data in upload payloads
 *   - User consent required before any data upload
 *
 * Framework: CMocka
 ****************************************************************************/

#ifndef __FIRMWARE_TESTS_TEST_PRIVACY_H
#define __FIRMWARE_TESTS_TEST_PRIVACY_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Simulated Flash storage size for testing */

#define TEST_FLASH_SIZE         4096
#define TEST_BLE_PACKET_MAX     244
#define TEST_UPLOAD_PAYLOAD_MAX 512

/* Forbidden medical diagnosis terms */

#define PRIVACY_DIAGNOSIS_TERMS \
  { "diagnosis", "disease", "disorder", "condition", "syndrome", \
    "pathology", "clinical", "medical", "patient", "treatment", \
    "prescription", "medication", "ICD", "DSM", NULL }

/* PPG/EDA waveform pattern signatures to detect in storage */

#define PRIVACY_PPG_PATTERN     0x50504700  /* "PPG\0" */
#define PRIVACY_EDA_PATTERN     0x45444100  /* "EDA\0" */

/****************************************************************************
 * Test Function Prototypes
 ****************************************************************************/

void test_no_raw_waveform_in_storage(void **state);
void test_no_raw_waveform_in_ble(void **state);
void test_encryption_integrity(void **state);
void test_secure_delete(void **state);
void test_mimo_no_medical_terms(void **state);
void test_mimo_no_raw_data(void **state);
void test_user_consent_required(void **state);

/****************************************************************************
 * Helper Types
 ****************************************************************************/

/* Simulated Flash storage record */

struct test_flash_record
{
  uint32_t magic;
  uint32_t length;
  uint8_t  data[256];
};

/* Simulated BLE packet */

struct test_ble_packet
{
  uint16_t uuid_offset;
  uint16_t length;
  uint8_t  payload[TEST_BLE_PACKET_MAX];
};

/* Simulated upload payload */

struct test_upload_payload
{
  uint32_t event_seq;
  uint32_t timestamp;
  uint8_t  label;
  uint8_t  confidence;
  uint16_t reason_flags;
  uint16_t hr_x10;
  uint16_t rmssd_x10;
  char     description[128];
  uint8_t  reserved[64];
};

/* Consent state */

enum test_consent_state
{
  TEST_CONSENT_NONE = 0,
  TEST_CONSENT_PROMPTED,
  TEST_CONSENT_GRANTED,
  TEST_CONSENT_REVOKED
};

/****************************************************************************
 * Helper Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: privacy_scan_flash_for_patterns
 *
 * Description:
 *   Scan a simulated Flash buffer for forbidden waveform data patterns.
 *
 * Returned Value:
 *   Number of violations found.
 *
 ****************************************************************************/

int privacy_scan_flash_for_patterns(const uint8_t *flash, size_t size);

/****************************************************************************
 * Name: privacy_scan_ble_for_raw_data
 *
 * Description:
 *   Scan BLE packet payloads for raw sensor data signatures.
 *
 * Returned Value:
 *   Number of violations found.
 *
 ****************************************************************************/

int privacy_scan_ble_for_raw_data(const uint8_t *payload, size_t size);

/****************************************************************************
 * Name: privacy_check_medical_terms
 *
 * Description:
 *   Check a text string for forbidden medical diagnosis terms.
 *
 * Returned Value:
 *   1 if violation found, 0 if clean.
 *
 ****************************************************************************/

int privacy_check_medical_terms(const char *text);

/****************************************************************************
 * Name: privacy_check_raw_sensor_data
 *
 * Description:
 *   Check an upload payload for raw sensor data (float arrays that
 *   look like waveform samples).
 *
 * Returned Value:
 *   1 if violation found, 0 if clean.
 *
 ****************************************************************************/

int privacy_check_raw_sensor_data(const void *payload, size_t size);

#endif /* __FIRMWARE_TESTS_TEST_PRIVACY_H */
