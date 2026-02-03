/*
 * training.c - Reward-based training system for tuning evaluation parameters
 * 
 * This module implements a genetic algorithm-like training system that:
 * 1. Tests the AI on puzzles 1-100 with randomized reward values
 * 2. Scores runs based on puzzle solving accuracy
 * 3. Keeps better parameters with higher probability
 * 4. Gradually refines tweaks as training progresses (simulated annealing)
 * 5. Outputs best parameters to file when new records are achieved
 */

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "chess.h"

// External declarations for PST and move score tables from rewards.c
extern double pawn_move_scores[4];
extern double knight_move_scores[8];
extern double bishop_move_scores[4];
extern double rook_move_scores[4];
extern double queen_move_scores[8];
extern double king_move_scores[8];

// Move distance score tables from rewards.c
extern double pawn_move_distance_scores[2];
extern double knight_move_distance_scores[8];
extern double bishop_move_distance_scores[7];
extern double rook_move_distance_scores[7];
extern double queen_move_distance_scores[7];
extern double king_move_distance_scores[1];

/* ======================== Training State ======================== */

typedef struct {
    RewardParams params;
    int score;
    int iteration;
} TrainingRun;

// Structure to track the top 5 best parameter sets
typedef struct {
    RewardParams params[5];  // Top 5 parameter sets
    int scores[5];           // Their scores
    int count;               // How many we have (0-5)
} Top5Params;

// Global variables for top 5 tracking (used by puzzle callback)
static Top5Params training_top5 = {0};

/* ======================== Configuration ======================== */

#define NUM_PARAMS 548  // 31 scalars + 448 PST values + 37 move offsets + 32 move distances
#define INITIAL_MUTATION_RATE 30.0       // Initial standard deviation for mutations (floating point)
#define MIN_MUTATION_RATE 0.001         // Minimum mutation rate (refined tweaks in decimals)
#define COOLING_SCHEDULE 0.99           // Decay factor per iteration (like temperature in annealing)
#define CHECKPOINT_INTERVAL 10          // Save progress every N iterations
#define BEST_PARAMS_FILE "best_params.txt"

/* ======================== Helper Functions ======================== */

/**
 * Get current mutation rate based on iteration (simulated annealing schedule)
 */
static double get_mutation_rate(int iteration, double initial_rate, double cooling) {
    double rate = initial_rate * pow(cooling, iteration);
    if (rate < MIN_MUTATION_RATE) rate = MIN_MUTATION_RATE;
    return rate;
}

/**
 * Gaussian random number (Box-Muller transform)
 * Returns a standard normal random variable N(0, 1)
 */
static double gaussian_random(void) {
    static int has_spare = 0;
    static double spare;
    
    if (has_spare) {
        has_spare = 0;
        return spare;
    }
    
    has_spare = 1;
    double u = ((double)rand() / RAND_MAX);
    double v = ((double)rand() / RAND_MAX);
    double r = sqrt(-2.0 * log(u));
    double theta = 2.0 * 3.14159265358979323846 * v;  // Use pi constant directly
    
    spare = r * sin(theta);
    return r * cos(theta);
}

/**
 * Apply mutation to a parameter with given mutation rate
 * Uses parameter-specific bounds to allow small integer-like values for distance/bonus params
 */
static double mutate_param_bounded(double value, double mutation_rate, double min_val, double max_val) {
    // Small tweak based on Gaussian distribution
    double delta = gaussian_random() * mutation_rate;
    double new_value = value + delta;
    
    // Keep values within parameter-specific bounds
    if (new_value < min_val) new_value = min_val;
    if (new_value > max_val) new_value = max_val;
    
    return new_value;
}

/**
 * Apply mutation to a parameter with default bounds
 */
static double mutate_param(double value, double mutation_rate) {
    return mutate_param_bounded(value, mutation_rate, 0.001, 1000000.0);
}

/**
 * Initialize parameters with exact hardcoded baseline values (no mutations)
 */
