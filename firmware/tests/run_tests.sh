#!/bin/bash
#
# VelaSense Test Runner
#
# Runs all unit tests, integration tests, and the privacy audit.
#
# Usage:
#   ./run_tests.sh              # Run all tests
#   ./run_tests.sh --unit       # Unit tests only
#   ./run_tests.sh --privacy    # Privacy audit only
#   ./run_tests.sh --integration # Integration tests only
#   ./run_tests.sh --verbose    # Verbose output
#

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
RESULTS_DIR="${SCRIPT_DIR}/results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test results
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------

log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $*"
    ((PASS_COUNT++))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $*"
    ((FAIL_COUNT++))
}

log_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $*"
    ((SKIP_COUNT++))
}

log_header() {
    echo ""
    echo "================================================================"
    echo "  $*"
    echo "================================================================"
}

# ---------------------------------------------------------------------------
# Check dependencies
# ---------------------------------------------------------------------------

check_dependencies() {
    log_info "Checking dependencies..."

    local missing=0

    if ! command -v cmake &> /dev/null; then
        echo "  ERROR: cmake not found"
        ((missing++))
    fi

    if ! command -v make &> /dev/null; then
        echo "  ERROR: make not found"
        ((missing++))
    fi

    if ! command -v python3 &> /dev/null; then
        echo "  WARNING: python3 not found (privacy audit will be skipped)"
    fi

    # Check for CMocka
    if ! pkg-config --exists cmocka 2>/dev/null; then
        if [ ! -f /usr/lib/libcmocka.so ] && \
           [ ! -f /usr/lib/x86_64-linux-gnu/libcmocka.so ] && \
           [ ! -f /usr/local/lib/libcmocka.so ]; then
            echo "  WARNING: CMocka not found. Attempting to install..."
            install_cmocka
        fi
    fi

    if [ $missing -gt 0 ]; then
        echo ""
        echo "  Please install missing dependencies and try again."
        exit 1
    fi

    log_info "All dependencies satisfied."
}

install_cmocka() {
    if command -v apt-get &> /dev/null; then
        sudo apt-get update -qq && sudo apt-get install -y -qq libcmocka-dev 2>/dev/null || true
    elif command -v brew &> /dev/null; then
        brew install cmocka 2>/dev/null || true
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --noconfirm cmocka 2>/dev/null || true
    else
        echo "  Cannot auto-install CMocka. Please install manually."
        echo "  Ubuntu/Debian: sudo apt-get install libcmocka-dev"
        echo "  macOS:         brew install cmocka"
    fi
}

# ---------------------------------------------------------------------------
# Build tests
# ---------------------------------------------------------------------------

build_tests() {
    log_header "Building Tests"

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    log_info "Running CMake..."
    cmake "${SCRIPT_DIR}" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -5

    log_info "Building..."
    make -j$(nproc) 2>&1 | tail -10

    if [ $? -eq 0 ]; then
        log_pass "Build completed successfully"
    else
        log_fail "Build failed"
        exit 1
    fi

    cd "${SCRIPT_DIR}"
}

# ---------------------------------------------------------------------------
# Run unit tests
# ---------------------------------------------------------------------------

run_unit_tests() {
    log_header "Unit Tests"

    mkdir -p "${RESULTS_DIR}"

    # DSP tests
    log_info "Running DSP tests..."
    if [ -f "${BUILD_DIR}/test_dsp" ]; then
        if "${BUILD_DIR}/test_dsp" 2>&1; then
            log_pass "DSP tests"
        else
            log_fail "DSP tests"
        fi
    else
        log_skip "DSP tests (binary not found)"
    fi

    # Feature extraction tests
    log_info "Running feature extraction tests..."
    if [ -f "${BUILD_DIR}/test_features" ]; then
        if "${BUILD_DIR}/test_features" 2>&1; then
            log_pass "Feature extraction tests"
        else
            log_fail "Feature extraction tests"
        fi
    else
        log_skip "Feature extraction tests (binary not found)"
    fi

    # Event state machine tests
    log_info "Running event state machine tests..."
    if [ -f "${BUILD_DIR}/test_event_sm" ]; then
        if "${BUILD_DIR}/test_event_sm" 2>&1; then
            log_pass "Event state machine tests"
        else
            log_fail "Event state machine tests"
        fi
    else
        log_skip "Event state machine tests (binary not found)"
    fi

    # Privacy tests
    log_info "Running privacy compliance tests..."
    if [ -f "${BUILD_DIR}/test_privacy" ]; then
        if "${BUILD_DIR}/test_privacy" 2>&1; then
            log_pass "Privacy compliance tests"
        else
            log_fail "Privacy compliance tests"
        fi
    else
        log_skip "Privacy compliance tests (binary not found)"
    fi
}

