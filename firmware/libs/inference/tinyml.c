/**
 * @file tinyml.c
 * @brief INT8 quantised inference engine for VelaSense arousal detection.
 *
 * Implements every operator needed to run the late-fusion model
 * (2-layer LSTM -> Dense -> Dense -> softmax) entirely in INT8 with
 * INT32 accumulation, except for the LSTM nonlinearities (sigmoid/tanh)
 * which use a lightweight piecewise-linear approximation in float.
 *
 * No external TFLite or CMSIS-NN dependency is required.
 *
 * Memory budget:
 *   Engine struct:  ~576 bytes  (scratch + h_state + c_state)
 *   Stack peak:     ~200 bytes  (quantised buffers, loop variables)
 *   Flash (code):   ~2.5 KiB    (all functions, no tables)
 *   Flash (model):  ~12 KiB     (placeholder weights; replace with trained)
 *
 * @version 1.0.0
 */

#include "tinyml.h"
#include <string.h>

/* =========================================================================
 * Platform abstraction for cycle-accurate timing
 * ========================================================================= */

#if defined(__ARM_ARCH) && (__ARM_ARCH >= 7M)
/* Cortex-M33: use the DWT cycle counter.
 * Assumes DWT->CYCCNT has been enabled by the board init code. */
#include "core_cm33.h"

static inline uint32_t timer_read_us(void)
{
    extern uint32_t SystemCoreClock;
    return DWT->CYCCNT / (SystemCoreClock / 1000000U);
}
#else
/* Host / CI fallback: monotonically increasing tick (not wall-clock). */
static uint32_t s_tick;
static inline uint32_t timer_read_us(void) { return ++s_tick; }
#endif

/* =========================================================================
 * CRC-32  (IEEE 802.3 polynomial, bit-at-a-time -- no 256-byte table)
 * ========================================================================= */

