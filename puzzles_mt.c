// puzzles_mt.c - Multi-threaded Lichess puzzle testing with proper thread safety
// This implementation uses GameState to avoid global state conflicts
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "chess.h"
#include "nn.h"

#define MAX_THREADS 256

// Export puzzle test count for compatibility
int PUZZLE_TEST_COUNT = 500;

// Global thread status tracking for TUI
typedef struct {
    int thread_id;
    int current_puzzle;
    int last_result;  // -1 = not started, 0 = fail, 1 = pass
    int is_active;
} ThreadStatus;

static ThreadStatus global_thread_statuses[MAX_THREADS];
static int global_num_threads = 0;
static pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread worker arguments
typedef struct {
    const char *puzzle_file;
    int search_depth;
    int start_puzzle;
    int end_puzzle;
    int *results;  // Array to store results (1 = pass, 0 = fail)
    void (*progress_callback)(int completed, int total, int passes);
    pthread_mutex_t *results_lock;
    int *completed_count;
    int total_puzzles;
    int   train_nn;        /* 1 = gradient-train the NN (teacher forcing) */
    float learning_rate;  /* SGD step size when train_nn = 1 */
} ThreadWorkerArgs;

// Execute a UCI move on the GameState
static int executeUciMove_ThreadSafe(struct GameState *state, const char *uci)
{
    if (!uci || strlen(uci) < 4)
        return 0;

    int fx = uci[0] - 'a';
    int fy = uci[1] - '1';
    int tx = uci[2] - 'a';
    int ty = uci[3] - '1';

    if (fx < 0 || fx > 7 || tx < 0 || tx > 7 || fy < 0 || fy > 7 || ty < 0 || ty > 7)
        return 0;

    struct Piece moving = state->board[fx][fy];
    if (moving.type == -1)
        return 0;

    // Simple move/capture
    state->board[tx][ty] = moving;
    state->board[fx][fy].type = -1;
    state->board[fx][fy].colour = -1;
    state->board[tx][ty].hasMoved = 1;

    // Promotion (5th char)
    if (strlen(uci) >= 5)
    {
        char pc = uci[4];
        switch (pc)
        {
        case 'q': case 'Q': state->board[tx][ty].type = QUEEN; break;
        case 'r': case 'R': state->board[tx][ty].type = ROOK; break;
        case 'b': case 'B': state->board[tx][ty].type = BISHOP; break;
        case 'n': case 'N': state->board[tx][ty].type = KNIGHT; break;
        default: break;
        }
    }

    // Castling: if king moved two squares, move rook accordingly
    if (state->board[tx][ty].type == KING && abs(tx - fx) == 2)
    {
        int row = ty;
        if (tx == 6)
        {
            state->board[5][row] = state->board[7][row];
            state->board[7][row].type = -1;
            state->board[7][row].colour = -1;
            state->board[5][row].hasMoved = 1;
        }
        else if (tx == 2)
        {
            state->board[3][row] = state->board[0][row];
            state->board[0][row].type = -1;
            state->board[0][row].colour = -1;
            state->board[3][row].hasMoved = 1;
        }
    }

    // Update last move
    state->lastMove.fromX = fx;
    state->lastMove.fromY = fy;
    state->lastMove.toX = tx;
    state->lastMove.toY = ty;

    // Reset halfmove clock on pawn move or capture (capture was already done above)
    if (state->board[tx][ty].type == PAWN)
        state->halfmoveClock = 0;
    else
        state->halfmoveClock++;

    return 1;
}

