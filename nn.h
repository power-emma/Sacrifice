/* nn.h - Simple 1-layer neural network for chess move prediction
 *
 * Architecture:
 *   Input  (832 neurons): board encoded as 64 squares × 13 one-hot categories
 *                         cat 0  = empty
 *                         cat 1-6  = white PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
 *                         cat 7-12 = black PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
 *
 *   Layer  (1 dense layer): output = sigmoid(W · input + b)
 *
 *   Output (832 neurons): same encoding — the predicted board state after a move
 *
 * Legal-move guarantee:
 *   The raw NN output is decoded by finding the legal move whose resulting board
 *   encoding has the smallest L2 distance to the NN output vector.
 */
#ifndef NN_H
#define NN_H

#include "chess.h"

#define NN_SQUARES     64
#define NN_PIECE_CATS  13                              /* 0=empty, 1-6=white, 7-12=black */
#define NN_INPUT_SIZE  (NN_SQUARES * NN_PIECE_CATS)   /* 832 */
#define NN_OUTPUT_SIZE NN_INPUT_SIZE                   /* 832 */
#define NN_HIDDEN_LAYERS 10
#define NN_TOTAL_LAYERS (NN_HIDDEN_LAYERS + 1)        /* hidden + output */
#define NN_LAYER_SIZE NN_INPUT_SIZE                    /* fixed-width MLP */

typedef struct {
    /* Per-layer params for a fixed-width deep MLP (832 -> ... -> 832). */
    float *weights_layers[NN_TOTAL_LAYERS];  /* each [NN_LAYER_SIZE][NN_LAYER_SIZE] */
    float *bias_layers[NN_TOTAL_LAYERS];     /* each [NN_LAYER_SIZE] */

    /* Legacy aliases used by single-layer CUDA path. */
    float *weights;
    float *bias;
} NeuralNet;

/* Global network instance — lazy-initialised on first move */
extern NeuralNet g_net;

/* Initialise network with Xavier-scaled random weights */
void nn_init(NeuralNet *net);

/* Free heap-allocated weight and bias arrays */
void nn_free(NeuralNet *net);

/* Encode an 8×8 board into a one-hot float vector of length NN_INPUT_SIZE */
void nn_encode_board(const struct Piece board[8][8], float *out);

/* Forward pass: output[i] = sigmoid( sum_j W[i][j]*input[j] + bias[i] ) */
void nn_forward(const NeuralNet *net, const float *input, float *output);

/* Pick the legal move whose resulting board encoding is nearest (L2) to the
 * raw NN output.  Returns the chosen Move; does NOT modify board. */
struct Move nn_pick_move(const NeuralNet *net,
                         struct Piece board[8][8],
                         enum Colour colour);

/* One SGD step: teach the network that input_board should map to target_board.
 * Thread-safe (uses an internal mutex).  Returns mean MSE loss. */
float nn_train_step(NeuralNet *net,
                    const struct Piece input_board[8][8],
                    const struct Piece target_board[8][8],
                    float learning_rate);

/* Persist weights to / restore weights from a binary file.
 * nn_load initialises the network if not already allocated.
 * Both return 1 on success, 0 on failure.                   */
int nn_save(const NeuralNet *net, const char *filepath);
int nn_load(NeuralNet *net,       const char *filepath);

/* ── GPU acceleration (compiled only when -DUSE_CUDA is defined) ──────────
 *
 * Weights are mirrored to GPU device memory.  nn_train_step() uses the GPU
 * path when the GPU is ready; nn_forward() always runs on CPU so that the
 * many puzzle-worker threads can evaluate moves in parallel without hitting
 * a GPU mutex.  Call nn_gpu_sync_to_cpu() to freshen the CPU copy before
 * saving or before a new inference pass begins.
 */
#ifdef USE_CUDA
int   nn_gpu_init(NeuralNet *net);      /* alloc device bufs + upload weights */
void  nn_gpu_sync_to_cpu(NeuralNet *net); /* download GPU weights → CPU copy  */
void  nn_gpu_free(void);                /* release all device allocations      */
int   nn_gpu_is_ready(void);            /* 1 if GPU is initialised             */
void  nn_forward_gpu(const float *input, float *output); /* GPU forward pass  */
float nn_train_step_gpu(const float *input, const float *target, float lr);
float nn_train_step_gpu_batch(const float *input_batch,
                              const float *target_batch,
                              int batch_size,
                              float lr);
#endif /* USE_CUDA */

#endif /* NN_H */
