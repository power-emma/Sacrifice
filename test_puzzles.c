#include <stdio.h>
#include "chess.h"

// Global board
struct Piece board[8][8];
int depth = 4;

extern int PUZZLE_TEST_COUNT;  // Defined in puzzleThreads.c

int main()
{
    printf("Running Lichess Puzzle Test (Puzzles 1-%d)\n", PUZZLE_TEST_COUNT);
    printf("Search Depth: %d\n", depth);
    printf("==========================================\n\n");

    int score = playPuzzles1To100("lichess_db_puzzle.csv", depth);

    printf("\n==========================================\n");
    printf("FINAL SCORE: %d correct moves\n", score);

    return 0;
}
