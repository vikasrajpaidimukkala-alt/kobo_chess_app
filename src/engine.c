#include "engine.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Alpha-beta negamax with iterative deepening, a capture-only
 * quiescence search, MVV-LVA plus killer move ordering, and a
 * material/piece-square evaluation.
 *
 * The Libra Colour is a slow single-threaded target and every screen
 * update costs the better part of a second, so the search is bounded
 * by wall clock as well as depth: whatever iteration finished last
 * supplies the move.
 */

#define INF         32000
#define MATE_SCORE  30000
#define MAX_PLY     64
#define MAX_QPLY    8

static const int PIECE_VALUE[7] = { 0, 100, 320, 330, 500, 900, 0 };

/*
 * Tables are written with rank 8 first, so a white piece reads
 * PST[sq ^ 56] and a black piece reads PST[sq].
 */
static const int PST_PAWN[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
     50,  50,  50,  50,  50,  50,  50,  50,
     10,  10,  20,  30,  30,  20,  10,  10,
      5,   5,  10,  25,  25,  10,   5,   5,
      0,   0,   0,  20,  20,   0,   0,   0,
      5,  -5, -10,   0,   0, -10,  -5,   5,
      5,  10,  10, -20, -20,  10,  10,   5,
      0,   0,   0,   0,   0,   0,   0,   0
};

static const int PST_KNIGHT[64] = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20,   0,   0,   0,   0, -20, -40,
    -30,   0,  10,  15,  15,  10,   0, -30,
    -30,   5,  15,  20,  20,  15,   5, -30,
    -30,   0,  15,  20,  20,  15,   0, -30,
    -30,   5,  10,  15,  15,  10,   5, -30,
    -40, -20,   0,   5,   5,   0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50
};

static const int PST_BISHOP[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,  10,  10,   5,   0, -10,
    -10,   5,   5,  10,  10,   5,   5, -10,
    -10,   0,  10,  10,  10,  10,   0, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   5,   0,   0,   0,   0,   5, -10,
    -20, -10, -10, -10, -10, -10, -10, -20
};

static const int PST_ROOK[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
      5,  10,  10,  10,  10,  10,  10,   5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
      0,   0,   0,   5,   5,   0,   0,   0
};

static const int PST_QUEEN[64] = {
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,   5,   5,   5,   0, -10,
     -5,   0,   5,   5,   5,   5,   0,  -5,
      0,   0,   5,   5,   5,   5,   0,  -5,
    -10,   5,   5,   5,   5,   5,   0, -10,
    -10,   0,   5,   0,   0,   0,   0, -10,
    -20, -10, -10,  -5,  -5, -10, -10, -20
};

static const int PST_KING_MID[64] = {
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
     20,  20,   0,   0,   0,   0,  20,  20,
     20,  30,  10,   0,   0,  10,  30,  20
};

static const int PST_KING_END[64] = {
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10,   0,   0, -10, -20, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50
};

typedef struct {
    Game g;
    long nodes;
    clock_t start;
    clock_t budget; /* in ticks, 0 when untimed */
    bool stopped;
    Move killer[MAX_PLY][2];
} Search;

/*
 * clock_t is 32-bit on the Kobo, so budgets are kept in ticks and
 * compared as elapsed differences. Multiplying milliseconds by
 * CLOCKS_PER_SEC first would overflow well before a five second limit.
 */
static clock_t ticks_per_ms(void)
{
    clock_t t = (clock_t)(CLOCKS_PER_SEC / 1000);

    return (t > 0) ? t : 1;
}

void engine_limits_for_level(int level, EngineLimits *out)
{
    switch (level) {
    /*
     * Depth is the ambition, milliseconds are the promise: iterative
     * deepening keeps whatever iteration finished, so on the Kobo's
     * slow core these degrade in depth rather than making the player
     * wait. Budgets sit alongside a ~1s screen refresh per move.
     */
    case ENGINE_EASY:
        out->max_depth = 2;
        out->max_millis = 600;
        out->blunder_cp = 90;
        break;
    case ENGINE_HARD:
        out->max_depth = 6;
        out->max_millis = 5000;
        out->blunder_cp = 0;
        break;
    case ENGINE_MEDIUM:
    default:
        out->max_depth = 4;
        out->max_millis = 2000;
        out->blunder_cp = 25;
        break;
    }
}

const char *engine_level_name(int level)
{
    switch (level) {
    case ENGINE_EASY:
        return "Easy";
    case ENGINE_MEDIUM:
        return "Medium";
    case ENGINE_HARD:
        return "Hard";
    default:
        return "2 Player";
    }
}

