/* evaluation.c - AI score computation (evaluation functions) */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include "chess.h"

// Helper: check if square (x,y) is defended by any piece of given colour
static int isSquareDefended(struct Piece board[8][8], int x, int y, enum Colour colour)
{
    for (int ax = 0; ax < 8; ax++) {
        for (int ay = 0; ay < 8; ay++) {
            if (ax == x && ay == y) continue;
            if (board[ax][ay].type == -1) continue;
            if (board[ax][ay].colour != colour) continue;
            struct Piece mover = board[ax][ay];
            int dx = x - ax;
            int dy = y - ay;

            switch (mover.type) {
                case PAWN:
                    if (mover.colour == WHITE) {
                        if (dy == 1 && (dx == 1 || dx == -1)) return 1;
                    } else {
                        if (dy == -1 && (dx == 1 || dx == -1)) return 1;
                    }
                    break;
                case KNIGHT: {
                    int kx = abs(dx), ky = abs(dy);
                    if ((kx == 1 && ky == 2) || (kx == 2 && ky == 1)) return 1;
                    break;
                }
                case BISHOP: {
                    if (abs(dx) == abs(dy) && dx != 0) {
                        int sx = (dx > 0) ? 1 : -1;
                        int sy = (dy > 0) ? 1 : -1;
                        int cx = ax + sx, cy = ay + sy;
                        int blocked = 0;
                        while (cx != x && cy != y) {
                            if (board[cx][cy].type != -1) { blocked = 1; break; }
                            cx += sx; cy += sy;
                        }
                        if (!blocked) return 1;
                    }
                    break;
                }
                case ROOK: {
                    if ((dx == 0 && dy != 0) || (dy == 0 && dx != 0)) {
                        int sx = (dx == 0) ? 0 : (dx > 0 ? 1 : -1);
                        int sy = (dy == 0) ? 0 : (dy > 0 ? 1 : -1);
                        int cx = ax + sx, cy = ay + sy;
                        int blocked = 0;
                        while (cx != x || cy != y) {
                            if (board[cx][cy].type != -1) { blocked = 1; break; }
                            cx += sx; cy += sy;
                        }
                        if (!blocked) return 1;
                    }
                    break;
                }
                case QUEEN: {
                    if (abs(dx) == abs(dy) && dx != 0) {
                        int sx = (dx > 0) ? 1 : -1;
                        int sy = (dy > 0) ? 1 : -1;
                        int cx = ax + sx, cy = ay + sy;
                        int blocked = 0;
                        while (cx != x && cy != y) {
                            if (board[cx][cy].type != -1) { blocked = 1; break; }
                            cx += sx; cy += sy;
                        }
                        if (!blocked) return 1;
                    } else if ((dx == 0 && dy != 0) || (dy == 0 && dx != 0)) {
                        int sx = (dx == 0) ? 0 : (dx > 0 ? 1 : -1);
                        int sy = (dy == 0) ? 0 : (dy > 0 ? 1 : -1);
                        int cx = ax + sx, cy = ay + sy;
                        int blocked = 0;
                        while (cx != x || cy != y) {
                            if (board[cx][cy].type != -1) { blocked = 1; break; }
                            cx += sx; cy += sy;
                        }
                        if (!blocked) return 1;
                    }
                    break;
                }
                case KING: {
                    if (abs(dx) <= 1 && abs(dy) <= 1) return 1;
                    break;
                }
                default:
                    break;
            }
        }
    }
    return 0;
}

