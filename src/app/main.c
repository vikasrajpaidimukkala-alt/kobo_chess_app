#include "chess/chess.h"
#include "chess/engine.h"
#include "gfx/canvas.h"
#include "platform/platform.h"
#include "ui/ui.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_quit_now;

static void on_signal(int sig)
{
    (void)sig;
    g_quit_now = 1;
}

static void install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

static void reset_ui_game(Ui *ui)
{
    chess_init(ui->game);
    ui->mode = MODE_PLAY;
    ui->selected = SQ_NONE;
    ui->legal = 0;
    ui->last_from = SQ_NONE;
    ui->last_to = SQ_NONE;
    ui->pending_from = SQ_NONE;
    ui->pending_to = SQ_NONE;
}

/*
 * The computer takes the side away from the player, i.e. the one at
 * the top of the board as it is currently oriented. It is pinned when
 * the level is chosen so that a later Flip, which people use just to
 * look at the position, cannot hand over their own pieces.
 */
static void cycle_level(Ui *ui)
{
    ui->ai_level = (ui->ai_level + 1) % (ENGINE_HARD + 1);
    ui->ai_color = ui->flipped ? CHESS_WHITE : CHESS_BLACK;
    ui->selected = SQ_NONE;
    ui->legal = 0;
}

static bool apply_move(Ui *ui, Move m)
{
    if (!chess_make(ui->game, m)) {
        return false;
    }

    ui->last_from = m.from;
    ui->last_to = m.to;
    ui->selected = SQ_NONE;
    ui->legal = 0;
    ui->pending_from = SQ_NONE;
    ui->pending_to = SQ_NONE;
    ui->mode = MODE_PLAY;
    chess_update_result(ui->game);
    return true;
}

static void play_engine_move(Ui *ui)
{
    EngineLimits lim;
    EngineInfo info;
    Move m;

    engine_limits_for_level(ui->ai_level, &lim);
    if (!engine_best_move(ui->game, &lim, &m, &info)) {
        return;
    }

    fprintf(stderr, "engine %s: depth %d score %+d %ld nodes %d ms\n",
            engine_level_name(ui->ai_level), info.depth, info.score,
            info.nodes, info.millis);

    apply_move(ui, m);
}

static bool try_move(Ui *ui, int from, int to, int promo)
{
    Move m;

    if (!chess_find_move(ui->game, from, to, promo, &m)) {
        return false;
    }

    if ((m.flags & MF_PROMO) && promo == 0) {
        ui->mode = MODE_PROMOTE;
        ui->pending_from = from;
        ui->pending_to = to;
        return true;
    }

    return apply_move(ui, m);
}

static void handle_board_tap(Ui *ui, int sq)
{
    int8_t piece;

    if (ui->game->result != RESULT_NONE) {
        return;
    }

    /* The board is read-only while the computer owns the move. */
    if (ui_ai_to_move(ui)) {
        return;
    }

    if (ui->selected != SQ_NONE && (ui->legal & ((uint64_t)1 << sq))) {
        try_move(ui, ui->selected, sq, 0);
        return;
    }

    piece = ui->game->board[sq];
    if (piece != 0 && piece_color(piece) == ui->game->side) {
        ui->selected = sq;
        ui->legal = chess_legal_mask(ui->game, sq);
        return;
    }

    ui->selected = SQ_NONE;
    ui->legal = 0;
}

