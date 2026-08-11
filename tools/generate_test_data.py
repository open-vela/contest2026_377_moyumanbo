#!/usr/bin/env python3
"""
generate_test_data.py -- Test Dataset Generator for VelaSense DSP Validation

Creates test datasets that exercise the firmware DSP pipeline:
  - PPG peak detection (firmware/libs/dsp/peak_detect.h)
  - PPG bandpass filter (firmware/libs/dsp/ppg_filter.h)
  - HRV analysis (firmware/libs/dsp/hrv.h)
  - Signal quality index (firmware/libs/dsp/ppg_sqi.h)
  - Activity classification (firmware/libs/features/activity_classify.h)
  - SCR event detection (firmware/drivers/ad5940)

For each DSP module, this tool generates:
  1. A CSV input file (synthetic sensor data)
  2. A CSV expected-output file (ground truth)
  3. A JSON metadata file describing the test case

The ground truth is derived from the known signal parameters, so
firmware DSP output can be compared against it automatically.

Usage examples:
  python generate_test_data.py --test peak_detect --hr 72 -o test_hr72/
  python generate_test_data.py --test sqi --snr-levels 5 10 20 50 -o test_sqi/
  python generate_test_data.py --test activity --modes walk run rest -o test_act/
  python generate_test_data.py --test hrv --duration 300 -o test_hrv/
  python generate_test_data.py --test scr --count 10 -o test_scr/
  python generate_test_data.py --all -o test_suite/
"""

import argparse
import csv
import json
import math
import os
import sys
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

