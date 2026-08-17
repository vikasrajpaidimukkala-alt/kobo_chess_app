#ifndef KOBOCHESS_ENGINE_H
#define KOBOCHESS_ENGINE_H

#include <stdbool.h>

#include "chess.h"

#define ENGINE_OFF    0
#define ENGINE_EASY   1
#define ENGINE_MEDIUM 2
#define ENGINE_HARD   3

typedef struct {
    int max_depth;   /* iterative deepening ceiling */
    int max_millis;  /* wall-clock budget, 0 for no limit */
    int blunder_cp;  /* pick randomly among moves within this of best */
} EngineLimits;

typedef struct {
    int depth;       /* deepest iteration that finished */
    int score;       /* centipawns, from the moving side's view */
    long nodes;
    int millis;
} EngineInfo;

void engine_limits_for_level(int level, EngineLimits *out);
const char *engine_level_name(int level);

/*
 * Picks a move for the side to move. Returns false only when there is
 * no legal move (checkmate or stalemate). `info` may be NULL.
 */
bool engine_best_move(const Game *g, const EngineLimits *lim, Move *best,
                      EngineInfo *info);

#endif
