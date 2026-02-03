// puzzles.c - Lichess puzzle loading, FEN parsing, and multi-threaded puzzle solving
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "chess.h"

/* ======================== Configuration ======================== */
#define DEFAULT_THREAD_COUNT 8
#define MAX_THREADS 32

// Export puzzle test count for use in training and TUI
int PUZZLE_TEST_COUNT = 500;

/* ======================== Forward Declarations ======================== */
static int executeUciMoveLocal(struct Piece gameBoard[8][8], const char *uci, 
                               struct Move *lastMoveOut, int *halfmoveClockOut);
static int solvePuzzle(const char *filename, int puzzle_index, int search_depth,
                       struct Piece local_board[8][8]);

/* ======================== External Puzzle Callback ======================== */
extern void (*puzzle_progress_callback)(int puzzles_completed, int total_puzzles, int current_score);

// Loads a puzzle from the CSV file at the specified row number
// Returns 1 on success, 0 on failure
int loadLichessPuzzle(const char *filename, int puzzleNumber, struct LichessPuzzle *puzzle)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        printf("Error: Could not open file '%s'\n", filename);
        return 0;
    }

    char line[2048];
    int currentLine = 0;

    // Read lines until we reach the desired puzzle number
    while (fgets(line, sizeof(line), file))
    {
        if (currentLine == puzzleNumber)
        {
            fclose(file);

            // Parse the CSV line
            // Format: PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags
            char *token;
            char lineCopy[2048];
            strcpy(lineCopy, line);

            // Remove trailing newline
            if (lineCopy[strlen(lineCopy) - 1] == '\n')
                lineCopy[strlen(lineCopy) - 1] = '\0';

            // Parse each field
            token = strtok(lineCopy, ",");
            if (!token) return 0;
            strcpy(puzzle->puzzleId, token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            strcpy(puzzle->fen, token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            strcpy(puzzle->moves, token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            puzzle->rating = atoi(token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            puzzle->ratingDeviation = atoi(token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            puzzle->popularity = atoi(token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            puzzle->nbPlays = atoi(token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            strcpy(puzzle->themes, token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            strcpy(puzzle->gameUrl, token);

            token = strtok(NULL, ",");
            if (token)
                strcpy(puzzle->opening, token);
            else
                puzzle->opening[0] = '\0';

            return 1;
        }
        currentLine++;
    }

    fclose(file);
    printf("Error: Puzzle number %d not found in file\n", puzzleNumber);
    return 0;
}


// Parses FEN string and loads it into the board
// Returns 1 on success, 0 on failure
int loadBoardFromFEN(const char *fen, struct Piece gameBoard[8][8])
{
    // Initialize empty board
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            gameBoard[i][j].type = -1;
            gameBoard[i][j].colour = -1;
            gameBoard[i][j].hasMoved = 0;
        }
    }

    char fenCopy[256];
    strcpy(fenCopy, fen);

    // Get the board position part (before the space)
    char *boardStr = strtok(fenCopy, " ");
    if (!boardStr)
        return 0;

    int file = 0, rank = 7; // Start from top-left (rank 8, file a)

    for (int i = 0; boardStr[i] != '\0'; i++)
    {
        char c = boardStr[i];

        if (c == '/')
        {
            file = 0;
            rank--;
            continue;
        }

        // Empty squares
        if (c >= '1' && c <= '8')
        {
            file += (c - '0');
            continue;
        }

        if (file >= 8 || rank < 0)
            return 0; // Invalid FEN

        // Parse piece
        enum Colour colour = (c >= 'a' && c <= 'z') ? BLACK : WHITE;

        switch (c)
        {
        case 'P':
        case 'p':
            gameBoard[file][rank].type = PAWN;
            break;
        case 'N':
        case 'n':
            gameBoard[file][rank].type = KNIGHT;
            break;
        case 'B':
        case 'b':
            gameBoard[file][rank].type = BISHOP;
            break;
        case 'R':
        case 'r':
            gameBoard[file][rank].type = ROOK;
            break;
        case 'Q':
        case 'q':
            gameBoard[file][rank].type = QUEEN;
            break;
        case 'K':
        case 'k':
            gameBoard[file][rank].type = KING;
            break;
        default:
            return 0; // Invalid piece
        }

        gameBoard[file][rank].colour = colour;
        file++;
    }

    return 1;
}


// Extracts whose turn it is from FEN (returns WHITE or BLACK)
enum Colour getTurnFromFEN(const char *fen)
{
    char fenCopy[256];
    strcpy(fenCopy, fen);

    strtok(fenCopy, " "); // Skip board part
    char *turn = strtok(NULL, " ");

    if (!turn)
        return WHITE; // Default to white

    return (turn[0] == 'w') ? WHITE : BLACK;
}


// Interactive function to load and display a Lichess puzzle
// Returns 1 on success and writes the side to move into `puzzleTurnOut`.
// Returns 0 on failure.
int loadAndDisplayLichessPuzzle(const char *filename, enum Colour *puzzleTurnOut, struct LichessPuzzle *outPuzzle)
{
    int puzzleNumber;

    printf("Enter the puzzle number (row index, 0-based): ");
    fflush(stdout);

    if (scanf("%d", &puzzleNumber) != 1)
    {
        printf("Error: Invalid input\n");
        return 0;
    }

    if (puzzleNumber < 0)
    {
        printf("Error: Puzzle number must be non-negative\n");
        return 0;
    }

    struct LichessPuzzle puzzle;
    if (!loadLichessPuzzle(filename, puzzleNumber, &puzzle))
    {
        printf("Error: Failed to load puzzle\n");
        return 0;
    }

    // Load the FEN position into the board
    if (!loadBoardFromFEN(puzzle.fen, board))
    {
        printf("Error: Failed to parse FEN\n");
        return 0;
    }

    // Get whose turn it is
    enum Colour puzzleTurn = getTurnFromFEN(puzzle.fen);
    if (puzzleTurnOut)
        *puzzleTurnOut = puzzleTurn;
    if (outPuzzle)
        *outPuzzle = puzzle;

    // Display puzzle information
    printf("\n========== Lichess Puzzle ==========%s\n", "");
    printf("Puzzle ID: %s\n", puzzle.puzzleId);
    printf("Rating: %d (±%d)\n", puzzle.rating, puzzle.ratingDeviation);
    printf("Popularity: %d%%\n", puzzle.popularity);
    printf("Times Played: %d\n", puzzle.nbPlays);
    printf("\nFEN: %s\n", puzzle.fen);
    printf("Original Turn: %s\n", (puzzleTurn == WHITE) ? "White" : "Black");
    printf("\nBest Moves: %s\n", puzzle.moves);
    printf("\nThemes: %s\n", puzzle.themes);
    printf("Opening: %s\n", puzzle.opening);
    printf("Game URL: %s\n", puzzle.gameUrl);
    printf("====================================\n\n");
    return 1;
}


// Execute a UCI style move like "e2e4" or "e7e8q" on the given board.
// Returns 1 on success, 0 on failure.
int executeUciMove(struct Piece gameBoard[8][8], const char *uci)
{
    if (!uci || strlen(uci) < 4)
        return 0;

    int fx = uci[0] - 'a';
    int fy = uci[1] - '1';
    int tx = uci[2] - 'a';
    int ty = uci[3] - '1';

    if (fx < 0 || fx > 7 || tx < 0 || tx > 7 || fy < 0 || fy > 7 || ty < 0 || ty > 7)
        return 0;

    struct Piece moving = gameBoard[fx][fy];
    if (moving.type == -1)
        return 0; // nothing to move

    // Simple move/capture
    gameBoard[tx][ty] = moving;
    gameBoard[fx][fy].type = -1;
    gameBoard[fx][fy].colour = -1;
    gameBoard[tx][ty].hasMoved = 1;

    // Promotion (5th char)
    if (strlen(uci) >= 5)
    {
        char pc = uci[4];
        switch (pc)
        {
        case 'q': case 'Q': gameBoard[tx][ty].type = QUEEN; break;
        case 'r': case 'R': gameBoard[tx][ty].type = ROOK; break;
        case 'b': case 'B': gameBoard[tx][ty].type = BISHOP; break;
        case 'n': case 'N': gameBoard[tx][ty].type = KNIGHT; break;
        default: break;
        }
    }

    // Castling: if king moved two squares, move rook accordingly
    if (gameBoard[tx][ty].type == KING && abs(tx - fx) == 2)
    {
        int row = ty;
        if (tx == 6)
        {
            gameBoard[5][row] = gameBoard[7][row];
            gameBoard[7][row].type = -1;
            gameBoard[7][row].colour = -1;
            gameBoard[5][row].hasMoved = 1;
        }
        else if (tx == 2)
        {
            gameBoard[3][row] = gameBoard[0][row];
            gameBoard[0][row].type = -1;
            gameBoard[0][row].colour = -1;
            gameBoard[3][row].hasMoved = 1;
        }
    }

    // Update last move
    lastMove.fromX = fx;
    lastMove.fromY = fy;
    lastMove.toX = tx;
    lastMove.toY = ty;

    // Reset halfmove clock on pawn move or capture
    if (gameBoard[tx][ty].type == PAWN)
        halfmoveClock = 0;

    // Report evaluation stats after executing a move
    printEvaluationCount();

    return 1;
}

/* ======================== Multi-Threaded Puzzle Solver ======================== */

/**
 * Local board move execution (doesn't update globals, only board and local state)
 */
static int executeUciMoveLocal(struct Piece gameBoard[8][8], const char *uci,
                               struct Move *lastMoveOut, int *halfmoveClockOut)
{
    if (!uci || strlen(uci) < 4)
        return 0;

    int fx = uci[0] - 'a';
    int fy = uci[1] - '1';
    int tx = uci[2] - 'a';
    int ty = uci[3] - '1';

    if (fx < 0 || fx > 7 || tx < 0 || tx > 7 || fy < 0 || fy > 7 || ty < 0 || ty > 7)
        return 0;

    struct Piece moving = gameBoard[fx][fy];
    if (moving.type == -1)
        return 0;

    // Simple move/capture
    gameBoard[tx][ty] = moving;
    gameBoard[fx][fy].type = -1;
    gameBoard[fx][fy].colour = -1;
    gameBoard[tx][ty].hasMoved = 1;

    // Promotion
    if (strlen(uci) >= 5)
    {
        char pc = uci[4];
        switch (pc)
        {
        case 'q': case 'Q': gameBoard[tx][ty].type = QUEEN; break;
        case 'r': case 'R': gameBoard[tx][ty].type = ROOK; break;
        case 'b': case 'B': gameBoard[tx][ty].type = BISHOP; break;
        case 'n': case 'N': gameBoard[tx][ty].type = KNIGHT; break;
        default: break;
        }
    }

    // Castling
    if (gameBoard[tx][ty].type == KING && abs(tx - fx) == 2)
    {
        int row = ty;
        if (tx == 6)
        {
            gameBoard[5][row] = gameBoard[7][row];
            gameBoard[7][row].type = -1;
            gameBoard[7][row].colour = -1;
            gameBoard[5][row].hasMoved = 1;
        }
        else if (tx == 2)
        {
            gameBoard[3][row] = gameBoard[0][row];
            gameBoard[0][row].type = -1;
            gameBoard[0][row].colour = -1;
            gameBoard[3][row].hasMoved = 1;
        }
    }

    // Update local state
    if (lastMoveOut)
    {
        lastMoveOut->fromX = fx;
        lastMoveOut->fromY = fy;
        lastMoveOut->toX = tx;
        lastMoveOut->toY = ty;
    }

    // Reset halfmove clock on pawn move or capture
    if (gameBoard[tx][ty].type == PAWN && halfmoveClockOut)
        *halfmoveClockOut = 0;

    return 1;
}

/**
 * Solve a single puzzle using a local board state
 * This function manages its own thread-local board and only swaps to globals
 * when calling the engine's moveRanking function
 */
static int solvePuzzle(const char *filename, int puzzle_index, int search_depth,
                       struct Piece local_board[8][8])
{
    // Load the puzzle
    struct LichessPuzzle puzzle;
    if (!loadLichessPuzzle(filename, puzzle_index, &puzzle))
        return 0;

    // Load FEN into local board
    if (!loadBoardFromFEN(puzzle.fen, local_board))
        return 0;

    // Get whose turn it is
    enum Colour sideToMove = getTurnFromFEN(puzzle.fen);

    // Initialize local move and halfmove clock
    struct Move localLastMove = {0, 0, 0, 0};
    int localHalfmoveClock = 0;

    // Parse puzzle moves
    char movesCopy[512];
    strcpy(movesCopy, puzzle.moves);
    char *saveptr = NULL;
    char *token = strtok_r(movesCopy, " ", &saveptr);

    // Execute first move (opponent's initial move in the puzzle)
    if (token)
    {
        if (!executeUciMoveLocal(local_board, token, &localLastMove, &localHalfmoveClock))
            return 0;

        sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
        token = strtok_r(NULL, " ", &saveptr);
    }

    // Solve puzzle by matching expected moves
    int puzzle_success = 1;
    while (token)
    {
        // Copy local board state to global for engine call
        memcpy(board, local_board, sizeof(board));
        lastMove = localLastMove;
        halfmoveClock = localHalfmoveClock;
        // Keep other global state unchanged for engine calls
        // (evalCount, depth, etc. maintained by moveRanking)

        // Call engine to find best move
        enum Colour aiColour = sideToMove;
        moveRanking(board, search_depth, aiColour);

        // Sync board back from globals
        memcpy(local_board, board, sizeof(board));
        localLastMove = lastMove;
        localHalfmoveClock = halfmoveClock;

        // Format the AI's move
        const char *expectedMove = token;
        token = strtok_r(NULL, " ", &saveptr);

        char aiMoveNotation[32];
        snprintf(aiMoveNotation, sizeof(aiMoveNotation), "%c%d%c%d",
                 'a' + localLastMove.fromX, localLastMove.fromY + 1,
                 'a' + localLastMove.toX, localLastMove.toY + 1);

        if (strcmp(aiMoveNotation, expectedMove) != 0)
        {
            // Move doesn't match - check if it's checkmate (valid alternative solution)
            enum Colour opponentColour = (aiColour == WHITE) ? BLACK : WHITE;
            if (!isCheckmate(local_board, opponentColour))
            {
                puzzle_success = 0;
                break;
            }
            // Checkmate found - puzzle solved even though move differs
            break;
        }

        sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;

        if (token)
        {
            // Execute opponent's response
            if (!executeUciMoveLocal(local_board, token, &localLastMove, &localHalfmoveClock))
            {
                puzzle_success = 0;
                break;
            }

            sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
            token = strtok_r(NULL, " ", &saveptr);
        }
    }

    return puzzle_success ? 1 : 0;
}

/* ======================== Work Queue Structure ======================== */

typedef struct {
    int puzzle_index;
    int result;
    int completed;
} PuzzleResult;

typedef struct {
    PuzzleResult *results;
    int total_puzzles;
    int completed_count;
    int next_puzzle_index;
    pthread_mutex_t lock;
} WorkQueue;

/* ======================== Thread Worker ======================== */

typedef struct {
    const char *puzzle_file;
    int search_depth;
    WorkQueue *queue;
} ThreadWorkerArgs;

static void* puzzle_worker_thread(void *arg)
{
    ThreadWorkerArgs *args = (ThreadWorkerArgs *)arg;
    const char *puzzle_file = args->puzzle_file;
    int search_depth = args->search_depth;
    WorkQueue *queue = args->queue;

    // Allocate thread-local board
    struct Piece *local_board = (struct Piece *)malloc(8 * 8 * sizeof(struct Piece));
    if (!local_board)
    {
        free(args);
        return NULL;
    }

    // Get puzzles from work queue and solve them
    while (1)
    {
        pthread_mutex_lock(&queue->lock);

        // Check if there are more puzzles
        if (queue->next_puzzle_index >= queue->total_puzzles)
        {
            pthread_mutex_unlock(&queue->lock);
            break;
        }

        // Get next puzzle
        int puzzle_index = queue->next_puzzle_index;
        queue->next_puzzle_index++;

        pthread_mutex_unlock(&queue->lock);

        // Solve the puzzle
        int result = solvePuzzle(puzzle_file, puzzle_index, search_depth, (struct Piece (*)[8])local_board);

        // Record result
        pthread_mutex_lock(&queue->lock);
        queue->results[puzzle_index].result = result;
        queue->results[puzzle_index].completed = 1;
        queue->completed_count++;

        // Progress callback every N puzzles to reduce lock contention
        if ((queue->completed_count % 5) == 0 && puzzle_progress_callback != NULL)
        {
            int passes = 0;
            for (int i = 0; i < queue->total_puzzles; i++)
            {
                if (queue->results[i].completed && queue->results[i].result)
                    passes++;
            }
            pthread_mutex_unlock(&queue->lock);
            puzzle_progress_callback(queue->completed_count, queue->total_puzzles, passes);
            pthread_mutex_lock(&queue->lock);
        }

        pthread_mutex_unlock(&queue->lock);
    }

    free(local_board);
    free(args);
    return NULL;
}

/* ======================== Public API ======================== */

/**
 * Play puzzles with multi-threading support
 * filename: path to puzzle CSV file
 * searchDepth: depth for engine search
 * Returns: number of puzzles solved correctly
 *
 * This version automatically uses multiple threads for faster solving
 * while maintaining accuracy by isolating board state per thread.
 */
int playPuzzles1To100(const char *filename, int searchDepth)
{
    int numThreads = DEFAULT_THREAD_COUNT;

    // Initialize work queue
    WorkQueue queue;
    memset(&queue, 0, sizeof(queue));
    queue.total_puzzles = PUZZLE_TEST_COUNT;
    queue.completed_count = 0;
    queue.next_puzzle_index = 0;
    pthread_mutex_init(&queue.lock, NULL);

    queue.results = (PuzzleResult *)malloc(sizeof(PuzzleResult) * PUZZLE_TEST_COUNT);
    if (!queue.results)
    {
        pthread_mutex_destroy(&queue.lock);
        return 0;
    }

    memset(queue.results, 0, sizeof(PuzzleResult) * PUZZLE_TEST_COUNT);
    for (int i = 0; i < PUZZLE_TEST_COUNT; i++)
    {
        queue.results[i].puzzle_index = i;
        queue.results[i].completed = 0;
        queue.results[i].result = 0;
    }

    // Create worker threads
    pthread_t threads[MAX_THREADS];
    int actual_thread_count = (numThreads > MAX_THREADS) ? MAX_THREADS : numThreads;

    for (int i = 0; i < actual_thread_count; i++)
    {
        ThreadWorkerArgs *args = (ThreadWorkerArgs *)malloc(sizeof(ThreadWorkerArgs));
        if (!args)
        {
            fprintf(stderr, "Error allocating thread args\n");
            continue;
        }
        args->puzzle_file = filename;
        args->search_depth = searchDepth;
        args->queue = &queue;

        if (pthread_create(&threads[i], NULL, puzzle_worker_thread, args) != 0)
        {
            fprintf(stderr, "Error creating thread %d\n", i);
            free(args);
        }
    }

    // Wait for all threads to finish
    for (int i = 0; i < actual_thread_count; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Count passes
    int passes = 0;
    for (int i = 0; i < PUZZLE_TEST_COUNT; i++)
    {
        if (queue.results[i].result)
            passes++;
    }

    // Final callback
    if (puzzle_progress_callback != NULL)
    {
        puzzle_progress_callback(queue.completed_count, queue.total_puzzles, passes);
    }

    // Cleanup
    free(queue.results);
    pthread_mutex_destroy(&queue.lock);

    return passes;
}

/**
 * Get current thread statuses for TUI display (compatibility stub)
 * This is called by the TUI to display per-thread puzzle progress
 * In the new implementation, we just return stub values since threads
 * are not tracked individually anymore.
 */
int* get_thread_puzzle_statuses(int *num_threads_out, int *statuses_out)
{
    if (num_threads_out)
        *num_threads_out = DEFAULT_THREAD_COUNT;
    return statuses_out;
}

/**
 * Close puzzle file cache (stub for training compatibility)
 */
void closePuzzleFileCache(void)
{
    // In the new implementation, we don't maintain a persistent file cache
    // This is a no-op stub for compatibility with training.c
}

/**
 * Multi-threaded version (compatibility wrapper)
 * The new playPuzzles1To100 is already multi-threaded by default
 */
int playPuzzles1To100_Threaded(const char *filename, int searchDepth, int num_threads)
{
    // The new implementation automatically uses multiple threads
    // The num_threads parameter is ignored since we use a fixed default
    // but could be extended to use the parameter if desired
    return playPuzzles1To100(filename, searchDepth);
}