# Import the sensor simulators
from simulate_sensors import (
    PPGConfig, IMUConfig, EDAConfig, TempConfig,
    generate_ppg, generate_imu, generate_eda, generate_temperature,
    PPG_SAMPLE_RATE, IMU_SAMPLE_RATE, EDA_SAMPLE_RATE
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def ensure_dir(path: str) -> str:
    """Create directory if it doesn't exist."""
    os.makedirs(path, exist_ok=True)
    return path


def write_csv(filepath: str, headers: List[str],
              rows: List[List[Any]]) -> None:
    """Write a generic CSV file."""
    with open(filepath, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(headers)
        for row in rows:
            writer.writerow(row)


def write_json(filepath: str, data: Dict) -> None:
    """Write a JSON metadata file."""
    with open(filepath, 'w') as f:
        json.dump(data, f, indent=2)


def find_peaks_simple(signal: np.ndarray, min_distance: int,
                      threshold_factor: float = 0.4) -> np.ndarray:
    """
    Simple peak detection for ground truth generation.

    Mirrors the firmware peak_detect algorithm:
    1. Compute running mean of peaks/troughs
    2. Adaptive threshold between them
    3. Find zero-crossing of derivative (descending)

    Returns array of peak indices.
    """
    peaks = []
    if len(signal) < 3:
        return np.array(peaks, dtype=int)

    # Compute running peak/trough means
    running_peak_mean = np.max(signal[:50]) if len(signal) >= 50 else np.max(signal)
    running_trough_mean = np.min(signal[:50]) if len(signal) >= 50 else np.min(signal)
    threshold = running_trough_mean + threshold_factor * (running_peak_mean - running_trough_mean)

    prev = signal[0]
    rising = True
    candidate_idx = 0
    candidate_val = signal[0]

    for i in range(1, len(signal)):
        if signal[i] > prev:
            if not rising:
                rising = True
                candidate_idx = i
                candidate_val = signal[i]
            elif signal[i] > candidate_val:
                candidate_idx = i
                candidate_val = signal[i]
        elif signal[i] < prev and rising:
            if candidate_val > threshold and (not peaks or
                    (i - peaks[-1]) >= min_distance):
                peaks.append(candidate_idx)
                # Update adaptive threshold
                running_peak_mean = 0.8 * running_peak_mean + 0.2 * candidate_val
                threshold = running_trough_mean + threshold_factor * (
                    running_peak_mean - running_trough_mean)
            rising = False
            if signal[i] < running_trough_mean:
                running_trough_mean = 0.8 * running_trough_mean + 0.2 * signal[i]
                threshold = running_trough_mean + threshold_factor * (
                    running_peak_mean - running_trough_mean)

        prev = signal[i]

    return np.array(peaks, dtype=int)


def bandpass_filter_simple(data: np.ndarray, fs: float,
                           low: float = 0.5, high: float = 5.0,
                           order: int = 4) -> np.ndarray:
    """
    Simple Butterworth bandpass filter for ground truth.

    Uses scipy if available, otherwise a naive FFT-based filter.
    """
    try:
        from scipy.signal import butter, filtfilt
        nyq = 0.5 * fs
        b, a = butter(order, [low / nyq, high / nyq], btype='band')
        return filtfilt(b, a, data)
    except ImportError:
        # Fallback: FFT-based bandpass
        n = len(data)
        freqs = np.fft.rfftfreq(n, d=1.0 / fs)
        fft_data = np.fft.rfft(data)
        mask = (freqs >= low) & (freqs <= high)
        fft_filtered = np.zeros_like(fft_data)
        fft_filtered[mask] = fft_data[mask]
        return np.fft.irfft(fft_filtered, n=n)


# ---------------------------------------------------------------------------
# Test: Peak Detection Validation
# ---------------------------------------------------------------------------

def generate_peak_detect_test(output_dir: str, heart_rates: List[float],
                              duration: float, seed: int) -> List[str]:
    """
    Generate test data for PPG peak detection validation.

    For each heart rate, generates:
      - Input PPG CSV (raw + filtered)
      - Expected peaks CSV (sample indices, timestamps, IBIs)
      - Metadata JSON with expected HR, IBI stats

    Returns list of generated file paths.
    """
    ensure_dir(output_dir)
    files = []

    for hr in heart_rates:
        tag = f'hr{int(hr)}'
        cfg = PPGConfig(
            heart_rate_bpm=hr,
            duration_s=duration,
            motion_noise_std=0.02,
            seed=seed
        )
        t, green, red, ir = generate_ppg(cfg)
        n = len(t)

        # Bandpass filter the green channel (0.5 - 5 Hz)
        green_float = green.astype(float)
        green_filtered = bandpass_filter_simple(
            green_float, PPG_SAMPLE_RATE, 0.5, 5.0
        )

        # Find peaks in filtered signal
        min_distance = int(PPG_SAMPLE_RATE * 60.0 / 200.0)  # Max 200 bpm
        peaks = find_peaks_simple(green_filtered, min_distance, 0.35)

        # Write input PPG CSV
        input_path = os.path.join(output_dir, f'{tag}_ppg_input.csv')
        with open(input_path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['timestamp', 'ppg_green', 'ppg_red', 'ppg_ir'])
            for i in range(n):
                writer.writerow([f'{t[i]:.4f}', f'{green[i]:.0f}',
                                 f'{red[i]:.0f}', f'{ir[i]:.0f}'])
        files.append(input_path)

        # Write expected peaks CSV
        peaks_path = os.path.join(output_dir, f'{tag}_expected_peaks.csv')
        peak_rows = []
        prev_idx = None
        for idx in peaks:
            ibi_ms = ((idx - prev_idx) / PPG_SAMPLE_RATE * 1000.0
                      if prev_idx is not None else 0.0)
            peak_rows.append([
                idx, f'{t[idx]:.4f}', f'{green_filtered[idx]:.2f}',
                f'{ibi_ms:.2f}', '0x01'  # PEAK_QUALITY_VALID
            ])
            prev_idx = idx
        write_csv(peaks_path,
                  ['sample_index', 'timestamp', 'amplitude', 'ibi_ms', 'quality'],
                  peak_rows)
        files.append(peaks_path)

        # Compute statistics
        ibis = []
        for i in range(1, len(peaks)):
            ibi = (peaks[i] - peaks[i - 1]) / PPG_SAMPLE_RATE * 1000.0
            ibis.append(ibi)
        ibis_arr = np.array(ibis) if ibis else np.array([0.0])

        # Write metadata
        meta = {
            'test': 'peak_detect',
            'heart_rate_bpm': hr,
            'duration_s': duration,
            'sample_rate': PPG_SAMPLE_RATE,
            'num_peaks': len(peaks),
            'expected_hr_bpm': float(np.mean(60000.0 / ibis_arr)) if ibis else hr,
            'ibi_mean_ms': float(np.mean(ibis_arr)),
            'ibi_std_ms': float(np.std(ibis_arr)),
            'ibi_min_ms': float(np.min(ibis_arr)),
            'ibi_max_ms': float(np.max(ibis_arr)),
            'signal_snr_db': 20.0,  # Approximate for synthetic data
            'files': {'input': os.path.basename(input_path),
                      'expected': os.path.basename(peaks_path)},
        }
        meta_path = os.path.join(output_dir, f'{tag}_meta.json')
        write_json(meta_path, meta)
        files.append(meta_path)

    return files


# ---------------------------------------------------------------------------
# Test: Signal Quality Index (SQI) Validation
# ---------------------------------------------------------------------------

def generate_sqi_test(output_dir: str, snr_levels: List[float],
                      duration: float, seed: int) -> List[str]:
    """
    Generate test data for SQI validation at different SNR levels.

    Higher SNR should produce higher SQI values.
    """
    ensure_dir(output_dir)
    files = []

    for snr_db in snr_levels:
        tag = f'snr{int(snr_db)}db'
        # Convert SNR in dB to noise std
        # SNR = 20*log10(signal_amp / noise_std)
        signal_amp = 50000.0  # Typical PPG AC amplitude in counts
        noise_std = signal_amp / (10 ** (snr_db / 20.0))
        ambient_noise = noise_std / PPG_SAMPLE_RATE  # Scale to per-sample

        cfg = PPGConfig(
            heart_rate_bpm=72.0,
            duration_s=duration,
            ambient_noise_std=ambient_noise / 3000.0,  # Scale to normalized noise
            motion_noise_std=0.0 if snr_db > 15 else 0.1,
            seed=seed
        )
        t, green, red, ir = generate_ppg(cfg)
        n = len(t)

        # Write PPG data
        ppg_path = os.path.join(output_dir, f'{tag}_ppg.csv')
        with open(ppg_path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['timestamp', 'ppg_green', 'ppg_red', 'ppg_ir'])
            for i in range(n):
                writer.writerow([f'{t[i]:.4f}', f'{green[i]:.0f}',
                                 f'{red[i]:.0f}', f'{ir[i]:.0f}'])
        files.append(ppg_path)

        # Compute approximate expected SQI
        # SQI based on coefficient of variation of AC component
        green_float = green.astype(float)
        green_filtered = bandpass_filter_simple(green_float, PPG_SAMPLE_RATE, 0.5, 5.0)
        cv = np.std(green_filtered) / (np.mean(np.abs(green_filtered)) + 1e-10)
        expected_sqi = max(0, min(100, 100 * (1.0 / (1.0 + cv * 5.0))))

        meta = {
            'test': 'sqi',
            'snr_db': snr_db,
            'duration_s': duration,
            'sample_rate': PPG_SAMPLE_RATE,
            'expected_sqi_approx': round(expected_sqi, 1),
            'signal_noise_ratio': f'{snr_db:.0f} dB',
            'motion_present': snr_db <= 15,
            'file': os.path.basename(ppg_path),
        }
        meta_path = os.path.join(output_dir, f'{tag}_meta.json')
        write_json(meta_path, meta)
        files.append(meta_path)

    return files


# ---------------------------------------------------------------------------
# Test: Activity Classification Validation
# ---------------------------------------------------------------------------

def generate_activity_test(output_dir: str, modes: List[str],
                           duration: float, seed: int) -> List[str]:
    """
    Generate IMU test data for activity classification validation.

    Expected classifications:
      rest -> ACTIVITY_REST (0)
      walk -> ACTIVITY_WALK (1)
      run  -> ACTIVITY_RUN (3)
    """
    ensure_dir(output_dir)
    files = []

    activity_map = {
        'rest': {'level': 0, 'name': 'ACTIVITY_REST'},
        'walk': {'level': 1, 'name': 'ACTIVITY_WALK'},
        'run': {'level': 3, 'name': 'ACTIVITY_RUN'},
        'arm_swing': {'level': 0, 'name': 'ACTIVITY_REST'},  # Looks like rest
    }

    for mode in modes:
        cfg = IMUConfig(mode=mode, duration_s=duration, seed=seed)
        t, ax, ay, az, gx, gy, gz = generate_imu(cfg)
        n = len(t)

        # Write IMU data
        imu_path = os.path.join(output_dir, f'{mode}_imu.csv')
        with open(imu_path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['timestamp', 'ax', 'ay', 'az', 'gx', 'gy', 'gz'])
            for i in range(n):
                writer.writerow([
                    f'{t[i]:.4f}',
                    f'{ax[i]:.4f}', f'{ay[i]:.4f}', f'{az[i]:.4f}',
                    f'{gx[i]:.4f}', f'{gy[i]:.4f}', f'{gz[i]:.4f}'
                ])
        files.append(imu_path)

        # Compute magnitude statistics
        magnitude = np.sqrt(ax**2 + ay**2 + az**2) / 9.80665  # in g units
        mean_mag = float(np.mean(magnitude))
        peak_mag = float(np.max(magnitude))

        expected = activity_map.get(mode, activity_map['rest'])

        meta = {
            'test': 'activity_classify',
            'mode': mode,
            'expected_activity_level': expected['level'],
            'expected_activity_name': expected['name'],
            'duration_s': duration,
            'sample_rate': IMU_SAMPLE_RATE,
            'mean_accel_magnitude_g': round(mean_mag, 4),
            'peak_accel_magnitude_g': round(peak_mag, 4),
            'file': os.path.basename(imu_path),
        }
        meta_path = os.path.join(output_dir, f'{mode}_meta.json')
        write_json(meta_path, meta)
        files.append(meta_path)

    return files


# ---------------------------------------------------------------------------
# Test: HRV Analysis Validation
# ---------------------------------------------------------------------------

def generate_hrv_test(output_dir: str, duration: float,
                      seed: int) -> List[str]:
    """
    Generate test data for HRV analysis.

    Creates PPG with known HRV characteristics:
    - Constant HR (low HRV baseline)
    - Adding RSA modulation (known LF/HF ratio)
    """
    ensure_dir(output_dir)
    files = []

    # Test 1: Constant HR (very low HRV)
    cfg_const = PPGConfig(
        heart_rate_bpm=72.0,
        duration_s=duration,
        rsa_amplitude=0.0,  # No RSA = no HRV modulation
        motion_noise_std=0.01,
        seed=seed
    )
    t, g, r, ir = generate_ppg(cfg_const)
    g_filt = bandpass_filter_simple(g.astype(float), PPG_SAMPLE_RATE, 0.5, 5.0)
    peaks_const = find_peaks_simple(g_filt, int(PPG_SAMPLE_RATE * 0.3))

    # Compute IBIs
    ibis_const = []
    for i in range(1, len(peaks_const)):
        ibi = (peaks_const[i] - peaks_const[i - 1]) / PPG_SAMPLE_RATE * 1000.0
        ibis_const.append(ibi)

    # Write constant HR data
    const_path = os.path.join(output_dir, 'hrv_constant_ppg.csv')
    with open(const_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'ppg_green', 'ppg_red', 'ppg_ir'])
        for i in range(len(t)):
            writer.writerow([f'{t[i]:.4f}', f'{g[i]:.0f}',
                             f'{r[i]:.0f}', f'{ir[i]:.0f}'])
    files.append(const_path)

    ibis_arr = np.array(ibis_const) if ibis_const else np.array([0.0])
    const_meta = {
        'test': 'hrv_constant',
        'heart_rate_bpm': 72.0,
        'duration_s': duration,
        'num_peaks': len(peaks_const),
        'ibi_mean_ms': round(float(np.mean(ibis_arr)), 2),
        'ibi_sdnn_ms': round(float(np.std(ibis_arr)), 2),
        'expected_sdnn_low': True,  # SDNN should be very low
        'file': os.path.basename(const_path),
    }
    write_json(os.path.join(output_dir, 'hrv_constant_meta.json'), const_meta)
    files.append(os.path.join(output_dir, 'hrv_constant_meta.json'))

    # Test 2: High RSA modulation (high HRV)
    cfg_rsa = PPGConfig(
        heart_rate_bpm=72.0,
        duration_s=duration,
        rsa_amplitude=0.25,  # Strong RSA modulation
        respiratory_rate_hz=0.25,  # 15 breaths/min
        motion_noise_std=0.01,
        seed=seed + 1
    )
    t2, g2, r2, ir2 = generate_ppg(cfg_rsa)
    g2_filt = bandpass_filter_simple(g2.astype(float), PPG_SAMPLE_RATE, 0.5, 5.0)
    peaks_rsa = find_peaks_simple(g2_filt, int(PPG_SAMPLE_RATE * 0.3))

    ibis_rsa = []
    for i in range(1, len(peaks_rsa)):
        ibi = (peaks_rsa[i] - peaks_rsa[i - 1]) / PPG_SAMPLE_RATE * 1000.0
        ibis_rsa.append(ibi)

    rsa_path = os.path.join(output_dir, 'hrv_rsa_ppg.csv')
    with open(rsa_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'ppg_green', 'ppg_red', 'ppg_ir'])
        for i in range(len(t2)):
            writer.writerow([f'{t2[i]:.4f}', f'{g2[i]:.0f}',
                             f'{r2[i]:.0f}', f'{ir2[i]:.0f}'])
    files.append(rsa_path)

    ibis_rsa_arr = np.array(ibis_rsa) if ibis_rsa else np.array([0.0])
    rsa_meta = {
        'test': 'hrv_rsa',
        'heart_rate_bpm': 72.0,
        'rsa_amplitude': 0.25,
        'respiratory_rate_hz': 0.25,
        'duration_s': duration,
        'num_peaks': len(peaks_rsa),
        'ibi_mean_ms': round(float(np.mean(ibis_rsa_arr)), 2),
        'ibi_sdnn_ms': round(float(np.std(ibis_rsa_arr)), 2),
        'expected_sdnn_high': True,  # SDNN should be higher with RSA
        'file': os.path.basename(rsa_path),
    }
    write_json(os.path.join(output_dir, 'hrv_rsa_meta.json'), rsa_meta)
    files.append(os.path.join(output_dir, 'hrv_rsa_meta.json'))

    return files


