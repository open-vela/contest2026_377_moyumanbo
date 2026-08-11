#!/usr/bin/env python3
"""
privacy_audit.py -- Automated Privacy Compliance Scanner for VelaSense

Scans the VelaSense firmware and application source code for privacy
violations, data leakage risks, and compliance issues.

Checks performed:
  1. Raw waveform storage: PPG/IMU/EDA raw data must not be persisted
     in Flash or transmitted over BLE in unencrypted form.
  2. Hardcoded secrets: API keys, tokens, passwords in source code.
  3. Encryption coverage: Verify event_crypto is used for all stored data.
  4. BLE data minimization: Check that GATT characteristics send only
     derived features, not raw waveforms.
  5. Cloud upload payload: Verify Mimo/sensorhub uploads contain only
     aggregated features, not raw sensor streams.
  6. PII exposure: Check for logging of personally identifiable data.
  7. Key management: Verify key derivation uses device UID + user PIN.

Compliance frameworks referenced:
  - GDPR Article 25 (Data Protection by Design)
  - IEEE 7012-2017 (Standard for Privacy Process for IoT)
  - HIPAA Safe Harbor (health data de-identification)

Usage:
  python privacy_audit.py --src ../firmware/ -o audit_report.json
  python privacy_audit.py --src ../firmware/ --src ../app/ --verbose
  python privacy_audit.py --src ../firmware/ --fix-suggestions
"""

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from enum import Enum
from typing import Dict, List, Optional, Set, Tuple


# ---------------------------------------------------------------------------
# Severity levels
# ---------------------------------------------------------------------------

class Severity(Enum):
    CRITICAL = 'critical'     # Must fix before deployment
    HIGH = 'high'             # Should fix, significant risk
    MEDIUM = 'medium'         # Recommended fix
    LOW = 'low'               # Informational
    INFO = 'info'             # Passed check


@dataclass
class Finding:
    """A single audit finding."""
    rule_id: str
    severity: Severity
    title: str
    description: str
    file_path: str
    line_number: int
    code_snippet: str
    recommendation: str
    cwe: str = ''  # CWE ID if applicable


# ---------------------------------------------------------------------------
# Rule definitions
# ---------------------------------------------------------------------------

# Rule 1: Raw waveform variable names in storage/BLE contexts
_RAW_WAVEFORM_PATTERNS = [
    # PPG raw data patterns
    r'\b(?:ppg_raw|raw_ppg|ppg_buffer|ppg_data|raw_signal)\b',
    r'\b(?:green_raw|red_raw|ir_raw)\b',
    r'\b(?:ppg_green|ppg_red|ppg_ir)\b.*(?:write|store|save|flash|ble|gatt)',
    # IMU raw data patterns
    r'\b(?:accel_raw|gyro_raw|imu_raw|raw_accel|raw_gyro)\b',
    r'\b(?:ax_raw|ay_raw|az_raw|gx_raw|gy_raw|gz_raw)\b',
    # EDA raw data patterns
    r'\b(?:scl_raw|eda_raw|raw_eda|gsr_raw|conductance_raw)\b',
    # Generic raw sensor patterns
    r'\b(?:raw_sample|raw_fifo|fifo_data|adc_raw)\b',
]

# Rule 2: Hardcoded secrets
_SECRET_PATTERNS = [
    (r'(?:api[_-]?key|apikey)\s*=\s*["\'][A-Za-z0-9+/=]{16,}["\']',
     'Hardcoded API key'),
    (r'(?:secret|token|password|passwd|pwd)\s*=\s*["\'][^"\']{8,}["\']',
     'Hardcoded secret/token/password'),
    (r'(?:bearer|authorization)\s*[:=]\s*["\'][^"\']{10,}["\']',
     'Hardcoded auth token'),
    (r'(?:aws[_-]?(?:access|secret)[_-]?key)\s*=\s*["\'][A-Za-z0-9/+=]{16,}["\']',
     'AWS credential'),
    (r'(?:private[_-]?key)\s*=\s*["\'][^"\']{16,}["\']',
     'Hardcoded private key'),
    (r'(?:mqtt[_-]?(?:pass|user|token))\s*=\s*["\'][^"\']{4,}["\']',
     'MQTT credential'),
]

