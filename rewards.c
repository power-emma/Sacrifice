/* rewards.c - Tunable reward and penalty constants for evaluation and search */
#include <stddef.h>

// === Development and Piece Positioning ===
double development_penalty_per_move = 2.0;       // Reduced: less critical without tactical eval
double global_position_table_scale = 15.0;       // Increased: positional table now more important
double knight_backstop_penalty = 50.0;           // Increased: backstop knights now more penalized
double knight_edge_penalty = 35.0;               // Increased: edge knights matter more
double slider_mobility_per_square = 8.0;         // Increased: mobility now primary factor

// === Pawn Evaluation ===
double undefended_central_pawn_penalty = 25.0;   // Increased: central pawns more critical
double central_pawn_bonus = 50.0;                // Increased: central control more rewarded
double pawn_promotion_immediate_bonus = 400.0;   // Increased: promotion is key endgame factor
double pawn_promotion_immediate_distance = 2.0;  // Distance threshold for immediate promotion bonus
double pawn_promotion_delayed_bonus = 100.0;     // Increased: distance matters more
double pawn_promotion_delayed_distance = 4.0;    // Distance threshold for delayed promotion bonus

// === King Safety and Castling ===
double king_hasmoved_penalty = 120.0;            // Increased: castling rights now more important
double king_center_exposure_penalty = 40.0;      // Increased: king center exposure critical
double castling_bonus = 70.0;                    // Increased: castling is valuable
double king_adjacent_attack_bonus = 25.0;        // Increased: king attack control more important

// === Tactical (Pieces Under Attack) ===
// REMOVED: Expensive tactical evaluation no longer computed
// These values are now unused but kept for compatibility
double defended_piece_support_bonus = 0.0;       // Disabled: tactical eval removed
double defended_piece_weaker_penalty = 0.0;      // Disabled: tactical eval removed
double undefended_piece_penalty = 0.0;           // Disabled: tactical eval removed

// === Check and Stalemate ===
double check_penalty_white = 150.0;              // Increased: check avoidance more critical
double check_bonus_black = 150.0;                // Increased: check advantage more valuable
double stalemate_black_penalty = 600.0;          // Increased: stalemate penalties higher
double stalemate_white_penalty = 600.0;          // Increased: stalemate penalties higher

// === Endgame King Island ===
// DISABLED: King island calculation removed for speed
double endgame_king_island_max_norm = 0.0;       // Disabled: king island calc removed
double endgame_king_island_bonus_scale = 0.0;    // Disabled: king island calc removed

// === Search Pruning and Evaluation ===
double static_futility_prune_margin = 300.0;     // Aggressive pruning for faster training (balance: speed vs accuracy)
double checkmate_score = 999999999.0;            // Score for checkmate
double stalemate_score = 500.0;                  // Score for stalemate (drawn position)
double draw_score = 0.0;                         // Score for threefold repetition or 50-move rule

// === Output Control ===
int suppress_engine_output = 0;             // Set to 1 to suppress printf statements during puzzle testing
char last_checkmate_message[256] = "";      // Buffer for last checkmate message

// === Training Callbacks ===
void (*puzzle_progress_callback)(int puzzles_completed, int total_puzzles, int current_score) = NULL;

// === Piece-Square Tables (PST) ===
// These 8x8 matrices define how favorable each square is for each piece type
// Higher values = better squares. Can be trained/tuned to improve play.
// Indexed as [x][y] where x=0-7 (a-h files), y=0-7 (rank 1-8)

// Pawn piece-square table
double pawn_pst[8][8] = {
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},   // Rank 1 (pawns don't start here)
    {5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0},   // Rank 2 (starting position)
    {2.0, 2.0, 3.0, 4.0, 4.0, 3.0, 2.0, 2.0},   // Rank 3
    {1.0, 1.0, 2.0, 5.0, 5.0, 2.0, 1.0, 1.0},   // Rank 4
    {0.5, 0.5, 1.0, 8.0, 8.0, 1.0, 0.5, 0.5},   // Rank 5
    {0.0, 0.0, 0.0, 10.0, 10.0, 0.0, 0.0, 0.0}, // Rank 6
    {0.0, 0.0, 0.0, 15.0, 15.0, 0.0, 0.0, 0.0}, // Rank 7
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}    // Rank 8 (promotes, doesn't exist)
};
double pawn_pst_scale = 1.0;  // Scale factor for pawn PST

