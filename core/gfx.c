#include "gfx.h"

uint8_t cz_fb[CZ_W * CZ_H];

/* RGB from the JS palette C{} (same as tools/pack_sprites.py). Index 0 is sprite transparency, never drawn. */
const uint8_t cz_pal_rgb[CZ_C_COUNT][3] = {
    {0x00, 0x00, 0x00}, /* transparent */
    {0x14, 0x1c, 0x3a}, /* navy  sky */
    {0x1e, 0x2a, 0x58}, /* dusk  horizon */
    {0x0a, 0x0e, 0x1f}, /* deep  city + ground */
    {0xf6, 0xf1, 0xe4}, /* ink   text + outlines */
    {0xf0, 0xb8, 0x40}, /* gold  windows + events */
    {0xf0, 0x6a, 0x5a}, /* coral hearts */
    {0x2b, 0xb8, 0xa8}, /* teal  secondary */
    {0x2a, 0x2a, 0x2e}, /* char  sprite only */
    {0xf4, 0xa3, 0xb5}, /* pink  sprite only */
    {0xff, 0xe3, 0x8a}, /* pale  sprite only */
};

#define RGB565(r, g, b) (uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3))
const uint16_t cz_pal565[CZ_C_COUNT] = {
    RGB565(0x00, 0x00, 0x00), RGB565(0x14, 0x1c, 0x3a), RGB565(0x1e, 0x2a, 0x58), RGB565(0x0a, 0x0e, 0x1f),
    RGB565(0xf6, 0xf1, 0xe4), RGB565(0xf0, 0xb8, 0x40), RGB565(0xf0, 0x6a, 0x5a), RGB565(0x2b, 0xb8, 0xa8),
    RGB565(0x2a, 0x2a, 0x2e), RGB565(0xf4, 0xa3, 0xb5), RGB565(0xff, 0xe3, 0x8a),
};

void cz_rect(int x, int y, int w, int h, int color) {
    int x1 = x + w, y1 = y + h, i, j;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > CZ_W) x1 = CZ_W;
    if (y1 > CZ_H) y1 = CZ_H;
    for (j = y; j < y1; j++)
        for (i = x; i < x1; i++) cz_fb[j * CZ_W + i] = (uint8_t)color;
}

void cz_blit(const uint8_t *spr, int x, int y, int scale) {
    int sx, sy;
    for (sy = 0; sy < CZ_SPRITE; sy++)
        for (sx = 0; sx < CZ_SPRITE; sx++) {
            uint8_t c = spr[sy * CZ_SPRITE + sx];
            if (c) cz_rect(x + sx * scale, y + sy * scale, scale, scale, c);
        }
}

/* 3x5 font, rows of 3 bits (MSB = left), ported from the JS F table. 0xB7 is the middle dot. */
static const struct { uint8_t ch, rows[5]; } FONT[] = {
    {'A', {2,5,7,5,5}}, {'B', {6,5,6,5,6}}, {'C', {3,4,4,4,3}}, {'D', {6,5,5,5,6}}, {'E', {7,4,6,4,7}},
    {'F', {7,4,6,4,4}}, {'G', {3,4,5,5,3}}, {'H', {5,5,7,5,5}}, {'I', {7,2,2,2,7}}, {'J', {1,1,1,5,2}},
    {'K', {5,5,6,5,5}}, {'L', {4,4,4,4,7}}, {'M', {5,7,7,5,5}}, {'N', {6,5,5,5,5}}, {'O', {2,5,5,5,2}},
    {'P', {6,5,6,4,4}}, {'Q', {2,5,5,7,3}}, {'R', {6,5,6,5,5}}, {'S', {3,4,2,1,6}}, {'T', {7,2,2,2,2}},
    {'U', {5,5,5,5,7}}, {'V', {5,5,5,5,2}}, {'W', {5,5,7,7,5}}, {'X', {5,5,2,5,5}}, {'Y', {5,5,2,2,2}},
    {'Z', {7,1,2,4,7}},
    {'0', {2,5,5,5,2}}, {'1', {2,6,2,2,7}}, {'2', {6,1,2,4,7}}, {'3', {6,1,2,1,6}}, {'4', {5,5,7,1,1}},
    {'5', {7,4,6,1,6}}, {'6', {3,4,6,5,2}}, {'7', {7,1,2,2,2}}, {'8', {2,5,2,5,2}}, {'9', {2,5,3,1,6}},
    {' ', {0,0,0,0,0}}, {'.', {0,0,0,0,2}}, {0xB7, {0,0,2,0,0}}, {':', {0,2,0,2,0}}, {'#', {5,7,5,7,5}},
    {'-', {0,0,7,0,0}}, {'+', {0,2,7,2,0}}, {'/', {1,1,2,4,4}}, {'!', {2,2,2,0,2}}, {'?', {6,1,2,0,2}},
    {'\'', {2,2,0,0,0}}, {',', {0,0,0,2,4}},
};
#define FONT_N (int)(sizeof FONT / sizeof FONT[0])
#define FONT_QMARK 45

