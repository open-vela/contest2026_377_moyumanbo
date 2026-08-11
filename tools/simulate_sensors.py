#!/usr/bin/env python3
"""
simulate_sensors.py -- Synthetic Sensor Data Generator for VelaSense

Generates realistic synthetic data streams matching the VelaSense wearable
hardware sensor suite:

  - PPG:   MAX86141 3-wavelength PPG (green/red/IR) at 100 Hz
  - IMU:   ICM-42688-P 6-axis accel+gyro at 100 Hz
  - EDA:   AD5940 electrodermal activity at 32 Hz
  - Temp:  MAX30208 skin temperature at 1 Hz

Each sensor model includes realistic noise, artifacts, and physiological
modulations so that downstream DSP and ML pipelines can be validated
without access to real hardware.

Usage examples:
  python simulate_sensors.py --sensor ppg --duration 60 --hr 72 -o ppg.csv
  python simulate_sensors.py --sensor imu --mode walk --duration 30 -o imu.csv
  python simulate_sensors.py --sensor eda --duration 120 -o eda.csv
  python simulate_sensors.py --sensor temp --duration 300 -o temp.csv
  python simulate_sensors.py --all --duration 60 --prefix sim_

Output CSV files use the same column names and sample rates as the real
firmware uORB topics, so they can be fed directly into DSP test harnesses.
"""

import argparse
import csv
import math
import os
import sys
import time
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import numpy as np


# ---------------------------------------------------------------------------
# Constants matching VelaSense firmware definitions
# ---------------------------------------------------------------------------

PPG_SAMPLE_RATE = 100       # Hz (MAX86141_SR_100HZ)
IMU_SAMPLE_RATE = 100       # Hz (ICM42688 ODR 100 Hz)
EDA_SAMPLE_RATE = 32        # Hz (AD5940_EDA_SAMPLE_RATE_HZ)
TEMP_SAMPLE_RATE = 1        # Hz (MAX30208 one-shot)


# ---------------------------------------------------------------------------
# PPG synthetic waveform generator
# ---------------------------------------------------------------------------

@dataclass
class PPGConfig:
    """Configuration for synthetic PPG generation."""
    heart_rate_bpm: float = 72.0        # Heart rate (40-180 bpm)
    sample_rate: int = PPG_SAMPLE_RATE  # Samples per second
    duration_s: float = 60.0            # Recording duration
    rsa_amplitude: float = 0.15         # Respiratory sinus arrhythmia depth
    respiratory_rate_hz: float = 0.25   # Breathing rate (~15 breaths/min)
    dicrotic_notch_depth: float = 0.12  # Dicrotic notch relative amplitude
    motion_noise_std: float = 0.0       # Motion artifact magnitude (0-1)
    ambient_noise_std: float = 0.02     # Ambient light noise
    baseline_wander_amp: float = 0.05   # Low-frequency baseline drift
    dc_offset_green: float = 200000.0   # Typical raw ADC DC level (green)
    dc_offset_red: float = 180000.0     # Typical raw ADC DC level (red)
    dc_offset_ir: float = 160000.0      # Typical raw ADC DC level (ir)
    seed: Optional[int] = None


def _ppg_cardiac_waveform(t: np.ndarray, period: float,
                          dicrotic_depth: float) -> np.ndarray:
    """
    Generate one cardiac cycle waveform with systolic peak and dicrotic notch.

    The waveform is a superposition of two Gaussian pulses:
      1. Systolic peak (larger, narrower)
      2. Dicrotic notch / reflected wave (smaller, wider, delayed)

    Args:
        t: Time array within one cardiac cycle (0 to period).
        period: Duration of one heartbeat in seconds.
        dicrotic_depth: Relative amplitude of dicrotic notch (0-1).

    Returns:
        Normalized PPG waveform values in [0, 1].
    """
    # Systolic peak at ~30% of the cycle
    systolic_center = 0.30 * period
    systolic_width = 0.06 * period
    systolic = np.exp(-0.5 * ((t - systolic_center) / systolic_width) ** 2)

    # Dicrotic notch at ~55% of the cycle
    dicrotic_center = 0.55 * period
    dicrotic_width = 0.10 * period
    dicrotic = dicrotic_depth * np.exp(
        -0.5 * ((t - dicrotic_center) / dicrotic_width) ** 2
    )

    waveform = systolic + dicrotic
    return waveform / np.max(waveform)  # Normalize to [0, 1]


