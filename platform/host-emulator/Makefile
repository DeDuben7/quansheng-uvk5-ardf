# scout host emulator — runs the real core/ firmware on the PC.
# Needs w64devkit (host gcc + make): https://github.com/skeeto/w64devkit
#
#   make           build build/scout-emu.exe
#   make test      run scenes/tests.txt (firmware logic checks)
#   make screens   render scenes/manual.txt into docs/manual/img/screens
#   make check     re-render and compare with the committed screens
W64DEVKIT ?= C:/w64devkit
CC    = $(W64DEVKIT)/bin/gcc.exe
CORE  = ../../core

# Core sources, as compiled by the radio platforms (minus hardware drivers).
CORE_SRCS  = external/printf/printf.c
CORE_SRCS += Src/bk4819/bk4819.c
CORE_SRCS += Src/calib/calib.c
CORE_SRCS += Src/st7565/st7565.c
CORE_SRCS += Src/radio/radio.c
CORE_SRCS += Src/audio_df/audio_df.c
CORE_SRCS += Src/scout/home.c
CORE_SRCS += Src/scout/app.c
CORE_SRCS += Src/scout/scan.c
CORE_SRCS += Src/scout/tx.c
CORE_SRCS += Src/scout/info.c
CORE_SRCS += Src/scout/gaincal.c
CORE_SRCS += Src/scout/version.c
CORE_SRCS += Src/scout/menu.c
CORE_SRCS += Src/battery/battery.c
CORE_SRCS += Src/ui/helper.c
CORE_SRCS += Src/ui/font.c
CORE_SRCS += Src/ui/bitmaps.c
CORE_SRCS += Src/ui/welcome.c
CORE_SRCS += Src/ui/inputbox.c

EMU_SRCS = $(wildcard Src/*.c)

CFLAGS = -O1 -g -std=c2x -fshort-enums -Wall -Wextra -MMD \
         -DPRINTF_INCLUDE_CONFIG_H -DSCOUT_EMULATOR \
         -DSCOUT_GIT_HASH=\"dev\"
INC    = -I Inc -I $(CORE)/Inc -I $(CORE)

OBJS = $(addprefix build/core/,$(CORE_SRCS:.c=.o)) $(addprefix build/emu/,$(notdir $(EMU_SRCS:.c=.o)))
SCREENS_DIR = ../../docs/manual/img/screens
EMU = build/scout-emu.exe

all: $(EMU)

$(EMU): $(OBJS)
	$(CC) $^ -o $@

build/core/%.o: $(CORE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/emu/%.o: Src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(OBJS): Makefile
-include $(OBJS:.o=.d)

test: $(EMU)
	@mkdir -p build/test-out
	$(EMU) scenes/tests.txt build/test-out

screens: $(EMU)
	@mkdir -p $(SCREENS_DIR)
	$(EMU) scenes/manual.txt $(SCREENS_DIR)

check: $(EMU)
	@rm -rf build/check && mkdir -p build/check
	$(EMU) scenes/manual.txt build/check
	@for f in build/check/*.png; do \
	  cmp -s "$$f" "$(SCREENS_DIR)/$$(basename $$f)" || { echo "CHANGED $$(basename $$f) (run make screens and review)"; exit 1; }; \
	done; echo "screens unchanged"

clean:
	rm -rf build

.PHONY: all test screens check clean
