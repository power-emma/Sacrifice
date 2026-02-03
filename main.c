#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <ncurses.h>

#include "chess.h"


// A1 -> H8
struct Piece board[8][8];

// Current recursion depth (defined here so other modules can reference it via extern)
int depth = 4;

int main()
{
    boardSetup();

    // Initialize TUI
    tui_init();
    
    // Show splash screen
    tui_show_splash();
    
    enum Colour aiColour = WHITE;
    enum Colour userColour = BLACK;
    enum Colour currentTurn = WHITE;

    // Main menu loop
    int menu_choice = 0;
    char input[64];
    while (menu_choice == 0) {
        erase();
        
        attron(COLOR_PAIR(7) | A_BOLD);
        mvprintw(2, (COLS - 30) / 2, "SACRIFICE - MAIN MENU");
        attroff(COLOR_PAIR(7) | A_BOLD);
        
        mvprintw(5, (COLS - 20) / 2, "Choose an option:");
        mvprintw(7, 10, "(g) Play Game");
        mvprintw(8, 10, "(p) Puzzle Test");
        mvprintw(9, 10, "(t) Train Engine");
        mvprintw(10, 10, "(q) Quit");
        
        mvprintw(12, 10, "Enter choice: ");
        refresh();
        
        tui_get_input(input, sizeof(input));
        char choice = input[0];
        
        switch (choice) {
            case 'g':
            case 'G':
                // Game mode
                tui_refresh_all(board, currentTurn, "Which colour would you like to play as? (w = White, b = Black): ", 0);
                tui_get_input(input, sizeof(input));
                char game_choice = input[0];
                userColour = (game_choice == 'b' || game_choice == 'B') ? BLACK : WHITE;
                aiColour = (userColour == WHITE) ? BLACK : WHITE;
                currentTurn = WHITE;
                menu_choice = 1;  // Exit menu, start game
                break;
                
            case 'p':
            case 'P':
                // Puzzle test mode
                tui_run_puzzle_test("lichess_db_puzzle.csv", depth);
                menu_choice = 0;  // Return to menu
                break;
                
            case 't':
            case 'T':
                // Training mode
                tui_reconfigure_for_training();
                tui_run_training_threaded("lichess_db_puzzle.csv", 50, 20, depth);
                tui_init();  // Re-initialize normal display
                menu_choice = 0;  // Return to menu
                break;
                
            case 'q':
            case 'Q':
                // Quit
                tui_cleanup();
                return 0;
                
            default:
                tui_show_message("Invalid choice. Try again.");
                menu_choice = 0;
                break;
        }
    }

    // Ask if user wants to load a puzzle during game
    tui_refresh_all(board, currentTurn, "Load a Lichess puzzle? (y/n): ", 0);
    tui_get_input(input, sizeof(input));
    char loadChoice = input[0];
    
    if (loadChoice == 'y' || loadChoice == 'Y')
    {
        // Enter a persistent puzzle session loop: keep loading puzzles until user declines
        while (1) {
            enum Colour puzzleTurn = WHITE;
            if (!tui_load_lichess_puzzle("lichess_db_puzzle.csv", &puzzleTurn)) {
                tui_show_message("Failed to load puzzle. Returning to main menu...");
                break;
            }

            // Execute the first puzzle move (opponent's move that sets up the puzzle)
            const char *firstMove = tui_get_next_puzzle_move();
            if (firstMove && executeUciMove(board, firstMove)) {
                tui_add_move(firstMove);
                tui_advance_puzzle_move();
                recordBoardHistory();
                // Set current turn to solver side
                currentTurn = (puzzleTurn == WHITE) ? BLACK : WHITE;
            }

            // Ensure move history is clear for this new puzzle
            tui_clear_move_history();

            // Refresh display to show the puzzle position after opponent's move
            tui_refresh_all(board, currentTurn, "Puzzle loaded! AI will solve it.", 0);
            napms(500);

            // In puzzle mode, AI solves the puzzle (plays both sides as needed)
            userColour = -1; // No user interaction during puzzle session
            aiColour = currentTurn; // AI starts as the solver side

            // Play the puzzle until completion or failure
            while (tui_is_puzzle_active()) {
                if (currentTurn == aiColour) {
                    tui_refresh_all(board, currentTurn, "AI thinking (puzzle mode)...", 1);
                    moveRanking(board, depth, aiColour);
                    // moveRanking will validate the AI move via tui_validate_puzzle_move()
                    if (!tui_is_puzzle_active()) break; // failed
                    currentTurn = (currentTurn == WHITE) ? BLACK : WHITE;
                    napms(400);
                } else {
                    const char *nextMove = tui_get_next_puzzle_move();
                    if (nextMove) {
                        tui_refresh_all(board, currentTurn, "Opponent responds...", 1);
                        if (executeUciMove(board, nextMove)) {
                            tui_add_move(nextMove);
                            tui_advance_puzzle_move();
                            recordBoardHistory();
                            currentTurn = (currentTurn == WHITE) ? BLACK : WHITE;
                            napms(400);
                        } else {
                            tui_show_message("Failed to execute opponent move!");
                            break;
                        }
                    } else {
                        break; // no more opponent moves
                    }
                }
            }

            // Puzzle complete or failed; ask user whether to load another puzzle
            tui_refresh_all(board, currentTurn, "Puzzle complete! Load another puzzle? (y/n): ", 0);
            char pinput[64];
            tui_get_input(pinput, sizeof(pinput));
            if (pinput[0] == 'y' || pinput[0] == 'Y') {
                // Reset board for next puzzle
                boardSetup();
                tui_clear_move_history();
                // continue loop to load another puzzle
                continue;
            } else {
                // Exit puzzle session and continue to normal game setup
                break;
            }
        }

        // After puzzle session, fall through to ask which colour user wants
        tui_refresh_all(board, currentTurn, "Exiting puzzle mode. Which colour would you like to play as? (w = White, b = Black): ", 0);
        tui_get_input(input, sizeof(input));
        char choice = input[0];

        userColour = (choice == 'b' || choice == 'B') ? BLACK : WHITE;
        aiColour = (userColour == WHITE) ? BLACK : WHITE;
        currentTurn = WHITE;
    } else {
        // Ask user which color they want to play
        tui_refresh_all(board, currentTurn, "Which colour would you like to play as? (w = White, b = Black): ", 0);
        tui_get_input(input, sizeof(input));
        char choice = input[0];
        
        userColour = (choice == 'b' || choice == 'B') ? BLACK : WHITE;
        aiColour = (userColour == WHITE) ? BLACK : WHITE;
        currentTurn = WHITE;
    }

    tui_refresh_all(board, currentTurn, "Game starting...", 0);

    // Main game loop
    while (1)
    {
        // Check if we're in puzzle mode
        if (tui_is_puzzle_active())
        {
            // AI's turn - it should find the correct puzzle move
            if (currentTurn == aiColour)
            {
                tui_refresh_all(board, currentTurn, "AI thinking (puzzle mode)...", 1);
                moveRanking(board, depth, aiColour);
                
                // moveRanking already validated the move via tui_validate_puzzle_move
                // Check if puzzle done
                if (!tui_is_puzzle_active()) {

                    tui_refresh_all(board, currentTurn, "Puzzle Done, Load another? (y/n): ", 0);
                    char input[64];
                    tui_get_input(input, sizeof(input));
                    
                    if (input[0] == 'y' || input[0] == 'Y') {
                        boardSetup();
                        tui_clear_move_history();
                        enum Colour puzzleTurn = WHITE;
                        if (tui_load_lichess_puzzle("lichess_db_puzzle.csv", &puzzleTurn)) {
                            const char *firstMove = tui_get_next_puzzle_move();
                            if (firstMove && executeUciMove(board, firstMove)) {
                                tui_add_move(firstMove);
                                tui_advance_puzzle_move();
                                recordBoardHistory();
                                currentTurn = (puzzleTurn == WHITE) ? BLACK : WHITE;
                            }
                            tui_refresh_all(board, currentTurn, "New puzzle loaded! AI will solve.", 0);
                            napms(500);
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                    continue;
                }
                
                currentTurn = (currentTurn == WHITE) ? BLACK : WHITE;
                napms(800);
            }
            // Opponent's response - execute expected move automatically
            else
            {
                const char *nextMove = tui_get_next_puzzle_move();
                if (nextMove)
                {
                    tui_refresh_all(board, currentTurn, "Opponent responds...", 1);
                    
                    if (executeUciMove(board, nextMove))
                    {
                        tui_add_move(nextMove);
                        tui_advance_puzzle_move();
                        recordBoardHistory();
                        currentTurn = (currentTurn == WHITE) ? BLACK : WHITE;
                        napms(800);
                    }
                    else
                    {
                        tui_show_message("Failed to execute opponent move!");
                        break;
                    }
                }
                else
                {
                    // No more moves - puzzle complete, ask for another
                    tui_refresh_all(board, currentTurn, "Puzzle complete! Load another puzzle? (y/n): ", 0);
                    char input[64];
                    tui_get_input(input, sizeof(input));
                    
                    if (input[0] == 'y' || input[0] == 'Y')
                    {
                        // Reset board and load new puzzle
                        boardSetup();
                        tui_clear_move_history();
                        enum Colour puzzleTurn = WHITE;
                        if (tui_load_lichess_puzzle("lichess_db_puzzle.csv", &puzzleTurn)) {
                            // Execute first move again
                            const char *firstMove = tui_get_next_puzzle_move();
                            if (firstMove && executeUciMove(board, firstMove)) {
                                tui_add_move(firstMove);
                                tui_advance_puzzle_move();
                                recordBoardHistory();
                                currentTurn = (puzzleTurn == WHITE) ? BLACK : WHITE;
                            }
                            tui_refresh_all(board, currentTurn, "New puzzle loaded! AI will solve.", 0);
                            napms(500);
                        } else {
                            tui_show_message("Failed to load puzzle. Exiting...");
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
        else if (currentTurn == aiColour)
        {
            tui_refresh_all(board, currentTurn, "AI is thinking...", 1);
            moveRanking(board, depth, aiColour);
            
            // Toggle turn after AI moves
            currentTurn = (currentTurn == WHITE) ? BLACK : WHITE;
        }
        else
        {
            tui_refresh_all(board, currentTurn, "Enter move (e.g., e4, Nf3, e2e4): ", 0);
            int validInput = 0;
            while (!validInput)
            {
                validInput = getUserMove(userColour);
                if (!validInput) {
                    tui_refresh_all(board, currentTurn, "Invalid move! Try again: ", 0);
                }
            }
            
            // Toggle turn after player moves
            currentTurn = (currentTurn == WHITE) ? BLACK : WHITE;
        }
    }

    tui_cleanup();
    return 0;
}