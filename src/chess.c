#include "chess.h"

#include <stdio.h>
#include <string.h>

static const int KNIGHT_D[8] = { 17, 15, 10, 6, -17, -15, -10, -6 };
static const int KING_D[8] = { 9, 8, 7, 1, -1, -7, -8, -9 };
static const int BISHOP_D[4] = { 9, 7, -9, -7 };
static const int ROOK_D[4] = { 8, 1, -8, -1 };

static bool same_line_ok(int from, int to, int delta)
{
    int df = sq_file(to) - sq_file(from);
    int dr = sq_rank(to) - sq_rank(from);

    if (delta == 1 || delta == -1) {
        return dr == 0;
    }
    if (delta == 8 || delta == -8) {
        return df == 0;
    }
    if (delta == 9 || delta == -9) {
        return df == dr;
    }
    if (delta == 7 || delta == -7) {
        return df == -dr;
    }
    return true;
}

static bool on_board_step(int from, int to)
{
    if (to < 0 || to > 63) {
        return false;
    }

    int df = sq_file(to) - sq_file(from);
    if (df > 2 || df < -2) {
        return false;
    }

    return true;
}

void chess_init(Game *g)
{
    static const int8_t start[64] = {
        4,  2,  3,  5,  6,  3,  2,  4,
        1,  1,  1,  1,  1,  1,  1,  1,
        0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,
       -1, -1, -1, -1, -1, -1, -1, -1,
       -4, -2, -3, -5, -6, -3, -2, -4
    };

    memset(g, 0, sizeof(*g));
    memcpy(g->board, start, sizeof(start));
    g->side = CHESS_WHITE;
    g->ep = SQ_NONE;
    g->castle = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ;
    g->fullmove = 1;
    g->result = RESULT_NONE;
}

static int find_king(const Game *g, int color)
{
    int8_t k = make_piece(PT_KING, color);
    int i;

    for (i = 0; i < 64; i++) {
        if (g->board[i] == k) {
            return i;
        }
    }

    return SQ_NONE;
}

static bool pawn_attacks(const Game *g, int sq, int by_color)
{
    int r = sq_rank(sq);
    int f = sq_file(sq);
    int dir = (by_color == CHESS_WHITE) ? 1 : -1;
    int pr = r - dir;
    int i;

    if (pr < 0 || pr > 7) {
        return false;
    }

    for (i = -1; i <= 1; i += 2) {
        int pf = f + i;
        int psq;

        if (pf < 0 || pf > 7) {
            continue;
        }

        psq = make_sq(pf, pr);
        if (g->board[psq] == make_piece(PT_PAWN, by_color)) {
            return true;
        }
    }

    return false;
}

static bool knight_attacks(const Game *g, int sq, int by_color)
{
    int i;
    int8_t n = make_piece(PT_KNIGHT, by_color);

    for (i = 0; i < 8; i++) {
        int to = sq + KNIGHT_D[i];

        if (!on_board_step(sq, to)) {
            continue;
        }
        if (g->board[to] == n) {
            return true;
        }
    }

    return false;
}

static bool king_attacks(const Game *g, int sq, int by_color)
{
    int i;
    int8_t k = make_piece(PT_KING, by_color);

    for (i = 0; i < 8; i++) {
        int to = sq + KING_D[i];

        if (!on_board_step(sq, to)) {
            continue;
        }
        if (g->board[to] == k) {
            return true;
        }
    }

    return false;
}

static bool slide_attacks(const Game *g, int sq, const int *dirs, int ndirs,
                          int t1, int t2, int by_color)
{
    int i;
    int8_t a = make_piece(t1, by_color);
    int8_t b = make_piece(t2, by_color);

    for (i = 0; i < ndirs; i++) {
        int from = sq;
        int to = sq + dirs[i];

        while (on_board_step(from, to) && same_line_ok(from, to, dirs[i])) {
            int8_t p = g->board[to];

            if (p != 0) {
                if (p == a || p == b) {
                    return true;
                }
                break;
            }

            from = to;
            to += dirs[i];
        }
    }

    return false;
}