// Returns bonus score for moves that advance pieces towards opponent's king
int evaluateEndgameAdvancement(struct Piece board[8][8], int fromX, int fromY, int toX, int toY, enum Colour colour)
{
    if (!isInEndgame(board))
    {
        return 0; // Not in endgame, no bonus
    }

    // Find enemy king position
    int enemyKingX = -1, enemyKingY = -1;
    enum Colour enemyColour = (colour == WHITE) ? BLACK : WHITE;

    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (board[i][j].type == KING && board[i][j].colour == enemyColour)
            {
                enemyKingX = i;
                enemyKingY = j;
                break;
            }
        }
        if (enemyKingX != -1)
            break;
    }

    if (enemyKingX == -1)
        return 0; // No enemy king found

    struct Piece movingPiece = board[fromX][fromY];
    if (movingPiece.type == PAWN || movingPiece.type == KING)
    {
        return 0; // Don't give bonus for pawns or king
    }

    int distanceBefore = squareDistance(fromX, fromY, enemyKingX, enemyKingY);
    int distanceAfter = squareDistance(toX, toY, enemyKingX, enemyKingY);

    // If move brings piece closer to enemy king, give bonus
    if (distanceAfter < distanceBefore)
    {
        // Check if the piece would be captured in the new position
        struct Piece tempBoard[8][8];
        for (int i = 0; i < 8; i++)
        {
            for (int j = 0; j < 8; j++)
            {
                tempBoard[i][j] = board[i][j];
            }
        }
        tempBoard[toX][toY] = tempBoard[fromX][fromY];
        tempBoard[fromX][fromY].type = -1;
        tempBoard[fromX][fromY].colour = -1;

        // Check if the piece can be captured at the new position
        if (canBeCaptured(tempBoard, toX, toY))
        {
            return 0; // Don't give bonus if piece would be captured
        }

        // Bonus scales with piece value and distance reduction
        // Closer proximity gets higher bonus
        int bonus = (distanceBefore - distanceAfter) * (5 - distanceAfter) * 0.5;
        return bonus;
    }

    return 0;
}


