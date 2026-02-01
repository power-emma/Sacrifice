// recursion.c - recursive search functions for move ranking
#include <stdio.h>
#include <string.h>
#include "chess.h"

// Move Ranking Recursive Function with Move Sequence Tracking
struct MoveSequence moveRankingRecursiveWithSequence(
    struct Piece board[8][8],
    int curDepth,
    int maxDepth,
    enum Colour player,
    int alpha,
    int beta)
{
    struct MoveSequence best;
    best.count = 0;
    best.score = -999999999;

    int oldDepth = depth;
    depth = curDepth;

    // Checkmate and stalemate are game ending conditions, check first

    // If either side is checkmated in this position, return immediately with extreme score
    if (isCheckmate(board, WHITE))
    {
        best.score = (player == WHITE) ? -999999999 : 999999999; // perspective relative to player
        depth = oldDepth;
        return best;
    }
    if (isCheckmate(board, BLACK))
    {
        best.score = (player == BLACK) ? -999999999 : 999999999; // perspective relative to player
        depth = oldDepth;
        return best;
    }

    // Check for stalemate
    if (isStalemate(board, WHITE))
    {
        best.score = (player == WHITE) ? -500 : 500; // perspective relative to player
        depth = oldDepth;
        return best;
    }
    if (isStalemate(board, BLACK))
    {
        best.score = (player == BLACK) ? -500 : 500; // perspective relative to player
        depth = oldDepth;
        return best;
    }

    // Check for threefold repetition (draw)
    if (countBoardRepetitions() >= 3)
    {
        best.score = 0; // Draw - neutral score
        depth = oldDepth;
        return best;
    }

    // Check for 50-move rule (draw)
    if (halfmoveClock >= 100)
    {
        best.score = 0; // Draw - neutral score
        depth = oldDepth;
        return best;
    }

    // Allow deeper search in endgame positions
    int effectiveMaxDepth = maxDepth;
    if (isInEndgame(board))
    {
        effectiveMaxDepth += 0; /* increase recursion depth by 4 in endgame */
    }

    // Recursion base case
    if (curDepth >= effectiveMaxDepth)
    {
        int eval = evaluateBoardPosition(board);
        best.score = (player == WHITE) ? eval : -eval;
        depth = oldDepth;
        return best;
    }

    struct MoveList moves = validMoves(board, player);

    if (moves.count == 0)
    {
        int eval = evaluateBoardPosition(board);
        best.score = (player == WHITE) ? eval : -eval;
        depth = oldDepth;
        return best;
    }

    for (int i = 0; i < moves.count; i++)
    {
        struct Piece tempBoard[8][8];

        for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++)
                tempBoard[x][y] = board[x][y];

        int fx = moves.moves[i].fromX;
        int fy = moves.moves[i].fromY;
        int tx = moves.moves[i].toX;
        int ty = moves.moves[i].toY;

        tempBoard[tx][ty] = tempBoard[fx][fy];
        tempBoard[fx][fy].type = -1;
        tempBoard[fx][fy].colour = -1;
        tempBoard[tx][ty].hasMoved = 1;

        // Castling
        if (tempBoard[tx][ty].type == KING && fx == 4)
        {
            if (tx == 6)
            {
                tempBoard[5][ty] = tempBoard[7][ty];
                tempBoard[7][ty].type = -1;
                tempBoard[7][ty].colour = -1;
                tempBoard[5][ty].hasMoved = 1;
            }
            else if (tx == 2)
            {
                tempBoard[3][ty] = tempBoard[0][ty];
                tempBoard[0][ty].type = -1;
                tempBoard[0][ty].colour = -1;
                tempBoard[3][ty].hasMoved = 1;
            }
        }

        // Static (shallow) futility pruning
        int staticEval = evaluateBoardPosition(tempBoard);
        int staticScore = (player == WHITE) ? staticEval : -staticEval;
        if (best.score > -900000000 && staticScore < best.score - 500)
        {
            staticPruneCount++;
            continue; // skip recursion for this move
        }

        struct MoveSequence child =
            moveRankingRecursiveWithSequence(
                tempBoard,
                curDepth + 1,
                maxDepth,
                player == WHITE ? BLACK : WHITE,
                -beta,
                -alpha);

        int score = -child.score;

        // Apply endgame advancement bonus at depth 0 (immediate move evaluation)
        if (depth == 0)
        {
            int advancementBonus = evaluateEndgameAdvancement(board, fx, fy, tx, ty, player);
            score += advancementBonus;
        }

        if (score > best.score)
        {
            best.score = score;
            best.count = 1;
            best.moves[0] = moves.moves[i];

            for (int j = 0; j < child.count && best.count < 224; j++)
            {
                best.moves[best.count++] = child.moves[j];
            }
        }

        if (score > alpha)
            alpha = score;

        // Alpha-beta prune
        if (alpha >= beta)
        {
            /* Count this prune occurrence */
            abPruneCount++;
            break;
        }
    }

    depth = oldDepth;
    return best;
}
