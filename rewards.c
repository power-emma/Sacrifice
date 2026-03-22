/* rewards.c - Runtime globals shared across modules */
#include <stddef.h>

/* Set to 1 to suppress printf during puzzle testing */
int suppress_engine_output = 0;

/* Buffer for last checkmate message (used by TUI) */
char last_checkmate_message[256] = "";

/* Optional callback to report puzzle progress */
void (*puzzle_progress_callback)(int puzzles_completed, int total_puzzles, int current_score) = NULL;
