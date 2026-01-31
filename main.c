#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
enum PieceType
{
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING
};

enum Colour
{
    WHITE,
    BLACK
};

struct Piece
{
    enum PieceType type;
    enum Colour colour;
    int hasMoved;
};

struct Move
{
    int fromX;
    int fromY;
    int toX;
    int toY;
};

struct MoveList
{
    struct Move moves[224]; // Maximum 218 possible moves in chess, but 224 allows for illegal positions
    int count;
};

// forward declarations
int isCheckmate(struct Piece gameBoard[8][8], enum Colour colour);
int isInCheck(struct Piece gameBoard[8][8], enum Colour colour);
struct MoveList validMoves(struct Piece gameBoard[8][8], enum Colour colour);
int checkAndExecuteOneMoveMate(struct Piece gameBoard[8][8], enum Colour currentPlayer);
void promotePawn(struct Piece gameBoard[8][8], int x, int y);

// A1 -> H8
struct Piece board[8][8];

// Track the last move for en passant detection
struct Move lastMove = {-1, -1, -1, -1};

// Track board states for threefold repetition detection
struct Piece boardHistory[200][8][8];
int boardHistoryCount = 0;

// Track halfmove clock for 50-move rule
int halfmoveClock = 0;

// Checks if a board is in stalemate
// A move should not be made if you are in a winning position and there is a stalemate
int isStalemate(struct Piece gameBoard[8][8], enum Colour colour)
{
    if (isInCheck(gameBoard, colour))
    {
        return 0; // In check
    }
    struct MoveList moves = validMoves(gameBoard, colour);
    return (moves.count == 0); // Stalemate if no legal moves and not in check
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

// Finds threefold repitions
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

// See if the king of a colour is in check
int isInCheck(struct Piece gameBoard[8][8], enum Colour colour)
{
    // Find the king
    int kingX = -1;
    int kingY = -1;
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (gameBoard[i][j].type == KING && gameBoard[i][j].colour == colour)
            {
                kingX = i;
                kingY = j;
                break;
            }
        }
        if (kingX != -1)
        {
            break;
        }
    }

    // If king not found (not possible except in custom boards)
    if (kingX == -1)
    {
        return 0;
    }

    enum Colour enemyColour = (colour == WHITE) ? BLACK : WHITE;

    // Could maybe integrate the checks into some other function to avoid duplication

    // Check for pawn threats
    // Black and White pawns move differently from the locked white pov
    if (colour == WHITE)
    {
        if (kingX - 1 >= 0 && kingY + 1 < 8 && gameBoard[kingX - 1][kingY + 1].type == PAWN && gameBoard[kingX - 1][kingY + 1].colour == BLACK)
        {
            return 1;
        }
        if (kingX + 1 < 8 && kingY + 1 < 8 && gameBoard[kingX + 1][kingY + 1].type == PAWN && gameBoard[kingX + 1][kingY + 1].colour == BLACK)
        {
            return 1;
        }
    }
    else
    {
        if (kingX - 1 >= 0 && kingY - 1 >= 0 && gameBoard[kingX - 1][kingY - 1].type == PAWN && gameBoard[kingX - 1][kingY - 1].colour == WHITE)
        {
            return 1;
        }
        if (kingX + 1 < 8 && kingY - 1 >= 0 && gameBoard[kingX + 1][kingY - 1].type == PAWN && gameBoard[kingX + 1][kingY - 1].colour == WHITE)
        {
            return 1;
        }
    }

    // Check for knight threats
    int knightMoves[8][2] = {
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
    for (int m = 0; m < 8; m++)
    {
        int checkX = kingX + knightMoves[m][0];
        int checkY = kingY + knightMoves[m][1];
        if (checkX >= 0 && checkX < 8 && checkY >= 0 && checkY < 8)
        {
            if (gameBoard[checkX][checkY].type == KNIGHT && gameBoard[checkX][checkY].colour == enemyColour)
            {
                return 1;
            }
        }
    }

    // Check for bishop and queen diagonal threats
    for (int dirX = -1; dirX <= 1; dirX += 2)
    {
        for (int dirY = -1; dirY <= 1; dirY += 2)
        {
            int x = kingX + dirX;
            int y = kingY + dirY;
            while (x >= 0 && x < 8 && y >= 0 && y < 8)
            {
                if (gameBoard[x][y].type != -1)
                {
                    if (gameBoard[x][y].colour == enemyColour && (gameBoard[x][y].type == BISHOP || gameBoard[x][y].type == QUEEN))
                    {
                        return 1;
                    }
                    break;
                }
                x += dirX;
                y += dirY;
            }
        }
    }

    // Check for rook and queen straight threats
    for (int dir = 0; dir < 4; dir++)
    {
        int dirX = (dir < 2) ? (dir == 0 ? 1 : -1) : 0;
        int dirY = (dir >= 2) ? (dir == 2 ? 1 : -1) : 0;
        int x = kingX + dirX;
        int y = kingY + dirY;
        while (x >= 0 && x < 8 && y >= 0 && y < 8)
        {
            if (gameBoard[x][y].type != -1)
            {
                if (gameBoard[x][y].colour == enemyColour && (gameBoard[x][y].type == ROOK || gameBoard[x][y].type == QUEEN))
                {
                    return 1;
                }
                break;
            }
            x += dirX;
            y += dirY;
        }
    }

    // Check for king threats (adjacent squares)
    for (int dirX = -1; dirX <= 1; dirX++)
    {
        for (int dirY = -1; dirY <= 1; dirY++)
        {
            if (dirX == 0 && dirY == 0)
                continue;
            int checkX = kingX + dirX;
            int checkY = kingY + dirY;
            if (checkX >= 0 && checkX < 8 && checkY >= 0 && checkY < 8)
            {
                if (gameBoard[checkX][checkY].type == KING && gameBoard[checkX][checkY].colour == enemyColour)
                {
                    return 1;
                }
            }
        }
    }

    return 0; // Not in check
}