// Worker thread function - processes a range of puzzles
static void* puzzle_worker_thread(void *arg)
{
    ThreadWorkerArgs *args = (ThreadWorkerArgs *)arg;
    
    // Determine thread ID for this worker
    int thread_id = -1;
    pthread_mutex_lock(&status_mutex);
    for (int i = 0; i < global_num_threads; i++)
    {
        if (global_thread_statuses[i].thread_id == -1)
        {
            thread_id = i;
            global_thread_statuses[i].thread_id = i;
            global_thread_statuses[i].is_active = 1;
            global_thread_statuses[i].current_puzzle = -1;
            global_thread_statuses[i].last_result = -1;
            break;
        }
    }
    pthread_mutex_unlock(&status_mutex);
    
    for (int puzzle_idx = args->start_puzzle; puzzle_idx < args->end_puzzle; puzzle_idx++)
    {
        // Update thread status - starting new puzzle
        if (thread_id >= 0)
        {
            pthread_mutex_lock(&status_mutex);
            global_thread_statuses[thread_id].current_puzzle = puzzle_idx;
            global_thread_statuses[thread_id].is_active = 1;
            pthread_mutex_unlock(&status_mutex);
        }
        
        // Create thread-local game state
        struct GameState state;
        initGameState(&state);
        
        // Load puzzle
        struct LichessPuzzle puzzle;
        if (!loadLichessPuzzle(args->puzzle_file, puzzle_idx, &puzzle))
        {
            args->results[puzzle_idx] = 0;
            
            if (thread_id >= 0)
            {
                pthread_mutex_lock(&status_mutex);
                global_thread_statuses[thread_id].last_result = 0;
                pthread_mutex_unlock(&status_mutex);
            }
            
            pthread_mutex_lock(args->results_lock);
            (*args->completed_count)++;
            if (args->progress_callback && ((*args->completed_count) % 5 == 0 || *args->completed_count == 1))
            {
                int passes = 0;
                for (int i = 0; i < args->total_puzzles; i++)
                    if (args->results[i] == 1) passes++;
                args->progress_callback(*args->completed_count, args->total_puzzles, passes);
            }
            pthread_mutex_unlock(args->results_lock);
            
            cleanupGameState(&state);
            continue;
        }
        
        // Load FEN position
        if (!loadBoardFromFEN(puzzle.fen, state.board))
        {
            args->results[puzzle_idx] = 0;
            
            if (thread_id >= 0)
            {
                pthread_mutex_lock(&status_mutex);
                global_thread_statuses[thread_id].last_result = 0;
                pthread_mutex_unlock(&status_mutex);
            }
            
            pthread_mutex_lock(args->results_lock);
            (*args->completed_count)++;
            if (args->progress_callback && ((*args->completed_count) % 5 == 0 || *args->completed_count == 1))
            {
                int passes = 0;
                for (int i = 0; i < args->total_puzzles; i++)
                    if (args->results[i] == 1) passes++;
                args->progress_callback(*args->completed_count, args->total_puzzles, passes);
            }
            pthread_mutex_unlock(args->results_lock);
            
            cleanupGameState(&state);
            continue;
        }
        
        enum Colour sideToMove = getTurnFromFEN(puzzle.fen);
        
        // Parse puzzle moves
        char movesCopy[512];
        strcpy(movesCopy, puzzle.moves);
        char *saveptr = NULL;
        char *token = strtok_r(movesCopy, " ", &saveptr);
        
        // Execute first move (opponent's move that creates the puzzle position)
        if (token)
        {
            if (!executeUciMove_ThreadSafe(&state, token))
            {
                args->results[puzzle_idx] = 0;
                
                pthread_mutex_lock(args->results_lock);
                (*args->completed_count)++;
                if (args->progress_callback && ((*args->completed_count) % 5 == 0 || *args->completed_count == 1))
                {
                    int passes = 0;
                    for (int i = 0; i < args->total_puzzles; i++)
                        if (args->results[i] == 1) passes++;
                    args->progress_callback(*args->completed_count, args->total_puzzles, passes);
                }
                pthread_mutex_unlock(args->results_lock);
                
                cleanupGameState(&state);
                continue;
            }
            recordBoardHistory_ThreadSafe(&state);
            sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
            token = strtok_r(NULL, " ", &saveptr);
        }
        
        // Solve puzzle - alternate between AI moves and expected responses
        int puzzle_success = 1;
        int nn_first_move_checked = 0;
        int nn_first_move_correct = 0;
        while (token && (puzzle_success || args->train_nn))
        {
            // AI's turn
            enum Colour aiColour = sideToMove;

            /* ── Training mode: teacher forcing ──────────────────────────────
             * 1. Build the target board from the expected (correct) move.
             * 2. Run one SGD step: teach NN that current board → target board.
             * 3. Ask NN what it would have picked (accuracy tracking only).
             * 4. Execute the EXPECTED move regardless so subsequent positions
             *    are always valid (teacher forcing).                          */
            if (args->train_nn) {
                const char *expectedMove = token;
                token = strtok_r(NULL, " ", &saveptr);

                /* Apply expected move to a copy to get target board */
                struct GameState target_state = state;
                if (executeUciMove_ThreadSafe(&target_state, expectedMove)) {
                    nn_train_step(&g_net,
                                  (const struct Piece (*)[8])state.board,
                                  (const struct Piece (*)[8])target_state.board,
                                  args->learning_rate);
                }

                /* Accuracy check only on first AI move per puzzle.
                 * This matches the training score definition and avoids
                 * expensive CPU-side move search on every ply. */
                if (!nn_first_move_checked) {
                    struct Move nn_choice = nn_pick_move(&g_net, state.board, aiColour);
                    char nn_uci[32] = "";
                    if (nn_choice.fromX >= 0)
                        snprintf(nn_uci, sizeof(nn_uci), "%c%d%c%d",
                                 'a' + nn_choice.fromX, nn_choice.fromY + 1,
                                 'a' + nn_choice.toX,   nn_choice.toY   + 1);
                    nn_first_move_correct = (strcmp(nn_uci, expectedMove) == 0);
                    nn_first_move_checked = 1;
                }

                /* Execute expected move (teacher forcing) */
                if (!executeUciMove_ThreadSafe(&state, expectedMove))
                    break;
                recordBoardHistory_ThreadSafe(&state);
                sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;

                /* Execute opponent response */
                if (token) {
                    if (!executeUciMove_ThreadSafe(&state, token))
                        break;
                    recordBoardHistory_ThreadSafe(&state);
                    sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
                    token = strtok_r(NULL, " ", &saveptr);
                }
                continue;  /* skip non-training path below */
            }

            struct Move nn_choice = nn_pick_move(&g_net, state.board, aiColour);
            
            if (nn_choice.fromX < 0)
            {
                puzzle_success = 0;
                break;
            }
            
            // Get expected move
            const char *expectedMove = token;
            token = strtok_r(NULL, " ", &saveptr);
            
            // Format AI's move as UCI
            char aiMoveNotation[32];
            snprintf(aiMoveNotation, sizeof(aiMoveNotation), "%c%d%c%d",
                     'a' + nn_choice.fromX, nn_choice.fromY + 1,
                     'a' + nn_choice.toX,   nn_choice.toY   + 1);
            
            // Check if move matches expected
            if (strcmp(aiMoveNotation, expectedMove) != 0)
            {
                // Move doesn't match - check if it's checkmate (valid alternative solution)
                enum Colour opponentColour = (aiColour == WHITE) ? BLACK : WHITE;
                
                // Execute the AI's move to check for checkmate
                int fx = nn_choice.fromX;
                int fy = nn_choice.fromY;
                int tx = nn_choice.toX;
                int ty = nn_choice.toY;
                
                state.board[tx][ty] = state.board[fx][fy];
                state.board[fx][fy].type = -1;
                state.board[fx][fy].colour = -1;
                state.board[tx][ty].hasMoved = 1;
                
                // Handle castling
                if (state.board[tx][ty].type == KING && fx == 4)
                {
                    if (tx == 6)
                    {
                        state.board[5][ty] = state.board[7][ty];
                        state.board[7][ty].type = -1;
                        state.board[7][ty].colour = -1;
                        state.board[5][ty].hasMoved = 1;
                    }
                    else if (tx == 2)
                    {
                        state.board[3][ty] = state.board[0][ty];
                        state.board[0][ty].type = -1;
                        state.board[0][ty].colour = -1;
                        state.board[3][ty].hasMoved = 1;
                    }
                }
                
                state.lastMove.fromX = fx;
                state.lastMove.fromY = fy;
                state.lastMove.toX = tx;
                state.lastMove.toY = ty;
                
                if (!isCheckmate(state.board, opponentColour))
                {
                    puzzle_success = 0;
                    break;
                }
                // Checkmate found - puzzle solved even though move differs
                break;
            }
            else
            {
                // Execute the matching move
                if (!executeUciMove_ThreadSafe(&state, expectedMove))
                {
                    puzzle_success = 0;
                    break;
                }
            }
            
            recordBoardHistory_ThreadSafe(&state);
            sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
            
            // If there's an opponent response, execute it
            if (token)
            {
                if (!executeUciMove_ThreadSafe(&state, token))
                {
                    puzzle_success = 0;
                    break;
                }
                recordBoardHistory_ThreadSafe(&state);
                sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
                token = strtok_r(NULL, " ", &saveptr);
            }
        }
        
        // Store result
        args->results[puzzle_idx] =
            (args->train_nn ? nn_first_move_correct : puzzle_success) ? 1 : 0;
        
        // Update thread status with result
        if (thread_id >= 0)
        {
            pthread_mutex_lock(&status_mutex);
            global_thread_statuses[thread_id].last_result =
                (args->train_nn ? nn_first_move_correct : puzzle_success) ? 1 : 0;
            pthread_mutex_unlock(&status_mutex);
        }
        
        // Update progress
        pthread_mutex_lock(args->results_lock);
        (*args->completed_count)++;
        if (args->progress_callback && ((*args->completed_count) % 5 == 0 || *args->completed_count == 1))
        {
            int passes = 0;
            for (int i = 0; i < args->total_puzzles; i++)
                if (args->results[i] == 1) passes++;
            args->progress_callback(*args->completed_count, args->total_puzzles, passes);
        }
        pthread_mutex_unlock(args->results_lock);
        
        // Cleanup
        cleanupGameState(&state);
    }
    
    // Don't mark thread as inactive yet - keep status visible
    // Will be cleared after all threads complete
    
    return NULL;
}

