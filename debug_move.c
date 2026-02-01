#include <stdio.h>
#include "chess.h"

// Provide globals expected by other compilation units
struct Piece board[8][8];
int depth = 4; // default search depth for this test

int main()
{
    // Initialize board
    boardSetup();

    // Show raw evaluation of starting position
    int initEval = evaluateBoardPosition(board);
    printf("Initial board evaluation: %d\n", initEval);

    // Set search depth
    depth = 4;

    // Let AI (White) pick a move from starting position
    int score = moveRanking(board, depth, WHITE);
    printf("moveRanking returned score: %d\n", score);

    return 0;
}