# Rule 3: Encryption module usage
_ENCRYPTION_REQUIRED_CALLS = [
    'evt_crypto_encrypt',
    'evt_crypto_init',
]

_ENCRYPTION_FILE_PATTERNS = [
    r'storage',
    r'flash',
    r'eeprom',
    r'nvs',
    r'persist',
    r'event_log',
    r'event_store',
]

# Rule 4: BLE data minimization
_BLE_RAW_PATTERNS = [
    (r'(?:bt_gatt_notify|gatt_notify|ble_send).*\b(?:ppg|raw|fifo|adc|sample)\b',
     'BLE notification may contain raw sensor data'),
    (r'(?:bt_gatt_attr_write|gatt_write).*\b(?:ppg|raw|fifo|adc)\b',
     'BLE write may contain raw sensor data'),
    (r'(?:event_notify|notify_data).*(?:ppg_raw|raw_data|fifo)',
     'Event notification contains raw waveform data'),
]

# Rule 5: Cloud upload payload
_UPLOAD_PATTERNS = [
    (r'(?:upload|mimo|sensorhub|cloud|mqtt_publish|http_post).*\b(?:raw|fifo|adc|waveform)\b',
     'Cloud upload may contain raw sensor data'),
    (r'(?:upload|send)_data.*\b(?:ppg_green|ppg_red|ppg_ir|accel|gyro|scl)\b',
     'Upload payload contains raw sensor channels'),
]

# Rule 6: PII / personal data logging
_PII_PATTERNS = [
    (r'(?:log|printf|print|syslog|dbg)\b.*\b(?:user_?id|email|phone|name|address|ssn|dob)\b',
     'Logging potentially personally identifiable information'),
    (r'(?:log|printf|print)\b.*\b(?:location|gps|lat|lon|longitude|latitude)\b',
     'Logging location data'),
    (r'(?:log|printf)\b.*\b(?:heart_rate|hr|bpm)\b.*\b(?:user|patient|subject)\b',
     'Logging health data with user identifier'),
]

# Rule 7: Key management
_KEY_PATTERNS = [
    (r'(?:master[_-]?key|encryption[_-]?key)\s*=\s*(?:0x)?[0-9a-fA-F]{16,}',
     'Hardcoded encryption key'),
    (r'(?:key|iv|nonce)\s*\[\s*\]\s*=\s*\{[^}]{20,}\}',
     'Hardcoded key/IV array'),
]

# Compile all regex patterns
_COMPILED_PATTERNS = {}
for name, patterns in [
    ('raw_waveform', _RAW_WAVEFORM_PATTERNS),
    ('secret', [p[0] for p in _SECRET_PATTERNS]),
    ('ble_raw', [p[0] for p in _BLE_RAW_PATTERNS]),
    ('upload_raw', [p[0] for p in _UPLOAD_PATTERNS]),
    ('pii_log', [p[0] for p in _PII_PATTERNS]),
    ('key_mgmt', [p[0] for p in _KEY_PATTERNS]),
]:
    _COMPILED_PATTERNS[name] = [re.compile(p, re.IGNORECASE) for p in patterns]


# ---------------------------------------------------------------------------
# Scanner
# ---------------------------------------------------------------------------

