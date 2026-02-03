// puzzles.c - Lichess puzzle loading and FEN parsing helpers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chess.h"

// Empty stub for compatibility (not used in file-based loading)
void closePuzzleFileCache(void)
{
    // No-op: file is opened and closed per puzzle load
}

// Loads a puzzle from the CSV file at the specified row number
// Returns 1 on success, 0 on failure
int loadLichessPuzzle(const char *filename, int puzzleNumber, struct LichessPuzzle *puzzle)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        printf("Error: Could not open file '%s'\n", filename);
        return 0;
    }

    char line[2048];
    int currentLine = 0;

    // Read lines until we reach the desired puzzle number
    while (fgets(line, sizeof(line), file))
    {
        if (currentLine == puzzleNumber)
        {
            fclose(file);

            // Parse the CSV line
            // Format: PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags
            char *token;
            char lineCopy[2048];
            strcpy(lineCopy, line);

            // Remove trailing newline
            if (lineCopy[strlen(lineCopy) - 1] == '\n')
                lineCopy[strlen(lineCopy) - 1] = '\0';

            // Parse each field
            token = strtok(lineCopy, ",");
            if (!token) return 0;
            strcpy(puzzle->puzzleId, token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            strcpy(puzzle->fen, token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            strcpy(puzzle->moves, token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            puzzle->rating = atoi(token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            puzzle->ratingDeviation = atoi(token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            puzzle->popularity = atoi(token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            puzzle->nbPlays = atoi(token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            strcpy(puzzle->themes, token);

            token = strtok(NULL, ",");
            if (!token) return 0;
            strcpy(puzzle->gameUrl, token);

            token = strtok(NULL, ",");
            if (token)
                strcpy(puzzle->opening, token);
            else
                puzzle->opening[0] = '\0';

            return 1;
        }
        currentLine++;
    }

    fclose(file);
    printf("Error: Puzzle number %d not found in file\n", puzzleNumber);
    return 0;
}


// Parses FEN string and loads it into the board
// Returns 1 on success, 0 on failure
int loadBoardFromFEN(const char *fen, struct Piece gameBoard[8][8])
{
    // Initialize empty board
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            gameBoard[i][j].type = -1;
            gameBoard[i][j].colour = -1;
            gameBoard[i][j].hasMoved = 0;
        }
    }

    char fenCopy[256];
    strcpy(fenCopy, fen);

    // Get the board position part (before the space)
    char *boardStr = strtok(fenCopy, " ");
    if (!boardStr)
        return 0;

    int file = 0, rank = 7; // Start from top-left (rank 8, file a)

    for (int i = 0; boardStr[i] != '\0'; i++)
    {
        char c = boardStr[i];

        if (c == '/')
        {
            file = 0;
            rank--;
            continue;
        }

        // Empty squares
        if (c >= '1' && c <= '8')
        {
            file += (c - '0');
            continue;
        }

        if (file >= 8 || rank < 0)
            return 0; // Invalid FEN

        // Parse piece
        enum Colour colour = (c >= 'a' && c <= 'z') ? BLACK : WHITE;

        switch (c)
        {
        case 'P':
        case 'p':
            gameBoard[file][rank].type = PAWN;
            break;
        case 'N':
        case 'n':
            gameBoard[file][rank].type = KNIGHT;
            break;
        case 'B':
        case 'b':
            gameBoard[file][rank].type = BISHOP;
            break;
        case 'R':
        case 'r':
            gameBoard[file][rank].type = ROOK;
            break;
        case 'Q':
        case 'q':
            gameBoard[file][rank].type = QUEEN;
            break;
        case 'K':
        case 'k':
            gameBoard[file][rank].type = KING;
            break;
        default:
            return 0; // Invalid piece
        }

        gameBoard[file][rank].colour = colour;
        file++;
    }

    return 1;
}


// Extracts whose turn it is from FEN (returns WHITE or BLACK)
enum Colour getTurnFromFEN(const char *fen)
{
    char fenCopy[256];
    strcpy(fenCopy, fen);

    strtok(fenCopy, " "); // Skip board part
    char *turn = strtok(NULL, " ");

    if (!turn)
        return WHITE; // Default to white

    return (turn[0] == 'w') ? WHITE : BLACK;
}


// Interactive function to load and display a Lichess puzzle
// Returns 1 on success and writes the side to move into `puzzleTurnOut`.
// Returns 0 on failure.
int loadAndDisplayLichessPuzzle(const char *filename, enum Colour *puzzleTurnOut, struct LichessPuzzle *outPuzzle)
{
    int puzzleNumber;

    printf("Enter the puzzle number (row index, 0-based): ");
    fflush(stdout);

    if (scanf("%d", &puzzleNumber) != 1)
    {
        printf("Error: Invalid input\n");
        return 0;
    }

    if (puzzleNumber < 0)
    {
        printf("Error: Puzzle number must be non-negative\n");
        return 0;
    }

    struct LichessPuzzle puzzle;
    if (!loadLichessPuzzle(filename, puzzleNumber, &puzzle))
    {
        printf("Error: Failed to load puzzle\n");
        return 0;
    }

    // Load the FEN position into the board
    if (!loadBoardFromFEN(puzzle.fen, board))
    {
        printf("Error: Failed to parse FEN\n");
        return 0;
    }

    // Get whose turn it is
    enum Colour puzzleTurn = getTurnFromFEN(puzzle.fen);
    if (puzzleTurnOut)
        *puzzleTurnOut = puzzleTurn;
    if (outPuzzle)
        *outPuzzle = puzzle;

    // Display puzzle information
    printf("\n========== Lichess Puzzle ==========%s\n", "");
    printf("Puzzle ID: %s\n", puzzle.puzzleId);
    printf("Rating: %d (±%d)\n", puzzle.rating, puzzle.ratingDeviation);
    printf("Popularity: %d%%\n", puzzle.popularity);
    printf("Times Played: %d\n", puzzle.nbPlays);
    printf("\nFEN: %s\n", puzzle.fen);
    printf("Original Turn: %s\n", (puzzleTurn == WHITE) ? "White" : "Black");
    printf("\nBest Moves: %s\n", puzzle.moves);
    printf("\nThemes: %s\n", puzzle.themes);
    printf("Opening: %s\n", puzzle.opening);
    printf("Game URL: %s\n", puzzle.gameUrl);
    printf("====================================\n\n");
    return 1;
}


// Execute a UCI style move like "e2e4" or "e7e8q" on the given board.
// Returns 1 on success, 0 on failure.
int executeUciMove(struct Piece gameBoard[8][8], const char *uci)
{
    if (!uci || strlen(uci) < 4)
        return 0;

    int fx = uci[0] - 'a';
    int fy = uci[1] - '1';
    int tx = uci[2] - 'a';
    int ty = uci[3] - '1';

    if (fx < 0 || fx > 7 || tx < 0 || tx > 7 || fy < 0 || fy > 7 || ty < 0 || ty > 7)
        return 0;

    struct Piece moving = gameBoard[fx][fy];
    if (moving.type == -1)
        return 0; // nothing to move

    // Simple move/capture
    gameBoard[tx][ty] = moving;
    gameBoard[fx][fy].type = -1;
    gameBoard[fx][fy].colour = -1;
    gameBoard[tx][ty].hasMoved = 1;

    // Promotion (5th char)
    if (strlen(uci) >= 5)
    {
        char pc = uci[4];
        switch (pc)
        {
        case 'q': case 'Q': gameBoard[tx][ty].type = QUEEN; break;
        case 'r': case 'R': gameBoard[tx][ty].type = ROOK; break;
        case 'b': case 'B': gameBoard[tx][ty].type = BISHOP; break;
        case 'n': case 'N': gameBoard[tx][ty].type = KNIGHT; break;
        default: break;
        }
    }

    // Castling: if king moved two squares, move rook accordingly
    if (gameBoard[tx][ty].type == KING && abs(tx - fx) == 2)
    {
        int row = ty;
        if (tx == 6)
        {
            gameBoard[5][row] = gameBoard[7][row];
            gameBoard[7][row].type = -1;
            gameBoard[7][row].colour = -1;
            gameBoard[5][row].hasMoved = 1;
        }
        else if (tx == 2)
        {
            gameBoard[3][row] = gameBoard[0][row];
            gameBoard[0][row].type = -1;
            gameBoard[0][row].colour = -1;
            gameBoard[3][row].hasMoved = 1;
        }
    }

    // Update last move
    lastMove.fromX = fx;
    lastMove.fromY = fy;
    lastMove.toX = tx;
    lastMove.toY = ty;

    // Reset halfmove clock on pawn move or capture
    if (gameBoard[tx][ty].type == PAWN)
        halfmoveClock = 0;

    // Report evaluation stats after executing a move
    printEvaluationCount();

    return 1;
}
