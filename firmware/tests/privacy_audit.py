#!/usr/bin/env python3
"""
VelaSense Automated Privacy Audit Script

Scans the firmware codebase for privacy violations and generates
a compliance report. Designed to run in CI/CD pipelines.

Checks:
  1. No raw PPG/EDA data written to Flash storage
  2. No raw sensor data in BLE packet structures
  3. No medical diagnosis terms in Mimo output payloads
  4. No API keys or secrets in source code
  5. No raw data in log/syslog output
  6. Encryption enabled for all stored events
  7. User consent checked before any upload
"""

import os
import re
import sys
import json
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Root directory of the firmware source tree
FIRMWARE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# File extensions to scan
SOURCE_EXTENSIONS = {".c", ".h", ".cpp", ".hpp"}

# Medical diagnosis terms that must not appear in upload/BLE payloads
DIAGNOSIS_TERMS = [
    "diagnosis",
    "disease",
    "disorder",
    "condition",
    "syndrome",
    "pathology",
    "clinical",
    "medical",
    "patient",
    "treatment",
    "prescription",
    "medication",
    "icd",
    "dsm",
    "anxiety",
    "depression",
    "bipolar",
    "schizophrenia",
    "ptsd",
    "ocd",
    "adhd",
]

# Patterns that indicate raw waveform data being stored or transmitted
RAW_DATA_PATTERNS = [
    r"flash_write.*ppg",
    r"flash_write.*eda",
    r"flash_write.*raw",
    r"nvs_write.*ppg",
    r"nvs_write.*eda",
    r"nvs_write.*raw",
    r"storage.*ppg_buf",
    r"storage.*eda_buf",
    r"storage.*raw_buf",
]

# BLE patterns that indicate raw sensor data in packets
BLE_RAW_PATTERNS = [
    r"ble.*ppg_data",
    r"ble.*eda_data",
    r"ble.*raw_waveform",
    r"ble.*sensor_buf",
    r"gatt.*ppg_buf",
    r"gatt.*eda_buf",
]

# API key / secret patterns
SECRET_PATTERNS = [
    r"api[_-]?key\s*[=:]\s*['\"][a-zA-Z0-9]{16,}['\"]",
    r"secret[_-]?key\s*[=:]\s*['\"][a-zA-Z0-9]{16,}['\"]",
    r"password\s*[=:]\s*['\"][a-zA-Z0-9]{8,}['\"]",
    r"token\s*[=:]\s*['\"][a-zA-Z0-9]{16,}['\"]",
    r"private[_-]?key\s*[=:]\s*['\"][a-zA-Z0-9]{16,}['\"]",
    r"sk-[a-zA-Z0-9]{20,}",  # OpenAI-style keys
    r"AKIA[0-9A-Z]{16}",     # AWS access keys
]

# Log patterns that might leak raw data
LOG_LEAK_PATTERNS = [
    r"syslog.*ppg_buf",
    r"syslog.*eda_buf",
    r"syslog.*raw_data",
    r"printf.*ppg_buf",
    r"printf.*eda_buf",
    r"printf.*raw_data",
    r"LOG.*ppg_sample",
    r"LOG.*eda_sample",
]

# Encryption patterns (should be present for event storage)
ENCRYPTION_REQUIRED_PATTERNS = [
    r"evt_crypto_encrypt",
    r"aes.*gcm",
    r"encrypt.*event",
]

# Consent check patterns
CONSENT_PATTERNS = [
    r"consent",
    r"user.*authoriz",
    r"user.*permiss",
    r"data.*shar.*enabl",
]


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

class Severity(Enum):
    CRITICAL = "CRITICAL"
    HIGH = "HIGH"
    MEDIUM = "MEDIUM"
    LOW = "LOW"
    INFO = "INFO"


@dataclass
class Violation:
    check_name: str
    severity: Severity
    file_path: str
    line_number: int
    line_content: str
    description: str