// Checks if a given move adheres to the checking rules
int isMoveValid(struct Piece gameBoard[8][8], int fromX, int fromY, int toX, int toY, enum Colour colour)
{
    // Save the piece edited pieces
    struct Piece movedPiece = gameBoard[fromX][fromY];
    struct Piece capturedPiece = gameBoard[toX][toY];

    // Simulate the move
    gameBoard[toX][toY] = movedPiece;
    gameBoard[fromX][fromY].type = -1;
    gameBoard[fromX][fromY].colour = -1;

    int inCheck = isInCheck(gameBoard, colour);

    // Undo the move
    gameBoard[fromX][fromY] = movedPiece;
    gameBoard[toX][toY] = capturedPiece;

    // Return 1 if move is valid
    return !inCheck;
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

    // printf("Total possible moves: %d\n", moveList.count);
    return moveList;
}

// Prints a console representation of the board
int printBoard() {
    printf("  a  b  c  d  e  f  g  h\n");
    for (int j = 7; j >= 0; j--)
    {
        printf("%d ", j + 1); // Rank number (1-8)
        for (int i = 0; i < 8; i++)
        {
            switch (board[i][j].colour)
            {
            case WHITE:
                printf("W");
                break;
            case BLACK:
                printf("B");
                break;
            default:
                printf(".");
                break;
            }
            switch (board[i][j].type)
            {
            case PAWN:
                printf("P ");
                break;
            case KNIGHT:
                printf("N ");
                break;
            case BISHOP:
                printf("B ");
                break;
            case ROOK:
                printf("R ");
                break;
            case QUEEN:
                printf("Q ");
                break;
            case KING:
                printf("K ");
                break;
            default:
                printf(". ");
                break;
            }
        }
        printf("\n");
    }
    printf("  a  b  c  d  e  f  g  h\n");
}

// Structure to hold a move sequence and its score
struct MoveSequence
{
    struct Move moves[224]; 
    int count;
    int score;
};

// Checks if a piece at (x, y) can be captured by opponent pieces
int canBeCaptured(struct Piece currentBoard[8][8], int x, int y)
{
    enum Colour pieceColour = currentBoard[x][y].colour;
    enum Colour opponentColour = (pieceColour == WHITE) ? BLACK : WHITE;

    // Copy pasted from a different function, could be put in its own function
    if (opponentColour == WHITE)
    {

        if (x - 1 >= 0 && y - 1 >= 0 && currentBoard[x - 1][y - 1].type == PAWN && currentBoard[x - 1][y - 1].colour == WHITE)
        {
            return 1;
        }
        if (x + 1 < 8 && y - 1 >= 0 && currentBoard[x + 1][y - 1].type == PAWN && currentBoard[x + 1][y - 1].colour == WHITE)
        {
            return 1;
        }
    }
    else
    {
        // Black pawns attack diagonally downward (decreasing y)
        if (x - 1 >= 0 && y + 1 < 8 && currentBoard[x - 1][y + 1].type == PAWN && currentBoard[x - 1][y + 1].colour == BLACK)
        {
            return 1;
        }
        if (x + 1 < 8 && y + 1 < 8 && currentBoard[x + 1][y + 1].type == PAWN && currentBoard[x + 1][y + 1].colour == BLACK)
        {
            return 1;
        }
    }

    // Check for knight attacks
    int knightMoves[8][2] = {
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
    for (int m = 0; m < 8; m++)
    {
        int checkX = x + knightMoves[m][0];
        int checkY = y + knightMoves[m][1];
        if (checkX >= 0 && checkX < 8 && checkY >= 0 && checkY < 8)
        {
            if (currentBoard[checkX][checkY].type == KNIGHT && currentBoard[checkX][checkY].colour == opponentColour)
            {
                return 1;
            }
        }
    }

    // Check for bishop/queen diagonal attacks
    for (int dirX = -1; dirX <= 1; dirX += 2)
    {
        for (int dirY = -1; dirY <= 1; dirY += 2)
        {
            int checkX = x + dirX;
            int checkY = y + dirY;
            while (checkX >= 0 && checkX < 8 && checkY >= 0 && checkY < 8)
            {
                if (currentBoard[checkX][checkY].type != -1)
                {
                    if (currentBoard[checkX][checkY].colour == opponentColour &&
                        (currentBoard[checkX][checkY].type == BISHOP || currentBoard[checkX][checkY].type == QUEEN))
                    {
                        return 1;
                    }
                    break;
                }
                checkX += dirX;
                checkY += dirY;
            }
        }
    }

    // Check for rook/queen straight attacks
    for (int dir = 0; dir < 4; dir++)
    {
        int dirX = (dir < 2) ? (dir == 0 ? 1 : -1) : 0;
        int dirY = (dir >= 2) ? (dir == 2 ? 1 : -1) : 0;
        int checkX = x + dirX;
        int checkY = y + dirY;
        while (checkX >= 0 && checkX < 8 && checkY >= 0 && checkY < 8)
        {
            if (currentBoard[checkX][checkY].type != -1)
            {
                if (currentBoard[checkX][checkY].colour == opponentColour &&
                    (currentBoard[checkX][checkY].type == ROOK || currentBoard[checkX][checkY].type == QUEEN))
                {
                    return 1;
                }
                break;
            }
            checkX += dirX;
            checkY += dirY;
        }
    }

    // Check for king attacks
    for (int dirX = -1; dirX <= 1; dirX++)
    {
        for (int dirY = -1; dirY <= 1; dirY++)
        {
            if (dirX == 0 && dirY == 0)
                continue;
            int checkX = x + dirX;
            int checkY = y + dirY;
            if (checkX >= 0 && checkX < 8 && checkY >= 0 && checkY < 8)
            {
                if (currentBoard[checkX][checkY].type == KING && currentBoard[checkX][checkY].colour == opponentColour)
                {
                    return 1;
                }
            }
        }
    }

    return 0;
}

// Counts non-pawn pieces for a given color
int countMajorPieces(struct Piece board[8][8], enum Colour colour)
{
    int count = 0;
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (board[i][j].colour == colour &&
                (board[i][j].type == QUEEN || board[i][j].type == ROOK || board[i][j].type == BISHOP || board[i][j].type == KNIGHT))
            {
                count++;
            }
        }
    }
    return count;
}

