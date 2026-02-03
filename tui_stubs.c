// tui_stubs.c - Stub functions for TUI when not using full TUI
#include "chess.h"

void tui_set_predicted_sequence(const char *sequence) { (void)sequence; }
void tui_update_stats(double think_time, unsigned long long positions, unsigned long long tt_hits, 
                      unsigned long long ab_prunes, unsigned long long static_prunes, int eval_score)
{
    (void)think_time; (void)positions; (void)tt_hits; (void)ab_prunes; (void)static_prunes; (void)eval_score;
}
void tui_add_move(const char *move) { (void)move; }
int tui_validate_puzzle_move(const char *move_uci) { (void)move_uci; return 0; }
