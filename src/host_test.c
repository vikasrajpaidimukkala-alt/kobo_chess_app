#include "chess.h"

#include <stdio.h>
#include <string.h>

static int g_fails;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fails++;
        return;
    }

    printf("ok  %s\n", msg);
}

static bool play(Game *g, int from, int to)
{
    Move m;

    if (!chess_find_move(g, from, to, 0, &m)) {
        return false;
    }

    return chess_make(g, m);
}

int main(void)
{
    Game g;
    Move mv[MAX_MOVES];
    int n;

    chess_init(&g);
    n = chess_generate_legal(&g, mv, MAX_MOVES);
    expect(n == 20, "white has 20 opening moves");

    expect(play(&g, make_sq(4, 1), make_sq(4, 3)), "e2e4");
    n = chess_generate_legal(&g, mv, MAX_MOVES);
    expect(n == 20, "black has 20 replies after e4");

    expect(play(&g, make_sq(4, 6), make_sq(4, 4)), "e7e5");
    expect(play(&g, make_sq(5, 0), make_sq(2, 3)), "Bf1c4");
    expect(play(&g, make_sq(1, 7), make_sq(2, 5)), "Nb8c6");
    expect(play(&g, make_sq(3, 0), make_sq(7, 4)), "Qd1h5");
    expect(play(&g, make_sq(6, 7), make_sq(5, 5)), "Ng8f6");
    expect(play(&g, make_sq(7, 4), make_sq(5, 6)), "Qh5xf7 mate");
    chess_update_result(&g);
    expect(g.result == RESULT_CHECKMATE, "scholar's mate is checkmate");
    expect(chess_undo(&g), "undo mate");
    chess_update_result(&g);
    expect(g.result == RESULT_NONE, "undo restores play");

    chess_init(&g);
    expect(play(&g, make_sq(4, 1), make_sq(4, 3)), "e4");
    expect(play(&g, make_sq(4, 6), make_sq(4, 4)), "e5");
    expect(play(&g, make_sq(6, 0), make_sq(5, 2)), "Nf3");
    expect(play(&g, make_sq(1, 7), make_sq(2, 5)), "Nc6");
    expect(play(&g, make_sq(5, 0), make_sq(1, 4)), "Bb5");
    expect(play(&g, make_sq(0, 6), make_sq(0, 5)), "a6");
    expect(play(&g, make_sq(4, 0), make_sq(6, 0)), "O-O");
    expect(g.board[make_sq(6, 0)] == make_piece(PT_KING, CHESS_WHITE),
           "king on g1 after O-O");
    expect(g.board[make_sq(5, 0)] == make_piece(PT_ROOK, CHESS_WHITE),
           "rook on f1 after O-O");

    chess_init(&g);
    g.board[make_sq(4, 4)] = make_piece(PT_PAWN, CHESS_WHITE);
    g.board[make_sq(4, 1)] = 0;
    g.side = CHESS_BLACK;
    expect(play(&g, make_sq(3, 6), make_sq(3, 4)), "d7d5");
    expect(g.ep == make_sq(3, 5), "ep square d6");
    expect(play(&g, make_sq(4, 4), make_sq(3, 5)), "exd6 ep");
    expect(g.board[make_sq(3, 4)] == 0, "black d5 pawn captured ep");
    expect(g.board[make_sq(3, 5)] == make_piece(PT_PAWN, CHESS_WHITE),
           "white pawn on d6");

    chess_init(&g);
    memset(g.board, 0, sizeof(g.board));
    g.board[make_sq(4, 6)] = make_piece(PT_PAWN, CHESS_WHITE);
    g.board[make_sq(4, 0)] = make_piece(PT_KING, CHESS_WHITE);
    g.board[make_sq(0, 7)] = make_piece(PT_KING, CHESS_BLACK);
    g.castle = 0;
    expect(play(&g, make_sq(4, 6), make_sq(4, 7)), "e7e8=Q auto");
    expect(piece_type(g.board[make_sq(4, 7)]) == PT_QUEEN, "promoted to queen");

    if (g_fails) {
        fprintf(stderr, "%d test(s) failed\n", g_fails);
        return 1;
    }

    printf("all tests passed\n");
    return 0;
}
