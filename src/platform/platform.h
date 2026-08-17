#ifndef KOBOCHESS_PLATFORM_H
#define KOBOCHESS_PLATFORM_H

#include <stdbool.h>

#include "gfx/canvas.h"

/*
 * The whole device-specific surface of this program.
 *
 * A port supplies one Platform and nothing else: the rules, the
 * engine and the UI never learn what they are running on. See
 * docs/PORTING.md for a walkthrough, and src/platform/host for a
 * backend small enough to read in one sitting.
 */

typedef enum {
    /*
     * Named for intent, not for any one controller's waveforms, so a
     * backend can map them onto whatever its panel actually offers.
     * A backend may treat all three the same; correctness never
     * depends on the distinction.
     */
    REFRESH_FULL = 0, /* highest fidelity, may flash, clears ghosting */
    REFRESH_UI,       /* ordinary redraw */
    REFRESH_FAST      /* lowest latency, fidelity may suffer */
} RefreshMode;

typedef enum {
    INPUT_NONE = 0,
    INPUT_TAP,   /* x, y in canvas pixels */
    INPUT_QUIT   /* hardware key or signal asking to leave */
} InputKind;

typedef struct {
    InputKind kind;
    int x;
    int y;
} InputEvent;

typedef struct {
    unsigned int width;
    unsigned int height;
} PlatformInfo;

typedef struct {
    const char *name;

    /*
     * Bring up display and input, and report the drawable size. The
     * caller allocates a Canvas of exactly that size. Returns 0 on
     * success, negative on failure.
     */
    int (*open)(PlatformInfo *info);

    /* Release the panel and input devices. Must be safe to call once. */
    void (*close)(void);

    /*
     * Put the canvas on screen. Expected to return only once the panel
     * has settled, so the caller may immediately draw the next frame.
     */
    void (*present)(const Canvas *canvas, RefreshMode mode);

    /*
     * Wait up to timeout_ms for input.
     * Returns 1 with *ev filled in, 0 on timeout, negative on error.
     */
    int (*poll)(InputEvent *ev, int timeout_ms);

    /* Discard queued input, e.g. taps made while the engine searched. */
    void (*drain)(void);
} Platform;

/* Supplied by whichever backend was compiled in. */
const Platform *platform_get(void);

#endif
