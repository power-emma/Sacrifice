// test_puzzles_mt.c - Test the multi-threaded puzzle solver
#include <stdio.h>
#include <stdlib.h>
#include "chess.h"

// Global required by other modules
struct Piece board[8][8];
int depth = 4;

void progress_callback(int completed, int total, int passes)
{
    printf("\rProgress: %d/%d puzzles completed, %d passed (%.1f%%)", 
           completed, total, passes, (100.0 * passes) / completed);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    const char *puzzle_file = "lichess_db_puzzle.csv";
    int search_depth = 4;
    int num_threads = 8;
    int num_puzzles = 100;
    
    if (argc > 1)
        num_threads = atoi(argv[1]);
    if (argc > 2)
        search_depth = atoi(argv[2]);
    if (argc > 3)
        num_puzzles = atoi(argv[3]);
    
    printf("Testing %d puzzles with %d threads at depth %d\n", 
           num_puzzles, num_threads, search_depth);
    printf("===========================================\n\n");
    
    int passes = playPuzzlesMultiThreaded(puzzle_file, search_depth, num_puzzles, 
                                          num_threads, progress_callback);
    
    printf("\n\nResults:\n");
    printf("===========================================\n");
    printf("Total puzzles: %d\n", num_puzzles);
    printf("Passed: %d\n", passes);
    printf("Failed: %d\n", num_puzzles - passes);
    printf("Success rate: %.2f%%\n", (100.0 * passes) / num_puzzles);
    
    return 0;
}
