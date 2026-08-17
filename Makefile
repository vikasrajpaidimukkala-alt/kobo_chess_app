# Kobo Chess — chess for e-ink readers.
#
#   make                     build for the Kobo (needs NickelTC)
#   make PLATFORM=host       build for this machine, frames come out as PNG
#   make test                chess rules + engine checks (no cross compiler)
#   make package             stage dist/ for copying onto a device
#   make help                toolchain notes
#
# Adding a device means adding src/platform/<name>/ and one case below.
# See docs/PORTING.md.

PLATFORM ?= kobo

BUILD := build/$(PLATFORM)
APP   := $(BUILD)/kobochess

# Portable: game rules, search, rasteriser, UI, main loop. No device here.
CORE_SRCS := \
	src/app/main.c \
	src/chess/chess.c \
	src/chess/engine.c \
	src/gfx/canvas.c \
	src/ui/ui.c

CPPFLAGS := -Isrc
override CFLAGS += -Wall -Wextra -O2 -std=gnu11
LDLIBS :=

ifeq ($(PLATFORM),kobo)

# Kobo userspace is 32-bit ARM hard-float. Build with NickelTC
# (arm-nickel-linux-gnueabihf), the toolchain NickelMenu uses; Fedora's
# generic arm-linux-gnueabihf-gcc produces binaries Nickel will not run.
CROSS_TC ?= arm-nickel-linux-gnueabihf

ifdef CROSS_COMPILE
CC          := $(CROSS_COMPILE)gcc
STRIP       := $(CROSS_COMPILE)strip
FBINK_CROSS := CROSS_COMPILE=$(CROSS_COMPILE)
else
CC          := $(CROSS_TC)-gcc
STRIP       := $(CROSS_TC)-strip
FBINK_CROSS := CROSS_TC=$(CROSS_TC)
endif

# Thumb armv7hf matches Kobo userspace, Libra Colour included: the SoC
# is a Cortex-A53 but libc is still built for this ABI.
override CFLAGS += -march=armv7-a -mtune=cortex-a8 -mfpu=neon \
	-mfloat-abi=hard -mthumb

FBINK_DIR := third_party/FBInk
FBINK_LIB := $(FBINK_DIR)/Release/libfbink.a

CPPFLAGS += -I$(FBINK_DIR) -I$(FBINK_DIR)/Release
LDLIBS   += $(FBINK_LIB) -lm -ldl

PLATFORM_SRCS := \
	src/platform/kobo/kobo_platform.c \
	src/platform/kobo/kobo_display.c \
	src/platform/kobo/kobo_input.c
PLATFORM_DEPS := $(FBINK_LIB)

else ifeq ($(PLATFORM),host)

CC    := cc
STRIP := :
LDLIBS += -lz

PLATFORM_SRCS := src/platform/host/host_platform.c
PLATFORM_DEPS :=

else
$(error Unknown PLATFORM '$(PLATFORM)'. Try 'kobo' or 'host')
endif

SRCS := $(CORE_SRCS) $(PLATFORM_SRCS)
OBJS := $(SRCS:%.c=$(BUILD)/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all app test host-test engine-bench clean distclean package help fbink

all: app

app: $(APP)

fbink: $(FBINK_LIB)

$(FBINK_DIR)/fbink.h:
	git submodule update --init --recursive

$(FBINK_LIB): $(FBINK_DIR)/fbink.h Makefile
	$(MAKE) -C $(FBINK_DIR) staticlib \
		KOBO=1 MINIMAL=1 DRAW=1 BITMAP=1 IMAGE=1 INPUT=1 \
		$(FBINK_CROSS)

$(BUILD)/%.o: %.c $(PLATFORM_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(APP): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
	$(STRIP) $@

-include $(DEPS)

# Rules and engine are portable, so these always build for this machine.
test: host-test engine-bench

host-test:
	@mkdir -p build/tests
	cc -std=gnu11 -Wall -Wextra -O2 -Isrc -o build/tests/chess_test \
		src/chess/chess.c tests/chess_test.c
	build/tests/chess_test

engine-bench:
	@mkdir -p build/tests
	cc -std=gnu11 -Wall -Wextra -O2 -Isrc -o build/tests/engine_bench \
		src/chess/chess.c src/chess/engine.c tests/engine_bench.c
	build/tests/engine_bench

package: $(APP)
ifneq ($(PLATFORM),kobo)
	$(error package only makes sense for PLATFORM=kobo)
endif
	mkdir -p dist/kobochess dist/nm
	cp -a $(APP) scripts/kobochess.sh scripts/restart-nickel.sh dist/kobochess/
	chmod 755 dist/kobochess/kobochess dist/kobochess/*.sh
	cp deploy/nm/kobochess dist/nm/kobochess
	@echo
	@echo "Copy dist/kobochess -> /mnt/onboard/.adds/kobochess"
	@echo "Copy dist/nm/kobochess -> /mnt/onboard/.adds/nm/kobochess"

clean:
	rm -rf build dist

distclean: clean
	if [ -f $(FBINK_DIR)/Makefile ]; then $(MAKE) -C $(FBINK_DIR) distclean; fi

help:
	@echo "Kobo Chess"
	@echo
	@echo "  make                    build for PLATFORM=$(PLATFORM)"
	@echo "  make PLATFORM=host      build and run anywhere, frames as PNG"
	@echo "  make test               chess rules + engine checks"
	@echo "  make package            stage dist/ for the device"
	@echo
	@echo "Kobo builds need NickelTC ($(CROSS_TC)):"
	@echo
	@echo "  1. Toolchain on PATH:"
	@echo "       git submodule update --init --recursive"
	@echo "       make && make package"
	@echo
	@echo "  2. Toolchain elsewhere:"
	@echo "       make CROSS_COMPILE=/path/to/bin/$(CROSS_TC)-"
	@echo
	@echo "  3. Container (what NickelMenu uses):"
	@echo "       ./scripts/nickeltc-make.sh"
	@echo "       ./scripts/nickeltc-make.sh package"