def generate_ppg(cfg: PPGConfig) -> Tuple[np.ndarray, np.ndarray,
                                           np.ndarray, np.ndarray]:
    """
    Generate synthetic 3-channel PPG data (green, red, IR).

    The signal model:
      - Base cardiac waveform at the configured heart rate
      - Respiratory sinus arrhythmia (RSA): HR modulation by breathing
      - Dicrotic notch in each cardiac cycle
      - Baseline wander (low-frequency drift)
      - Motion artifact (broadband noise correlated across channels)
      - Ambient light noise (independent per channel)

    Returns:
        Tuple of (timestamps, ppg_green, ppg_red, ppg_ir) arrays.
        Green/red/IR are in raw ADC counts (20-bit range, ~MAX86141 style).
    """
    rng = np.random.default_rng(cfg.seed)
    n_samples = int(cfg.sample_rate * cfg.duration_s)
    t = np.arange(n_samples) / cfg.sample_rate  # Time in seconds

    # Heart period with RSA modulation
    base_period = 60.0 / cfg.heart_rate_bpm
    rsa = cfg.rsa_amplitude * np.sin(2.0 * math.pi * cfg.respiratory_rate_hz * t)
    instantaneous_hr = cfg.heart_rate_bpm * (1.0 + rsa)
    instantaneous_period = 60.0 / instantaneous_hr

    # Build PPG by accumulating cardiac cycles
    ppg_base = np.zeros(n_samples)
    phase_accum = 0.0
    cycle_time = 0.0
    current_period = instantaneous_period[0]

    for i in range(n_samples):
        # Update cycle period at each sample (tracks RSA)
        current_period = instantaneous_period[i]
        cycle_time = phase_accum

        # Generate waveform value at current position in cardiac cycle
        ppg_base[i] = _ppg_cardiac_waveform(
            np.array([cycle_time]), current_period, cfg.dicrotic_notch_depth
        )[0]

        # Advance phase
        dt = 1.0 / cfg.sample_rate
        phase_accum += dt
        if phase_accum >= current_period:
            phase_accum -= current_period

    # Baseline wander (0.05 - 0.5 Hz)
    baseline = cfg.baseline_wander_amp * (
        0.5 * np.sin(2 * math.pi * 0.1 * t) +
        0.3 * np.sin(2 * math.pi * 0.25 * t) +
        0.2 * np.sin(2 * math.pi * 0.05 * t)
    )

    # Motion artifact (simulates arm movement)
    if cfg.motion_noise_std > 0:
        # Low-pass filtered random walk for realistic motion
        motion_raw = rng.normal(0, cfg.motion_noise_std, n_samples)
        # Simple 10-sample moving average
        kernel = np.ones(10) / 10.0
        motion = np.convolve(motion_raw, kernel, mode='same')
    else:
        motion = np.zeros(n_samples)

    # Ambient light noise (independent per channel)
    noise_green = rng.normal(0, cfg.ambient_noise_std, n_samples)
    noise_red = rng.normal(0, cfg.ambient_noise_std, n_samples)
    noise_ir = rng.normal(0, cfg.ambient_noise_std, n_samples)

    # Combine: AC signal amplitude differs by channel (green strongest)
    ac_green = ppg_base * 50000.0   # ~50k counts AC amplitude
    ac_red = ppg_base * 35000.0     # Red has smaller AC component
    ac_ir = ppg_base * 30000.0      # IR smallest

    ppg_green = (cfg.dc_offset_green + ac_green +
                 baseline * 10000 + motion * 5000 + noise_green * 3000)
    ppg_red = (cfg.dc_offset_red + ac_red +
               baseline * 8000 + motion * 4000 + noise_red * 2500)
    ppg_ir = (cfg.dc_offset_ir + ac_ir +
              baseline * 7000 + motion * 3500 + noise_ir * 2000)

    # Clip to 20-bit ADC range (0 .. 1048575) like MAX86141
    ppg_green = np.clip(ppg_green, 0, 1048575)
    ppg_red = np.clip(ppg_red, 0, 1048575)
    ppg_ir = np.clip(ppg_ir, 0, 1048575)

    return t, ppg_green, ppg_red, ppg_ir