// Knight piece-square table
double knight_pst[8][8] = {
    {-10.0, -5.0, -2.0, -2.0, -2.0, -2.0, -5.0, -10.0},  // Rank 1
    {-5.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0, -5.0},          // Rank 2
    {-2.0, 3.0, 8.0, 10.0, 10.0, 8.0, 3.0, -2.0},        // Rank 3
    {-2.0, 3.0, 10.0, 12.0, 12.0, 10.0, 3.0, -2.0},      // Rank 4
    {-2.0, 3.0, 10.0, 12.0, 12.0, 10.0, 3.0, -2.0},      // Rank 5
    {-2.0, 3.0, 8.0, 10.0, 10.0, 8.0, 3.0, -2.0},        // Rank 6
    {-5.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0, -5.0},          // Rank 7
    {-10.0, -5.0, -2.0, -2.0, -2.0, -2.0, -5.0, -10.0}   // Rank 8
};
double knight_pst_scale = 1.0;  // Scale factor for knight PST

// Bishop piece-square table
double bishop_pst[8][8] = {
    {-5.0, -2.0, -2.0, -2.0, -2.0, -2.0, -2.0, -5.0},   // Rank 1
    {-2.0, 3.0, 2.0, 2.0, 2.0, 2.0, 3.0, -2.0},         // Rank 2
    {-2.0, 2.0, 8.0, 3.0, 3.0, 8.0, 2.0, -2.0},         // Rank 3
    {-2.0, 2.0, 3.0, 10.0, 10.0, 3.0, 2.0, -2.0},       // Rank 4
    {-2.0, 2.0, 3.0, 10.0, 10.0, 3.0, 2.0, -2.0},       // Rank 5
    {-2.0, 2.0, 8.0, 3.0, 3.0, 8.0, 2.0, -2.0},         // Rank 6
    {-2.0, 3.0, 2.0, 2.0, 2.0, 2.0, 3.0, -2.0},         // Rank 7
    {-5.0, -2.0, -2.0, -2.0, -2.0, -2.0, -2.0, -5.0}    // Rank 8
};
double bishop_pst_scale = 1.0;  // Scale factor for bishop PST

// Rook piece-square table
double rook_pst[8][8] = {
    {0.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 0.0},       // Rank 1
    {2.0, 2.0, 2.0, 3.0, 3.0, 2.0, 2.0, 2.0},       // Rank 2
    {0.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 0.0},       // Rank 3
    {0.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 0.0},       // Rank 4
    {0.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 0.0},       // Rank 5
    {0.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 0.0},       // Rank 6
    {2.0, 2.0, 2.0, 3.0, 3.0, 2.0, 2.0, 2.0},       // Rank 7
    {0.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 0.0}        // Rank 8
};
double rook_pst_scale = 1.0;  // Scale factor for rook PST

// Queen piece-square table
double queen_pst[8][8] = {
    {-5.0, -3.0, -3.0, -1.0, -1.0, -3.0, -3.0, -5.0},    // Rank 1
    {-3.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -3.0},          // Rank 2
    {-3.0, 0.0, 2.0, 1.0, 1.0, 2.0, 0.0, -3.0},          // Rank 3
    {-1.0, 0.0, 1.0, 3.0, 3.0, 1.0, 0.0, -1.0},          // Rank 4
    {-1.0, 0.0, 1.0, 3.0, 3.0, 1.0, 0.0, -1.0},          // Rank 5
    {-3.0, 0.0, 2.0, 1.0, 1.0, 2.0, 0.0, -3.0},          // Rank 6
    {-3.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -3.0},          // Rank 7
    {-5.0, -3.0, -3.0, -1.0, -1.0, -3.0, -3.0, -5.0}     // Rank 8
};
double queen_pst_scale = 1.0;  // Scale factor for queen PST

// King piece-square table (middlegame - avoid center)
double king_pst_mg[8][8] = {
    {-10.0, -10.0, -10.0, -10.0, -10.0, -10.0, -10.0, -10.0},  // Rank 1
    {-10.0, -8.0, -8.0, -8.0, -8.0, -8.0, -8.0, -10.0},        // Rank 2
    {-10.0, -8.0, -5.0, -5.0, -5.0, -5.0, -8.0, -10.0},        // Rank 3
    {-10.0, -8.0, -5.0, -2.0, -2.0, -5.0, -8.0, -10.0},        // Rank 4
    {-10.0, -8.0, -5.0, -2.0, -2.0, -5.0, -8.0, -10.0},        // Rank 5
    {-10.0, -8.0, -5.0, -5.0, -5.0, -5.0, -8.0, -10.0},        // Rank 6
    {-10.0, -8.0, -8.0, -8.0, -8.0, -8.0, -8.0, -10.0},        // Rank 7
    {-10.0, -10.0, -10.0, -10.0, -10.0, -10.0, -10.0, -10.0}   // Rank 8
};
double king_pst_mg_scale = 1.0;  // Scale factor for middlegame king PST