//Check if we're in endgame (opponent has at most 2 major pieces)
int isInEndgame(struct Piece board[8][8])
{
    int blackMajorPieces = countMajorPieces(board, BLACK);
    int whiteMajorPieces = countMajorPieces(board, WHITE);

    return (blackMajorPieces <= 2);
}

// Calculate square distance (Chebyshev distance) between two squares
int squareDistance(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    // Chebyshev distance (max of absolute differences) - how king moves
    // Side note: kinda wild that they made a whole coordinate space just for chess, thanks wikipedia
    return (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
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
        // Create a temporary board with the move executed
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
        // [Tuning] - King Advancement Bonus
        int bonus = (distanceBefore - distanceAfter) * (5 - distanceAfter);
        return bonus;
    }

    return 0;
}

int evaluateBoardPosition(struct Piece board[8][8])
{

    int score = 0;

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

            // --- Base piece value ---
            // [Tuning] - Piece value multiplier
            score += sign * pieceValue[p.type];

            // -[Tuning] - Global positional table multiplier
            score += sign * globalTable[x][y] * 10;

            // Pawn Special Scoring
            if (p.type == PAWN)
            {
                // Overextended central pawns
                int defended = 0;
                if (p.colour == WHITE && x >= 4 && (y == 3 || y == 4))
                {
                    if (x > 0)
                    {
                        if (y > 0 && board[x - 1][y - 1].type == PAWN && board[x - 1][y - 1].colour == WHITE)
                            defended = 1;
                        if (y < 7 && board[x - 1][y + 1].type == PAWN && board[x - 1][y + 1].colour == WHITE)
                            defended = 1;
                    }
                    if (!defended)
                        // [Tuning] - Overextended central pawn penalty - WHITE
                        score -= 50;
                }
                if (p.colour == BLACK && x <= 3 && (y == 3 || y == 4))
                {
                    if (x < 7)
                    {
                        if (y > 0 && board[x + 1][y - 1].type == PAWN && board[x + 1][y - 1].colour == BLACK)
                            defended = 1;
                        if (y < 7 && board[x + 1][y + 1].type == PAWN && board[x + 1][y + 1].colour == BLACK)
                            defended = 1;
                    }
                    if (!defended)
                        // [Tuning] - Overextended central pawn penalty - BLACK
                        score += 50;
                }

                // [Tuning] - Early Game Pawn Pushing
                if (p.colour == WHITE && x == 3 && (y == 3 || y == 4))
                    score += 40;
                if (p.colour == BLACK && x == 4 && (y == 3 || y == 4))
                    score -= 40;

                // Avoids enemy pawns getting too close to promotion
                if (p.colour == BLACK)
                {
                    int promotionDistance = y; 
                    
                    if (promotionDistance <= 2)
                        // [Tuning] - Enemy pawn promotion threat penalty - Rank 1, 2
                        score += 300 * (2 - promotionDistance); // 200, 400 penalty
                    else if (promotionDistance <= 4)
                        // [Tuning] - Enemy pawn promotion threat penalty - Rank 3, 4
                        score += 80 * (4 - promotionDistance); // 80, 160 penalty
                }

                // Pawn attacks
                if (p.colour == WHITE)
                {
                    if (x > 0 && y > 0)
                        attackMap[x - 1][y - 1] = 1;
                    if (x > 0 && y < 7)
                        attackMap[x - 1][y + 1] = 1;
                }
                else
                {
                    if (x < 7 && y > 0)
                        attackMap[x + 1][y - 1] = 1;
                    if (x < 7 && y < 7)
                        attackMap[x + 1][y + 1] = 1;
                }
            }

            // Knight Special Scoring
            if (p.type == KNIGHT)
            {
                if ((p.colour == WHITE && x == 0 && y >= 2 && y <= 5) ||
                    (p.colour == BLACK && x == 7 && y >= 2 && y <= 5))
                    // [Tuning] - Knight in corner penalty
                    score -= sign * 40;
                if (x == 0 || x == 7)
                    // [Tuning] - Knight on edge penalty
                    score -= sign * 30;

                // Knight attack squares
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
                    // [Tuning] - Sliding piece squares visible multiplier
                    score += sign * mobility * 5;
                }
            }

            // King Special Scoring
            if (p.type == KING)
            {
                if (p.hasMoved)
                    // [Tuning] - King moved penalty - encourages castling
                    score += (p.colour == WHITE) ? -100 : 100;
                if ((x == 3 || x == 4) && y >= 2 && y <= 5)
                    // [Tuning] - King in center penalty
                    score += (p.colour == WHITE) ? -30 : 30;

                // King attack squares
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
    // [Tuning] - King surrounding square control bonus
    int kingSquaresBonus = 20;

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
    }

    // [Tuning] - Check bonuses/penalties
    if (isInCheck(board, WHITE))
        score -= 200;
    if (isInCheck(board, BLACK))
        score += 200;

    // [Tuning] - Stalemate penalties
    if (isStalemate(board, BLACK) && score > 0)
    {
        score = -500; // drawing when winning is bad
    }
    if (isStalemate(board, WHITE) && score < 0)
    {
        score = 500; // drawing when losing is beneficial
    }

    return score;
}