def write_ppg_csv(filepath: str, timestamps: np.ndarray,
                  green: np.ndarray, red: np.ndarray,
                  ir: np.ndarray) -> None:
    """Write PPG data to CSV matching VelaSense sensor_ppgd topic format."""
    with open(filepath, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'ppg_green', 'ppg_red', 'ppg_ir'])
        for i in range(len(timestamps)):
            writer.writerow([
                f'{timestamps[i]:.4f}',
                f'{green[i]:.0f}',
                f'{red[i]:.0f}',
                f'{ir[i]:.0f}'
            ])


# ---------------------------------------------------------------------------
# IMU synthetic data generator
# ---------------------------------------------------------------------------

@dataclass
class IMUConfig:
    """Configuration for synthetic IMU generation."""
    mode: str = 'rest'                  # rest, walk, run, arm_swing
    sample_rate: int = IMU_SAMPLE_RATE  # Samples per second
    duration_s: float = 60.0            # Recording duration
    accel_fsr_g: float = 8.0            # +/- 8g (ICM42688_ACCEL_FSR_8G)
    gyro_fsr_dps: float = 2000.0        # +/- 2000 dps (ICM42688 gyro FSR)
    accel_noise_std_g: float = 0.004    # Accel noise in g (low-noise mode)
    gyro_noise_std_dps: float = 0.02    # Gyro noise in dps
    seed: Optional[int] = None


# Activity-specific parameters (frequency, amplitude in g units)
_ACTIVITY_PARAMS = {
    'rest': {
        'step_freq_hz': 0.0,
        'accel_amp_g': 0.0,
        'gyro_amp_dps': 0.0,
        'arm_swing_amp_g': 0.0,
        'arm_swing_freq_hz': 0.0,
    },
    'walk': {
        'step_freq_hz': 1.8,            # ~108 steps/min
        'accel_amp_g': 0.3,
        'gyro_amp_dps': 30.0,
        'arm_swing_amp_g': 0.5,
        'arm_swing_freq_hz': 0.9,       # Arm swing at half step rate
    },
    'run': {
        'step_freq_hz': 2.8,            # ~168 steps/min
        'accel_amp_g': 0.7,
        'gyro_amp_dps': 60.0,
        'arm_swing_amp_g': 1.2,
        'arm_swing_freq_hz': 1.4,
    },
    'arm_swing': {
        'step_freq_hz': 0.5,            # Slow arm movement
        'accel_amp_g': 0.15,
        'gyro_amp_dps': 15.0,
        'arm_swing_amp_g': 0.8,
        'arm_swing_freq_hz': 0.5,
    },
}


