/*
 * Engine sanity checks and a speed probe.
 *
 * Runs on the host with `make engine-bench`. It can also be cross
 * compiled and run on the Kobo to measure the real search rate, which
 * is what the per-level depth and time budgets are tuned against.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess/chess.h"
#include "chess/engine.h"

static void move_text(Move m, char *buf, size_t n)
{
    snprintf(buf, n, "%c%c%c%c%s",
             'a' + sq_file(m.from), '1' + sq_rank(m.from),
             'a' + sq_file(m.to), '1' + sq_rank(m.to),
             (m.flags & MF_PROMO) ? "=Q" : "");
}

static int failures;

static void check(bool ok, const char *what)
{
    printf("  %-40s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        failures++;
    }
}

static void bench_levels(void)
{
    int level;

    printf("Start position, one search per level:\n");
    for (level = ENGINE_EASY; level <= ENGINE_HARD; level++) {
        Game g;
        EngineLimits lim;
        EngineInfo info;
        Move m;
        char buf[16];

        chess_init(&g);
        engine_limits_for_level(level, &lim);
        if (!engine_best_move(&g, &lim, &m, &info)) {
            printf("  %-8s no move\n", engine_level_name(level));
            continue;
        }

        move_text(m, buf, sizeof(buf));
        printf("  %-8s %-6s depth %d  score %+5d  %8ld nodes  %5d ms  %ld n/s\n",
               engine_level_name(level), buf, info.depth, info.score,
               info.nodes, info.millis,
               info.millis > 0 ? info.nodes * 1000 / info.millis : 0L);
    }
}

static void test_mate_in_one(void)
{
    Game g;
    EngineLimits lim;
    Move m;

    chess_init(&g);
    memset(g.board, 0, sizeof(g.board));
    g.board[0] = make_piece(PT_ROOK, CHESS_WHITE);   /* a1 */
    g.board[7] = make_piece(PT_KING, CHESS_WHITE);   /* h1 */
    g.board[63] = make_piece(PT_KING, CHESS_BLACK);  /* h8 */
    g.board[54] = make_piece(PT_PAWN, CHESS_BLACK);  /* g7 */
    g.board[55] = make_piece(PT_PAWN, CHESS_BLACK);  /* h7 */
    g.side = CHESS_WHITE;
    g.castle = 0;
    g.ep = SQ_NONE;

    engine_limits_for_level(ENGINE_MEDIUM, &lim);
    lim.blunder_cp = 0;

    printf("Tactics:\n");
    if (!engine_best_move(&g, &lim, &m, NULL)) {
        check(false, "back-rank mate in one: found a move");
        return;
    }

    check(m.from == 0 && m.to == 56, "back-rank mate in one: Ra8#");
}

static void test_takes_hanging_queen(void)
{
    Game g;
    EngineLimits lim;
    Move m;

    chess_init(&g);
    memset(g.board, 0, sizeof(g.board));
    g.board[4] = make_piece(PT_KING, CHESS_WHITE);    /* e1 */
    g.board[60] = make_piece(PT_KING, CHESS_BLACK);   /* e8 */
    g.board[8] = make_piece(PT_ROOK, CHESS_WHITE);    /* a2 */
    g.board[16] = make_piece(PT_QUEEN, CHESS_BLACK);  /* a3, undefended */
    g.side = CHESS_WHITE;
    g.castle = 0;
    g.ep = SQ_NONE;

    engine_limits_for_level(ENGINE_MEDIUM, &lim);
    lim.blunder_cp = 0;

    if (!engine_best_move(&g, &lim, &m, NULL)) {
        check(false, "wins the hanging queen: found a move");
        return;
    }

    check(m.from == 8 && m.to == 16, "wins the hanging queen: Rxa3");
}

/*
 * The Kobo is slow enough that the time budget, not the depth, is what
 * usually ends a search. Even an impossible budget has to come back
 * with a legal move rather than nothing.
 */
static void test_tight_budget(void)
{
    Game g;
    EngineLimits lim;
    Move m;
    Move legal[MAX_MOVES];
    int n;
    int i;
    bool ok = false;

    chess_init(&g);
    engine_limits_for_level(ENGINE_HARD, &lim);
    lim.max_millis = 1;

    if (!engine_best_move(&g, &lim, &m, NULL)) {
        check(false, "1 ms budget still returns a move");
        return;
    }

    n = chess_generate_legal(&g, legal, MAX_MOVES);
    for (i = 0; i < n; i++) {
        if (legal[i].from == m.from && legal[i].to == m.to) {
            ok = true;
            break;
        }
    }

    check(ok, "1 ms budget still returns a legal move");
}

static void selfplay(int level, int plies)
{
    Game g;
    EngineLimits lim;
    long total_nodes = 0;
    int total_ms = 0;
    int worst_ms = 0;
    int played = 0;
    int i;

    chess_init(&g);
    engine_limits_for_level(level, &lim);

    printf("Self-play, %s, %d plies:\n", engine_level_name(level), plies);

    for (i = 0; i < plies; i++) {
        EngineInfo info;
        Move m;
        Move legal[MAX_MOVES];
        int n;
        int j;
        bool found = false;

        chess_update_result(&g);
        if (g.result != RESULT_NONE) {
            printf("  game ended after %d plies\n", played);
            break;
        }

        if (!engine_best_move(&g, &lim, &m, &info)) {
            break;
        }

        n = chess_generate_legal(&g, legal, MAX_MOVES);
        for (j = 0; j < n; j++) {
            if (legal[j].from == m.from && legal[j].to == m.to &&
                legal[j].promo == m.promo) {
                found = true;
                break;
            }
        }
        if (!found) {
            check(false, "engine returned a legal move");
            return;
        }

        chess_make(&g, m);
        total_nodes += info.nodes;
        total_ms += info.millis;
        if (info.millis > worst_ms) {
            worst_ms = info.millis;
        }
        played++;
    }

    if (played > 0) {
        printf("  %d moves, avg %d ms, worst %d ms, %ld nodes total\n",
               played, total_ms / played, worst_ms, total_nodes);
    }
    check(true, "self-play produced only legal moves");
}

int main(void)
{
    bench_levels();
    printf("\n");
    test_mate_in_one();
    test_takes_hanging_queen();
    test_tight_budget();
    printf("\n");
    selfplay(ENGINE_MEDIUM, 30);

    printf("\n%s\n", failures == 0 ? "engine bench: all checks passed"
                                   : "engine bench: FAILURES");
    return failures == 0 ? 0 : 1;
}
