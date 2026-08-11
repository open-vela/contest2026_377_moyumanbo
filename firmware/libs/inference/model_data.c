/**
 * @file model_data.c
 * @brief Placeholder model weights for the VelaSense arousal classifier.
 *
 * All arrays have the correct dimensions for the 2-layer LSTM(32) ->
 * Dense(16) -> Dense(5) architecture.  Values are zero-initialised
 * placeholders -- replace with trained INT8 weights before deployment.
 *
 * The g_velasense_model descriptor at the bottom wires everything together
 * and carries the expected CRC-32 (update after inserting real weights).
 *
 * @version 1.0.0
 */

#include "model_data.h"

/* =========================================================================
 * LSTM Layer 1  (input_dim = 15, units = 32, gates = 4)
 * Weight layout: [gate_offset + row * input_dim + col]
 *   gate order: forget (f), input (i), cell (g), output (o)
 *   W_ih shape: [128 x 15]  => 1920 elements
 *   W_hh shape: [128 x 32]  => 4096 elements
 *   bias  shape: [128]       => 128  elements
 * ========================================================================= */

const int8_t g_lstm1_W_ih[LSTM1_W_IH_SIZE] = { 0 };

const int8_t g_lstm1_W_hh[LSTM1_W_HH_SIZE] = { 0 };

const int32_t g_lstm1_bias[LSTM1_BIAS_SIZE] = { 0 };

/* =========================================================================
 * LSTM Layer 2  (input_dim = 32, units = 32, gates = 4)
 *   W_ih shape: [128 x 32]  => 4096 elements
 *   W_hh shape: [128 x 32]  => 4096 elements
 *   bias  shape: [128]       => 128  elements
 * ========================================================================= */

const int8_t g_lstm2_W_ih[LSTM2_W_IH_SIZE] = { 0 };

const int8_t g_lstm2_W_hh[LSTM2_W_HH_SIZE] = { 0 };

const int32_t g_lstm2_bias[LSTM2_BIAS_SIZE] = { 0 };

/* =========================================================================
 * Dense Layer 1  (input_dim = 32, units = 16, ReLU)
 *   W    shape: [16 x 32] => 512 elements
 *   bias shape: [16]      => 16  elements
 * ========================================================================= */

const int8_t g_dense1_W[DENSE1_W_SIZE] = { 0 };

const int32_t g_dense1_bias[DENSE1_BIAS_SIZE] = { 0 };

/* =========================================================================
 * Dense Layer 2  (input_dim = 16, units = 5, linear -- before softmax)
 *   W    shape: [5 x 16] => 80 elements
 *   bias shape: [5]      =>  5 elements
 * ========================================================================= */

const int8_t g_dense2_W[DENSE2_W_SIZE] = { 0 };

const int32_t g_dense2_bias[DENSE2_BIAS_SIZE] = { 0 };

/* =========================================================================
 * Quantization parameters
 *
 * Symmetric INT8 quantization:  real = (q - zp) * scale
 *   - All zero_points are 0 (symmetric around zero).
 *   - scales are set to reasonable defaults for physiological signals.
 *
 * IMPORTANT: After training, replace these with the actual per-tensor
 * scales exported by your quantisation toolchain (e.g. TFLite converter).
 * ========================================================================= */

/* --- LSTM 1 input quantization (covers the 15 raw features) -------------- */
static const tinyml_quant_t q_lstm1_input = {
    .scale      = 0.03921569f,   /* ~1/25.5  -- assumes input in [-5, +5] */
    .zero_point = 0
};

static const tinyml_quant_t q_lstm1_W_ih = {
    .scale      = 0.00392157f,   /* ~1/255 */
    .zero_point = 0
};

