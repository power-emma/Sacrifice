// tui.c - Terminal User Interface using ncurses
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include "chess.h"

// Mutex for thread-safe ncurses access
static pthread_mutex_t tui_mutex = PTHREAD_MUTEX_INITIALIZER;

// Hash function for parameter sets
static uint32_t hash_params(const RewardParams *params) {
    uint32_t hash = 5381;
    const unsigned char *bytes = (const unsigned char *)params;
    size_t size = sizeof(RewardParams);
    for (size_t i = 0; i < size; i++) {
        hash = ((hash << 5) + hash) ^ bytes[i];
    }
    return hash;
}

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
static WINDOW *stats_params_win = NULL;    // Shows training status parameters
static WINDOW *best_params_win = NULL;     // Shows best score parameters

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
    char moves_played[256];  // Store moves for display during puzzle test
    char last_puzzle_moves[256];  // Last puzzle's engine moves
    char last_puzzle_expected[256];  // Last puzzle's expected moves
    char last_puzzle_id[64];  // Last puzzle ID
    int last_puzzle_passed;  // Whether last puzzle passed
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
    
    // Create sub-windows for game screens (board on left, stats/best_line/moves on right)
    // Board window: left side
    int board_width = 40;
    int board_height = 26;
    board_win = newwin(board_height, board_width, 2, 2);
    
    // Stats window: top right
    int stats_width = 32;
    int stats_height = 12;
    stats_win = newwin(stats_height, stats_width, 2, board_width + 4);
    
    // Best line window: top right corner
    int best_line_width = max_x - board_width - stats_width - 10;
    int best_line_height = 12;
    best_line_win = newwin(best_line_height, best_line_width, 2, board_width + stats_width + 6);
    
    // Moves window: bottom of right side
    int moves_height = 10;
    moves_win = newwin(moves_height, stats_width, stats_height + 3, board_width + 4);
    
    // Info window: full width bottom
    int info_height = max_y - board_height - 4;
    if (info_height < 5) info_height = 5;
    info_win = newwin(info_height, max_x - 4, board_height + 3, 2);

    refresh();
}

