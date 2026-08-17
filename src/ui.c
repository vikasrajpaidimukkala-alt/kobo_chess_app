#include "ui.h"

#include <stdio.h>
#include <string.h>

#define COL_BG_R  232
#define COL_BG_G  232
#define COL_BG_B  232

#define COL_LIGHT_R 220
#define COL_LIGHT_G 220
#define COL_LIGHT_B 220

#define COL_DARK_R  140
#define COL_DARK_G  140
#define COL_DARK_B  140

/*
 * Highlights darken a square relative to its own shade instead of
 * replacing it with a flat grey, so a highlighted light square stays
 * lighter than a plain dark one and the checker pattern survives.
 * Multiples of 17 so each tint is a whole GC16 grey level.
 */
#define TINT_SEL    51
#define TINT_LAST   34

#define COL_LEGAL_R 40
#define COL_LEGAL_G 40
#define COL_LEGAL_B 40

#define COL_TEXT_R  16
#define COL_TEXT_G  16
#define COL_TEXT_B  16

#define COL_WHITE_R 250
#define COL_WHITE_G 250
#define COL_WHITE_B 250

#define COL_BLACK_R 16
#define COL_BLACK_G 16
#define COL_BLACK_B 16

#define COL_EXIT_R  16
#define COL_EXIT_G  16
#define COL_EXIT_B  16

