/* V3 (PY32F071) keyboard matrix scan — GPIOB rows/cols (PTT is read
 * separately, in Src/radio/tx_hw.c).
 *
 * Public API and key-mapping table are V1's (Src/peripherals/keyboard.c +
 * Inc/peripherals/keyboard.h): KEYBOARD_Poll() returns a KEY_Code_t, and the
 * gKeyReading0/gKeyReading1/gDebounceCounter/gWasFKeyPressed globals are
 * declared extern by keyboard.h and defined here exactly as V1 does (V1's
 * app.c does not touch them, but any future file that includes keyboard.h
 * expects them to exist). V1's own KEYBOARD_Poll() never assigns them
 * either — they are carried over unused, matching V1.
 *
 * V1's DP32 KEYBOARD_Poll() (dp32g030 Src/peripherals/keyboard.c:95-153)
 * drives 4 "row" pins (GPIOA_PIN_KEYBOARD_4..7) one-hot low and senses 4
 * "col" pins (GPIOA_PIN_KEYBOARD_0..3), plus a zero-drive special case for
 * SIDE1/SIDE2. Its keyboard[5] table (driven-pin index j = 0..4, sensed-pin
 * index i = 0..3) is:
 *   j=0 (no drive):      SIDE1, SIDE2, INVALID, INVALID
 *   j=1 (drive KB_4):    MENU,  1,     4,        7
 *   j=2 (drive KB_5):    UP,    2,     5,        8
 *   j=3 (drive KB_6):    DOWN,  3,     6,        9
 *   j=4 (drive KB_7):    EXIT,  STAR,  0,        F
 *
 * armel's V3 keyboard[5][4] table (App/driver/keyboard.c:149-183, col index
 * j = 0..4, row index i = 0..3) is IDENTICAL cell-for-cell to V1's table
 * above. There is no matrix-orientation disagreement between V1 and armel
 * for this radio — the table below is transcribed as-is from both.
 *
 * Pin assignment (armel App/driver/gpio.h:31 GPIO_PIN_PTT and
 * App/driver/keyboard.c:136-142):
 *   Cols (driven, one-hot active-low): GPIOB3-6
 *     PIN_COL(n) = 1 << (6-n), called as PIN_COL(j-1) for j=1..4
 *       -> j=1: PB6, j=2: PB5, j=3: PB4, j=4: PB3
 *   Rows (sensed, active-low, pulled up): GPIOB12-15
 *     PIN_MASK_ROW(i) = 1 << (15-i) for i=0..3 -> PB15, PB14, PB13, PB12
 *   PTT: NOT read here. Scout is an RX-only fox-hunt receiver (no TX), and
 *     V1's KEYBOARD_Poll() likewise never returns KEY_PTT (its PTT check is
 *     commented out). So KEYBOARD_Poll() below returns matrix keys only, never
 *     KEY_PTT — matching V1's actual behaviour. (armel does layer PTT on top
 *     via GPIO_IsPttPressed() for its TX firmware; not applicable to scout.)
 *
 * Debounce is transcribed from armel App/driver/keyboard.c:206-281
 * (KEYBOARD_Poll): scan all 5 columns without early-exit-on-noise, for each
 * column take up to 8 samples 10us apart and require 2 consecutive matching
 * reads before trusting the row bits, and always leave all columns driven
 * high on exit. GPIO access is reimplemented against raw CMSIS registers
 * (GPIOB->BSRR/IDR/MODER/PUPDR) to match this port's no-HAL style already
 * used in Src/peripherals/spi_lcd.c and Src/peripherals/audio_path.c,
 * instead of armel's PY32 LL driver calls.
 *
 * No I2C/voice-chip pin sharing on V3 — V1's KEYBOARD_Poll() calls
 * I2C_Stop() and re-drives two of its row pins afterward because DP32
 * shares those GPIOA pins with I2C/voice hardware. V3's keyboard pins
 * (GPIOB3-6/10/12-15) are dedicated, so that cleanup has no PY32
 * equivalent and is dropped entirely.
 */
#include "peripherals/keyboard.h"
#include "peripherals/delay.h"
#include "py32f0xx.h"

KEY_Code_t gKeyReading0     = KEY_INVALID;
KEY_Code_t gKeyReading1     = KEY_INVALID;
uint16_t   gDebounceCounter = 0;
bool       gWasFKeyPressed  = false;

#define COL_PIN_MASK  ((1u << 3) | (1u << 4) | (1u << 5) | (1u << 6))   /* PB3-6 */
#define ROW_PIN_MASK  ((1u << 12) | (1u << 13) | (1u << 14) | (1u << 15)) /* PB12-15 */

/* Driven column pin for loop index j = 1..4 -- armel keyboard.c:139 PIN_COL(n)
 * = 1 << (6-n), invoked as PIN_COL(j-1) at keyboard.c:220. */
static inline uint32_t col_pin(unsigned int j)
{
    return 1u << (7u - j);
}

