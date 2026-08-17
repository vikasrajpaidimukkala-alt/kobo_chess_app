#ifndef KOBOCHESS_INPUT_H
#define KOBOCHESS_INPUT_H

#include <stdbool.h>

#include "display.h"

typedef enum {
    INP_NONE = 0,
    INP_TAP,
    INP_EXIT_KEY
} InputKind;

typedef struct {
    InputKind kind;
    int x;
    int y;
} InputEvent;

int input_init(const Display *d);
void input_close(void);
int input_poll(InputEvent *ev, int timeout_ms);
void input_drain(void);

#endif