int evaluateBoardPosition(struct Piece board[8][8])
{
    // Transposition table lookup: avoid recomputing positions we've already evaluated
    // Simple direct-mapped table with 64K entries
    const int TT_BITS = 16;
    const size_t TT_SIZE = (1u << TT_BITS);

    // Compute board hash
    uint64_t key = 14695981039346656037ULL; // FNV-1a offset
    const uint64_t FNV_PRIME = 1099511628211ULL;
    for (int x = 0; x < 8; x++)
    {
        for (int y = 0; y < 8; y++)
        {
            int t = (board[x][y].type >= 0) ? (board[x][y].type + 1) : 0; // 0 = empty
            int c = (board[x][y].colour == WHITE) ? 1 : (board[x][y].colour == BLACK) ? 2 : 0;
            uint8_t bytes[3];
            bytes[0] = (uint8_t)t;
            bytes[1] = (uint8_t)c;
            bytes[2] = (uint8_t)(board[x][y].hasMoved ? 1 : 0);
            for (int b = 0; b < 3; b++)
            {
                key ^= (uint64_t)bytes[b];
                key *= FNV_PRIME;
            }
        }
    }

    // Simple direct-mapped transposition table stored as static inside this function
    typedef struct { uint64_t key; int score; } TTEntry;
    static TTEntry *tt = NULL;
    if (!tt)
    {
        tt = calloc(TT_SIZE, sizeof(TTEntry));
    }
    size_t idx = (size_t)(key & (TT_SIZE - 1));
    if (tt[idx].key == key && tt[idx].key != 0)
    {
        /* Transposition table hit */
        ttHitCount++;
        return tt[idx].score;
    }

        int score = 0;

        /* Count this as a unique evaluated position (cache miss) */
        static unsigned long long localEvalCount = 0ULL;
        localEvalCount++;
        evalCount = localEvalCount;

    // [Tuning] - Piece values
    int pieceValue[] = {100, 300, 300, 500, 900, 20000};

    // [Tuning] - Global positional table
    int globalTable[8][8] = {
        {0, 0, 0, 0, 0, 0, 0, 0},
        {0, 1, 1, 1, 1, 1, 1, 0},
        {0, 1, 2, 2, 2, 2, 1, 0},
        {0, 1, 2, 3, 3, 2, 1, 0},
        {0, 1, 2, 3, 3, 2, 1, 0},
        {0, 1, 2, 2, 2, 2, 1, 0},
        {0, 1, 1, 1, 1, 1, 1, 0},
        {0, 0, 0, 0, 0, 0, 0, 0}};

    int whiteAttacks[8][8] = {0};
    int blackAttacks[8][8] = {0};

    int whiteKingX = -1, whiteKingY = -1, blackKingX = -1, blackKingY = -1;

    // First pass: score pieces, mark attacks, find kings
    for (int x = 0; x < 8; x++)
    {
        for (int y = 0; y < 8; y++)
        {
            struct Piece p = board[x][y];
            if (p.type == -1)
                continue;

            int sign = (p.colour == WHITE) ? 1 : -1;
            int (*attackMap)[8]; // pointer to array of 8 ints
            if (p.colour == WHITE)
                attackMap = whiteAttacks;
            else
                attackMap = blackAttacks;

            // Track king positions
            if (p.type == KING)
            {
                if (p.colour == WHITE)
                {
                    whiteKingX = x;
                    whiteKingY = y;
                }
                else
                {
                    blackKingX = x;
                    blackKingY = y;
                }
            }

            // Penalize pieces that are still on their starting squares.
            // Penalty scales with number of moves played (boardHistoryCount).
            {
                int movesPlayed = boardHistoryCount; /* number of recorded positions / moves */
                int penaltyPerPiece = 3 * movesPlayed ;
                int atStart = 0;
                if (p.hasMoved == 0)
                {
                    if (p.colour == WHITE)
                    {
                        if (p.type == PAWN && y == 1)
                            atStart = 1;
                        if (p.type == ROOK && (x == 0 || x == 7) && y == 0)
                            atStart = 1;
                        if (p.type == KNIGHT && (x == 1 || x == 6) && y == 0)
                            atStart = 1;
                        if (p.type == BISHOP && (x == 2 || x == 5) && y == 0)
                            atStart = 1;
                        if (p.type == QUEEN && x == 3 && y == 0)
                            atStart = 1;
                        if (p.type == KING && x == 4 && y == 0)
                            atStart = 1;
                    }
                    else // BLACK 
                    {
                        if (p.type == PAWN && y == 6)
                            atStart = 1;
                        if (p.type == ROOK && (x == 0 || x == 7) && y == 7)
                            atStart = 1;
                        if (p.type == KNIGHT && (x == 1 || x == 6) && y == 7)
                            atStart = 1;
                        if (p.type == BISHOP && (x == 2 || x == 5) && y == 7)
                            atStart = 1;
                        if (p.type == QUEEN && x == 3 && y == 7)
                            atStart = 1;
                        if (p.type == KING && x == 4 && y == 7)
                            atStart = 1;
                    }
                }
                if (atStart && penaltyPerPiece > 0)
                {
                    score -= sign * penaltyPerPiece; // reduce side's score for lack of development
                }
            }

            // --- Base piece value ---
            score += sign * pieceValue[p.type];

            // -[Tuning] - Global positional table multiplier
            score += sign * globalTable[x][y] * 10;

            // Pawn Special Scoring
            if (p.type == PAWN)
            {
                // Check if pawn is defended by any friendly piece (not just pawns)
                int defended = 0;
                if ((x == 3 || x == 4) && (y == 3 || y == 4))
                {
                    if (isSquareDefended(board, x, y, p.colour))
                        defended = 1;

                    // Penalize undefended central pawns slightly; reward central control
                    if (!defended)
                        score -= sign * 20; // reduced penalty

                    // Bonus for central pawn presence
                    score += sign * 40;
                }

                // Promotion distance bonus (distance to promotion rank)
                int promotionDistance;
                if (p.colour == WHITE)
                {
                    promotionDistance = 7 - y; // Distance to rank 8 (y=7)
                }
                else
                {
                    promotionDistance = y; // Distance to rank 1 (y=0)
                }
                
                // Give exponential bonus as pawn approaches promotion
                if (promotionDistance <= 2)
                    score += sign * 300 * (2 - promotionDistance);
                else if (promotionDistance <= 4)
                    score += sign * 80 * (4 - promotionDistance);

                // Pawn attacks (diagonal squares in front of the pawn)
                if (p.colour == WHITE)
                {
                    // White pawns attack upward-diagonally
                    if (x > 0 && y < 7)
                        attackMap[x - 1][y + 1] = 1;
                    if (x < 7 && y < 7)
                        attackMap[x + 1][y + 1] = 1;
                }
                else
                {
                    // Black pawns attack downward-diagonally
                    if (x > 0 && y > 0)
                        attackMap[x - 1][y - 1] = 1;
                    if (x < 7 && y > 0)
                        attackMap[x + 1][y - 1] = 1;
                }
            }

            // Knight Special Scoring
            if (p.type == KNIGHT)
            {
                if ((p.colour == WHITE && x == 0 && y >= 2 && y <= 5) ||
                    (p.colour == BLACK && x == 7 && y >= 2 && y <= 5))
                    score -= sign * 40;
                if (x == 0 || x == 7)
                    score -= sign * 30;

                int moves[8][2] = {{2, 1}, {1, 2}, {-1, 2}, {-2, 1}, {-2, -1}, {-1, -2}, {1, -2}, {2, -1}};
                for (int k = 0; k < 8; k++)
                {
                    int nx = x + moves[k][0], ny = y + moves[k][1];
                    if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8)
                        attackMap[nx][ny] = 1;
                }
            }

            // Sliding Pieces Special Scoring
            if (p.type == BISHOP || p.type == ROOK || p.type == QUEEN)
            {
                int dirs[8][2];
                int dirCount = 0;

                if (p.type == BISHOP || p.type == QUEEN)
                {
                    int bishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
                    for (int d = 0; d < 4; d++)
                    {
                        dirs[dirCount][0] = bishopDirs[d][0];
                        dirs[dirCount][1] = bishopDirs[d][1];
                        dirCount++;
                    }
                }
                if (p.type == ROOK || p.type == QUEEN)
                {
                    int rookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (int d = 0; d < 4; d++)
                    {
                        dirs[dirCount][0] = rookDirs[d][0];
                        dirs[dirCount][1] = rookDirs[d][1];
                        dirCount++;
                    }
                }

                for (int d = 0; d < dirCount; d++)
                {
                    int cx = x + dirs[d][0];
                    int cy = y + dirs[d][1];
                    int mobility = 0;
                    while (cx >= 0 && cx < 8 && cy >= 0 && cy < 8)
                    {
                        mobility++;
                        attackMap[cx][cy] = 1;
                        if (board[cx][cy].type != -1)
                            break;
                        cx += dirs[d][0];
                        cy += dirs[d][1];
                    }
                    score += sign * mobility * 5;
                }
            }

            // King Special Scoring
            if (p.type == KING)
            {
                if (p.hasMoved)
                    score += (p.colour == WHITE) ? -100 : 100;
                if ((x == 3 || x == 4) && y >= 2 && y <= 5)
                    score += (p.colour == WHITE) ? -30 : 30;

                for (int kx = -1; kx <= 1; kx++)
                {
                    for (int ky = -1; ky <= 1; ky++)
                    {
                        if (kx == 0 && ky == 0)
                            continue;
                        int nx = x + kx, ny = y + ky;
                        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8)
                            attackMap[nx][ny] = 1;
                    }
                }
            }
        }
    }

    // Reward control of squares around enemy king
    int kingAdj[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int kingSquaresBonus = 20;

    // Evaluate tactical liability for pieces under attack in a color-agnostic way.
    // For each piece, find minimum attacker value and maximum defender value.
    // If defenders can trade for equal-or-higher value (maxDef >= minAtt) give a
    // positive bonus (the piece is tactically supported). Otherwise apply a
    // small penalty if defended by weaker pieces, or a large penalty if undefended.
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            if (board[x][y].type == -1) continue;
            struct Piece targ = board[x][y];
            int targVal = pieceValue[targ.type];
            int minAttackerVal = 999999;
            int maxDefenderVal = -1;
            int hasAttacker = 0;
            int hasDefender = 0;

            // Scan the board for attackers and defenders
            for (int ax = 0; ax < 8; ax++) {
                for (int ay = 0; ay < 8; ay++) {
                    if (ax == x && ay == y) continue;
                    if (board[ax][ay].type == -1) continue;
                    struct Piece mover = board[ax][ay];
                    // Determine if mover can attack (x,y) by piece movement rules
                    int attacks = 0;
                    int dx = x - ax;
                    int dy = y - ay;

                    switch (mover.type) {
                        case PAWN:
                            if (mover.colour == WHITE) {
                                if (dy == 1 && (dx == 1 || dx == -1)) attacks = 1;
                            } else {
                                if (dy == -1 && (dx == 1 || dx == -1)) attacks = 1;
                            }
                            break;
                        case KNIGHT: {
                            int kx = abs(dx), ky = abs(dy);
                            if ((kx == 1 && ky == 2) || (kx == 2 && ky == 1)) attacks = 1;
                            break;
                        }
                        case BISHOP: {
                            if (abs(dx) == abs(dy) && dx != 0) {
                                int sx = (dx > 0) ? 1 : -1;
                                int sy = (dy > 0) ? 1 : -1;
                                int cx = ax + sx, cy = ay + sy;
                                int blocked = 0;
                                while (cx != x && cy != y) {
                                    if (board[cx][cy].type != -1) { blocked = 1; break; }
                                    cx += sx; cy += sy;
                                }
                                if (!blocked) attacks = 1;
                            }
                            break;
                        }
                        case ROOK: {
                            if ((dx == 0 && dy != 0) || (dy == 0 && dx != 0)) {
                                int sx = (dx == 0) ? 0 : (dx > 0 ? 1 : -1);
                                int sy = (dy == 0) ? 0 : (dy > 0 ? 1 : -1);
                                int cx = ax + sx, cy = ay + sy;
                                int blocked = 0;
                                while (cx != x || cy != y) {
                                    if (board[cx][cy].type != -1) { blocked = 1; break; }
                                    cx += sx; cy += sy;
                                }
                                if (!blocked) attacks = 1;
                            }
                            break;
                        }
                        case QUEEN: {
                            if (abs(dx) == abs(dy) && dx != 0) {
                                int sx = (dx > 0) ? 1 : -1;
                                int sy = (dy > 0) ? 1 : -1;
                                int cx = ax + sx, cy = ay + sy;
                                int blocked = 0;
                                while (cx != x && cy != y) {
                                    if (board[cx][cy].type != -1) { blocked = 1; break; }
                                    cx += sx; cy += sy;
                                }
                                if (!blocked) attacks = 1;
                            } else if ((dx == 0 && dy != 0) || (dy == 0 && dx != 0)) {
                                int sx = (dx == 0) ? 0 : (dx > 0 ? 1 : -1);
                                int sy = (dy == 0) ? 0 : (dy > 0 ? 1 : -1);
                                int cx = ax + sx, cy = ay + sy;
                                int blocked = 0;
                                while (cx != x || cy != y) {
                                    if (board[cx][cy].type != -1) { blocked = 1; break; }
                                    cx += sx; cy += sy;
                                }
                                if (!blocked) attacks = 1;
                            }
                            break;
                        }
                        case KING: {
                            if (abs(dx) <= 1 && abs(dy) <= 1) attacks = 1;
                            break;
                        }
                        default:
                            break;
                    }

                    if (attacks) {
                        if (mover.colour != targ.colour) {
                            hasAttacker = 1;
                            int mvVal = pieceValue[mover.type];
                            if (mvVal < minAttackerVal) minAttackerVal = mvVal;
                        } else {
                            hasDefender = 1;
                            int mvVal = pieceValue[mover.type];
                            if (mvVal > maxDefenderVal) maxDefenderVal = mvVal;
                        }
                    }
                }
            }

            if (!hasAttacker) continue; // not under attack

            int small_penalty = 10;
            int large_penalty = 70;
            int support_bonus = 120;

            int sign = (targ.colour == WHITE) ? 1 : -1;

            if (hasDefender && maxDefenderVal >= minAttackerVal) {
                // Defender can trade equal or better -> tactical strength
                score += sign * support_bonus;
            } else if (hasDefender) {
                // Defended but by weaker piece -> small penalty
                score -= sign * small_penalty;
            } else {
                // Undefended -> large penalty
                score -= sign * large_penalty;
            }
        }
    }

    if (whiteKingX != -1 && whiteKingY != -1)
    {
        for (int d = 0; d < 8; d++)
        {
            int tx = whiteKingX + kingAdj[d][0];
            int ty = whiteKingY + kingAdj[d][1];
            if (tx < 0 || tx > 7 || ty < 0 || ty > 7)
                continue;
            if (blackAttacks[tx][ty])
                score += kingSquaresBonus;
        }
    }
    if (blackKingX != -1 && blackKingY != -1)
    {
        for (int d = 0; d < 8; d++)
        {
            int tx = blackKingX + kingAdj[d][0];
            int ty = blackKingY + kingAdj[d][1];
            if (tx < 0 || tx > 7 || ty < 0 || ty > 7)
                continue;
            if (whiteAttacks[tx][ty])
                score -= kingSquaresBonus;
        }

        // Castling bonus: give a flat bonus when a side has castled (king/rook in castled positions)
        // White kingside: king at g1 (6,0) and rook at f1 (5,0)
        if (whiteKingX == 6 && whiteKingY == 0 && board[5][0].type == ROOK && board[5][0].colour == WHITE)
        {
            score += 50;
        }
        // White queenside: king at c1 (2,0) and rook at d1 (3,0)
        if (whiteKingX == 2 && whiteKingY == 0 && board[3][0].type == ROOK && board[3][0].colour == WHITE)
        {
            score += 50;
        }
        // Black kingside: king at g8 (6,7) and rook at f8 (5,7)
        if (blackKingX == 6 && blackKingY == 7 && board[5][7].type == ROOK && board[5][7].colour == BLACK)
        {
            score -= 50;
        }
        // Black queenside: king at c8 (2,7) and rook at d8 (3,7)
        if (blackKingX == 2 && blackKingY == 7 && board[3][7].type == ROOK && board[3][7].colour == BLACK)
        {
            score -= 50;
        }
    }

    // Endgame: reward reducing the opponent king's "island" (flood-fill of reachable safe squares)
    if (isInEndgame(board))
    {
        int whiteIsland = 0;
        int blackIsland = 0;

        // Flood-fill helper arrays (max 64 squares)
        int qx[64], qy[64];
        int visited[8][8];

        // Compute White king island (safe squares reachable by king without stepping into enemy attacks or adjacent to enemy king)
        if (whiteKingX != -1 && whiteKingY != -1)
        {
            memset(visited, 0, sizeof(visited));
            int head = 0, tail = 0;
            // Start from the king's current square (even if currently attacked, we want reachable safe squares)
            visited[whiteKingX][whiteKingY] = 1;
            qx[tail] = whiteKingX; qy[tail] = whiteKingY; tail++;

            while (head < tail)
            {
                int cx = qx[head];
                int cy = qy[head];
                head++;
                // Only count this square if it's a legal destination for the king (empty or capturable)
                if (board[cx][cy].type == -1 || board[cx][cy].colour == BLACK)
                {
                    // Square must not be attacked by the enemy to be considered safe
                    if (!blackAttacks[cx][cy])
                    {
                        // Also avoid squares adjacent to the enemy king (illegal proximity)
                        if (!(blackKingX != -1 && abs(cx - blackKingX) <= 1 && abs(cy - blackKingY) <= 1))
                        {
                            whiteIsland++;
                        }
                    }
                }

                // Expand to neighboring king moves regardless of whether current square was safe,
                // because king can move from an attacked square into a safe one.
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        if (dx == 0 && dy == 0) continue;
                        int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
                        if (visited[nx][ny]) continue;
                        // Destination square must be empty or contain an enemy piece (capturable)
                        if (board[nx][ny].type == -1 || board[nx][ny].colour == BLACK)
                        {
                            // Avoid squares adjacent to enemy king
                            if (blackKingX != -1 && abs(nx - blackKingX) <= 1 && abs(ny - blackKingY) <= 1)
                                continue;
                            // We don't require the square to be safe to enqueue; safety will be checked when counting
                            visited[nx][ny] = 1;
                            qx[tail] = nx; qy[tail] = ny; tail++;
                        }
                    }
                }
            }
        }

        // Compute Black king island similarly
        if (blackKingX != -1 && blackKingY != -1)
        {
            memset(visited, 0, sizeof(visited));
            int head = 0, tail = 0;
            visited[blackKingX][blackKingY] = 1;
            qx[tail] = blackKingX; qy[tail] = blackKingY; tail++;

            while (head < tail)
            {
                int cx = qx[head];
                int cy = qy[head];
                head++;
                if (board[cx][cy].type == -1 || board[cx][cy].colour == WHITE)
                {
                    if (!whiteAttacks[cx][cy])
                    {
                        if (!(whiteKingX != -1 && abs(cx - whiteKingX) <= 1 && abs(cy - whiteKingY) <= 1))
                        {
                            blackIsland++;
                        }
                    }
                }

                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        if (dx == 0 && dy == 0) continue;
                        int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
                        if (visited[nx][ny]) continue;
                        if (board[nx][ny].type == -1 || board[nx][ny].colour == WHITE)
                        {
                            if (whiteKingX != -1 && abs(nx - whiteKingX) <= 1 && abs(ny - whiteKingY) <= 1)
                                continue;
                            visited[nx][ny] = 1;
                            qx[tail] = nx; qy[tail] = ny; tail++;
                        }
                    }
                }
            }
        }

        const int MAX_ISLAND_NORM = 16; // Normalization cap for island size
        const int islandBonusScale = 4;

        int blackBonus = (MAX_ISLAND_NORM - blackIsland) > 0 ? (MAX_ISLAND_NORM - blackIsland) * islandBonusScale : 0;
        int whiteBonus = (MAX_ISLAND_NORM - whiteIsland) > 0 ? (MAX_ISLAND_NORM - whiteIsland) * islandBonusScale : 0;

        // Smaller black island => advantage to White
        score += blackBonus;
        // Smaller white island => advantage to Black
        score -= whiteBonus;
    }

    // Check bonuses/penalties
    if (isInCheck(board, WHITE))
        score -= 100;
    if (isInCheck(board, BLACK))
        score += 100;

    // Stalemate handling
    if (isStalemate(board, BLACK) && score > 0)
    {
        score = -500;
    }
    if (isStalemate(board, WHITE) && score < 0)
    {
        score = 500;
    }

    // Store into transposition table
    if (tt)
    {
        tt[idx].key = key ? key : 1; // avoid zero key meaning empty
        tt[idx].score = score;
    }

    return score;
}

