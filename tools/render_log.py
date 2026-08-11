#!/usr/bin/env python3
"""
render_log.py -- Sensor Data Visualization for VelaSense

Reads CSV sensor logs (from files or serial port) and renders plots:
  - PPG waveform with detected peaks and heart rate
  - IMU accelerometer magnitude and per-axis data
  - EDA skin conductance with SCR events marked
  - Skin temperature trend

Supports two modes:
  1. File mode: reads a CSV file and generates a static plot
  2. Real-time mode: reads from a serial port and updates a live plot

CSV format expected:
  PPG:   timestamp, ppg_green, ppg_red, ppg_ir
  IMU:   timestamp, ax, ay, az, gx, gy, gz
  EDA:   timestamp, scl, scr_flag
  Temp:  timestamp, temp_c

Usage examples:
  # File mode -- single sensor
  python render_log.py --sensor ppg --input ppg_data.csv -o ppg_plot.png
  python render_log.py --sensor imu --input imu_data.csv -o imu_plot.png
  python render_log.py --sensor eda --input eda_data.csv -o eda_plot.png

  # File mode -- multi-sensor dashboard
  python render_log.py --input-prefix sim_ -o dashboard.png

  # Real-time mode
  python render_log.py --sensor ppg --serial /dev/ttyUSB0 --baud 115200

  # Interactive window (requires display)
  python render_log.py --sensor ppg --input ppg_data.csv --show

Dependencies: numpy, matplotlib
Optional: pyserial (for real-time mode)
"""

import argparse
import csv
import os
import sys
import time
from typing import Dict, List, Optional, Tuple

import numpy as np

try:
    import matplotlib
    matplotlib.use('Agg')  # Non-interactive backend by default
    import matplotlib.pyplot as plt
    import matplotlib.gridspec as gridspec
    from matplotlib.patches import Rectangle
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------

def load_csv(filepath: str) -> Dict[str, np.ndarray]:
    """
    Load a CSV file into a dictionary of numpy arrays.

    Automatically detects the sensor type from column headers.

    Returns:
        Dictionary with column names as keys and numpy arrays as values.
    """
    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)
        columns = {name: [] for name in reader.fieldnames}
        for row in reader:
            for name in reader.fieldnames:
                try:
                    columns[name].append(float(row[name]))
                except (ValueError, KeyError):
                    columns[name].append(0.0)

    return {name: np.array(vals) for name, vals in columns.items()}


def detect_sensor_type(headers: List[str]) -> str:
    """
    Detect sensor type from CSV column headers.

    Returns: 'ppg', 'imu', 'eda', 'temp', or 'unknown'.
    """
    header_set = set(h.lower() for h in headers)
    if 'ppg_green' in header_set:
        return 'ppg'
    if 'ax' in header_set and 'gx' in header_set:
        return 'imu'
    if 'scl' in header_set:
        return 'eda'
    if 'temp_c' in header_set:
        return 'temp'
    return 'unknown'


# ---------------------------------------------------------------------------
# Peak detection for PPG visualization
# ---------------------------------------------------------------------------

def find_peaks_for_plot(signal: np.ndarray, fs: float,
                        min_bpm: float = 30,
                        max_bpm: float = 220) -> Tuple[np.ndarray,
                                                        np.ndarray]:
    """
    Simple peak detection for visualization overlay.

    Returns:
        Tuple of (peak_indices, ibi_values_ms).
    """
    min_distance = int(fs * 60.0 / max_bpm)

    # Bandpass filter
    try:
        from scipy.signal import butter, filtfilt
        nyq = 0.5 * fs
        b, a = butter(4, [0.5 / nyq, 5.0 / nyq], btype='band')
        filtered = filtfilt(b, a, signal.astype(float))
    except ImportError:
        filtered = signal.astype(float)

    # Adaptive peak detection
    peaks = []
    running_peak = np.max(filtered[:int(fs * 2)])
    running_trough = np.min(filtered[:int(fs * 2)])
    threshold = running_trough + 0.35 * (running_peak - running_trough)

    prev = filtered[0]
    rising = True
    cand_idx = 0
    cand_val = filtered[0]

    for i in range(1, len(filtered)):
        if filtered[i] > prev:
            if not rising:
                rising = True
                cand_idx = i
                cand_val = filtered[i]
            elif filtered[i] > cand_val:
                cand_idx = i
                cand_val = filtered[i]
        elif filtered[i] < prev and rising:
            if cand_val > threshold and (
                    not peaks or (cand_idx - peaks[-1]) >= min_distance):
                peaks.append(cand_idx)
                running_peak = 0.8 * running_peak + 0.2 * cand_val
            rising = False
            if filtered[i] < running_trough:
                running_trough = 0.8 * running_trough + 0.2 * filtered[i]
            threshold = running_trough + 0.35 * (running_peak - running_trough)
        prev = filtered[i]

    peaks_arr = np.array(peaks, dtype=int)

    # Compute IBIs
    ibis = np.zeros(len(peaks_arr))
    for i in range(1, len(peaks_arr)):
        ibis[i] = (peaks_arr[i] - peaks_arr[i - 1]) / fs * 1000.0

    return peaks_arr, ibis