def generate_imu(cfg: IMUConfig) -> Tuple[np.ndarray, np.ndarray,
                                           np.ndarray, np.ndarray,
                                           np.ndarray, np.ndarray,
                                           np.ndarray]:
    """
    Generate synthetic 6-axis IMU data (accel XYZ + gyro XYZ).

    The signal model:
      - Gravity component on Z-axis (~1g when wrist is flat)
      - Activity-specific periodic motion (step frequency + harmonics)
      - Arm swing oscillation (primarily X-axis for wrist-worn sensor)
      - Gyroscope rotation during movement
      - Sensor noise (Gaussian, independent per axis)

    Physical units:
      - Accelerometer: m/s^2 (matching ICM42688 output after conversion)
      - Gyroscope: rad/s (matching ICM42688 output after conversion)

    Returns:
        Tuple of (timestamps, ax, ay, az, gx, gy, gz) arrays.
    """
    rng = np.random.default_rng(cfg.seed)
    n_samples = int(cfg.sample_rate * cfg.duration_s)
    t = np.arange(n_samples) / cfg.sample_rate

    params = _ACTIVITY_PARAMS.get(cfg.mode, _ACTIVITY_PARAMS['rest'])

    G = 9.80665  # m/s^2 per g

    # Gravity vector (wrist flat: Z-up)
    grav_x = np.zeros(n_samples)
    grav_y = np.zeros(n_samples)
    grav_z = np.full(n_samples, 1.0 * G)  # 1g downward in sensor frame

    # Step / locomotion acceleration
    step_freq = params['step_freq_hz']
    amp_g = params['accel_amp_g']

    if step_freq > 0:
        step_phase = 2.0 * math.pi * step_freq * t
        # Vertical bounce (Z-axis)
        bounce = amp_g * np.sin(step_phase)
        # Forward-backward sway (Y-axis)
        sway = amp_g * 0.5 * np.sin(step_phase + math.pi / 4)
        # Add harmonics for realism
        bounce += amp_g * 0.3 * np.sin(2 * step_phase + 0.5)
        sway += amp_g * 0.2 * np.sin(2 * step_phase + 1.0)
    else:
        bounce = np.zeros(n_samples)
        sway = np.zeros(n_samples)

    # Arm swing (primarily X-axis for wrist sensor)
    swing_freq = params['arm_swing_freq_hz']
    swing_amp = params['arm_swing_amp_g']

    if swing_freq > 0:
        arm_swing = swing_amp * np.sin(2.0 * math.pi * swing_freq * t)
    else:
        arm_swing = np.zeros(n_samples)

    # Gyroscope rotation
    gyro_amp = params['gyro_amp_dps']
    gyro_freq = step_freq if step_freq > 0 else swing_freq

    if gyro_freq > 0 and gyro_amp > 0:
        gx_signal = gyro_amp * np.sin(2 * math.pi * gyro_freq * t)
        gy_signal = gyro_amp * 0.5 * np.sin(2 * math.pi * gyro_freq * t + 1.0)
        gz_signal = gyro_amp * 0.3 * np.sin(2 * math.pi * swing_freq * t)
    else:
        gx_signal = np.zeros(n_samples)
        gy_signal = np.zeros(n_samples)
        gz_signal = np.zeros(n_samples)

    # Add sensor noise
    noise_ax = rng.normal(0, cfg.accel_noise_std_g, n_samples) * G
    noise_ay = rng.normal(0, cfg.accel_noise_std_g, n_samples) * G
    noise_az = rng.normal(0, cfg.accel_noise_std_g, n_samples) * G
    noise_gx = rng.normal(0, cfg.gyro_noise_std_dps, n_samples) * (math.pi / 180)
    noise_gy = rng.normal(0, cfg.gyro_noise_std_dps, n_samples) * (math.pi / 180)
    noise_gz = rng.normal(0, cfg.gyro_noise_std_dps, n_samples) * (math.pi / 180)

    # Combine: accel in m/s^2, gyro in rad/s
    ax = (grav_x + arm_swing + noise_ax) * G + noise_ax
    ay = (grav_y + sway + noise_ay) * G + noise_ay
    az = (grav_z + bounce + noise_az) * G + noise_az

    gx = gx_signal + noise_gx
    gy = gy_signal + noise_gy
    gz = gz_signal + noise_gz

    # Clip to FSR range
    accel_max = cfg.accel_fsr_g * G
    gyro_max = cfg.gyro_fsr_dps * (math.pi / 180)
    ax = np.clip(ax, -accel_max, accel_max)
    ay = np.clip(ay, -accel_max, accel_max)
    az = np.clip(az, -accel_max, accel_max)
    gx = np.clip(gx, -gyro_max, gyro_max)
    gy = np.clip(gy, -gyro_max, gyro_max)
    gz = np.clip(gz, -gyro_max, gyro_max)

    return t, ax, ay, az, gx, gy, gz


def write_imu_csv(filepath: str, timestamps: np.ndarray,
                  ax: np.ndarray, ay: np.ndarray, az: np.ndarray,
                  gx: np.ndarray, gy: np.ndarray, gz: np.ndarray) -> None:
    """Write IMU data to CSV matching VelaSense sensor_accel/sensor_gyro topics."""
    with open(filepath, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'ax', 'ay', 'az', 'gx', 'gy', 'gz'])
        for i in range(len(timestamps)):
            writer.writerow([
                f'{timestamps[i]:.4f}',
                f'{ax[i]:.4f}', f'{ay[i]:.4f}', f'{az[i]:.4f}',
                f'{gx[i]:.4f}', f'{gy[i]:.4f}', f'{gz[i]:.4f}'
            ])


# ---------------------------------------------------------------------------
# EDA synthetic data generator
# ---------------------------------------------------------------------------

