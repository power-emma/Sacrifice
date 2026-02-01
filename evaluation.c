/* evaluation.c - AI score computation (evaluation functions) */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include "chess.h"

// Returns bonus score for moves that advance pieces towards opponent's king
int evaluateEndgameAdvancement(struct Piece board[8][8], int fromX, int fromY, int toX, int toY, enum Colour colour)
{
    if (!isInEndgame(board))
    {
        return 0; // Not in endgame, no bonus
    }

    // Find enemy king position
    int enemyKingX = -1, enemyKingY = -1;
    enum Colour enemyColour = (colour == WHITE) ? BLACK : WHITE;

    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (board[i][j].type == KING && board[i][j].colour == enemyColour)
            {
                enemyKingX = i;
                enemyKingY = j;
                break;
            }
        }
        if (enemyKingX != -1)
            break;
    }

    if (enemyKingX == -1)
        return 0; // No enemy king found

    struct Piece movingPiece = board[fromX][fromY];
    if (movingPiece.type == PAWN || movingPiece.type == KING)
    {
        return 0; // Don't give bonus for pawns or king
    }

    int distanceBefore = squareDistance(fromX, fromY, enemyKingX, enemyKingY);
    int distanceAfter = squareDistance(toX, toY, enemyKingX, enemyKingY);

    // If move brings piece closer to enemy king, give bonus
    if (distanceAfter < distanceBefore)
    {
        // Check if the piece would be captured in the new position
        struct Piece tempBoard[8][8];
        for (int i = 0; i < 8; i++)
        {
            for (int j = 0; j < 8; j++)
            {
                tempBoard[i][j] = board[i][j];
            }
        }
        tempBoard[toX][toY] = tempBoard[fromX][fromY];
        tempBoard[fromX][fromY].type = -1;
        tempBoard[fromX][fromY].colour = -1;

        // Check if the piece can be captured at the new position
        if (canBeCaptured(tempBoard, toX, toY))
        {
            return 0; // Don't give bonus if piece would be captured
        }

        // Bonus scales with piece value and distance reduction
        // Closer proximity gets higher bonus
        int bonus = (distanceBefore - distanceAfter) * (5 - distanceAfter) * 0.5;
        return bonus;
    }

    return 0;
}


