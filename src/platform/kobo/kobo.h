#ifndef KOBOCHESS_KOBO_H
#define KOBOCHESS_KOBO_H

#include "fbink.h"
#include "platform/platform.h"

/*
 * Internal seam between the two halves of the Kobo backend. Nothing
 * outside src/platform/kobo should include this.
 */

int kobo_display_open(PlatformInfo *info);
void kobo_display_close(void);
void kobo_display_present(const Canvas *canvas, RefreshMode mode);

/* Touch axis quirks are reported by FBInk alongside the panel info. */
const FBInkState *kobo_display_state(void);

int kobo_input_open(unsigned int width, unsigned int height);
void kobo_input_close(void);
int kobo_input_poll(InputEvent *ev, int timeout_ms);
void kobo_input_drain(void);

#endif
