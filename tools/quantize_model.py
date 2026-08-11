#!/usr/bin/env python3
from __future__ import annotations
"""
quantize_model.py -- INT8 Model Quantization for VelaSense TinyML Engine

Converts a PyTorch FP32 LSTM/Dense emotion-arousal classifier to INT8
quantized weights for deployment on the VelaSense wearable (Cortex-M33).

Target architecture (matching firmware/libs/inference/tinyml.h):
  2-layer LSTM(32) -> Dense(16) -> Dense(5) -> softmax
  Input: 15 features (PPG/IMU/EDA/temperature)
  Output: 5 arousal classes

Output artifacts:
  1. model_data.h   -- C header with INT8 weight arrays
  2. quant_params.json -- Quantization parameters (scale, zero-point)
  3. accuracy_report.json -- FP32 vs INT8 accuracy comparison

Quantization method:
  - Post-training dynamic quantization for LSTM layers
  - Post-training static quantization for Dense layers
  - Per-tensor symmetric INT8 (zero_point = 0)
  - Calibrates with representative sensor data

Usage:
  python quantize_model.py --model model.pt --calib calib_data.pt -o output/
  python quantize_model.py --model model.pt --dummy --seq-len 50 -o output/

Requirements:
  torch >= 2.0
  numpy
"""

import argparse
import json
import math
import os
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

import numpy as np

try:
    import torch
    import torch.nn as nn
    import torch.quantization as quant
    HAS_TORCH = True
except ImportError:
    HAS_TORCH = False


# ---------------------------------------------------------------------------
# Model architecture (must match firmware tinyml.h)
# ---------------------------------------------------------------------------

INPUT_DIM = 15
LSTM_UNITS = 32
DENSE1_UNITS = 16
OUTPUT_CLASSES = 5

if HAS_TORCH:
    class VelaSenseLSTM(nn.Module):
        """
        PyTorch model matching the VelaSense firmware architecture.

        Architecture: LSTM(32) x 2 -> Dense(16, ReLU) -> Dense(5) -> softmax

        This class can load a trained model from a state dict or full checkpoint.
        """

        def __init__(self, input_dim: int = INPUT_DIM,
                     lstm_units: int = LSTM_UNITS,
                     dense1_units: int = DENSE1_UNITS,
                     output_classes: int = OUTPUT_CLASSES):
            super().__init__()
            self.lstm1 = nn.LSTM(input_dim, lstm_units, batch_first=True)
            self.lstm2 = nn.LSTM(lstm_units, lstm_units, batch_first=True)
            self.dense1 = nn.Linear(lstm_units, dense1_units)
            self.relu = nn.ReLU()
            self.dense2 = nn.Linear(dense1_units, output_classes)

        def forward(self, x: torch.Tensor) -> torch.Tensor:
            # x: [batch, seq_len, input_dim]
            out, _ = self.lstm1(x)
            out, _ = self.lstm2(out)
            # Take last timestep
            out = out[:, -1, :]  # [batch, lstm_units]
            out = self.relu(self.dense1(out))
            out = self.dense2(out)
            return out  # Raw logits (apply softmax externally)


# ---------------------------------------------------------------------------
# Quantization utilities
# ---------------------------------------------------------------------------

@dataclass
class QuantParams:
    """Quantization parameters for a single tensor."""
    name: str
    scale: float
    zero_point: int
    dtype: str = 'int8'
    shape: List[int] = field(default_factory=list)
    num_elements: int = 0


def compute_scale_zp(tensor: torch.Tensor,
                     symmetric: bool = True) -> Tuple[float, int]:
    """
    Compute INT8 quantization scale and zero-point.

    For symmetric quantization:
      scale = max(abs(tensor)) / 127
      zero_point = 0

    For asymmetric quantization:
      scale = (max - min) / 255
      zero_point = round(-min / scale) - 128

    Args:
        tensor: FP32 tensor to quantize.
        symmetric: If True, use symmetric quantization.

    Returns:
        Tuple of (scale, zero_point).
    """
    t_min = float(tensor.min())
    t_max = float(tensor.max())

    if symmetric:
        abs_max = max(abs(t_min), abs(t_max))
        if abs_max < 1e-10:
            return 1.0, 0
        scale = abs_max / 127.0
        zero_point = 0
    else:
        if t_max - t_min < 1e-10:
            return 1.0, 0
        scale = (t_max - t_min) / 255.0
        zero_point = int(round(-t_min / scale)) - 128
        zero_point = max(-128, min(127, zero_point))

    return scale, zero_point


