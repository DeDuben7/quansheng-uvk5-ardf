/* AMG (Agger Martini Groep) procedural welcome screen.
 * Big block letters drawn from filled rectangles, framed with a single
 * border that extends through the LCD's status-line strip too.
 */

#include <string.h>

#include "st7565/st7565.h"
#include "ui/helper.h"
#include "ui/welcome.h"
#include "scout/version.h"

extern uint8_t gStatusLine[128];

static void amg_fill_rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    for (int16_t y = y1; y <= y2; y++)
        UI_DrawLineBuffer(gFrameBuffer, x1, y, x2, y, true);
}

// Each block letter is 24 wide x 30 tall.
static void amg_letter_A(int16_t x, int16_t y)
{
    amg_fill_rect(x +  4, y +  0, x + 19, y +  4);   // top crown
    amg_fill_rect(x +  0, y +  5, x +  4, y + 29);   // left leg
    amg_fill_rect(x + 19, y +  5, x + 23, y + 29);   // right leg
    amg_fill_rect(x +  4, y + 14, x + 19, y + 18);   // crossbar
}

static void amg_letter_M(int16_t x, int16_t y)
{
    amg_fill_rect(x +  0, y +  0, x +  4, y + 29);   // left leg
    amg_fill_rect(x + 19, y +  0, x + 23, y + 29);   // right leg
    amg_fill_rect(x +  0, y +  0, x + 23, y +  4);   // top bar
    amg_fill_rect(x + 10, y +  5, x + 13, y + 16);   // middle stub
}

static void amg_letter_G(int16_t x, int16_t y)
{
    amg_fill_rect(x +  0, y +  0, x + 23, y +  4);   // top
    amg_fill_rect(x +  0, y +  0, x +  4, y + 29);   // left
    amg_fill_rect(x +  0, y + 25, x + 23, y + 29);   // bottom
    amg_fill_rect(x + 19, y + 14, x + 23, y + 29);   // right lower
    amg_fill_rect(x + 12, y + 14, x + 23, y + 18);   // middle spur
}

void UI_DisplayWelcome(void)
{
    memset(gStatusLine, 0, sizeof(gStatusLine));
    UI_DisplayClear();

    // Top frame line + side rails through the status-line strip.
    for (uint8_t x = 0; x < 128; x++) gStatusLine[x] |= 0x01;
    gStatusLine[0]   = 0xFF;
    gStatusLine[127] = 0xFF;

    // Body frame — left, right, bottom only (top blends with status strip).
    UI_DrawLineBuffer(gFrameBuffer,   0,  0,   0, 55, true);
    UI_DrawLineBuffer(gFrameBuffer, 127,  0, 127, 55, true);
    UI_DrawLineBuffer(gFrameBuffer,   0, 55, 127, 55, true);

    amg_letter_A(22, 0);
    amg_letter_M(52, 0);
    amg_letter_G(82, 0);

    UI_PrintStringSmallNormal("AGGER MARTINI", 0, 128, 4);
    UI_PrintStringSmallNormal("GROEP",         0, 128, 5);
    UI_PrintStringSmallNormal(scout_version_label(), 0, 128, 6);
    // The text overwrote bit 7 of page 6 — that's the bottom frame line.
    for (uint8_t x = 0; x < 128; x++) gFrameBuffer[6][x] |= 0x80;

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

void UI_DisplayReleaseKeys(void)
{
    memset(gStatusLine, 0, sizeof(gStatusLine));
    UI_DisplayClear();

    UI_PrintString("RELEASE", 0, 127, 1, 10);
    UI_PrintString("ALL KEYS", 0, 127, 3, 10);

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}