@dataclass
class EDAConfig:
    """Configuration for synthetic EDA generation."""
    baseline_scl_us: float = 5.0        # Baseline skin conductance (2-10 uS)
    sample_rate: int = EDA_SAMPLE_RATE  # 32 Hz (AD5940 default)
    duration_s: float = 120.0           # Recording duration
    scr_count: int = 5                  # Number of SCR events to inject
    scr_amplitude_us: float = 0.8       # SCR amplitude in uS
    scr_rise_time_s: float = 2.0        # SCR rise time (1-3s)
    scr_recovery_s: float = 8.0         # SCR recovery time (5-15s)
    noise_std_us: float = 0.02          # Measurement noise
    slow_drift_amp: float = 0.3         # Slow SCL drift amplitude
    seed: Optional[int] = None


def _scr_response(t: np.ndarray, onset: float, rise_time: float,
                  recovery_time: float, amplitude: float) -> np.ndarray:
    """
    Generate a single SCR (skin conductance response) event.

    Uses a bi-exponential model:
      SCR(t) = A * (1 - exp(-(t-t0)/tau_rise)) * exp(-(t-t0)/tau_recovery)
    for t >= t0, 0 otherwise.

    Args:
        t: Time array.
        onset: Event onset time in seconds.
        rise_time: Rise time constant (seconds).
        recovery_time: Recovery time constant (seconds).
        amplitude: Peak amplitude in uS.

    Returns:
        SCR waveform array (same shape as t).
    """
    result = np.zeros_like(t)
    mask = t >= onset
    dt = t[mask] - onset
    tau_rise = rise_time / 3.0      # 10-90% rise time ~ 2.2*tau
    tau_recovery = recovery_time / 3.0
    result[mask] = amplitude * (1.0 - np.exp(-dt / tau_rise)) * \
                   np.exp(-dt / tau_recovery)
    return result


def generate_eda(cfg: EDAConfig) -> Tuple[np.ndarray, np.ndarray,
                                           np.ndarray]:
    """
    Generate synthetic EDA (electrodermal activity) data.

    The signal model:
      - Baseline SCL (skin conductance level) with slow circadian drift
      - SCR events (skin conductance responses) - phasic component
      - Measurement noise (Gaussian)

    Returns:
        Tuple of (timestamps, scl, scr_flag) arrays.
        scl: Skin conductance in microsiemens (uS).
        scr_flag: 1 during an SCR event, 0 otherwise.
    """
    rng = np.random.default_rng(cfg.seed)
    n_samples = int(cfg.sample_rate * cfg.duration_s)
    t = np.arange(n_samples) / cfg.sample_rate

    # Baseline with slow drift
    baseline = cfg.baseline_scl_us + cfg.slow_drift_amp * np.sin(
        2.0 * math.pi * (1.0 / cfg.duration_s) * t
    )

    # Generate SCR events at random or evenly-spaced times
    scr_signal = np.zeros(n_samples)
    scr_flag = np.zeros(n_samples, dtype=int)

    if cfg.scr_count > 0:
        # Distribute events with some jitter across the recording
        event_spacing = cfg.duration_s / (cfg.scr_count + 1)
        for i in range(cfg.scr_count):
            onset = event_spacing * (i + 1) + rng.uniform(-5, 5)
            onset = max(10.0, min(onset, cfg.duration_s - 20.0))

            amplitude = cfg.scr_amplitude_us * rng.uniform(0.6, 1.4)
            rise = cfg.scr_rise_time_s * rng.uniform(0.7, 1.3)
            recovery = cfg.scr_recovery_s * rng.uniform(0.7, 1.3)

            scr_wave = _scr_response(t, onset, rise, recovery, amplitude)
            scr_signal += scr_wave

            # Mark the active region of this SCR event
            # Region: from onset to onset + rise_time + 0.5 * recovery_time
            # This captures the meaningful phasic response without overlap
            end_time = onset + rise + 0.5 * recovery
            event_mask = (t >= onset) & (t <= end_time)
            scr_flag[event_mask] = 1

    # Measurement noise
    noise = rng.normal(0, cfg.noise_std_us, n_samples)

    # Total SCL
    scl = baseline + scr_signal + noise
    # SCL must be positive (physical constraint)
    scl = np.maximum(scl, 0.1)

    return t, scl, scr_flag