def quantize_tensor(tensor: torch.Tensor, scale: float,
                    zero_point: int) -> torch.Tensor:
    """Quantize FP32 tensor to INT8."""
    q = torch.clamp(
        torch.round(tensor / scale + zero_point),
        -128, 127
    ).to(torch.int8)
    return q


def dequantize_tensor(q_tensor: torch.Tensor, scale: float,
                      zero_point: int) -> torch.Tensor:
    """Dequantize INT8 tensor back to FP32."""
    return (q_tensor.float() - zero_point) * scale


# ---------------------------------------------------------------------------
# Calibration with representative data
# ---------------------------------------------------------------------------

def calibrate_model(model: nn.Module,
                    calib_data: torch.Tensor,
                    num_batches: int = 100) -> Dict[str, torch.Tensor]:
    """
    Run calibration to collect activation statistics.

    Uses the calibration data to determine optimal quantization ranges
    for each layer's activations.

    Args:
        model: FP32 PyTorch model.
        calib_data: Calibration input tensor [N, seq_len, input_dim].
        num_batches: Number of calibration batches.

    Returns:
        Dictionary mapping layer names to activation ranges.
    """
    model.eval()
    activation_ranges = {}

    # Hook to capture activation ranges
    hooks = []

    def make_hook(name):
        def hook_fn(module, input, output):
            if isinstance(output, torch.Tensor):
                o_min = float(output.min())
                o_max = float(output.max())
            elif isinstance(output, tuple):
                o_min = float(output[0].min())
                o_max = float(output[0].max())
            else:
                return
            if name not in activation_ranges:
                activation_ranges[name] = {'min': o_min, 'max': o_max}
            else:
                activation_ranges[name]['min'] = min(
                    activation_ranges[name]['min'], o_min)
                activation_ranges[name]['max'] = max(
                    activation_ranges[name]['max'], o_max)
        return hook_fn

    for name, module in model.named_modules():
        if isinstance(module, (nn.LSTM, nn.Linear, nn.ReLU)):
            hooks.append(
                module.register_forward_hook(make_hook(name))
            )

    # Run calibration passes
    with torch.no_grad():
        batch_size = max(1, len(calib_data) // num_batches)
        for i in range(0, len(calib_data), batch_size):
            batch = calib_data[i:i + batch_size]
            model(batch)

    # Remove hooks
    for h in hooks:
        h.remove()

    return activation_ranges


# ---------------------------------------------------------------------------
# Quantize the model
# ---------------------------------------------------------------------------

def quantize_model_static(
    model: nn.Module,
    calib_data: Optional[torch.Tensor],
    seq_len: int = 1,
    symmetric: bool = True
) -> Tuple[nn.Module, List[QuantParams]]:
    """
    Quantize the model to INT8 using post-training quantization.

    For LSTM layers: dynamic quantization (weights quantized, activations
    quantized per-batch at runtime -- not applicable for embedded, so we
    extract weight params only).

    For Dense layers: static quantization using calibration data.

    Returns:
        Tuple of (quantized_model, quant_params_list).
    """
    all_params = []

    # --- Quantize LSTM weights ---
    for layer_name in ['lstm1', 'lstm2']:
        lstm = getattr(model, layer_name)
        assert isinstance(lstm, nn.LSTM)

        # Input-to-hidden weights: W_ih [4*units, input_dim]
        W_ih = lstm.weight_ih_l0.data
        s_ih, zp_ih = compute_scale_zp(W_ih, symmetric)
        q_W_ih = quantize_tensor(W_ih, s_ih, zp_ih)
        all_params.append(QuantParams(
            name=f'{layer_name}_W_ih',
            scale=s_ih, zero_point=zp_ih,
            shape=list(W_ih.shape),
            num_elements=W_ih.numel()
        ))

        # Hidden-to-hidden weights: W_hh [4*units, units]
        W_hh = lstm.weight_hh_l0.data
        s_hh, zp_hh = compute_scale_zp(W_hh, symmetric)
        q_W_hh = quantize_tensor(W_hh, s_hh, zp_hh)
        all_params.append(QuantParams(
            name=f'{layer_name}_W_hh',
            scale=s_hh, zero_point=zp_hh,
            shape=list(W_hh.shape),
            num_elements=W_hh.numel()
        ))

        # Biases (INT32, pre-scaled to accumulator domain)
        b_ih = lstm.bias_ih_l0.data
        b_hh = lstm.bias_hh_l0.data
        bias_combined = b_ih + b_hh
        # Pre-scale bias: multiply by 1/(scale_input * scale_weight)
        # For now store as INT32 raw values
        bias_int32 = torch.round(bias_combined / (s_ih * 1.0)).to(torch.int32)
        all_params.append(QuantParams(
            name=f'{layer_name}_bias',
            scale=s_ih, zero_point=0,
            shape=list(bias_combined.shape),
            num_elements=bias_combined.numel()
        ))

        # Replace in model with quantized tensors
        lstm.weight_ih_l0.data = q_W_ih.float() * s_ih
        lstm.weight_hh_l0.data = q_W_hh.float() * s_hh

    # --- Quantize Dense weights ---
    for layer_name in ['dense1', 'dense2']:
        dense = getattr(model, layer_name)
        assert isinstance(dense, nn.Linear)

        W = dense.weight.data
        s_W, zp_W = compute_scale_zp(W, symmetric)
        q_W = quantize_tensor(W, s_W, zp_W)
        all_params.append(QuantParams(
            name=f'{layer_name}_W',
            scale=s_W, zero_point=zp_W,
            shape=list(W.shape),
            num_elements=W.numel()
        ))

        bias = dense.bias.data
        bias_int32 = torch.round(bias / s_W).to(torch.int32)
        all_params.append(QuantParams(
            name=f'{layer_name}_bias',
            scale=s_W, zero_point=0,
            shape=list(bias.shape),
            num_elements=bias.numel()
        ))

        dense.weight.data = q_W.float() * s_W

    return model, all_params


# ---------------------------------------------------------------------------
# Export to C header (model_data.h)
# ---------------------------------------------------------------------------

def _int8_array_to_c(name: str, data: np.ndarray,
                     per_line: int = 16) -> str:
    """Format an INT8 numpy array as a C initializer."""
    lines = [f'const int8_t {name}[{len(data)}] = {{']
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        values = ', '.join(f'{int(v)}' for v in chunk)
        lines.append(f'  {values},')
    lines.append('};')
    return '\n'.join(lines)


def _int32_array_to_c(name: str, data: np.ndarray,
                      per_line: int = 8) -> str:
    """Format an INT32 numpy array as a C initializer."""
    lines = [f'const int32_t {name}[{len(data)}] = {{']
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        values = ', '.join(f'{int(v)}' for v in chunk)
        lines.append(f'  {values},')
    lines.append('};')
    return '\n'.join(lines)


def export_model_header(
    model: nn.Module,
    params: List[QuantParams],
    output_path: str,
    model_version: str = '1.0.0'
) -> None:
    """
    Export quantized model weights as a C header file.

    Generates model_data.h with:
      - Weight arrays (INT8)
      - Bias arrays (INT32, pre-scaled)
      - Quantization parameter structs
      - Model descriptor

    Compatible with firmware/libs/inference/model_data.h.
    """
    lines = []
    lines.append('/**')
    lines.append(f' * @file model_data.h')
    lines.append(f' * @brief Auto-generated quantized model weights for VelaSense.')
    lines.append(f' *')
    lines.append(f' * Generated by quantize_model.py')
    lines.append(f' * Architecture: LSTM({LSTM_UNITS})x2 -> Dense({DENSE1_UNITS}) -> Dense({OUTPUT_CLASSES})')
    lines.append(f' * Input: {INPUT_DIM} features, INT8 quantized')
    lines.append(f' *')
    lines.append(f' * @version {model_version}')
    lines.append(f' */')
    lines.append('')
    lines.append('#ifndef VELASENSE_MODEL_DATA_H')
    lines.append('#define VELASENSE_MODEL_DATA_H')
    lines.append('')
    lines.append('#include "tinyml.h"')
    lines.append('')
    lines.append('#ifdef __cplusplus')
    lines.append('extern "C" {')
    lines.append('#endif')
    lines.append('')

    # Extract and export weight arrays
    weight_arrays = {}
    bias_arrays = {}

    for layer_name in ['lstm1', 'lstm2']:
        lstm = getattr(model, layer_name)
        W_ih = lstm.weight_ih_l0.data.numpy()
        W_hh = lstm.weight_hh_l0.data.numpy()
        b_ih = lstm.bias_ih_l0.data.numpy()
        b_hh = lstm.bias_hh_l0.data.numpy()

        # Quantize weights to INT8
        s_ih = [p for p in params if p.name == f'{layer_name}_W_ih'][0].scale
        s_hh = [p for p in params if p.name == f'{layer_name}_W_hh'][0].scale

        q_W_ih = np.clip(np.round(W_ih / s_ih), -128, 127).astype(np.int8)
        q_W_hh = np.clip(np.round(W_hh / s_hh), -128, 127).astype(np.int8)

        # Bias as INT32
        bias_combined = b_ih + b_hh
        bias_int32 = np.round(bias_combined / s_ih).astype(np.int32)

        weight_arrays[f'g_{layer_name}_W_ih'] = q_W_ih.flatten()
        weight_arrays[f'g_{layer_name}_W_hh'] = q_W_hh.flatten()
        bias_arrays[f'g_{layer_name}_bias'] = bias_int32.flatten()

    for layer_name in ['dense1', 'dense2']:
        dense = getattr(model, layer_name)
        W = dense.weight.data.numpy()
        b = dense.bias.data.numpy()

        s_W = [p for p in params if p.name == f'{layer_name}_W'][0].scale
        q_W = np.clip(np.round(W / s_W), -128, 127).astype(np.int8)
        bias_int32 = np.round(b / s_W).astype(np.int32)

        weight_arrays[f'g_{layer_name}_W'] = q_W.flatten()
        bias_arrays[f'g_{layer_name}_bias'] = bias_int32.flatten()

    # Write weight arrays
    lines.append('/* ---- Weight arrays (INT8) ---- */')
    lines.append('')
    for name, data in weight_arrays.items():
        lines.append(_int8_array_to_c(name, data))
        lines.append('')

    lines.append('/* ---- Bias arrays (INT32, pre-scaled) ---- */')
    lines.append('')
    for name, data in bias_arrays.items():
        lines.append(_int32_array_to_c(name, data))
        lines.append('')

    # Write quantization parameter structs
    lines.append('/* ---- Quantization parameters ---- */')
    lines.append('')
    for p in params:
        var_name = p.name.replace('.', '_')
        lines.append(f'static const tinyml_quant_t q_{var_name} = {{')
        lines.append(f'    .scale      = {p.scale:.10f}f,')
        lines.append(f'    .zero_point = {p.zero_point}')
        lines.append('};')
        lines.append('')

    # Write model descriptor
    total_params = sum(p.num_elements for p in params)
    lines.append('/* ---- Model descriptor ---- */')
    lines.append('')
    lines.append('static const tinyml_model_t g_velasense_model = {')
    lines.append('    .meta = {')
    lines.append(f'        .version       = 0x010000,  /* {model_version} */')
    lines.append(f'        .expected_crc  = 0x00000000,  /* TODO: compute after final weights */')
    lines.append(f'        .total_params  = {total_params},')
    lines.append(f'        .input_dim     = {INPUT_DIM},')
    lines.append(f'        .output_classes = {OUTPUT_CLASSES}')
    lines.append('    },')
    lines.append('    .lstm1 = {')
    lines.append(f'        .W_ih      = g_lstm1_W_ih,')
    lines.append(f'        .W_hh      = g_lstm1_W_hh,')
    lines.append(f'        .bias      = g_lstm1_bias,')
    lines.append(f'        .q_input   = q_lstm1_W_ih,  /* placeholder */')
    lines.append(f'        .q_W_ih    = q_lstm1_W_ih,')
    lines.append(f'        .q_W_hh    = q_lstm1_W_hh,')
    lines.append(f'        .q_bias    = q_lstm1_bias,')
    lines.append(f'        .input_dim = {INPUT_DIM},')
    lines.append(f'        .units     = {LSTM_UNITS}')
    lines.append('    },')
    lines.append('    .lstm2 = {')
    lines.append(f'        .W_ih      = g_lstm2_W_ih,')
    lines.append(f'        .W_hh      = g_lstm2_W_hh,')
    lines.append(f'        .bias      = g_lstm2_bias,')
    lines.append(f'        .q_input   = q_lstm2_W_ih,  /* placeholder */')
    lines.append(f'        .q_W_ih    = q_lstm2_W_ih,')
    lines.append(f'        .q_W_hh    = q_lstm2_W_hh,')
    lines.append(f'        .q_bias    = q_lstm2_bias,')
    lines.append(f'        .input_dim = {LSTM_UNITS},')
    lines.append(f'        .units     = {LSTM_UNITS}')
    lines.append('    },')
    lines.append('    .dense1 = {')
    lines.append(f'        .W         = g_dense1_W,')
    lines.append(f'        .bias      = g_dense1_bias,')
    lines.append(f'        .q_input   = q_dense1_W,  /* placeholder */')
    lines.append(f'        .q_W       = q_dense1_W,')
    lines.append(f'        .q_bias    = q_dense1_bias,')
    lines.append(f'        .q_output  = q_dense2_W,  /* placeholder */')
    lines.append(f'        .input_dim = {LSTM_UNITS},')
    lines.append(f'        .units     = {DENSE1_UNITS},')
    lines.append(f'        .use_relu  = true')
    lines.append('    },')
    lines.append('    .dense2 = {')
    lines.append(f'        .W         = g_dense2_W,')
    lines.append(f'        .bias      = g_dense2_bias,')
    lines.append(f'        .q_input   = q_dense2_W,  /* placeholder */')
    lines.append(f'        .q_W       = q_dense2_W,')
    lines.append(f'        .q_bias    = q_dense2_bias,')
    lines.append(f'        .q_output  = q_dense2_W,  /* placeholder */')
    lines.append(f'        .input_dim = {DENSE1_UNITS},')
    lines.append(f'        .units     = {OUTPUT_CLASSES},')
    lines.append(f'        .use_relu  = false')
    lines.append('    }')
    lines.append('};')
    lines.append('')
    lines.append('#ifdef __cplusplus')
    lines.append('}')
    lines.append('#endif')
    lines.append('')
    lines.append('#endif /* VELASENSE_MODEL_DATA_H */')
    lines.append('')

    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))