// Move Ranking Recursive Function with Move Sequence Tracking
struct MoveSequence moveRankingRecursiveWithSequence(
    struct Piece board[8][8],
    int depth,
    int maxDepth,
    enum Colour player,
    int alpha,
    int beta)
{
    struct MoveSequence best;
    best.count = 0;
    best.score = -999999999;

    // Checkmate and stalemate are game ending conditions, check first

    // If either side is checkmated in this position, return immediately with extreme score
    if (isCheckmate(board, WHITE))
    {
        best.score = -999999999; // White is checkmated
        return best;
    }
    if (isCheckmate(board, BLACK))
    {
        best.score = 999999999; // Black is checkmated
        return best;
    }

    // Check for stalemate
    if (isStalemate(board, WHITE))
    {
        best.score = -500; // Stalemate is bad for White (should avoid)
        return best;
    }
    if (isStalemate(board, BLACK))
    {
        best.score = 500; // Stalemate is good for White (opponent can't move)
        return best;
    }

    // Check for threefold repetition (draw)
    if (countBoardRepetitions() >= 3)
    {
        best.score = 0; // Draw - neutral score
        return best;
    }

    // Check for 50-move rule (draw)
    if (halfmoveClock >= 100)
    {
        best.score = 0; // Draw - neutral score
        return best;
    }

    // Recursion base case
    if (depth >= maxDepth)
    {
        best.score = evaluateBoardPosition(board);
        return best;
    }

    struct MoveList moves = validMoves(board, player);

    if (moves.count == 0)
    {
        best.score = evaluateBoardPosition(board);
        return best;
    }

    for (int i = 0; i < moves.count; i++)
    {
        struct Piece tempBoard[8][8];

        for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++)
                tempBoard[x][y] = board[x][y];

        int fx = moves.moves[i].fromX;
        int fy = moves.moves[i].fromY;
        int tx = moves.moves[i].toX;
        int ty = moves.moves[i].toY;

        tempBoard[tx][ty] = tempBoard[fx][fy];
        tempBoard[fx][fy].type = -1;
        tempBoard[fx][fy].colour = -1;
        tempBoard[tx][ty].hasMoved = 1;

        // Castling
        if (tempBoard[tx][ty].type == KING && fx == 4)
        {
            if (tx == 6)
            {
                tempBoard[5][ty] = tempBoard[7][ty];
                tempBoard[7][ty].type = -1;
                tempBoard[7][ty].colour = -1;
                tempBoard[5][ty].hasMoved = 1;
            }
            else if (tx == 2)
            {
                tempBoard[3][ty] = tempBoard[0][ty];
                tempBoard[0][ty].type = -1;
                tempBoard[0][ty].colour = -1;
                tempBoard[3][ty].hasMoved = 1;
            }
        }

        struct MoveSequence child =
            moveRankingRecursiveWithSequence(
                tempBoard,
                depth + 1,
                maxDepth,
                player == WHITE ? BLACK : WHITE,
                -beta,
                -alpha);

        int score = -child.score;

        // Apply endgame advancement bonus at depth 0 (immediate move evaluation)
        if (depth == 0)
        {
            int advancementBonus = evaluateEndgameAdvancement(board, fx, fy, tx, ty, player);
            score += advancementBonus;
        }

        if (score > best.score)
        {
            best.score = score;
            best.count = 1;
            best.moves[0] = moves.moves[i];

            for (int j = 0; j < child.count && best.count < 224; j++)
            {
                best.moves[best.count++] = child.moves[j];
            }
        }

        if (score > alpha)
            alpha = score;

        // Alpha-beta prune
        if (alpha >= beta)
            break;
    }

    return best;
}

