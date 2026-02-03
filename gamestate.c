// gamestate.c - GameState initialization and management
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "chess.h"

#define TT_SIZE 1048576  // 1M entries = ~32MB per thread (trade memory for speed)

typedef struct {
    uint64_t key;
    double score;
    int depth;
    uint8_t valid;
} TTEntry;

void initGameState(struct GameState *state)
{
    // Initialize board to empty
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            state->board[i][j].type = -1;
            state->board[i][j].colour = -1;
            state->board[i][j].hasMoved = 0;
        }
    }
    
    // Initialize lastMove
    state->lastMove.fromX = -1;
    state->lastMove.fromY = -1;
    state->lastMove.toX = -1;
    state->lastMove.toY = -1;
    
    // Initialize board history
    memset(state->boardHistory, 0, sizeof(state->boardHistory));
    state->boardHistoryCount = 0;
    state->halfmoveClock = 0;
    state->depth = 0;
    
    // Initialize statistics
    state->evalCount = 0ULL;
    state->ttHitCount = 0ULL;
    state->abPruneCount = 0ULL;
    state->staticPruneCount = 0ULL;
    
    // Allocate transposition table for massive speedup
    state->transposition_table = calloc(TT_SIZE, sizeof(TTEntry));
    if (state->transposition_table == NULL) {
        fprintf(stderr, "Warning: Failed to allocate transposition table\n");
    }
}

void cleanupGameState(struct GameState *state)
{
    // Free transposition table if allocated
    if (state->transposition_table != NULL)
    {
        free(state->transposition_table);
        state->transposition_table = NULL;
    }
}

void recordBoardHistory_ThreadSafe(struct GameState *state)
{
    if (state->boardHistoryCount < 200)
    {
        for (int x = 0; x < 8; x++)
        {
            for (int y = 0; y < 8; y++)
            {
                state->boardHistory[state->boardHistoryCount][x][y] = state->board[x][y];
            }
        }
        state->boardHistoryCount++;
    }
}

int countBoardRepetitions_ThreadSafe(struct GameState *state)
{
    int repetitions = 0;
    
    // Compare current board with all boards in history
    for (int h = 0; h < state->boardHistoryCount; h++)
    {
        int match = 1;
        for (int x = 0; x < 8 && match; x++)
        {
            for (int y = 0; y < 8 && match; y++)
            {
                if (state->board[x][y].type != state->boardHistory[h][x][y].type ||
                    state->board[x][y].colour != state->boardHistory[h][x][y].colour)
                {
                    match = 0;
                }
            }
        }
        if (match)
            repetitions++;
    }
    
    return repetitions;
}