/* Next character as one glyph code: uppercased, UTF-8 middle dot (C2 B7) -> 0xB7, other multibyte -> '?'. */
static int next_char(const unsigned char **p) {
    unsigned c = *(*p)++;
    if (c >= 'a' && c <= 'z') return (int)(c - 32);
    if (c == 0xC2 && **p == 0xB7) { (*p)++; return 0xB7; }
    if (c >= 0xC0) { while (**p >= 0x80 && **p < 0xC0) (*p)++; return '?'; }
    return (int)c;
}

int cz_text_width(const char *s, int scale) {
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p) { next_char(&p); n++; }
    return n ? n * 4 * scale - scale : 0;
}

int cz_text(const char *s, int x, int y, int color, int scale, int align) {
    const unsigned char *p = (const unsigned char *)s;
    int w = cz_text_width(s, scale);
    if (align == CZ_CENTER) x -= w / 2; /* == Math.round(x - w/2) for w >= 0 */
    if (align == CZ_RIGHT) x -= w;
    while (*p) {
        int ch = next_char(&p), g = FONT_QMARK, i, rx, ry;
        for (i = 0; i < FONT_N; i++) if (FONT[i].ch == ch) { g = i; break; }
        for (ry = 0; ry < 5; ry++)
            for (rx = 0; rx < 3; rx++)
                if (FONT[g].rows[ry] & (4 >> rx)) cz_rect(x + rx * scale, y + ry * scale, scale, scale, color);
        x += 4 * scale;
    }
    return w;
}

/* 8x8 icons from the JS ICON table, MSB = left. */
static const uint8_t ICONS[CZ_ICONS][8] = {
    {0x66,0xff,0xff,0xff,0x7e,0x3c,0x18,0x00}, /* heart */
    {0x66,0x99,0x81,0x81,0x42,0x24,0x18,0x00}, /* heartO */
    {0x00,0x3c,0x42,0xff,0x7e,0x3c,0x00,0x00}, /* bowl */
    {0x3c,0x66,0xdf,0xbf,0xff,0xff,0x7e,0x3c}, /* ball */
    {0x7e,0x81,0x81,0x81,0x7e,0x0c,0x08,0x00}, /* bubble */
    {0xf0,0x20,0x40,0xf0,0x0f,0x02,0x04,0x0f}, /* zz */
};

void cz_icon(int icon, int x, int y, int color, int scale) {
    int rx, ry;
    for (ry = 0; ry < 8; ry++)
        for (rx = 0; rx < 8; rx++)
            if (ICONS[icon][ry] & (0x80 >> rx)) cz_rect(x + rx * scale, y + ry * scale, scale, scale, color);
}

void cz_dim(int a) {
    uint8_t lut[CZ_C_COUNT];
    int i, k, j;
    for (i = 0; i < CZ_C_COUNT; i++) {
        int t[3], best = CZ_C_DEEP;
        long bd = 0x7fffffffL;
        for (k = 0; k < 3; k++) t[k] = (cz_pal_rgb[i][k] * (100 - a) + cz_pal_rgb[CZ_C_DEEP][k] * a) / 100;
        for (j = 1; j < CZ_C_COUNT; j++) {
            long d = 0;
            for (k = 0; k < 3; k++) d += (long)(t[k] - cz_pal_rgb[j][k]) * (t[k] - cz_pal_rgb[j][k]);
            if (d < bd) { bd = d; best = j; }
        }
        lut[i] = (uint8_t)best;
    }
    for (i = 0; i < CZ_W * CZ_H; i++) cz_fb[i] = lut[cz_fb[i]];
}
