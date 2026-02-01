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
    int score;
};

// Prototypes for board checking functions
struct MoveList validMoves(struct Piece gameBoard[8][8], enum Colour colour);
int isStalemate(struct Piece gameBoard[8][8], enum Colour colour);
int isInCheck(struct Piece gameBoard[8][8], enum Colour colour);
int isMoveValid(struct Piece gameBoard[8][8], int fromX, int fromY, int toX, int toY, enum Colour colour);
int isInEndgame(struct Piece board[8][8]);
int isCheckmate(struct Piece gameBoard[8][8], enum Colour colour);
int checkAndExecuteOneMoveMate(struct Piece gameBoard[8][8], enum Colour currentPlayer);
int isLegalUciMove(struct Piece gameBoard[8][8], enum Colour colour, const char *uci);
int boardSetup();

// Evaluation / AI scoring
int evaluateEndgameAdvancement(struct Piece board[8][8], int fromX, int fromY, int toX, int toY, enum Colour colour);
int evaluateBoardPosition(struct Piece board[8][8]);

// Puzzle / FEN helpers
int loadLichessPuzzle(const char *filename, int puzzleNumber, struct LichessPuzzle *puzzle);
int loadBoardFromFEN(const char *fen, struct Piece gameBoard[8][8]);
enum Colour getTurnFromFEN(const char *fen);
int loadAndDisplayLichessPuzzle(const char *filename, enum Colour *puzzleTurnOut, struct LichessPuzzle *outPuzzle);
int executeUciMove(struct Piece gameBoard[8][8], const char *uci);
// Input parsing and user helpers
int parseChessNotation(const char *notation, int *fromX, int *fromY, int *toX, int *toY, enum Colour colour, int *promotionPiece);
int getUserMove(enum Colour colour);

// Promote pawn helper used by input handling
void promotePawn(struct Piece gameBoard[8][8], int x, int y);

// Output helpers
void printBoard(void);

// Additional utility / evaluation helpers
int canBeCaptured(struct Piece currentBoard[8][8], int x, int y);
int countMajorPieces(struct Piece board[8][8], enum Colour colour);
int squareDistance(int x1, int y1, int x2, int y2);

// Recursion: move-ranking recursive function
struct MoveSequence moveRankingRecursiveWithSequence(
    struct Piece board[8][8],
    int depth,
    int maxDepth,
    enum Colour player,
    int alpha,
    int beta);

// Top-level move ranking: runs recursive search and executes best move on board
int moveRanking(struct Piece currentBoard[8][8], int maxRecursiveDepth, enum Colour aiColour);

// Helpers and globals used by recursive search
int countBoardRepetitions();
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

#endif // CHESS_H