def write_eda_csv(filepath: str, timestamps: np.ndarray,
                  scl: np.ndarray, scr_flag: np.ndarray) -> None:
    """Write EDA data to CSV matching VelaSense sensor_impd topic format."""
    with open(filepath, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'scl', 'scr_flag'])
        for i in range(len(timestamps)):
            writer.writerow([
                f'{timestamps[i]:.4f}',
                f'{scl[i]:.4f}',
                f'{scr_flag[i]}'
            ])


# ---------------------------------------------------------------------------
# Temperature synthetic data generator
# ---------------------------------------------------------------------------

@dataclass
class TempConfig:
    """Configuration for synthetic temperature generation."""
    baseline_temp_c: float = 34.0       # Baseline skin temperature (32-36 C)
    sample_rate: int = TEMP_SAMPLE_RATE # 1 Hz (MAX30208 one-shot)
    duration_s: float = 300.0           # Recording duration
    circadian_amp_c: float = 0.5        # Circadian rhythm amplitude
    noise_std_c: float = 0.1            # Measurement noise ( +/- 0.1 C)
    seed: Optional[int] = None


def generate_temperature(cfg: TempConfig) -> Tuple[np.ndarray, np.ndarray]:
    """
    Generate synthetic skin temperature data.

    The signal model:
      - Baseline skin temperature (~34 C for wrist)
      - Slow circadian drift (simulated as low-frequency sinusoid)
      - Measurement noise (Gaussian, +/- 0.1 C typical)

    Returns:
        Tuple of (timestamps, temp_c) arrays.
    """
    rng = np.random.default_rng(cfg.seed)
    n_samples = int(cfg.sample_rate * cfg.duration_s)
    t = np.arange(n_samples) / cfg.sample_rate

    # Circadian rhythm (slow sinusoidal drift)
    circadian = cfg.circadian_amp_c * np.sin(
        2.0 * math.pi * t / cfg.duration_s
    )

    # Very slow random walk for realistic drift
    drift = np.cumsum(rng.normal(0, 0.005, n_samples))
    # Normalize drift to stay within reasonable bounds
    drift = drift - np.mean(drift)
    drift = drift * (0.3 / (np.std(drift) + 1e-10))

    # Measurement noise
    noise = rng.normal(0, cfg.noise_std_c, n_samples)

    # Combine
    temp_c = cfg.baseline_temp_c + circadian + drift + noise

    # Physical constraints: skin temperature range 30-38 C
    temp_c = np.clip(temp_c, 30.0, 38.0)

    return t, temp_c


def write_temp_csv(filepath: str, timestamps: np.ndarray,
                   temp_c: np.ndarray) -> None:
    """Write temperature data to CSV matching VelaSense sensor_temp topic."""
    with open(filepath, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'temp_c'])
        for i in range(len(timestamps)):
            writer.writerow([
                f'{timestamps[i]:.4f}',
                f'{temp_c[i]:.2f}'
            ])


# ---------------------------------------------------------------------------
# Multi-sensor combined generation
# ---------------------------------------------------------------------------

