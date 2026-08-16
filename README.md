# Kobo Chess

Two-player chess for the Kobo Libra Colour. Drawing uses the same hybrid path as `src/fb_direct_test.c`: pixels go straight into the framebuffer, FBInk is only used to open the device and issue e-ink refreshes. That avoids the lag people hit when they render a whole UI through `fbink_print`.

## Build on Fedora (NickelTC)

The Kobo userspace is 32-bit ARM hard-float. This project is built with [NickelTC](https://github.com/pgaskin/NickelTC) (`arm-nickel-linux-gnueabihf`), the same toolchain NickelMenu uses. Do not use Fedora’s generic `arm-linux-gnueabihf-gcc`.

```bash
git submodule update --init --recursive
```

If `arm-nickel-linux-gnueabihf-gcc` is already on your `PATH` (extracted NickelTC tarball, or a shell inside the image):

```bash
make
make package
```

If the tools live in a directory that is not on `PATH` (NickelMenu-style):

```bash
make CROSS_COMPILE=/path/to/nickeltc/bin/arm-nickel-linux-gnueabihf-
make CROSS_COMPILE=/path/to/nickeltc/bin/arm-nickel-linux-gnueabihf- package
```

To build inside the NickelTC container with Podman or Docker:

```bash
sudo dnf install podman git make   # or docker
./scripts/nickeltc-make.sh
./scripts/nickeltc-make.sh package
```

That wrapper uses `ghcr.io/pgaskin/nickeltc:1.0`, matching NickelMenu. Override with `NICKELTC_IMAGE=ghcr.io/pgaskin/nickeltc:1` if you want the rolling major tag.

Chess rules can be tested on the Fedora box without the cross compiler:

```bash
make host-test
```

## Install

With the Kobo mounted (or over SSH/USBNet):

```text
dist/kobochess/     ->  /mnt/onboard/.adds/kobochess/
dist/nm/kobochess   ->  /mnt/onboard/.adds/nm/kobochess
```

Scripts must be executable. Reboot once so NickelMenu loads the new item. Start it from Nickel's main menu: **Chess**.

## Playing

Tap a piece, then a highlighted square. Bottom bar: **EXIT**, **Undo**, **Reset**, **Flip** (so Black can play from their side). Promotion asks Queen / Rook / Bishop / Knight.

## Leaving without bricking the device

The red **EXIT** button is the normal way out. It asks for confirmation, then the launcher restarts Nickel.

Also safe:

- Either page-turn button (opens the same confirm dialog; press again to confirm)
- SIGTERM from SSH (`killall kobochess`)
- If Nickel cannot be restarted, the launcher **reboots** instead of leaving a dead screen

The power button is not grabbed, so a 15-second hold still force-reboots if something goes wrong.

Do not enable Wi-Fi or Bluetooth from the chess app. Reloading those MediaTek modules on a Libra Colour is a known kernel-panic path that can drop NickelMenu.

## Logs

`/mnt/onboard/.adds/kobochess/kobochess.log`
