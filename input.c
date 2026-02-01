// input.c - parsing chess notation and user input helpers
#include <stdio.h>
#include <string.h>
#include "chess.h"

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
