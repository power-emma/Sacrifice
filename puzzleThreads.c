/*
 * puzzleThreads.c - Multithreaded puzzle testing system
 * 
 * This module implements parallel puzzle testing where each thread processes
 * one puzzle independently and reports pass/fail results. Designed for use
 * with the training system to evaluate parameter sets faster.
 * 
 * Uses thread-local storage to maintain independent board state for each thread,
 * enabling fully parallel execution without serialization bottlenecks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#include "chess.h"

/* ======================== Configuration ======================== */

#define DEFAULT_THREAD_COUNT 8
#define MAX_THREADS 32
#define BOARD_HISTORY_SIZE 512

// Export puzzle test count for use in training and TUI
int PUZZLE_TEST_COUNT = 500;

/* ======================== Thread-Local Storage ======================== */

typedef struct {
    struct Piece board[8][8];
    struct Move lastMove;
    struct Piece boardHistory[200][8][8];  // Thread-local board history
    int boardHistoryCount;                  // Thread-local history counter
    int depth;                              // Thread-local depth counter for recursion
    int halfmoveClock;                      // Thread-local halfmove clock for 50-move rule
    unsigned long long evalCount;           // Thread-local evaluation counter
    unsigned long long ttHitCount;          // Thread-local transposition table hits
    unsigned long long abPruneCount;        // Thread-local alpha-beta prune count
    unsigned long long staticPruneCount;    // Thread-local static prune count
} ThreadLocalState;

static __thread ThreadLocalState thread_state;

/* ======================== Work Queue ======================== */

typedef struct {
    int puzzle_index;
    int assigned;
    int completed;
} WorkItem;

typedef struct {
    WorkItem *items;
    int total_items;
    int next_item;
    pthread_mutex_t lock;
    pthread_cond_t work_available;
} WorkQueue;

/* ======================== Thread-Safe Data Structures ======================== */

typedef struct {
    int puzzle_index;        // Which puzzle (0-99)
    int result;              // 1 = pass, 0 = fail
    int completed;           // 1 = thread finished, 0 = still working
} PuzzleResult;

typedef struct {
    int thread_id;           // Thread identifier (0-7 for 8 threads)
    int current_puzzle;      // Current puzzle index being tested
    int last_result;         // Last puzzle result: 1=pass, 0=fail, -1=not started
    int is_active;           // 1 if thread is currently working
} ThreadStatus;

typedef struct {
    PuzzleResult *results;   // Array of results, one per puzzle
    int total_puzzles;
    int completed_count;     // Count of completed puzzles
    int num_threads;         // Number of worker threads
    ThreadStatus *thread_statuses;  // Status of each thread
    pthread_mutex_t lock;    // Synchronization
    void (*progress_callback)(int completed, int total, int passes);  // Optional callback
} ThreadPoolState;

/* ======================== Global Thread Pool State ======================== */

static ThreadPoolState *last_pool_state = NULL;
static ThreadPoolState global_pool_state;
static pthread_mutex_t pool_state_access = PTHREAD_MUTEX_INITIALIZER;

/* ======================== Helper: Swap Global State With Thread-Local ======================== */

/**
 * Save current global state to thread-local storage
 */
static void save_thread_state(void)
{
    memcpy(thread_state.board, board, sizeof(board));
    thread_state.lastMove = lastMove;
    thread_state.halfmoveClock = halfmoveClock;
    thread_state.depth = depth;
    thread_state.evalCount = evalCount;
    thread_state.ttHitCount = ttHitCount;
    thread_state.abPruneCount = abPruneCount;
    thread_state.staticPruneCount = staticPruneCount;
}

/**
 * Restore thread-local state to globals
 * Must be called before using engine functions that expect global state
 */
static void restore_thread_state(void)
{
    memcpy(board, thread_state.board, sizeof(board));
    lastMove = thread_state.lastMove;
    halfmoveClock = thread_state.halfmoveClock;
    depth = thread_state.depth;
    evalCount = thread_state.evalCount;
    ttHitCount = thread_state.ttHitCount;
    abPruneCount = thread_state.abPruneCount;
    staticPruneCount = thread_state.staticPruneCount;
    // Reset board history for this thread's work
    memset(boardHistory, 0, sizeof(boardHistory));
    boardHistoryCount = 0;
}

/**
 * Update thread-local state from global after engine moves
 */
