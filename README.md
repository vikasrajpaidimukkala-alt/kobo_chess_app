# Kobo Chess

Chess for e-ink readers, with a two-player mode and a small built-in engine.
Developed on a **Kobo Libra Colour**, which is the reference device, but the
device-specific code is confined to one backend so other readers can be added
without touching the game. See [docs/PORTING.md](docs/PORTING.md).

## Playing

Tap a piece, then a highlighted square. The bottom bar is **EXIT**, **Undo**,
**Reset**, **Flip** (so Black can play from their side) and an opponent button
that cycles **2 Player → Easy → Medium → Hard**.

With an opponent selected the computer takes the side away from you, meaning
the one at the top of the board as it is currently oriented. That choice is
pinned when you pick the level, so pressing Flip later to study the position
will not hand your own pieces to the engine. Undo takes back two moves when you
are playing the computer, so you get your own move back rather than handing it
straight to the engine.

Promotion asks Queen / Rook / Bishop / Knight.

The engine is alpha-beta negamax with iterative deepening, a quiescence search,
and a material plus piece-square evaluation. Each level pairs a depth with a
time budget, and the budget always wins: on a slow reader it loses depth rather
than making you wait.

## Building for the Kobo

Kobo userspace is 32-bit ARM hard-float. Build with
[NickelTC](https://github.com/pgaskin/NickelTC) (`arm-nickel-linux-gnueabihf`),
the toolchain NickelMenu uses. A generic `arm-linux-gnueabihf-gcc` produces
binaries Nickel will not run.

```bash
git submodule update --init --recursive
make
make package
```

If the toolchain is not on `PATH`:

```bash
make CROSS_COMPILE=/path/to/nickeltc/bin/arm-nickel-linux-gnueabihf-
```

Or build in the container NickelMenu uses, which needs only podman or docker:

```bash
./scripts/nickeltc-make.sh
./scripts/nickeltc-make.sh package
```

## Building without a device

```bash
make PLATFORM=host   # frames come out as PNG
make test            # chess rules and engine checks
```

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Installing

With the Kobo mounted, or over SSH/USBNet:

```text
dist/kobochess/     ->  /mnt/onboard/.adds/kobochess/
dist/nm/kobochess   ->  /mnt/onboard/.adds/nm/kobochess
```

Scripts must stay executable. Reboot once so NickelMenu picks up the new item,
then start it from Nickel's main menu: **Chess**.

## Leaving without bricking the device

The red **EXIT** button is the normal way out. It asks for confirmation, then
the launcher restarts Nickel. Also safe:

- Either page-turn button, which opens the same dialog; press again to confirm
- `killall kobochess` over SSH
- If Nickel cannot be restarted, the launcher reboots rather than leave you
  with a dead screen

The power button is not grabbed, so a 15-second hold still force-reboots.

Do not enable Wi-Fi or Bluetooth from inside the app. Reloading those MediaTek
modules on a Libra Colour is a known kernel-panic path that can take NickelMenu
with it.

## Logs

`/mnt/onboard/.adds/kobochess/kobochess.log`

For display problems, `KOBOCHESS_DUMP=1` additionally writes the drawn canvas
and a readback of the framebuffer, which distinguishes a drawing bug from a
device one.

## Layout

```
src/chess/      rules and search, no I/O
src/gfx/        software rasteriser onto an RGBA canvas
src/ui/         board, pieces, buttons, hit-testing
src/app/        game loop
src/platform/   kobo/ and host/ backends
```