// Main function: test puzzles with specified number of threads
int playPuzzlesMultiThreaded(const char *filename, int searchDepth, int numPuzzles, int numThreads,
                              void (*progress_callback)(int, int, int))
{
    if (numThreads < 1)
        numThreads = 1;
    if (numThreads > MAX_THREADS)
        numThreads = MAX_THREADS;
    
    // Initialize global thread status tracking
    pthread_mutex_lock(&status_mutex);
    global_num_threads = numThreads;
    for (int i = 0; i < MAX_THREADS; i++)
    {
        global_thread_statuses[i].thread_id = -1;
        global_thread_statuses[i].current_puzzle = -1;
        global_thread_statuses[i].last_result = -1;
        global_thread_statuses[i].is_active = 0;
    }
    pthread_mutex_unlock(&status_mutex);
    
    // Allocate results array
    int *results = (int *)calloc(numPuzzles, sizeof(int));
    if (!results)
    {
        fprintf(stderr, "Failed to allocate results array\n");
        return 0;
    }
    
    // Shared state for progress tracking
    pthread_mutex_t results_lock = PTHREAD_MUTEX_INITIALIZER;
    int completed_count = 0;
    
    // Create threads
    pthread_t threads[MAX_THREADS];
    ThreadWorkerArgs thread_args[MAX_THREADS];
    
    int puzzles_per_thread = numPuzzles / numThreads;
    int remainder = numPuzzles % numThreads;
    
    int start_puzzle = 0;
    for (int i = 0; i < numThreads; i++)
    {
        int end_puzzle = start_puzzle + puzzles_per_thread + (i < remainder ? 1 : 0);
        
        thread_args[i].puzzle_file = filename;
        thread_args[i].search_depth = searchDepth;
        thread_args[i].start_puzzle = start_puzzle;
        thread_args[i].end_puzzle = end_puzzle;
        thread_args[i].results = results;
        thread_args[i].progress_callback = progress_callback;
        thread_args[i].results_lock = &results_lock;
        thread_args[i].completed_count = &completed_count;
        thread_args[i].total_puzzles = numPuzzles;
        thread_args[i].train_nn = 0;
        thread_args[i].learning_rate = 0.0f;
        
        if (pthread_create(&threads[i], NULL, puzzle_worker_thread, &thread_args[i]) != 0)
        {
            fprintf(stderr, "Failed to create thread %d\n", i);
            free(results);
            return 0;
        }
        
        start_puzzle = end_puzzle;
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < numThreads; i++)
    {
        pthread_join(threads[i], NULL);
    }
    
    // Count total passes
    int passes = 0;
    for (int i = 0; i < numPuzzles; i++)
    {
        if (results[i] == 1)
            passes++;
    }
    
    // Final callback
    if (progress_callback)
    {
        progress_callback(numPuzzles, numPuzzles, passes);
    }
    
    // Clear thread status after everything is done
    pthread_mutex_lock(&status_mutex);
    global_num_threads = 0;
    pthread_mutex_unlock(&status_mutex);
    
    free(results);
    pthread_mutex_destroy(&results_lock);
    
    return passes;
}