static void sync_thread_state(void)
{
    thread_state.lastMove = lastMove;
    thread_state.halfmoveClock = halfmoveClock;
    thread_state.depth = depth;
    thread_state.evalCount = evalCount;
    thread_state.ttHitCount = ttHitCount;
    thread_state.abPruneCount = abPruneCount;
    thread_state.staticPruneCount = staticPruneCount;
    thread_state.boardHistoryCount = boardHistoryCount;
}

/* ======================== Thread Worker Function ======================== */

typedef struct {
    const char *puzzle_file;
    int search_depth;
    ThreadPoolState *pool_state;
    WorkQueue *work_queue;
} ThreadWorkerArgs;

/**
 * Worker function for each thread
 * Each thread pulls puzzles from work queue and solves them
 * Fully parallel with minimal locking - each thread works continuously until queue is empty
 */
static void* puzzle_worker_thread(void *arg)
{
    ThreadWorkerArgs *args = (ThreadWorkerArgs *)arg;
    const char *puzzle_file = args->puzzle_file;
    int search_depth = args->search_depth;
    ThreadPoolState *pool_state = args->pool_state;
    WorkQueue *work_queue = args->work_queue;
    
    int puzzle_index;
    int thread_id = -1;
    
    // Process puzzles from work queue until empty
    while (1)
    {
        // Get next puzzle from queue
        pthread_mutex_lock(&work_queue->lock);
        if (work_queue->next_item >= work_queue->total_items)
        {
            pthread_mutex_unlock(&work_queue->lock);
            break;  // No more work
        }
        
        puzzle_index = work_queue->next_item;
        work_queue->next_item++;
        
        // Determine thread_id from puzzle index for status tracking
        if (thread_id < 0) thread_id = puzzle_index % pool_state->num_threads;
        
        pthread_mutex_unlock(&work_queue->lock);
        
        // Load the puzzle
        struct LichessPuzzle puzzle;
        if (!loadLichessPuzzle(puzzle_file, puzzle_index, &puzzle))
        {
            pthread_mutex_lock(&pool_state->lock);
            pool_state->results[puzzle_index].result = 0;
            pool_state->results[puzzle_index].completed = 1;
            pool_state->completed_count++;
            pthread_mutex_unlock(&pool_state->lock);
            continue;
        }
        
        // Update thread status
        pthread_mutex_lock(&pool_state->lock);
        if (thread_id < pool_state->num_threads)
        {
            pool_state->thread_statuses[thread_id].current_puzzle = puzzle_index;
            pool_state->thread_statuses[thread_id].is_active = 1;
        }
        pthread_mutex_unlock(&pool_state->lock);
        
        // Initialize thread-local board
        memset(thread_state.board, 0, sizeof(thread_state.board));
        for (int i = 0; i < 8; i++)
        {
            for (int j = 0; j < 8; j++)
            {
                thread_state.board[i][j].type = -1;
                thread_state.board[i][j].colour = -1;
                thread_state.board[i][j].hasMoved = 0;
            }
        }
        thread_state.lastMove.fromX = 0;
        thread_state.lastMove.fromY = 0;
        thread_state.lastMove.toX = 0;
        thread_state.lastMove.toY = 0;
        
        // Initialize thread-local board history and evaluation counters
        memset(thread_state.boardHistory, 0, sizeof(thread_state.boardHistory));
        thread_state.boardHistoryCount = 0;
        thread_state.halfmoveClock = 0;
        thread_state.depth = 0;
        thread_state.evalCount = 0;
        thread_state.ttHitCount = 0;
        thread_state.abPruneCount = 0;
        thread_state.staticPruneCount = 0;
        
        // Swap in thread-local board
        memcpy(board, thread_state.board, sizeof(board));
        lastMove = thread_state.lastMove;
        halfmoveClock = 0;
        depth = 0;
        evalCount = 0;
        ttHitCount = 0;
        abPruneCount = 0;
        staticPruneCount = 0;
        
        // Load FEN position
        enum Colour sideToMove = WHITE;
        if (!loadBoardFromFEN(puzzle.fen, board))
        {
            pthread_mutex_lock(&pool_state->lock);
            pool_state->results[puzzle_index].result = 0;
            pool_state->results[puzzle_index].completed = 1;
            pool_state->completed_count++;
            if (thread_id < pool_state->num_threads)
                pool_state->thread_statuses[thread_id].is_active = 0;
            pthread_mutex_unlock(&pool_state->lock);
            continue;
        }
        
        sideToMove = getTurnFromFEN(puzzle.fen);
        save_thread_state();
        
        // Parse puzzle moves
        char movesCopy[512];
        strcpy(movesCopy, puzzle.moves);
        char *saveptr = NULL;  // For strtok_r
        char *token = strtok_r(movesCopy, " ", &saveptr);
        
        // Execute first move
        if (token)
        {
            restore_thread_state();
            if (executeUciMove(board, token))
            {
                recordBoardHistory();
                save_thread_state();
                sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
                token = strtok_r(NULL, " ", &saveptr);
            }
            else
            {
                pthread_mutex_lock(&pool_state->lock);
                pool_state->results[puzzle_index].result = 0;
                pool_state->results[puzzle_index].completed = 1;
                pool_state->completed_count++;
                if (thread_id < pool_state->num_threads)
                    pool_state->thread_statuses[thread_id].is_active = 0;
                pthread_mutex_unlock(&pool_state->lock);
                continue;
            }
        }
        
        // Solve puzzle
        int puzzle_success = 1;
        while (token)
        {
            restore_thread_state();
            enum Colour aiColour = sideToMove;
            moveRanking(board, search_depth, aiColour);
            sync_thread_state();
            
            const char *expectedMove = token;
            token = strtok_r(NULL, " ", &saveptr);  // Advance to opponent's response or NULL
            
            char aiMoveNotation[32];
            snprintf(aiMoveNotation, sizeof(aiMoveNotation), "%c%d%c%d",
                     'a' + thread_state.lastMove.fromX, thread_state.lastMove.fromY + 1,
                     'a' + thread_state.lastMove.toX, thread_state.lastMove.toY + 1);
            
            if (strcmp(aiMoveNotation, expectedMove) != 0)
            {
                // Move doesn't match - check if it's checkmate (valid alternative solution)
                enum Colour opponentColour = (aiColour == WHITE) ? BLACK : WHITE;
                if (!isCheckmate(board, opponentColour))
                {
                    puzzle_success = 0;
                    break;
                }
                // Checkmate found - puzzle solved even though move differs
                save_thread_state();
                break;  // No more moves after checkmate
            }
            
            save_thread_state();
            sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
            
            if (token)
            {
                restore_thread_state();
                if (!executeUciMove(board, token))
                {
                    puzzle_success = 0;
                    break;
                }
                recordBoardHistory();
                save_thread_state();
                sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
                token = strtok_r(NULL, " ", &saveptr);
            }
        }
        
        // Record result
        pthread_mutex_lock(&pool_state->lock);
        int result = puzzle_success ? 1 : 0;
        if (thread_id < pool_state->num_threads)
        {
            pool_state->thread_statuses[thread_id].last_result = result;
            pool_state->thread_statuses[thread_id].is_active = 0;
        }
        pool_state->results[puzzle_index].result = result;
        pool_state->results[puzzle_index].completed = 1;
        pool_state->completed_count++;
        
        // Count passes for callback (batched - only call every N completions to reduce lock contention)
        if ((pool_state->completed_count % 5) == 0 && pool_state->progress_callback != NULL)
        {
            int passes = 0;
            for (int i = 0; i < pool_state->total_puzzles; i++)
            {
                if (pool_state->results[i].completed && pool_state->results[i].result)
                    passes++;
            }
            pthread_mutex_unlock(&pool_state->lock);
            pool_state->progress_callback(pool_state->completed_count, pool_state->total_puzzles, passes);
            pthread_mutex_lock(&pool_state->lock);
        }
        
        pthread_mutex_unlock(&pool_state->lock);
    }
    
    free(args);
    return NULL;
}


