/**
 * @file tinyml.h
 * @brief TinyML Inference Engine for VelaSense Emotion Arousal Detection
 *
 * Late feature fusion model: 2-layer LSTM(32) -> Dense(16) -> Dense(5) -> softmax
 * Input: 15 features from PPG/IMU/EDA/temperature sensors
 * Output: 5-class arousal classification (excitement, nervous, surprise, stress, other)
 *
 * All weights and activations are INT8 quantized. Accumulation uses INT32.
 * Target: <100ms inference on Cortex-M33 @ 100MHz.
 *
 * @version 1.0.0
 * @date    2026-08-11
 */

#ifndef VELASENSE_TINYML_H
#define VELASENSE_TINYML_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Version and constants
 * ---------------------------------------------------------------------------*/
#define TINYML_VERSION_MAJOR   1
#define TINYML_VERSION_MINOR   0
#define TINYML_VERSION_PATCH   0
#define TINYML_VERSION_STRING  "1.0.0"

#define TINYML_INPUT_DIM       15
#define TINYML_OUTPUT_CLASSES  5
#define TINYML_LSTM_UNITS      32
#define TINYML_DENSE1_UNITS    16
#define TINYML_SEQUENCE_LEN    1      /**< Single-timestep unrolled inference */

#define TINYML_SCRATCH_SIZE    512    /**< Runtime scratch buffer (bytes) */

/* Arousal class indices */
#define AROUSAL_EXCITEMENT     0
#define AROUSAL_NERVOUS        1
#define AROUSAL_SURPRISE       2
#define AROUSAL_STRESS         3
#define AROUSAL_OTHER          4

/* ---------------------------------------------------------------------------
 * Status codes
 * ---------------------------------------------------------------------------*/
typedef enum {
    TINYML_OK                =  0,
    TINYML_ERR_NULL_PTR      = -1,
    TINYML_ERR_INVALID_MODEL = -2,
    TINYML_ERR_CRC_MISMATCH  = -3,
    TINYML_ERR_BAD_DIM       = -4,
    TINYML_ERR_VERSION       = -5,
    TINYML_ERR_OVERFLOW      = -6,
} tinyml_status_t;

/* ---------------------------------------------------------------------------
 * Quantization parameter block (per-tensor symmetric INT8)
 *
 *   real_value = (quantized_int8 - zero_point) * scale
 *   quantized  = clamp(round(real_value / scale + zero_point), -128, 127)
 *
 * For symmetric quantization zero_point is typically 0.
 * ---------------------------------------------------------------------------*/
typedef struct {
    float   scale;      /**< Multiplier to convert INT8 <-> float */
    int8_t  zero_point; /**< Offset in INT8 domain               */
} tinyml_quant_t;

/* ---------------------------------------------------------------------------
 * Inference result
 * ---------------------------------------------------------------------------*/
typedef struct {
    float    probs[TINYML_OUTPUT_CLASSES]; /**< Softmax probability distribution */
    uint8_t  class_id;                     /**< Index of the winning class       */
    float    confidence;                   /**< Probability of the winner        */
    uint32_t inference_us;                 /**< Wall-clock time in microseconds  */
} tinyml_result_t;

/* ---------------------------------------------------------------------------
 * Opaque engine handle (caller allocates; engine fills it on init)
 * ---------------------------------------------------------------------------*/
typedef struct tinyml_engine tinyml_engine_t;

/* ---------------------------------------------------------------------------
 * Model metadata (embedded in each layer struct via the data headers)
 * ---------------------------------------------------------------------------*/
typedef struct {
    uint32_t version;       /**< Model semantic version  (major<<16|minor<<8|patch) */
    uint32_t expected_crc;  /**< CRC-32 of all weight/bias blobs                   */
    uint32_t total_params;  /**< Total number of INT8 parameters                   */
    uint16_t input_dim;     /**< Expected feature vector length                    */
    uint16_t output_classes;/**< Number of output classes                          */
} tinyml_model_meta_t;

/* ---------------------------------------------------------------------------
 * Layer descriptors (defined in model_data.h / model_data.c)
 *
 * These are compile-time constant structures that point into the weight
 * arrays.  The engine dereferences them during inference but never writes
 * to them, so they can live in flash.
 * ---------------------------------------------------------------------------*/
typedef struct {
    const int8_t   *W_ih;          /**< Input-to-hidden weights [4U x input_dim] */
    const int8_t   *W_hh;          /**< Hidden-to-hidden weights [4U x U]        */
    const int32_t  *bias;          /**< Combined gate biases [4U]                */
    tinyml_quant_t  q_input;       /**< Quantization params for input tensor     */
    tinyml_quant_t  q_W_ih;        /**< Quantization params for W_ih             */
    tinyml_quant_t  q_W_hh;        /**< Quantization params for W_hh             */
    tinyml_quant_t  q_bias;        /**< Quantization params for bias             */
    tinyml_quant_t  q_hidden;      /**< Quantization params for hidden state     */
    tinyml_quant_t  q_cell;        /**< Quantization params for cell state       */
    tinyml_quant_t  q_output;      /**< Quantization params for gate outputs     */
    uint16_t        input_dim;
    uint16_t        units;
} tinyml_lstm_layer_t;