// Main move ranking function that executes best move and prints sequence
int moveRanking(struct Piece currentBoard[8][8], int maxRecursiveDepth)
{
    // Check for one-move checkmate first - Always checkmate if available
    if (checkAndExecuteOneMoveMate(currentBoard, WHITE))
    {
        return 999999999;
    }

    struct MoveSequence bestSequence = moveRankingRecursiveWithSequence(currentBoard, 0, maxRecursiveDepth, WHITE, -999999999, 999999999);

    if (bestSequence.count == 0)
    {
        printf("No valid moves available.\n");
        return 0;
    }

    // Print the predicted move sequence
    printf("Predicted best move sequence (depth %d):\n", maxRecursiveDepth);
    enum Colour currentPlayer = WHITE;
    for (int i = 0; i < bestSequence.count; i++)
    {
        char moveNotation[10];
        sprintf(moveNotation, "%c%d%c%d",
                'a' + bestSequence.moves[i].fromX, bestSequence.moves[i].fromY + 1,
                'a' + bestSequence.moves[i].toX, bestSequence.moves[i].toY + 1);
        printf("  %d. %s (%s)\n", i + 1, moveNotation, currentPlayer == WHITE ? "White" : "Black");
        currentPlayer = (currentPlayer == WHITE) ? BLACK : WHITE;
    }
    printf("Final evaluation: %d\n\n", bestSequence.score);

    // Execute the first move on the global board
    if (bestSequence.count > 0)
    {
        int bestFromX = bestSequence.moves[0].fromX;
        int bestFromY = bestSequence.moves[0].fromY;
        int bestToX = bestSequence.moves[0].toX;
        int bestToY = bestSequence.moves[0].toY;

        char notation[10];
        sprintf(notation, "%c%d%c%d",
                'a' + bestFromX, bestFromY + 1,
                'a' + bestToX, bestToY + 1);
        printf("White plays: %s\n", notation);

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
    }

    return bestSequence.score;
}