int evaluateBoardPosition(struct Piece board[8][8])
{
    // Transposition table lookup: avoid recomputing positions we've already evaluated
    // Simple direct-mapped table with 64K entries
    const int TT_BITS = 16;
    const size_t TT_SIZE = (1u << TT_BITS);

    // Compute board hash
    uint64_t key = 14695981039346656037ULL; // FNV-1a offset
    const uint64_t FNV_PRIME = 1099511628211ULL;
    for (int x = 0; x < 8; x++)
    {
        for (int y = 0; y < 8; y++)
        {
            int t = (board[x][y].type >= 0) ? (board[x][y].type + 1) : 0; // 0 = empty
            int c = (board[x][y].colour == WHITE) ? 1 : (board[x][y].colour == BLACK) ? 2 : 0;
            uint8_t bytes[3];
            bytes[0] = (uint8_t)t;
            bytes[1] = (uint8_t)c;
            bytes[2] = (uint8_t)(board[x][y].hasMoved ? 1 : 0);
            for (int b = 0; b < 3; b++)
            {
                key ^= (uint64_t)bytes[b];
                key *= FNV_PRIME;
            }
        }
    }

    // Simple direct-mapped transposition table stored as static inside this function
    typedef struct { uint64_t key; int score; } TTEntry;
    static TTEntry *tt = NULL;
    if (!tt)
    {
        tt = calloc(TT_SIZE, sizeof(TTEntry));
    }
    size_t idx = (size_t)(key & (TT_SIZE - 1));
    if (tt[idx].key == key && tt[idx].key != 0)
    {
        /* Transposition table hit */
        ttHitCount++;
        return tt[idx].score;
    }

        int score = 0;

        /* Count this as a unique evaluated position (cache miss) */
        static unsigned long long localEvalCount = 0ULL;
        localEvalCount++;
        evalCount = localEvalCount;

    // [Tuning] - Piece values
    int pieceValue[] = {100, 300, 300, 500, 900, 20000};

    // [Tuning] - Global positional table
    int globalTable[8][8] = {
        {0, 0, 0, 0, 0, 0, 0, 0},
        {0, 1, 1, 1, 1, 1, 1, 0},
        {0, 1, 2, 2, 2, 2, 1, 0},
        {0, 1, 2, 3, 3, 2, 1, 0},
        {0, 1, 2, 3, 3, 2, 1, 0},
        {0, 1, 2, 2, 2, 2, 1, 0},
        {0, 1, 1, 1, 1, 1, 1, 0},
        {0, 0, 0, 0, 0, 0, 0, 0}};

    int whiteAttacks[8][8] = {0};
    int blackAttacks[8][8] = {0};

    int whiteKingX = -1, whiteKingY = -1, blackKingX = -1, blackKingY = -1;

    // First pass: score pieces, mark attacks, find kings
    for (int x = 0; x < 8; x++)
    {
        for (int y = 0; y < 8; y++)
        {
            struct Piece p = board[x][y];
            if (p.type == -1)
                continue;

            int sign = (p.colour == WHITE) ? 1 : -1;
            int (*attackMap)[8]; // pointer to array of 8 ints
            if (p.colour == WHITE)
                attackMap = whiteAttacks;
            else
                attackMap = blackAttacks;

            // Track king positions
            if (p.type == KING)
            {
                if (p.colour == WHITE)
                {
                    whiteKingX = x;
                    whiteKingY = y;
                }
                else
                {
                    blackKingX = x;
                    blackKingY = y;
                }
            }

            // --- Base piece value ---
            score += sign * pieceValue[p.type];

            // -[Tuning] - Global positional table multiplier
            score += sign * globalTable[x][y] * 10;

            // Pawn Special Scoring
            if (p.type == PAWN)
            {
                int defended = 0;
                if (p.colour == WHITE && x >= 4 && (y == 3 || y == 4))
                {
                    if (x > 0)
                    {
                        if (y > 0 && board[x - 1][y - 1].type == PAWN && board[x - 1][y - 1].colour == WHITE)
                            defended = 1;
                        if (y < 7 && board[x - 1][y + 1].type == PAWN && board[x - 1][y + 1].colour == WHITE)
                            defended = 1;
                    }
                    if (!defended)
                        score -= 50;
                }
                if (p.colour == BLACK && x <= 3 && (y == 3 || y == 4))
                {
                    if (x < 7)
                    {
                        if (y > 0 && board[x + 1][y - 1].type == PAWN && board[x + 1][y - 1].colour == BLACK)
                            defended = 1;
                        if (y < 7 && board[x + 1][y + 1].type == PAWN && board[x + 1][y + 1].colour == BLACK)
                            defended = 1;
                    }
                    if (!defended)
                        score += 50;
                }

                if (p.colour == WHITE && x == 3 && (y == 3 || y == 4))
                    score += 40;
                if (p.colour == BLACK && x == 4 && (y == 3 || y == 4))
                    score -= 40;

                if (p.colour == BLACK)
                {
                    int promotionDistance = y;
                    if (promotionDistance <= 2)
                        score += 300 * (2 - promotionDistance);
                    else if (promotionDistance <= 4)
                        score += 80 * (4 - promotionDistance);
                }

                // Pawn attacks
                if (p.colour == WHITE)
                {
                    if (x > 0 && y > 0)
                        attackMap[x - 1][y - 1] = 1;
                    if (x > 0 && y < 7)
                        attackMap[x - 1][y + 1] = 1;
                }
                else
                {
                    if (x < 7 && y > 0)
                        attackMap[x + 1][y - 1] = 1;
                    if (x < 7 && y < 7)
                        attackMap[x + 1][y + 1] = 1;
                }
            }

            // Knight Special Scoring
            if (p.type == KNIGHT)
            {
                if ((p.colour == WHITE && x == 0 && y >= 2 && y <= 5) ||
                    (p.colour == BLACK && x == 7 && y >= 2 && y <= 5))
                    score -= sign * 40;
                if (x == 0 || x == 7)
                    score -= sign * 30;

                int moves[8][2] = {{2, 1}, {1, 2}, {-1, 2}, {-2, 1}, {-2, -1}, {-1, -2}, {1, -2}, {2, -1}};
                for (int k = 0; k < 8; k++)
                {
                    int nx = x + moves[k][0], ny = y + moves[k][1];
                    if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8)
                        attackMap[nx][ny] = 1;
                }
            }

            // Sliding Pieces Special Scoring
            if (p.type == BISHOP || p.type == ROOK || p.type == QUEEN)
            {
                int dirs[8][2];
                int dirCount = 0;

                if (p.type == BISHOP || p.type == QUEEN)
                {
                    int bishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
                    for (int d = 0; d < 4; d++)
                    {
                        dirs[dirCount][0] = bishopDirs[d][0];
                        dirs[dirCount][1] = bishopDirs[d][1];
                        dirCount++;
                    }
                }
                if (p.type == ROOK || p.type == QUEEN)
                {
                    int rookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (int d = 0; d < 4; d++)
                    {
                        dirs[dirCount][0] = rookDirs[d][0];
                        dirs[dirCount][1] = rookDirs[d][1];
                        dirCount++;
                    }
                }

                for (int d = 0; d < dirCount; d++)
                {
                    int cx = x + dirs[d][0];
                    int cy = y + dirs[d][1];
                    int mobility = 0;
                    while (cx >= 0 && cx < 8 && cy >= 0 && cy < 8)
                    {
                        mobility++;
                        attackMap[cx][cy] = 1;
                        if (board[cx][cy].type != -1)
                            break;
                        cx += dirs[d][0];
                        cy += dirs[d][1];
                    }
                    score += sign * mobility * 5;
                }
            }

            // King Special Scoring
            if (p.type == KING)
            {
                if (p.hasMoved)
                    score += (p.colour == WHITE) ? -100 : 100;
                if ((x == 3 || x == 4) && y >= 2 && y <= 5)
                    score += (p.colour == WHITE) ? -30 : 30;

                for (int kx = -1; kx <= 1; kx++)
                {
                    for (int ky = -1; ky <= 1; ky++)
                    {
                        if (kx == 0 && ky == 0)
                            continue;
                        int nx = x + kx, ny = y + ky;
                        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8)
                            attackMap[nx][ny] = 1;
                    }
                }
            }
        }
    }

    // Reward control of squares around enemy king
    int kingAdj[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int kingSquaresBonus = 20;

    if (whiteKingX != -1 && whiteKingY != -1)
    {
        for (int d = 0; d < 8; d++)
        {
            int tx = whiteKingX + kingAdj[d][0];
            int ty = whiteKingY + kingAdj[d][1];
            if (tx < 0 || tx > 7 || ty < 0 || ty > 7)
                continue;
            if (blackAttacks[tx][ty])
                score += kingSquaresBonus;
        }
    }
    if (blackKingX != -1 && blackKingY != -1)
    {
        for (int d = 0; d < 8; d++)
        {
            int tx = blackKingX + kingAdj[d][0];
            int ty = blackKingY + kingAdj[d][1];
            if (tx < 0 || tx > 7 || ty < 0 || ty > 7)
                continue;
            if (whiteAttacks[tx][ty])
                score -= kingSquaresBonus;
        }
    }

    // Check bonuses/penalties
    if (isInCheck(board, WHITE))
        score -= 200;
    if (isInCheck(board, BLACK))
        score += 200;

    // Stalemate handling
    if (isStalemate(board, BLACK) && score > 0)
    {
        score = -500;
    }
    if (isStalemate(board, WHITE) && score < 0)
    {
        score = 500;
    }

    // Store into transposition table
    if (tt)
    {
        tt[idx].key = key ? key : 1; // avoid zero key meaning empty
        tt[idx].score = score;
    }

    return score;
}