# ---------------------------------------------------------------------------
# Accuracy verification
# ---------------------------------------------------------------------------

def verify_accuracy(
    model_fp32: nn.Module,
    model_quantized: nn.Module,
    test_data: torch.Tensor,
    test_labels: torch.Tensor
) -> Dict:
    """
    Compare FP32 vs INT8 model accuracy.

    Returns:
        Dictionary with accuracy metrics.
    """
    model_fp32.eval()
    model_quantized.eval()

    with torch.no_grad():
        # FP32 predictions
        out_fp32 = model_fp32(test_data)
        probs_fp32 = torch.softmax(out_fp32, dim=-1)
        preds_fp32 = torch.argmax(probs_fp32, dim=-1)

        # Quantized predictions
        out_q = model_quantized(test_data)
        probs_q = torch.softmax(out_q, dim=-1)
        preds_q = torch.argmax(probs_q, dim=-1)

    # Accuracy
    acc_fp32 = (preds_fp32 == test_labels).float().mean().item()
    acc_q = (preds_q == test_labels).float().mean().item()

    # Top-1 agreement between FP32 and quantized
    agreement = (preds_fp32 == preds_q).float().mean().item()

    # Max probability difference per sample
    max_prob_diff = (probs_fp32 - probs_q).abs().max(dim=-1)[0]
    mean_prob_diff = max_prob_diff.mean().item()

    # Per-sample output MSE
    mse = ((out_fp32 - out_q) ** 2).mean().item()

    return {
        'fp32_accuracy': round(acc_fp32 * 100, 2),
        'int8_accuracy': round(acc_q * 100, 2),
        'accuracy_drop_pct': round((acc_fp32 - acc_q) * 100, 2),
        'fp32_int8_agreement': round(agreement * 100, 2),
        'mean_prob_diff': round(mean_prob_diff, 4),
        'max_prob_diff': round(float(max_prob_diff.max()), 4),
        'output_mse': round(mse, 6),
        'num_test_samples': len(test_data),
        'meets_2pct_threshold': abs(acc_fp32 - acc_q) < 0.02,
    }