bool chess_in_check(const Game *g, int color)
{
    int k = find_king(g, color);
    int enemy = 1 - color;

    if (k == SQ_NONE) {
        return true;
    }

    if (pawn_attacks(g, k, enemy)) {
        return true;
    }
    if (knight_attacks(g, k, enemy)) {
        return true;
    }
    if (king_attacks(g, k, enemy)) {
        return true;
    }
    if (slide_attacks(g, k, BISHOP_D, 4, PT_BISHOP, PT_QUEEN, enemy)) {
        return true;
    }
    if (slide_attacks(g, k, ROOK_D, 4, PT_ROOK, PT_QUEEN, enemy)) {
        return true;
    }

    return false;
}

static bool add_move(Move *out, int *n, int max, int from, int to,
                     int promo, int flags)
{
    if (*n >= max) {
        return false;
    }

    out[*n].from = (uint8_t)from;
    out[*n].to = (uint8_t)to;
    out[*n].promo = (uint8_t)promo;
    out[*n].flags = (uint8_t)flags;
    (*n)++;
    return true;
}

static void add_pawn_to(const Game *g, Move *out, int *n, int max,
                        int from, int to, int flags)
{
    int rank = sq_rank(to);
    int promo_rank = (g->side == CHESS_WHITE) ? 7 : 0;

    if (rank == promo_rank) {
        add_move(out, n, max, from, to, PT_QUEEN, flags | MF_PROMO);
        add_move(out, n, max, from, to, PT_ROOK, flags | MF_PROMO);
        add_move(out, n, max, from, to, PT_BISHOP, flags | MF_PROMO);
        add_move(out, n, max, from, to, PT_KNIGHT, flags | MF_PROMO);
        return;
    }

    add_move(out, n, max, from, to, 0, flags);
}

static void gen_pawn(const Game *g, int from, Move *out, int *n, int max)
{
    int color = g->side;
    int dir = (color == CHESS_WHITE) ? 8 : -8;
    int start_rank = (color == CHESS_WHITE) ? 1 : 6;
    int to = from + dir;
    int f = sq_file(from);
    int i;

    if (to >= 0 && to < 64 && g->board[to] == 0) {
        add_pawn_to(g, out, n, max, from, to, MF_NONE);

        if (sq_rank(from) == start_rank) {
            int to2 = from + 2 * dir;

            if (g->board[to2] == 0) {
                add_move(out, n, max, from, to2, 0, MF_DOUBLE);
            }
        }
    }

    for (i = -1; i <= 1; i += 2) {
        int cf = f + i;
        int cap;

        if (cf < 0 || cf > 7) {
            continue;
        }

        cap = from + dir + i;
        if (cap < 0 || cap > 63) {
            continue;
        }
        if (sq_file(cap) != cf) {
            continue;
        }

        if (g->board[cap] != 0 && piece_color(g->board[cap]) == 1 - color) {
            add_pawn_to(g, out, n, max, from, cap, MF_CAPTURE);
        } else if (cap == g->ep) {
            add_move(out, n, max, from, cap, 0, MF_EP | MF_CAPTURE);
        }
    }
}

static void gen_leaper(const Game *g, int from, const int *dirs, int ndirs,
                       Move *out, int *n, int max)
{
    int color = g->side;
    int i;

    for (i = 0; i < ndirs; i++) {
        int to = from + dirs[i];
        int8_t p;
        int flags;

        if (!on_board_step(from, to)) {
            continue;
        }

        p = g->board[to];
        if (p != 0 && piece_color(p) == color) {
            continue;
        }

        flags = (p != 0) ? MF_CAPTURE : MF_NONE;
        add_move(out, n, max, from, to, 0, flags);
    }
}

