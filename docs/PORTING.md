# Porting to another e-ink device

Everything device-specific lives behind one interface, `src/platform/platform.h`.
A port supplies one `Platform` struct and touches nothing else: the rules, the
search and the UI never learn what they are running on.

Two backends already exist and are worth reading before you start.
`src/platform/host` is about 250 lines and writes PNG files instead of driving
a panel. `src/platform/kobo` is the real one, for a Kobo Libra Colour.

## The interface

```c
typedef struct {
    const char *name;
    int  (*open)(PlatformInfo *info);
    void (*close)(void);
    void (*present)(const Canvas *canvas, RefreshMode mode);
    int  (*poll)(InputEvent *ev, int timeout_ms);
    void (*drain)(void);
} Platform;

const Platform *platform_get(void);
```

**`open`** brings up the display and input, then reports the drawable size in
`info`. The app allocates a `Canvas` of exactly that size. Return 0, or negative
if the device cannot be used.

**`present`** puts the canvas on screen. The `Canvas` is RGBA8888 with a known
row stride; convert to whatever your panel wants. It should return only once
the panel has settled, because the caller may immediately draw the next frame.
On e-ink, writing the framebuffer while a waveform is still running produces
torn frames, so most backends will want to wait on a completion fence here.

**`poll`** waits up to `timeout_ms` and returns 1 with an event, 0 on timeout,
or negative on error. Coordinates in `INPUT_TAP` are canvas pixels, so if your
touch panel is rotated or mirrored relative to the display, fix it here.
`INPUT_QUIT` covers a hardware key or anything else meaning "leave".

**`drain`** discards queued input. The app calls it after the engine has been
searching for several seconds so that taps aimed at the old position are not
replayed against the new one.

`RefreshMode` is named for intent rather than for any one controller's
waveforms: `REFRESH_FULL` for highest fidelity, `REFRESH_UI` for an ordinary
redraw, `REFRESH_FAST` for lowest latency. A backend may collapse all three
onto the same update; the Kobo one does, because partial updates ghost badly
on its Kaleido panel. Correctness never depends on the distinction.

## Steps

1. Create `src/platform/<device>/` with your implementation of the five
   functions and a `platform_get()` that returns them.
2. Add a case to the `Makefile` next to `kobo` and `host`, setting `CC`, any
   extra `CPPFLAGS`/`LDLIBS`, and `PLATFORM_SRCS`.
3. Build with `make PLATFORM=<device>`.

## Getting it working

Bring the display up before worrying about input. A useful first milestone is
filling the canvas with a solid colour and presenting it, then four squares in
the corners: if those land in the right places, your stride, pixel format and
rotation are all correct.

If the picture is wrong, find out whether the bytes leaving the program are
wrong or the panel is mangling them. The Kobo backend answers that with
`KOBOCHESS_DUMP=1`, which writes the canvas and a post-refresh readback of the
framebuffer as PGM files, and reports how many scanlines differ. That
distinction is worth reproducing: a whole class of "the display driver is
broken" bugs turn out to be ordinary drawing bugs, and a dump settles it in
seconds.

Since the UI is pure, you can also render any screen with no hardware at all:

```bash
make PLATFORM=host
printf 'tap 722 1070\ntap 722 774\nquit\n' > taps.txt
KOBOCHESS_SCRIPT=taps.txt KOBOCHESS_OUT=/tmp/frames ./build/host/kobochess
```

Set `KOBOCHESS_SIZE=WIDTHxHEIGHT` to check your device's geometry before the
hardware works. The UI lays itself out from the canvas size, but it has only
been exercised at 1264x1680; layout bug reports at other sizes are welcome.

## Notes from the Kobo port

These cost real debugging time and may generalise.

- All refreshes flash. Partial updates on Kaleido 3 leave grey ghosts of the
  previous position.
- Do not set `HWTCON_FLAG_CFA_SKIP` at 32bpp. The driver then picks the wrong
  working buffer.
- `clock_t` is 32-bit. Computing a deadline as `millis * CLOCKS_PER_SEC / 1000`
  overflows before five seconds, so budgets are kept in ticks and compared as
  elapsed differences.
- The engine's time budget matters more than its depth. Iterative deepening
  keeps whatever iteration finished, so a slow device loses depth rather than
  making the player wait. Tune `engine_limits_for_level` in
  `src/chess/engine.c` against `make engine-bench` compiled for your device.