static void init_baseline_params(RewardParams *params) {
    params->development_penalty_per_move = 3.0;
    params->global_position_table_scale = 10.0;
    params->knight_backstop_penalty = 40.0;
    params->knight_edge_penalty = 30.0;
    params->slider_mobility_per_square = 5.0;
    
    params->undefended_central_pawn_penalty = 20.0;
    params->central_pawn_bonus = 40.0;
    params->pawn_promotion_immediate_bonus = 300.0;
    params->pawn_promotion_immediate_distance = 2.0;
    params->pawn_promotion_delayed_bonus = 80.0;
    params->pawn_promotion_delayed_distance = 4.0;
    
    params->king_hasmoved_penalty = 100.0;
    params->king_center_exposure_penalty = 30.0;
    params->castling_bonus = 50.0;
    params->king_adjacent_attack_bonus = 20.0;
    
    params->defended_piece_support_bonus = 120.0;
    params->defended_piece_weaker_penalty = 10.0;
    params->undefended_piece_penalty = 70.0;
    
    params->check_penalty_white = 100.0;
    params->check_bonus_black = 100.0;
    params->stalemate_black_penalty = 500.0;
    params->stalemate_white_penalty = 500.0;
    
    params->endgame_king_island_max_norm = 16.0;
    params->endgame_king_island_bonus_scale = 4.0;
    
    params->static_futility_prune_margin = 500.0;
    params->checkmate_score = 999999999.0;
    params->stalemate_score = 500.0;
    params->draw_score = 0.0;
    
    // Copy PST values from globals
    memcpy(params->pawn_pst, pawn_pst, sizeof(pawn_pst));
    memcpy(params->knight_pst, knight_pst, sizeof(knight_pst));
    memcpy(params->bishop_pst, bishop_pst, sizeof(bishop_pst));
    memcpy(params->rook_pst, rook_pst, sizeof(rook_pst));
    memcpy(params->queen_pst, queen_pst, sizeof(queen_pst));
    memcpy(params->king_pst_mg, king_pst_mg, sizeof(king_pst_mg));
    memcpy(params->king_pst_eg, king_pst_eg, sizeof(king_pst_eg));
    
    // Copy move offset scores from globals
    memcpy(params->pawn_move_scores, pawn_move_scores, sizeof(pawn_move_scores));
    memcpy(params->knight_move_scores, knight_move_scores, sizeof(knight_move_scores));
    memcpy(params->bishop_move_scores, bishop_move_scores, sizeof(bishop_move_scores));
    memcpy(params->rook_move_scores, rook_move_scores, sizeof(rook_move_scores));
    memcpy(params->queen_move_scores, queen_move_scores, sizeof(queen_move_scores));
    memcpy(params->king_move_scores, king_move_scores, sizeof(king_move_scores));
    
    // Copy move distance scores from globals
    memcpy(params->pawn_move_distance_scores, pawn_move_distance_scores, sizeof(pawn_move_distance_scores));
    memcpy(params->knight_move_distance_scores, knight_move_distance_scores, sizeof(knight_move_distance_scores));
    memcpy(params->bishop_move_distance_scores, bishop_move_distance_scores, sizeof(bishop_move_distance_scores));
    memcpy(params->rook_move_distance_scores, rook_move_distance_scores, sizeof(rook_move_distance_scores));
    memcpy(params->queen_move_distance_scores, queen_move_distance_scores, sizeof(queen_move_distance_scores));
    memcpy(params->king_move_distance_scores, king_move_distance_scores, sizeof(king_move_distance_scores));
}

/**
 * Initialize parameters with randomized values
 */
static void init_random_params(RewardParams *params, double mutation_rate) {
    // Start from baseline values and add mutations with parameter-specific bounds
    params->development_penalty_per_move = mutate_param_bounded(3.0, mutation_rate, 0.1, 20.0);
    params->global_position_table_scale = mutate_param_bounded(10.0, mutation_rate, 1.0, 100.0);
    params->knight_backstop_penalty = mutate_param_bounded(40.0, mutation_rate, 1.0, 200.0);
    params->knight_edge_penalty = mutate_param_bounded(30.0, mutation_rate, 1.0, 200.0);
    params->slider_mobility_per_square = mutate_param_bounded(5.0, mutation_rate, 0.1, 50.0);
    
    params->undefended_central_pawn_penalty = mutate_param_bounded(20.0, mutation_rate, 1.0, 100.0);
    params->central_pawn_bonus = mutate_param_bounded(40.0, mutation_rate, 1.0, 100.0);
    params->pawn_promotion_immediate_bonus = mutate_param_bounded(300.0, mutation_rate, 50.0, 1000.0);
    params->pawn_promotion_immediate_distance = mutate_param_bounded(2.0, mutation_rate, 0.5, 10.0);
    params->pawn_promotion_delayed_bonus = mutate_param_bounded(80.0, mutation_rate, 10.0, 500.0);
    params->pawn_promotion_delayed_distance = mutate_param_bounded(4.0, mutation_rate, 0.5, 10.0);
    
    params->king_hasmoved_penalty = mutate_param_bounded(100.0, mutation_rate, 1.0, 500.0);
    params->king_center_exposure_penalty = mutate_param_bounded(30.0, mutation_rate, 1.0, 200.0);
    params->castling_bonus = mutate_param_bounded(50.0, mutation_rate, 1.0, 500.0);
    params->king_adjacent_attack_bonus = mutate_param_bounded(20.0, mutation_rate, 0.1, 200.0);
    
    params->defended_piece_support_bonus = mutate_param_bounded(120.0, mutation_rate, 10.0, 500.0);
    params->defended_piece_weaker_penalty = mutate_param_bounded(10.0, mutation_rate, 0.1, 100.0);
    params->undefended_piece_penalty = mutate_param_bounded(70.0, mutation_rate, 1.0, 300.0);
    
    params->check_penalty_white = mutate_param_bounded(100.0, mutation_rate, 1.0, 500.0);
    params->check_bonus_black = mutate_param_bounded(100.0, mutation_rate, 1.0, 500.0);
    params->stalemate_black_penalty = mutate_param_bounded(500.0, mutation_rate, 100.0, 2000.0);
    params->stalemate_white_penalty = mutate_param_bounded(500.0, mutation_rate, 100.0, 2000.0);
    
    params->endgame_king_island_max_norm = mutate_param_bounded(16.0, mutation_rate, 1.0, 100.0);
    params->endgame_king_island_bonus_scale = mutate_param_bounded(4.0, mutation_rate, 0.1, 50.0);
    
    params->static_futility_prune_margin = mutate_param_bounded(500.0, mutation_rate, 10.0, 2000.0);
    params->checkmate_score = mutate_param_bounded(999999999.0, mutation_rate, 1000000.0, 1000000000.0);
    params->stalemate_score = mutate_param_bounded(500.0, mutation_rate, 0.0, 2000.0);
    params->draw_score = mutate_param_bounded(0.0, mutation_rate, -500.0, 500.0);
    
    // Mutate all PST values
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            params->pawn_pst[i][j] = mutate_param_bounded(pawn_pst[i][j], mutation_rate, -50.0, 50.0);
            params->knight_pst[i][j] = mutate_param_bounded(knight_pst[i][j], mutation_rate, -50.0, 50.0);
            params->bishop_pst[i][j] = mutate_param_bounded(bishop_pst[i][j], mutation_rate, -50.0, 50.0);
            params->rook_pst[i][j] = mutate_param_bounded(rook_pst[i][j], mutation_rate, -50.0, 50.0);
            params->queen_pst[i][j] = mutate_param_bounded(queen_pst[i][j], mutation_rate, -50.0, 50.0);
            params->king_pst_mg[i][j] = mutate_param_bounded(king_pst_mg[i][j], mutation_rate, -50.0, 50.0);
            params->king_pst_eg[i][j] = mutate_param_bounded(king_pst_eg[i][j], mutation_rate, -50.0, 50.0);
        }
    }
    
    // Mutate move offset scores
    for (int i = 0; i < 4; i++) {
        params->pawn_move_scores[i] = mutate_param_bounded(pawn_move_scores[i], mutation_rate, 0.1, 20.0);
        params->bishop_move_scores[i] = mutate_param_bounded(bishop_move_scores[i], mutation_rate, 0.1, 20.0);
        params->rook_move_scores[i] = mutate_param_bounded(rook_move_scores[i], mutation_rate, 0.1, 20.0);
    }
    for (int i = 0; i < 8; i++) {
        params->knight_move_scores[i] = mutate_param_bounded(knight_move_scores[i], mutation_rate, 0.1, 20.0);
        params->queen_move_scores[i] = mutate_param_bounded(queen_move_scores[i], mutation_rate, 0.1, 20.0);
        params->king_move_scores[i] = mutate_param_bounded(king_move_scores[i], mutation_rate, 0.1, 20.0);
    }
    
    // Mutate move distance scores
    for (int i = 0; i < 2; i++) {
        params->pawn_move_distance_scores[i] = mutate_param_bounded(pawn_move_distance_scores[i], mutation_rate, 0.1, 20.0);
    }
    for (int i = 0; i < 8; i++) {
        params->knight_move_distance_scores[i] = mutate_param_bounded(knight_move_distance_scores[i], mutation_rate, 0.1, 20.0);
    }
    for (int i = 0; i < 7; i++) {
        params->bishop_move_distance_scores[i] = mutate_param_bounded(bishop_move_distance_scores[i], mutation_rate, 0.1, 20.0);
        params->rook_move_distance_scores[i] = mutate_param_bounded(rook_move_distance_scores[i], mutation_rate, 0.1, 20.0);
        params->queen_move_distance_scores[i] = mutate_param_bounded(queen_move_distance_scores[i], mutation_rate, 0.1, 20.0);
    }
    for (int i = 0; i < 1; i++) {
        params->king_move_distance_scores[i] = mutate_param_bounded(king_move_distance_scores[i], mutation_rate, 0.1, 20.0);
    }
}