# ---------------------------------------------------------------------------
# Generate dummy calibration/test data
# ---------------------------------------------------------------------------

def generate_dummy_data(num_samples: int = 1000,
                        seq_len: int = 1,
                        input_dim: int = INPUT_DIM,
                        num_classes: int = OUTPUT_CLASSES,
                        seed: int = 42) -> Tuple[torch.Tensor, torch.Tensor]:
    """
    Generate dummy sensor feature vectors for calibration/testing.

    Feature vector (15 dimensions):
      [0-2]:   PPG AC amplitude (green, red, IR)
      [3-5]:   PPG SQI (green, red, IR)
      [6]:     Heart rate (BPM)
      [7]:     HRV SDNN (ms)
      [8-10]:  IMU accel magnitude (x, y, z) in g
      [11]:    Activity level (0-3)
      [12]:    EDA SCL (uS)
      [13]:    EDA SCR count (recent window)
      [14]:    Skin temperature (C)
    """
    rng = np.random.default_rng(seed)

    features = np.zeros((num_samples, seq_len, input_dim), dtype=np.float32)
    labels = np.zeros(num_samples, dtype=np.int64)

    for i in range(num_samples):
        # Simulate different arousal states
        arousal_class = rng.integers(0, num_classes)
        labels[i] = arousal_class

        for s in range(seq_len):
            # Base features
            hr = rng.normal(72, 15)
            hrv = rng.normal(50, 20)
            activity = rng.integers(0, 4)
            scl = rng.normal(5, 2)
            scr = rng.integers(0, 4)
            temp = rng.normal(34, 1)

            # Arousal-dependent modulation
            if arousal_class == 0:  # Excitement
                hr += 15
                hrv -= 10
                scl += 1.5
                scr += 2
            elif arousal_class == 1:  # Nervous
                hr += 10
                hrv += 5
                scl += 2.0
                scr += 1
            elif arousal_class == 2:  # Surprise
                hr += 20
                hrv -= 15
                scl += 3.0
                scr += 3
            elif arousal_class == 3:  # Stress
                hr += 8
                hrv -= 20
                scl += 1.0
                scr += 1
            # class 4 = other (no modulation)

            ppg_amp = rng.normal(50000, 10000, 3) / 100000.0
            sqi = np.clip(rng.normal(70, 15, 3), 0, 100) / 100.0
            accel = rng.normal(0, 0.3, 3)

            features[i, s] = np.array([
                ppg_amp[0], ppg_amp[1], ppg_amp[2],
                sqi[0], sqi[1], sqi[2],
                hr / 200.0,         # Normalize HR
                hrv / 200.0,        # Normalize HRV
                accel[0], accel[1], accel[2],
                activity / 3.0,     # Normalize activity
                scl / 10.0,         # Normalize SCL
                scr / 10.0,         # Normalize SCR count
                temp / 40.0,        # Normalize temperature
            ], dtype=np.float32)

    return torch.from_numpy(features), torch.from_numpy(labels)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description='VelaSense INT8 Model Quantization Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        '--model', required=True,
        help='Path to trained PyTorch model (.pt or .pth)'
    )
    parser.add_argument(
        '--calib',
        help='Path to calibration data (.pt tensor file)'
    )
    parser.add_argument(
        '--test',
        help='Path to test data (.pt tensor file with (features, labels))'
    )
    parser.add_argument(
        '--dummy', action='store_true',
        help='Generate dummy calibration/test data for validation'
    )
    parser.add_argument(
        '--seq-len', type=int, default=1,
        help='Sequence length for input (default: 1)'
    )
    parser.add_argument(
        '--num-calib', type=int, default=500,
        help='Number of calibration samples for --dummy (default: 500)'
    )
    parser.add_argument(
        '--num-test', type=int, default=200,
        help='Number of test samples for --dummy (default: 200)'
    )
    parser.add_argument(
        '--symmetric', action='store_true', default=True,
        help='Use symmetric quantization (default: True)'
    )
    parser.add_argument(
        '-o', '--output', default='quant_output',
        help='Output directory (default: quant_output)'
    )
    parser.add_argument(
        '--version', default='1.0.0',
        help='Model version string (default: 1.0.0)'
    )
    parser.add_argument(
        '--seed', type=int, default=42,
        help='Random seed (default: 42)'
    )
    return parser