static const int *pst_for(int type, bool endgame)
{
    switch (type) {
    case PT_PAWN:
        return PST_PAWN;
    case PT_KNIGHT:
        return PST_KNIGHT;
    case PT_BISHOP:
        return PST_BISHOP;
    case PT_ROOK:
        return PST_ROOK;
    case PT_QUEEN:
        return PST_QUEEN;
    case PT_KING:
        return endgame ? PST_KING_END : PST_KING_MID;
    default:
        return NULL;
    }
}

static int evaluate(const Game *g)
{
    int score = 0;
    int heavy = 0;
    int bishops[2] = { 0, 0 };
    int kings[2] = { SQ_NONE, SQ_NONE };
    bool endgame;
    int sq;

    for (sq = 0; sq < 64; sq++) {
        int8_t p = g->board[sq];
        int type;
        int color;
        const int *pst;

        if (p == 0) {
            continue;
        }

        type = piece_type(p);
        color = piece_color(p);

        if (type == PT_KING) {
            kings[color] = sq;
            continue;
        }

        if (type != PT_PAWN) {
            heavy += PIECE_VALUE[type];
        }
        if (type == PT_BISHOP) {
            bishops[color]++;
        }

        pst = pst_for(type, false);
        if (color == CHESS_WHITE) {
            score += PIECE_VALUE[type] + pst[sq ^ 56];
        } else {
            score -= PIECE_VALUE[type] + pst[sq];
        }
    }

    endgame = (heavy <= 2600);

    if (kings[CHESS_WHITE] != SQ_NONE) {
        score += pst_for(PT_KING, endgame)[kings[CHESS_WHITE] ^ 56];
    }
    if (kings[CHESS_BLACK] != SQ_NONE) {
        score -= pst_for(PT_KING, endgame)[kings[CHESS_BLACK]];
    }

    if (bishops[CHESS_WHITE] >= 2) {
        score += 30;
    }
    if (bishops[CHESS_BLACK] >= 2) {
        score -= 30;
    }

    return (g->side == CHESS_WHITE) ? score : -score;
}

static bool out_of_time(Search *s)
{
    if (s->budget == 0) {
        return false;
    }
    if ((s->nodes & 1023) != 0) {
        return false;
    }

    return (clock() - s->start) >= s->budget;
}

static bool same_move(Move a, Move b)
{
    return a.from == b.from && a.to == b.to && a.promo == b.promo;
}

static int score_move(const Search *s, Move m, int ply, Move first)
{
    if (first.from != first.to && same_move(m, first)) {
        return 1000000;
    }

    if (m.flags & MF_CAPTURE) {
        int victim = (m.flags & MF_EP)
                         ? PT_PAWN
                         : piece_type(s->g.board[m.to]);
        int attacker = piece_type(s->g.board[m.from]);

        return 100000 + PIECE_VALUE[victim] * 10 - PIECE_VALUE[attacker];
    }

    if (m.flags & MF_PROMO) {
        return 90000 + PIECE_VALUE[m.promo];
    }

    if (ply < MAX_PLY) {
        if (same_move(m, s->killer[ply][0])) {
            return 80000;
        }
        if (same_move(m, s->killer[ply][1])) {
            return 79000;
        }
    }

    return 0;
}

/* Selection sort one slot at a time: most searches cut off early. */
static void pick_move(Move *moves, int *scores, int n, int i)
{
    int best = i;
    int j;

    for (j = i + 1; j < n; j++) {
        if (scores[j] > scores[best]) {
            best = j;
        }
    }

    if (best != i) {
        Move tm = moves[i];
        int ts = scores[i];

        moves[i] = moves[best];
        scores[i] = scores[best];
        moves[best] = tm;
        scores[best] = ts;
    }
}

static void store_killer(Search *s, Move m, int ply)
{
    if (ply >= MAX_PLY || (m.flags & (MF_CAPTURE | MF_PROMO))) {
        return;
    }
    if (same_move(s->killer[ply][0], m)) {
        return;
    }

    s->killer[ply][1] = s->killer[ply][0];
    s->killer[ply][0] = m;
}