// Convert chess notation (e.g., "e2e4" or "Nf3" or "Qxg4", "b1=Q+" for promotion) to coordinates
// promotionPiece: output parameter set to promotion piece type (QUEEN/ROOK/BISHOP/KNIGHT) or -1 if no promotion
// Returns 1 on success, 0 on failure
int parseChessNotation(const char *notation, int *fromX, int *fromY, int *toX, int *toY, enum Colour colour, int *promotionPiece)
{
    *promotionPiece = -1; // Default: no promotion
    if (notation == NULL || strlen(notation) == 0)
    {
        printf("Invalid notation format.\n");
        return 0;
    }

    // Create a trimmed copy without newline
    char notation_clean[256];
    strncpy(notation_clean, notation, 255);
    notation_clean[255] = '\0';
    int len = strlen(notation_clean);
    if (len > 0 && notation_clean[len - 1] == '\n')
    {
        notation_clean[len - 1] = '\0';
        len--;
    }

    // Check for castling notation first
    if (strcmp(notation_clean, "O-O") == 0 || strcmp(notation_clean, "0-0") == 0)
    {
        // Kingside castling
        int kingStartY = (colour == WHITE) ? 0 : 7;
        *fromX = 4; // King at e-file
        *fromY = kingStartY;
        *toX = 6; // King moves to g-file
        *toY = kingStartY;
        return 1;
    }

    if (strcmp(notation_clean, "O-O-O") == 0 || strcmp(notation_clean, "0-0-0") == 0)
    {
        // Queenside castling
        int kingStartY = (colour == WHITE) ? 0 : 7;
        *fromX = 4; // King at e-file
        *fromY = kingStartY;
        *toX = 2; // King moves to c-file
        *toY = kingStartY;
        return 1;
    }

    // Check for promotion notation (e.g., "b1=Q" or "b1=Q+")
    int hasPromotionNotation = 0;
    for (int i = 0; i < len; i++)
    {
        if (notation_clean[i] == '=')
        {
            hasPromotionNotation = 1;
            if (i + 1 < len)
            {
                char promotionChar = notation_clean[i + 1];
                switch (promotionChar)
                {
                case 'Q':
                    *promotionPiece = QUEEN;
                    break;
                case 'R':
                    *promotionPiece = ROOK;
                    break;
                case 'B':
                    *promotionPiece = BISHOP;
                    break;
                case 'N':
                    *promotionPiece = KNIGHT;
                    break;
                default:
                    printf("Invalid promotion piece: %c\n", promotionChar);
                    return 0;
                }
            }
            break;
        }
    }

    // Check if it's long algebraic notation (e.g., "e2e4" or with promotion "e7e8=Q")
    if ((len == 4 || (len >= 5 && hasPromotionNotation)) &&
        notation_clean[0] >= 'a' && notation_clean[0] <= 'h' && notation_clean[1] >= '1' && notation_clean[1] <= '8' &&
        notation_clean[2] >= 'a' && notation_clean[2] <= 'h' && notation_clean[3] >= '1' && notation_clean[3] <= '8')
    {

        // Convert to coordinates
        *fromX = notation_clean[0] - 'a';
        *fromY = notation_clean[1] - '1'; // rank 1 = index 0, rank 8 = index 7
        *toX = notation_clean[2] - 'a';
        *toY = notation_clean[3] - '1'; // rank 1 = index 0, rank 8 = index 7
        return 1;
    }

    // Try 2-character square notation with promotion (e.g., "b1=Q" or "b1=Q+")
    if (len >= 4 && hasPromotionNotation &&
        notation_clean[0] >= 'a' && notation_clean[0] <= 'h' && notation_clean[1] >= '1' && notation_clean[1] <= '8' &&
        notation_clean[2] == '=')
    {

        // This is a simplified notation like "b1=Q" - destination only
        // We need to find a pawn that can move to this square
        *toX = notation_clean[0] - 'a';
        *toY = notation_clean[1] - '1';

        // Find the source pawn
        struct MoveList moves = validMoves(board, colour);
        int foundCount = 0;
        int foundFromX = -1, foundFromY = -1;
        for (int m = 0; m < moves.count; m++)
        {
            int x = moves.moves[m].fromX;
            int y = moves.moves[m].fromY;

            // Check if this is a pawn moving to the destination
            if (board[x][y].type == PAWN && board[x][y].colour == colour &&
                moves.moves[m].toX == *toX && moves.moves[m].toY == *toY)
            {
                foundFromX = x;
                foundFromY = y;
                foundCount++;
            }
        }

        if (foundCount != 1)
        {
            printf("Could not find unique pawn move to %c%d\n", 'a' + *toX, *toY + 1);
            return 0;
        }
        *fromX = foundFromX;
        *fromY = foundFromY;
        return 1;
    }

    // Check if it's long algebraic notation (e.g., "e2e4")
    if (len == 4 &&
        notation_clean[0] >= 'a' && notation_clean[0] <= 'h' && notation_clean[1] >= '1' && notation_clean[1] <= '8' &&
        notation_clean[2] >= 'a' && notation_clean[2] <= 'h' && notation_clean[3] >= '1' && notation_clean[3] <= '8')
    {

        // Convert to coordinates
        *fromX = notation_clean[0] - 'a';
        *fromY = notation_clean[1] - '1'; // rank 1 = index 0, rank 8 = index 7
        *toX = notation_clean[2] - 'a';
        *toY = notation_clean[3] - '1'; // rank 1 = index 0, rank 8 = index 7
        return 1;
    }

    // Standard algebraic notation parsing (e.g., "Nf3", "e4", "Qxg4", "exd5")
    int notationIdx = 0;
    enum PieceType pieceType = PAWN; // Default to pawn if no piece specified
    int hasCapture = 0;
    int fromFileSpecified = -1; // -1 means not specified, 0-7 for a-h
    int fromRankSpecified = -1; // -1 means not specified, 1-8 for rank numbers (not board index)

    // Parse piece type (if specified)
    if (notation_clean[0] >= 'A' && notation_clean[0] <= 'Z')
    {
        switch (notation_clean[0])
        {
        case 'N':
            pieceType = KNIGHT;
            notationIdx++;
            break;
        case 'B':
            pieceType = BISHOP;
            notationIdx++;
            break;
        case 'R':
            pieceType = ROOK;
            notationIdx++;
            break;
        case 'Q':
            pieceType = QUEEN;
            notationIdx++;
            break;
        case 'K':
            pieceType = KING;
            notationIdx++;
            break;
        default:
            printf("Invalid piece type: %c\n", notation_clean[0]);
            return 0;
        }
    }

    // Check for disambiguation (e.g., "Nbd2" or "R1e1" or "dxe4")
    if (notationIdx < len && notation_clean[notationIdx] >= 'a' && notation_clean[notationIdx] <= 'h')
    {
        // Could be disambiguation file or destination file
        // Look ahead to determine
        if (notationIdx + 1 < len)
        {
            if (notation_clean[notationIdx + 1] >= '1' && notation_clean[notationIdx + 1] <= '8')
            {
                // This is destination (e.g., "Nf3") - don't consume it yet
            }
            else if (notation_clean[notationIdx + 1] >= 'a' && notation_clean[notationIdx + 1] <= 'h')
            {
                // This is disambiguation file (e.g., "Nbd2")
                fromFileSpecified = notation_clean[notationIdx] - 'a';
                notationIdx++;
            }
            else if (notation_clean[notationIdx + 1] == 'x')
            {
                // This is disambiguation file before capture (e.g., "dxe4")
                fromFileSpecified = notation_clean[notationIdx] - 'a';
                notationIdx++;
            }
        }
    }
    else if (notationIdx < len && notation_clean[notationIdx] >= '1' && notation_clean[notationIdx] <= '8')
    {
        // Disambiguation rank (e.g., "R1e1")
        fromRankSpecified = notation_clean[notationIdx] - '0'; // Store as 1-8
        notationIdx++;
    }

    // Check for capture symbol
    if (notationIdx < len && notation_clean[notationIdx] == 'x')
    {
        hasCapture = 1;
        notationIdx++;
    }

    // Parse destination square
    if (notationIdx + 1 >= len ||
        notation_clean[notationIdx] < 'a' || notation_clean[notationIdx] > 'h' ||
        notation_clean[notationIdx + 1] < '1' || notation_clean[notationIdx + 1] > '8')
    {
        printf("Invalid destination square in notation.\n");
        return 0;
    }

    *toX = notation_clean[notationIdx] - 'a';
    *toY = notation_clean[notationIdx + 1] - '1'; // rank 1 = index 0, rank 8 = index 7

    // Find the source piece
    int foundCount = 0;
    int foundFromX = -1, foundFromY = -1;

    struct MoveList moves = validMoves(board, colour);

    for (int m = 0; m < moves.count; m++)
    {
        int x = moves.moves[m].fromX;
        int y = moves.moves[m].fromY;

        // Check if destination matches
        if (moves.moves[m].toX != *toX || moves.moves[m].toY != *toY)
        {
            continue;
        }

        // Check if this is our color's piece and matches the piece type
        if (board[x][y].colour != colour || board[x][y].type != pieceType)
        {
            continue;
        }

        // Check disambiguation constraints
        if (fromFileSpecified != -1 && x != fromFileSpecified)
        {
            continue;
        }
        if (fromRankSpecified != -1 && y != (fromRankSpecified - 1))
        { // Convert rank 1-8 to index 0-7
            continue;
        }

        // For pawn moves without piece symbol, also check file matches (e.g., "d5" from d-file)
        if (pieceType == PAWN && fromFileSpecified == -1 && fromRankSpecified == -1)
        {
            // Pawn notation without disambiguation must be from the same file as destination
            if (x != *toX)
            {
                continue;
            }
        }

        foundFromX = x;
        foundFromY = y;
        foundCount++;
    }

    if (foundCount == 0)
    {
        printf("No legal move found with notation: %s\n", notation);
        return 0;
    }
    else if (foundCount > 1)
    {
        printf("Ambiguous notation: %s (multiple pieces can move there). Please disambiguate.\n", notation);
        return 0;
    }

    *fromX = foundFromX;
    *fromY = foundFromY;

    // Check if there's promotion notation at the end (e.g., "e8=Q" or "e8Q")
    if (hasPromotionNotation && *promotionPiece == -1)
    {
        // Try to extract promotion piece from end of notation
        for (int i = len - 1; i >= 0; i--)
        {
            if (notation_clean[i] >= 'A' && notation_clean[i] <= 'Z' &&
                (notation_clean[i] == 'Q' || notation_clean[i] == 'R' || notation_clean[i] == 'B' || notation_clean[i] == 'N'))
            {
                switch (notation_clean[i])
                {
                case 'Q':
                    *promotionPiece = QUEEN;
                    break;
                case 'R':
                    *promotionPiece = ROOK;
                    break;
                case 'B':
                    *promotionPiece = BISHOP;
                    break;
                case 'N':
                    *promotionPiece = KNIGHT;
                    break;
                }
                break;
            }
        }
    }

    return 1;
}

