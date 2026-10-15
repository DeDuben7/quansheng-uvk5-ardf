# scout — UV-K5 V3 (PY32F071) firmware. Bare-metal, no HAL, no libc.
# Shared code lives in ../../core and is compiled here (per-platform ISA).
TARGET = firmware_uvk5_v3
CORE   = ../../core

# Object list — ORDER IS SIGNIFICANT (code addresses depend on it; keep in sync
# with the pre-refactor order or binaries shift).
OBJS =
OBJS += build/plat/startup_py32f071.o
OBJS += build/plat/crt_stubs.o
OBJS += build/plat/bsp/py32f071/system_py32f071.o
OBJS += build/core/external/printf/printf.o
OBJS += build/plat/Src/scheduler.o
OBJS += build/plat/Src/peripherals/delay.o
OBJS += build/plat/Src/peripherals/spi_lcd.o
OBJS += build/plat/Src/peripherals/audio_path.o
OBJS += build/plat/Src/peripherals/keyboard.o
OBJS += build/plat/Src/peripherals/adc.o
OBJS += build/plat/Src/peripherals/backlight.o
OBJS += build/plat/Src/peripherals/spi_flash.o
OBJS += build/core/Src/battery/battery.o
OBJS += build/core/Src/bk4819/bk4819.o
OBJS += build/plat/Src/bk4819/bk4819_hw.o
OBJS += build/plat/Src/radio/tx_hw.o
OBJS += build/core/Src/calib/calib.o
OBJS += build/plat/Src/calib/calib_hw.o
OBJS += build/core/Src/radio/radio.o
OBJS += build/core/Src/st7565/st7565.o
OBJS += build/core/Src/ui/helper.o
OBJS += build/core/Src/ui/font.o
OBJS += build/core/Src/ui/bitmaps.o
OBJS += build/core/Src/ui/welcome.o
OBJS += build/core/Src/ui/inputbox.o
OBJS += build/core/Src/audio_df/audio_df.o
OBJS += build/core/Src/scout/home.o
OBJS += build/core/Src/scout/app.o
OBJS += build/core/Src/scout/scan.o
OBJS += build/core/Src/scout/tx.o
OBJS += build/core/Src/scout/info.o
OBJS += build/core/Src/scout/gaincal.o
OBJS += build/core/Src/scout/version.o
OBJS += build/core/Src/scout/menu.o
OBJS += build/core/Src/scout/light.o
OBJS += build/plat/main.o

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc
LD      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

# Flashing (PY32F071 / ProtocolV2 bootloader). K5TOOL cannot write this
# bootloader; we use the vendored muzkr serialtool (tools/k5v3-flasher).
# BLVER must match the radio's bootloader version family (first <=4 chars) to
# unlock programming — this radio reports 7.00.07, so 7.00.
# Override on the command line: make flash PORT=COM7 BLVER=7.00
PORT  ?= COM9
BLVER ?= 7.00

CPU = -mcpu=cortex-m0plus

# Commit baked into the image (version.c), shown on the Info screen:
# "8b8060a", or "8b8060a-dirty" if the tree has uncommitted changes.
# --exclude '*': ignore tags, so a release tag cannot replace the hash.
SCOUT_GIT_HASH ?= $(shell git describe --always --dirty --abbrev=7 --exclude '*' 2>/dev/null)
ifeq (, $(SCOUT_GIT_HASH))
    SCOUT_GIT_HASH := dev
endif
# Evaluate git once, not once per compiled file.
SCOUT_GIT_HASH := $(SCOUT_GIT_HASH)

CFLAGS  = -Os -Wall -Wextra -Werror $(CPU) -mthumb -fno-builtin -fshort-enums \
          -ffunction-sections -fdata-sections -std=c2x -MMD \
          -DPY32F071xB -DPRINTF_INCLUDE_CONFIG_H \
          -DSCOUT_GIT_HASH=\"$(SCOUT_GIT_HASH)\"
INC     = -I bsp/py32f071/Include -I Inc -I $(CORE)/Inc -I $(CORE) -I .
ASFLAGS = -c $(CPU) -mthumb
# -nostartfiles: the vendor startup_py32f071.s provides Reset_Handler, so
#   newlib's crt0 must NOT be pulled in.
# --specs=nano.specs: needed for __aeabi_idiv / memcpy / strlen, which the UI
#   code pulls in. crt_stubs.c still defines __libc_init_array as a real
#   object-file symbol, so it resolves before (and overrides) newlib's copy.
#   No syscall stubs are provided — a future malloc/printf call would need
#   --specs=nosys.specs or explicit retarget stubs.
LDFLAGS = $(CPU) -mthumb -nostartfiles --specs=nano.specs \
          -Wl,-T,firmware_py32f071.ld -Wl,--gc-sections

DEPS = $(OBJS:.o=.d)

all: $(TARGET)
	$(OBJCOPY) -O binary $< $<.bin
	$(SIZE) $<

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

build/core/%.o: $(CORE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/plat/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/plat/%.o: %.s
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
	rm -rf build $(TARGET) $(TARGET).bin

# Rebuild, then flash the raw .bin over the ProtocolV2 bootloader.
# Radio must be in bootloader mode (hold PTT while powering on).
flash: all
	python -u tools/k5v3-flasher/cli.py flash --port $(PORT) --bl-ver "$(BLVER)" $(TARGET).bin

.PHONY: all clean flash FORCE