typedef struct {
    const int8_t   *W;             /**< Weight matrix [units x input_dim]        */
    const int32_t  *bias;          /**< Bias vector [units]                      */
    tinyml_quant_t  q_input;
    tinyml_quant_t  q_W;
    tinyml_quant_t  q_bias;
    tinyml_quant_t  q_output;
    uint16_t        input_dim;
    uint16_t        units;
    bool            use_relu;      /**< Apply ReLU activation if true             */
} tinyml_dense_layer_t;

typedef struct {
    tinyml_model_meta_t   meta;
    tinyml_lstm_layer_t   lstm1;
    tinyml_lstm_layer_t   lstm2;
    tinyml_dense_layer_t  dense1;
    tinyml_dense_layer_t  dense2;
} tinyml_model_t;

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

/**
 * @brief Get the inference engine version string.
 */
const char *tinyml_version(void);

/**
 * @brief Initialise the inference engine with the given model.
 *
 * Validates model integrity (CRC-32) and dimensions before accepting.
 * Must be called once before any inference call.
 *
 * @param[out] engine  Caller-allocated engine state (cleared on failure).
 * @param[in]  model   Pointer to a compile-time constant model descriptor.
 * @return TINYML_OK on success, or a negative error code.
 */
tinyml_status_t tinyml_init(tinyml_engine_t *engine, const tinyml_model_t *model);

/**
 * @brief Run a single inference pass.
 *
 * @param[in,out] engine  Initialised engine (scratch memory lives here).
 * @param[in]     input   Feature vector of length TINYML_INPUT_DIM.
 * @param[out]    result  Classification result with timing.
 * @return TINYML_OK on success.
 */
tinyml_status_t tinyml_predict(tinyml_engine_t       *engine,
                               const float            input[TINYML_INPUT_DIM],
                               tinyml_result_t       *result);

/**
 * @brief Compute CRC-32 over a byte buffer (same polynomial as the engine).
 *
 * Useful for external verification of weight blobs.
 */
uint32_t tinyml_crc32(const void *data, uint32_t len);

/**
 * @brief Return a human-readable status string.
 */
const char *tinyml_status_str(tinyml_status_t status);

/* ---------------------------------------------------------------------------
 * Internal kernel prototypes (exposed for unit testing)
 * ---------------------------------------------------------------------------*/

/**
 * @brief INT8 matrix multiply with INT32 accumulation and requantization.
 *
 * Computes: out[i] = requantize( sum_j( (A[i*lda+j] - za) * (B[j*ldb+k] - zb) ) + bias[k] )
 *
 * Dimensions:
 *   A  [M x K]  row-major, INT8
 *   B  [K x N]  row-major, INT8
 *   bias [N]     INT32 (pre-scaled to match accumulator domain)
 *   out [M x N]  row-major, INT8
 */
void tinyml_int8_matmul(const int8_t  *A,  int M, int K, int lda,
                        const int8_t  *B,  int N, int ldb,
                        const int32_t *bias,
                        int8_t        *out,
                        tinyml_quant_t qA, tinyml_quant_t qB, tinyml_quant_t qOut);

/**
 * @brief INT8 fully-connected (dense) layer.
 *
 * out = requantize( (W * input) + bias )
 * Optionally applies ReLU in the INT8 domain.
 */
void tinyml_int8_dense(const int8_t  *input,
                       const int8_t  *W,     int in_dim, int out_dim,
                       const int32_t *bias,
                       int8_t        *out,
                       tinyml_quant_t qIn, tinyml_quant_t qW,
                       tinyml_quant_t qBias, tinyml_quant_t qOut,
                       bool           use_relu);

/**
 * @brief Single LSTM timestep (all four gates).
 *
 * Processes one time step through the LSTM cell:
 *   f = sigmoid(Wf*[x,h] + bf)
 *   i = sigmoid(Wi*[x,h] + bi)
 *   g = tanh(Wg*[x,h] + bg)
 *   o = sigmoid(Wo*[x,h] + bo)
 *   c = f*c_prev + i*g
 *   h = o * tanh(c)
 *
 * All intermediate arithmetic is INT8/INT32; nonlinear activations use
 * a lightweight piecewise-linear approximation in float.
 */
void tinyml_int8_lstm_cell(const int8_t  *input,      /**< [input_dim]  INT8       */
                           float         *h,          /**< [units]      float (in/out) */
                           float         *c,          /**< [units]      float (in/out) */
                           int8_t        *acc_buf,    /**< [4*units]    scratch    */
                           const tinyml_lstm_layer_t *layer);

/**
 * @brief Softmax over a dequantized INT8 vector.
 *
 * Converts INT8 -> float, applies softmax, writes probabilities.
 */
void tinyml_softmax(const int8_t  *input,
                    int            n,
                    tinyml_quant_t q,
                    float         *probs);

#ifdef __cplusplus
}
#endif

#endif /* VELASENSE_TINYML_H */