int getUserMove(enum Colour colour)
{
    int fromX, fromY, toX, toY, promotionPiece;
    char notation[20];

    printf("\n%s's turn. Enter move (e.g., e4, Nf3, Qxg4, e2e4, O-O for castling, or b1=Q for promotion): ",
           (colour == WHITE) ? "White" : "Black");
    fflush(stdout);

    int result = scanf("%s", notation);
    printf("[DEBUG] scanf returned: %d, notation='%s'\n", result, notation);
    fflush(stdout);

    if (result != 1)
    {
        printf("Failed to read input.\n");
        return 0;
    }

    // Parse chess notation
    if (!parseChessNotation(notation, &fromX, &fromY, &toX, &toY, colour, &promotionPiece))
    {
        printf("Failed to parse chess notation.\n");
        return 0;
    }

    // Validate the move
    struct MoveList moves = validMoves(board, colour);
    for (int i = 0; i < moves.count; i++)
    {
        if (moves.moves[i].fromX == fromX && moves.moves[i].fromY == fromY &&
            moves.moves[i].toX == toX && moves.moves[i].toY == toY)
        {

            // Check if this is a capture move (before we overwrite the destination)
            int isCapture = (board[toX][toY].type != -1);
            int isAPawnMove = (board[fromX][fromY].type == PAWN);

            // Execute the move
            board[toX][toY] = board[fromX][fromY];
            board[fromX][fromY].type = -1;
            board[fromX][fromY].colour = -1;
            board[toX][toY].hasMoved = 1; // Mark piece as moved

            // Handle en passant capture (pawn moving diagonally to EMPTY square)
            // Only if: it's a pawn, diagonal move, destination was empty, and there's an enemy pawn on the side
            if (!isCapture && board[toX][toY].type == PAWN &&
                fromX != toX && fromY != toY && // diagonal move
                board[toX][fromY].type == PAWN && board[toX][fromY].colour != colour)
            {
                // This is en passant - remove the enemy pawn at the original rank
                board[toX][fromY].type = -1;
                board[toX][fromY].colour = -1;
            }

            // Handle pawn promotion
            if (board[toX][toY].type == PAWN && promotionPiece != -1)
            {
                board[toX][toY].type = promotionPiece;
                printf("Pawn promoted to %s!\n",
                       promotionPiece == QUEEN ? "Queen" : promotionPiece == ROOK ? "Rook"
                                                       : promotionPiece == BISHOP ? "Bishop"
                                                       : promotionPiece == KNIGHT ? "Knight"
                                                                                  : "Unknown");
            }

            // Handle castling - move the rook
            if (board[toX][toY].type == KING && fromX == 4)
            {
                if (toX == 6)
                {
                    // Kingside castling - move rook from h-file to f-file
                    board[5][toY] = board[7][toY];
                    board[7][toY].type = -1;
                    board[7][toY].colour = -1;
                    board[5][toY].hasMoved = 1;
                }
                else if (toX == 2)
                {
                    // Queenside castling - move rook from a-file to d-file
                    board[3][toY] = board[0][toY];
                    board[0][toY].type = -1;
                    board[0][toY].colour = -1;
                    board[3][toY].hasMoved = 1;
                }
            }

            // Track the last move for en passant detection
            lastMove.fromX = fromX;
            lastMove.fromY = fromY;
            lastMove.toX = toX;
            lastMove.toY = toY;

            // Update halfmove clock for 50-move rule
            // Reset on pawn move or capture, otherwise increment
            if (isAPawnMove || isCapture)
            {
                halfmoveClock = 0;
            }
            else
            {
                halfmoveClock++;
            }

            // Record board state for threefold repetition detection
            recordBoardHistory();

            printf("Move executed: %s\n", notation);
            return 1;
        }
    }

    printf("Invalid move! That move is not in the list of legal moves.\n");
    return 0;
}

