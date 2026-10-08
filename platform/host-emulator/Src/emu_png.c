/* Writes the emulated LCD as a PNG, without any image library.
 *
 * Each LCD pixel becomes a 4x4 block (a dark 3x3 dot with a slightly
 * lighter right/bottom edge, like a real dot-matrix glass) on an 8 px
 * margin: 528 x 272, 8-bit palette. The zlib stream uses stored (raw)
 * deflate blocks, so there is nothing to compress and the output is
 * byte-for-byte deterministic — `make check` relies on that.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu/emu.h"
#include "st7565/st7565.h"

#define SCALE   4
#define MARGIN  8
#define IMG_W   (LCD_WIDTH  * SCALE + 2 * MARGIN)
#define IMG_H   (LCD_HEIGHT * SCALE + 2 * MARGIN)

enum { PAL_GLASS = 0, PAL_DOT = 1, PAL_EDGE = 2 };
static const uint8_t PALETTE[3][3] = {
    { 0xCF, 0xD9, 0xC9 },   /* glass */
    { 0x1E, 0x26, 0x22 },   /* dot */
    { 0x46, 0x51, 0x4A },   /* dot edge */
};

static uint32_t s_crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++)
    {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        s_crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        crc = s_crc_table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char type[4], const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put_be32(tail, crc);
    return (fwrite(hdr, 1, 8, f) == 8
         && (len == 0 || fwrite(data, 1, len, f) == len)
         && fwrite(tail, 1, 4, f) == 4) ? 0 : -1;
}

static uint8_t pixel_index(int px, int py)
{
    const int x = px - MARGIN, y = py - MARGIN;
    if (x < 0 || y < 0 || x >= LCD_WIDTH * SCALE || y >= LCD_HEIGHT * SCALE)
        return PAL_GLASS;
    if (!emu_lcd_pixel(x / SCALE, y / SCALE))
        return PAL_GLASS;
    const int sx = x % SCALE, sy = y % SCALE;
    return (sx == SCALE - 1 || sy == SCALE - 1) ? PAL_EDGE : PAL_DOT;
}

int emu_png_write(const char *path)
{
    crc_init();

    /* Raw scanlines: filter byte 0 + one palette index per pixel. */
    const size_t raw_len = (size_t)IMG_H * (IMG_W + 1);
    uint8_t *raw = malloc(raw_len);
    if (!raw) return -1;
    for (int y = 0; y < IMG_H; y++)
    {
        uint8_t *row = raw + (size_t)y * (IMG_W + 1);
        row[0] = 0;
        for (int x = 0; x < IMG_W; x++)
            row[1 + x] = pixel_index(x, y);
    }

    /* zlib: header, stored deflate blocks, Adler-32. */
    const size_t nblocks = (raw_len + 65534) / 65535;
    const size_t z_len = 2 + nblocks * 5 + raw_len + 4;
    uint8_t *z = malloc(z_len);
    if (!z) { free(raw); return -1; }
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    for (size_t done = 0; done < raw_len; )
    {
        const size_t n = (raw_len - done > 65535) ? 65535 : raw_len - done;
        z[o++] = (done + n == raw_len) ? 1 : 0;           /* BFINAL, BTYPE=00 */
        z[o++] = (uint8_t)n;          z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n;         z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + done, n);
        o += n;
        done += n;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++)
    {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    put_be32(z + o, (b << 16) | a);
    o += 4;

    uint8_t ihdr[13];
    put_be32(ihdr, IMG_W);
    put_be32(ihdr + 4, IMG_H);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 3;    /* palette */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

    int rc = -1;
    FILE *f = fopen(path, "wb");
    if (f)
    {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        rc = (fwrite(sig, 1, 8, f) == 8
           && write_chunk(f, "IHDR", ihdr, sizeof(ihdr)) == 0
           && write_chunk(f, "PLTE", &PALETTE[0][0], sizeof(PALETTE)) == 0
           && write_chunk(f, "IDAT", z, (uint32_t)o) == 0
           && write_chunk(f, "IEND", NULL, 0) == 0) ? 0 : -1;
        if (fclose(f) != 0) rc = -1;
    }
    free(z);
    free(raw);
    return rc;
}
