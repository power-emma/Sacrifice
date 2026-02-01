#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>

#include "chess.h"


// A1 -> H8
struct Piece board[8][8];

// Current recursion depth (defined here so other modules can reference it via extern)
int depth = 4;

int main()
{
    boardSetup();

    // First, ask whether to load a puzzle. If a puzzle is loaded,
    char loadChoice = 'n';
    enum Colour aiColour = WHITE;
    enum Colour userColour = BLACK;
    enum Colour currentTurn = WHITE;

    printf("Load a Lichess puzzle from 'lichess_db_puzzle.csv'? (y/n) [n]: ");
    fflush(stdout);
    if (scanf(" %c", &loadChoice) == 1 && (loadChoice == 'y' || loadChoice == 'Y'))
    {
        // Puzzle test loop: repeatedly ask for puzzle number, run one AI response, compare, repeat
        for (;;)
        {
            enum Colour puzzleTurn = WHITE;
            struct LichessPuzzle puzzle;

            if (!loadAndDisplayLichessPuzzle("lichess_db_puzzle.csv", &puzzleTurn, &puzzle))
            {
                printf("Failed to load puzzle. Try another? (y/n) [y]: ");
                char tryAgain = 'y';
                fflush(stdout);
                if (scanf(" %c", &tryAgain) != 1 || (tryAgain != 'y' && tryAgain != 'Y'))
                    break;
                else
                    continue;
            }

            // user plays the side-to-move from the puzzle; AI plays the opponent
            userColour = puzzleTurn;
            aiColour = (userColour == WHITE) ? BLACK : WHITE;

            // Parse puzzle moves into tokens
            char movesCopy[512];
            strncpy(movesCopy, puzzle.moves, sizeof(movesCopy) - 1);
            movesCopy[sizeof(movesCopy) - 1] = '\0';
            char *tokens[128];
            int tokenCount = 0;
            char *t = strtok(movesCopy, " ");
            while (t && tokenCount < 128)
            {
                tokens[tokenCount++] = t;
                t = strtok(NULL, " ");
            }

            // Reset board and load FEN for this puzzle (loadAndDisplayLichessPuzzle already loaded board)
            if (tokenCount == 0)
            {
                printf("Puzzle has no moves listed.\n");
            }
            else
            {
                int allMatched = 1;
                for (int idx = 0; idx < tokenCount; idx++)
                {
                    if (idx % 2 == 0)
                    {
                        // Puzzle (user) move
                        const char *pMove = tokens[idx];
                        if (!isLegalUciMove(board, userColour, pMove))
                        {
                            printf("Puzzle move '%s' is not legal in the current position. Aborting puzzle.\n", pMove);
                            allMatched = 0;
                            break;
                        }
                        if (!executeUciMove(board, pMove))
                        {
                            printf("Failed to execute puzzle move '%s'\n", pMove);
                            allMatched = 0;
                            break;
                        }
                        printf("Puzzle move played (by %s): %s\n", userColour == WHITE ? "White" : "Black", pMove);

                        // If this was the last token, puzzle ends here (no AI reply expected)
                        if (idx + 1 >= tokenCount)
                        {
                            printf("Puzzle concluded after last puzzle move.\n");
                            break;
                        }
                    }
                    else
                    {
                        // AI to move and expected reply is tokens[idx]
                        const char *expected = tokens[idx];
                        moveRanking(board, depth, aiColour);

                        char aiMove[16] = "";
                        if (lastMove.fromX >= 0)
                        {
                            snprintf(aiMove, sizeof(aiMove), "%c%d%c%d", 'a' + lastMove.fromX, lastMove.fromY + 1, 'a' + lastMove.toX, lastMove.toY + 1);
                        }

                        if (aiMove[0] != '\0' && strcmp(aiMove, expected) == 0)
                        {
                            printf("AI move matches puzzle best move: %s -> passed\n", aiMove);
                        }
                        else
                        {
                            printf("AI deviated from puzzle at reply %d.\n", idx);
                            printf("  AI move : %s\n", aiMove[0] ? aiMove : "(none)");
                            printf("  Best move: %s\n", expected);
                            printf("  Puzzle moves sequence: %s\n", puzzle.moves);
                            allMatched = 0;
                            // continue through the rest of the sequence to fully conclude
                        }
                    }
                }

                if (allMatched)
                    printf("AI followed the entire puzzle sequence -> PASSED.\n");
                else
                    printf("AI did not fully follow puzzle sequence.\n");
            }

            // Ask whether to test another puzzle
            char again = 'y';
            printf("Test another puzzle? (y/n) [y]: ");
            fflush(stdout);
            if (scanf(" %c", &again) != 1 || (again != 'y' && again != 'Y'))
                break;

            // Reset board for next puzzle
            boardSetup();
        }
    }
    else
    {
        // No puzzle: ask user which color they want to play
        char choice = 'w';
        printf("Choose your color (w = White, b = Black) [w]: ");
        fflush(stdout);
        if (scanf(" %c", &choice) != 1)
            choice = 'w';
        userColour = (choice == 'b' || choice == 'B') ? BLACK : WHITE;
        aiColour = (userColour == WHITE) ? BLACK : WHITE;
        currentTurn = WHITE;
    }

    printf("Initial board:\n");
    printBoard();

    while (1)
    {
        if (currentTurn == aiColour)
        {
            printf("\n=== AI's Turn (%s) ===\n", aiColour == WHITE ? "White" : "Black");
            moveRanking(board, depth, aiColour);
            printBoard();
        }
        else
        {
            printf("\n=== Your Turn (%s) ===\n", userColour == WHITE ? "White" : "Black");
            int validInput = 0;
            while (!validInput)
            {
                validInput = getUserMove(userColour);
            }

            printBoard();
        }

        // Toggle turn
        currentTurn = (currentTurn == WHITE) ? BLACK : WHITE;
    }

    return 0;
}