// Convenience function with default puzzle count
int playPuzzles1To100_MultiThreaded(const char *filename, int searchDepth, int numThreads)
{
    return playPuzzlesMultiThreaded(filename, searchDepth, PUZZLE_TEST_COUNT, numThreads, puzzle_progress_callback);
}

// Alias for compatibility with training code
int playPuzzles1To100_Threaded(const char *filename, int searchDepth, int numThreads)
{
    return playPuzzles1To100_MultiThreaded(filename, searchDepth, numThreads);
}

// Get thread status for TUI display
int* get_thread_puzzle_statuses(int *num_threads_out, int *statuses_out)
{
    pthread_mutex_lock(&status_mutex);
    
    *num_threads_out = global_num_threads;
    
    // Copy status information
    for (int i = 0; i < global_num_threads && i < MAX_THREADS; i++)
    {
        statuses_out[i * 2] = global_thread_statuses[i].current_puzzle;
        statuses_out[i * 2 + 1] = global_thread_statuses[i].last_result;
    }
    
    pthread_mutex_unlock(&status_mutex);
    
    return statuses_out;
}

/* ════════════════════════════════════════════════════════════════════════════
 * playPuzzlesMultiThreaded_Train
 *
 * Runs the puzzle loop with NN training (teacher forcing) enabled on every
 * worker thread.  After all threads finish the weights are written to
 * "nn_weights.bin" so progress survives between runs.
 *
 * Returns the number of puzzles where the NN predicted the correct move.
 * ════════════════════════════════════════════════════════════════════════════ */
