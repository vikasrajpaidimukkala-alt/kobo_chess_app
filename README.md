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

## Installing with NickelMenu

The app is launched from [NickelMenu](https://pgaskin.net/NickelMenu/), which
injects extra items into Nickel (Kobo's stock reader). On firmware 4.23 and
later — including the Libra Colour — the old top-left main menu is gone, and
those items appear as tabs on the **bottom-right of the home screen**. That
home-screen tab bar is what NickelMenu still calls `main`.

### 1. Install NickelMenu

The easiest way is [KoboPatch Web UI](https://kp.nicoverbruggen.be) ([source](https://github.com/nicoverbruggen/kobopatch-webui)): connect the Kobo over USB, choose NickelMenu, and let it write the files. Manual install:

1. Connect the Kobo over USB.
2. Download `KoboRoot.tgz` from the
   [NickelMenu releases](https://github.com/pgaskin/NickelMenu/releases).
3. Copy it into the hidden `.kobo` folder on the device
   (`KOBOeReader/.kobo/KoboRoot.tgz`). You may need to show hidden files.
4. Eject the reader and wait for it to reboot.

You should now see a **NickelMenu** tab on the home screen. The full
configuration reference is installed on the device as `.adds/nm/doc`, and is
also online in the
[NickelMenu documentation](https://github.com/pgaskin/NickelMenu/blob/master/res/doc).

### 2. Copy the chess files

With the Kobo mounted, or over SSH/USBNet:

```text
dist/kobochess/     ->  /mnt/onboard/.adds/kobochess/
dist/nm/kobochess   ->  /mnt/onboard/.adds/nm/kobochess
```

On a computer that is looking at the USB volume, that is:

```text
KOBOeReader/.adds/kobochess/     <-  dist/kobochess/
KOBOeReader/.adds/nm/kobochess   <-  dist/nm/kobochess
```

The launcher scripts must stay executable (`chmod 755` if you copied them
without permissions). Reboot once so NickelMenu reloads its config.

The shipped config is one line:

```
menu_item :main :Chess :cmd_spawn :quiet :exec /bin/sh /mnt/onboard/.adds/kobochess/kobochess.sh
```

`cmd_spawn` returns immediately so Nickel can be stopped by the launcher.
`quiet` hides the PID toast. After reboot, open the **NickelMenu** tab on the
home screen and tap **Chess**.

### 3. Optional: make Chess its own home-screen tab

By default Chess lives *inside* the NickelMenu tab. To relabel that tab so it
reads **Chess** on the home-screen bar, add these lines to the same file (or
to any other file in `.adds/nm/`):

```
experimental :menu_main_15505_label :Chess
```

Both the idle and active icons have to be set or neither shows. An 87×87 PNG
on a Libra Colour, stored on the device, works:

```
experimental :menu_main_15505_icon        :/mnt/onboard/.adds/kobochess/icon.png
experimental :menu_main_15505_icon_active :/mnt/onboard/.adds/kobochess/icon.png
```

This is an experimental NickelMenu option and may change across firmware. It
renames the NickelMenu tab itself; other NickelMenu items you have configured
will still appear when you open it.

### 4. Optional: also add Chess to the web browser menu

To have **Chess** in the overflow menu of Nickel's web browser as well as on
the home screen, add a second line:

```
menu_item :browser :Chess :cmd_spawn :quiet :exec /bin/sh /mnt/onboard/.adds/kobochess/kobochess.sh
```

Other NickelMenu locations (`reader`, `library`, `selection`) work the same
way if you want Chess from those menus too.

## Building for the Kobo

Kobo userspace is 32-bit ARM hard-float. Build with
[NickelTC](https://github.com/pgaskin/NickelTC) (`arm-nickel-linux-gnueabihf`),
the toolchain NickelMenu uses. A generic `arm-linux-gnueabihf-gcc` produces
binaries Nickel will not run. The display backend is
[FBInk](https://github.com/NiLuJe/FBInk), pulled in as a git submodule.

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

Then copy `dist/` onto the device as in [step 2](#2-copy-the-chess-files).

## Building without a device

```bash
make PLATFORM=host   # frames come out as PNG
make test            # chess rules and engine checks
```

See [CONTRIBUTING.md](CONTRIBUTING.md).

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

## Related software

- [NickelMenu](https://github.com/pgaskin/NickelMenu) — injects the Chess item
  into Nickel. Website: [pgaskin.net/NickelMenu](https://pgaskin.net/NickelMenu/)
- [FBInk](https://github.com/NiLuJe/FBInk) — talks to the e-ink panel on Kobo
- [NickelTC](https://github.com/pgaskin/NickelTC) — the cross compiler that
  produces binaries Nickel will actually run

## License

This project's own source is dedicated to the public domain under the
[Unlicense](https://unlicense.org/) — copy, modify, sell, or ignore it, no
attribution required. See [LICENSE](LICENSE).

The Kobo build statically links [FBInk](https://github.com/NiLuJe/FBInk), which
is [GPL-3.0-or-later](https://github.com/NiLuJe/FBInk/blob/master/LICENSE). A
binary you distribute that includes FBInk is therefore a GPL combined work:
you must provide its corresponding source, including this repository and the
FBInk version you linked. The host build (`make PLATFORM=host`) does not link
FBInk.

NickelMenu and NickelTC are separate programs you install yourself; they are
not part of this tree.