/**
 * Mutate existing parameters with parameter-specific bounds
 */
static void mutate_params(RewardParams *dest, const RewardParams *src, double mutation_rate) {
    dest->development_penalty_per_move = mutate_param_bounded(src->development_penalty_per_move, mutation_rate, 0.1, 20.0);
    dest->global_position_table_scale = mutate_param_bounded(src->global_position_table_scale, mutation_rate, 1.0, 100.0);
    dest->knight_backstop_penalty = mutate_param_bounded(src->knight_backstop_penalty, mutation_rate, 1.0, 200.0);
    dest->knight_edge_penalty = mutate_param_bounded(src->knight_edge_penalty, mutation_rate, 1.0, 200.0);
    dest->slider_mobility_per_square = mutate_param_bounded(src->slider_mobility_per_square, mutation_rate, 0.1, 50.0);
    
    dest->undefended_central_pawn_penalty = mutate_param_bounded(src->undefended_central_pawn_penalty, mutation_rate, 1.0, 100.0);
    dest->central_pawn_bonus = mutate_param_bounded(src->central_pawn_bonus, mutation_rate, 1.0, 100.0);
    dest->pawn_promotion_immediate_bonus = mutate_param_bounded(src->pawn_promotion_immediate_bonus, mutation_rate, 50.0, 1000.0);
    dest->pawn_promotion_immediate_distance = mutate_param_bounded(src->pawn_promotion_immediate_distance, mutation_rate, 0.5, 10.0);
    dest->pawn_promotion_delayed_bonus = mutate_param_bounded(src->pawn_promotion_delayed_bonus, mutation_rate, 10.0, 500.0);
    dest->pawn_promotion_delayed_distance = mutate_param_bounded(src->pawn_promotion_delayed_distance, mutation_rate, 0.5, 10.0);
    
    dest->king_hasmoved_penalty = mutate_param_bounded(src->king_hasmoved_penalty, mutation_rate, 1.0, 500.0);
    dest->king_center_exposure_penalty = mutate_param_bounded(src->king_center_exposure_penalty, mutation_rate, 1.0, 200.0);
    dest->castling_bonus = mutate_param_bounded(src->castling_bonus, mutation_rate, 1.0, 500.0);
    dest->king_adjacent_attack_bonus = mutate_param_bounded(src->king_adjacent_attack_bonus, mutation_rate, 0.1, 200.0);
    
    dest->defended_piece_support_bonus = mutate_param_bounded(src->defended_piece_support_bonus, mutation_rate, 10.0, 500.0);
    dest->defended_piece_weaker_penalty = mutate_param_bounded(src->defended_piece_weaker_penalty, mutation_rate, 0.1, 100.0);
    dest->undefended_piece_penalty = mutate_param_bounded(src->undefended_piece_penalty, mutation_rate, 1.0, 300.0);
    
    dest->check_penalty_white = mutate_param_bounded(src->check_penalty_white, mutation_rate, 1.0, 500.0);
    dest->check_bonus_black = mutate_param_bounded(src->check_bonus_black, mutation_rate, 1.0, 500.0);
    dest->stalemate_black_penalty = mutate_param_bounded(src->stalemate_black_penalty, mutation_rate, 100.0, 2000.0);
    dest->stalemate_white_penalty = mutate_param_bounded(src->stalemate_white_penalty, mutation_rate, 100.0, 2000.0);
    
    dest->endgame_king_island_max_norm = mutate_param_bounded(src->endgame_king_island_max_norm, mutation_rate, 1.0, 100.0);
    dest->endgame_king_island_bonus_scale = mutate_param_bounded(src->endgame_king_island_bonus_scale, mutation_rate, 0.1, 50.0);
    
    dest->static_futility_prune_margin = mutate_param_bounded(src->static_futility_prune_margin, mutation_rate, 10.0, 2000.0);
    dest->checkmate_score = mutate_param_bounded(src->checkmate_score, mutation_rate, 1000000.0, 1000000000.0);
    dest->stalemate_score = mutate_param_bounded(src->stalemate_score, mutation_rate, 0.0, 2000.0);
    dest->draw_score = mutate_param_bounded(src->draw_score, mutation_rate, -500.0, 500.0);
    
    // Mutate all PST values
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            dest->pawn_pst[i][j] = mutate_param_bounded(src->pawn_pst[i][j], mutation_rate, -50.0, 50.0);
            dest->knight_pst[i][j] = mutate_param_bounded(src->knight_pst[i][j], mutation_rate, -50.0, 50.0);
            dest->bishop_pst[i][j] = mutate_param_bounded(src->bishop_pst[i][j], mutation_rate, -50.0, 50.0);
            dest->rook_pst[i][j] = mutate_param_bounded(src->rook_pst[i][j], mutation_rate, -50.0, 50.0);
            dest->queen_pst[i][j] = mutate_param_bounded(src->queen_pst[i][j], mutation_rate, -50.0, 50.0);
            dest->king_pst_mg[i][j] = mutate_param_bounded(src->king_pst_mg[i][j], mutation_rate, -50.0, 50.0);
            dest->king_pst_eg[i][j] = mutate_param_bounded(src->king_pst_eg[i][j], mutation_rate, -50.0, 50.0);
        }
    }
    
    // Mutate move offset scores
    for (int i = 0; i < 4; i++) {
        dest->pawn_move_scores[i] = mutate_param_bounded(src->pawn_move_scores[i], mutation_rate, 0.1, 20.0);
        dest->bishop_move_scores[i] = mutate_param_bounded(src->bishop_move_scores[i], mutation_rate, 0.1, 20.0);
        dest->rook_move_scores[i] = mutate_param_bounded(src->rook_move_scores[i], mutation_rate, 0.1, 20.0);
    }
    for (int i = 0; i < 8; i++) {
        dest->knight_move_scores[i] = mutate_param_bounded(src->knight_move_scores[i], mutation_rate, 0.1, 20.0);
        dest->queen_move_scores[i] = mutate_param_bounded(src->queen_move_scores[i], mutation_rate, 0.1, 20.0);
        dest->king_move_scores[i] = mutate_param_bounded(src->king_move_scores[i], mutation_rate, 0.1, 20.0);
    }
    
    // Mutate move distance scores
    for (int i = 0; i < 2; i++) {
        dest->pawn_move_distance_scores[i] = mutate_param_bounded(src->pawn_move_distance_scores[i], mutation_rate, 0.1, 20.0);
    }
    for (int i = 0; i < 8; i++) {
        dest->knight_move_distance_scores[i] = mutate_param_bounded(src->knight_move_distance_scores[i], mutation_rate, 0.1, 20.0);
    }
    for (int i = 0; i < 7; i++) {
        dest->bishop_move_distance_scores[i] = mutate_param_bounded(src->bishop_move_distance_scores[i], mutation_rate, 0.1, 20.0);
        dest->rook_move_distance_scores[i] = mutate_param_bounded(src->rook_move_distance_scores[i], mutation_rate, 0.1, 20.0);
        dest->queen_move_distance_scores[i] = mutate_param_bounded(src->queen_move_distance_scores[i], mutation_rate, 0.1, 20.0);
    }
    for (int i = 0; i < 1; i++) {
        dest->king_move_distance_scores[i] = mutate_param_bounded(src->king_move_distance_scores[i], mutation_rate, 0.1, 20.0);
    }
}
static void copy_params(RewardParams *dest, const RewardParams *src) {
    memcpy(dest, src, sizeof(RewardParams));
}