# ---------------------------------------------------------------------------
# Run integration tests
# ---------------------------------------------------------------------------

run_integration_tests() {
    log_header "Integration Tests"

    log_info "Running integration test scenarios..."
    if [ -f "${BUILD_DIR}/test_integration" ]; then
        if "${BUILD_DIR}/test_integration" 2>&1; then
            log_pass "Integration tests"
        else
            log_fail "Integration tests"
        fi
    else
        log_skip "Integration tests (binary not found)"
    fi
}

# ---------------------------------------------------------------------------
# Run privacy audit
# ---------------------------------------------------------------------------

run_privacy_audit() {
    log_header "Privacy Audit"

    if command -v python3 &> /dev/null; then
        log_info "Running automated privacy audit..."

        mkdir -p "${RESULTS_DIR}"

        if python3 "${SCRIPT_DIR}/privacy_audit.py" \
            --root "$(dirname "${SCRIPT_DIR}")" \
            --output "${RESULTS_DIR}/privacy_audit_report.txt" \
            2>&1; then
            log_pass "Privacy audit completed"
        else
            log_fail "Privacy audit found violations"
        fi

        # Also generate JSON report
        python3 "${SCRIPT_DIR}/privacy_audit.py" \
            --root "$(dirname "${SCRIPT_DIR}")" \
            --json \
            --output "${RESULTS_DIR}/privacy_audit_report.json" \
            2>/dev/null || true
    else
        log_skip "Privacy audit (python3 not available)"
    fi
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

print_summary() {
    log_header "Test Summary"

    local total=$((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))

    echo ""
    echo "  Total:  ${total}"
    echo -e "  Passed: ${GREEN}${PASS_COUNT}${NC}"
    echo -e "  Failed: ${RED}${FAIL_COUNT}${NC}"
    echo -e "  Skipped: ${YELLOW}${SKIP_COUNT}${NC}"
    echo ""

    if [ $FAIL_COUNT -gt 0 ]; then
        echo -e "  ${RED}RESULT: FAILED${NC}"
        echo ""
        return 1
    else
        echo -e "  ${GREEN}RESULT: PASSED${NC}"
        echo ""
        return 0
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

main() {
    local run_unit=0
    local run_integ=0
    local run_priv=0
    local run_all=1
    local verbose=0

    # Parse arguments

    for arg in "$@"; do
        case $arg in
            --unit)
                run_unit=1
                run_all=0
                ;;
            --integration)
                run_integ=1
                run_all=0
                ;;
            --privacy)
                run_priv=1
                run_all=0
                ;;
            --verbose)
                verbose=1
                ;;
            --help|-h)
                echo "Usage: $0 [OPTIONS]"
                echo ""
                echo "Options:"
                echo "  --unit          Run unit tests only"
                echo "  --integration   Run integration tests only"
                echo "  --privacy       Run privacy audit only"
                echo "  --verbose       Verbose output"
                echo "  --help          Show this help"
                echo ""
                echo "With no options, all tests are run."
                exit 0
                ;;
            *)
                echo "Unknown option: $arg"
                exit 1
                ;;
        esac
    done

    log_header "VelaSense Test Suite"

    check_dependencies

    build_tests

    if [ $run_all -eq 1 ] || [ $run_unit -eq 1 ]; then
        run_unit_tests
    fi

    if [ $run_all -eq 1 ] || [ $run_integ -eq 1 ]; then
        run_integration_tests
    fi

    if [ $run_all -eq 1 ] || [ $run_priv -eq 1 ]; then
        run_privacy_audit
    fi

    print_summary
}

main "$@"
