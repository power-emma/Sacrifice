/* nn.c - deep MLP chess move selector (10 hidden layers)
 *
 * Replaces evaluation.c.  The engine no longer uses minimax search or
 * hand-crafted piece-value heuristics.  Instead a deep dense network maps
 * the current board encoding to a "desired" board encoding, and the legal
 * move whose resulting position is closest (L2) to that output is chosen.
 *
 * Weights start out random (Xavier init).  They can be trained later via
 * any gradient-descent scheme — the forward pass here is all that is needed
 * at inference time.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <float.h>
#include <stdint.h>
#include <pthread.h>

#include "chess.h"
#include "nn.h"

/* ── Global network instance ─────────────────────────────────────────────── */
NeuralNet g_net = {0};

/* ── Evaluation-counter globals (required by chess.h / recursion.c) ──────── */
unsigned long long evalCount        = 0ULL;
unsigned long long ttHitCount       = 0ULL;
unsigned long long abPruneCount     = 0ULL;
unsigned long long staticPruneCount = 0ULL;

/* ── Extern globals defined in rules.c / rewards.c ───────────────────────── */
extern struct Piece  board[8][8];
extern struct Move   lastMove;
extern int           boardHistoryCount;
extern struct Piece  boardHistory[200][8][8];
extern int           halfmoveClock;
extern int           depth;
extern int           suppress_engine_output;

/* ── Mutex for thread-safe puzzle evaluation ─────────────────────────────── */
static pthread_mutex_t nn_eval_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ════════════════════════════════════════════════════════════════════════════
 * Board encoding
 * ════════════════════════════════════════════════════════════════════════════ */

/* Map a Piece to a one-hot category index [0..12].
 *   0        = empty
 *   1..6     = white PAWN..KING
 *   7..12    = black PAWN..KING                                              */
static int piece_to_cat(const struct Piece *p)
{
    if (p->type == -1)
        return 0;
    int base = (p->colour == WHITE) ? 1 : 7;
    return base + (int)p->type;   /* PAWN=0 → 1 or 7 … KING=5 → 6 or 12 */
}

void nn_encode_board(const struct Piece b[8][8], float *out)
{
    memset(out, 0, NN_INPUT_SIZE * sizeof(float));
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            int sq  = x * 8 + y;
            int cat = piece_to_cat(&b[x][y]);
            out[sq * NN_PIECE_CATS + cat] = 1.0f;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * Network lifecycle
 * ════════════════════════════════════════════════════════════════════════════ */

void nn_init(NeuralNet *net)
{
    const size_t w_bytes = (size_t)NN_LAYER_SIZE * NN_LAYER_SIZE * sizeof(float);
    const size_t b_bytes = (size_t)NN_LAYER_SIZE * sizeof(float);

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        net->weights_layers[l] = malloc(w_bytes);
        net->bias_layers[l] = malloc(b_bytes);
        if (!net->weights_layers[l] || !net->bias_layers[l]) {
            fprintf(stderr, "nn_init: out of memory\n");
            exit(EXIT_FAILURE);
        }
    }

    net->weights = net->weights_layers[0];
    net->bias = net->bias_layers[0];

    /* Xavier initialisation per layer: scale = 1 / sqrt(fan_in) */
    float scale = 1.0f / sqrtf((float)NN_LAYER_SIZE);
    srand((unsigned int)time(NULL));
    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        float *w = net->weights_layers[l];
        float *b = net->bias_layers[l];
        for (int i = 0; i < NN_LAYER_SIZE * NN_LAYER_SIZE; i++)
            w[i] = ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * scale;
        memset(b, 0, b_bytes);
    }

#ifdef USE_CUDA
    nn_gpu_init(net);   /* upload freshly Xavier-initialised weights to GPU */
#endif
}

void nn_free(NeuralNet *net)
{
    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        free(net->weights_layers[l]);
        free(net->bias_layers[l]);
        net->weights_layers[l] = NULL;
        net->bias_layers[l] = NULL;
    }
    net->weights = NULL;
    net->bias    = NULL;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Forward pass
 * ════════════════════════════════════════════════════════════════════════════ */

