// boardchecks.c - implementations of board checking functions
#include <stdio.h>
#include <string.h>
#include "chess.h"

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

    // Check for pawn threats
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

// Check if a UCI move is legal for the given colour on the provided board
int isLegalUciMove(struct Piece gameBoard[8][8], enum Colour colour, const char *uci)
{
    if (!uci || strlen(uci) < 4)
        return 0;
    int fx = uci[0] - 'a';
    int fy = uci[1] - '1';
    int tx = uci[2] - 'a';
    int ty = uci[3] - '1';
    struct MoveList moves = validMoves(gameBoard, colour);
    for (int i = 0; i < moves.count; i++)
    {
        if (moves.moves[i].fromX == fx && moves.moves[i].fromY == fy && moves.moves[i].toX == tx && moves.moves[i].toY == ty)
            return 1;
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

            // Print evaluation counter after executing the checkmate move
            printEvaluationCount();

            return 1;
        }
    }

    return 0; // No one-move checkmate found
}


// Checks if a piece at (x, y) can be captured by opponent pieces
int canBeCaptured(struct Piece currentBoard[8][8], int x, int y)
{
    enum Colour pieceColour = currentBoard[x][y].colour;
    enum Colour opponentColour = (pieceColour == WHITE) ? BLACK : WHITE;

    // Pawn attacks
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

// Check if we're in endgame (opponent has at most 2 major pieces)
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
    return (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
}