/* Global counter for unique evaluations (defined here) */
unsigned long long evalCount = 0ULL;
/* Transposition table hits */
unsigned long long ttHitCount = 0ULL;
/* Alpha-beta prune count (incremented from recursion.c) */
unsigned long long abPruneCount = 0ULL;
/* Static-futility prune count (incremented from recursion.c) */
unsigned long long staticPruneCount = 0ULL;

void printEvaluationCount(void)
{
    printf("Evaluated unique board positions: %llu\n", evalCount);
    printf("Transposition-table hits (lookup): %llu\n", ttHitCount);
    printf("Alpha-beta prunes: %llu\n", abPruneCount);
    printf("Static-futility prunes (shallow): %llu\n", staticPruneCount);
}


// Main move ranking function that executes best move and prints sequence
int moveRanking(struct Piece currentBoard[8][8], int maxRecursiveDepth, enum Colour aiColour)
{
    // Check for one-move checkmate first - Always checkmate if available
    if (checkAndExecuteOneMoveMate(currentBoard, aiColour))
    {
        return 999999999;
    }

    // Reset per-search evaluation counters (we want per-search stats)
    evalCount = 0ULL;
    ttHitCount = 0ULL;
    abPruneCount = 0ULL;
    staticPruneCount = 0ULL;

    struct timespec tstart, tend;
    clock_gettime(CLOCK_MONOTONIC, &tstart);

    struct MoveSequence bestSequence = moveRankingRecursiveWithSequence(currentBoard, 0, maxRecursiveDepth, aiColour, -999999999, 999999999);

    clock_gettime(CLOCK_MONOTONIC, &tend);
    double elapsed = (double)(tend.tv_sec - tstart.tv_sec) + (double)(tend.tv_nsec - tstart.tv_nsec) / 1e9;

    if (bestSequence.count == 0)
    {
        printf("No valid moves available.\n");
        return 0;
    }

    // Print the predicted move sequence
    printf("Predicted best move sequence (depth %d):\n", maxRecursiveDepth);
    enum Colour currentPlayer = aiColour;
    for (int i = 0; i < bestSequence.count; i++)
    {
        char moveNotation[16];
        snprintf(moveNotation, sizeof(moveNotation), "%c%d%c%d",
                 'a' + bestSequence.moves[i].fromX, bestSequence.moves[i].fromY + 1,
                 'a' + bestSequence.moves[i].toX, bestSequence.moves[i].toY + 1);
        printf("  %d. %s (%s)\n", i + 1, moveNotation, currentPlayer == WHITE ? "White" : "Black");
        currentPlayer = (currentPlayer == WHITE) ? BLACK : WHITE;
    }
    printf("Final evaluation: %d\n\n", bestSequence.score);

    // Execute the first move on the global board
    if (bestSequence.count > 0)
    {
        int bestFromX = bestSequence.moves[0].fromX;
        int bestFromY = bestSequence.moves[0].fromY;
        int bestToX = bestSequence.moves[0].toX;
        int bestToY = bestSequence.moves[0].toY;

        char notation[16];
        snprintf(notation, sizeof(notation), "%c%d%c%d",
                 'a' + bestFromX, bestFromY + 1,
                 'a' + bestToX, bestToY + 1);
        printf("%s plays: %s\n", aiColour == WHITE ? "White (AI)" : "Black (AI)", notation);

        // Check if this is a capture move (before we overwrite the destination)
        int whiteIsCapture = (board[bestToX][bestToY].type != -1);

        board[bestToX][bestToY] = board[bestFromX][bestFromY];
        board[bestFromX][bestFromY].type = -1;
        board[bestFromX][bestFromY].colour = -1;
        board[bestToX][bestToY].hasMoved = 1;

        // Handle pawn promotion
        promotePawn(board, bestToX, bestToY);

        // Handle castling - move the rook
        if (board[bestToX][bestToY].type == KING && bestFromX == 4)
        {
            if (bestToX == 6)
            {
                board[5][bestToY] = board[7][bestToY];
                board[7][bestToY].type = -1;
                board[7][bestToY].colour = -1;
                board[5][bestToY].hasMoved = 1;
            }
            else if (bestToX == 2)
            {
                board[3][bestToY] = board[0][bestToY];
                board[0][bestToY].type = -1;
                board[0][bestToY].colour = -1;
                board[3][bestToY].hasMoved = 1;
            }
        }

        // Update halfmove clock for 50-move rule
        if (board[bestToX][bestToY].type == PAWN || whiteIsCapture)
        {
            halfmoveClock = 0;
        }
        else
        {
            halfmoveClock++;
        }

        // Record board state for threefold repetition detection
        recordBoardHistory();

        // Track the last move for en passant detection
        lastMove.fromX = bestFromX;
        lastMove.fromY = bestFromY;
        lastMove.toX = bestToX;
        lastMove.toY = bestToY;
    
        // Print how long the AI thought and evaluation counter after AI executes a move
        printf("AI thinking time: %.3f seconds\n", elapsed);
        printEvaluationCount();
    }

    return bestSequence.score;
}
