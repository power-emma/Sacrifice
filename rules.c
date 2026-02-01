// rules.c - board setup and move validation rules
#include <stdio.h>
#include <string.h>
#include "chess.h"

// Global state
struct Move lastMove = {-1, -1, -1, -1};
struct Piece boardHistory[200][8][8];
int boardHistoryCount = 0;
int halfmoveClock = 0;

// Sets up the chess board with pieces in starting positions
int boardSetup()
{
    // Initialize empty squares
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            board[i][j].type = -1;
            board[i][j].colour = -1;
            board[i][j].hasMoved = 0;
        }
    }
    // Pawns
    for (int i = 0; i < 8; i++)
    {
        board[i][1].type = PAWN;
        board[i][1].colour = WHITE;
        board[i][6].type = PAWN;
        board[i][6].colour = BLACK;
    }
    // Rooks
    board[0][0].type = ROOK;
    board[0][0].colour = WHITE;
    board[7][0].type = ROOK;
    board[7][0].colour = WHITE;
    board[0][7].type = ROOK;
    board[0][7].colour = BLACK;
    board[7][7].type = ROOK;
    board[7][7].colour = BLACK;

    // Knights
    board[1][0].type = KNIGHT;
    board[1][0].colour = WHITE;
    board[6][0].type = KNIGHT;
    board[6][0].colour = WHITE;
    board[1][7].type = KNIGHT;
    board[1][7].colour = BLACK;
    board[6][7].type = KNIGHT;
    board[6][7].colour = BLACK;

    // Bishops
    board[2][0].type = BISHOP;
    board[2][0].colour = WHITE;
    board[5][0].type = BISHOP;
    board[5][0].colour = WHITE;
    board[2][7].type = BISHOP;
    board[2][7].colour = BLACK;
    board[5][7].type = BISHOP;
    board[5][7].colour = BLACK;

    // Royals
    board[3][0].type = QUEEN;
    board[3][0].colour = WHITE;
    board[4][0].type = KING;
    board[4][0].colour = WHITE;
    board[3][7].type = QUEEN;
    board[3][7].colour = BLACK;
    board[4][7].type = KING;
    board[4][7].colour = BLACK;
}

struct MoveList validMoves(struct Piece gameBoard[8][8], enum Colour colour)
{
    struct MoveList moveList;
    moveList.count = 0;

    int inCheck = isInCheck(gameBoard, colour);

