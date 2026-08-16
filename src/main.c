#include "chess.h"
#include "display.h"
#include "input.h"
#include "ui.h"

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

int main(void)
{
    Display display;
    Game game;
    Ui ui;
    int rc = 0;

    install_signals();

    fprintf(stderr, "Kobo Chess starting (pid %d)\n", getpid());

    if (display_init(&display) != 0) {
        return 1;
    }

    if (input_init(&display) != 0) {
        fprintf(stderr, "Input init failed; EXIT still works via signals.\n");
    }

    chess_init(&game);

    memset(&ui, 0, sizeof(ui));
    ui.d = &display;
    ui.game = &game;
    ui.selected = SQ_NONE;
    ui.last_from = SQ_NONE;
    ui.last_to = SQ_NONE;
    ui.pending_from = SQ_NONE;
    ui.pending_to = SQ_NONE;
    ui_layout(&ui);
    ui_draw(&ui, true);

    while (!g_quit_now) {
        InputEvent ev;
        int pr;

        pr = input_poll(&ev, 400);
        if (pr < 0) {
            break;
        }
        if (pr == 0) {
            continue;
        }

        if (ev.kind == INP_EXIT_KEY) {
            if (ui.mode == MODE_CONFIRM_EXIT) {
                break;
            }
            ui.mode = MODE_CONFIRM_EXIT;
            ui_draw(&ui, false);
            continue;
        }

        if (ev.kind == INP_TAP) {
            int sq = SQ_NONE;
            UiHit hit = ui_hit(&ui, ev.x, ev.y, &sq);

            fprintf(stderr, "tap %d,%d hit=%d sq=%d\n", ev.x, ev.y, (int)hit, sq);
            if (handle_hit(&ui, hit, sq)) {
                break;
            }
            ui_draw(&ui, false);
        }
    }

    fprintf(stderr, "Exiting chess; releasing input and framebuffer\n");
    input_close();
    display_close(&display);
    return rc;
}