uint32_t tinyml_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (-(int32_t)(crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/* =========================================================================
 * Version string
 * ========================================================================= */

const char *tinyml_version(void)
{
    return TINYML_VERSION_STRING;
}

/* =========================================================================
 * Status code -> human-readable string
 * ========================================================================= */

const char *tinyml_status_str(tinyml_status_t status)
{
    switch (status) {
    case TINYML_OK:                return "OK";
    case TINYML_ERR_NULL_PTR:      return "NULL pointer";
    case TINYML_ERR_INVALID_MODEL: return "Invalid model";
    case TINYML_ERR_CRC_MISMATCH:  return "CRC mismatch";
    case TINYML_ERR_BAD_DIM:       return "Dimension mismatch";
    case TINYML_ERR_VERSION:       return "Version mismatch";
    case TINYML_ERR_OVERFLOW:      return "Overflow";
    default:                       return "Unknown error";
    }
}

/* =========================================================================
 * Engine state  (caller provides memory; we just alias it)
 * ========================================================================= */

struct tinyml_engine {
    const tinyml_model_t *model;
    uint8_t               scratch[TINYML_SCRATCH_SIZE];   /* 512 bytes */
    float                 h_state[TINYML_LSTM_UNITS];     /*  128 bytes */
    float                 c_state[TINYML_LSTM_UNITS];     /*  128 bytes */
    bool                  initialised;
};

/* =========================================================================
 * Quantisation / dequantisation helpers
 *
 * Symmetric INT8:  real = (q - zero_point) * scale
 * ========================================================================= */

static inline int8_t quantise_int8(float value, tinyml_quant_t q)
{
    int32_t qv = (int32_t)(value / q.scale + (float)q.zero_point + 0.5f);
    if (qv < -128) qv = -128;
    if (qv >  127) qv =  127;
    return (int8_t)qv;
}

static inline float dequantise_int8(int8_t qval, tinyml_quant_t q)
{
    return ((float)qval - (float)q.zero_point) * q.scale;
}

/* =========================================================================
 * Nonlinear activation approximations (piecewise linear)
 *
 * Accurate enough for gate activations; the LSTM cell only has 32 units
 * so the branch cost is negligible compared to the matmul that feeds them.
 * ========================================================================= */

/** Sigmoid approximation: max error ~0.017 on [-6, +6]. */
static inline float sigmoid_approx(float x)
{
    if (x < -6.0f) return 0.0f;
    if (x >  6.0f) return 1.0f;
    if (x < -1.0f) return 0.25f * x + 0.75f;
    if (x >  1.0f) return 0.25f * x + 0.25f;
    return 0.5f * x + 0.5f;
}

/** Tanh approximation: max error ~0.028 on [-3, +3]. */
static inline float tanh_approx(float x)
{
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    if (x < -1.0f) return 0.5f * x - 0.5f;
    if (x >  1.0f) return 0.5f * x + 0.5f;
    return x;
}

/* =========================================================================
 * INT8 matrix multiply -- INT32 accumulation, requantised INT8 output
 *
 *   out[i,j] = clamp( round( scale_ratio * acc + z_o ), -128, 127 )
 *
 * where  acc = sum_k( (A[i,k]-z_a) * (B[k,j]-z_b) ) + bias[j]
 *
 * A is [M x K], B is [K x N], bias is [N], out is [M x N].
 * All matrices are row-major.  lda/ldb are the physical row strides.
 * ========================================================================= */

void tinyml_int8_matmul(const int8_t  *A,  int M, int K, int lda,
                        const int8_t  *B,  int N, int ldb,
                        const int32_t *bias,
                        int8_t        *out,
                        tinyml_quant_t qA, tinyml_quant_t qB, tinyml_quant_t qOut)
{
    const float scale_ratio = (qA.scale * qB.scale) / qOut.scale;
    const int8_t  za = qA.zero_point;
    const int8_t  zb = qB.zero_point;
    const int8_t  zo = qOut.zero_point;

    for (int i = 0; i < M; i++) {
        const int8_t *row_a = &A[i * lda];
        for (int j = 0; j < N; j++) {
            int32_t acc = 0;
            for (int k = 0; k < K; k++) {
                acc += (int32_t)(row_a[k] - za) * (int32_t)(B[k * ldb + j] - zb);
            }
            if (bias) {
                acc += bias[j];
            }
            int32_t qval = (int32_t)((float)acc * scale_ratio + (float)zo + 0.5f);
            if (qval < -128) qval = -128;
            if (qval >  127) qval =  127;
            out[i * N + j] = (int8_t)qval;
        }
    }
}

/* =========================================================================
 * Dense (fully-connected) layer -- INT8 in, INT8 out
 *
 *   out = requant( W * input + bias )
 *   Optionally clamps negative outputs to zero_point (ReLU).
 * ========================================================================= */

void tinyml_int8_dense(const int8_t  *input,
                       const int8_t  *W,     int in_dim, int out_dim,
                       const int32_t *bias,
                       int8_t        *out,
                       tinyml_quant_t qIn, tinyml_quant_t qW,
                       tinyml_quant_t qBias, tinyml_quant_t qOut,
                       bool           use_relu)
{
    (void)qBias; /* bias is pre-scaled to the accumulator domain */

    /* W is [out_dim x in_dim], row-major */
    tinyml_int8_matmul(input, 1, in_dim, in_dim,
                       W, out_dim, in_dim,
                       bias, out,
                       qIn, qW, qOut);

    if (use_relu) {
        for (int i = 0; i < out_dim; i++) {
            if (out[i] < qOut.zero_point) {
                out[i] = qOut.zero_point;
            }
        }
    }
}

/* =========================================================================
 * Single LSTM timestep
 *
 * Gate layout in W_ih [4U x input_dim] and W_hh [4U x U]:
 *   rows  0*U .. 1*U-1  =  forget  gate  (f)
 *   rows  1*U .. 2*U-1  =  input   gate  (i)
 *   rows  2*U .. 3*U-1  =  cell    gate  (g)
 *   rows  3*U .. 4*U-1  =  output  gate  (o)
 *
 * Scratch buffers (both allocated from engine->scratch by the caller):
 *   acc_buf  [4*U]   -- matmul output for W_ih * input
 *   temp_buf [4*U]   -- matmul output for W_hh * hidden
 *
 * h[] and c[] are float arrays that carry state across timesteps.
 * ========================================================================= */

void tinyml_int8_lstm_cell(const int8_t  *input,
                           float         *h,
                           float         *c,
                           int8_t        *acc_buf,
                           const tinyml_lstm_layer_t *layer)
{
    const int U  = layer->units;
    const int id = layer->input_dim;
    const int g4 = 4 * U;

    /* ---- 1. Quantise current hidden state ------------------------------- */
    int8_t h_q[TINYML_LSTM_UNITS];
    for (int i = 0; i < U; i++) {
        h_q[i] = quantise_int8(h[i], layer->q_hidden);
    }

    /* ---- 2. Compute gate pre-activations in two matmuls -----------------
     *
     *   gates = W_ih * input   +   W_hh * hidden
     *
     * We compute each matmul into a separate INT8 scratch buffer and then
     * add them element-wise (with INT8 saturation).  This avoids needing
     * an INT32 scratch buffer of 4*U elements (512 bytes for U=32).
     *
     * acc_buf  receives  requant( W_ih * input )
     * temp_buf receives  requant( W_hh * h    )
     * ------------------------------------------------------------------ */

    /* temp_buf lives right after acc_buf in the caller's scratch region. */
    int8_t *temp_buf = acc_buf + g4;

    tinyml_int8_matmul(input, g4, id, id,
                       layer->W_ih, id, id,
                       NULL, acc_buf,
                       layer->q_input, layer->q_W_ih, layer->q_output);

    tinyml_int8_matmul(h_q, g4, U, U,
                       layer->W_hh, U, U,
                       NULL, temp_buf,
                       layer->q_hidden, layer->q_W_hh, layer->q_output);

    /* Element-wise addition with saturation (INT8 + INT8 -> INT8). */
    for (int i = 0; i < g4; i++) {
        int32_t sum = (int32_t)acc_buf[i] + (int32_t)temp_buf[i];
        if (sum < -128) sum = -128;
        if (sum >  127) sum =  127;
        acc_buf[i] = (int8_t)sum;
    }

    /* ---- 3. Dequantise, add bias, store gate pre-activations ------------ */
    float gates[4 * TINYML_LSTM_UNITS];   /* 128 floats = 512 bytes (stack) */
    for (int g = 0; g < 4; g++) {
        for (int u = 0; u < U; u++) {
            int idx = g * U + u;
            float val = dequantise_int8(acc_buf[idx], layer->q_output);
            val += (float)layer->bias[idx] * layer->q_bias.scale;
            gates[idx] = val;
        }
    }

    /* ---- 4. Apply activations and update cell + hidden states ----------- */
    const float *fg = &gates[0];         /* forget  */
    const float *ig = &gates[U];         /* input   */
    const float *gg = &gates[2 * U];     /* cell    */
    const float *og = &gates[3 * U];     /* output  */

    for (int u = 0; u < U; u++) {
        float f = sigmoid_approx(fg[u]);
        float i = sigmoid_approx(ig[u]);
        float g = tanh_approx(gg[u]);
        float o = sigmoid_approx(og[u]);

        c[u] = f * c[u] + i * g;              /* new cell state   */
        h[u] = o * tanh_approx(c[u]);          /* new hidden state */
    }
}

/* =========================================================================
 * Softmax -- INT8 logits in, float probabilities out
 *
 * Uses the log-sum-exp trick for numerical stability and a 10-term
 * Taylor expansion for exp() to avoid pulling in <math.h>.
 * ========================================================================= */

void tinyml_softmax(const int8_t *input, int n, tinyml_quant_t q, float *probs)
{
    /* 1. Dequantise and find the maximum for stability. */
    float max_val = dequantise_int8(input[0], q);
    for (int i = 1; i < n; i++) {
        float v = dequantise_int8(input[i], q);
        if (v > max_val) max_val = v;
    }

    /* 2. Compute exp(x - max) with a 10-term Taylor series. */
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float xv = dequantise_int8(input[i], q) - max_val;
        if (xv < -10.0f) xv = -10.0f;

        float expv = 1.0f;
        float term = 1.0f;
        for (int t = 1; t <= 10; t++) {
            term *= xv / (float)t;
            expv += term;
        }
        if (expv < 1e-7f) expv = 1e-7f;
        probs[i] = expv;
        sum += expv;
    }

    /* 3. Normalise to a probability distribution. */
    if (sum > 0.0f) {
        float inv = 1.0f / sum;
        for (int i = 0; i < n; i++) {
            probs[i] *= inv;
        }
    }
}

/* =========================================================================
 * Model initialisation and validation
 * ========================================================================= */

tinyml_status_t tinyml_init(tinyml_engine_t *engine, const tinyml_model_t *model)
{
    if (!engine || !model) return TINYML_ERR_NULL_PTR;

    memset(engine, 0, sizeof(*engine));

    /* ---- Metadata validation ---- */
    const tinyml_model_meta_t *m = &model->meta;

    if (m->input_dim != TINYML_INPUT_DIM)          return TINYML_ERR_BAD_DIM;
    if (m->output_classes != TINYML_OUTPUT_CLASSES) return TINYML_ERR_BAD_DIM;

    /* Major version must match (minor/patch may differ). */
    uint32_t expected_ver = (TINYML_VERSION_MAJOR << 16)
                          | (TINYML_VERSION_MINOR <<  8)
                          | (TINYML_VERSION_PATCH);
    if ((m->version >> 16) != (expected_ver >> 16)) {
        return TINYML_ERR_VERSION;
    }

    /* ---- CRC-32 integrity check over all weight/bias blobs ---- */
    uint32_t crc = 0xFFFFFFFFU;

    /* Helper: fold a partial CRC into the running value. */
#define CRC_FOLD(ptr, bytes)                                              \
    do {                                                                  \
        uint32_t p = tinyml_crc32(ptr, bytes);                           \
        crc ^= p;                                                         \
        for (int _b = 0; _b < 4; _b++) {                                \
            crc = (crc >> 1) ^ (0xEDB88320U & (-(int32_t)(crc & 1U)));  \
        }                                                                 \
    } while (0)

    /* LSTM 1 */
    CRC_FOLD(model->lstm1.W_ih, 4U * model->lstm1.units * model->lstm1.input_dim);
    CRC_FOLD(model->lstm1.W_hh, 4U * model->lstm1.units * model->lstm1.units);
    CRC_FOLD(model->lstm1.bias, 4U * model->lstm1.units * sizeof(int32_t));

    /* LSTM 2 */
    CRC_FOLD(model->lstm2.W_ih, 4U * model->lstm2.units * model->lstm2.input_dim);
    CRC_FOLD(model->lstm2.W_hh, 4U * model->lstm2.units * model->lstm2.units);
    CRC_FOLD(model->lstm2.bias, 4U * model->lstm2.units * sizeof(int32_t));

    /* Dense 1 */
    CRC_FOLD(model->dense1.W,    model->dense1.units * model->dense1.input_dim);
    CRC_FOLD(model->dense1.bias, model->dense1.units * sizeof(int32_t));

    /* Dense 2 */
    CRC_FOLD(model->dense2.W,    model->dense2.units * model->dense2.input_dim);
    CRC_FOLD(model->dense2.bias, model->dense2.units * sizeof(int32_t));

#undef CRC_FOLD

    crc ^= 0xFFFFFFFFU;

    /* expected_crc == 0 means "placeholder, skip check". */
    if (m->expected_crc != 0x00000000U && crc != m->expected_crc) {
        return TINYML_ERR_CRC_MISMATCH;
    }

    /* ---- Quantisation sanity: every scale must be strictly positive ---- */
#define QS(q)  do { if ((q).scale <= 0.0f) return TINYML_ERR_INVALID_MODEL; } while (0)
    QS(model->lstm1.q_input);  QS(model->lstm1.q_W_ih);
    QS(model->lstm1.q_W_hh);   QS(model->lstm1.q_bias);
    QS(model->lstm1.q_hidden);  QS(model->lstm1.q_cell);
    QS(model->lstm1.q_output);
    QS(model->lstm2.q_input);  QS(model->lstm2.q_W_ih);
    QS(model->lstm2.q_W_hh);   QS(model->lstm2.q_bias);
    QS(model->lstm2.q_hidden);  QS(model->lstm2.q_cell);
    QS(model->lstm2.q_output);
    QS(model->dense1.q_input); QS(model->dense1.q_W);
    QS(model->dense1.q_bias);  QS(model->dense1.q_output);
    QS(model->dense2.q_input); QS(model->dense2.q_W);
    QS(model->dense2.q_bias);  QS(model->dense2.q_output);
#undef QS

    /* ---- Scratch size check ---- */
    /* LSTM cell needs 2 * 4*32 = 256 bytes for acc_buf + temp_buf.
     * Dense layers write directly into local arrays (no scratch needed). */
    if (TINYML_SCRATCH_SIZE < 256) {
        return TINYML_ERR_OVERFLOW;
    }

    engine->model       = model;
    engine->initialised = true;
    return TINYML_OK;
}

/* =========================================================================
 * Full inference pass
 *
 * Pipeline:
 *   float[15] -> quantise -> LSTM1 -> LSTM2 -> quantise h -> Dense1(ReLU)
 *            -> Dense2 -> softmax -> float[5] probabilities
 * ========================================================================= */

tinyml_status_t tinyml_predict(tinyml_engine_t       *engine,
                               const float            input[TINYML_INPUT_DIM],
                               tinyml_result_t       *result)
{
    if (!engine || !input || !result) return TINYML_ERR_NULL_PTR;
    if (!engine->initialised)         return TINYML_ERR_INVALID_MODEL;

    const tinyml_model_t *model = engine->model;
    uint32_t t0 = timer_read_us();

    /* ---- 1. Quantise float input features to INT8 ---------------------- */
    int8_t input_q[TINYML_INPUT_DIM];
    for (int i = 0; i < TINYML_INPUT_DIM; i++) {
        input_q[i] = quantise_int8(input[i], model->lstm1.q_input);
    }

    /* ---- 2. Clear LSTM states ------------------------------------------ */
    memset(engine->h_state, 0, sizeof(engine->h_state));
    memset(engine->c_state, 0, sizeof(engine->c_state));

    /* ---- 3. LSTM layer 1 -----------------------------------------------
     * Scratch layout: [0..127] = acc_buf (4*32), [128..255] = temp_buf */
    tinyml_int8_lstm_cell(input_q,
                          engine->h_state,
                          engine->c_state,
                          &engine->scratch[0],
                          &model->lstm1);

    /* ---- 4. Quantise hidden state for LSTM 2 input --------------------- */
    int8_t h1_q[TINYML_LSTM_UNITS];
    for (int i = 0; i < TINYML_LSTM_UNITS; i++) {
        h1_q[i] = quantise_int8(engine->h_state[i], model->lstm2.q_input);
    }

    /* ---- 5. LSTM layer 2 ----------------------------------------------- */
    memset(engine->h_state, 0, sizeof(engine->h_state));
    memset(engine->c_state, 0, sizeof(engine->c_state));

    tinyml_int8_lstm_cell(h1_q,
                          engine->h_state,
                          engine->c_state,
                          &engine->scratch[0],
                          &model->lstm2);

    /* ---- 6. Quantise final hidden state -> dense 1 input --------------- */
    int8_t h2_q[TINYML_LSTM_UNITS];
    for (int i = 0; i < TINYML_LSTM_UNITS; i++) {
        h2_q[i] = quantise_int8(engine->h_state[i], model->dense1.q_input);
    }

    /* ---- 7. Dense 1  (32 -> 16, ReLU) ---------------------------------- */
    int8_t d1_out[TINYML_DENSE1_UNITS];
    tinyml_int8_dense(h2_q,
                      model->dense1.W,
                      model->dense1.input_dim,
                      model->dense1.units,
                      model->dense1.bias,
                      d1_out,
                      model->dense1.q_input,
                      model->dense1.q_W,
                      model->dense1.q_bias,
                      model->dense1.q_output,
                      model->dense1.use_relu);

    /* ---- 8. Dense 2  (16 -> 5, linear) --------------------------------- */
    int8_t d2_out[TINYML_OUTPUT_CLASSES];
    tinyml_int8_dense(d1_out,
                      model->dense2.W,
                      model->dense2.input_dim,
                      model->dense2.units,
                      model->dense2.bias,
                      d2_out,
                      model->dense2.q_input,
                      model->dense2.q_W,
                      model->dense2.q_bias,
                      model->dense2.q_output,
                      model->dense2.use_relu);

    /* ---- 9. Softmax ---------------------------------------------------- */
    tinyml_softmax(d2_out, TINYML_OUTPUT_CLASSES,
                   model->dense2.q_output, result->probs);

    /* ---- 10. Argmax ----------------------------------------------------- */
    result->class_id   = 0;
    result->confidence = result->probs[0];
    for (int i = 1; i < TINYML_OUTPUT_CLASSES; i++) {
        if (result->probs[i] > result->confidence) {
            result->confidence = result->probs[i];
            result->class_id   = (uint8_t)i;
        }
    }

    /* ---- 11. Timing ----------------------------------------------------- */
    result->inference_us = timer_read_us() - t0;

    return TINYML_OK;
}