    for (int i = 0; i < 8; i++)
    { // row
        for (int j = 0; j < 8; j++)
        { // column
            struct Piece p = gameBoard[i][j];
            if (p.colour != colour)
                continue;

            switch (p.type)
            {
            case PAWN:
            {
                int dir = (colour == WHITE) ? 1 : -1;
                int startRow = (colour == WHITE) ? 1 : 6;

                // Forward 1 square
                if (j + dir >= 0 && j + dir < 8 && gameBoard[i][j + dir].type == -1)
                    if (isMoveValid(gameBoard, i, j, i, j + dir, colour))
                        moveList.moves[moveList.count++] = (struct Move){i, j, i, j + dir};

                // Forward 2 from start row
                if (j == startRow && gameBoard[i][j + dir].type == -1 && gameBoard[i][j + 2 * dir].type == -1)
                    if (isMoveValid(gameBoard, i, j, i, j + 2 * dir, colour))
                        moveList.moves[moveList.count++] = (struct Move){i, j, i, j + 2 * dir};

                // Captures
                for (int di = -1; di <= 1; di += 2)
                {
                    int ni = i + di;
                    int nj = j + dir;
                    if (ni >= 0 && ni < 8 && nj >= 0 && nj < 8)
                    {
                        if (gameBoard[ni][nj].type != -1 && gameBoard[ni][nj].colour != colour)
                            if (isMoveValid(gameBoard, i, j, ni, nj, colour))
                                moveList.moves[moveList.count++] = (struct Move){i, j, ni, nj};

                        // En passant - only valid if opponent just moved a pawn two squares
                        // White pawns on y=4 can capture black pawns that just moved to rank 5
                        // Black pawns on y=3 can capture white pawns that just moved to rank 4
                        if (gameBoard[ni][j].type == PAWN && gameBoard[ni][j].colour != colour)
                        {
                            if (lastMove.fromX == ni && lastMove.toX == ni &&
                                ((colour == WHITE && j == 4 && lastMove.fromY == 6 && lastMove.toY == 4) ||
                                 (colour == BLACK && j == 3 && lastMove.fromY == 1 && lastMove.toY == 3)))
                            {
                                if (isMoveValid(gameBoard, i, j, ni, nj, colour))
                                    moveList.moves[moveList.count++] = (struct Move){i, j, ni, nj};
                            }
                        }
                    }
                }
                break;
            }

            case KNIGHT:
            {
                int knightMoves[8][2] = {{2, 1}, {1, 2}, {-1, 2}, {-2, 1}, {-2, -1}, {-1, -2}, {1, -2}, {2, -1}};
                for (int k = 0; k < 8; k++)
                {
                    int ni = i + knightMoves[k][0], nj = j + knightMoves[k][1];
                    if (ni >= 0 && ni < 8 && nj >= 0 && nj < 8 &&
                        (gameBoard[ni][nj].type == -1 || gameBoard[ni][nj].colour != colour))
                        if (isMoveValid(gameBoard, i, j, ni, nj, colour))
                            moveList.moves[moveList.count++] = (struct Move){i, j, ni, nj};
                }
                break;
            }

            case BISHOP:
            case ROOK:
            case QUEEN:
            {
                int dirs[8][2];
                int ndirs = 0;

                if (p.type == BISHOP)
                {
                    int tmp[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
                    for (int t = 0; t < 4; t++)
                    {
                        dirs[t][0] = tmp[t][0];
                        dirs[t][1] = tmp[t][1];
                    }
                    ndirs = 4;
                }
                if (p.type == ROOK)
                {
                    int tmp[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                    for (int t = 0; t < 4; t++)
                    {
                        dirs[t][0] = tmp[t][0];
                        dirs[t][1] = tmp[t][1];
                    }
                    ndirs = 4;
                }
                if (p.type == QUEEN)
                {
                    int tmp[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
                    for (int t = 0; t < 8; t++)
                    {
                        dirs[t][0] = tmp[t][0];
                        dirs[t][1] = tmp[t][1];
                    }
                    ndirs = 8;
                }

                for (int d = 0; d < ndirs; d++)
                {
                    int ni = i + dirs[d][0], nj = j + dirs[d][1];
                    while (ni >= 0 && ni < 8 && nj >= 0 && nj < 8)
                    {
                        if (gameBoard[ni][nj].type == -1)
                        {
                            if (isMoveValid(gameBoard, i, j, ni, nj, colour))
                                moveList.moves[moveList.count++] = (struct Move){i, j, ni, nj};
                        }
                        else
                        {
                            if (gameBoard[ni][nj].colour != colour)
                                if (isMoveValid(gameBoard, i, j, ni, nj, colour))
                                    moveList.moves[moveList.count++] = (struct Move){i, j, ni, nj};
                            break;
                        }
                        ni += dirs[d][0];
                        nj += dirs[d][1];
                    }
                }
                break;
            }

            case KING:
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        if (dx == 0 && dy == 0)
                            continue;
                        int ni = i + dx, nj = j + dy;
                        if (ni >= 0 && ni < 8 && nj >= 0 && nj < 8 &&
                            (gameBoard[ni][nj].type == -1 || gameBoard[ni][nj].colour != colour))
                            if (isMoveValid(gameBoard, i, j, ni, nj, colour))
                                moveList.moves[moveList.count++] = (struct Move){i, j, ni, nj};
                    }
                }

                // Castling
                int row = j;
                if (!p.hasMoved && !isInCheck(gameBoard, colour))
                {
                    // Kingside
                    if (gameBoard[7][row].type == ROOK && gameBoard[7][row].colour == colour && !gameBoard[7][row].hasMoved)
                    {
                        if (gameBoard[5][row].type == -1 && gameBoard[6][row].type == -1)
                        {
                            struct Piece tmp = p;
                            gameBoard[i][j].type = -1;
                            gameBoard[i][j].colour = -1;
                            gameBoard[5][row] = tmp;
                            if (!isInCheck(gameBoard, colour))
                                moveList.moves[moveList.count++] = (struct Move){i, j, 6, row};
                            gameBoard[5][row].type = -1;
                            gameBoard[5][row].colour = -1;
                            gameBoard[i][j] = tmp;
                        }
                    }
                    // Queenside
                    if (gameBoard[0][row].type == ROOK && gameBoard[0][row].colour == colour && !gameBoard[0][row].hasMoved)
                    {
                        if (gameBoard[1][row].type == -1 && gameBoard[2][row].type == -1 && gameBoard[3][row].type == -1)
                        {
                            struct Piece tmp = p;
                            gameBoard[i][j].type = -1;
                            gameBoard[i][j].colour = -1;
                            gameBoard[3][row] = tmp;
                            if (!isInCheck(gameBoard, colour))
                                moveList.moves[moveList.count++] = (struct Move){i, j, 2, row};
                            gameBoard[3][row].type = -1;
                            gameBoard[3][row].colour = -1;
                            gameBoard[i][j] = tmp;
                        }
                    }
                }
                break;
            }

            default:
                break;
            }
        }
    }

    return moveList;
}

// Adds current move to board history
void recordBoardHistory()
{
    if (boardHistoryCount < 200)
    {
        for (int i = 0; i < 8; i++)
        {
            for (int j = 0; j < 8; j++)
            {
                boardHistory[boardHistoryCount][i][j] = board[i][j];
            }
        }
        boardHistoryCount++;
    }
}

// Finds threefold repetitions
int countBoardRepetitions()
{
    if (boardHistoryCount == 0)
        return 0;

    // Compare the current board with all previous positions
    int repetitions = 1;

    for (int h = boardHistoryCount - 1; h >= 0 && repetitions < 3; h--)
    {
        int match = 1;
        for (int i = 0; i < 8 && match; i++)
        {
            for (int j = 0; j < 8 && match; j++)
            {
                if (boardHistory[h][i][j].type != board[i][j].type ||
                    boardHistory[h][i][j].colour != board[i][j].colour)
                {
                    match = 0;
                }
            }
        }
        if (match)
        {
            repetitions++;
        }
    }

    return repetitions;
}

// Handle pawn promotion when a pawn reaches the last rank
// Promotes to Queen by default
void promotePawn(struct Piece gameBoard[8][8], int x, int y)
{
    if (gameBoard[x][y].type != PAWN)
        return;

    // White pawns promote at rank 8 (y=7), Black pawns at rank 1 (y=0)
    if ((gameBoard[x][y].colour == WHITE && y == 7) ||
        (gameBoard[x][y].colour == BLACK && y == 0))
    {
        gameBoard[x][y].type = QUEEN; // Promote to Queen
        printf("Pawn promoted to Queen at %c%d!\n", 'a' + x, y + 1);
    }
}
