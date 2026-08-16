# Draw into a packed RGBA bitmap, copy into the native framebuffer, then
# full-screen refresh (0x0 region). Do not use fbink_print_raw_data on
# Libra Colour: it software-rotates an already-portrait buffer.
#
# Built with NickelTC (arm-nickel-linux-gnueabihf). See `make help`.

# Triple used inside ghcr.io/pgaskin/nickeltc and a host-extracted NickelTC.
CROSS_TC ?= arm-nickel-linux-gnueabihf

# NickelMenu-style prefix, including a path if the tools are not on PATH:
#   make CROSS_COMPILE=/path/to/nickeltc/bin/arm-nickel-linux-gnueabihf-
ifdef CROSS_COMPILE
CC          := $(CROSS_COMPILE)gcc
STRIP       := $(CROSS_COMPILE)strip
FBINK_CROSS := CROSS_COMPILE=$(CROSS_COMPILE)
else
CC          := $(CROSS_TC)-gcc
STRIP       := $(CROSS_TC)-strip
FBINK_CROSS := CROSS_TC=$(CROSS_TC)
endif

# Flags from the NickelTC README. Thumb armv7hf matches Kobo userspace,
# including Libra Colour (the kernel is A53; libc is still this ABI).
NICKEL_CFLAGS := -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard -mthumb

FBINK_DIR := third_party/FBInk
FBINK_LIB := $(FBINK_DIR)/Release/libfbink.a

CPPFLAGS := -I$(FBINK_DIR) -I$(FBINK_DIR)/Release
override CFLAGS += -Wall -Wextra -O2 -std=gnu11 $(NICKEL_CFLAGS)
LDFLAGS  :=
LDLIBS   := $(FBINK_LIB) -lm -ldl

BUILD := build
APP   := $(BUILD)/kobochess

SRCS := src/main.c src/chess.c src/display.c src/input.c src/ui.c
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

.PHONY: all app tests host-test clean distclean package help fbink

all: app tests

app: $(APP)

fbink: $(FBINK_LIB)

$(FBINK_DIR)/fbink.h:
	git submodule update --init --recursive

$(FBINK_LIB): $(FBINK_DIR)/fbink.h Makefile
	$(MAKE) -C $(FBINK_DIR) staticlib \
		KOBO=1 MINIMAL=1 DRAW=1 BITMAP=1 IMAGE=1 INPUT=1 \
		$(FBINK_CROSS)

$(BUILD)/%.o: src/%.c $(FBINK_DIR)/fbink.h src/chess.h src/display.h src/input.h src/ui.h
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(APP): $(OBJS) $(FBINK_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
	$(STRIP) $@

$(BUILD)/fbink_test: src/fbink_test.c $(FBINK_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ src/fbink_test.c $(LDLIBS)
	$(STRIP) $@

$(BUILD)/fb_direct_test: src/fb_direct_test.c $(FBINK_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ src/fb_direct_test.c $(LDLIBS)
	$(STRIP) $@

tests: $(BUILD)/fbink_test $(BUILD)/fb_direct_test

host-test:
	mkdir -p $(BUILD)
	cc -std=c11 -Wall -Wextra -O2 -o $(BUILD)/host_test src/chess.c src/host_test.c
	$(BUILD)/host_test

package: $(APP)
	mkdir -p dist/kobochess dist/nm
	cp -a $(APP) scripts/kobochess.sh scripts/restart-nickel.sh dist/kobochess/
	chmod 755 dist/kobochess/kobochess dist/kobochess/*.sh
	cp deploy/nm/kobochess dist/nm/kobochess
	@echo
	@echo "Copy dist/kobochess -> /mnt/onboard/.adds/kobochess"
	@echo "Copy dist/nm/kobochess -> /mnt/onboard/.adds/nm/kobochess"

clean:
	rm -f $(OBJS) $(APP) $(BUILD)/fbink_test $(BUILD)/fb_direct_test $(BUILD)/host_test
	rm -rf dist

distclean: clean
	if [ -f $(FBINK_DIR)/Makefile ]; then $(MAKE) -C $(FBINK_DIR) distclean; fi

help:
	@echo "Kobo Chess — NickelTC (Fedora)"
	@echo
	@echo "Toolchain triple: $(CROSS_TC)"
	@echo "Compiler:         $(CC)"
	@echo
	@echo "1. NickelTC already on PATH (extracted tarball or image /tc):"
	@echo "     git submodule update --init --recursive"
	@echo "     make"
	@echo "     make package"
	@echo
	@echo "2. NickelTC installed but not on PATH:"
	@echo "     make CROSS_COMPILE=/path/to/nickeltc/bin/arm-nickel-linux-gnueabihf-"
	@echo
	@echo "3. Podman/Docker image (same invocation NickelMenu uses):"
	@echo "     ./scripts/nickeltc-make.sh"
	@echo "     ./scripts/nickeltc-make.sh package"
	@echo
	@echo "4. Chess-rules test on the Fedora box (no cross compiler):"
	@echo "     make host-test"
	@echo
	@echo "Then copy dist/kobochess to /mnt/onboard/.adds/kobochess"
	@echo "and dist/nm/kobochess to /mnt/onboard/.adds/nm/kobochess"