# ---------------------------------------------------------------------------
# Plot renderers
# ---------------------------------------------------------------------------

def render_ppg(data: Dict[str, np.ndarray], ax_list: List,
               title: str = 'PPG Signal', show_peaks: bool = True,
               sample_rate: float = 100.0) -> None:
    """
    Plot PPG waveform with optional peak markers.

    Args:
        data: Dictionary with 'timestamp', 'ppg_green', 'ppg_red', 'ppg_ir'.
        ax_list: List of matplotlib axes (needs at least 2: waveform + HR).
        title: Plot title.
        show_peaks: Whether to overlay detected peaks.
        sample_rate: PPG sample rate in Hz.
    """
    t = data['timestamp']
    green = data['ppg_green']
    red = data.get('ppg_red', None)
    ir = data.get('ppg_ir', None)

    ax_wave = ax_list[0]
    ax_hr = ax_list[1] if len(ax_list) > 1 else None

    # Plot waveforms
    ax_wave.plot(t, green, 'g-', linewidth=0.5, alpha=0.8, label='Green')
    if red is not None:
        offset = np.mean(green) - np.mean(red)
        ax_wave.plot(t, red + offset, 'r-', linewidth=0.5, alpha=0.6, label='Red')
    if ir is not None:
        offset = np.mean(green) - np.mean(ir)
        ax_wave.plot(t, ir + offset, color='purple', linewidth=0.5,
                     alpha=0.6, label='IR')

    ax_wave.set_ylabel('PPG (ADC counts)')
    ax_wave.set_title(title)
    ax_wave.legend(loc='upper right', fontsize=8)

    # Detect and overlay peaks
    if show_peaks and len(green) > sample_rate * 2:
        peaks, ibis = find_peaks_for_plot(green, sample_rate)
        if len(peaks) > 0:
            ax_wave.plot(t[peaks], green[peaks], 'rv', markersize=4,
                         label='Peaks', zorder=5)

            # Plot heart rate trend on second axis
            if ax_hr is not None and len(ibis) > 1:
                hr = 60000.0 / ibis[1:]  # Skip first (zero) IBI
                hr = np.clip(hr, 30, 220)
                ax_hr.plot(t[peaks[1:]], hr, 'b-', linewidth=1.0)
                ax_hr.axhline(y=np.mean(hr), color='r', linestyle='--',
                              alpha=0.5, label=f'Mean HR: {np.mean(hr):.0f}')
                ax_hr.set_ylabel('Heart Rate (BPM)')
                ax_hr.set_ylim(30, 200)
                ax_hr.legend(loc='upper right', fontsize=8)


def render_imu(data: Dict[str, np.ndarray], ax_list: List,
               title: str = 'IMU Accelerometer') -> None:
    """
    Plot IMU accelerometer data.

    Args:
        data: Dictionary with 'timestamp', 'ax', 'ay', 'az'.
        ax_list: List of matplotlib axes (needs 2: accel + magnitude).
        title: Plot title.
    """
    t = data['timestamp']
    ax = data['ax']
    ay = data['ay']
    az = data['az']

    ax_acc = ax_list[0]
    ax_mag = ax_list[1] if len(ax_list) > 1 else None

    # Per-axis acceleration
    G = 9.80665
    ax_acc.plot(t, ax / G, 'r-', linewidth=0.5, alpha=0.7, label='X')
    ax_acc.plot(t, ay / G, 'g-', linewidth=0.5, alpha=0.7, label='Y')
    ax_acc.plot(t, az / G, 'b-', linewidth=0.5, alpha=0.7, label='Z')
    ax_acc.set_ylabel('Acceleration (g)')
    ax_acc.set_title(title)
    ax_acc.legend(loc='upper right', fontsize=8)

    # Acceleration magnitude
    if ax_mag is not None:
        mag = np.sqrt(ax**2 + ay**2 + az**2) / G
        ax_mag.plot(t, mag, 'k-', linewidth=0.8)
        ax_mag.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5)
        ax_mag.axhline(y=1.1, color='orange', linestyle='--', alpha=0.5,
                       label='Rest threshold')
        ax_mag.axhline(y=1.4, color='red', linestyle='--', alpha=0.5,
                       label='Run threshold')
        ax_mag.set_ylabel('Magnitude (g)')
        ax_mag.legend(loc='upper right', fontsize=8)