int main()
{
    boardSetup();
    printf("Initial board:\n");
    printBoard();

    // Black's turn - user input
    while (1)
    {
        // White's turn - AI plays best move
        printf("\n=== White's Turn ===\n");
        moveRanking(board, 4);
        printBoard();

        int validInput = 0;
        while (!validInput)
        {
            struct MoveList legalMoves = validMoves(board, BLACK);
            // printf("Legal moves for Black:\n");
            // for (int i = 0; i < legalMoves.count; i++) {
            //     struct Move m = legalMoves.moves[i];
            //     printf("  %d. %c%d%c%d\n", i + 1,
            //         'a' + m.fromX, m.fromY + 1,
            //         'a' + m.toX, m.toY + 1);
            // }

            validInput = getUserMove(BLACK);
        }

        printBoard();
    }
    return 0;
}

// Check for one-move checkmate and execute it if found
// Returns 1 if checkmate was found and executed, 0 otherwise
int checkAndExecuteOneMoveMate(struct Piece gameBoard[8][8], enum Colour currentPlayer)
{
    enum Colour opponent = (currentPlayer == WHITE) ? BLACK : WHITE;

    // Get all valid moves for the current player
    struct MoveList moves = validMoves(gameBoard, currentPlayer);

    for (int i = 0; i < moves.count; i++)
    {
        struct Move move = moves.moves[i];
        struct Piece tempBoard[8][8];
        memcpy(tempBoard, gameBoard, sizeof(tempBoard));

        // Simulate the move
        tempBoard[move.toX][move.toY] = tempBoard[move.fromX][move.fromY];
        tempBoard[move.fromX][move.fromY].type = -1;
        tempBoard[move.fromX][move.fromY].colour = -1;
        tempBoard[move.toX][move.toY].hasMoved = 1;

        // Check if this move delivers checkmate
        if (isCheckmate(tempBoard, opponent))
        {
            // Checkmate found! Execute the move on the actual board
            gameBoard[move.toX][move.toY] = gameBoard[move.fromX][move.fromY];
            gameBoard[move.fromX][move.fromY].type = -1;
            gameBoard[move.fromX][move.fromY].colour = -1;
            gameBoard[move.toX][move.toY].hasMoved = 1;

            // Handle pawn promotion
            promotePawn(gameBoard, move.toX, move.toY);

            // Handle castling - move the rook
            if (gameBoard[move.toX][move.toY].type == KING && move.fromX == 4)
            {
                if (move.toX == 6)
                {
                    gameBoard[5][move.toY] = gameBoard[7][move.toY];
                    gameBoard[7][move.toY].type = -1;
                    gameBoard[7][move.toY].colour = -1;
                    gameBoard[5][move.toY].hasMoved = 1;
                }
                else if (move.toX == 2)
                {
                    gameBoard[3][move.toY] = gameBoard[0][move.toY];
                    gameBoard[0][move.toY].type = -1;
                    gameBoard[0][move.toY].colour = -1;
                    gameBoard[3][move.toY].hasMoved = 1;
                }
            }

            // Print the checkmate move
            char notation[10];
            sprintf(notation, "%c%d%c%d",
                    'a' + move.fromX, move.fromY + 1,
                    'a' + move.toX, move.toY + 1);
            printf("CHECKMATE! %s plays: %s\n", currentPlayer == WHITE ? "White" : "Black", notation);

            return 1;
        }
    }

    return 0; // No one-move checkmate found
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

        enum Colour pawnColour = gameBoard[x][y].colour;
        gameBoard[x][y].type = QUEEN; // Promote to Queen
        printf("Pawn promoted to Queen at %c%d!\n", 'a' + x, y + 1);
    }
}

int isCheckmate(struct Piece gameBoard[8][8], enum Colour colour)
{
    if (!isInCheck(gameBoard, colour))
    {
        return 0; // Not in check, so not checkmate
    }

    struct MoveList moves = validMoves(gameBoard, colour);
    for (int i = 0; i < moves.count; i++)
    {
        struct Move move = moves.moves[i];
        struct Piece tempBoard[8][8];
        memcpy(tempBoard, gameBoard, sizeof(tempBoard));

        // Simulate the move
        tempBoard[move.toX][move.toY] = tempBoard[move.fromX][move.fromY];
        tempBoard[move.fromX][move.fromY].type = -1;
        tempBoard[move.fromX][move.fromY].colour = -1;

        if (!isInCheck(tempBoard, colour))
        {
            return 0; // Found a move that gets out of check
        }
    }

    return 1; // No moves get out of check, so it's checkmate
}