static int quiesce(Search *s, int alpha, int beta, int ply, int qply)
{
    Move moves[MAX_MOVES];
    int scores[MAX_MOVES];
    Move none;
    int n;
    int i;
    bool in_check;

    s->nodes++;
    if (out_of_time(s)) {
        s->stopped = true;
        return 0;
    }

    memset(&none, 0, sizeof(none));
    in_check = chess_in_check(&s->g, s->g.side);

    if (in_check) {
        if (qply >= MAX_QPLY) {
            return evaluate(&s->g);
        }
    } else {
        int stand = evaluate(&s->g);

        if (stand >= beta) {
            return stand;
        }
        if (stand > alpha) {
            alpha = stand;
        }
        if (qply >= MAX_QPLY) {
            return stand;
        }
    }

    /*
     * Out of check every reply has to be considered, so that mate is
     * not missed; otherwise only captures and promotions, which is
     * what keeps this bounded.
     */
    if (in_check) {
        n = chess_generate_legal_into(&s->g, moves, MAX_MOVES);
        if (n == 0) {
            return -MATE_SCORE + ply;
        }
    } else {
        n = chess_generate_captures_into(&s->g, moves, MAX_MOVES);
        if (n == 0) {
            return alpha;
        }
    }

    for (i = 0; i < n; i++) {
        scores[i] = score_move(s, moves[i], ply, none);
    }

    for (i = 0; i < n; i++) {
        int score;

        pick_move(moves, scores, n, i);

        if (!chess_make(&s->g, moves[i])) {
            continue;
        }
        score = -quiesce(s, -beta, -alpha, ply + 1, qply + 1);
        chess_undo(&s->g);

        if (s->stopped) {
            return 0;
        }
        if (score >= beta) {
            return score;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

static int negamax(Search *s, int depth, int alpha, int beta, int ply)
{
    Move moves[MAX_MOVES];
    int scores[MAX_MOVES];
    Move none;
    int n;
    int i;
    int best = -INF;

    if (depth <= 0) {
        return quiesce(s, alpha, beta, ply, 0);
    }

    s->nodes++;
    if (out_of_time(s)) {
        s->stopped = true;
        return 0;
    }

    if (s->g.halfmove >= 100) {
        return 0;
    }

    n = chess_generate_legal_into(&s->g, moves, MAX_MOVES);
    if (n == 0) {
        if (chess_in_check(&s->g, s->g.side)) {
            return -MATE_SCORE + ply;
        }
        return 0;
    }

    memset(&none, 0, sizeof(none));
    for (i = 0; i < n; i++) {
        scores[i] = score_move(s, moves[i], ply, none);
    }

    for (i = 0; i < n; i++) {
        int score;

        pick_move(moves, scores, n, i);

        if (!chess_make(&s->g, moves[i])) {
            continue;
        }
        score = -negamax(s, depth - 1, -beta, -alpha, ply + 1);
        chess_undo(&s->g);

        if (s->stopped) {
            return 0;
        }

        if (score > best) {
            best = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            store_killer(s, moves[i], ply);
            break;
        }
    }

    return best;
}

static void seed_once(void)
{
    static bool seeded;

    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = true;
    }
}

bool engine_best_move(const Game *g, const EngineLimits *lim, Move *best,
                      EngineInfo *info)
{
    Search s;
    Move moves[MAX_MOVES];
    int scores[MAX_MOVES];
    Move best_move;
    int best_score = 0;
    int completed = 0;
    int n;
    int depth;
    int i;

    memset(&s, 0, sizeof(s));
    s.g = *g;
    s.start = clock();
    s.budget = (lim->max_millis > 0)
                   ? (clock_t)lim->max_millis * ticks_per_ms()
                   : 0;

    n = chess_generate_legal_into(&s.g, moves, MAX_MOVES);
    if (n == 0) {
        return false;
    }

    seed_once();
    best_move = moves[0];

    for (depth = 1; depth <= lim->max_depth; depth++) {
        int alpha = -INF;
        int iter_best = -INF;
        Move iter_move = best_move;

        for (i = 0; i < n; i++) {
            scores[i] = score_move(&s, moves[i], 0, best_move);
        }

        for (i = 0; i < n; i++) {
            int score;

            pick_move(moves, scores, n, i);

            if (!chess_make(&s.g, moves[i])) {
                continue;
            }
            /*
             * With a blunder allowance every root move needs a real
             * score, not just a bound, so the window stays open.
             */
            score = -negamax(&s, depth - 1, -INF,
                             (lim->blunder_cp > 0) ? INF : -alpha, 1);
            chess_undo(&s.g);

            if (s.stopped) {
                break;
            }

            scores[i] = score;
            if (score > iter_best) {
                iter_best = score;
                iter_move = moves[i];
            }
            if (score > alpha) {
                alpha = score;
            }
        }

        if (s.stopped) {
            break;
        }

        best_move = iter_move;
        best_score = iter_best;
        completed = depth;

        if (lim->blunder_cp > 0) {
            int cands[MAX_MOVES];
            int c = 0;

            for (i = 0; i < n; i++) {
                if (scores[i] >= iter_best - lim->blunder_cp) {
                    cands[c++] = i;
                }
            }
            if (c > 0) {
                best_move = moves[cands[rand() % c]];
            }
        }

        if (iter_best > MATE_SCORE - MAX_PLY ||
            iter_best < -MATE_SCORE + MAX_PLY) {
            break;
        }
    }

    *best = best_move;

    if (info != NULL) {
        info->depth = completed;
        info->score = best_score;
        info->nodes = s.nodes;
        info->millis = (int)((clock() - s.start) / ticks_per_ms());
    }

    return true;
}