def render_eda(data: Dict[str, np.ndarray], ax_list: List,
               title: str = 'Electrodermal Activity') -> None:
    """
    Plot EDA skin conductance with SCR events marked.

    Args:
        data: Dictionary with 'timestamp', 'scl', 'scr_flag'.
        ax_list: List of matplotlib axes.
        title: Plot title.
    """
    t = data['timestamp']
    scl = data['scl']
    scr_flag = data.get('scr_flag', None)

    ax_scl = ax_list[0]

    # Plot SCL trace
    ax_scl.plot(t, scl, 'b-', linewidth=0.8, label='SCL')
    ax_scl.set_ylabel('Skin Conductance (uS)')
    ax_scl.set_title(title)
    ax_scl.legend(loc='upper right', fontsize=8)

    # Mark SCR events with shaded regions
    if scr_flag is not None:
        in_event = False
        event_start = 0
        for i in range(len(scr_flag)):
            if scr_flag[i] > 0.5 and not in_event:
                in_event = True
                event_start = i
            elif scr_flag[i] <= 0.5 and in_event:
                in_event = False
                ax_scl.axvspan(t[event_start], t[i],
                               alpha=0.2, color='red', label='SCR event')
        if in_event:
            ax_scl.axvspan(t[event_start], t[-1],
                           alpha=0.2, color='red')

        # Mark peaks within SCR events
        scr_indices = np.where(scr_flag > 0.5)[0]
        if len(scr_indices) > 0:
            # Find peak within each contiguous SCR region
            regions = np.split(scr_indices,
                               np.where(np.diff(scr_indices) > 1)[0] + 1)
            for region in regions:
                if len(region) > 0:
                    peak_idx = region[np.argmax(scl[region])]
                    ax_scl.plot(t[peak_idx], scl[peak_idx], 'r^',
                               markersize=8, zorder=5)


def render_temperature(data: Dict[str, np.ndarray], ax_list: List,
                       title: str = 'Skin Temperature') -> None:
    """
    Plot skin temperature trend.

    Args:
        data: Dictionary with 'timestamp', 'temp_c'.
        ax_list: List of matplotlib axes.
        title: Plot title.
    """
    t = data['timestamp']
    temp = data['temp_c']

    ax = ax_list[0]

    ax.plot(t, temp, 'r-', linewidth=0.8)
    ax.axhline(y=np.mean(temp), color='blue', linestyle='--', alpha=0.5,
               label=f'Mean: {np.mean(temp):.1f} C')
    ax.fill_between(t,
                     np.mean(temp) - 0.2,
                     np.mean(temp) + 0.2,
                     alpha=0.1, color='blue')
    ax.set_ylabel('Temperature (C)')
    ax.set_title(title)
    ax.legend(loc='upper right', fontsize=8)


# ---------------------------------------------------------------------------
# Multi-sensor dashboard
# ---------------------------------------------------------------------------