# ---------------------------------------------------------------------------
# Test: SCR Event Detection Validation
# ---------------------------------------------------------------------------

def generate_scr_test(output_dir: str, count: int, duration: float,
                      seed: int) -> List[str]:
    """
    Generate EDA test data for SCR event detection validation.

    Creates data with known SCR event times and amplitudes.
    """
    ensure_dir(output_dir)
    files = []

    cfg = EDAConfig(
        baseline_scl_us=5.0,
        duration_s=duration,
        scr_count=count,
        scr_amplitude_us=1.0,
        scr_rise_time_s=2.0,
        scr_recovery_s=8.0,
        noise_std_us=0.02,
        seed=seed
    )
    t, scl, scr_flag = generate_eda(cfg)
    n = len(t)

    # Write EDA data
    eda_path = os.path.join(output_dir, 'scr_eda.csv')
    with open(eda_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'scl', 'scr_flag'])
        for i in range(n):
            writer.writerow([f'{t[i]:.4f}', f'{scl[i]:.4f}', int(scr_flag[i])])
    files.append(eda_path)

    # Find SCR event boundaries
    events = []
    in_event = False
    event_start = 0
    for i in range(n):
        if scr_flag[i] and not in_event:
            in_event = True
            event_start = i
        elif not scr_flag[i] and in_event:
            in_event = False
            peak_idx = event_start + np.argmax(scl[event_start:i])
            events.append({
                'onset_index': int(event_start),
                'onset_time': round(float(t[event_start]), 4),
                'peak_index': int(peak_idx),
                'peak_time': round(float(t[peak_idx]), 4),
                'peak_scl': round(float(scl[peak_idx]), 4),
                'amplitude_us': round(float(scl[peak_idx] - scl[event_start]), 4),
                'duration_s': round(float(t[i] - t[event_start]), 2),
            })
    if in_event:
        peak_idx = event_start + np.argmax(scl[event_start:])
        events.append({
            'onset_index': int(event_start),
            'onset_time': round(float(t[event_start]), 4),
            'peak_index': int(peak_idx),
            'peak_time': round(float(t[peak_idx]), 4),
            'peak_scl': round(float(scl[peak_idx]), 4),
            'amplitude_us': round(float(scl[peak_idx] - scl[event_start]), 4),
            'duration_s': round(float(t[-1] - t[event_start]), 2),
        })

    meta = {
        'test': 'scr_detection',
        'duration_s': duration,
        'sample_rate': EDA_SAMPLE_RATE,
        'baseline_scl_us': cfg.baseline_scl_us,
        'expected_num_events': count,
        'detected_events': len(events),
        'events': events,
        'file': os.path.basename(eda_path),
    }
    meta_path = os.path.join(output_dir, 'scr_meta.json')
    write_json(meta_path, meta)
    files.append(meta_path)

    return files