int playPuzzlesMultiThreaded_Train(const char *filename, float learning_rate,
                                   int numPuzzles, int numThreads,
                                   void (*progress_callback)(int, int, int))
{
    if (numThreads < 1) numThreads = 1;
    if (numThreads > MAX_THREADS) numThreads = MAX_THREADS;

    /* Initialise (or load) NN weights once before spawning threads */
    if (!g_net.weights) {
        if (!nn_load(&g_net, "nn_weights.bin"))
            nn_init(&g_net);
    }

    /* Initialise thread status tracking */
    pthread_mutex_lock(&status_mutex);
    global_num_threads = numThreads;
    for (int i = 0; i < MAX_THREADS; i++) {
        global_thread_statuses[i].thread_id   = -1;
        global_thread_statuses[i].current_puzzle = -1;
        global_thread_statuses[i].last_result = -1;
        global_thread_statuses[i].is_active   = 0;
    }
    pthread_mutex_unlock(&status_mutex);

    int *results = (int *)calloc(numPuzzles, sizeof(int));
    if (!results) { fprintf(stderr, "OOM in playPuzzlesMultiThreaded_Train\n"); return 0; }

    pthread_mutex_t results_lock = PTHREAD_MUTEX_INITIALIZER;
    int completed_count = 0;

    pthread_t threads[MAX_THREADS];
    ThreadWorkerArgs thread_args[MAX_THREADS];

    int puzzles_per_thread = numPuzzles / numThreads;
    int remainder          = numPuzzles % numThreads;
    int start_puzzle       = 0;

    for (int i = 0; i < numThreads; i++) {
        int end_puzzle = start_puzzle + puzzles_per_thread + (i < remainder ? 1 : 0);

        thread_args[i].puzzle_file      = filename;
        thread_args[i].search_depth     = 0;          /* unused in training mode */
        thread_args[i].start_puzzle     = start_puzzle;
        thread_args[i].end_puzzle       = end_puzzle;
        thread_args[i].results          = results;
        thread_args[i].progress_callback= progress_callback;
        thread_args[i].results_lock     = &results_lock;
        thread_args[i].completed_count  = &completed_count;
        thread_args[i].total_puzzles    = numPuzzles;
        thread_args[i].train_nn         = 1;
        thread_args[i].learning_rate    = learning_rate;

        if (pthread_create(&threads[i], NULL, puzzle_worker_thread, &thread_args[i]) != 0) {
            fprintf(stderr, "Failed to create training thread %d\n", i);
            free(results);
            return 0;
        }
        start_puzzle = end_puzzle;
    }

    for (int i = 0; i < numThreads; i++)
        pthread_join(threads[i], NULL);

    /* Tally results */
    int passes = 0;
    for (int i = 0; i < numPuzzles; i++)
        if (results[i] == 1) passes++;

    if (progress_callback)
        progress_callback(numPuzzles, numPuzzles, passes);

    free(results);
    pthread_mutex_destroy(&results_lock);

    /* Persist updated weights */
    if (nn_save(&g_net, "nn_weights.bin"))
        printf("NN weights saved to nn_weights.bin\n");

    return passes;
}

/* Convenience wrapper using the global puzzle count and default learning rate */
int playPuzzles1To100_MT_Train(const char *filename, float learning_rate, int numThreads)
{
    return playPuzzlesMultiThreaded_Train(filename, learning_rate,
                                          PUZZLE_TEST_COUNT, numThreads,
                                          puzzle_progress_callback);
}