static float sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

void nn_forward(const NeuralNet *net, const float *input, float *output)
{
    float cur[NN_LAYER_SIZE];
    float nxt[NN_LAYER_SIZE];

    memcpy(cur, input, NN_LAYER_SIZE * sizeof(float));
    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        const float *w = net->weights_layers[l];
        const float *b = net->bias_layers[l];
        for (int i = 0; i < NN_LAYER_SIZE; i++) {
            float acc = b[i];
            const float *row = w + (size_t)i * NN_LAYER_SIZE;
            for (int j = 0; j < NN_LAYER_SIZE; j++)
                acc += row[j] * cur[j];
            nxt[i] = sigmoid(acc);
        }
        memcpy(cur, nxt, NN_LAYER_SIZE * sizeof(float));
    }

    memcpy(output, cur, NN_OUTPUT_SIZE * sizeof(float));
}

/* ════════════════════════════════════════════════════════════════════════════
 * Board-state manipulation helpers
 * ════════════════════════════════════════════════════════════════════════════ */

/* Apply move m to src, writing the result into dst.
 * Handles: normal moves, captures, en-passant, castling, pawn promotion.  */
static void apply_move_to_copy(const struct Piece src[8][8],
                                struct Piece       dst[8][8],
                                struct Move        m)
{
    memcpy(dst, src, sizeof(struct Piece) * 8 * 8);

    /* En-passant: pawn moves diagonally to an empty square */
    int is_ep = (dst[m.fromX][m.fromY].type == PAWN &&
                 m.fromX != m.toX &&
                 dst[m.toX][m.toY].type == -1);
    if (is_ep) {
        dst[m.toX][m.fromY].type   = -1;
        dst[m.toX][m.fromY].colour = -1;
    }

    /* Move the piece */
    dst[m.toX][m.toY]             = dst[m.fromX][m.fromY];
    dst[m.fromX][m.fromY].type    = -1;
    dst[m.fromX][m.fromY].colour  = -1;
    dst[m.toX][m.toY].hasMoved    = 1;

    /* Pawn promotion → queen */
    if (dst[m.toX][m.toY].type == PAWN) {
        if (dst[m.toX][m.toY].colour == WHITE && m.toY == 7)
            dst[m.toX][m.toY].type = QUEEN;
        else if (dst[m.toX][m.toY].colour == BLACK && m.toY == 0)
            dst[m.toX][m.toY].type = QUEEN;
    }