/**
 * Add or update a parameter set in the top 5 list
 * Keeps the list sorted by score (best first)
 */
static void update_top5(Top5Params *top5, const RewardParams *params, int score) {
    // Find position to insert (keep sorted, highest score first)
    int insert_pos = top5->count;
    for (int i = 0; i < top5->count; i++) {
        if (score > top5->scores[i]) {
            insert_pos = i;
            break;
        }
    }
    
    // If already at capacity and new score would go at the end, ignore
    if (insert_pos >= 5) {
        return;  // Not in top 5
    }
    
    // Shift everything at and after insert_pos down by 1
    if (top5->count < 5) {
        // Haven't reached capacity yet, just add
        if (insert_pos < top5->count) {
            for (int i = top5->count; i > insert_pos; i--) {
                copy_params(&top5->params[i], &top5->params[i-1]);
                top5->scores[i] = top5->scores[i-1];
            }
        }
        top5->count++;
    } else {
        // At capacity, shift down from the end
        for (int i = 4; i > insert_pos; i--) {
            copy_params(&top5->params[i], &top5->params[i-1]);
            top5->scores[i] = top5->scores[i-1];
        }
    }
    
    // Insert the new params at insert_pos
    copy_params(&top5->params[insert_pos], params);
    top5->scores[insert_pos] = score;
}