class PrivacyScanner:
    """
    Scans source files for privacy violations.

    The scanner maintains state across files to detect cross-file issues
    (e.g., storage module without encryption calls).
    """

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.findings: List[Finding] = []
        self.files_scanned: int = 0
        self.files_skipped: int = 0

        # Tracking state for cross-file checks
        self._storage_files: List[str] = []
        self._has_encryption_init: bool = False
        self._has_encryption_calls: Set[str] = set()
        self._ble_files: List[str] = []

    def scan_directory(self, src_dir: str) -> None:
        """
        Recursively scan all source files in a directory.

        Scans .c, .h, .cpp, .hpp, .py, .json, .yaml, .yml files.
        """
        if not os.path.isdir(src_dir):
            print(f'WARNING: Directory not found: {src_dir}')
            return

        for root, dirs, files in os.walk(src_dir):
            # Skip hidden directories and build artifacts
            dirs[:] = [d for d in dirs
                       if not d.startswith('.') and d not in
                       ('build', 'output', '__pycache__', 'node_modules')]

            for fname in sorted(files):
                if not fname.endswith(('.c', '.h', '.cpp', '.hpp', '.py',
                                       '.json', '.yaml', '.yml')):
                    continue

                filepath = os.path.join(root, fname)
                self.scan_file(filepath)

    def scan_file(self, filepath: str) -> None:
        """Scan a single file for privacy issues."""
        try:
            with open(filepath, 'r', errors='replace') as f:
                content = f.read()
                lines = content.split('\n')
        except (IOError, OSError) as e:
            if self.verbose:
                print(f'  Skipping {filepath}: {e}')
            self.files_skipped += 1
            return

        self.files_scanned += 1
        basename = os.path.basename(filepath).lower()

        # Track file categories for cross-file checks
        is_storage = any(p in basename for p in
                         ('storage', 'flash', 'nvs', 'eeprom', 'event_store',
                          'event_log', 'persist'))
        is_ble = any(p in basename for p in
                     ('ble', 'gatt', 'bluetooth', 'bt_'))
        is_crypto = 'crypto' in basename or 'encrypt' in basename

        if is_storage:
            self._storage_files.append(filepath)
        if is_ble:
            self._ble_files.append(filepath)

        # Check for encryption module usage
        for pattern in _ENCRYPTION_REQUIRED_CALLS:
            if pattern in content:
                self._has_encryption_calls.add(pattern)
                if pattern == 'evt_crypto_init':
                    self._has_encryption_init = True

        # Run all pattern checks
        for line_num, line in enumerate(lines, 1):
            self._check_line(filepath, line_num, line, lines)

    def _check_line(self, filepath: str, line_num: int,
                    line: str, all_lines: List[str]) -> None:
        """Check a single line against all rules."""
        stripped = line.strip()
        if not stripped or stripped.startswith('//') or stripped.startswith('*'):
            return  # Skip comments

        basename = os.path.basename(filepath).lower()
        line_lower = line.lower()

        # Rule 1: Raw waveform in storage/BLE context
        if any(p in basename for p in ('storage', 'flash', 'ble', 'gatt',
                                        'upload', 'cloud', 'mqtt')):
            for pattern in _COMPILED_PATTERNS['raw_waveform']:
                if pattern.search(line):
                    self._add_finding(Finding(
                        rule_id='PRIV-001',
                        severity=Severity.HIGH,
                        title='Raw waveform variable in storage/BLE context',
                        description=(
                            f'Variable name suggests raw sensor data may be '
                            f'stored or transmitted without processing/encryption.'
                        ),
                        file_path=filepath,
                        line_number=line_num,
                        code_snippet=stripped[:120],
                        recommendation=(
                            'Only store/transmit derived features (HR, HRV, '
                            'activity level, arousal class). Raw waveforms '
                            'should be processed on-device and discarded.'
                        ),
                        cwe='CWE-200'
                    ))

        # Rule 2: Hardcoded secrets
        for i, (pattern_str, desc) in enumerate(_SECRET_PATTERNS):
            pattern = _COMPILED_PATTERNS['secret'][i]
            if pattern.search(line) and 'example' not in filepath.lower():
                self._add_finding(Finding(
                    rule_id='PRIV-002',
                    severity=Severity.CRITICAL,
                    title=f'Hardcoded secret: {desc}',
                    description=(
                        f'Potential hardcoded credential found in source code. '
                        f'Secrets must be stored in secure storage or injected '
                        f'at build time via environment variables.'
                    ),
                    file_path=filepath,
                    line_number=line_num,
                    code_snippet='[REDACTED]',
                    recommendation=(
                        'Move secrets to device secure storage (e.g., '
                        '/dev/uid0 fuse, NVS with encryption). Use HKDF '
                        'key derivation as in event_crypto.h.'
                    ),
                    cwe='CWE-798'
                ))

        # Rule 3: Storage files without encryption
        if is_storage_file(filepath):
            if 'encrypt' not in line_lower and 'crypto' not in line_lower:
                if any(kw in line_lower for kw in
                       ('write', 'store', 'save', 'append', 'flash_write')):
                    self._add_finding(Finding(
                        rule_id='PRIV-003',
                        severity=Severity.HIGH,
                        title='Storage write without encryption',
                        description=(
                            'Data is being written to persistent storage '
                            'without apparent encryption. All sensor data '
                            'stored in Flash must be encrypted with '
                            'AES-128-GCM (event_crypto.h).'
                        ),
                        file_path=filepath,
                        line_number=line_num,
                        code_snippet=stripped[:120],
                        recommendation=(
                            'Use evt_crypto_encrypt() before writing to Flash. '
                            'See firmware/libs/event/event_crypto.h for the '
                            'AES-128-GCM encryption API.'
                        ),
                        cwe='CWE-311'
                    ))

        # Rule 4: BLE raw data
        for i, (pattern_str, desc) in enumerate(_BLE_RAW_PATTERNS):
            pattern = _COMPILED_PATTERNS['ble_raw'][i]
            if pattern.search(line):
                self._add_finding(Finding(
                    rule_id='PRIV-004',
                    severity=Severity.HIGH,
                    title=f'BLE data minimization: {desc}',
                    description=(
                        'BLE GATT characteristic may be transmitting raw '
                        'sensor data. Per GDPR Article 25, only derived '
                        'features should leave the device.'
                    ),
                    file_path=filepath,
                    line_number=line_num,
                    code_snippet=stripped[:120],
                    recommendation=(
                        'Transmit only derived features over BLE: '
                        'event class, confidence, HR, HRV, activity level. '
                        'Raw waveforms should never leave the device.'
                    ),
                    cwe='CWE-200'
                ))

        # Rule 5: Cloud upload with raw data
        for i, (pattern_str, desc) in enumerate(_UPLOAD_PATTERNS):
            pattern = _COMPILED_PATTERNS['upload_raw'][i]
            if pattern.search(line):
                self._add_finding(Finding(
                    rule_id='PRIV-005',
                    severity=Severity.CRITICAL,
                    title=f'Cloud upload: {desc}',
                    description=(
                        'Cloud/MQTT upload payload appears to contain raw '
                        'sensor data. This violates data minimization '
                        'principles and may expose health information.'
                    ),
                    file_path=filepath,
                    line_number=line_num,
                    code_snippet=stripped[:120],
                    recommendation=(
                        'Upload only aggregated features: arousal class, '
                        'confidence score, daily HR/HRV statistics. '
                        'Never upload raw PPG/IMU/EDA waveforms to cloud.'
                    ),
                    cwe='CWE-200'
                ))

        # Rule 6: PII logging
        for i, (pattern_str, desc) in enumerate(_PII_PATTERNS):
            pattern = _COMPILED_PATTERNS['pii_log'][i]
            if pattern.search(line):
                self._add_finding(Finding(
                    rule_id='PRIV-006',
                    severity=Severity.MEDIUM,
                    title=f'PII in logs: {desc}',
                    description=(
                        'Log statement may contain personally identifiable '
                        'information. Logs can be intercepted or stored '
                        'insecurely.'
                    ),
                    file_path=filepath,
                    line_number=line_num,
                    code_snippet=stripped[:120],
                    recommendation=(
                        'Remove PII from log statements or use anonymized '
                        'identifiers. Health data should never be logged '
                        'with user identifiers.'
                    ),
                    cwe='CWE-532'
                ))

        # Rule 7: Hardcoded keys
        for i, (pattern_str, desc) in enumerate(_KEY_PATTERNS):
            pattern = _COMPILED_PATTERNS['key_mgmt'][i]
            if pattern.search(line):
                self._add_finding(Finding(
                    rule_id='PRIV-007',
                    severity=Severity.CRITICAL,
                    title=f'Key management: {desc}',
                    description=(
                        'Cryptographic key material found hardcoded in '
                        'source code. Keys must be derived at runtime '
                        'from device UID + user PIN.'
                    ),
                    file_path=filepath,
                    line_number=line_num,
                    code_snippet='[REDACTED]',
                    recommendation=(
                        'Derive keys using HKDF from device-unique fuse '
                        '(EVT_CRYPTO_UID_PATH) and user PIN. See '
                        'event_crypto.h for the key hierarchy.'
                    ),
                    cwe='CWE-321'
                ))

    def _add_finding(self, finding: Finding) -> None:
        """Add a finding, deduplicating by file+line+rule."""
        # Avoid duplicate findings on the same line
        for existing in self.findings:
            if (existing.file_path == finding.file_path and
                    existing.line_number == finding.line_number and
                    existing.rule_id == finding.rule_id):
                return
        self.findings.append(finding)

    def run_cross_file_checks(self) -> None:
        """Run checks that span multiple files."""
        # Check: Storage files exist but no encryption module usage
        if self._storage_files and not self._has_encryption_init:
            for filepath in self._storage_files:
                self._add_finding(Finding(
                    rule_id='PRIV-008',
                    severity=Severity.HIGH,
                    title='Storage module without encryption initialization',
                    description=(
                        'Persistent storage module detected but '
                        'evt_crypto_init() was never called in the codebase. '
                        'All stored data must be encrypted.'
                    ),
                    file_path=filepath,
                    line_number=0,
                    code_snippet='',
                    recommendation=(
                        'Ensure evt_crypto_init() is called at boot before '
                        'any data is written to Flash. See the key hierarchy '
                        'in event_crypto.h.'
                    ),
                    cwe='CWE-311'
                ))

        # Check: BLE service defined without data minimization
        if self._ble_files:
            for filepath in self._ble_files:
                try:
                    with open(filepath, 'r') as f:
                        content = f.read()
                    # Check for raw data fields in BLE characteristics
                    if re.search(r'ppg|raw|fifo|adc|waveform', content,
                                 re.IGNORECASE):
                        self._add_finding(Finding(
                            rule_id='PRIV-009',
                            severity=Severity.MEDIUM,
                            title='BLE service references raw sensor data',
                            description=(
                                'BLE GATT service definition references '
                                'raw sensor data types. Ensure only derived '
                                'features are exposed via BLE characteristics.'
                            ),
                            file_path=filepath,
                            line_number=0,
                            code_snippet='',
                            recommendation=(
                                'Review BLE characteristic definitions. '
                                'Only transmit: event_class (u8), confidence '
                                '(u8), hr_bpm (u16), hrv_sdnn (u16), '
                                'activity_level (u8).'
                            ),
                            cwe='CWE-200'
                        ))
                except (IOError, OSError):
                    pass

    def generate_report(self) -> Dict:
        """Generate a structured compliance report."""
        self.run_cross_file_checks()

        severity_counts = {s.value: 0 for s in Severity}
        for f in self.findings:
            severity_counts[f.severity.value] += 1

        report = {
            'tool': 'velasense_privacy_audit',
            'version': '1.0.0',
            'summary': {
                'files_scanned': self.files_scanned,
                'files_skipped': self.files_skipped,
                'total_findings': len(self.findings),
                'by_severity': severity_counts,
                'compliant': severity_counts['critical'] == 0 and
                             severity_counts['high'] == 0,
            },
            'findings': [
                {
                    'rule_id': f.rule_id,
                    'severity': f.severity.value,
                    'title': f.title,
                    'description': f.description,
                    'file': f.file_path,
                    'line': f.line_number,
                    'code_snippet': f.code_snippet,
                    'recommendation': f.recommendation,
                    'cwe': f.cwe,
                }
                for f in self.findings
            ],
            'compliance_checklist': {
                'data_minimization': not any(
                    f.rule_id in ('PRIV-001', 'PRIV-004', 'PRIV-005')
                    for f in self.findings
                ),
                'encryption_at_rest': not any(
                    f.rule_id in ('PRIV-003', 'PRIV-008')
                    for f in self.findings
                ),
                'no_hardcoded_secrets': not any(
                    f.rule_id in ('PRIV-002', 'PRIV-007')
                    for f in self.findings
                ),
                'pii_protection': not any(
                    f.rule_id == 'PRIV-006' for f in self.findings
                ),
            }
        }

        return report