def render_dashboard(ppg_data: Optional[Dict] = None,
                     imu_data: Optional[Dict] = None,
                     eda_data: Optional[Dict] = None,
                     temp_data: Optional[Dict] = None,
                     output_path: Optional[str] = None,
                     show: bool = False,
                     figsize: Tuple[int, int] = (14, 12)) -> None:
    """
    Render a multi-sensor dashboard with all available data.
    """
    if not HAS_MATPLOTLIB:
        print('ERROR: matplotlib is required for rendering.')
        print('  pip install matplotlib')
        return

    # Count available sensors
    sensors = []
    if ppg_data is not None:
        sensors.append('ppg')
    if imu_data is not None:
        sensors.append('imu')
    if eda_data is not None:
        sensors.append('eda')
    if temp_data is not None:
        sensors.append('temp')

    if not sensors:
        print('No sensor data to render.')
        return

    # Calculate subplot layout
    n_plots = 0
    if ppg_data is not None:
        n_plots += 2  # Waveform + HR
    if imu_data is not None:
        n_plots += 2  # Axes + magnitude
    if eda_data is not None:
        n_plots += 1
    if temp_data is not None:
        n_plots += 1

    fig = plt.figure(figsize=figsize)
    gs = gridspec.GridSpec(n_plots, 1, hspace=0.4)

    ax_idx = 0
    axes = []

    if ppg_data is not None:
        ax1 = fig.add_subplot(gs[ax_idx])
        ax2 = fig.add_subplot(gs[ax_idx + 1], sharex=ax1)
        render_ppg(ppg_data, [ax1, ax2], title='PPG (MAX86141)')
        axes.extend([ax1, ax2])
        ax_idx += 2

    if imu_data is not None:
        ax3 = fig.add_subplot(gs[ax_idx])
        ax4 = fig.add_subplot(gs[ax_idx + 1], sharex=ax3)
        render_imu(imu_data, [ax3, ax4], title='IMU (ICM-42688-P)')
        axes.extend([ax3, ax4])
        ax_idx += 2

    if eda_data is not None:
        ax5 = fig.add_subplot(gs[ax_idx])
        render_eda(eda_data, [ax5], title='EDA (AD5940)')
        axes.append(ax5)
        ax_idx += 1

    if temp_data is not None:
        ax6 = fig.add_subplot(gs[ax_idx])
        render_temperature(temp_data, [ax6], title='Temperature (MAX30208)')
        axes.append(ax6)
        ax_idx += 1

    # Set x-label on bottom axis only
    axes[-1].set_xlabel('Time (seconds)')

    fig.suptitle('VelaSense Sensor Dashboard', fontsize=14, fontweight='bold')

    if output_path:
        fig.savefig(output_path, dpi=150, bbox_inches='tight')
        print(f'Saved plot to {output_path}')

    if show:
        plt.show()
    else:
        plt.close(fig)


# ---------------------------------------------------------------------------
# Real-time serial mode
# ---------------------------------------------------------------------------

def realtime_plot(sensor_type: str, serial_port: str, baud: int,
                  window_s: float = 10.0) -> None:
    """
    Real-time sensor data visualization from serial port.

    Reads CSV-formatted lines from the serial port and updates a live plot.

    Args:
        sensor_type: 'ppg', 'imu', 'eda', or 'temp'.
        serial_port: Serial port path (e.g., /dev/ttyUSB0).
        baud: Baud rate.
        window_s: Time window to display (seconds).
    """
    if not HAS_MATPLOTLIB:
        print('ERROR: matplotlib is required for real-time rendering.')
        return

    try:
        import serial
    except ImportError:
        print('ERROR: pyserial is required for real-time mode.')
        print('  pip install pyserial')
        return

    plt.ion()
    fig, ax = plt.subplots(figsize=(12, 4))
    fig.suptitle(f'VelaSense Real-Time: {sensor_type.upper()}')

    # Data buffers
    buffer_size = int(window_s * 100)  # Assume max 100 Hz
    timestamps = np.zeros(buffer_size)
    values = np.zeros(buffer_size)
    write_idx = 0
    count = 0

    print(f'Opening serial port {serial_port} at {baud} baud...')
    ser = serial.Serial(serial_port, baud, timeout=0.1)
    print('Connected. Press Ctrl+C to stop.')

    # Skip header line if present
    header = ser.readline().decode('utf-8', errors='ignore').strip()
    print(f'Header: {header}')

    try:
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue

            parts = line.split(',')
            if len(parts) < 2:
                continue

            try:
                ts = float(parts[0])
                val = float(parts[1])
            except ValueError:
                continue

            # Circular buffer
            idx = write_idx % buffer_size
            timestamps[idx] = ts
            values[idx] = val
            write_idx += 1
            count = min(count + 1, buffer_size)

            # Update plot periodically
            if write_idx % 10 == 0:
                ax.clear()
                if count < buffer_size:
                    ax.plot(timestamps[:count], values[:count], 'b-', linewidth=0.8)
                else:
                    # Reorder circular buffer
                    ordered_ts = np.concatenate([
                        timestamps[idx + 1:],
                        timestamps[:idx + 1]
                    ])
                    ordered_vals = np.concatenate([
                        values[idx + 1:],
                        values[:idx + 1]
                    ])
                    ax.plot(ordered_ts, ordered_vals, 'b-', linewidth=0.8)

                ax.set_xlabel('Time (s)')
                ax.set_ylabel(f'{sensor_type.upper()} value')
                ax.set_title(f'Samples: {count}  Rate: ~{count / window_s:.0f} Hz')
                fig.canvas.draw()
                fig.canvas.flush_events()

    except KeyboardInterrupt:
        print('\nStopped.')
    finally:
        ser.close()
        plt.ioff()
        plt.close(fig)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description='VelaSense Sensor Data Visualization',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        '--sensor', choices=['ppg', 'imu', 'eda', 'temp'],
        help='Sensor type (auto-detected from CSV headers if not specified)'
    )
    parser.add_argument(
        '--input', '-i',
        help='Input CSV file path'
    )
    parser.add_argument(
        '--input-prefix',
        help='Input file prefix for multi-sensor mode '
             '(reads <prefix>ppg.csv, <prefix>imu.csv, etc.)'
    )
    parser.add_argument(
        '--serial',
        help='Serial port for real-time mode (e.g., /dev/ttyUSB0)'
    )
    parser.add_argument(
        '--baud', type=int, default=115200,
        help='Serial baud rate (default: 115200)'
    )
    parser.add_argument(
        '--sample-rate', type=float, default=100.0,
        help='Sensor sample rate in Hz (default: 100)'
    )
    parser.add_argument(
        '--output', '-o',
        help='Output image file path (PNG, SVG, PDF)'
    )
    parser.add_argument(
        '--show', action='store_true',
        help='Show interactive plot window'
    )
    parser.add_argument(
        '--no-peaks', action='store_true',
        help='Disable peak detection overlay for PPG'
    )
    parser.add_argument(
        '--dpi', type=int, default=150,
        help='Output image DPI (default: 150)'
    )
    parser.add_argument(
        '--figsize', type=float, nargs=2, default=[14, 8],
        help='Figure size in inches (default: 14 8)'
    )
    return parser