static int in_rect(Rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static Rect make_rect(int x, int y, int w, int h)
{
    Rect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static void vis_sq(const Ui *ui, int sq, int *file, int *rank)
{
    *file = sq_file(sq);
    *rank = sq_rank(sq);
    if (ui->flipped) {
        *file = 7 - *file;
        *rank = 7 - *rank;
    }
}

static void sq_origin(const Ui *ui, int sq, int *x, int *y)
{
    int f;
    int r;

    vis_sq(ui, sq, &f, &r);
    *x = ui->board.x + f * ui->square;
    *y = ui->board.y + (7 - r) * ui->square;
}

void ui_layout(Ui *ui)
{
    Display *d = ui->d;
    int margin = 20;
    int status_h = 88;
    int btn_h = 100;
    int gutter = 36;
    int avail_w;
    int avail_h;
    int sq;
    int board_w;
    int btn_y;
    int btn_w;
    int gap = 16;
    int i;
    int dlg_w;
    int dlg_h;

    avail_w = (int)d->width - 2 * margin - gutter;
    avail_h = (int)d->height - status_h - btn_h - 3 * margin - gutter;
    sq = avail_w / 8;
    if (avail_h / 8 < sq) {
        sq = avail_h / 8;
    }
    if (sq < 48) {
        sq = 48;
    }
    sq &= ~3;

    ui->square = sq;
    board_w = 8 * sq;
    ui->board = make_rect(((int)d->width - board_w - gutter) / 2 + gutter,
                          status_h + margin,
                          board_w, board_w);
    ui->board.x &= ~3;
    ui->board.y &= ~3;

    btn_y = (int)d->height - margin - btn_h;
    btn_w = ((int)d->width - 2 * margin - 3 * gap) / 4;
    ui->btn_exit = make_rect(margin, btn_y, btn_w, btn_h);
    ui->btn_undo = make_rect(margin + btn_w + gap, btn_y, btn_w, btn_h);
    ui->btn_reset = make_rect(margin + 2 * (btn_w + gap), btn_y, btn_w, btn_h);
    ui->btn_flip = make_rect(margin + 3 * (btn_w + gap), btn_y, btn_w, btn_h);

    dlg_w = (int)d->width * 4 / 5;
    dlg_h = 360;
    ui->dlg = make_rect(((int)d->width - dlg_w) / 2,
                        ((int)d->height - dlg_h) / 2,
                        dlg_w, dlg_h);
    ui->dlg_no = make_rect(ui->dlg.x + 24,
                           ui->dlg.y + dlg_h - 120,
                           dlg_w / 2 - 36, 88);
    ui->dlg_yes = make_rect(ui->dlg.x + dlg_w / 2 + 12,
                            ui->dlg.y + dlg_h - 120,
                            dlg_w / 2 - 36, 88);

    for (i = 0; i < 4; i++) {
        ui->promo[i] = make_rect(margin + i * (btn_w + gap), btn_y, btn_w, btn_h);
    }
}

static void fill_r(Display *d, Rect r, uint8_t cr, uint8_t cg, uint8_t cb)
{
    display_fill_rect(d, r.x, r.y, r.w, r.h, cr, cg, cb);
}

static void label_in(Display *d, Rect r, const char *text, int scale,
                     uint8_t cr, uint8_t cg, uint8_t cb)
{
    int tw = display_text_width(text, scale);
    int th = display_text_height(scale);
    int x = r.x + (r.w - tw) / 2;
    int y = r.y + (r.h - th) / 2;

    display_text(d, x, y, scale, text, cr, cg, cb);
}

static void draw_button(Display *d, Rect r, const char *text,
                        uint8_t br, uint8_t bg, uint8_t bb,
                        uint8_t tr, uint8_t tg, uint8_t tb)
{
    int scale;

    fill_r(d, r, br, bg, bb);
    display_fill_rect(d, r.x, r.y, r.w, 3, 16, 16, 16);
    display_fill_rect(d, r.x, r.y + r.h - 3, r.w, 3, 16, 16, 16);
    display_fill_rect(d, r.x, r.y, 3, r.h, 16, 16, 16);
    display_fill_rect(d, r.x + r.w - 3, r.y, 3, r.h, 16, 16, 16);

    scale = 4;
    if (display_text_width(text, scale) > r.w - 12) {
        scale = 3;
    }
    label_in(d, r, text, scale, tr, tg, tb);
}

static void piece_palette(int8_t piece, uint8_t *fr, uint8_t *fg, uint8_t *fb,
                          uint8_t *sr, uint8_t *sg, uint8_t *sb)
{
    if (piece_color(piece) == CHESS_WHITE) {
        *fr = COL_WHITE_R;
        *fg = COL_WHITE_G;
        *fb = COL_WHITE_B;
        *sr = COL_BLACK_R;
        *sg = COL_BLACK_G;
        *sb = COL_BLACK_B;
        return;
    }

    *fr = COL_BLACK_R;
    *fg = COL_BLACK_G;
    *fb = COL_BLACK_B;
    *sr = COL_WHITE_R;
    *sg = COL_WHITE_G;
    *sb = COL_WHITE_B;
}

static void orect(Display *d, int x, int y, int w, int h, int t,
                  uint8_t fr, uint8_t fg, uint8_t fb,
                  uint8_t sr, uint8_t sg, uint8_t sb)
{
    display_fill_rect(d, x - t, y - t, w + 2 * t, h + 2 * t, sr, sg, sb);
    display_fill_rect(d, x, y, w, h, fr, fg, fb);
}

static void draw_pawn(Display *d, int x, int y, int s,
                      uint8_t fr, uint8_t fg, uint8_t fb,
                      uint8_t sr, uint8_t sg, uint8_t sb)
{
    int t = s / 28 + 2;
    int cx = x + s / 2;
    int head_r = s / 7;
    int base_w = s / 2;
    int base_h = s / 6;
    int body_w = s / 4;
    int body_h = s / 4;

    orect(d, cx - base_w / 2, y + s - s / 6 - base_h, base_w, base_h, t,
          fr, fg, fb, sr, sg, sb);
    orect(d, cx - body_w / 2, y + s / 2, body_w, body_h, t,
          fr, fg, fb, sr, sg, sb);
    display_fill_circle(d, cx, y + s / 3, head_r + t, sr, sg, sb);
    display_fill_circle(d, cx, y + s / 3, head_r, fr, fg, fb);
}

static void draw_rook(Display *d, int x, int y, int s,
                      uint8_t fr, uint8_t fg, uint8_t fb,
                      uint8_t sr, uint8_t sg, uint8_t sb)
{
    int t = s / 28 + 2;
    int cx = x + s / 2;
    int body_w = s / 3;
    int merlon = s / 10;
    int i;

    orect(d, cx - s / 3, y + s - s / 5, (2 * s) / 3, s / 7, t,
          fr, fg, fb, sr, sg, sb);
    orect(d, cx - body_w / 2, y + s / 3, body_w, s / 2, t,
          fr, fg, fb, sr, sg, sb);
    for (i = 0; i < 3; i++) {
        int mx = cx - body_w / 2 + i * (body_w / 2) - merlon / 2;

        orect(d, mx, y + s / 5, merlon, s / 6, t, fr, fg, fb, sr, sg, sb);
    }
}

static void draw_bishop(Display *d, int x, int y, int s,
                        uint8_t fr, uint8_t fg, uint8_t fb,
                        uint8_t sr, uint8_t sg, uint8_t sb)
{
    int t = s / 28 + 2;
    int cx = x + s / 2;
    int rows;
    int span;
    int i;

    orect(d, cx - s / 3, y + s - s / 5, (2 * s) / 3, s / 8, t,
          fr, fg, fb, sr, sg, sb);

    rows = s / 3;
    span = (rows > 1) ? rows - 1 : 1;

    for (i = 0; i < rows; i++) {
        int w = 4 + ((s / 2 - 4) * i) / span;

        display_fill_rect(d, cx - w / 2 - t, y + s / 3 + i - t, w + 2 * t, 3,
                          sr, sg, sb);
        display_fill_rect(d, cx - w / 2, y + s / 3 + i, w, 2, fr, fg, fb);
    }

    display_fill_circle(d, cx, y + s / 4, s / 10 + t, sr, sg, sb);
    display_fill_circle(d, cx, y + s / 4, s / 10, fr, fg, fb);
    display_fill_rect(d, cx - 2, y + s / 3, 4, s / 5, sr, sg, sb);
}

static void draw_knight(Display *d, int x, int y, int s,
                        uint8_t fr, uint8_t fg, uint8_t fb,
                        uint8_t sr, uint8_t sg, uint8_t sb)
{
    int t = s / 28 + 2;
    int cx = x + s / 2;

    orect(d, cx - s / 3, y + s - s / 5, (2 * s) / 3, s / 8, t,
          fr, fg, fb, sr, sg, sb);
    orect(d, cx - s / 6, y + s / 3, s / 3, s / 2, t, fr, fg, fb, sr, sg, sb);
    orect(d, cx - s / 3, y + s / 4, s / 2, s / 4, t, fr, fg, fb, sr, sg, sb);
    display_fill_circle(d, cx - s / 8, y + s / 4, s / 8 + t, sr, sg, sb);
    display_fill_circle(d, cx - s / 8, y + s / 4, s / 8, fr, fg, fb);
    display_fill_rect(d, cx + s / 12, y + s / 5, s / 7, s / 10, fr, fg, fb);
}

static void draw_queen(Display *d, int x, int y, int s,
                       uint8_t fr, uint8_t fg, uint8_t fb,
                       uint8_t sr, uint8_t sg, uint8_t sb)
{
    int t = s / 28 + 2;
    int cx = x + s / 2;
    int i;

    orect(d, cx - s / 3, y + s - s / 5, (2 * s) / 3, s / 8, t,
          fr, fg, fb, sr, sg, sb);
    display_fill_circle(d, cx, y + s / 2, s / 5 + t, sr, sg, sb);
    display_fill_circle(d, cx, y + s / 2, s / 5, fr, fg, fb);

    for (i = -2; i <= 2; i++) {
        int px = cx + i * (s / 10);
        int py = y + s / 4 - ((i == 0) ? s / 16 : 0);

        display_fill_circle(d, px, py, s / 16 + t, sr, sg, sb);
        display_fill_circle(d, px, py, s / 16, fr, fg, fb);
    }
}

static void draw_king(Display *d, int x, int y, int s,
                      uint8_t fr, uint8_t fg, uint8_t fb,
                      uint8_t sr, uint8_t sg, uint8_t sb)
{
    int t = s / 28 + 2;
    int cx = x + s / 2;
    int cross = s / 7;

    orect(d, cx - s / 3, y + s - s / 5, (2 * s) / 3, s / 8, t,
          fr, fg, fb, sr, sg, sb);
    orect(d, cx - s / 5, y + s / 3, (2 * s) / 5, s / 2, t,
          fr, fg, fb, sr, sg, sb);
    orect(d, cx - cross / 2, y + s / 7, cross, s / 4, t,
          fr, fg, fb, sr, sg, sb);
    orect(d, cx - s / 7, y + s / 5, (2 * s) / 7, cross, t,
          fr, fg, fb, sr, sg, sb);
}

static void draw_piece(Display *d, int x, int y, int s, int8_t piece)
{
    uint8_t fr, fg, fb, sr, sg, sb;

    if (piece == 0) {
        return;
    }

    piece_palette(piece, &fr, &fg, &fb, &sr, &sg, &sb);

    switch (piece_type(piece)) {
    case PT_PAWN:
        draw_pawn(d, x, y, s, fr, fg, fb, sr, sg, sb);
        break;
    case PT_KNIGHT:
        draw_knight(d, x, y, s, fr, fg, fb, sr, sg, sb);
        break;
    case PT_BISHOP:
        draw_bishop(d, x, y, s, fr, fg, fb, sr, sg, sb);
        break;
    case PT_ROOK:
        draw_rook(d, x, y, s, fr, fg, fb, sr, sg, sb);
        break;
    case PT_QUEEN:
        draw_queen(d, x, y, s, fr, fg, fb, sr, sg, sb);
        break;
    case PT_KING:
        draw_king(d, x, y, s, fr, fg, fb, sr, sg, sb);
        break;
    default:
        break;
    }
}

static void square_color(const Ui *ui, int sq,
                         uint8_t *r, uint8_t *g, uint8_t *b)
{
    int light = ((sq_file(sq) + sq_rank(sq)) & 1) == 0;
    int shade = light ? COL_LIGHT_R : COL_DARK_R;

    if (ui->selected == sq) {
        shade -= TINT_SEL;
    } else if (sq == ui->last_from || sq == ui->last_to) {
        shade -= TINT_LAST;
    }

    if (shade < 0) {
        shade = 0;
    }

    *r = (uint8_t)shade;
    *g = (uint8_t)shade;
    *b = (uint8_t)shade;
}

static void draw_board(Ui *ui)
{
    Display *d = ui->d;
    Game *g = ui->game;
    int sq;
    const char *files = "abcdefgh";
    const char *ranks = "12345678";
    int i;
    int scale = (ui->square >= 120) ? 3 : 2;

    for (sq = 0; sq < 64; sq++) {
        int x;
        int y;
        uint8_t r, gc, b;

        sq_origin(ui, sq, &x, &y);
        square_color(ui, sq, &r, &gc, &b);
        display_fill_rect(d, x, y, ui->square, ui->square, r, gc, b);
        draw_piece(d, x, y, ui->square, g->board[sq]);

        if (ui->selected != SQ_NONE && (ui->legal & ((uint64_t)1 << sq))) {
            int rad = ui->square / 8;
            int cx = x + ui->square / 2;
            int cy = y + ui->square / 2;

            if (g->board[sq] != 0) {
                display_stroke_circle(d, cx, cy, ui->square / 2 - 6, 5,
                                      COL_LEGAL_R, COL_LEGAL_G, COL_LEGAL_B);
            } else {
                display_fill_circle(d, cx, cy, rad,
                                    COL_LEGAL_R, COL_LEGAL_G, COL_LEGAL_B);
            }
        }
    }

    for (i = 0; i < 8; i++) {
        int vis = ui->flipped ? (7 - i) : i;
        char fs[2];
        char rs[2];
        int lx;
        int ly;
        int ry;

        fs[0] = files[i];
        fs[1] = 0;
        rs[0] = ranks[i];
        rs[1] = 0;
        lx = ui->board.x + vis * ui->square + ui->square / 2 -
             display_text_width(fs, scale) / 2;
        ly = ui->board.y + ui->board.h + 6;
        ry = ui->board.y + (7 - vis) * ui->square + ui->square / 2 -
             display_text_height(scale) / 2;

        display_text(d, lx, ly, scale, fs, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
        display_text(d, ui->board.x - 8 * scale - 8, ry, scale, rs,
                     COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
    }
}

static void draw_dialog(Ui *ui, const char *title, const char *sub,
                        const char *yes_label)
{
    Display *d = ui->d;

    fill_r(d, ui->dlg, 250, 250, 250);
    display_fill_rect(d, ui->dlg.x, ui->dlg.y, ui->dlg.w, 4, 16, 16, 16);
    display_fill_rect(d, ui->dlg.x, ui->dlg.y + ui->dlg.h - 4, ui->dlg.w, 4,
                      16, 16, 16);
    display_fill_rect(d, ui->dlg.x, ui->dlg.y, 4, ui->dlg.h, 16, 16, 16);
    display_fill_rect(d, ui->dlg.x + ui->dlg.w - 4, ui->dlg.y, 4, ui->dlg.h,
                      16, 16, 16);

    label_in(d, make_rect(ui->dlg.x, ui->dlg.y + 40, ui->dlg.w, 80),
             title, 4, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
    label_in(d, make_rect(ui->dlg.x + 20, ui->dlg.y + 130, ui->dlg.w - 40, 80),
             sub, 3, 80, 80, 80);

    draw_button(d, ui->dlg_no, "Stay", 220, 220, 220, COL_TEXT_R, COL_TEXT_G,
                COL_TEXT_B);
    draw_button(d, ui->dlg_yes, yes_label, COL_EXIT_R, COL_EXIT_G, COL_EXIT_B,
                255, 255, 255);
}

void ui_draw(Ui *ui, bool full_flash)
{
    Display *d = ui->d;
    char status[128];
    int scale;

    display_clear(d, COL_BG_R, COL_BG_G, COL_BG_B);

    chess_status_text(ui->game, status, sizeof(status));
    scale = 4;
    if (display_text_width(status, scale) > (int)d->width - 40) {
        scale = 3;
    }
    display_text(d, 24, 28, scale, status, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);

    draw_board(ui);

    if (ui->mode == MODE_PROMOTE) {
        draw_button(d, ui->promo[0], "Queen", 40, 40, 40, 255, 255, 255);
        draw_button(d, ui->promo[1], "Rook", 40, 40, 40, 255, 255, 255);
        draw_button(d, ui->promo[2], "Bishop", 40, 40, 40, 255, 255, 255);
        draw_button(d, ui->promo[3], "Knight", 40, 40, 40, 255, 255, 255);
    } else {
        draw_button(d, ui->btn_exit, "EXIT", COL_EXIT_R, COL_EXIT_G, COL_EXIT_B,
                    255, 255, 255);
        draw_button(d, ui->btn_undo, "Undo", 48, 48, 48, 255, 255, 255);
        draw_button(d, ui->btn_reset, "Reset", 48, 48, 48, 255, 255, 255);
        draw_button(d, ui->btn_flip, "Flip", 48, 48, 48, 255, 255, 255);
    }

    display_text(d, 24, ui->btn_exit.y - 28, 2, "Page turn also exits.",
                 90, 90, 90);

    if (ui->mode == MODE_CONFIRM_EXIT) {
        draw_dialog(ui, "Exit to Nickel?", "Nickel will come back.", "Exit");
    } else if (ui->mode == MODE_CONFIRM_RESET) {
        draw_dialog(ui, "Start a new game?", "The current game will be lost.",
                    "Reset");
    }

    display_refresh_full(d, full_flash);
}

static int xy_to_sq(const Ui *ui, int x, int y)
{
    int f;
    int r;

    if (!in_rect(ui->board, x, y)) {
        return SQ_NONE;
    }

    f = (x - ui->board.x) / ui->square;
    r = 7 - (y - ui->board.y) / ui->square;
    if (f < 0 || f > 7 || r < 0 || r > 7) {
        return SQ_NONE;
    }
    if (ui->flipped) {
        f = 7 - f;
        r = 7 - r;
    }

    return make_sq(f, r);
}

UiHit ui_hit(const Ui *ui, int x, int y, int *square_out)
{
    int sq;

    if (square_out) {
        *square_out = SQ_NONE;
    }

    if (ui->mode == MODE_CONFIRM_EXIT || ui->mode == MODE_CONFIRM_RESET) {
        if (in_rect(ui->dlg_yes, x, y)) {
            return HIT_YES;
        }
        if (in_rect(ui->dlg_no, x, y)) {
            return HIT_NO;
        }
        return HIT_NONE;
    }

    if (ui->mode == MODE_PROMOTE) {
        if (in_rect(ui->promo[0], x, y)) {
            return HIT_PROMO_Q;
        }
        if (in_rect(ui->promo[1], x, y)) {
            return HIT_PROMO_R;
        }
        if (in_rect(ui->promo[2], x, y)) {
            return HIT_PROMO_B;
        }
        if (in_rect(ui->promo[3], x, y)) {
            return HIT_PROMO_N;
        }
        return HIT_NONE;
    }

    if (in_rect(ui->btn_exit, x, y)) {
        return HIT_EXIT;
    }
    if (in_rect(ui->btn_undo, x, y)) {
        return HIT_UNDO;
    }
    if (in_rect(ui->btn_reset, x, y)) {
        return HIT_RESET;
    }
    if (in_rect(ui->btn_flip, x, y)) {
        return HIT_FLIP;
    }

    sq = xy_to_sq(ui, x, y);
    if (sq != SQ_NONE) {
        if (square_out) {
            *square_out = sq;
        }
        return HIT_BOARD;
    }

    return HIT_NONE;
}
