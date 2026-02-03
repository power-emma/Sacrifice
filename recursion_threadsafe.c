// recursion_threadsafe.c - thread-safe recursive search functions for move ranking
#include <stdio.h>
#include <string.h>
#include "chess.h"

// Thread-safe Move Ranking Recursive Function with Move Sequence Tracking
struct MoveSequence moveRankingRecursiveWithSequence_ThreadSafe(
    struct GameState *state,
    int curDepth,
    int maxDepth,
    enum Colour player,
    double alpha,
    double beta)
{
    struct MoveSequence best;
    best.count = 0;
    best.score = -checkmate_score;

    int oldDepth = state->depth;
    state->depth = curDepth;

    // Checkmate and stalemate are game ending conditions, check first

    // If either side is checkmated in this position, return immediately with extreme score
    if (isCheckmate(state->board, WHITE))
    {
        best.score = (player == WHITE) ? -checkmate_score : checkmate_score;
        state->depth = oldDepth;
        return best;
    }
    if (isCheckmate(state->board, BLACK))
    {
        best.score = (player == BLACK) ? -checkmate_score : checkmate_score;
        state->depth = oldDepth;
        return best;
    }

    // Check for stalemate
    if (isStalemate(state->board, WHITE))
    {
        best.score = (player == WHITE) ? -stalemate_score : stalemate_score;
        state->depth = oldDepth;
        return best;
    }
    if (isStalemate(state->board, BLACK))
    {
        best.score = (player == BLACK) ? -stalemate_score : stalemate_score;
        state->depth = oldDepth;
        return best;
    }

    // Check for threefold repetition (draw)
    if (countBoardRepetitions_ThreadSafe(state) >= 3)
    {
        best.score = 0; // Draw - neutral score
        state->depth = oldDepth;
        return best;
    }

    // Check for 50-move rule (draw)
    if (state->halfmoveClock >= 100)
    {
        best.score = 0; // Draw - neutral score
        state->depth = oldDepth;
        return best;
    }

    // Allow deeper search in endgame positions
    int effectiveMaxDepth = maxDepth;
    if (isInEndgame(state->board))
    {
        effectiveMaxDepth += 0; /* increase recursion depth by N in endgame */
    }

    // Recursion base case
    if (curDepth >= effectiveMaxDepth)
    {
        double eval = evaluateBoardPosition_ThreadSafe(state);
        best.score = (player == WHITE) ? eval : -eval;
        state->depth = oldDepth;
        return best;
    }

    struct MoveList moves = validMoves(state->board, player);

    if (moves.count == 0)
    {
        double eval = evaluateBoardPosition_ThreadSafe(state);
        best.score = (player == WHITE) ? eval : -eval;
        state->depth = oldDepth;
        return best;
    }

    for (int i = 0; i < moves.count; i++)
    {
        struct Piece tempBoard[8][8];

        for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++)
                tempBoard[x][y] = state->board[x][y];

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
        // Create temporary state for evaluation
        struct GameState tempState = *state;
        memcpy(tempState.board, tempBoard, sizeof(tempBoard));
        
        double staticEval = evaluateBoardPosition_ThreadSafe(&tempState);
        double staticScore = (player == WHITE) ? staticEval : -staticEval;
        if (best.score > -checkmate_score && staticScore < best.score - static_futility_prune_margin)
        {
            state->staticPruneCount++;
            continue; // skip recursion for this move
        }

        // Recurse with modified board
        memcpy(tempState.board, tempBoard, sizeof(tempBoard));
        
        struct MoveSequence child =
            moveRankingRecursiveWithSequence_ThreadSafe(
                &tempState,
                curDepth + 1,
                maxDepth,
                player == WHITE ? BLACK : WHITE,
                -beta,
                -alpha);

        // Propagate statistics back to parent state
        state->evalCount = tempState.evalCount;
        state->ttHitCount = tempState.ttHitCount;
        state->abPruneCount = tempState.abPruneCount;
        state->staticPruneCount = tempState.staticPruneCount;

        double score = -child.score;

        // Apply endgame advancement bonus at depth 0 (immediate move evaluation)
        if (state->depth == 0)
        {
            int advancementBonus = evaluateEndgameAdvancement(state->board, fx, fy, tx, ty, player);
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
            state->abPruneCount++;
            break;
        }
    }

    // If pruning/ordering left us with no selected move but there were legal
    //   moves available, pick the first legal move as a fallback 
    if (best.count == 0 && moves.count > 0)
    {
        struct Move m = moves.moves[0];
        struct Piece tempBoard[8][8];
        for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++)
                tempBoard[x][y] = state->board[x][y];

        int fx = m.fromX, fy = m.fromY, tx = m.toX, ty = m.toY;
        tempBoard[tx][ty] = tempBoard[fx][fy];
        tempBoard[fx][fy].type = -1;
        tempBoard[fx][fy].colour = -1;
        tempBoard[tx][ty].hasMoved = 1;

        struct GameState tempState = *state;
        memcpy(tempState.board, tempBoard, sizeof(tempBoard));
        double eval = evaluateBoardPosition_ThreadSafe(&tempState);
        best.score = (player == WHITE) ? eval : -eval;
        best.count = 1;
        best.moves[0] = m;
    }

    state->depth = oldDepth;
    return best;
}

// Top-level thread-safe function to compute best move
struct MoveSequence computeBestMove_ThreadSafe(struct GameState *state, int maxRecursiveDepth, enum Colour aiColour)
{
    // Reset evaluation statistics
    state->evalCount = 0ULL;
    state->ttHitCount = 0ULL;
    state->abPruneCount = 0ULL;
    state->staticPruneCount = 0ULL;

    struct MoveSequence bestSequence = moveRankingRecursiveWithSequence_ThreadSafe(
        state, 0, maxRecursiveDepth, aiColour, -checkmate_score, checkmate_score);

    return bestSequence;
}