def is_storage_file(filepath: str) -> bool:
    """Check if a file path indicates a storage-related module."""
    basename = os.path.basename(filepath).lower()
    return any(p in basename for p in _ENCRYPTION_FILE_PATTERNS)


# ---------------------------------------------------------------------------
# Pretty-print report
# ---------------------------------------------------------------------------

def print_report(report: Dict, verbose: bool = False) -> None:
    """Print a human-readable audit report."""
    summary = report['summary']
    checklist = report['compliance_checklist']

    print('=' * 70)
    print('  VelaSense Privacy Compliance Audit Report')
    print('=' * 70)
    print()
    print(f'  Files scanned:  {summary["files_scanned"]}')
    print(f'  Files skipped:  {summary["files_skipped"]}')
    print(f'  Total findings: {summary["total_findings"]}')
    print()

    # Severity breakdown
    print('  Findings by severity:')
    for sev, count in summary['by_severity'].items():
        if count > 0:
            marker = {'critical': '!!!', 'high': '!! ', 'medium': '!  ',
                      'low': '.  ', 'info': '   '}.get(sev, '   ')
            print(f'    [{marker}] {sev.upper():10s}  {count}')
    print()

    # Compliance checklist
    print('  Compliance Checklist:')
    all_pass = True
    for check, passed in checklist.items():
        status = 'PASS' if passed else 'FAIL'
        marker = '[OK]' if passed else '[!!]'
        if not passed:
            all_pass = False
        print(f'    {marker} {check.replace("_", " ").title():30s}  {status}')
    print()

    if all_pass:
        print('  OVERALL: COMPLIANT')
    else:
        print('  OVERALL: NON-COMPLIANT -- issues found')
    print()

    # Detailed findings
    if verbose or summary['total_findings'] > 0:
        print('-' * 70)
        print('  Detailed Findings:')
        print('-' * 70)

        for i, f in enumerate(report['findings'], 1):
            sev = f['severity'].upper()
            print(f'\n  [{i}] {f["rule_id"]} ({sev}): {f["title"]}')
            print(f'      File: {f["file"]}:{f["line"]}')
            if f['code_snippet']:
                print(f'      Code: {f["code_snippet"]}')
            print(f'      Issue: {f["description"]}')
            print(f'      Fix:   {f["recommendation"]}')
            if f['cwe']:
                print(f'      CWE:   {f["cwe"]}')

    print()
    print('=' * 70)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description='VelaSense Privacy Compliance Scanner',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        '--src', action='append', required=True,
        help='Source directory to scan (can be specified multiple times)'
    )
    parser.add_argument(
        '-o', '--output',
        help='Output JSON report path'
    )
    parser.add_argument(
        '--verbose', '-v', action='store_true',
        help='Show detailed output including passed checks'
    )
    parser.add_argument(
        '--fix-suggestions', action='store_true',
        help='Include fix suggestions in output'
    )
    parser.add_argument(
        '--fail-on-high', action='store_true',
        help='Exit with non-zero code if HIGH or CRITICAL findings exist'
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    scanner = PrivacyScanner(verbose=args.verbose)

    for src_dir in args.src:
        abs_dir = os.path.abspath(src_dir)
        print(f'Scanning: {abs_dir}')
        scanner.scan_directory(abs_dir)

    report = scanner.generate_report()
    print_report(report, verbose=args.verbose)

    if args.output:
        with open(args.output, 'w') as f:
            json.dump(report, f, indent=2)
        print(f'\nJSON report saved to: {args.output}')

    if args.fail_on_high:
        sev = report['summary']['by_severity']
        if sev.get('critical', 0) > 0 or sev.get('high', 0) > 0:
            return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
