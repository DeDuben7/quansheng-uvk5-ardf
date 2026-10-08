# scout — UV-K5 V1 (DP32G030) firmware.
# Shared code lives in ../../core and is compiled here (per-platform ISA).
TARGET = firmware_uvk5_v1
CORE   = ../../core

BSP_DEFINITIONS := $(wildcard hardware/*/*.def)
BSP_HEADERS     := $(patsubst hardware/%,bsp/%,$(BSP_DEFINITIONS))
BSP_HEADERS     := $(patsubst %.def,%.h,$(BSP_HEADERS))

# Object list — ORDER IS SIGNIFICANT (code addresses depend on it; keep in sync
# with the pre-refactor order or binaries shift).
OBJS =
OBJS += build/plat/start.o
OBJS += build/plat/init.o
OBJS += build/core/external/printf/printf.o
OBJS += build/plat/Src/peripherals/adc.o
OBJS += build/plat/Src/peripherals/aes.o
OBJS += build/plat/Src/peripherals/backlight.o
OBJS += build/plat/Src/peripherals/crc.o
OBJS += build/plat/Src/peripherals/eeprom.o
OBJS += build/plat/Src/peripherals/gpio.o
OBJS += build/plat/Src/peripherals/i2c.o
OBJS += build/plat/Src/peripherals/keyboard.o
OBJS += build/plat/Src/peripherals/spi.o
OBJS += build/plat/Src/peripherals/spi_lcd.o
OBJS += build/plat/Src/peripherals/system.o
OBJS += build/plat/Src/peripherals/systick.o
OBJS += build/core/Src/bk4819/bk4819.o
OBJS += build/plat/Src/bk4819/bk4819_hw.o
OBJS += build/plat/Src/radio/tx_hw.o
OBJS += build/core/Src/calib/calib.o
OBJS += build/plat/Src/calib/calib_hw.o
OBJS += build/core/Src/st7565/st7565.o
OBJS += build/core/Src/radio/radio.o
OBJS += build/core/Src/audio_df/audio_df.o
OBJS += build/core/Src/scout/home.o
OBJS += build/core/Src/scout/app.o
OBJS += build/core/Src/scout/scan.o
OBJS += build/core/Src/scout/tx.o
OBJS += build/core/Src/scout/info.o
OBJS += build/core/Src/scout/gaincal.o
OBJS += build/core/Src/scout/version.o
OBJS += build/core/Src/scout/menu.o
OBJS += build/core/Src/battery/battery.o
OBJS += build/core/Src/ui/helper.o
OBJS += build/core/Src/ui/font.o
OBJS += build/core/Src/ui/bitmaps.o
OBJS += build/core/Src/ui/welcome.o
OBJS += build/core/Src/ui/inputbox.o
OBJS += build/plat/Src/board.o
OBJS += build/plat/Src/scheduler.o
OBJS += build/plat/Src/version.o
OBJS += build/plat/Src/main.o

ifeq ($(OS), Windows_NT)
    RM = del /Q
    FixPath = $(subst /,\,$1)
    WHERE = where
    NULL_OUTPUT = nul
else
    RM = rm -f
    FixPath = $1
    WHERE = which
    NULL_OUTPUT = /dev/null
endif

AS      = arm-none-eabi-gcc
LD      = arm-none-eabi-gcc
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

AUTHOR_STRING ?= SCOUT
ifneq (, $(shell $(WHERE) git))
    VERSION_STRING ?= $(shell git rev-parse --short HEAD 2>$(NULL_OUTPUT))
endif
ifeq (, $(VERSION_STRING))
    VERSION_STRING := dev
endif

# Commit baked into the image (version.c), shown on the Info screen:
# "8b8060a", or "8b8060a-dirty" if the tree has uncommitted changes.
# --exclude '*': ignore tags, so a release tag cannot replace the hash.
SCOUT_GIT_HASH ?= $(shell git describe --always --dirty --abbrev=7 --exclude '*' 2>$(NULL_OUTPUT))
ifeq (, $(SCOUT_GIT_HASH))
    SCOUT_GIT_HASH := dev
endif
# Evaluate git once, not once per compiled file.
SCOUT_GIT_HASH := $(SCOUT_GIT_HASH)

ASFLAGS = -c -mcpu=cortex-m0

CFLAGS  = -Os -Wall -Werror -mcpu=cortex-m0 -fno-builtin -fshort-enums \
          -fno-delete-null-pointer-checks -std=c2x -MMD -flto=auto -Wextra \
          -DPRINTF_INCLUDE_CONFIG_H \
          -DAUTHOR_STRING=\"$(AUTHOR_STRING)\" \
          -DVERSION_STRING=\"$(VERSION_STRING)\" \
          -DSCOUT_GIT_HASH=\"$(SCOUT_GIT_HASH)\"

LDFLAGS = -z noexecstack -mcpu=cortex-m0 -nostartfiles \
          -Wl,-T,firmware.ld -Wl,--gc-sections --specs=nano.specs

INC  = -I Inc
INC += -I .
INC += -I $(CORE)/Inc
INC += -I $(CORE)
INC += -I external/CMSIS_5/CMSIS/Core/Include/
INC += -I external/CMSIS_5/Device/ARM/ARMCM0/Include

LIBS =
DEPS = $(OBJS:.o=.d)

ifneq (, $(shell $(WHERE) python))
    MY_PYTHON := python
else ifneq (, $(shell $(WHERE) python3))
    MY_PYTHON := python3
endif

all: $(TARGET)
	$(OBJCOPY) -O binary $< $<.bin
ifdef MY_PYTHON
	-$(MY_PYTHON) fw-pack.py $<.bin $(AUTHOR_STRING) $(VERSION_STRING) $<.packed.bin
endif
	$(SIZE) $<

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

bsp/dp32g030/%.h: hardware/dp32g030/%.def

build/core/%.o: $(CORE)/%.c | $(BSP_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/plat/%.o: %.c | $(BSP_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/plat/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

-include $(DEPS)

# Rebuild everything if CFLAGS/OBJS change (OBJS order is load-bearing for
# code addresses, so a reorder must trigger a full rebuild too).
$(OBJS): Makefile

# version.o embeds SCOUT_GIT_HASH via CFLAGS, which make does not track. Keep
# the hash in a stamp file that is rewritten only when the value changes, and
# make version.o depend on it, so a new commit / dirty tree / clean tree always
# recompiles version.o (and never otherwise).
build/scout_git_hash.stamp: FORCE
	@mkdir -p $(dir $@)
	@echo '$(SCOUT_GIT_HASH)' | cmp -s - $@ || echo '$(SCOUT_GIT_HASH)' > $@
FORCE:
build/core/Src/scout/version.o: build/scout_git_hash.stamp

# Note: POSIX syntax (compile recipes above already use mkdir -p via Git
# Bash/MSYS, so clean must match or it silently no-ops on Windows).
clean:
	rm -rf build $(TARGET) $(TARGET).bin $(TARGET).packed.bin

.PHONY: all clean FORCE
