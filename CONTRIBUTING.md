# Contributing

## Getting a build without a device

Everything except the display and input backend is portable, so you can build
and see the UI on your own machine:

```bash
make PLATFORM=host
printf 'tap 722 1070\ntap 722 774\nquit\n' > taps.txt
KOBOCHESS_SCRIPT=taps.txt KOBOCHESS_OUT=/tmp/frames ./build/host/kobochess
```

Each screen is written to `/tmp/frames/frame-NNNN.png`. The rules and the
engine need no backend at all:

```bash
make test
```

## Layout

```
src/chess/      rules and the search engine, no I/O of any kind
src/gfx/        software rasteriser onto an RGBA Canvas
src/ui/         draws the game into a Canvas, and hit-tests taps
src/app/        the game loop
src/platform/   one directory per device; the only device-aware code
tests/          rules tests and the engine bench
```

The dependency rule is one-way: `platform` may use `gfx`, and `app` may use
everything, but nothing under `chess`, `gfx` or `ui` may include a platform
header. That is what makes a new device a self-contained addition rather than
a change spread across the tree.

Adding a device is documented in [docs/PORTING.md](docs/PORTING.md).

## Style

The existing code is C11, four-space indent, no tabs, and declarations at the
top of a block. It builds clean under `-Wall -Wextra`; please keep it that way.

Comment sparingly, and only for things the code cannot say itself: a hardware
quirk, a constraint, a reason to avoid the obvious approach. Do not add
comments narrating what the next line does.

## Testing a UI change

The renderer is deterministic, so screenshots are a real regression test. If
you are changing drawing code, render the screens before and after and compare
them; identical output means the change is safe, and a diff tells you exactly
what moved. The host backend exists largely for this.

Please say which device and firmware you tested on. A change that is correct
on a Libra Colour can easily be wrong on a different panel.

## Reporting a display bug

Include the log, and if the image itself is wrong, the framebuffer dumps:

```bash
KOBOCHESS_DUMP=1 ./kobochess
```

That writes `dump_canvas.pgm` (what the program drew) and `dump_fb.pgm` (what
was in the framebuffer after the refresh) next to the binary, and logs how many
scanlines differ. It tells us in one step whether the bug is in the drawing
code or in the device path, which is otherwise very hard to tell apart.
