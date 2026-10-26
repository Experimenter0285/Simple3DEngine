# Simple3DEngine - portable Makefile (Linux / FreeBSD / MinGW on Windows)
#
# Targets:
#   make            - build libsimple3d.a + simplecraft demo (software renderer)
#   make gl         - same, with OpenGL 1.x backend enabled (needs GL headers/libs)
#   make test       - run the headless voxel sandbox smoke test (writes build/*.ppm)
#   make clean      - remove build artifacts
#
# Options:
#   CC=gcc|clang    - compiler
#   S3D_X11=1       - enable the raw X11/GLX window backend (no SDL needed)
#   S3D_SDL=1       - use the SDL2 platform backend instead
#   CFLAGS_EXTRA=... - extra flags

CC      ?= cc
UNAME_S := $(shell uname -s)

BUILD   := build
LIB     := $(BUILD)/libsimple3d.a

CFLAGS  ?= -O2 -g -std=gnu99 -Wall -Wextra -Iinclude -Isrc
LDFLAGS ?=
LDLIBS  := -lm

CORE_SRC := src/engine.c src/platform.c src/input.c src/renderer_software.c \
            src/renderer_gl.c src/game_object.c src/voxel.c src/voxel_game.c \
            src/html_ui.c third_party/box2d.c

ifeq ($(S3D_X11),1)
CFLAGS += -DS3D_HAS_X11
LDLIBS += -lX11 -lGL
endif

ifeq ($(S3D_SDL),1)
CFLAGS += -DS3D_HAS_SDL $(shell pkg-config --cflags sdl2 2>/dev/null)
CORE_SRC += src/platform_sdl.c
LDLIBS += $(shell pkg-config --libs sdl2 2>/dev/null || echo -lSDL2)
endif

ifeq ($(gl),1)
CFLAGS += -DS3D_HAS_GL $(shell pkg-config --cflags gl 2>/dev/null)
LDLIBS += $(shell pkg-config --libs gl 2>/dev/null || echo -lGL)
endif

OBJ := $(CORE_SRC:%.c=$(BUILD)/%.o)

.PHONY: all gl test clean dirs

all: $(BUILD)/simplecraft

gl:
	$(MAKE) gl=1 all

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	@mkdir -p $(BUILD)
	ar rcs $@ $^

$(BUILD)/simplecraft: $(BUILD)/examples/simplecraft/main.o $(LIB)
	$(CC) $(CFLAGS) -o $@ $(BUILD)/examples/simplecraft/main.o $(LIB) $(LDFLAGS) $(LDLIBS)

test: $(BUILD)/simplecraft
	./$(BUILD)/simplecraft --headless 120 --width 640 --height 480
	@ls -la $(BUILD)/*.ppm && echo "TEST OK"

clean:
	rm -rf $(BUILD)