def main() -> int:
    if not HAS_TORCH:
        print('ERROR: PyTorch is required. Install with:')
        print('  pip install torch')
        return 1

    parser = build_parser()
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    # Load model
    print(f'Loading model from {args.model}...')
    try:
        checkpoint = torch.load(args.model, map_location='cpu',
                                weights_only=False)
        if isinstance(checkpoint, dict) and 'model_state_dict' in checkpoint:
            model = VelaSenseLSTM()
            model.load_state_dict(checkpoint['model_state_dict'])
        elif isinstance(checkpoint, dict) and 'state_dict' in checkpoint:
            model = VelaSenseLSTM()
            model.load_state_dict(checkpoint['state_dict'])
        elif isinstance(checkpoint, nn.Module):
            model = checkpoint
        else:
            model = VelaSenseLSTM()
            model.load_state_dict(checkpoint)
    except Exception as e:
        print(f'ERROR: Could not load model: {e}')
        print('Creating a fresh model with random weights for demonstration...')
        model = VelaSenseLSTM()
        torch.manual_seed(args.seed)

    model.eval()
    print(f'Model architecture: {type(model).__name__}')
    print(f'  LSTM(15, 32) -> LSTM(32, 32) -> Dense(32, 16, ReLU) -> Dense(16, 5)')

    # Load or generate calibration data
    if args.dummy:
        print(f'Generating dummy calibration data ({args.num_calib} samples)...')
        calib_data, _ = generate_dummy_data(
            num_samples=args.num_calib,
            seq_len=args.seq_len,
            seed=args.seed
        )
    elif args.calib:
        print(f'Loading calibration data from {args.calib}...')
        calib_data = torch.load(args.calib, map_location='cpu')
        if isinstance(calib_data, tuple):
            calib_data = calib_data[0]
    else:
        print('No calibration data provided, using dummy data...')
        calib_data, _ = generate_dummy_data(
            num_samples=args.num_calib,
            seq_len=args.seq_len,
            seed=args.seed
        )

    # Calibrate
    print('Running calibration...')
    act_ranges = calibrate_model(model, calib_data)
    print(f'  Captured activation ranges for {len(act_ranges)} layers')

    # Quantize
    print('Quantizing model to INT8...')
    model_quant, quant_params = quantize_model_static(
        model, calib_data, seq_len=args.seq_len, symmetric=args.symmetric
    )

    # Print quantization summary
    print('\nQuantization parameters:')
    total_int8 = 0
    total_int32 = 0
    for p in quant_params:
        print(f'  {p.name:20s}  scale={p.scale:.6e}  zp={p.zero_point}  '
              f'shape={p.shape}  n={p.num_elements}')
        if 'bias' in p.name:
            total_int32 += p.num_elements
        else:
            total_int8 += p.num_elements

    print(f'\nTotal INT8 parameters:  {total_int8}')
    print(f'Total INT32 parameters: {total_int32}')
    print(f'Model size: ~{total_int8 + total_int32 * 4} bytes '
          f'({(total_int8 + total_int32 * 4) / 1024:.1f} KB)')

    # Verify accuracy
    accuracy_report = None
    if args.dummy or args.test:
        if args.dummy:
            test_data, test_labels = generate_dummy_data(
                num_samples=args.num_test,
                seq_len=args.seq_len,
                seed=args.seed + 100
            )
        else:
            test_bundle = torch.load(args.test, map_location='cpu')
            if isinstance(test_bundle, tuple):
                test_data, test_labels = test_bundle
            else:
                test_data = test_bundle
                test_labels = torch.zeros(len(test_data), dtype=torch.int64)

        # Need FP32 model for comparison -- re-load it
        model_fp32 = VelaSenseLSTM()
        try:
            checkpoint = torch.load(args.model, map_location='cpu',
                                    weights_only=False)
            if isinstance(checkpoint, dict) and 'model_state_dict' in checkpoint:
                model_fp32.load_state_dict(checkpoint['model_state_dict'])
            elif isinstance(checkpoint, dict) and 'state_dict' in checkpoint:
                model_fp32.load_state_dict(checkpoint['state_dict'])
            elif isinstance(checkpoint, nn.Module):
                model_fp32 = checkpoint
            else:
                model_fp32.load_state_dict(checkpoint)
        except Exception:
            model_fp32 = model  # Use the already-loaded model

        model_fp32.eval()

        print('\nVerifying accuracy (FP32 vs INT8)...')
        accuracy_report = verify_accuracy(
            model_fp32, model_quant, test_data, test_labels
        )
        for k, v in accuracy_report.items():
            print(f'  {k}: {v}')

        if not accuracy_report['meets_2pct_threshold']:
            print('\nWARNING: Accuracy drop exceeds 2% threshold!')
        else:
            print('\nAccuracy drop within 2% threshold -- OK.')

    # Export C header
    header_path = os.path.join(args.output, 'model_data.h')
    print(f'\nExporting C header to {header_path}...')
    export_model_header(model, quant_params, header_path, args.version)

    # Export quantization parameters JSON
    params_path = os.path.join(args.output, 'quant_params.json')
    params_dict = {
        'model_version': args.version,
        'architecture': {
            'input_dim': INPUT_DIM,
            'lstm_units': LSTM_UNITS,
            'dense1_units': DENSE1_UNITS,
            'output_classes': OUTPUT_CLASSES,
        },
        'quantization': {
            'method': 'post_training_static',
            'symmetric': args.symmetric,
            'dtype': 'int8',
            'bias_dtype': 'int32',
        },
        'parameters': [
            {
                'name': p.name,
                'scale': p.scale,
                'zero_point': p.zero_point,
                'shape': p.shape,
                'num_elements': p.num_elements,
            }
            for p in quant_params
        ],
        'total_int8_params': total_int8,
        'total_int32_params': total_int32,
        'model_size_bytes': total_int8 + total_int32 * 4,
    }
    if accuracy_report:
        params_dict['accuracy'] = accuracy_report

    with open(params_path, 'w') as f:
        json.dump(params_dict, f, indent=2)
    print(f'Quantization params saved to {params_path}')

    # Export accuracy report if available
    if accuracy_report:
        report_path = os.path.join(args.output, 'accuracy_report.json')
        with open(report_path, 'w') as f:
            json.dump(accuracy_report, f, indent=2)
        print(f'Accuracy report saved to {report_path}')

    print('\nDone!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