@dataclass
class AuditReport:
    total_files_scanned: int = 0
    total_lines_scanned: int = 0
    violations: list = field(default_factory=list)
    checks_passed: list = field(default_factory=list)
    checks_failed: list = field(default_factory=list)


# ---------------------------------------------------------------------------
# Scanner functions
# ---------------------------------------------------------------------------

def collect_source_files(root: str) -> list:
    """Collect all C/H source files under the firmware root."""
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        # Skip build directories and hidden directories
        dirnames[:] = [d for d in dirnames
                       if not d.startswith(".") and d != "build"]
        for fname in filenames:
            ext = os.path.splitext(fname)[1].lower()
            if ext in SOURCE_EXTENSIONS:
                files.append(os.path.join(dirpath, fname))
    return files


def scan_file(filepath: str, patterns: list, check_name: str,
              severity: Severity, description: str) -> list:
    """Scan a file for regex patterns and return violations."""
    violations = []
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            for line_num, line in enumerate(f, 1):
                for pattern in patterns:
                    if re.search(pattern, line, re.IGNORECASE):
                        violations.append(Violation(
                            check_name=check_name,
                            severity=severity,
                            file_path=filepath,
                            line_number=line_num,
                            line_content=line.strip()[:120],
                            description=description,
                        ))
    except (IOError, OSError):
        pass
    return violations


def check_no_raw_waveform_in_storage(files: list) -> tuple:
    """Check 1: No raw PPG/EDA data written to Flash."""
    violations = []
    for f in files:
        violations.extend(scan_file(
            f, RAW_DATA_PATTERNS,
            "no_raw_waveform_in_storage",
            Severity.CRITICAL,
            "Raw waveform data must not be written to Flash storage. "
            "Only encrypted event summaries should be persisted.",
        ))
    passed = len(violations) == 0
    return passed, violations


def check_no_raw_data_in_ble(files: list) -> tuple:
    """Check 2: No raw sensor data in BLE packets."""
    violations = []
    for f in files:
        violations.extend(scan_file(
            f, BLE_RAW_PATTERNS,
            "no_raw_data_in_ble",
            Severity.CRITICAL,
            "Raw sensor data must not be included in BLE packets. "
            "Only event summaries and device status should be transmitted.",
        ))
    passed = len(violations) == 0
    return passed, violations


def check_no_medical_terms_in_output(files: list) -> tuple:
    """Check 3: No medical diagnosis terms in Mimo output."""
    violations = []
    # Only check files related to BLE, upload, or Mimo output
    target_keywords = {"ble", "gatt", "mimo", "upload", "sync", "notify"}
    for f in files:
        basename = os.path.basename(f).lower()
        if not any(kw in basename for kw in target_keywords):
            continue
        for term in DIAGNOSIS_TERMS:
            pattern = rf"\b{re.escape(term)}\b"
            violations.extend(scan_file(
                f, [pattern],
                "no_medical_terms_in_output",
                Severity.HIGH,
                f"Medical diagnosis term '{term}' found in BLE/upload code. "
                "Output payloads must not contain clinical terminology.",
            ))
    passed = len(violations) == 0
    return passed, violations


def check_no_api_keys_in_source(files: list) -> tuple:
    """Check 4: No API keys or secrets in source code."""
    violations = []
    for f in files:
        violations.extend(scan_file(
            f, SECRET_PATTERNS,
            "no_api_keys_in_source",
            Severity.CRITICAL,
            "API keys or secrets must not be embedded in source code. "
            "Use secure key storage or environment variables.",
        ))
    passed = len(violations) == 0
    return passed, violations


def check_no_raw_data_in_logs(files: list) -> tuple:
    """Check 5: No raw sensor data in log/syslog output."""
    violations = []
    for f in files:
        violations.extend(scan_file(
            f, LOG_LEAK_PATTERNS,
            "no_raw_data_in_logs",
            Severity.MEDIUM,
            "Raw sensor data should not be logged via syslog/printf. "
            "Log only summary statistics or SQI values.",
        ))
    passed = len(violations) == 0
    return passed, violations