/* ======================== Public Thread Pool Interface ======================== */

/**
 * Test all puzzles using a thread pool
 * Returns: number of puzzles passed
 * 
 * This implementation is fully parallel:
 * - Each thread maintains its own thread-local board state
 * - Threads swap state in/out with globals only when calling engine functions
 * - No serialization on puzzle solving - true parallel evaluation
 * - Callback updates are protected by a single mutex (fast operation)
 */
int playPuzzles1To100_Threaded(const char *filename, int searchDepth, int num_threads)
{
    if (num_threads < 1) num_threads = DEFAULT_THREAD_COUNT;
    if (num_threads > MAX_THREADS) num_threads = MAX_THREADS;
    
    // Initialize global thread pool state
    memset(&global_pool_state, 0, sizeof(global_pool_state));
    ThreadPoolState *pool_state = &global_pool_state;
    
    pool_state->total_puzzles = PUZZLE_TEST_COUNT;
    pool_state->completed_count = 0;
    pool_state->num_threads = num_threads;
    pool_state->progress_callback = puzzle_progress_callback;
    
    pthread_mutex_init(&pool_state->lock, NULL);
    
    pool_state->results = (PuzzleResult *)malloc(sizeof(PuzzleResult) * PUZZLE_TEST_COUNT);
    for (int i = 0; i < PUZZLE_TEST_COUNT; i++)
    {
        pool_state->results[i].puzzle_index = i;
        pool_state->results[i].result = 0;
        pool_state->results[i].completed = 0;
    }
    
    // Initialize thread status tracking
    pool_state->thread_statuses = (ThreadStatus *)malloc(sizeof(ThreadStatus) * num_threads);
    for (int i = 0; i < num_threads; i++)
    {
        pool_state->thread_statuses[i].thread_id = i;
        pool_state->thread_statuses[i].current_puzzle = -1;
        pool_state->thread_statuses[i].last_result = -1;
        pool_state->thread_statuses[i].is_active = 0;
    }
    
    // Initialize work queue
    WorkQueue work_queue;
    pthread_mutex_init(&work_queue.lock, NULL);
    pthread_cond_init(&work_queue.work_available, NULL);
    work_queue.items = (WorkItem *)malloc(sizeof(WorkItem) * PUZZLE_TEST_COUNT);
    work_queue.total_items = PUZZLE_TEST_COUNT;
    work_queue.next_item = 0;
    for (int i = 0; i < PUZZLE_TEST_COUNT; i++)
    {
        work_queue.items[i].puzzle_index = i;
        work_queue.items[i].assigned = 0;
        work_queue.items[i].completed = 0;
    }
    
    // Store reference for TUI access
    pthread_mutex_lock(&pool_state_access);
    last_pool_state = pool_state;
    pthread_mutex_unlock(&pool_state_access);
    
    // Create all worker threads upfront
    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < num_threads; i++)
    {
        ThreadWorkerArgs *args = (ThreadWorkerArgs *)malloc(sizeof(ThreadWorkerArgs));
        args->puzzle_file = filename;
        args->search_depth = searchDepth;
        args->pool_state = pool_state;
        args->work_queue = &work_queue;
        
        if (pthread_create(&threads[i], NULL, puzzle_worker_thread, args) != 0)
        {
            fprintf(stderr, "Error creating thread %d\n", i);
            free(args);
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }
    
    // Count passes
    int passes = 0;
    for (int i = 0; i < PUZZLE_TEST_COUNT; i++)
    {
        if (pool_state->results[i].result)
            passes++;
    }
    
    // Final callback with complete count
    if (pool_state->progress_callback != NULL)
    {
        pool_state->progress_callback(pool_state->completed_count, pool_state->total_puzzles, passes);
    }
    
    // Cleanup
    free(pool_state->results);
    free(pool_state->thread_statuses);
    free(work_queue.items);
    pthread_mutex_destroy(&pool_state->lock);
    pthread_mutex_destroy(&work_queue.lock);
    pthread_cond_destroy(&work_queue.work_available);
    
    return passes;
}