    /* Castling: move the corresponding rook */
    if (dst[m.toX][m.toY].type == KING && m.fromX == 4) {
        if (m.toX == 6) {                    /* king-side */
            dst[5][m.toY]             = dst[7][m.toY];
            dst[7][m.toY].type        = -1;
            dst[7][m.toY].colour      = -1;
            dst[5][m.toY].hasMoved    = 1;
        } else if (m.toX == 2) {            /* queen-side */
            dst[3][m.toY]             = dst[0][m.toY];
            dst[0][m.toY].type        = -1;
            dst[0][m.toY].colour      = -1;
            dst[3][m.toY].hasMoved    = 1;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * Legal-move selection
 * ════════════════════════════════════════════════════════════════════════════ */

struct Move nn_pick_move(const NeuralNet *net,
                         struct Piece     gameBoard[8][8],
                         enum Colour      colour)
{
    /* Encode the current board and run the forward pass */
    float input[NN_INPUT_SIZE];
    float nn_out[NN_OUTPUT_SIZE];
    nn_encode_board(gameBoard, input);
    nn_forward(net, input, nn_out);

    /* Enumerate all legal moves */
    struct MoveList moves = validMoves(gameBoard, colour);
    if (moves.count == 0)
        return (struct Move){-1, -1, -1, -1};

    /* For each legal move, encode the resulting board and measure L2 distance
     * to the NN output.  The move that produces the nearest board wins.     */
    float        best_dist = FLT_MAX;
    int          best_idx  = 0;
    float        candidate[NN_OUTPUT_SIZE];
    struct Piece tmp[8][8];

    for (int m = 0; m < moves.count; m++) {
        apply_move_to_copy(gameBoard, tmp, moves.moves[m]);
        nn_encode_board(tmp, candidate);

        float dist = 0.0f;
        for (int i = 0; i < NN_OUTPUT_SIZE; i++) {
            float d = nn_out[i] - candidate[i];
            dist += d * d;
        }
        if (dist < best_dist) {
            best_dist = dist;
            best_idx  = m;
        }
    }

    return moves.moves[best_idx];
}

/* ════════════════════════════════════════════════════════════════════════════
 * moveRanking — drop-in replacement for the minimax engine entry point
 * ════════════════════════════════════════════════════════════════════════════ */

int moveRanking(struct Piece currentBoard[8][8],
                int          maxRecursiveDepth,
                enum Colour  aiColour)
{
    (void)maxRecursiveDepth;   /* depth unused — NN evaluates in one pass */

    /* Lazy-initialise weights once */
    if (!g_net.weights)
        nn_init(&g_net);

    /* Immediate checkmate always takes priority */
    if (checkAndExecuteOneMoveMate(currentBoard, aiColour))
        return 999999999;

    struct Move chosen = nn_pick_move(&g_net, currentBoard, aiColour);

    if (chosen.fromX == -1) {
        if (!suppress_engine_output)
            printf("No valid moves available.\n");
        return 0;
    }

    char notation[16];
    snprintf(notation, sizeof(notation), "%c%d%c%d",
             'a' + chosen.fromX, chosen.fromY + 1,
             'a' + chosen.toX,   chosen.toY   + 1);

    if (!suppress_engine_output)
        printf("%s plays: %s\n",
               aiColour == WHITE ? "White (AI)" : "Black (AI)",
               notation);

    /* Apply the chosen move to the global board */
    int is_capture = (board[chosen.toX][chosen.toY].type != -1);

    board[chosen.toX][chosen.toY]            = board[chosen.fromX][chosen.fromY];
    board[chosen.fromX][chosen.fromY].type   = -1;
    board[chosen.fromX][chosen.fromY].colour = -1;
    board[chosen.toX][chosen.toY].hasMoved   = 1;

    /* En-passant capture on global board */
    if (board[chosen.toX][chosen.toY].type == PAWN &&
        chosen.fromX != chosen.toX && is_capture == 0) {
        board[chosen.toX][chosen.fromY].type   = -1;
        board[chosen.toX][chosen.fromY].colour = -1;
    }

    /* Pawn promotion */
    promotePawn(board, chosen.toX, chosen.toY);

    /* Castling: move the rook on the global board */
    if (board[chosen.toX][chosen.toY].type == KING && chosen.fromX == 4) {
        if (chosen.toX == 6) {
            board[5][chosen.toY]             = board[7][chosen.toY];
            board[7][chosen.toY].type        = -1;
            board[7][chosen.toY].colour      = -1;
            board[5][chosen.toY].hasMoved    = 1;
        } else if (chosen.toX == 2) {
            board[3][chosen.toY]             = board[0][chosen.toY];
            board[0][chosen.toY].type        = -1;
            board[0][chosen.toY].colour      = -1;
            board[3][chosen.toY].hasMoved    = 1;
        }
    }

    /* Halfmove clock (50-move rule) */
    if (board[chosen.toX][chosen.toY].type == PAWN || is_capture)
        halfmoveClock = 0;
    else
        halfmoveClock++;

    recordBoardHistory();

    lastMove.fromX = chosen.fromX;
    lastMove.fromY = chosen.fromY;
    lastMove.toX   = chosen.toX;
    lastMove.toY   = chosen.toY;

    tui_update_stats(0.0, 0, 0, 0, 0, 0.0);
    tui_add_move(notation);
    tui_set_predicted_sequence(notation);
    tui_validate_puzzle_move(notation);

    return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * moveRanking_Synchronized — thread-safe wrapper used by puzzle testing
 * ════════════════════════════════════════════════════════════════════════════ */

int moveRanking_Synchronized(struct Piece currentBoard[8][8],
                              int          maxRecursiveDepth,
                              enum Colour  aiColour,
                              struct Move *result_move)
{
    (void)maxRecursiveDepth;

    if (!g_net.weights)
        nn_init(&g_net);

    pthread_mutex_lock(&nn_eval_mutex);

    struct Move chosen = nn_pick_move(&g_net, currentBoard, aiColour);
    *result_move = chosen;

    if (chosen.fromX != -1)
        apply_move_to_copy((const struct Piece (*)[8])currentBoard,
                           currentBoard, chosen);

    pthread_mutex_unlock(&nn_eval_mutex);
    return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Training: single SGD step (teacher forcing)
 *
 *   Loss  = MSE(output, target)
 *   delta_i = 2*(out_i - target_i) * out_i*(1-out_i)   ← chain rule through sigmoid
 *   W[i][j] -= lr * delta_i * input[j]
 *   bias[i] -= lr * delta_i
 *
 * Thread-safe: serialised through nn_train_mutex so concurrent threads write to
 * the shared weight matrix one at a time (Hogwild-style serial updates).
 * ════════════════════════════════════════════════════════════════════════════ */

static pthread_mutex_t nn_train_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifdef USE_CUDA
#define NN_GPU_BATCH_SIZE 32
static float *g_gpu_batch_inputs = NULL;
static float *g_gpu_batch_targets = NULL;
static int g_gpu_batch_count = 0;
static float g_gpu_batch_lr = 0.0f;

static int nn_gpu_batch_ensure(void)
{
    if (g_gpu_batch_inputs && g_gpu_batch_targets) return 1;
    g_gpu_batch_inputs = malloc((size_t)NN_GPU_BATCH_SIZE * NN_INPUT_SIZE * sizeof(float));
    g_gpu_batch_targets = malloc((size_t)NN_GPU_BATCH_SIZE * NN_OUTPUT_SIZE * sizeof(float));
    if (!g_gpu_batch_inputs || !g_gpu_batch_targets) {
        free(g_gpu_batch_inputs);
        free(g_gpu_batch_targets);
        g_gpu_batch_inputs = NULL;
        g_gpu_batch_targets = NULL;
        return 0;
    }
    return 1;
}

static void nn_gpu_flush_pending_locked(void)
{
    if (!nn_gpu_is_ready() || g_gpu_batch_count <= 0) return;
    nn_train_step_gpu_batch(g_gpu_batch_inputs,
                            g_gpu_batch_targets,
                            g_gpu_batch_count,
                            g_gpu_batch_lr);
    g_gpu_batch_count = 0;
}
#endif

float nn_train_step(NeuralNet *net,
                    const struct Piece input_board[8][8],
                    const struct Piece target_board[8][8],
                    float learning_rate)
{
    if (!net->weights_layers[0]) return 0.0f;

    float input[NN_INPUT_SIZE];
    float target[NN_OUTPUT_SIZE];

    nn_encode_board(input_board,  input);
    nn_encode_board(target_board, target);

    pthread_mutex_lock(&nn_train_mutex);

#ifdef USE_CUDA
    if (nn_gpu_is_ready()) {
        if (!nn_gpu_batch_ensure()) {
            float loss = nn_train_step_gpu(input, target, learning_rate);
            pthread_mutex_unlock(&nn_train_mutex);
            return loss;
        }

        if (g_gpu_batch_count > 0 && fabsf(learning_rate - g_gpu_batch_lr) > 1e-12f)
            nn_gpu_flush_pending_locked();

        if (g_gpu_batch_count == 0)
            g_gpu_batch_lr = learning_rate;

        memcpy(g_gpu_batch_inputs + (size_t)g_gpu_batch_count * NN_INPUT_SIZE,
               input,
               NN_INPUT_SIZE * sizeof(float));
        memcpy(g_gpu_batch_targets + (size_t)g_gpu_batch_count * NN_OUTPUT_SIZE,
               target,
               NN_OUTPUT_SIZE * sizeof(float));
        g_gpu_batch_count++;

        if (g_gpu_batch_count >= NN_GPU_BATCH_SIZE)
            nn_gpu_flush_pending_locked();

        pthread_mutex_unlock(&nn_train_mutex);
        return 0.0f;
    }
#endif

    /* ── CPU fallback ──────────────────────────────────────────────────── */
    float activations[NN_TOTAL_LAYERS + 1][NN_LAYER_SIZE];
    float deltas[NN_TOTAL_LAYERS][NN_LAYER_SIZE];

    memcpy(activations[0], input, NN_LAYER_SIZE * sizeof(float));

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        float *out = activations[l + 1];
        const float *in = activations[l];
        const float *w = net->weights_layers[l];
        const float *b = net->bias_layers[l];
        for (int i = 0; i < NN_LAYER_SIZE; i++) {
            float acc = b[i];
            const float *row = w + (size_t)i * NN_LAYER_SIZE;
            for (int j = 0; j < NN_LAYER_SIZE; j++)
                acc += row[j] * in[j];
            out[i] = sigmoid(acc);
        }
    }

    float total_loss = 0.0f;
    const int out_l = NN_TOTAL_LAYERS - 1;
    for (int i = 0; i < NN_LAYER_SIZE; i++) {
        float out = activations[NN_TOTAL_LAYERS][i];
        float err = out - target[i];
        total_loss += err * err;
        deltas[out_l][i] = 2.0f * err * out * (1.0f - out);
    }

    for (int l = NN_TOTAL_LAYERS - 2; l >= 0; l--) {
        const float *w_next = net->weights_layers[l + 1];
        for (int j = 0; j < NN_LAYER_SIZE; j++) {
            float sum = 0.0f;
            for (int k = 0; k < NN_LAYER_SIZE; k++)
                sum += w_next[(size_t)k * NN_LAYER_SIZE + j] * deltas[l + 1][k];
            float a = activations[l + 1][j];
            deltas[l][j] = sum * a * (1.0f - a);
        }
    }

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        float *w = net->weights_layers[l];
        float *b = net->bias_layers[l];
        const float *in = activations[l];
        const float *d = deltas[l];
        for (int i = 0; i < NN_LAYER_SIZE; i++) {
            float delta = d[i];
            float *row = w + (size_t)i * NN_LAYER_SIZE;
            for (int j = 0; j < NN_LAYER_SIZE; j++)
                row[j] -= learning_rate * delta * in[j];
            b[i] -= learning_rate * delta;
        }
    }

    pthread_mutex_unlock(&nn_train_mutex);
    return total_loss / NN_OUTPUT_SIZE;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Weight persistence
 * ════════════════════════════════════════════════════════════════════════════ */

int nn_save(const NeuralNet *net, const char *filepath)
{
    if (!net->weights_layers[0]) return 0;
#ifdef USE_CUDA
    /* Flush GPU weights to CPU before writing to disk */
    if (nn_gpu_is_ready()) {
        pthread_mutex_lock(&nn_train_mutex);
        nn_gpu_flush_pending_locked();
        nn_gpu_sync_to_cpu((NeuralNet *)net);
        pthread_mutex_unlock(&nn_train_mutex);
    }
#endif
    FILE *f = fopen(filepath, "wb");
    if (!f) return 0;
    const char magic[4] = {'N', 'N', 'D', 'P'};
    uint32_t version = 2;
    uint32_t layers = NN_TOTAL_LAYERS;
    uint32_t width = NN_LAYER_SIZE;
    fwrite(magic, 1, 4, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&layers, sizeof(layers), 1, f);
    fwrite(&width, sizeof(width), 1, f);

    const size_t w_count = (size_t)NN_LAYER_SIZE * NN_LAYER_SIZE;
    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        fwrite(net->weights_layers[l], sizeof(float), w_count,      f);
        fwrite(net->bias_layers[l],    sizeof(float), NN_LAYER_SIZE, f);
    }
    fclose(f);
    return 1;
}

int nn_load(NeuralNet *net, const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) return 0;

    if (!net->weights_layers[0]) {
        const size_t w_bytes = (size_t)NN_LAYER_SIZE * NN_LAYER_SIZE * sizeof(float);
        const size_t b_bytes = (size_t)NN_LAYER_SIZE * sizeof(float);
        for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
            net->weights_layers[l] = malloc(w_bytes);
            net->bias_layers[l] = malloc(b_bytes);
            if (!net->weights_layers[l] || !net->bias_layers[l]) {
                fclose(f);
                return 0;
            }
        }
        net->weights = net->weights_layers[0];
        net->bias = net->bias_layers[0];
    }

    char magic[4] = {0};
    size_t rm = fread(magic, 1, 4, f);
    if (rm == 4 && magic[0] == 'N' && magic[1] == 'N' && magic[2] == 'D' && magic[3] == 'P') {
        uint32_t version = 0, layers = 0, width = 0;
        if (fread(&version, sizeof(version), 1, f) != 1 ||
            fread(&layers, sizeof(layers), 1, f) != 1 ||
            fread(&width, sizeof(width), 1, f) != 1) {
            fclose(f);
            return 0;
        }
        if (version != 2 || layers != (uint32_t)NN_TOTAL_LAYERS || width != (uint32_t)NN_LAYER_SIZE) {
            fclose(f);
            return 0;
        }

        const size_t w_count = (size_t)NN_LAYER_SIZE * NN_LAYER_SIZE;
        for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
            size_t rw = fread(net->weights_layers[l], sizeof(float), w_count,      f);
            size_t rb = fread(net->bias_layers[l],    sizeof(float), NN_LAYER_SIZE, f);
            if (rw != w_count || rb != (size_t)NN_LAYER_SIZE) {
                fclose(f);
                return 0;
            }
        }
        fclose(f);
    } else {
        rewind(f);
        const size_t w_count = (size_t)NN_LAYER_SIZE * NN_LAYER_SIZE;
        size_t rw = fread(net->weights_layers[0], sizeof(float), w_count,      f);
        size_t rb = fread(net->bias_layers[0],    sizeof(float), NN_LAYER_SIZE, f);
        fclose(f);
        if (rw != w_count || rb != (size_t)NN_LAYER_SIZE) return 0;

        for (int l = 1; l < NN_TOTAL_LAYERS; l++) {
            memcpy(net->weights_layers[l], net->weights_layers[0], w_count * sizeof(float));
            memcpy(net->bias_layers[l], net->bias_layers[0], NN_LAYER_SIZE * sizeof(float));
        }
    }

#ifdef USE_CUDA
    /* Push loaded weights to GPU (nn_gpu_init is idempotent) */
    nn_gpu_init(net);
#endif
    return 1;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Stubs for functions previously in evaluation.c that other TUs may reference
 * ════════════════════════════════════════════════════════════════════════════ */

double evaluateBoardPosition(struct Piece b[8][8])
{
    (void)b;
    return 0.0;
}

int evaluateEndgameAdvancement(struct Piece b[8][8],
                                int fromX, int fromY,
                                int toX,   int toY,
                                enum Colour colour)
{
    (void)b; (void)fromX; (void)fromY; (void)toX; (void)toY; (void)colour;
    return 0;
}

double evaluateBoardPosition_ThreadSafe(struct GameState *s)
{
    (void)s;
    return 0.0;
}

void clear_thread_transposition_table(void) {}

void printEvaluationCount(void) {}