static void gen_slider(const Game *g, int from, const int *dirs, int ndirs,
                       Move *out, int *n, int max)
{
    int color = g->side;
    int i;

    for (i = 0; i < ndirs; i++) {
        int prev = from;
        int to = from + dirs[i];

        while (on_board_step(prev, to) && same_line_ok(prev, to, dirs[i])) {
            int8_t p = g->board[to];
            int flags;

            if (p != 0) {
                if (piece_color(p) != color) {
                    flags = MF_CAPTURE;
                    add_move(out, n, max, from, to, 0, flags);
                }
                break;
            }

            add_move(out, n, max, from, to, 0, MF_NONE);
            prev = to;
            to += dirs[i];
        }
    }
}

static bool path_empty(const Game *g, int a, int b)
{
    int step = (b > a) ? 1 : -1;
    int sq;

    for (sq = a + step; sq != b; sq += step) {
        if (g->board[sq] != 0) {
            return false;
        }
    }

    return true;
}

static bool square_safe(Game *g, int sq, int color)
{
    int8_t saved = g->board[sq];
    bool attacked;
    int k = find_king(g, color);

    /*
     * Temporarily put the king on `sq` so x-ray attacks through the
     * vacated king square are counted (needed for castling).
     */
    if (k != SQ_NONE && k != sq) {
        g->board[sq] = g->board[k];
        g->board[k] = 0;
        attacked = chess_in_check(g, color);
        g->board[k] = g->board[sq];
        g->board[sq] = saved;
        return !attacked;
    }

    return !chess_in_check(g, color);
}

static void gen_castling(Game *g, Move *out, int *n, int max)
{
    int color = g->side;
    int rank = (color == CHESS_WHITE) ? 0 : 7;
    int king = make_sq(4, rank);
    uint8_t ks = (color == CHESS_WHITE) ? CASTLE_WK : CASTLE_BK;
    uint8_t qs = (color == CHESS_WHITE) ? CASTLE_WQ : CASTLE_BQ;

    if (g->board[king] != make_piece(PT_KING, color)) {
        return;
    }
    if (chess_in_check(g, color)) {
        return;
    }

    if ((g->castle & ks) &&
        path_empty(g, king, make_sq(7, rank)) &&
        square_safe(g, make_sq(5, rank), color) &&
        square_safe(g, make_sq(6, rank), color)) {
        add_move(out, n, max, king, make_sq(6, rank), 0, MF_CASTLE);
    }

    if ((g->castle & qs) &&
        path_empty(g, king, make_sq(0, rank)) &&
        square_safe(g, make_sq(3, rank), color) &&
        square_safe(g, make_sq(2, rank), color)) {
        add_move(out, n, max, king, make_sq(2, rank), 0, MF_CASTLE);
    }
}

static int gen_pseudo(Game *g, Move *out, int max)
{
    int n = 0;
    int sq;

    for (sq = 0; sq < 64; sq++) {
        int8_t p = g->board[sq];
        int t;

        if (p == 0 || piece_color(p) != g->side) {
            continue;
        }

        t = piece_type(p);
        switch (t) {
        case PT_PAWN:
            gen_pawn(g, sq, out, &n, max);
            break;
        case PT_KNIGHT:
            gen_leaper(g, sq, KNIGHT_D, 8, out, &n, max);
            break;
        case PT_BISHOP:
            gen_slider(g, sq, BISHOP_D, 4, out, &n, max);
            break;
        case PT_ROOK:
            gen_slider(g, sq, ROOK_D, 4, out, &n, max);
            break;
        case PT_QUEEN:
            gen_slider(g, sq, BISHOP_D, 4, out, &n, max);
            gen_slider(g, sq, ROOK_D, 4, out, &n, max);
            break;
        case PT_KING:
            gen_leaper(g, sq, KING_D, 8, out, &n, max);
            break;
        default:
            break;
        }
    }

    gen_castling(g, out, &n, max);
    return n;
}

static void update_castle_rights(Game *g, int from, int to)
{
    if (from == 4 || to == 4 || from == 7 || to == 7) {
        g->castle &= (uint8_t)~CASTLE_WK;
    }
    if (from == 4 || to == 4 || from == 0 || to == 0) {
        g->castle &= (uint8_t)~CASTLE_WQ;
    }
    if (from == 60 || to == 60 || from == 63 || to == 63) {
        g->castle &= (uint8_t)~CASTLE_BK;
    }
    if (from == 60 || to == 60 || from == 56 || to == 56) {
        g->castle &= (uint8_t)~CASTLE_BQ;
    }
}