/**
 * Get current thread statuses for TUI display
 * Returns array of thread statuses (caller should not free)
 * num_threads_out: pointer to int where thread count will be stored
 */
int* get_thread_puzzle_statuses(int *num_threads_out, int *statuses_out)
{
    pthread_mutex_lock(&pool_state_access);
    
    if (last_pool_state == NULL)
    {
        *num_threads_out = 0;
        pthread_mutex_unlock(&pool_state_access);
        return NULL;
    }
    
    *num_threads_out = last_pool_state->num_threads;
    
    // Copy current puzzle info and result for each thread
    for (int i = 0; i < last_pool_state->num_threads; i++)
    {
        ThreadStatus *ts = &last_pool_state->thread_statuses[i];
        statuses_out[i * 2] = ts->current_puzzle;      // Current puzzle index
        statuses_out[i * 2 + 1] = ts->last_result;     // -1=not started, 0=fail, 1=pass
    }
    
    pthread_mutex_unlock(&pool_state_access);
    return statuses_out;
}

/**
 * Test all puzzles using a thread pool with custom callback
 */
int playPuzzles1To100_ThreadedWithCallback(const char *filename, int searchDepth, int num_threads,
                                           void (*callback)(int, int, int))
{
    void (*old_callback)(int, int, int) = puzzle_progress_callback;
    puzzle_progress_callback = callback;
    
    int result = playPuzzles1To100_Threaded(filename, searchDepth, num_threads);
    
    puzzle_progress_callback = old_callback;
    return result;
}