/**
 * Select a random parameter set from the top 5
 * Returns one of the best found so far
 */
static void select_random_from_top5(RewardParams *dest, Top5Params *top5) {
    if (top5->count == 0) {
        // Shouldn't happen, but fallback to baseline
        init_baseline_params(dest);
        return;
    }
    
    // Pick random from available top parameters
    int idx = rand() % top5->count;
    copy_params(dest, &top5->params[idx]);
}

/**
 * Apply parameters to global reward variables
 */
static void apply_params(const RewardParams *params) {
    development_penalty_per_move = params->development_penalty_per_move;
    global_position_table_scale = params->global_position_table_scale;
    knight_backstop_penalty = params->knight_backstop_penalty;
    knight_edge_penalty = params->knight_edge_penalty;
    slider_mobility_per_square = params->slider_mobility_per_square;
    
    undefended_central_pawn_penalty = params->undefended_central_pawn_penalty;
    central_pawn_bonus = params->central_pawn_bonus;
    pawn_promotion_immediate_bonus = params->pawn_promotion_immediate_bonus;
    pawn_promotion_immediate_distance = params->pawn_promotion_immediate_distance;
    pawn_promotion_delayed_bonus = params->pawn_promotion_delayed_bonus;
    pawn_promotion_delayed_distance = params->pawn_promotion_delayed_distance;
    
    king_hasmoved_penalty = params->king_hasmoved_penalty;
    king_center_exposure_penalty = params->king_center_exposure_penalty;
    castling_bonus = params->castling_bonus;
    king_adjacent_attack_bonus = params->king_adjacent_attack_bonus;
    
    defended_piece_support_bonus = params->defended_piece_support_bonus;
    defended_piece_weaker_penalty = params->defended_piece_weaker_penalty;
    undefended_piece_penalty = params->undefended_piece_penalty;
    
    check_penalty_white = params->check_penalty_white;
    check_bonus_black = params->check_bonus_black;
    stalemate_black_penalty = params->stalemate_black_penalty;
    stalemate_white_penalty = params->stalemate_white_penalty;
    
    endgame_king_island_max_norm = params->endgame_king_island_max_norm;
    endgame_king_island_bonus_scale = params->endgame_king_island_bonus_scale;
    
    static_futility_prune_margin = params->static_futility_prune_margin;
    checkmate_score = params->checkmate_score;
    stalemate_score = params->stalemate_score;
    draw_score = params->draw_score;
    
    // Copy all PST values
    memcpy(pawn_pst, params->pawn_pst, sizeof(pawn_pst));
    memcpy(knight_pst, params->knight_pst, sizeof(knight_pst));
    memcpy(bishop_pst, params->bishop_pst, sizeof(bishop_pst));
    memcpy(rook_pst, params->rook_pst, sizeof(rook_pst));
    memcpy(queen_pst, params->queen_pst, sizeof(queen_pst));
    memcpy(king_pst_mg, params->king_pst_mg, sizeof(king_pst_mg));
    memcpy(king_pst_eg, params->king_pst_eg, sizeof(king_pst_eg));
    
    // Copy all move offset scores
    memcpy(pawn_move_scores, params->pawn_move_scores, sizeof(pawn_move_scores));
    memcpy(knight_move_scores, params->knight_move_scores, sizeof(knight_move_scores));
    memcpy(bishop_move_scores, params->bishop_move_scores, sizeof(bishop_move_scores));
    memcpy(rook_move_scores, params->rook_move_scores, sizeof(rook_move_scores));
    memcpy(queen_move_scores, params->queen_move_scores, sizeof(queen_move_scores));
    memcpy(king_move_scores, params->king_move_scores, sizeof(king_move_scores));
    
    // Copy all move distance scores
    memcpy(pawn_move_distance_scores, params->pawn_move_distance_scores, sizeof(pawn_move_distance_scores));
    memcpy(knight_move_distance_scores, params->knight_move_distance_scores, sizeof(knight_move_distance_scores));
    memcpy(bishop_move_distance_scores, params->bishop_move_distance_scores, sizeof(bishop_move_distance_scores));
    memcpy(rook_move_distance_scores, params->rook_move_distance_scores, sizeof(rook_move_distance_scores));
    memcpy(queen_move_distance_scores, params->queen_move_distance_scores, sizeof(queen_move_distance_scores));
    memcpy(king_move_distance_scores, params->king_move_distance_scores, sizeof(king_move_distance_scores));
}

/**
 * Save best parameters to file
 */