static const tinyml_quant_t q_lstm1_W_hh = {
    .scale      = 0.00392157f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm1_bias = {
    .scale      = 0.0001f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm1_hidden = {
    .scale      = 0.01960784f,   /* ~1/51 -- hidden state in [-2.5, +2.5] */
    .zero_point = 0
};

static const tinyml_quant_t q_lstm1_cell = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm1_output = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

/* --- LSTM 2 -------------------------------------------------------------- */
static const tinyml_quant_t q_lstm2_input = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm2_W_ih = {
    .scale      = 0.00392157f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm2_W_hh = {
    .scale      = 0.00392157f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm2_bias = {
    .scale      = 0.0001f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm2_hidden = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm2_cell = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

static const tinyml_quant_t q_lstm2_output = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

/* --- Dense 1 (32 -> 16, ReLU) ------------------------------------------- */
static const tinyml_quant_t q_dense1_input = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

static const tinyml_quant_t q_dense1_W = {
    .scale      = 0.00392157f,
    .zero_point = 0
};

static const tinyml_quant_t q_dense1_bias = {
    .scale      = 0.0001f,
    .zero_point = 0
};

static const tinyml_quant_t q_dense1_output = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

/* --- Dense 2 (16 -> 5, linear) ------------------------------------------ */
static const tinyml_quant_t q_dense2_input = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

static const tinyml_quant_t q_dense2_W = {
    .scale      = 0.00392157f,
    .zero_point = 0
};

static const tinyml_quant_t q_dense2_bias = {
    .scale      = 0.0001f,
    .zero_point = 0
};

static const tinyml_quant_t q_dense2_output = {
    .scale      = 0.01960784f,
    .zero_point = 0
};

/* =========================================================================
 * Top-level model descriptor
 *
 * expected_crc must be recomputed after replacing placeholder weights.
 * tinyml_init() will reject the model if the CRC does not match.
 *
 * To compute the CRC at build time, run:
 *   python3 -c "
 *     import struct, binascii
 *     blobs = [ ... ]   # concatenate all weight/bias byte arrays
 *     crc = binascii.crc32(blobs) & 0xffffffff
 *     print(f'0x{crc:08X}')
 *   "
 *
 * For the zero-filled placeholder the CRC is 0x00000000.
 * ========================================================================= */

const tinyml_model_t g_velasense_model = {
    /* ---- metadata ---- */
    .meta = {
        .version       = (TINYML_VERSION_MAJOR << 16)
                       | (TINYML_VERSION_MINOR <<  8)
                       | (TINYML_VERSION_PATCH),
        .expected_crc  = 0x00000000U,   /* TODO: update after inserting real weights */
        .total_params  = LSTM1_W_IH_SIZE + LSTM1_W_HH_SIZE + LSTM1_BIAS_SIZE
                       + LSTM2_W_IH_SIZE + LSTM2_W_HH_SIZE + LSTM2_BIAS_SIZE
                       + DENSE1_W_SIZE   + DENSE1_BIAS_SIZE
                       + DENSE2_W_SIZE   + DENSE2_BIAS_SIZE,
        .input_dim     = MODEL_INPUT_DIM,
        .output_classes = MODEL_OUTPUT_CLASSES,
    },

    /* ---- LSTM 1 ---- */
    .lstm1 = {
        .W_ih      = g_lstm1_W_ih,
        .W_hh      = g_lstm1_W_hh,
        .bias      = g_lstm1_bias,
        .q_input   = q_lstm1_input,
        .q_W_ih    = q_lstm1_W_ih,
        .q_W_hh    = q_lstm1_W_hh,
        .q_bias    = q_lstm1_bias,
        .q_hidden  = q_lstm1_hidden,
        .q_cell    = q_lstm1_cell,
        .q_output  = q_lstm1_output,
        .input_dim = MODEL_INPUT_DIM,
        .units     = MODEL_LSTM_UNITS,
    },

    /* ---- LSTM 2 ---- */
    .lstm2 = {
        .W_ih      = g_lstm2_W_ih,
        .W_hh      = g_lstm2_W_hh,
        .bias      = g_lstm2_bias,
        .q_input   = q_lstm2_input,
        .q_W_ih    = q_lstm2_W_ih,
        .q_W_hh    = q_lstm2_W_hh,
        .q_bias    = q_lstm2_bias,
        .q_hidden  = q_lstm2_hidden,
        .q_cell    = q_lstm2_cell,
        .q_output  = q_lstm2_output,
        .input_dim = MODEL_LSTM_UNITS,
        .units     = MODEL_LSTM_UNITS,
    },

    /* ---- Dense 1 (32 -> 16, ReLU) ---- */
    .dense1 = {
        .W          = g_dense1_W,
        .bias       = g_dense1_bias,
        .q_input    = q_dense1_input,
        .q_W        = q_dense1_W,
        .q_bias     = q_dense1_bias,
        .q_output   = q_dense1_output,
        .input_dim  = MODEL_LSTM_UNITS,
        .units      = MODEL_DENSE1_UNITS,
        .use_relu   = true,
    },

    /* ---- Dense 2 (16 -> 5, linear) ---- */
    .dense2 = {
        .W          = g_dense2_W,
        .bias       = g_dense2_bias,
        .q_input    = q_dense2_input,
        .q_W        = q_dense2_W,
        .q_bias     = q_dense2_bias,
        .q_output   = q_dense2_output,
        .input_dim  = MODEL_DENSE1_UNITS,
        .units      = MODEL_OUTPUT_CLASSES,
        .use_relu   = false,
    },
};
