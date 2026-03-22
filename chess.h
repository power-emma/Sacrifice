// chess.h - central definitions for chess enums and structs 
#ifndef CHESS_H
#define CHESS_H

enum PieceType
{
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING
};

enum Colour
{
    WHITE,
    BLACK
};

struct Piece
{
    enum PieceType type;
    enum Colour colour;
    int hasMoved;
};

struct Move
{
    int fromX;
    int fromY;
    int toX;
    int toY;
};

struct MoveList
{
    struct Move moves[224]; // Maximum 218 possible moves in chess, but 224 allows for illegal positions
    int count;
};

// Thread-safe game state structure
// Contains all state needed for game evaluation without globals
struct GameState
{
    struct Piece board[8][8];
    struct Move lastMove;
    struct Piece boardHistory[200][8][8];
    int boardHistoryCount;
    int halfmoveClock;
    int depth;  // current recursive depth
    
    // Evaluation statistics (per-thread)
    unsigned long long evalCount;
    unsigned long long ttHitCount;
    unsigned long long abPruneCount;
    unsigned long long staticPruneCount;
    
    // Thread-local transposition table pointer
    void *transposition_table;
};

struct LichessPuzzle
{
    char puzzleId[32];
    char fen[96];
    char moves[512];      // Space-separated best moves in UCI format (e.g., "e2e4 d7d5")
    int rating;
    int ratingDeviation;
    int popularity;
    int nbPlays;
    char themes[256];     // Space-separated themes
    char gameUrl[256];
    char opening[128];
};


struct MoveSequence
{
    struct Move moves[224];
    int count;
    double score;
};

// Helper to initialize GameState
void initGameState(struct GameState *state);
void cleanupGameState(struct GameState *state);

// Prototypes for board checking functions (thread-safe versions)
struct MoveList validMoves(struct Piece gameBoard[8][8], enum Colour colour);
int isStalemate(struct Piece gameBoard[8][8], enum Colour colour);
int isInCheck(struct Piece gameBoard[8][8], enum Colour colour);
int isMoveValid(struct Piece gameBoard[8][8], int fromX, int fromY, int toX, int toY, enum Colour colour);
int isInEndgame(struct Piece board[8][8]);
int isCheckmate(struct Piece gameBoard[8][8], enum Colour colour);
int checkAndExecuteOneMoveMate(struct Piece gameBoard[8][8], enum Colour currentPlayer);
int isLegalUciMove(struct Piece gameBoard[8][8], enum Colour colour, const char *uci);
int boardSetup();

// Evaluation / AI scoring (thread-safe versions)
int evaluateEndgameAdvancement(struct Piece board[8][8], int fromX, int fromY, int toX, int toY, enum Colour colour);
double evaluateBoardPosition_ThreadSafe(struct GameState *state);
double evaluateBoardPosition(struct Piece board[8][8]);  // Legacy version for non-threaded code

// Puzzle / FEN helpers
int loadLichessPuzzle(const char *filename, int puzzleNumber, struct LichessPuzzle *puzzle);
void closePuzzleFileCache(void);
int loadBoardFromFEN(const char *fen, struct Piece gameBoard[8][8]);
enum Colour getTurnFromFEN(const char *fen);
int loadAndDisplayLichessPuzzle(const char *filename, enum Colour *puzzleTurnOut, struct LichessPuzzle *outPuzzle);
int executeUciMove(struct Piece gameBoard[8][8], const char *uci);
int playPuzzles1To100(const char *filename, int searchDepth);

// Multi-threaded puzzle testing (new thread-safe implementation)
int playPuzzlesMultiThreaded(const char *filename, int searchDepth, int numPuzzles, int numThreads,
                              void (*progress_callback)(int, int, int));
int playPuzzles1To100_MultiThreaded(const char *filename, int searchDepth, int numThreads);

// Multi-threaded NN training via teacher forcing on puzzle positions
// Each AI-turn position is used as one gradient step: input=current board, target=correct-move board.
// Weights are auto-saved to nn_weights.bin after each call.
// Returns the number of puzzles where the NN predicted the correct first move.
int playPuzzlesMultiThreaded_Train(const char *filename, float learning_rate,
                                   int numPuzzles, int numThreads,
                                   void (*progress_callback)(int, int, int));
int playPuzzles1To100_MT_Train(const char *filename, float learning_rate, int numThreads);