static void save_params_to_file(const RewardParams *params, int score, int iteration) {
    FILE *f = fopen(BEST_PARAMS_FILE, "w");
    if (!f) {
        fprintf(stderr, "Warning: Could not open %s for writing\n", BEST_PARAMS_FILE);
        return;
    }
    
    fprintf(f, "=== BEST PARAMETERS ===\n");
    fprintf(f, "Iteration: %d\n", iteration);
    fprintf(f, "Score: %d / %d puzzles\n\n", score, PUZZLE_TEST_COUNT);
    
    fprintf(f, "// Development and Piece Positioning\n");
    fprintf(f, "double development_penalty_per_move = %.17g;\n", params->development_penalty_per_move);
    fprintf(f, "double global_position_table_scale = %.17g;\n", params->global_position_table_scale);
    fprintf(f, "double knight_backstop_penalty = %.17g;\n", params->knight_backstop_penalty);
    fprintf(f, "double knight_edge_penalty = %.17g;\n", params->knight_edge_penalty);
    fprintf(f, "double slider_mobility_per_square = %.17g;\n", params->slider_mobility_per_square);
    
    fprintf(f, "\n// Pawn Evaluation\n");
    fprintf(f, "double undefended_central_pawn_penalty = %.17g;\n", params->undefended_central_pawn_penalty);
    fprintf(f, "double central_pawn_bonus = %.17g;\n", params->central_pawn_bonus);
    fprintf(f, "double pawn_promotion_immediate_bonus = %.17g;\n", params->pawn_promotion_immediate_bonus);
    fprintf(f, "double pawn_promotion_immediate_distance = %.17g;\n", params->pawn_promotion_immediate_distance);
    fprintf(f, "double pawn_promotion_delayed_bonus = %.17g;\n", params->pawn_promotion_delayed_bonus);
    fprintf(f, "double pawn_promotion_delayed_distance = %.17g;\n", params->pawn_promotion_delayed_distance);
    
    fprintf(f, "\n// King Safety and Castling\n");
    fprintf(f, "double king_hasmoved_penalty = %.17g;\n", params->king_hasmoved_penalty);
    fprintf(f, "double king_center_exposure_penalty = %.17g;\n", params->king_center_exposure_penalty);
    fprintf(f, "double castling_bonus = %.17g;\n", params->castling_bonus);
    fprintf(f, "double king_adjacent_attack_bonus = %.17g;\n", params->king_adjacent_attack_bonus);
    
    fprintf(f, "\n// Tactical (Pieces Under Attack)\n");
    fprintf(f, "double defended_piece_support_bonus = %.17g;\n", params->defended_piece_support_bonus);
    fprintf(f, "double defended_piece_weaker_penalty = %.17g;\n", params->defended_piece_weaker_penalty);
    fprintf(f, "double undefended_piece_penalty = %.17g;\n", params->undefended_piece_penalty);
    
    fprintf(f, "\n// Check and Stalemate\n");
    fprintf(f, "double check_penalty_white = %.17g;\n", params->check_penalty_white);
    fprintf(f, "double check_bonus_black = %.17g;\n", params->check_bonus_black);
    fprintf(f, "double stalemate_black_penalty = %.17g;\n", params->stalemate_black_penalty);
    fprintf(f, "double stalemate_white_penalty = %.17g;\n", params->stalemate_white_penalty);
    
    fprintf(f, "\n// Endgame King Island\n");
    fprintf(f, "double endgame_king_island_max_norm = %.17g;\n", params->endgame_king_island_max_norm);
    fprintf(f, "double endgame_king_island_bonus_scale = %.17g;\n", params->endgame_king_island_bonus_scale);
    
    fprintf(f, "\n// Search Pruning and Evaluation\n");
    fprintf(f, "double static_futility_prune_margin = %.17g;\n", params->static_futility_prune_margin);
    fprintf(f, "double checkmate_score = %.17g;\n", params->checkmate_score);
    fprintf(f, "double stalemate_score = %.17g;\n", params->stalemate_score);
    fprintf(f, "double draw_score = %.17g;\n", params->draw_score);
    
    fprintf(f, "\n// Piece-Square Tables\n");
    fprintf(f, "double pawn_pst[8][8] = {\n");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "    {");
        for (int j = 0; j < 8; j++) {
            fprintf(f, "%.17g", params->pawn_pst[i][j]);
            if (j < 7) fprintf(f, ", ");
        }
        fprintf(f, "}%s\n", i < 7 ? "," : "");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double knight_pst[8][8] = {\n");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "    {");
        for (int j = 0; j < 8; j++) {
            fprintf(f, "%.17g", params->knight_pst[i][j]);
            if (j < 7) fprintf(f, ", ");
        }
        fprintf(f, "}%s\n", i < 7 ? "," : "");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double bishop_pst[8][8] = {\n");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "    {");
        for (int j = 0; j < 8; j++) {
            fprintf(f, "%.17g", params->bishop_pst[i][j]);
            if (j < 7) fprintf(f, ", ");
        }
        fprintf(f, "}%s\n", i < 7 ? "," : "");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double rook_pst[8][8] = {\n");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "    {");
        for (int j = 0; j < 8; j++) {
            fprintf(f, "%.17g", params->rook_pst[i][j]);
            if (j < 7) fprintf(f, ", ");
        }
        fprintf(f, "}%s\n", i < 7 ? "," : "");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double queen_pst[8][8] = {\n");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "    {");
        for (int j = 0; j < 8; j++) {
            fprintf(f, "%.17g", params->queen_pst[i][j]);
            if (j < 7) fprintf(f, ", ");
        }
        fprintf(f, "}%s\n", i < 7 ? "," : "");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double king_pst_mg[8][8] = {\n");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "    {");
        for (int j = 0; j < 8; j++) {
            fprintf(f, "%.17g", params->king_pst_mg[i][j]);
            if (j < 7) fprintf(f, ", ");
        }
        fprintf(f, "}%s\n", i < 7 ? "," : "");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double king_pst_eg[8][8] = {\n");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "    {");
        for (int j = 0; j < 8; j++) {
            fprintf(f, "%.17g", params->king_pst_eg[i][j]);
            if (j < 7) fprintf(f, ", ");
        }
        fprintf(f, "}%s\n", i < 7 ? "," : "");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "\n// Move Offset Score Tables\n");
    fprintf(f, "double pawn_move_scores[4] = {");
    for (int i = 0; i < 4; i++) {
        fprintf(f, "%.17g", params->pawn_move_scores[i]);
        if (i < 3) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double knight_move_scores[8] = {");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "%.17g", params->knight_move_scores[i]);
        if (i < 7) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double bishop_move_scores[4] = {");
    for (int i = 0; i < 4; i++) {
        fprintf(f, "%.17g", params->bishop_move_scores[i]);
        if (i < 3) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double rook_move_scores[4] = {");
    for (int i = 0; i < 4; i++) {
        fprintf(f, "%.17g", params->rook_move_scores[i]);
        if (i < 3) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double queen_move_scores[8] = {");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "%.17g", params->queen_move_scores[i]);
        if (i < 7) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double king_move_scores[8] = {");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "%.17g", params->king_move_scores[i]);
        if (i < 7) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "\n// Move Distance Score Tables\n");
    fprintf(f, "double pawn_move_distance_scores[2] = {");
    for (int i = 0; i < 2; i++) {
        fprintf(f, "%.17g", params->pawn_move_distance_scores[i]);
        if (i < 1) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double knight_move_distance_scores[8] = {");
    for (int i = 0; i < 8; i++) {
        fprintf(f, "%.17g", params->knight_move_distance_scores[i]);
        if (i < 7) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double bishop_move_distance_scores[7] = {");
    for (int i = 0; i < 7; i++) {
        fprintf(f, "%.17g", params->bishop_move_distance_scores[i]);
        if (i < 6) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double rook_move_distance_scores[7] = {");
    for (int i = 0; i < 7; i++) {
        fprintf(f, "%.17g", params->rook_move_distance_scores[i]);
        if (i < 6) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double queen_move_distance_scores[7] = {");
    for (int i = 0; i < 7; i++) {
        fprintf(f, "%.17g", params->queen_move_distance_scores[i]);
        if (i < 6) fprintf(f, ", ");
    }
    fprintf(f, "};\n");
    
    fprintf(f, "double king_move_distance_scores[1] = {");
    fprintf(f, "%.17g", params->king_move_distance_scores[0]);
    fprintf(f, "};\n");
    
    fclose(f);
}

/* ======================== TUI Callback Infrastructure ======================== */

// Static variables for current training state (updated during puzzle testing)
static int training_current_iteration = 0;
static int training_current_best_score = 0;
static int training_best_iteration = 0;    // Which iteration had the best score
static double training_current_mutation_rate = 30.0;
static int training_current_is_new_record = 0;
static IterationHistory training_history_buffer[5] = {0};  // Static buffer for last 5 iterations
static IterationHistory *training_current_history = training_history_buffer;
static int training_current_history_count = 0;
static int training_current_puzzle = 0;  // Current puzzle number being tested
static int training_num_threads = 8;     // Number of threads for puzzle evaluation
static RewardParams training_best_params = {0};  // Best parameters found so far
static int training_search_depth = 4;    // Search depth for puzzle evaluation
static time_t training_start_time = 0;   // Start time for elapsed time tracking

// Getter function to access current puzzle number from other modules
int get_training_current_puzzle(void)
{
    return training_current_puzzle;
}

// Forward declaration for TUI update function
extern void tui_update_training_display(int iteration, int score, int best_score, int best_iteration, double mutation_rate, int is_new_record, int pass_count, IterationHistory *last_5, int history_count, const RewardParams *best_params, int elapsed_seconds, const RewardParams *top5_params, const int *top5_scores, int top5_count);

// Callback function for puzzle progress updates during training
static void training_puzzle_progress(int puzzles_completed, int total_puzzles, int current_score)
{
    training_current_puzzle = puzzles_completed;  // Update current puzzle number
    int elapsed_seconds = (int)(time(NULL) - training_start_time);
    // Update the display with live progress as puzzles complete
    tui_update_training_display(training_current_iteration, current_score, training_current_best_score, training_best_iteration,
                               training_current_mutation_rate, training_current_is_new_record, current_score, 
                               training_current_history, training_current_history_count, &training_best_params, elapsed_seconds,
                               training_top5.params, training_top5.scores, training_top5.count);
}

/**
 * Run puzzle test with current parameters using multithreading
 * Uses training_num_threads and training_search_depth for evaluation
 */
static int test_parameters(const RewardParams *params) {
    apply_params(params);
    suppress_engine_output = 1;  // Suppress output during testing
    
    // Set up callback for progress updates
    puzzle_progress_callback = training_puzzle_progress;
    
    // Use threaded version for speed (8x faster than non-threaded)
    int score = playPuzzles1To100_Threaded("lichess_db_puzzle.csv", training_search_depth, training_num_threads);
    
    // Clear callback
    puzzle_progress_callback = NULL;
    suppress_engine_output = 0;  // Re-enable output
    return score;
}

/* ======================== Main Training Loop ======================== */

/**
 * Run the training system
 * iterations: number of training iterations to run
 * search_depth: search depth for puzzle evaluation
 */
int train_rewards(int iterations, int search_depth) {
    
    // Set search depth for puzzle evaluation
    training_search_depth = search_depth;
    
    // Reconfigure TUI windows for training display
    tui_reconfigure_for_training();
    
    srand((unsigned int)time(NULL));
    training_start_time = time(NULL);
    
    RewardParams best_params, candidate_params, parent_params;
    int best_score = 0;
    
    // Initialize top 5 tracker (use global for access in callback)
    training_top5.count = 0;
    
    // Use static history buffer for last 5 iterations
    memset(training_history_buffer, 0, sizeof(training_history_buffer));
    int history_count = 0;
    
    // Initialize with baseline random parameters
    training_current_iteration = 0;
    training_current_best_score = 0;
    training_best_iteration = 0;
    training_current_mutation_rate = INITIAL_MUTATION_RATE;
    training_current_is_new_record = 0;
    training_current_history = training_history_buffer;
    training_current_history_count = 0;
    
    init_baseline_params(&best_params);
    best_score = test_parameters(&best_params);
    copy_params(&training_best_params, &best_params);
    
    // Add baseline to top 5
    update_top5(&training_top5, &best_params, best_score);
    
    // Add initial iteration to history
    training_history_buffer[0].iteration = 0;
    training_history_buffer[0].score = best_score;
    training_history_buffer[0].pass_count = best_score;
    history_count = 1;
    training_current_history_count = history_count;
    
    int elapsed_seconds = (int)(time(NULL) - training_start_time);
    tui_update_training_display(0, best_score, best_score, 0, INITIAL_MUTATION_RATE, 0, best_score, training_history_buffer, history_count, &best_params, elapsed_seconds, training_top5.params, training_top5.scores, training_top5.count);
    save_params_to_file(&best_params, best_score, 0);
    
    // Training loop
    for (int iter = 1; iter < iterations; iter++) {
        double mutation_rate = get_mutation_rate(iter, INITIAL_MUTATION_RATE, COOLING_SCHEDULE);
        int elapsed_seconds = (int)(time(NULL) - training_start_time);
        
        // Update static variables for callbacks
        training_current_iteration = iter;
        training_current_best_score = best_score;
        training_current_mutation_rate = mutation_rate;
        training_current_history = training_history_buffer;
        training_current_history_count = history_count;
        
        // Select random parent from top 5 best parameters
        select_random_from_top5(&parent_params, &training_top5);
        
        // Generate candidate parameters by mutating from randomly selected parent
        mutate_params(&candidate_params, &parent_params, mutation_rate);
        int candidate_score = test_parameters(&candidate_params);
        
        // Acceptance criterion: always accept if better, sometimes accept if worse (simulated annealing)
        // Always compare against the best score found (not the parent)
        int accept = 0;
        if (candidate_score > best_score) {
            accept = 1;  // Better - always accept
        } else {
            // Worse than best - accept with decreasing probability based on temperature (simulated annealing)
            double temperature = (double)iter / iterations;  // Decreasing temperature
            double acceptance_prob = exp(-((double)(best_score - candidate_score) / (temperature * 10.0 + 1.0)));
            accept = (rand() / (double)RAND_MAX) < acceptance_prob;
        }
        
        // Add to history (keep last 5)
        if (history_count < 5) {
            training_history_buffer[history_count].iteration = iter;
            training_history_buffer[history_count].score = candidate_score;
            training_history_buffer[history_count].pass_count = candidate_score;
            history_count++;
        } else {
            // Shift history and add new entry
            for (int i = 0; i < 4; i++) {
                training_history_buffer[i] = training_history_buffer[i + 1];
            }
            training_history_buffer[4].iteration = iter;
            training_history_buffer[4].score = candidate_score;
            training_history_buffer[4].pass_count = candidate_score;
        }
        
        if (accept) {
            // Update best if this candidate is better
            if (candidate_score > best_score) {
                copy_params(&best_params, &candidate_params);
                copy_params(&training_best_params, &candidate_params);
                best_score = candidate_score;
                training_best_iteration = iter;
                training_current_is_new_record = 1;
                
                tui_update_training_display(iter, best_score, best_score, iter, mutation_rate, 1, candidate_score, training_history_buffer, history_count, &training_best_params, elapsed_seconds, training_top5.params, training_top5.scores, training_top5.count);
                save_params_to_file(&best_params, best_score, iter);
            } else {
                // Simulated annealing: accept worse solution temporarily (but don't update best)
                training_current_is_new_record = 0;
                tui_update_training_display(iter, candidate_score, best_score, training_best_iteration, mutation_rate, 0, candidate_score, training_history_buffer, history_count, &training_best_params, elapsed_seconds, training_top5.params, training_top5.scores, training_top5.count);
            }
        } else {
            training_current_is_new_record = 0;
            tui_update_training_display(iter, candidate_score, best_score, training_best_iteration, mutation_rate, 0, candidate_score, training_history_buffer, history_count, &training_best_params, elapsed_seconds, training_top5.params, training_top5.scores, training_top5.count);
        }
        
        // Add to top 5 regardless of acceptance (track all good candidates, not just accepted ones)
        update_top5(&training_top5, &candidate_params, candidate_score);
    }
    
    closePuzzleFileCache();  // Clean up file cache
    return best_score;
}

/**
 * Run training with specified number of threads
 * iterations: number of training iterations
 * num_threads: number of threads for parallel puzzle evaluation
 * search_depth: search depth for puzzle evaluation
 */
int train_rewards_threaded(int iterations, int num_threads, int search_depth)
{
    // Set thread count for puzzle evaluation
    training_num_threads = num_threads;
    if (training_num_threads < 1) training_num_threads = 1;
    if (training_num_threads > 256) training_num_threads = 256;
    
    // Run training with configured thread count and search depth
    return train_rewards(iterations, search_depth);
}