def main() -> int:
    if not HAS_MATPLOTLIB:
        print('ERROR: matplotlib is required.')
        print('  pip install matplotlib')
        return 1

    parser = build_parser()
    args = parser.parse_args()

    # Real-time mode
    if args.serial:
        sensor = args.sensor or 'ppg'
        realtime_plot(sensor, args.serial, args.baud)
        return 0

    # Multi-sensor dashboard mode
    if args.input_prefix:
        prefix = args.input_prefix
        ppg_data = load_csv(f'{prefix}ppg.csv') if os.path.exists(f'{prefix}ppg.csv') else None
        imu_data = load_csv(f'{prefix}imu.csv') if os.path.exists(f'{prefix}imu.csv') else None
        eda_data = load_csv(f'{prefix}eda.csv') if os.path.exists(f'{prefix}eda.csv') else None
        temp_data = load_csv(f'{prefix}temp.csv') if os.path.exists(f'{prefix}temp.csv') else None

        output = args.output or 'dashboard.png'
        render_dashboard(ppg_data, imu_data, eda_data, temp_data,
                         output_path=output, show=args.show,
                         figsize=tuple(args.figsize))
        return 0

    # Single sensor file mode
    if not args.input:
        print('ERROR: --input or --input-prefix or --serial is required.')
        parser.print_help()
        return 1

    data = load_csv(args.input)
    headers = list(data.keys())
    sensor_type = args.sensor or detect_sensor_type(headers)

    print(f'Detected sensor type: {sensor_type}')
    print(f'Columns: {headers}')
    print(f'Samples: {len(data[headers[0]])}')

    if args.show:
        matplotlib.use('TkAgg')
        import matplotlib.pyplot as plt_show

    fig, ax_list = plt.subplots(2 if sensor_type in ('ppg', 'imu') else 1,
                                1, figsize=tuple(args.figsize))

    if not isinstance(ax_list, (list, np.ndarray)):
        ax_list = [ax_list]
    else:
        ax_list = list(ax_list)

    title = {
        'ppg': 'PPG Signal (MAX86141)',
        'imu': 'IMU Accelerometer (ICM-42688-P)',
        'eda': 'Electrodermal Activity (AD5940)',
        'temp': 'Skin Temperature (MAX30208)',
    }.get(sensor_type, 'Sensor Data')

    if sensor_type == 'ppg':
        render_ppg(data, ax_list, title=title,
                   show_peaks=not args.no_peaks,
                   sample_rate=args.sample_rate)
    elif sensor_type == 'imu':
        render_imu(data, ax_list, title=title)
    elif sensor_type == 'eda':
        render_eda(data, ax_list, title=title)
    elif sensor_type == 'temp':
        render_temperature(data, ax_list, title=title)
    else:
        print(f'Unknown sensor type: {sensor_type}')
        print('Use --sensor to specify manually.')
        return 1

    # Set x-label on bottom axis
    ax_list[-1].set_xlabel('Time (seconds)')

    plt.tight_layout()

    output = args.output
    if output:
        fig.savefig(output, dpi=args.dpi, bbox_inches='tight')
        print(f'Saved plot to {output}')

    if args.show:
        plt.show()
    else:
        plt.close(fig)

    return 0


if __name__ == '__main__':
    sys.exit(main())