bool chess_make(Game *g, Move m)
{
    Undo *u;
    int8_t piece;
    int8_t captured;
    int ep_cap;

    if (g->hist_len >= MAX_HIST) {
        return false;
    }

    u = &g->hist[g->hist_len++];
    u->move = m;
    u->ep_old = (uint8_t)g->ep;
    u->castle_old = g->castle;
    u->halfmove_old = g->halfmove;

    piece = g->board[m.from];
    captured = g->board[m.to];
    u->captured = captured;

    g->board[m.to] = piece;
    g->board[m.from] = 0;
    g->ep = SQ_NONE;

    if (m.flags & MF_EP) {
        ep_cap = m.to + ((g->side == CHESS_WHITE) ? -8 : 8);
        u->captured = g->board[ep_cap];
        g->board[ep_cap] = 0;
    }

    if (m.flags & MF_CASTLE) {
        int rank = sq_rank(m.from);

        if (m.to == make_sq(6, rank)) {
            g->board[make_sq(5, rank)] = g->board[make_sq(7, rank)];
            g->board[make_sq(7, rank)] = 0;
        } else {
            g->board[make_sq(3, rank)] = g->board[make_sq(0, rank)];
            g->board[make_sq(0, rank)] = 0;
        }
    }

    if (m.flags & MF_PROMO) {
        g->board[m.to] = make_piece((int)m.promo, g->side);
    }

    if (m.flags & MF_DOUBLE) {
        g->ep = (int)((m.from + m.to) / 2);
    }

    update_castle_rights(g, m.from, m.to);

    if (piece_type(piece) == PT_PAWN || captured != 0 || (m.flags & MF_EP)) {
        g->halfmove = 0;
    } else {
        g->halfmove++;
    }

    if (g->side == CHESS_BLACK) {
        g->fullmove++;
    }

    g->side = 1 - g->side;
    g->result = RESULT_NONE;
    return true;
}

bool chess_undo(Game *g)
{
    Undo *u;
    Move m;
    int8_t piece;

    if (g->hist_len <= 0) {
        return false;
    }

    u = &g->hist[--g->hist_len];
    m = u->move;
    g->side = 1 - g->side;
    g->ep = (int)u->ep_old;
    g->castle = u->castle_old;
    g->halfmove = u->halfmove_old;
    g->result = RESULT_NONE;

    if (g->side == CHESS_BLACK && g->fullmove > 1) {
        g->fullmove--;
    }

    piece = g->board[m.to];
    if (m.flags & MF_PROMO) {
        piece = make_piece(PT_PAWN, g->side);
    }

    g->board[m.from] = piece;
    g->board[m.to] = 0;

    if (m.flags & MF_EP) {
        int ep_cap = m.to + ((g->side == CHESS_WHITE) ? -8 : 8);

        g->board[ep_cap] = u->captured;
    } else if (!(m.flags & MF_CASTLE)) {
        g->board[m.to] = u->captured;
    }

    if (m.flags & MF_CASTLE) {
        int rank = sq_rank(m.from);

        if (m.to == make_sq(6, rank)) {
            g->board[make_sq(7, rank)] = g->board[make_sq(5, rank)];
            g->board[make_sq(5, rank)] = 0;
        } else {
            g->board[make_sq(0, rank)] = g->board[make_sq(3, rank)];
            g->board[make_sq(3, rank)] = 0;
        }
    }

    return true;
}

static bool is_legal_after(Game *g, Move m)
{
    int us = g->side;
    bool ok;

    if (!chess_make(g, m)) {
        return false;
    }

    ok = !chess_in_check(g, us);
    chess_undo(g);
    return ok;
}

/*
 * Search variant: make/unmake leaves `g` exactly as it was, so the
 * caller's live position can be used directly. chess_generate_legal
 * copies the whole Game (~4 KB) per call, which a search cannot pay
 * at every node.
 */