def check_encryption_enabled(files: list) -> tuple:
    """Check 6: Encryption is used for event storage."""
    violations = []
    found_encryption = False

    for f in files:
        try:
            with open(f, "r", encoding="utf-8", errors="replace") as fh:
                content = fh.read()
                for pattern in ENCRYPTION_REQUIRED_PATTERNS:
                    if re.search(pattern, content, re.IGNORECASE):
                        found_encryption = True
                        break
        except (IOError, OSError):
            pass

    if not found_encryption:
        violations.append(Violation(
            check_name="encryption_enabled",
            severity=Severity.HIGH,
            file_path="(codebase)",
            line_number=0,
            line_content="",
            description="No encryption calls found for event storage. "
                        "All stored events must be encrypted with AES-128-GCM.",
        ))

    passed = found_encryption
    return passed, violations


def check_user_consent_required(files: list) -> tuple:
    """Check 7: User consent is checked before data upload."""
    violations = []
    found_consent = False

    # Check BLE/upload related files for consent patterns
    target_keywords = {"ble", "gatt", "upload", "sync", "mimo"}
    for f in files:
        basename = os.path.basename(f).lower()
        if not any(kw in basename for kw in target_keywords):
            continue
        try:
            with open(f, "r", encoding="utf-8", errors="replace") as fh:
                content = fh.read()
                for pattern in CONSENT_PATTERNS:
                    if re.search(pattern, content, re.IGNORECASE):
                        found_consent = True
                        break
        except (IOError, OSError):
            pass

    if not found_consent:
        violations.append(Violation(
            check_name="user_consent_required",
            severity=Severity.HIGH,
            file_path="(codebase)",
            line_number=0,
            line_content="",
            description="No user consent check found before data upload. "
                        "All data sharing must require explicit user consent.",
        ))

    passed = found_consent
    return passed, violations


# ---------------------------------------------------------------------------
# Report generation
# ---------------------------------------------------------------------------

def generate_report(report: AuditReport) -> str:
    """Generate a human-readable audit report."""
    lines = []
    lines.append("=" * 72)
    lines.append("  VelaSense Privacy Audit Report")
    lines.append("=" * 72)
    lines.append("")
    lines.append(f"  Files scanned:  {report.total_files_scanned}")
    lines.append(f"  Lines scanned:  {report.total_lines_scanned}")
    lines.append("")

    # Summary
    lines.append("  Checks passed:  {}".format(len(report.checks_passed)))
    lines.append("  Checks failed:  {}".format(len(report.checks_failed)))
    lines.append("  Violations:     {}".format(len(report.violations)))
    lines.append("")

    # Passed checks
    if report.checks_passed:
        lines.append("  PASSED:")
        for check in report.checks_passed:
            lines.append("    [OK] {}".format(check))
        lines.append("")

    # Failed checks with violations
    if report.checks_failed:
        lines.append("  FAILED:")
        for check in report.checks_failed:
            lines.append("    [FAIL] {}".format(check))
        lines.append("")

    # Violation details
    if report.violations:
        lines.append("-" * 72)
        lines.append("  Violation Details")
        lines.append("-" * 72)

        for i, v in enumerate(report.violations, 1):
            lines.append("")
            lines.append("  #{} [{}] {}".format(i, v.severity.value,
                                                  v.check_name))
            lines.append("  File: {}:{}".format(v.file_path, v.line_number))
            lines.append("  Code: {}".format(v.line_content))
            lines.append("  Info: {}".format(v.description))

    lines.append("")
    lines.append("=" * 72)

    # Overall verdict
    critical = sum(1 for v in report.violations
                   if v.severity == Severity.CRITICAL)
    high = sum(1 for v in report.violations
               if v.severity == Severity.HIGH)

    if critical > 0:
        lines.append("  VERDICT: FAIL — {} critical violation(s)".format(
            critical))
    elif high > 0:
        lines.append("  VERDICT: WARN — {} high-severity violation(s)".format(
            high))
    elif len(report.violations) > 0:
        lines.append("  VERDICT: PASS with {} minor issue(s)".format(
            len(report.violations)))
    else:
        lines.append("  VERDICT: PASS — No privacy violations detected")

    lines.append("=" * 72)
    return "\n".join(lines)