// Callback for puzzle progress during testing (can be NULL)
extern void (*puzzle_progress_callback)(int puzzles_completed, int total_puzzles, int current_score);
// Input parsing and user helpers
int parseChessNotation(const char *notation, int *fromX, int *fromY, int *toX, int *toY, enum Colour colour, int *promotionPiece);
int getUserMove(enum Colour colour);

// Promote pawn helper used by input handling
void promotePawn(struct Piece gameBoard[8][8], int x, int y);

// Output helpers
void printBoard(void);

// TUI helpers
void tui_init(void);
void tui_reconfigure_for_training(void);
void tui_cleanup(void);
void tui_show_splash(void);
void tui_draw_board(struct Piece board[8][8]);
void tui_draw_stats(enum Colour current_turn);
void tui_draw_moves(void);
void tui_draw_info(const char *message, int is_ai_turn);
void tui_update_stats(double think_time, unsigned long long positions, unsigned long long tt_hits, 
                      unsigned long long ab_prunes, unsigned long long static_prunes, int eval_score);
void tui_set_predicted_sequence(const char *sequence);
void tui_add_move(const char *move);
void tui_get_input(char *buffer, int max_len);
void tui_refresh_all(struct Piece board[8][8], enum Colour current_turn, const char *message, int is_ai_turn);
void tui_show_message(const char *message);
int tui_load_lichess_puzzle(const char *filename, enum Colour *puzzleTurnOut);
void tui_start_puzzle(const char *expected_moves, const char *puzzle_id, int rating);
int tui_validate_puzzle_move(const char *move_uci);
const char* tui_get_puzzle_status(void);
int tui_is_puzzle_active(void);
const char* tui_get_next_puzzle_move(void);
void tui_advance_puzzle_move(void);
void tui_clear_move_history(void);
void tui_run_puzzle_test(const char *filename, int searchDepth);

// Additional utility / evaluation helpers
int canBeCaptured(struct Piece currentBoard[8][8], int x, int y);
int countMajorPieces(struct Piece board[8][8], enum Colour colour);
int squareDistance(int x1, int y1, int x2, int y2);

// Top-level move ranking: runs NN and executes best move on board
int moveRanking(struct Piece currentBoard[8][8], int maxRecursiveDepth, enum Colour aiColour);

// Helpers and globals used by search
int countBoardRepetitions_ThreadSafe(struct GameState *state);
int countBoardRepetitions();
void recordBoardHistory_ThreadSafe(struct GameState *state);
void recordBoardHistory();
extern int halfmoveClock;

// Globals used across translation units
extern struct Move lastMove;
extern struct Piece board[8][8];
extern struct Piece boardHistory[200][8][8];
extern int boardHistoryCount;
extern int depth; // current recursive depth (set by recursion.c)
// Evaluation stats
extern unsigned long long evalCount; // number of unique boards evaluated
extern unsigned long long ttHitCount; // transposition table lookup hits
extern unsigned long long abPruneCount; // alpha-beta prune count
extern unsigned long long staticPruneCount; // static-futility prune count
void printEvaluationCount(void);

// Suppress engine output during puzzle testing
extern int suppress_engine_output;

// Checkmate message buffer for puzzle testing
extern char last_checkmate_message[256];

// Per-iteration training record (used by training display)
typedef struct {
    int iteration;
    int score;
    int pass_count;
} IterationHistory;

// === Training TUI (defined in tui.c) ===
void tui_show_training_complete(int best_score, int total_iterations);
void tui_nn_training_display(int iteration, int total_iterations, int score,
                             int best_score, int best_iteration,
                             IterationHistory *last_5, int history_count,
                             int elapsed_seconds, float learning_rate);
void tui_run_training_threaded(const char *puzzle_file, int iterations, int num_threads, int search_depth);

// === Evaluation System ===
void clear_thread_transposition_table(void);  // Clear thread-local TT between puzzles
int moveRanking_Synchronized(struct Piece currentBoard[8][8], int maxRecursiveDepth, enum Colour aiColour, struct Move *result_move);  // Thread-safe wrapper

// === Multithreaded Puzzle Testing (defined in puzzleThreads.c) ===
extern int PUZZLE_TEST_COUNT;  // Number of puzzles to test (defined in puzzleThreads.c)
int playPuzzles1To100_Threaded(const char *filename, int searchDepth, int num_threads);
int playPuzzles1To100_ThreadedWithCallback(const char *filename, int searchDepth, int num_threads,
                                           void (*callback)(int, int, int));
int* get_thread_puzzle_statuses(int *num_threads_out, int *statuses_out);

#endif // CHESS_H

