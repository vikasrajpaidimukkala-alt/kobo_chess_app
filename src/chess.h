#ifndef KOBOCHESS_CHESS_H
#define KOBOCHESS_CHESS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define CHESS_WHITE 0
#define CHESS_BLACK 1

#define PT_NONE   0
#define PT_PAWN   1
#define PT_KNIGHT 2
#define PT_BISHOP 3
#define PT_ROOK   4
#define PT_QUEEN  5
#define PT_KING   6

#define SQ_NONE 64

#define MF_NONE    0
#define MF_CAPTURE 1
#define MF_DOUBLE  2
#define MF_EP      4
#define MF_CASTLE  8
#define MF_PROMO   16

#define CASTLE_WK 1
#define CASTLE_WQ 2
#define CASTLE_BK 4
#define CASTLE_BQ 8

#define MAX_MOVES 256
#define MAX_HIST  512

typedef struct {
    uint8_t from;
    uint8_t to;
    uint8_t promo; /* PT_* or 0 */
    uint8_t flags;
} Move;

typedef struct {
    Move move;
    int8_t captured;
    uint8_t ep_old;
    uint8_t castle_old;
    uint8_t halfmove_old;
} Undo;

typedef enum {
    RESULT_NONE = 0,
    RESULT_CHECKMATE,
    RESULT_STALEMATE
} GameResult;

typedef struct {
    int8_t board[64];
    int side;          /* CHESS_WHITE or CHESS_BLACK */
    int ep;            /* 0-63 or SQ_NONE */
    uint8_t castle;
    uint8_t halfmove;
    uint16_t fullmove;
    Undo hist[MAX_HIST];
    int hist_len;
    GameResult result;
} Game;

static inline int sq_file(int sq)
{
    return sq & 7;
}

static inline int sq_rank(int sq)
{
    return sq >> 3;
}

static inline int make_sq(int file, int rank)
{
    return (rank << 3) | file;
}

static inline int piece_type(int8_t p)
{
    return p < 0 ? -p : p;
}

static inline int piece_color(int8_t p)
{
    if (p > 0) {
        return CHESS_WHITE;
    }
    if (p < 0) {
        return CHESS_BLACK;
    }
    return -1;
}

static inline int8_t make_piece(int type, int color)
{
    return (int8_t)(color == CHESS_WHITE ? type : -type);
}

void chess_init(Game *g);
bool chess_in_check(const Game *g, int color);
int chess_generate_legal(const Game *g, Move *out, int max);
int chess_moves_from(const Game *g, int from, Move *out, int max);
uint64_t chess_legal_mask(const Game *g, int from);
bool chess_find_move(const Game *g, int from, int to, int promo, Move *out);
bool chess_make(Game *g, Move m);
bool chess_undo(Game *g);
void chess_update_result(Game *g);
char chess_piece_letter(int8_t p);
void chess_status_text(const Game *g, char *buf, size_t n);

#endif
