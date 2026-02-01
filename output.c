// output.c - console output helpers (board printing)
#include <stdio.h>
#include "chess.h"

// Prints a console representation of the board
void printBoard(void)
{
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