// King piece-square table (endgame - centralize)
double king_pst_eg[8][8] = {
    {-5.0, -3.0, -3.0, -3.0, -3.0, -3.0, -3.0, -5.0},       // Rank 1
    {-3.0, 0.0, 2.0, 3.0, 3.0, 2.0, 0.0, -3.0},             // Rank 2
    {-3.0, 2.0, 5.0, 8.0, 8.0, 5.0, 2.0, -3.0},             // Rank 3
    {-3.0, 3.0, 8.0, 10.0, 10.0, 8.0, 3.0, -3.0},           // Rank 4
    {-3.0, 3.0, 8.0, 10.0, 10.0, 8.0, 3.0, -3.0},           // Rank 5
    {-3.0, 2.0, 5.0, 8.0, 8.0, 5.0, 2.0, -3.0},             // Rank 6
    {-3.0, 0.0, 2.0, 3.0, 3.0, 2.0, 0.0, -3.0},             // Rank 7
    {-5.0, -3.0, -3.0, -3.0, -3.0, -3.0, -3.0, -5.0}        // Rank 8
};
double king_pst_eg_scale = 1.0;  // Scale factor for endgame king PST

// === Move Offset Score Tables ===
// These store scores for each possible move type/offset a piece can make
// Allows training to learn which directions are valuable

// Pawn move offsets: forward 1, forward 2 (from start), capture left, capture right
// Index: 0=forward1, 1=forward2, 2=capture_left, 3=capture_right
double pawn_move_scores[4] = {
    2.0,    // forward 1 square
    3.0,    // forward 2 squares (from starting position)
    5.0,    // capture left diagonal
    5.0     // capture right diagonal
};

// Knight move offsets: 8 possible L-shaped moves
// Index order: up-right, right-up, right-down, down-right, down-left, left-down, left-up, up-left
double knight_move_scores[8] = {
    3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0  // All equally weighted initially
};

// Bishop move offsets: 4 diagonal directions (up-right, down-right, down-left, up-left)
double bishop_move_scores[4] = {
    2.0, 2.0, 2.0, 2.0  // All diagonal directions equally weighted
};

// Rook move offsets: 4 orthogonal directions (up, right, down, left)
double rook_move_scores[4] = {
    2.0, 2.0, 2.0, 2.0  // All orthogonal directions equally weighted
};

// Queen move offsets: 8 directions (up, up-right, right, down-right, down, down-left, left, up-left)
double queen_move_scores[8] = {
    2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0  // All directions equally weighted
};

// King move offsets: 8 directions (same as queen, but only 1 square)
double king_move_scores[8] = {
    1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0  // All directions equally weighted
};

// === Move Distance Score Tables ===
// Score bonuses for pieces moving different distances
// Allows training to learn whether close or distant moves are more valuable

// Pawn move distance: 1 or 2 squares forward
// Index: 0=1square, 1=2squares
double pawn_move_distance_scores[2] = {
    1.0, 0.5  // Forward 1 more favored than forward 2
};

// Knight move distance: always ~2.24 squares (constant), so just 1 entry per diagonal
// Not useful to vary, but we could have direction preferences
// Keeping simple: index represents which of the 8 moves
double knight_move_distance_scores[8] = {
    1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0  // All equally weighted (fixed distance)
};

// Bishop move distance: 1-7 squares on diagonals
// Index: 0=1square, 1=2squares, 2=3squares, ..., 6=7squares
double bishop_move_distance_scores[7] = {
    2.0, 1.8, 1.6, 1.4, 1.2, 1.0, 0.8  // Closer moves slightly favored
};

// Rook move distance: 1-7 squares orthogonally
// Index: 0=1square, 1=2squares, 2=3squares, ..., 6=7squares
double rook_move_distance_scores[7] = {
    1.0, 1.2, 1.4, 1.6, 1.4, 1.2, 1.0  // Moderate distances favored
};

// Queen move distance: 1-7 squares (diagonals or orthogonal)
// Index: 0=1square, 1=2squares, 2=3squares, ..., 6=7squares
double queen_move_distance_scores[7] = {
    1.5, 1.6, 1.7, 1.8, 1.7, 1.6, 1.5  // Mid-range distances favored
};

// King move distance: always 1 square (fixed)
// Index: just 1 entry (king never moves >1)
double king_move_distance_scores[1] = {
    1.0  // Single distance (1 square)
};