/* Sensed row pin for row index i = 0..3 -- armel keyboard.c:142
 * PIN_MASK_ROW(n) = 1 << (15-n). */
static inline uint32_t row_pin(unsigned int i)
{
    return 1u << (15u - i);
}

/* keyboard[j][i]: j = driven column (0 = none driven, 1..4 = PB6..PB3),
 * i = sensed row (PB15..PB12). Identical to both V1's keyboard[] table
 * (dp32g030 Src/peripherals/keyboard.c:43-93) and armel's keyboard[5][4]
 * (App/driver/keyboard.c:149-183) -- see file header. */
static const KEY_Code_t keyboard[5][4] = {
    {   /* j=0: no column driven -- SIDE1/SIDE2 wired direct to rows */
        KEY_SIDE1,
        KEY_SIDE2,
        KEY_INVALID,
        KEY_INVALID,
    },
    {   /* j=1: drive PB6 */
        KEY_MENU,
        KEY_1,
        KEY_4,
        KEY_7,
    },
    {   /* j=2: drive PB5 */
        KEY_UP,
        KEY_2,
        KEY_5,
        KEY_8,
    },
    {   /* j=3: drive PB4 */
        KEY_DOWN,
        KEY_3,
        KEY_6,
        KEY_9,
    },
    {   /* j=4: drive PB3 */
        KEY_EXIT,
        KEY_STAR,
        KEY_0,
        KEY_F,
    },
};

static void keyboard_gpio_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    (void)RCC->IOPENR; /* read-back: ensure the enable took effect */

    /* Cols PB3-6: general-purpose push-pull outputs (MODER = 01), idle
     * high. OTYPER reset default (push-pull) is already 0, left as-is. */
    GPIOB->MODER &= ~((3u << (3u * 2u)) | (3u << (4u * 2u)) |
                       (3u << (5u * 2u)) | (3u << (6u * 2u)));
    GPIOB->MODER |=  ((1u << (3u * 2u)) | (1u << (4u * 2u)) |
                       (1u << (5u * 2u)) | (1u << (6u * 2u)));
    GPIOB->BSRR = COL_PIN_MASK; /* drive all columns high (idle/unselected) */

    /* Rows PB12-15: inputs (MODER = 00, reset default) with
     * pull-up enabled (PUPDR = 01), matching armel's pulled-up sense pins. */
    GPIOB->MODER &= ~((3u << (12u * 2u)) |
                       (3u << (13u * 2u)) | (3u << (14u * 2u)) |
                       (3u << (15u * 2u)));
    GPIOB->PUPDR &= ~((3u << (12u * 2u)) |
                       (3u << (13u * 2u)) | (3u << (14u * 2u)) |
                       (3u << (15u * 2u)));
    GPIOB->PUPDR |=  ((1u << (12u * 2u)) |
                       (1u << (13u * 2u)) | (1u << (14u * 2u)) |
                       (1u << (15u * 2u)));
}

KEY_Code_t KEYBOARD_Poll(void)
{
    static bool inited = false;
    if (!inited)
    {
        keyboard_gpio_init();
        inited = true;
    }

    KEY_Code_t Key = KEY_INVALID;

    /* Scan all 5 columns -- never break early on noise, to avoid leaving
     * GPIO in a half-scanned state (armel keyboard.c:208-274). */
    for (unsigned int j = 0; j < 5; j++)
    {
        uint32_t reg;
        uint32_t match_count = 0; /* consecutive matching reads */

        /* Set all columns high, then pull just this column low
         * (armel keyboard.c:214-221). */
        GPIOB->BSRR = COL_PIN_MASK;
        if (j > 0)
        {
            GPIOB->BSRR = col_pin(j) << 16u; /* reset (drive low) */
        }

        /* Debounce: sample rows up to 8 times, require 2 consecutive
         * matching reads before trusting them (armel keyboard.c:227-249).
         * armel waits 10 us between samples; we use 50 us, which is what the
         * old, too-slow busy-loop DelayUs(10) really took — the spacing the
         * keypad was verified reliable with. */
        reg = 0;
        for (unsigned int k = 0; k < 8; k++)
        {
            DelayUs(50);
            uint32_t reg2 = GPIOB->IDR & ROW_PIN_MASK;

            if (reg2 != reg)
            {
                match_count = 0;
                reg = reg2;
            }
            else
            {
                match_count++;
            }

            if (match_count >= 2)
            {
                break; /* stable signal */
            }
        }

        /* Debounce failed (too much noise) -- skip this column, keep
         * scanning the rest (armel keyboard.c:253-258). */
        if (match_count < 2)
        {
            continue;
        }

        for (unsigned int i = 0; i < 4; i++)
        {
            if (!(reg & row_pin(i)))
            {
                Key = keyboard[j][i];
                break;
            }
        }

        if (Key != KEY_INVALID)
        {
            break;
        }
    }

    /* Always leave all columns driven high on exit (armel keyboard.c:278). */
    GPIOB->BSRR = COL_PIN_MASK;

    return Key;
}
