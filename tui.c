// tui.c - Terminal User Interface using ncurses
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "chess.h"

// Color pairs
#define COLOR_WHITE_PIECE 1
#define COLOR_BLACK_PIECE 2
#define COLOR_BOARD_LIGHT 3
#define COLOR_BOARD_DARK 4
#define COLOR_BORDER 5
#define COLOR_HIGHLIGHT 6
#define COLOR_TITLE 7
#define COLOR_INFO 8
#define COLOR_SUCCESS 9
#define COLOR_WARNING 10
#define COLOR_WHITE_ON_LIGHT 11
#define COLOR_BLACK_ON_LIGHT 12
#define COLOR_WHITE_ON_DARK 13
#define COLOR_BLACK_ON_DARK 14

// Window pointers
static WINDOW *main_win = NULL;
static WINDOW *board_win = NULL;
static WINDOW *stats_win = NULL;
static WINDOW *best_line_win = NULL;
static WINDOW *moves_win = NULL;
static WINDOW *info_win = NULL;

// Move history storage
#define MAX_MOVE_HISTORY 100
static char move_history[MAX_MOVE_HISTORY][32];
static int move_history_count = 0;

// Stats storage
static struct GameStats {
    double last_think_time;
    uint64_t positions_evaluated;
    uint64_t tt_hits;
    uint64_t ab_prunes;
    uint64_t static_prunes;
    int last_eval_score;
    char predicted_sequence[512];
} game_stats = {0};

// Puzzle state tracking
static struct PuzzleState {
    int is_active;
    char expected_moves[512];
    int move_index;
    int total_moves;
    int failed;
    char puzzle_id[64];
    int rating;
} puzzle_state = {0};

// Initialize ncurses and color pairs
void tui_init(void)
{
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    // Initialize color pairs
    init_pair(COLOR_WHITE_PIECE, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_BLACK_PIECE, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_BOARD_LIGHT, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_BOARD_DARK, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_BORDER, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_HIGHLIGHT, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_TITLE, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_INFO, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_SUCCESS, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_WARNING, COLOR_RED, COLOR_BLACK);
    
    // Pieces on different square colors
    init_pair(COLOR_WHITE_ON_LIGHT, COLOR_BLACK, COLOR_WHITE);  // White pieces on white squares
    init_pair(COLOR_BLACK_ON_LIGHT, COLOR_MAGENTA, COLOR_WHITE); // Black pieces on white squares
    init_pair(COLOR_WHITE_ON_DARK, COLOR_WHITE, COLOR_BLACK);   // White pieces on black squares
    init_pair(COLOR_BLACK_ON_DARK, COLOR_MAGENTA, COLOR_BLACK);  // Black pieces on black squares

    // Get terminal dimensions
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Create main window
    main_win = newwin(max_y, max_x, 0, 0);
    
    // Create sub-windows with borders
    // Board window: left side
    int board_width = 40;
    int board_height = 26;
    board_win = newwin(board_height, board_width, 2, 2);
    
    // Stats window: top middle column
    int stats_width = 32;
    int stats_height = 12;
    stats_win = newwin(stats_height, stats_width, 2, board_width + 4);
    
    // Best line window: top right column
    int best_line_width = max_x - board_width - stats_width - 10;
    int best_line_height = 12;
    best_line_win = newwin(best_line_height, best_line_width, 2, board_width + stats_width + 6);
    
    // Moves window: bottom of middle column
    int moves_height = 10;
    moves_win = newwin(moves_height, stats_width, stats_height + 3, board_width + 4);
    
    // Info window: bottom
    int info_height = max_y - board_height - 4;
    if (info_height < 5) info_height = 5;  // Minimum height
    info_win = newwin(info_height, max_x - 4, board_height + 3, 2);

    refresh();
}

// Clean up ncurses
void tui_cleanup(void)
{
    if (board_win) delwin(board_win);
    if (stats_win) delwin(stats_win);
    if (best_line_win) delwin(best_line_win);
    if (moves_win) delwin(moves_win);
    if (info_win) delwin(info_win);
    if (main_win) delwin(main_win);
    endwin();
}