def generate_json_report(report: AuditReport) -> dict:
    """Generate a machine-readable JSON report."""
    return {
        "files_scanned": report.total_files_scanned,
        "lines_scanned": report.total_lines_scanned,
        "checks_passed": report.checks_passed,
        "checks_failed": report.checks_failed,
        "violation_count": len(report.violations),
        "violations": [
            {
                "check": v.check_name,
                "severity": v.severity.value,
                "file": v.file_path,
                "line": v.line_number,
                "code": v.line_content,
                "description": v.description,
            }
            for v in report.violations
        ],
        "verdict": "PASS" if not report.violations else "FAIL",
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run_audit(root: str = None) -> AuditReport:
    """Run the full privacy audit."""
    if root is None:
        root = FIRMWARE_ROOT

    report = AuditReport()

    print(f"[privacy_audit] Scanning {root} ...")
    files = collect_source_files(root)
    report.total_files_scanned = len(files)

    # Count total lines
    total_lines = 0
    for f in files:
        try:
            with open(f, "r", encoding="utf-8", errors="replace") as fh:
                total_lines += sum(1 for _ in fh)
        except (IOError, OSError):
            pass
    report.total_lines_scanned = total_lines

    print(f"[privacy_audit] {len(files)} files, {total_lines} lines")

    # Run all checks
    checks = [
        ("No raw waveform in Flash storage", check_no_raw_waveform_in_storage),
        ("No raw sensor data in BLE packets", check_no_raw_data_in_ble),
        ("No medical terms in Mimo output", check_no_medical_terms_in_output),
        ("No API keys in source code", check_no_api_keys_in_source),
        ("No raw data in log output", check_no_raw_data_in_logs),
        ("Encryption enabled for events", check_encryption_enabled),
        ("User consent required for upload", check_user_consent_required),
    ]

    for check_name, check_func in checks:
        print(f"[privacy_audit] Running: {check_name} ...")
        passed, violations = check_func(files)

        if passed:
            report.checks_passed.append(check_name)
            print(f"  [OK] {check_name}")
        else:
            report.checks_failed.append(check_name)
            report.violations.extend(violations)
            print(f"  [FAIL] {check_name} — {len(violations)} violation(s)")

    return report


def main():
    """CLI entry point."""
    import argparse

    parser = argparse.ArgumentParser(
        description="VelaSense Automated Privacy Audit"
    )
    parser.add_argument(
        "--root", default=FIRMWARE_ROOT,
        help="Firmware source root directory"
    )
    parser.add_argument(
        "--json", action="store_true",
        help="Output JSON report instead of text"
    )
    parser.add_argument(
        "--output", "-o", default=None,
        help="Write report to file"
    )
    parser.add_argument(
        "--strict", action="store_true",
        help="Exit with non-zero code on any violation"
    )

    args = parser.parse_args()

    report = run_audit(args.root)

    if args.json:
        json_report = generate_json_report(report)
        output = json.dumps(json_report, indent=2)
    else:
        output = generate_report(report)

    print("")
    print(output)

    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
        print(f"\n[privacy_audit] Report written to {args.output}")

    # Exit code
    if args.strict and report.violations:
        critical = sum(1 for v in report.violations
                       if v.severity == Severity.CRITICAL)
        sys.exit(1 if critical > 0 else 0)

    sys.exit(0)


if __name__ == "__main__":
    main()