# ---------------------------------------------------------------------------
# Generate complete test suite
# ---------------------------------------------------------------------------

def generate_test_suite(output_dir: str, seed: int) -> List[str]:
    """Generate a complete test suite covering all DSP modules."""
    ensure_dir(output_dir)
    all_files = []

    print('Generating peak detection tests...')
    all_files.extend(generate_peak_detect_test(
        os.path.join(output_dir, 'peak_detect'),
        heart_rates=[50, 72, 100, 140, 180],
        duration=30.0, seed=seed
    ))

    print('Generating SQI tests...')
    all_files.extend(generate_sqi_test(
        os.path.join(output_dir, 'sqi'),
        snr_levels=[5, 10, 20, 40],
        duration=20.0, seed=seed
    ))

    print('Generating activity classification tests...')
    all_files.extend(generate_activity_test(
        os.path.join(output_dir, 'activity'),
        modes=['rest', 'walk', 'run'],
        duration=10.0, seed=seed
    ))

    print('Generating HRV tests...')
    all_files.extend(generate_hrv_test(
        os.path.join(output_dir, 'hrv'),
        duration=120.0, seed=seed
    ))

    print('Generating SCR detection tests...')
    all_files.extend(generate_scr_test(
        os.path.join(output_dir, 'scr'),
        count=8, duration=120.0, seed=seed
    ))

    # Generate suite index
    suite_meta = {
        'test_suite': 'velasense_dsp_validation',
        'version': '1.0.0',
        'seed': seed,
        'modules_tested': [
            'peak_detect', 'sqi', 'activity_classify', 'hrv', 'scr_detection'
        ],
        'total_files': len(all_files),
        'files': [os.path.basename(f) for f in all_files],
    }
    index_path = os.path.join(output_dir, 'test_suite_index.json')
    write_json(index_path, suite_meta)
    all_files.append(index_path)

    return all_files


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description='VelaSense DSP Test Data Generator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        '--test',
        choices=['peak_detect', 'sqi', 'activity', 'hrv', 'scr'],
        help='Specific DSP module to generate test data for'
    )
    parser.add_argument(
        '--all', action='store_true',
        help='Generate complete test suite for all DSP modules'
    )
    parser.add_argument(
        '-o', '--output', default='test_data',
        help='Output directory (default: test_data)'
    )
    parser.add_argument(
        '--hr', type=float, nargs='+', default=[60, 72, 100, 140],
        help='Heart rates for peak_detect tests (default: 60 72 100 140)'
    )
    parser.add_argument(
        '--snr-levels', type=float, nargs='+', default=[5, 10, 20, 40],
        help='SNR levels in dB for SQI tests (default: 5 10 20 40)'
    )
    parser.add_argument(
        '--modes', nargs='+',
        choices=['rest', 'walk', 'run', 'arm_swing'],
        default=['rest', 'walk', 'run'],
        help='Activity modes for classification tests'
    )
    parser.add_argument(
        '--count', type=int, default=8,
        help='Number of SCR events (default: 8)'
    )
    parser.add_argument(
        '--duration', type=float, default=60.0,
        help='Duration in seconds (default: 60)'
    )
    parser.add_argument(
        '--seed', type=int, default=42,
        help='Random seed for reproducibility (default: 42)'
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if not args.test and not args.all:
        parser.print_help()
        return 1

    if args.all:
        files = generate_test_suite(args.output, args.seed)
        print(f'\nGenerated {len(files)} files in {args.output}/')
        return 0

    ensure_dir(args.output)

    if args.test == 'peak_detect':
        files = generate_peak_detect_test(
            args.output, args.hr, args.duration, args.seed
        )
    elif args.test == 'sqi':
        files = generate_sqi_test(
            args.output, args.snr_levels, args.duration, args.seed
        )
    elif args.test == 'activity':
        files = generate_activity_test(
            args.output, args.modes, args.duration, args.seed
        )
    elif args.test == 'hrv':
        files = generate_hrv_test(args.output, args.duration, args.seed)
    elif args.test == 'scr':
        files = generate_scr_test(
            args.output, args.count, args.duration, args.seed
        )
    else:
        parser.print_help()
        return 1

    print(f'Generated {len(files)} files in {args.output}/')
    for f in files:
        print(f'  {f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
