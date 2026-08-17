#ifndef KOBOCHESS_UI_H
#define KOBOCHESS_UI_H

#include <stdbool.h>

#include "chess/chess.h"
#include "gfx/canvas.h"

typedef enum {
    MODE_PLAY = 0,
    MODE_CONFIRM_EXIT,
    MODE_CONFIRM_RESET,
    MODE_PROMOTE
} UiMode;

typedef enum {
    HIT_NONE = 0,
    HIT_BOARD,
    HIT_EXIT,
    HIT_UNDO,
    HIT_RESET,
    HIT_FLIP,
    HIT_LEVEL,
    HIT_YES,
    HIT_NO,
    HIT_PROMO_Q,
    HIT_PROMO_R,
    HIT_PROMO_B,
    HIT_PROMO_N
} UiHit;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Rect;

typedef struct {
    Canvas *canvas;
    Game *game;
    UiMode mode;
    int selected;      /* SQ_NONE or 0-63 */
    uint64_t legal;
    int last_from;
    int last_to;
    bool flipped;
    int pending_from;
    int pending_to;
    int ai_level;   /* ENGINE_OFF, or a difficulty the computer plays at */
    int ai_color;   /* colour the computer owns while ai_level is on */
    Rect board;
    int square;
    Rect btn_exit;
    Rect btn_undo;
    Rect btn_reset;
    Rect btn_flip;
    Rect btn_level;
    Rect dlg;
    Rect dlg_yes;
    Rect dlg_no;
    Rect promo[4];
} Ui;

void ui_layout(Ui *ui);

/* Draws the whole screen into ui->canvas. Presenting is the caller's job. */
void ui_render(Ui *ui);

UiHit ui_hit(const Ui *ui, int x, int y, int *square_out);

/* True when the computer owns the side to move and the game is live. */
bool ui_ai_to_move(const Ui *ui);

#endif