/* Global counter for unique evaluations (defined here) */
unsigned long long evalCount = 0ULL;
/* Transposition table hits */
unsigned long long ttHitCount = 0ULL;
/* Alpha-beta prune count (incremented from recursion.c) */
unsigned long long abPruneCount = 0ULL;
/* Static-futility prune count (incremented from recursion.c) */
unsigned long long staticPruneCount = 0ULL;

void printEvaluationCount(void)
{
    //printf("Evaluated unique board positions: %llu\n", evalCount);
    //printf("Transposition-table hits (lookup): %llu\n", ttHitCount);
    //printf("Alpha-beta prunes: %llu\n", abPruneCount);
    //("Static-futility prunes (shallow): %llu\n", staticPruneCount);
}


// Main move ranking function that executes best move and prints sequence
int moveRanking(struct Piece currentBoard[8][8], int maxRecursiveDepth, enum Colour aiColour)
{
    // Check for one-move checkmate first - Always checkmate if available
    if (checkAndExecuteOneMoveMate(currentBoard, aiColour))
    {
        return 999999999;
    }

    // Reset per-search evaluation counters (we want per-search stats)
    evalCount = 0ULL;
    ttHitCount = 0ULL;
    abPruneCount = 0ULL;
    staticPruneCount = 0ULL;

    struct timespec tstart, tend;
    clock_gettime(CLOCK_MONOTONIC, &tstart);

    struct MoveSequence bestSequence = moveRankingRecursiveWithSequence(currentBoard, 0, maxRecursiveDepth, aiColour, -999999999, 999999999);

    clock_gettime(CLOCK_MONOTONIC, &tend);
    double elapsed = (double)(tend.tv_sec - tstart.tv_sec) + (double)(tend.tv_nsec - tstart.tv_nsec) / 1e9;

    if (bestSequence.count == 0)
    {
        printf("No valid moves available.\n");
        return 0;
    }

    // Build the predicted move sequence string for TUI
    char predicted[512] = "";
    enum Colour currentPlayer = aiColour;
    for (int i = 0; i < bestSequence.count; i++)
    {
        char moveNotation[16];
        snprintf(moveNotation, sizeof(moveNotation), "%c%d%c%d",
                 'a' + bestSequence.moves[i].fromX, bestSequence.moves[i].fromY + 1,
                 'a' + bestSequence.moves[i].toX, bestSequence.moves[i].toY + 1);
        
        char temp[64];
        snprintf(temp, sizeof(temp), "%d. %s (%s)  ", 
                 i + 1, moveNotation, currentPlayer == WHITE ? "White" : "Black");
        strncat(predicted, temp, sizeof(predicted) - strlen(predicted) - 1);
        
        currentPlayer = (currentPlayer == WHITE) ? BLACK : WHITE;
    }
    
    // Update TUI with predicted sequence
    tui_set_predicted_sequence(predicted);

    // Execute the first move on the global board
    if (bestSequence.count > 0)
    {
        int bestFromX = bestSequence.moves[0].fromX;
        int bestFromY = bestSequence.moves[0].fromY;
        int bestToX = bestSequence.moves[0].toX;
        int bestToY = bestSequence.moves[0].toY;

        char notation[16];
        snprintf(notation, sizeof(notation), "%c%d%c%d",
                 'a' + bestFromX, bestFromY + 1,
                 'a' + bestToX, bestToY + 1);
        printf("%s plays: %s\n", aiColour == WHITE ? "White (AI)" : "Black (AI)", notation);

        // Check if this is a capture move (before we overwrite the destination)
        int whiteIsCapture = (board[bestToX][bestToY].type != -1);

        board[bestToX][bestToY] = board[bestFromX][bestFromY];
        board[bestFromX][bestFromY].type = -1;
        board[bestFromX][bestFromY].colour = -1;
        board[bestToX][bestToY].hasMoved = 1;

        // Handle pawn promotion
        promotePawn(board, bestToX, bestToY);

        // Handle castling - move the rook
        if (board[bestToX][bestToY].type == KING && bestFromX == 4)
        {
            if (bestToX == 6)
            {
                board[5][bestToY] = board[7][bestToY];
                board[7][bestToY].type = -1;
                board[7][bestToY].colour = -1;
                board[5][bestToY].hasMoved = 1;
            }
            else if (bestToX == 2)
            {
                board[3][bestToY] = board[0][bestToY];
                board[0][bestToY].type = -1;
                board[0][bestToY].colour = -1;
                board[3][bestToY].hasMoved = 1;
            }
        }

        // Update halfmove clock for 50-move rule
        if (board[bestToX][bestToY].type == PAWN || whiteIsCapture)
        {
            halfmoveClock = 0;
        }
        else
        {
            halfmoveClock++;
        }

        // Record board state for threefold repetition detection
        recordBoardHistory();

        // Track the last move for en passant detection
        lastMove.fromX = bestFromX;
        lastMove.fromY = bestFromY;
        lastMove.toX = bestToX;
        lastMove.toY = bestToY;
    
        // Update TUI with stats and add move to history
        tui_update_stats(elapsed, evalCount, ttHitCount, abPruneCount, staticPruneCount, bestSequence.score);
        tui_add_move(notation);
        
        // Validate against puzzle if active
        tui_validate_puzzle_move(notation);
    }

    return bestSequence.score;
}