def generate_all(prefix: str, duration: float, hr: float, mode: str,
                 seed: int) -> List[str]:
    """Generate all sensor data files with matching timestamps."""
    files = []

    # PPG
    ppg_cfg = PPGConfig(
        heart_rate_bpm=hr, duration_s=duration, seed=seed,
        motion_noise_std=0.1 if mode != 'rest' else 0.02
    )
    t, g, r, ir = generate_ppg(ppg_cfg)
    ppg_path = f'{prefix}ppg.csv'
    write_ppg_csv(ppg_path, t, g, r, ir)
    files.append(ppg_path)

    # IMU
    imu_cfg = IMUConfig(mode=mode, duration_s=duration, seed=seed + 1 if seed else None)
    t, ax, ay, az, gx, gy, gz = generate_imu(imu_cfg)
    imu_path = f'{prefix}imu.csv'
    write_imu_csv(imu_path, t, ax, ay, az, gx, gy, gz)
    files.append(imu_path)

    # EDA
    eda_cfg = EDAConfig(duration_s=duration, seed=seed + 2 if seed else None)
    t, scl, scr = generate_eda(eda_cfg)
    eda_path = f'{prefix}eda.csv'
    write_eda_csv(eda_path, t, scl, scr)
    files.append(eda_path)

    # Temperature
    temp_cfg = TempConfig(duration_s=duration, seed=seed + 3 if seed else None)
    t, tc = generate_temperature(temp_cfg)
    temp_path = f'{prefix}temp.csv'
    write_temp_csv(temp_path, t, tc)
    files.append(temp_path)

    return files


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description='VelaSense Synthetic Sensor Data Generator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        '--sensor', choices=['ppg', 'imu', 'eda', 'temp'],
        help='Single sensor to generate'
    )
    parser.add_argument(
        '--all', action='store_true',
        help='Generate all sensors with matching durations'
    )
    parser.add_argument(
        '--prefix', default='sim_',
        help='Output file prefix when using --all (default: sim_)'
    )
    parser.add_argument(
        '-o', '--output',
        help='Output CSV file path (single sensor mode)'
    )
    parser.add_argument(
        '--duration', type=float, default=60.0,
        help='Recording duration in seconds (default: 60)'
    )
    parser.add_argument(
        '--hr', type=float, default=72.0,
        help='Heart rate in BPM for PPG (default: 72)'
    )
    parser.add_argument(
        '--mode', choices=['rest', 'walk', 'run', 'arm_swing'],
        default='rest',
        help='Activity mode for IMU (default: rest)'
    )
    parser.add_argument(
        '--baseline-scl', type=float, default=5.0,
        help='Baseline SCL in uS for EDA (default: 5.0)'
    )
    parser.add_argument(
        '--scr-count', type=int, default=5,
        help='Number of SCR events for EDA (default: 5)'
    )
    parser.add_argument(
        '--baseline-temp', type=float, default=34.0,
        help='Baseline skin temperature in C (default: 34.0)'
    )
    parser.add_argument(
        '--motion-noise', type=float, default=0.0,
        help='Motion noise magnitude for PPG (0-1, default: 0)'
    )
    parser.add_argument(
        '--seed', type=int, default=None,
        help='Random seed for reproducibility'
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if not args.sensor and not args.all:
        parser.print_help()
        return 1

    if args.all:
        files = generate_all(
            prefix=args.prefix,
            duration=args.duration,
            hr=args.hr,
            mode=args.mode,
            seed=args.seed or 42
        )
        for f in files:
            print(f'Generated: {f}')
        return 0

    # Single sensor mode
    output = args.output or f'{args.sensor}_sim.csv'

    if args.sensor == 'ppg':
        cfg = PPGConfig(
            heart_rate_bpm=args.hr,
            duration_s=args.duration,
            motion_noise_std=args.motion_noise,
            seed=args.seed
        )
        t, g, r, ir = generate_ppg(cfg)
        write_ppg_csv(output, t, g, r, ir)
        print(f'Generated PPG: {output}  ({len(t)} samples, '
              f'{cfg.duration_s}s, {cfg.heart_rate_bpm} bpm)')

    elif args.sensor == 'imu':
        cfg = IMUConfig(
            mode=args.mode,
            duration_s=args.duration,
            seed=args.seed
        )
        t, ax, ay, az, gx, gy, gz = generate_imu(cfg)
        write_imu_csv(output, t, ax, ay, az, gx, gy, gz)
        print(f'Generated IMU: {output}  ({len(t)} samples, '
              f'{cfg.duration_s}s, mode={cfg.mode})')

    elif args.sensor == 'eda':
        cfg = EDAConfig(
            baseline_scl_us=args.baseline_scl,
            duration_s=args.duration,
            scr_count=args.scr_count,
            seed=args.seed
        )
        t, scl, scr = generate_eda(cfg)
        write_eda_csv(output, t, scl, scr)
        print(f'Generated EDA: {output}  ({len(t)} samples, '
              f'{cfg.duration_s}s, {cfg.scr_count} SCR events)')

    elif args.sensor == 'temp':
        cfg = TempConfig(
            baseline_temp_c=args.baseline_temp,
            duration_s=args.duration,
            seed=args.seed
        )
        t, tc = generate_temperature(cfg)
        write_temp_csv(output, t, tc)
        print(f'Generated Temp: {output}  ({len(t)} samples, '
              f'{cfg.duration_s}s, baseline={cfg.baseline_temp_c}C)')

    return 0


if __name__ == '__main__':
    sys.exit(main())