static int handle_hit(Ui *ui, UiHit hit, int sq)
{
    switch (hit) {
    case HIT_EXIT:
        ui->mode = MODE_CONFIRM_EXIT;
        break;
    case HIT_YES:
        if (ui->mode == MODE_CONFIRM_EXIT) {
            return 1;
        }
        if (ui->mode == MODE_CONFIRM_RESET) {
            reset_ui_game(ui);
        }
        break;
    case HIT_NO:
        ui->mode = MODE_PLAY;
        break;
    case HIT_UNDO:
        if (chess_undo(ui->game)) {
            /*
             * Against the computer one undo would only hand the move
             * straight back to it, so take the reply off too.
             */
            chess_update_result(ui->game);
            if (ui_ai_to_move(ui)) {
                chess_undo(ui->game);
            }

            ui->selected = SQ_NONE;
            ui->legal = 0;
            if (ui->game->hist_len > 0) {
                Move last = ui->game->hist[ui->game->hist_len - 1].move;

                ui->last_from = last.from;
                ui->last_to = last.to;
            } else {
                ui->last_from = SQ_NONE;
                ui->last_to = SQ_NONE;
            }
            chess_update_result(ui->game);
        }
        break;
    case HIT_RESET:
        ui->mode = MODE_CONFIRM_RESET;
        break;
    case HIT_FLIP:
        ui->flipped = !ui->flipped;
        break;
    case HIT_LEVEL:
        cycle_level(ui);
        break;
    case HIT_BOARD:
        handle_board_tap(ui, sq);
        break;
    case HIT_PROMO_Q:
        try_move(ui, ui->pending_from, ui->pending_to, PT_QUEEN);
        break;
    case HIT_PROMO_R:
        try_move(ui, ui->pending_from, ui->pending_to, PT_ROOK);
        break;
    case HIT_PROMO_B:
        try_move(ui, ui->pending_from, ui->pending_to, PT_BISHOP);
        break;
    case HIT_PROMO_N:
        try_move(ui, ui->pending_from, ui->pending_to, PT_KNIGHT);
        break;
    case HIT_NONE:
    default:
        break;
    }

    return 0;
}

static void show(const Platform *plat, Ui *ui, RefreshMode mode)
{
    ui_render(ui);
    plat->present(ui->canvas, mode);
}

int main(void)
{
    const Platform *plat = platform_get();
    PlatformInfo info;
    Canvas canvas;
    Game game;
    Ui ui;

    install_signals();

    fprintf(stderr, "Kobo Chess starting on '%s' (pid %d)\n",
            plat->name, getpid());

    if (plat->open(&info) != 0) {
        return 1;
    }

    if (!canvas_init(&canvas, info.width, info.height)) {
        fprintf(stderr, "Cannot allocate a %ux%u canvas\n",
                info.width, info.height);
        plat->close();
        return 1;
    }

    chess_init(&game);

    memset(&ui, 0, sizeof(ui));
    ui.canvas = &canvas;
    ui.game = &game;
    ui.selected = SQ_NONE;
    ui.last_from = SQ_NONE;
    ui.last_to = SQ_NONE;
    ui.pending_from = SQ_NONE;
    ui.pending_to = SQ_NONE;
    ui.ai_level = ENGINE_OFF;
    ui.ai_color = CHESS_BLACK;
    ui_layout(&ui);
    show(plat, &ui, REFRESH_FULL);

    while (!g_quit_now) {
        InputEvent ev;
        int pr;

        /*
         * The paint from the previous pass already told the player the
         * computer is thinking, so search here and repaint the reply.
         */
        if (ui_ai_to_move(&ui) && ui.mode == MODE_PLAY) {
            play_engine_move(&ui);
            plat->drain();
            show(plat, &ui, REFRESH_UI);
            continue;
        }

        pr = plat->poll(&ev, 400);
        if (pr < 0) {
            break;
        }
        if (pr == 0) {
            continue;
        }

        if (ev.kind == INPUT_QUIT) {
            if (ui.mode == MODE_CONFIRM_EXIT) {
                break;
            }
            ui.mode = MODE_CONFIRM_EXIT;
            show(plat, &ui, REFRESH_UI);
            continue;
        }

        if (ev.kind == INPUT_TAP) {
            int sq = SQ_NONE;
            UiHit hit = ui_hit(&ui, ev.x, ev.y, &sq);

            fprintf(stderr, "tap %d,%d hit=%d sq=%d\n", ev.x, ev.y, (int)hit, sq);
            if (handle_hit(&ui, hit, sq)) {
                break;
            }
            show(plat, &ui, REFRESH_UI);
        }
    }

    fprintf(stderr, "Exiting chess; releasing the panel and input\n");
    canvas_free(&canvas);
    plat->close();
    return 0;
}
