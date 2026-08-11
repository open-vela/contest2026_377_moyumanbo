/**
 * @file model_data.h
 * @brief Model weight/bias arrays for VelaSense emotion arousal classifier.
 *
 * Architecture: 2-layer LSTM(32) -> Dense(16) -> Dense(5) -> softmax
 *
 * All weight arrays are INT8 quantized.  Biases are INT32 (pre-scaled to
 * the accumulator domain so they can be added directly after the INT8 matmul
 * without extra shifting).
 *
 * The arrays defined here are *placeholders* with the correct dimensions.
 * Replace them with real trained weights before deploying to production.
 *
 * @version 1.0.0
 */

#ifndef VELASENSE_MODEL_DATA_H
#define VELASENSE_MODEL_DATA_H

#include "tinyml.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Dimensions --------------------------------------------------------- */

#define MODEL_LSTM_UNITS       TINYML_LSTM_UNITS     /* 32  */
#define MODEL_LSTM_GATES       4
#define MODEL_INPUT_DIM        TINYML_INPUT_DIM      /* 15  */
#define MODEL_DENSE1_UNITS     TINYML_DENSE1_UNITS   /* 16  */
#define MODEL_OUTPUT_CLASSES   TINYML_OUTPUT_CLASSES  /*  5  */

/* ---- Derived weight sizes ----------------------------------------------- */

/* LSTM layer 1: input_dim=15, units=32 */
#define LSTM1_W_IH_SIZE   (MODEL_LSTM_GATES * MODEL_LSTM_UNITS * MODEL_INPUT_DIM)   /* 1920 */
#define LSTM1_W_HH_SIZE   (MODEL_LSTM_GATES * MODEL_LSTM_UNITS * MODEL_LSTM_UNITS)  /* 4096 */
#define LSTM1_BIAS_SIZE   (MODEL_LSTM_GATES * MODEL_LSTM_UNITS)                      /* 128  */

/* LSTM layer 2: input_dim=32, units=32 */
#define LSTM2_W_IH_SIZE   (MODEL_LSTM_GATES * MODEL_LSTM_UNITS * MODEL_LSTM_UNITS)  /* 4096 */
#define LSTM2_W_HH_SIZE   (MODEL_LSTM_GATES * MODEL_LSTM_UNITS * MODEL_LSTM_UNITS)  /* 4096 */
#define LSTM2_BIAS_SIZE   (MODEL_LSTM_GATES * MODEL_LSTM_UNITS)                      /* 128  */

/* Dense 1: input_dim=32, units=16 */
#define DENSE1_W_SIZE     (MODEL_DENSE1_UNITS * MODEL_LSTM_UNITS)                    /* 512  */
#define DENSE1_BIAS_SIZE  (MODEL_DENSE1_UNITS)                                        /* 16   */

/* Dense 2: input_dim=16, units=5 */
#define DENSE2_W_SIZE     (MODEL_OUTPUT_CLASSES * MODEL_DENSE1_UNITS)                 /* 80   */
#define DENSE2_BIAS_SIZE  (MODEL_OUTPUT_CLASSES)                                      /*  5   */

/* ---- LSTM layer 1 weight/bias arrays ------------------------------------ */

extern const int8_t  g_lstm1_W_ih[LSTM1_W_IH_SIZE];
extern const int8_t  g_lstm1_W_hh[LSTM1_W_HH_SIZE];
extern const int32_t g_lstm1_bias[LSTM1_BIAS_SIZE];

/* ---- LSTM layer 2 weight/bias arrays ------------------------------------ */

extern const int8_t  g_lstm2_W_ih[LSTM2_W_IH_SIZE];
extern const int8_t  g_lstm2_W_hh[LSTM2_W_HH_SIZE];
extern const int32_t g_lstm2_bias[LSTM2_BIAS_SIZE];

/* ---- Dense layer 1 weight/bias arrays ----------------------------------- */

extern const int8_t  g_dense1_W[DENSE1_W_SIZE];
extern const int32_t g_dense1_bias[DENSE1_BIAS_SIZE];

/* ---- Dense layer 2 weight/bias arrays ----------------------------------- */

extern const int8_t  g_dense2_W[DENSE2_W_SIZE];
extern const int32_t g_dense2_bias[DENSE2_BIAS_SIZE];

/* ---- Complete model descriptor ------------------------------------------ */

extern const tinyml_model_t g_velasense_model;

#ifdef __cplusplus
}
#endif

#endif /* VELASENSE_MODEL_DATA_H */