int chess_generate_legal_into(Game *g, Move *out, int max)
{
    Move pseudo[MAX_MOVES];
    int pn;
    int n = 0;
    int i;

    pn = gen_pseudo(g, pseudo, MAX_MOVES);
    for (i = 0; i < pn && n < max; i++) {
        if (is_legal_after(g, pseudo[i])) {
            out[n++] = pseudo[i];
        }
    }

    return n;
}

/*
 * Captures and promotions only, filtered before the legality test so
 * a quiescence search does not pay for a make/in-check/unmake on every
 * quiet move it is about to discard anyway.
 */
int chess_generate_captures_into(Game *g, Move *out, int max)
{
    Move pseudo[MAX_MOVES];
    int pn;
    int n = 0;
    int i;

    pn = gen_pseudo(g, pseudo, MAX_MOVES);
    for (i = 0; i < pn && n < max; i++) {
        if (!(pseudo[i].flags & (MF_CAPTURE | MF_PROMO))) {
            continue;
        }
        if (is_legal_after(g, pseudo[i])) {
            out[n++] = pseudo[i];
        }
    }

    return n;
}

int chess_generate_legal(const Game *g, Move *out, int max)
{
    Game tmp = *g;

    return chess_generate_legal_into(&tmp, out, max);
}

int chess_moves_from(const Game *g, int from, Move *out, int max)
{
    Move all[MAX_MOVES];
    int nall = chess_generate_legal(g, all, MAX_MOVES);
    int n = 0;
    int i;

    for (i = 0; i < nall && n < max; i++) {
        if (all[i].from == from) {
            out[n++] = all[i];
        }
    }

    return n;
}

uint64_t chess_legal_mask(const Game *g, int from)
{
    Move mv[MAX_MOVES];
    int n = chess_moves_from(g, from, mv, MAX_MOVES);
    uint64_t mask = 0;
    int i;

    for (i = 0; i < n; i++) {
        mask |= (uint64_t)1 << mv[i].to;
    }

    return mask;
}

bool chess_find_move(const Game *g, int from, int to, int promo, Move *out)
{
    Move mv[MAX_MOVES];
    int n = chess_moves_from(g, from, mv, MAX_MOVES);
    int i;
    int fallback = -1;

    for (i = 0; i < n; i++) {
        if (mv[i].to != to) {
            continue;
        }
        if (promo != 0 && mv[i].promo == (uint8_t)promo) {
            *out = mv[i];
            return true;
        }
        if (promo == 0) {
            if (mv[i].promo == PT_QUEEN || mv[i].promo == 0) {
                *out = mv[i];
                return true;
            }
            if (fallback < 0) {
                fallback = i;
            }
        }
    }

    if (fallback >= 0) {
        *out = mv[fallback];
        return true;
    }

    return false;
}

void chess_update_result(Game *g)
{
    Move mv[MAX_MOVES];
    int n = chess_generate_legal(g, mv, MAX_MOVES);

    if (n > 0) {
        g->result = RESULT_NONE;
        return;
    }

    g->result = chess_in_check(g, g->side) ? RESULT_CHECKMATE
                                           : RESULT_STALEMATE;
}

char chess_piece_letter(int8_t p)
{
    static const char *letters = " PNBRQK";
    int t = piece_type(p);

    if (t < 1 || t > 6) {
        return ' ';
    }

    return letters[t];
}

void chess_status_text(const Game *g, char *buf, size_t n)
{
    const char *side = (g->side == CHESS_WHITE) ? "White" : "Black";

    if (g->result == RESULT_CHECKMATE) {
        snprintf(buf, n, "Checkmate - %s wins",
                 (g->side == CHESS_WHITE) ? "Black" : "White");
        return;
    }
    if (g->result == RESULT_STALEMATE) {
        snprintf(buf, n, "Stalemate - draw");
        return;
    }
    if (chess_in_check(g, g->side)) {
        snprintf(buf, n, "%s to move - Check", side);
        return;
    }

    snprintf(buf, n, "%s to move", side);
}