// Draw a fancy border with title
void draw_fancy_border(WINDOW *win, const char *title)
{
    int h, w;
    getmaxyx(win, h, w);
    
    wattron(win, COLOR_PAIR(COLOR_BORDER) | A_BOLD);
    box(win, 0, 0);
    
    if (title && strlen(title) > 0) {
        wattron(win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
        mvwprintw(win, 0, (w - strlen(title)) / 2, " %s ", title);
        wattroff(win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    }
    
    wattroff(win, COLOR_PAIR(COLOR_BORDER) | A_BOLD);
}

// Get piece symbol
const char* get_piece_symbol(enum PieceType type, enum Colour colour)
{
    // ASCII chess pieces - works on all terminals
    if (colour == WHITE) {
        switch (type) {
            case KING:   return "K";
            case QUEEN:  return "Q";
            case ROOK:   return "R";
            case BISHOP: return "B";
            case KNIGHT: return "N";
            case PAWN:   return "P";
            default:     return " ";
        }
    } else {
        switch (type) {
            case KING:   return "k";
            case QUEEN:  return "q";
            case ROOK:   return "r";
            case BISHOP: return "b";
            case KNIGHT: return "n";
            case PAWN:   return "p";
            default:     return " ";
        }
    }
}

// Draw the chess board
void tui_draw_board(struct Piece board[8][8])
{
    werase(board_win);
    draw_fancy_border(board_win, "CHESS BOARD");
    
    // Draw file labels (a-h)
    mvwprintw(board_win, 1, 5, "a   b   c   d   e   f   g   h");
    mvwprintw(board_win, 2, 3, "+---+---+---+---+---+---+---+---+");
    
    // Draw the board
    for (int rank = 7; rank >= 0; rank--) {
        // Rank number
        mvwprintw(board_win, 3 + (7 - rank) * 2, 2, "%d", rank + 1);
        wprintw(board_win, "|");
        
        for (int file = 0; file < 8; file++) {
            // Determine square color
            int is_light = (rank + file) % 2 == 0;
            int color_pair = is_light ? COLOR_BOARD_LIGHT : COLOR_BOARD_DARK;
            
            // Draw square with piece
            wattron(board_win, COLOR_PAIR(color_pair));
            wprintw(board_win, " ");
            
            // Draw piece if present
            if (board[file][rank].type != -1) {
                const char *symbol = get_piece_symbol(board[file][rank].type, board[file][rank].colour);
                
                // Choose color pair based on both square color and piece color
                int piece_color_pair;
                if (is_light) {
                    piece_color_pair = (board[file][rank].colour == WHITE) ? COLOR_WHITE_ON_LIGHT : COLOR_BLACK_ON_LIGHT;
                } else {
                    piece_color_pair = (board[file][rank].colour == WHITE) ? COLOR_WHITE_ON_DARK : COLOR_BLACK_ON_DARK;
                }
                
                wattroff(board_win, COLOR_PAIR(color_pair));
                wattron(board_win, COLOR_PAIR(piece_color_pair) | A_BOLD);
                
                wprintw(board_win, "%s", symbol);
                
                wattroff(board_win, COLOR_PAIR(piece_color_pair) | A_BOLD);
                wattron(board_win, COLOR_PAIR(color_pair));
            } else {
                wprintw(board_win, " ");
            }
            
            wprintw(board_win, " ");
            wattroff(board_win, COLOR_PAIR(color_pair));
            
            // Draw separator
            wprintw(board_win, "|");
        }
        
        // Rank number on right side
        wprintw(board_win, "%d", rank + 1);
        
        // Draw horizontal separator between ranks
        mvwprintw(board_win, 4 + (7 - rank) * 2, 3, "+---+---+---+---+---+---+---+---+");
    }
    
    // Draw file labels again at bottom
    mvwprintw(board_win, 19, 5, "a   b   c   d   e   f   g   h");
    
    wrefresh(board_win);
}

// Draw statistics panel
void tui_draw_stats(enum Colour current_turn)
{
    werase(stats_win);
    draw_fancy_border(stats_win, "ENGINE STATS");
    
    wattron(stats_win, COLOR_PAIR(COLOR_INFO));
    
    mvwprintw(stats_win, 2, 2, "Turn: ");
    if (current_turn == WHITE) {
        wattron(stats_win, COLOR_PAIR(COLOR_WHITE_PIECE) | A_BOLD);
        wprintw(stats_win, "WHITE");
    } else {
        wattron(stats_win, COLOR_PAIR(COLOR_BLACK_PIECE) | A_BOLD);
        wprintw(stats_win, "BLACK");
    }
    wattroff(stats_win, COLOR_PAIR(COLOR_BLACK_PIECE) | A_BOLD);
    wattroff(stats_win, COLOR_PAIR(COLOR_WHITE_PIECE) | A_BOLD);
    
    mvwprintw(stats_win, 3, 2, "----------------------------");
    
    wattron(stats_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    mvwprintw(stats_win, 4, 2, "Think time: ");
    wattron(stats_win, COLOR_PAIR(COLOR_SUCCESS));
    wprintw(stats_win, "%.3fs", game_stats.last_think_time);
    
    wattron(stats_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    mvwprintw(stats_win, 5, 2, "Positions:  ");
    wattron(stats_win, COLOR_PAIR(COLOR_INFO));
    wprintw(stats_win, "%llu", (unsigned long long)game_stats.positions_evaluated);
    
    wattron(stats_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    mvwprintw(stats_win, 6, 2, "TT hits:    ");
    wattron(stats_win, COLOR_PAIR(COLOR_INFO));
    wprintw(stats_win, "%llu", (unsigned long long)game_stats.tt_hits);
    
    wattron(stats_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    mvwprintw(stats_win, 7, 2, "AB prunes:  ");
    wattron(stats_win, COLOR_PAIR(COLOR_INFO));
    wprintw(stats_win, "%llu", (unsigned long long)game_stats.ab_prunes);
    
    wattron(stats_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    // Show puzzle status if active
    if (puzzle_state.is_active || puzzle_state.move_index > 0) {
        mvwprintw(stats_win, 9, 2, "Puzzle: %s", puzzle_state.puzzle_id);
        mvwprintw(stats_win, 10, 2, "Status: ");
        
        if (puzzle_state.failed) {
            wattron(stats_win, COLOR_PAIR(COLOR_WARNING) | A_BOLD);
        } else if (!puzzle_state.is_active && puzzle_state.move_index >= puzzle_state.total_moves) {
            wattron(stats_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
        } else {
            wattron(stats_win, COLOR_PAIR(COLOR_INFO));
        }
        wprintw(stats_win, "%s", tui_get_puzzle_status());
        wattroff(stats_win, A_BOLD);
        wattroff(stats_win, COLOR_PAIR(COLOR_WARNING));
        wattroff(stats_win, COLOR_PAIR(COLOR_SUCCESS));
        wattroff(stats_win, COLOR_PAIR(COLOR_INFO));
    } else {
        mvwprintw(stats_win, 10, 2, "Score: ");
        if (game_stats.last_eval_score > 0) {
            wattron(stats_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
        } else if (game_stats.last_eval_score < 0) {
            wattron(stats_win, COLOR_PAIR(COLOR_WARNING) | A_BOLD);
        } else {
            wattron(stats_win, COLOR_PAIR(COLOR_INFO));
        }
        wprintw(stats_win, "%+d", game_stats.last_eval_score);
    }
    
    wattroff(stats_win, A_BOLD);
    wattroff(stats_win, COLOR_PAIR(COLOR_INFO));
    
    wrefresh(stats_win);
}

// Draw best line panel
void tui_draw_best_line(void)
{
    werase(best_line_win);
    
    int h, w;
    getmaxyx(best_line_win, h, w);
    
    // If in puzzle mode, show both AI's line and puzzle solution side by side
    if (puzzle_state.is_active || puzzle_state.move_index > 0) {
        draw_fancy_border(best_line_win, "AI vs PUZZLE SOLUTION");
        
        int mid_col = w / 2;
        int y = 2;
        
        // Draw column headers
        wattron(best_line_win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
        mvwprintw(best_line_win, y, 2, "AI's Line:");
        mvwprintw(best_line_win, y, mid_col, "Expected:");
        wattroff(best_line_win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
        y += 2;
        
        // Parse AI's predicted sequence
        char ai_moves[10][32] = {0};
        int ai_count = 0;
        if (strlen(game_stats.predicted_sequence) > 0) {
            char seq_copy[512];
            strncpy(seq_copy, game_stats.predicted_sequence, sizeof(seq_copy) - 1);
            seq_copy[sizeof(seq_copy) - 1] = '\0';
            
            char *ptr = seq_copy;
            int move_num;
            char move[16], color[16];
            
            while (ai_count < 10 && sscanf(ptr, "%d. %15s (%15[^)])", &move_num, move, color) == 3) {
                snprintf(ai_moves[ai_count++], 32, "%s", move);
                ptr = strchr(ptr, ')');
                if (ptr == NULL) break;
                ptr++;
                while (*ptr == ' ') ptr++;
                if (!(*ptr >= '0' && *ptr <= '9')) break;
            }
        }
        
        // Parse puzzle solution
        char puzzle_moves[10][32] = {0};
        int puzzle_count = 0;
        if (strlen(puzzle_state.expected_moves) > 0) {
            char moves_copy[512];
            strncpy(moves_copy, puzzle_state.expected_moves, sizeof(moves_copy));
            char *token = strtok(moves_copy, " ");
            while (token != NULL && puzzle_count < 10) {
                strncpy(puzzle_moves[puzzle_count++], token, 31);
                token = strtok(NULL, " ");
            }
        }
        
        // Display moves side by side
        int max_moves = (ai_count > puzzle_count) ? ai_count : puzzle_count;
        wattron(best_line_win, COLOR_PAIR(COLOR_INFO));
        for (int i = 0; i < max_moves && y < h - 1; i++) {
            // AI's move
            if (i < ai_count) {
                // Highlight if it matches expected move
                if (i < puzzle_count && strcmp(ai_moves[i], puzzle_moves[i]) == 0) {
                    wattron(best_line_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
                }
                mvwprintw(best_line_win, y, 2, "%d. %s", i + 1, ai_moves[i]);
                wattroff(best_line_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
            }
            
            // Expected move
            if (i < puzzle_count) {
                mvwprintw(best_line_win, y, mid_col, "%d. %s", i + 1, puzzle_moves[i]);
            }
            
            y++;
        }
        wattroff(best_line_win, COLOR_PAIR(COLOR_INFO));
    } else {
        // Normal mode - just show AI's best line
        draw_fancy_border(best_line_win, "BEST LINE");
        
        wattron(best_line_win, COLOR_PAIR(COLOR_INFO));
        int y = 2;
        
        if (strlen(game_stats.predicted_sequence) > 0) {
            char seq_copy[512];
            strncpy(seq_copy, game_stats.predicted_sequence, sizeof(seq_copy) - 1);
            seq_copy[sizeof(seq_copy) - 1] = '\0';
            
            char *ptr = seq_copy;
            int move_num;
            char move[16], color[16];
            
            while (y < h - 1 && sscanf(ptr, "%d. %15s (%15[^)])", &move_num, move, color) == 3) {
                mvwprintw(best_line_win, y++, 2, "%d. %s (%s)", move_num, move, color);
                
                ptr = strchr(ptr, ')');
                if (ptr == NULL) break;
                ptr++;
                while (*ptr == ' ') ptr++;
                if (!(*ptr >= '0' && *ptr <= '9')) break;
            }
        } else {
            mvwprintw(best_line_win, 2, 2, "Waiting for AI...");
        }
        
        wattroff(best_line_win, COLOR_PAIR(COLOR_INFO));
    }
    
    wrefresh(best_line_win);
}

// Draw move history
void tui_draw_moves(void)
{
    werase(moves_win);
    draw_fancy_border(moves_win, "MOVE HISTORY");
    
    wattron(moves_win, COLOR_PAIR(COLOR_INFO));
    
    int start = (move_history_count > 8) ? move_history_count - 8 : 0;
    int y = 2;
    
    for (int i = start; i < move_history_count && y < 10; i++) {
        if (i % 2 == 0) {
            mvwprintw(moves_win, y, 2, "%d. ", i / 2 + 1);
            wattron(moves_win, COLOR_PAIR(COLOR_WHITE_PIECE));
            wprintw(moves_win, "%s", move_history[i]);
            wattroff(moves_win, COLOR_PAIR(COLOR_WHITE_PIECE));
        } else {
            wattron(moves_win, COLOR_PAIR(COLOR_BLACK_PIECE));
            wprintw(moves_win, " %s", move_history[i]);
            wattroff(moves_win, COLOR_PAIR(COLOR_BLACK_PIECE));
            y++;
        }
    }
    
    wattroff(moves_win, COLOR_PAIR(COLOR_INFO));
    wrefresh(moves_win);
}

// Draw info panel
void tui_draw_info(const char *message, int is_ai_turn)
{
    werase(info_win);
    draw_fancy_border(info_win, is_ai_turn ? "AI ANALYSIS" : "PLAYER INPUT");
    
    wattron(info_win, COLOR_PAIR(COLOR_INFO));
    
    if (is_ai_turn && strlen(game_stats.predicted_sequence) > 0) {
        mvwprintw(info_win, 2, 2, "Predicted variation:");
        
        // Word wrap the predicted sequence
        char *seq = strdup(game_stats.predicted_sequence);
        char *line = seq;
        int y = 3;
        int max_width = COLS - 8;
        
        while (strlen(line) > 0 && y < 8) {
            if ((int)strlen(line) <= max_width) {
                mvwprintw(info_win, y, 2, "%s", line);
                break;
            }
            
            // Find last space before max_width
            int cut = max_width;
            while (cut > 0 && line[cut] != ' ') cut--;
            if (cut == 0) cut = max_width;
            
            char temp = line[cut];
            line[cut] = '\0';
            mvwprintw(info_win, y, 2, "%s", line);
            line[cut] = temp;
            line += cut + 1;
            y++;
        }
        
        free(seq);
    }
    
    if (message && strlen(message) > 0) {
        int h, w;
        getmaxyx(info_win, h, w);
        int msg_y = h - 3;
        
        wattron(info_win, COLOR_PAIR(COLOR_HIGHLIGHT) | A_BOLD);
        mvwprintw(info_win, msg_y, 2, "%s", message);
        wattroff(info_win, COLOR_PAIR(COLOR_HIGHLIGHT) | A_BOLD);
    }
    
    wattroff(info_win, COLOR_PAIR(COLOR_INFO));
    wrefresh(info_win);
}

// Update game statistics
void tui_update_stats(double think_time, unsigned long long positions, unsigned long long tt_hits, 
                      unsigned long long ab_prunes, unsigned long long static_prunes, int eval_score)
{
    game_stats.last_think_time = think_time;
    game_stats.positions_evaluated = positions;
    game_stats.tt_hits = tt_hits;
    game_stats.ab_prunes = ab_prunes;
    game_stats.static_prunes = static_prunes;
    game_stats.last_eval_score = eval_score;
}

// Set predicted move sequence
void tui_set_predicted_sequence(const char *sequence)
{
    strncpy(game_stats.predicted_sequence, sequence, sizeof(game_stats.predicted_sequence) - 1);
    game_stats.predicted_sequence[sizeof(game_stats.predicted_sequence) - 1] = '\0';
}

// Start puzzle tracking
void tui_start_puzzle(const char *moves, const char *puzzle_id, int rating)
{
    puzzle_state.is_active = 1;
    strncpy(puzzle_state.expected_moves, moves, sizeof(puzzle_state.expected_moves) - 1);
    puzzle_state.expected_moves[sizeof(puzzle_state.expected_moves) - 1] = '\0';
    puzzle_state.move_index = 0;
    puzzle_state.failed = 0;
    strncpy(puzzle_state.puzzle_id, puzzle_id, sizeof(puzzle_state.puzzle_id) - 1);
    puzzle_state.puzzle_id[sizeof(puzzle_state.puzzle_id) - 1] = '\0';
    puzzle_state.rating = rating;
    
    // Count total moves
    puzzle_state.total_moves = 0;
    char *temp = strdup(moves);
    char *token = strtok(temp, " ");
    while (token != NULL) {
        puzzle_state.total_moves++;
        token = strtok(NULL, " ");
    }
    free(temp);
}

// Validate a move against the puzzle solution
// Returns: 1 = correct, 0 = wrong, -1 = puzzle complete
int tui_validate_puzzle_move(const char *move_uci)
{
    if (!puzzle_state.is_active || puzzle_state.failed) return 0;
    
    // Parse expected moves to find the current one
    char moves_copy[512];
    strncpy(moves_copy, puzzle_state.expected_moves, sizeof(moves_copy));
    
    char *token = strtok(moves_copy, " ");
    int current_index = 0;
    
    while (token != NULL) {
        if (current_index == puzzle_state.move_index) {
            // Check if user move matches expected move
            if (strcmp(token, move_uci) == 0) {
                puzzle_state.move_index++;
                
                // Check if puzzle is complete
                if (puzzle_state.move_index >= puzzle_state.total_moves) {
                    puzzle_state.is_active = 0;
                    return -1; // Puzzle complete
                }
                return 1; // Correct move
            } else {
                puzzle_state.failed = 1;
                puzzle_state.is_active = 0;
                return 0; // Wrong move
            }
        }
        token = strtok(NULL, " ");
        current_index++;
    }
    
    return 0;
}

// Check if puzzle is active
int tui_is_puzzle_active(void)
{
    return puzzle_state.is_active;
}

// Get the next expected puzzle move (returns NULL if no more moves)
const char* tui_get_next_puzzle_move(void)
{
    if (!puzzle_state.is_active) return NULL;
    
    char moves_copy[512];
    strncpy(moves_copy, puzzle_state.expected_moves, sizeof(moves_copy));
    
    char *token = strtok(moves_copy, " ");
    int current_index = 0;
    
    while (token != NULL) {
        if (current_index == puzzle_state.move_index) {
            static char move_buffer[16];
            strncpy(move_buffer, token, sizeof(move_buffer) - 1);
            move_buffer[sizeof(move_buffer) - 1] = '\0';
            return move_buffer;
        }
        token = strtok(NULL, " ");
        current_index++;
    }
    
    return NULL;
}

// Advance puzzle to next move (used when AI executes puzzle move)
void tui_advance_puzzle_move(void)
{
    if (puzzle_state.is_active) {
        puzzle_state.move_index++;
        if (puzzle_state.move_index >= puzzle_state.total_moves) {
            puzzle_state.is_active = 0;
        }
    }
}

// Get puzzle status string
const char* tui_get_puzzle_status(void)
{
    static char status[128];
    if (!puzzle_state.is_active && puzzle_state.move_index == 0) {
        return "No active puzzle";
    }
    
    if (puzzle_state.failed) {
        snprintf(status, sizeof(status), "FAILED (%d/%d moves)", 
                 puzzle_state.move_index, puzzle_state.total_moves);
    } else if (puzzle_state.move_index >= puzzle_state.total_moves) {
        snprintf(status, sizeof(status), "PASSED! (%d/%d)", 
                 puzzle_state.total_moves, puzzle_state.total_moves);
    } else if (puzzle_state.is_active) {
        snprintf(status, sizeof(status), "In Progress (%d/%d)", 
                 puzzle_state.move_index, puzzle_state.total_moves);
    } else {
        snprintf(status, sizeof(status), "Completed (%d/%d)", 
                 puzzle_state.move_index, puzzle_state.total_moves);
    }
    return status;
}

// Add move to history
void tui_add_move(const char *move)
{
    if (move_history_count < MAX_MOVE_HISTORY) {
        strncpy(move_history[move_history_count], move, sizeof(move_history[0]) - 1);
        move_history[move_history_count][sizeof(move_history[0]) - 1] = '\0';
        move_history_count++;
    } else {
        // Shift history up
        for (int i = 0; i < MAX_MOVE_HISTORY - 1; i++) {
            strcpy(move_history[i], move_history[i + 1]);
        }
        strncpy(move_history[MAX_MOVE_HISTORY - 1], move, sizeof(move_history[0]) - 1);
        move_history[MAX_MOVE_HISTORY - 1][sizeof(move_history[0]) - 1] = '\0';
    }
}

// Clear the TUI move history
void tui_clear_move_history(void)
{
    for (int i = 0; i < MAX_MOVE_HISTORY; i++) {
        move_history[i][0] = '\0';
    }
    move_history_count = 0;
}

// Get input from user
void tui_get_input(char *buffer, int max_len)
{
    // Position cursor at end of message in info window
    int y, x;
    getyx(info_win, y, x);
    
    // Set input text color
    wattron(info_win, COLOR_PAIR(COLOR_INFO));
    
    echo();
    curs_set(1);
    wrefresh(info_win);
    
    // Get input
    wgetnstr(info_win, buffer, max_len - 1);
    
    wattroff(info_win, COLOR_PAIR(COLOR_INFO));
    noecho();
    curs_set(0);
}

// Refresh all windows
void tui_refresh_all(struct Piece board[8][8], enum Colour current_turn, const char *message, int is_ai_turn)
{
    // Draw main title first
    werase(main_win);
    wattron(main_win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    mvwprintw(main_win, 0, (COLS - 25) / 2, "  SACRIFICE CHESS ENGINE ");
    wattroff(main_win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    wrefresh(main_win);
    
    tui_draw_board(board);
    tui_draw_stats(current_turn);
    tui_draw_best_line();
    tui_draw_moves();
    tui_draw_info(message, is_ai_turn);
}

// Show a message and wait for key press
void tui_show_message(const char *message)
{
    werase(info_win);
    draw_fancy_border(info_win, "MESSAGE");
    
    wattron(info_win, COLOR_PAIR(COLOR_WARNING) | A_BOLD);
    mvwprintw(info_win, 2, 2, "%s", message);
    mvwprintw(info_win, 4, 2, "Press any key to continue...");
    wattroff(info_win, COLOR_PAIR(COLOR_WARNING) | A_BOLD);
    
    wrefresh(info_win);
    getch();
}

// Show welcome splash screen
void tui_show_splash(void)
{
    clear();
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int start_y = max_y / 2 - 10;
    int start_x = max_x / 2 - 35;
    
    wattron(stdscr, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    
    mvprintw(start_y++, start_x, "+===================================================================+");
    mvprintw(start_y++, start_x, "|                                                                   |");
    mvprintw(start_y++, start_x, "|                      SACRIFICE  CHESS                             |");
    mvprintw(start_y++, start_x, "|                                                                   |");
    mvprintw(start_y++, start_x, "|                 A Terminal-Based Chess Engine                     |");
    mvprintw(start_y++, start_x, "|                                                                   |");
    mvprintw(start_y++, start_x, "+===================================================================+");
    
    wattroff(stdscr, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    
    start_y += 2;
    
    wattron(stdscr, COLOR_PAIR(COLOR_INFO));
    mvprintw(start_y++, start_x + 5, "By:");
    mvprintw(start_y++, start_x + 5, "     Emma Power");

    
    start_y += 2;
    wattron(stdscr, COLOR_PAIR(COLOR_HIGHLIGHT) | A_BOLD);
    mvprintw(start_y++, start_x + 15, "Press any key to start...");
    wattroff(stdscr, COLOR_PAIR(COLOR_HIGHLIGHT) | A_BOLD);
    
    wattroff(stdscr, COLOR_PAIR(COLOR_INFO));
    
    refresh();
    getch();
    clear();
}

// Load a Lichess puzzle via TUI prompt. Returns 1 on success, 0 on failure.
int tui_load_lichess_puzzle(const char *filename, enum Colour *puzzleTurnOut)
{
    char input[64];
    werase(info_win);
    draw_fancy_border(info_win, "LOAD PUZZLE");
    wattron(info_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(info_win, 2, 2, "Enter puzzle row index (0-based): ");
    wattroff(info_win, COLOR_PAIR(COLOR_INFO));
    wrefresh(info_win);

    // Get input from user
    tui_get_input(input, sizeof(input));

    // Parse number
    int puzzleNumber = atoi(input);
    if (puzzleNumber < 0) {
        tui_show_message("Invalid puzzle number");
        return 0;
    }

    struct LichessPuzzle puzzle;
    if (!loadLichessPuzzle(filename, puzzleNumber, &puzzle)) {
        tui_show_message("Failed to load puzzle from file");
        return 0;
    }

    // Load board from FEN
    if (!loadBoardFromFEN(puzzle.fen, board)) {
        tui_show_message("Failed to parse FEN from puzzle");
        return 0;
    }

    // Reset history and halfmove clock so engine state matches puzzle position
    boardHistoryCount = 0;
    recordBoardHistory();
    halfmoveClock = 0;

    enum Colour puzzleTurn = getTurnFromFEN(puzzle.fen);
    if (puzzleTurnOut) *puzzleTurnOut = puzzleTurn;

    // Start puzzle tracking
    tui_start_puzzle(puzzle.moves, puzzle.puzzleId, puzzle.rating);

    // Puzzle loaded successfully - display will be refreshed by caller
    return 1;
}