// Reconfigure windows for training display (full-width three columns)
void tui_reconfigure_for_training(void)
{
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // Delete old windows if they exist
    if (board_win) delwin(board_win);
    if (stats_win) delwin(stats_win);
    if (best_line_win) delwin(best_line_win);
    if (moves_win) delwin(moves_win);
    if (info_win) delwin(info_win);
    if (stats_params_win) delwin(stats_params_win);
    if (best_params_win) delwin(best_params_win);
    
    // Create new windows for training
    // Top row: three columns (stats, best_line, moves) - 11 lines each
    int top_box_width = (max_x - 8) / 3;
    int top_box_height = 11;
    int top_box_start_x = 4;
    
    // Stats window: left column
    stats_win = newwin(top_box_height, top_box_width, 2, top_box_start_x);
    
    // Best line window: middle column
    best_line_win = newwin(top_box_height, top_box_width, 2, top_box_start_x + top_box_width + 2);
    
    // Moves window: right column
    moves_win = newwin(top_box_height, top_box_width, 2, top_box_start_x + (top_box_width + 2) * 2);
    
    // Middle row: two parameter windows below stats and best_line - 9 lines each
    int middle_box_height = 9;
    int middle_row_y = top_box_height + 4;
    
    stats_params_win = newwin(middle_box_height, top_box_width, middle_row_y, top_box_start_x);
    best_params_win = newwin(middle_box_height, top_box_width, middle_row_y, top_box_start_x + top_box_width + 2);
    
    // Info window: full width bottom - thread status
    int info_height = max_y - (top_box_height + 4 + middle_box_height) - 6;
    if (info_height < 3) info_height = 3;  // Minimum height
    info_win = newwin(info_height, max_x - 4, middle_row_y + middle_box_height + 2, 2);
    
    // Board window is not used in training, but keep it null
    board_win = NULL;
    
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
    if (stats_params_win) delwin(stats_params_win);
    if (best_params_win) delwin(best_params_win);
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

// Run puzzles with live TUI display
void tui_run_puzzle_test(const char *filename, int searchDepth)
{
    suppress_engine_output = 1;  // Suppress engine stdout output
    
    int correctMoves = 0;
    int totalMoves = 0;
    int puzzlesPassed = 0;
    int puzzlesFailed = 0;

    for (int puzzleNum = 0; puzzleNum < PUZZLE_TEST_COUNT; puzzleNum++)
    {
        struct LichessPuzzle puzzle;
        if (!loadLichessPuzzle(filename, puzzleNum, &puzzle))
        {
            continue;
        }

        // Clear previous checkmate message
        last_checkmate_message[0] = '\0';

        // Reset board and load FEN
        memset(board, 0, sizeof(board));
        for (int i = 0; i < 8; i++)
        {
            for (int j = 0; j < 8; j++)
            {
                board[i][j].type = -1;
                board[i][j].colour = -1;
                board[i][j].hasMoved = 0;
            }
        }

        enum Colour sideToMove = WHITE;
        if (!loadBoardFromFEN(puzzle.fen, board))
        {
            continue;
        }

        sideToMove = getTurnFromFEN(puzzle.fen);

        // Parse puzzle moves (space-separated UCI notation)
        char movesCopy[512];
        strcpy(movesCopy, puzzle.moves);
        char *token = strtok(movesCopy, " ");

        // Execute the first move (puzzle setup move)
        if (token)
        {
            if (executeUciMove(board, token))
            {
                recordBoardHistory();
                sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
                token = strtok(NULL, " ");
            }
            else
            {
                continue;
            }
        }

        // Now alternate: AI solves, opponent responds
        int puzzleSuccess = 1;
        int puzzleMoveCount = 0;  // Track moves in this puzzle
        puzzle_state.moves_played[0] = '\0';  // Clear moves buffer
        
        while (token)
        {
            // AI's turn (solver side)
            enum Colour aiColour = sideToMove;
            moveRanking(board, searchDepth, aiColour);

            // Get the next expected move from puzzle
            const char *expectedMove = token;
            token = strtok(NULL, " ");

            // Verify AI made the correct move
            char aiMoveNotation[16];
            snprintf(aiMoveNotation, sizeof(aiMoveNotation), "%c%d%c%d",
                     'a' + lastMove.fromX, lastMove.fromY + 1,
                     'a' + lastMove.toX, lastMove.toY + 1);

            // Add move to display buffer
            if (puzzle_state.moves_played[0] != '\0')
                strncat(puzzle_state.moves_played, " ", sizeof(puzzle_state.moves_played) - 1);
            strncat(puzzle_state.moves_played, aiMoveNotation, sizeof(puzzle_state.moves_played) - 1);

            puzzleMoveCount++;
            totalMoves++;

            if (strcmp(aiMoveNotation, expectedMove) != 0)
            {
                // Move doesn't match expected, but check if it's checkmate
                // (multiple checkmate solutions are valid in puzzles)
                enum Colour opponentColour = (aiColour == WHITE) ? BLACK : WHITE;
                if (!isCheckmate(board, opponentColour))
                {
                    puzzleSuccess = 0;
                    break;
                }
                // If it's checkmate, it counts as a win even though move differs
                correctMoves++;
                // Puzzle is complete - no more opponent moves after checkmate
                break;
            }

            correctMoves++;

            sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;

            // Opponent's response (if any)
            if (token)
            {
                if (!executeUciMove(board, token))
                {
                    puzzleSuccess = 0;
                    break;
                }
                recordBoardHistory();
                sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
                token = strtok(NULL, " ");
            }
        }

        if (puzzleSuccess)
        {
            puzzlesPassed++;
        }
        else
        {
            puzzlesFailed++;
        }

        // Save last puzzle info
        strcpy(puzzle_state.last_puzzle_id, puzzle.puzzleId);
        strcpy(puzzle_state.last_puzzle_moves, puzzle_state.moves_played);
        
        // Append checkmate message if one was generated
        if (last_checkmate_message[0] != '\0') {
            strncat(puzzle_state.last_puzzle_moves, " ", sizeof(puzzle_state.last_puzzle_moves) - 1);
            strncat(puzzle_state.last_puzzle_moves, last_checkmate_message, sizeof(puzzle_state.last_puzzle_moves) - 1);
            last_checkmate_message[0] = '\0';  // Clear for next puzzle
        }
        
        strcpy(puzzle_state.last_puzzle_expected, puzzle.moves);
        puzzle_state.last_puzzle_passed = puzzleSuccess;

        // Display live progress
        erase();
        
        attron(COLOR_PAIR(COLOR_TITLE));
        mvprintw(1, 3, "+===================================================+");
        mvprintw(2, 3, "|   PUZZLE TEST: Lichess Puzzles                   |");
        mvprintw(3, 3, "+===================================================+");
        attroff(COLOR_PAIR(COLOR_TITLE));

        mvprintw(5, 5, "Progress: Puzzle %3d / %d", puzzleNum + 1, PUZZLE_TEST_COUNT);
        mvprintw(6, 5, "ID: %s | Rating: %d", puzzle.puzzleId, puzzle.rating);
        
        attron(COLOR_PAIR(puzzleSuccess ? COLOR_SUCCESS : COLOR_WARNING));
        mvprintw(7, 5, "Status: %s", puzzleSuccess ? "[PASS]" : "[FAIL]");
        attroff(COLOR_PAIR(puzzleSuccess ? COLOR_SUCCESS : COLOR_WARNING));

        attron(COLOR_PAIR(COLOR_SUCCESS));
        mvprintw(9, 5, "Passed:  %2d / %3d", puzzlesPassed, puzzleNum + 1);
        attroff(COLOR_PAIR(COLOR_SUCCESS));

        attron(COLOR_PAIR(COLOR_WARNING));
        mvprintw(10, 5, "Failed:  %2d / %3d", puzzlesFailed, puzzleNum + 1);
        attroff(COLOR_PAIR(COLOR_WARNING));

        mvprintw(12, 5, "Correct Moves: %d / %d", correctMoves, totalMoves > 0 ? totalMoves : 1);
        mvprintw(13, 5, "Success Rate:  %.1f%%", (correctMoves * 100.0) / (totalMoves > 0 ? totalMoves : 1));

        // Last Puzzle Section
        attron(COLOR_PAIR(COLOR_TITLE));
        mvprintw(15, 3, "+===================================================+");
        mvprintw(16, 3, "|   LAST PUZZLE                                      |");
        mvprintw(17, 3, "+===================================================+");
        attroff(COLOR_PAIR(COLOR_TITLE));

        mvprintw(18, 5, "ID: %s", puzzle_state.last_puzzle_id);
        
        attron(COLOR_PAIR(puzzle_state.last_puzzle_passed ? COLOR_SUCCESS : COLOR_WARNING));
        mvprintw(19, 5, "Result: %s", puzzle_state.last_puzzle_passed ? "[PASS]" : "[FAIL]");
        attroff(COLOR_PAIR(puzzle_state.last_puzzle_passed ? COLOR_SUCCESS : COLOR_WARNING));

        attron(COLOR_PAIR(COLOR_INFO));
        mvprintw(20, 5, "Engine: %s", puzzle_state.last_puzzle_moves);
        mvprintw(21, 5, "Best:   %s", puzzle_state.last_puzzle_expected);
        attroff(COLOR_PAIR(COLOR_INFO));

        attron(COLOR_PAIR(COLOR_TITLE));
        mvprintw(22, 3, "+===================================================+");
        attroff(COLOR_PAIR(COLOR_TITLE));

        refresh();
    }

    // Final summary screen
    erase();
    attron(COLOR_PAIR(COLOR_TITLE));
    mvprintw(2, 3, "+===================================================+");
    mvprintw(3, 3, "|        PUZZLE TEST COMPLETE - FINAL RESULTS        |");
    mvprintw(4, 3, "+===================================================+");
    attroff(COLOR_PAIR(COLOR_TITLE));

    mvprintw(6, 5, "Total Puzzles: %d", PUZZLE_TEST_COUNT);
    
    attron(COLOR_PAIR(COLOR_SUCCESS));
    mvprintw(7, 5, "Passed:  %d", puzzlesPassed);
    attroff(COLOR_PAIR(COLOR_SUCCESS));

    attron(COLOR_PAIR(COLOR_WARNING));
    mvprintw(8, 5, "Failed:  %d", puzzlesFailed);
    attroff(COLOR_PAIR(COLOR_WARNING));

    mvprintw(10, 5, "Correct Moves: %d / %d", correctMoves, totalMoves);
    mvprintw(11, 5, "Success Rate:  %.1f%%", (correctMoves * 100.0) / (totalMoves > 0 ? totalMoves : 1));

    attron(COLOR_PAIR(COLOR_TITLE));
    mvprintw(13, 3, "+===================================================+");
    attroff(COLOR_PAIR(COLOR_TITLE));

    attron(COLOR_PAIR(COLOR_INFO));
    mvprintw(15, 10, "Press any key to return to menu...");
    attroff(COLOR_PAIR(COLOR_INFO));

    refresh();
    getch();
    
    suppress_engine_output = 0;  // Re-enable output for normal play
}

// ============================================================================
// TRAINING SYSTEM TUI DISPLAY
// ============================================================================

// Update training display in TUI using separate windows
void tui_update_training_display(int iteration, int score, int best_score, int best_iteration, double mutation_rate, int is_new_record __attribute__((unused)), int pass_count, IterationHistory *last_5, int history_count, const RewardParams *best_params, int elapsed_seconds, const RewardParams *top5_params, const int *top5_scores, int top5_count)
{
    // Lock for thread-safe ncurses access
    pthread_mutex_lock(&tui_mutex);
    
    // Clear and setup stats window (left column)
    werase(stats_win);
    draw_fancy_border(stats_win, "TRAINING STATUS");
    
    int y = 2;
    wattron(stats_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(stats_win, y++, 2, "Iteration: %d", iteration);
    mvwprintw(stats_win, y++, 2, "Puzzle: %d/%d", get_training_current_puzzle(), PUZZLE_TEST_COUNT);
    mvwprintw(stats_win, y++, 2, "Current: %d/%d", score, PUZZLE_TEST_COUNT);
    mvwprintw(stats_win, y++, 2, "Best: %d/%d", best_score, PUZZLE_TEST_COUNT);
    mvwprintw(stats_win, y++, 2, "Mutation: %.1f", mutation_rate);
    
    // Algorithm state
    y++;
    wattron(stats_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    mvwprintw(stats_win, y++, 2, "State:");
    wattroff(stats_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    
    const char *state_msg = "Broad Explore";
    if (mutation_rate < 20) state_msg = "Converge";
    if (mutation_rate < 10) state_msg = "Fine Tune";
    if (mutation_rate < 2) state_msg = "Refine";
    
    mvwprintw(stats_win, y++, 2, "%s", state_msg);
    wattroff(stats_win, COLOR_PAIR(COLOR_INFO));
    
    wrefresh(stats_win);
    
    // Clear and setup best line window (middle column) - now shows top 5
    werase(best_line_win);
    draw_fancy_border(best_line_win, "TOP 5 SCORES");
    
    y = 2;
    wattron(best_line_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
    mvwprintw(best_line_win, y, 2, "Best Leaders:");
    wattroff(best_line_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
    
    // Display top 5 scores
    for (int i = 0; i < top5_count && i < 5; i++) {
        wattron(best_line_win, COLOR_PAIR(COLOR_SUCCESS));
        mvwprintw(best_line_win, y++, 2, "#%d: %d/%d (%.0f%%)", i+1, top5_scores[i], PUZZLE_TEST_COUNT,
                  (top5_scores[i] / (double)PUZZLE_TEST_COUNT) * 100.0);
        wattroff(best_line_win, COLOR_PAIR(COLOR_SUCCESS));
    }
    
    y++;
    wattron(best_line_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    wattroff(best_line_win, COLOR_PAIR(COLOR_HIGHLIGHT));

  
    
    // Format and display elapsed time
    int hours = elapsed_seconds / 3600;
    int minutes = (elapsed_seconds % 3600) / 60;
    int seconds = elapsed_seconds % 60;
    if (hours > 0) {
        mvwprintw(best_line_win, y++, 2, "Elapsed: %dh %dm %ds", hours, minutes, seconds);
    } else if (minutes > 0) {
        mvwprintw(best_line_win, y++, 2, "Elapsed: %dm %ds", minutes, seconds);
    } else {
        mvwprintw(best_line_win, y++, 2, "Elapsed: %ds", seconds);
    }
    
    wrefresh(best_line_win);
    
    // Clear and setup moves window (right column)
    werase(moves_win);
    draw_fancy_border(moves_win, "ITERATIONS");
    
    y = 2;
    wattron(moves_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
    mvwprintw(moves_win, y++, 2, ">>> %3d: %d/%d", iteration, pass_count, PUZZLE_TEST_COUNT);
    wattroff(moves_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
    
    for (int i = 0; i < history_count && y < 9; i++)
    {
        wattron(moves_win, COLOR_PAIR(COLOR_INFO));
        mvwprintw(moves_win, y++, 2, "    %3d: %d/%d", 
                  last_5[i].iteration,
                  last_5[i].pass_count,
                  PUZZLE_TEST_COUNT);
        wattroff(moves_win, COLOR_PAIR(COLOR_INFO));
    }
    
    wrefresh(moves_win);
    
    // Clear and setup stats params window (left column, below stats)
    werase(stats_params_win);
    draw_fancy_border(stats_params_win, "CURRENT PARAMS");
    
    y = 2;
    wattron(stats_params_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(stats_params_win, y++, 2, "0x%02x %02x %02x %02x %02x", 
              development_penalty_per_move, global_position_table_scale,
              knight_backstop_penalty, knight_edge_penalty, slider_mobility_per_square);
    mvwprintw(stats_params_win, y++, 2, "0x%02x %02x %02x %02x %02x",
              undefended_central_pawn_penalty, central_pawn_bonus,
              pawn_promotion_immediate_bonus, pawn_promotion_immediate_distance,
              pawn_promotion_delayed_bonus);
    mvwprintw(stats_params_win, y++, 2, "0x%02x %02x %02x %02x %02x",
              pawn_promotion_delayed_distance, king_hasmoved_penalty,
              king_center_exposure_penalty, castling_bonus, king_adjacent_attack_bonus);
    mvwprintw(stats_params_win, y++, 2, "0x%02x %02x %02x %02x %02x",
              defended_piece_support_bonus, defended_piece_weaker_penalty,
              undefended_piece_penalty, check_penalty_white, check_bonus_black);
    mvwprintw(stats_params_win, y++, 2, "0x%02x %02x %02x %02x %02x",
              stalemate_black_penalty, stalemate_white_penalty,
              endgame_king_island_max_norm, endgame_king_island_bonus_scale,
              static_futility_prune_margin);
    wattroff(stats_params_win, COLOR_PAIR(COLOR_INFO));
    
    wrefresh(stats_params_win);
    
    // Clear and setup best params window (middle column, below best_line)
    werase(best_params_win);
    draw_fancy_border(best_params_win, "TOP 5 BEST PARAMS");
    
    y = 2;
    for (int i = 0; i < top5_count && i < 5; i++) {
        
        int color = COLOR_SUCCESS;
        if (i == 1) color = COLOR_INFO;  // second best in cyan
        
        wattron(best_params_win, COLOR_PAIR(color));
        uint32_t param_hash = hash_params(&top5_params[i]);
        mvwprintw(best_params_win, y++, 2, "#%d (%d/500): 0x%08x", i+1, top5_scores[i], param_hash);
        wattroff(best_params_win, COLOR_PAIR(color));
    }
    
    wrefresh(best_params_win);
    
    // Clear and setup info window (bottom) - for thread status
    werase(info_win);
    draw_fancy_border(info_win, "THREAD STATUS (Parallel)");
    
    y = 2;
    int thread_statuses[512];  // Support up to 256 threads (256 * 2 values)
    int num_threads = 0;
    get_thread_puzzle_statuses(&num_threads, thread_statuses);
    
    if (num_threads > 0)
    {
        int max_y_info, max_x_info;
        getmaxyx(info_win, max_y_info, max_x_info);
        
        // Calculate threads per row based on window width
        // Each thread entry is "T##:[##]X" = 9 characters + 2 spaces = 11 chars per thread
        int thread_entry_width = 11;
        int threads_per_row = (max_x_info - 4) / thread_entry_width;
        if (threads_per_row < 1) threads_per_row = 1;  // Minimum 1 per row
        
        for (int i = 0; i < num_threads && y < max_y_info - 1; i++)
        {
            int puzzle_idx = thread_statuses[i * 2];
            int result = thread_statuses[i * 2 + 1];
            
            const char *status_char = "-";
            int color = COLOR_INFO;
            
            if (result == 1)
            {
                status_char = "!";
                color = COLOR_SUCCESS;
            }
            else if (result == 0)
            {
                status_char = "X";
                color = COLOR_WARNING;
            }
            
            int col = 2 + (i % threads_per_row) * thread_entry_width;
            
            wattron(info_win, COLOR_PAIR(color));
            if (puzzle_idx >= 0)
            {
                mvwprintw(info_win, y, col, "T%02d:[%3d]%s", i, puzzle_idx, status_char);
            }
            else
            {
                mvwprintw(info_win, y, col, "T%02d: idle", i);
            }
            
            if ((i + 1) % threads_per_row == 0)
                y++;
            wattroff(info_win, COLOR_PAIR(color));
        }
    }
    else
    {
        // No threads active - show message
        wattron(info_win, COLOR_PAIR(COLOR_INFO));
        mvwprintw(info_win, y, 2, "No active threads (waiting for puzzle test to start...)");
        wattroff(info_win, COLOR_PAIR(COLOR_INFO));
    }
    
    wrefresh(info_win);
    napms(50);
    
    // Unlock ncurses access
    pthread_mutex_unlock(&tui_mutex);
}

// Show training complete screen
void tui_show_training_complete(int best_score, int total_iterations)
{
    werase(main_win);
    draw_fancy_border(main_win, "TRAINING COMPLETE");
    
    int y = 2;
    
    wattron(main_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
    mvwprintw(main_win, y++, 2, "Training Successfully Completed!");
    wattroff(main_win, COLOR_PAIR(COLOR_SUCCESS) | A_BOLD);
    
    y += 2;
    
    wattron(main_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(main_win, y++, 4, "Final Results:");
    wattroff(main_win, COLOR_PAIR(COLOR_INFO));
    
    y += 1;
    
    wattron(main_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    mvwprintw(main_win, y++, 6, "Best Score: %d / %d puzzles", best_score, PUZZLE_TEST_COUNT);
    mvwprintw(main_win, y++, 6, "Total Iterations: %d", total_iterations);
    mvwprintw(main_win, y++, 6, "Output File: best_params.txt");
    wattroff(main_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    
    y += 2;
    
    wattron(main_win, COLOR_PAIR(COLOR_SUCCESS));
    mvwprintw(main_win, y++, 4, "Next Steps:");
    wattroff(main_win, COLOR_PAIR(COLOR_SUCCESS));
    
    mvwprintw(main_win, y++, 6, "1. Review the optimized parameters in best_params.txt");
    mvwprintw(main_win, y++, 6, "2. Copy the values to rewards.c");
    mvwprintw(main_win, y++, 6, "3. Rebuild the project: make clean && make");
    mvwprintw(main_win, y++, 6, "4. Test improvements: run puzzle test (t)");
    
    y += 2;
    
    wattron(main_win, COLOR_PAIR(COLOR_WARNING));
    mvwprintw(main_win, y++, 4, "Press any key to return to menu...");
    wattroff(main_win, COLOR_PAIR(COLOR_WARNING));
    
    wrefresh(main_win);
    getch();
}

// Training interface function with TUI display
void tui_run_training(const char *puzzle_file, int iterations, int search_depth)
{
    (void)puzzle_file;  // Parameter reserved for future use
    
    int max_y, max_x;
    getmaxyx(main_win, max_y, max_x);
    
    // Show initial training screen
    werase(main_win);
    draw_fancy_border(main_win, "TRAINING INITIALIZATION");
    
    int y = 2;
    wattron(main_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(main_win, y++, 4, "Initializing training system...");
    mvwprintw(main_win, y++, 4, "Loading puzzle database...");
    mvwprintw(main_win, y++, 4, "Preparing evaluation parameters...");
    wattroff(main_win, COLOR_PAIR(COLOR_INFO));
    
    y += 2;
    
    wattron(main_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    mvwprintw(main_win, y++, 4, "Training will run for %d iterations", iterations);
    mvwprintw(main_win, y++, 4, "Testing %d puzzles per iteration", PUZZLE_TEST_COUNT);
    mvwprintw(main_win, y++, 4, "Search depth: %d", search_depth);
    wattroff(main_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    
    wrefresh(main_win);
    napms(1500);  // Give more time to see initialization message
    
    // Show "Testing..." message before first evaluation
    werase(main_win);
    draw_fancy_border(main_win, "TRAINING IN PROGRESS");
    int test_y = max_y / 2 - 2;
    wattron(main_win, COLOR_PAIR(COLOR_HIGHLIGHT) | A_BOLD);
    mvwprintw(main_win, test_y++, (max_x - 40) / 2, "Testing initial parameters...");
    mvwprintw(main_win, test_y++, (max_x - 40) / 2, "Please wait...");
    wattroff(main_win, COLOR_PAIR(COLOR_HIGHLIGHT) | A_BOLD);
    wrefresh(main_win);
    
    // Run training with search depth
    int best_score = train_rewards(iterations, search_depth);
    
    // Show completion screen
    tui_show_training_complete(best_score, iterations);
}

// Training interface function with TUI display and configurable threads
void tui_run_training_threaded(const char *puzzle_file, int iterations, int num_threads, int search_depth)
{
    (void)puzzle_file;  // Parameter reserved for future use
    
    int max_y, max_x;
    getmaxyx(main_win, max_y, max_x);
    
    // Prompt for training parameters
    werase(main_win);
    draw_fancy_border(main_win, "TRAINING CONFIGURATION");
    
    int y = 2;
    wattron(main_win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    mvwprintw(main_win, y++, 4, "Configure Training Parameters");
    wattroff(main_win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    
    y += 2;
    
    // Prompt for number of puzzles
    wattron(main_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(main_win, y++, 4, "Number of puzzles per iteration (default 500): ");
    wattroff(main_win, COLOR_PAIR(COLOR_INFO));
    wrefresh(main_win);
    
    echo();
    curs_set(1);
    char input[64];
    wgetnstr(main_win, input, sizeof(input) - 1);
    noecho();
    curs_set(0);
    
    int num_puzzles = atoi(input);
    if (num_puzzles <= 0 || num_puzzles > 10000) num_puzzles = 500;
    PUZZLE_TEST_COUNT = num_puzzles;
    
    // Prompt for number of threads
    y++;
    wattron(main_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(main_win, y++, 4, "Number of threads (default 20): ");
    wattroff(main_win, COLOR_PAIR(COLOR_INFO));
    wrefresh(main_win);
    
    echo();
    curs_set(1);
    wgetnstr(main_win, input, sizeof(input) - 1);
    noecho();
    curs_set(0);
    
    int threads = atoi(input);
    if (threads <= 0 || threads > 256) threads = num_threads;
    num_threads = threads;
    
    // Prompt for number of iterations
    y++;
    wattron(main_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(main_win, y++, 4, "Number of iterations (default 50): ");
    wattroff(main_win, COLOR_PAIR(COLOR_INFO));
    wrefresh(main_win);
    
    echo();
    curs_set(1);
    wgetnstr(main_win, input, sizeof(input) - 1);
    noecho();
    curs_set(0);
    
    int iters = atoi(input);
    if (iters <= 0 || iters > 10000) iters = iterations;
    iterations = iters;
    
    // Prompt for search depth
    y++;
    wattron(main_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(main_win, y++, 4, "Search depth (default 4, lower=faster): ");
    wattroff(main_win, COLOR_PAIR(COLOR_INFO));
    wrefresh(main_win);
    
    echo();
    curs_set(1);
    wgetnstr(main_win, input, sizeof(input) - 1);
    noecho();
    curs_set(0);
    
    int depth_input = atoi(input);
    if (depth_input <= 0 || depth_input > 10) depth_input = search_depth;
    search_depth = depth_input;
    
    // Prompt for prune margin
    y++;
    wattron(main_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(main_win, y++, 4, "Prune margin (default 300, higher=more accurate): ");
    wattroff(main_win, COLOR_PAIR(COLOR_INFO));
    wrefresh(main_win);
    
    echo();
    curs_set(1);
    wgetnstr(main_win, input, sizeof(input) - 1);
    noecho();
    curs_set(0);
    
    int prune_margin_input = atoi(input);
    if (prune_margin_input <= 0 || prune_margin_input > 2000) prune_margin_input = 300;
    static_futility_prune_margin = (double)prune_margin_input;
    
    // Show initial training screen
    werase(main_win);
    draw_fancy_border(main_win, "TRAINING INITIALIZATION");
    
    y = 2;
    wattron(main_win, COLOR_PAIR(COLOR_INFO));
    mvwprintw(main_win, y++, 4, "Initializing training system...");
    mvwprintw(main_win, y++, 4, "Loading puzzle database...");
    mvwprintw(main_win, y++, 4, "Preparing evaluation parameters...");
    wattroff(main_win, COLOR_PAIR(COLOR_INFO));
    
    y += 2;
    
    wattron(main_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    mvwprintw(main_win, y++, 4, "Training will run for %d iterations", iterations);
    mvwprintw(main_win, y++, 4, "Testing %d puzzles per iteration", PUZZLE_TEST_COUNT);
    mvwprintw(main_win, y++, 4, "Using %d threads for parallel evaluation", num_threads);
    mvwprintw(main_win, y++, 4, "Search depth: %d", search_depth);
    wattroff(main_win, COLOR_PAIR(COLOR_HIGHLIGHT));
    
    wrefresh(main_win);
    napms(1500);  // Give more time to see initialization message
    
    // Show "Testing..." message before first evaluation
    werase(main_win);
    draw_fancy_border(main_win, "TRAINING IN PROGRESS");
    int test_y = max_y / 2 - 2;
    wattron(main_win, COLOR_PAIR(COLOR_HIGHLIGHT) | A_BOLD);
    mvwprintw(main_win, test_y++, (max_x - 40) / 2, "Testing initial parameters...");
    mvwprintw(main_win, test_y++, (max_x - 40) / 2, "Please wait...");
    wattroff(main_win, COLOR_PAIR(COLOR_HIGHLIGHT) | A_BOLD);
    wrefresh(main_win);
    
    // Run training with specified thread count and search depth
    int best_score = train_rewards_threaded(iterations, num_threads, search_depth);
    
    // Show completion screen
    tui_show_training_complete(best_score, iterations);
}

