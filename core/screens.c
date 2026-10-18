/* Every screen, ported from SCREEN_DESIGN_SOURCE.html with the same coordinates and colours. */
#include "gfx.h"
#include "qrcodegen.h"

#define CX 120

extern int cz_hatch_y; /* sprite top on Hatch, set by citizen.c from the screen timer */

static void px2(int color, const uint8_t (*pts)[2], int n, int sc) {
    int i;
    for (i = 0; i < n; i++) cz_rect(pts[i][0] * sc, pts[i][1] * sc, sc, sc, color);
}

static int phase; /* enum cz_phase of the frame being drawn, set by cz_draw */

static int sky;  /* enum cz_sky of the frame being drawn, set by cz_draw */
static int band; /* the horizon band's colour this phase: what shows through gaps in a skyline */

/* 2x2 Bresenham line and midpoint circle ring, for the Flyer's spokes and rim */
static void line2(int x0, int y0, int x1, int y1, int c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 2 : -2, sy = y0 < y1 ? 2 : -2, e = dx + dy, e2;
    for (;;) {
        cz_rect(x0, y0, 2, 2, c);
        if ((sx > 0 ? x0 >= x1 : x0 <= x1) && (sy > 0 ? y0 >= y1 : y0 <= y1)) break;
        e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void ring2(int cx, int cy, int r, int c) {
    int x = r, y = 0, e = 1 - r;
    while (x >= y) {
        cz_rect(cx + x, cy + y, 2, 2, c); cz_rect(cx - x, cy + y, 2, 2, c); cz_rect(cx + x, cy - y, 2, 2, c); cz_rect(cx - x, cy - y, 2, 2, c);
        cz_rect(cx + y, cy + x, 2, 2, c); cz_rect(cx - y, cy + x, 2, 2, c); cz_rect(cx + y, cy - x, 2, 2, c); cz_rect(cx - y, cy - x, 2, 2, c);
        y++;
        if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
    }
}

static void kl(int night) {
    static const uint8_t lit[][2] = {{150,102},{154,112},{170,108},{174,130},{60,148},{66,154},
                                     {6,144},{184,144},{228,154},{150,136},{172,148},{46,122}};
    const int s = CZ_C_DEEP;
    cz_rect(0,136,24,28,s); cz_rect(24,144,14,20,s);
    cz_rect(40,118,16,46,s); cz_rect(44,110,8,8,s); cz_rect(46,106,4,4,s);         /* Sultan Abdul Samad clock tower */
    cz_rect(56,140,20,24,s); cz_rect(76,148,10,16,s);
    cz_rect(148,92,10,72,s); cz_rect(150,82,6,10,s); cz_rect(152,72,2,10,s);       /* Petronas twin 1 */
    cz_rect(168,92,10,72,s); cz_rect(170,82,6,10,s); cz_rect(172,72,2,10,s);       /* Petronas twin 2 */
    cz_rect(158,122,10,2,s);                                                       /* skybridge */
    cz_rect(180,138,12,26,s); cz_rect(192,146,16,18,s);
    cz_rect(218,108,4,56,s); cz_rect(214,116,12,8,s); cz_rect(216,112,8,4,s); cz_rect(218,100,4,8,s); /* KL Tower */
    cz_rect(226,150,14,14,s);
    int i;
    if (night) for (i = 0; i < 12; i++) cz_rect(lit[i][0], lit[i][1], 2, 2, CZ_C_GOLD); /* full-scale coordinates */
}

/* Marina Bay: Sands with its SkyPark overhanging north, the CBD behind, the Esplanade's durian domes, the Flyer. */
static void sg(int night) {
    static const uint8_t lit[][2] = {{8,52},{15,52},{22,52},{8,62},{22,62},{15,70},{8,74},{22,74},   /* Sands, 2x scale */
                                     {68,52},{70,60},{76,56},{78,64},{84,58},{84,70}};               /* CBD */
    const int s = CZ_C_DEEP;
    int i, x;
    for (i = 0; i < 3; i++) {                                     /* three slim towers, each on two legs */
        x = 14 + 14 * i;
        cz_rect(x, 96, 8, 68, s);
        cz_rect(x - 1, 150, 10, 14, s);
        cz_rect(x + 3, 136, 2, 28, band);                          /* the split between the legs shows sky */
    }
    cz_rect(0, 90, 50, 6, s); cz_rect(2, 88, 44, 2, s);           /* SkyPark: long, cantilevered well past the north tower */
    cz_rect(8, 86, 4, 2, s); cz_rect(24, 86, 6, 2, s); cz_rect(38, 86, 2, 2, s); /* its trees */
    cz_rect(130, 108, 12, 56, s); cz_rect(144, 92, 14, 72, s); cz_rect(150, 84, 2, 8, s);
    cz_rect(160, 116, 12, 48, s); cz_rect(174, 128, 10, 36, s);   /* the CBD, peeking past the pet */
    cz_rect(62, 152, 14, 12, s); cz_rect(64, 148, 10, 4, s); cz_rect(66, 146, 6, 2, s); /* Esplanade */
    cz_rect(78, 154, 12, 10, s); cz_rect(80, 150, 8, 4, s);
    for (x = 64; x < 90; x += 4) cz_rect(x, 156 - (x & 4), 2, 2, CZ_C_NAVY);  /* their spikes catch the light */
    ring2(206, 112, 28, s);                                       /* Singapore Flyer */
    for (i = 0; i < 8; i++) {
        static const int8_t sp[8][2] = {{28,0},{20,20},{0,28},{-20,20},{-28,0},{-20,-20},{0,-28},{20,-20}};
        line2(206, 112, 206 + sp[i][0], 112 + sp[i][1], s);
    }
    cz_rect(202, 108, 10, 10, s);                                 /* hub */
    line2(206, 116, 190, 156, s); line2(206, 116, 222, 156, s);   /* the A-frame legs */
    cz_rect(184, 154, 52, 10, s); cz_rect(236, 158, 4, 6, s);     /* terminal */
    if (night) {
        px2(CZ_C_GOLD, lit, 14, 2);
        for (x = 2; x < 48; x += 4) cz_rect(x, 92, 2, 2, CZ_C_GOLD); /* SkyPark rim lights */
        for (i = 0; i < 8; i++) {
            static const int8_t cap[8][2] = {{30,0},{21,21},{0,30},{-21,21},{-30,0},{-21,-21},{0,-30},{21,-21}};
            cz_rect(206 + cap[i][0], 112 + cap[i][1], 2, 2, CZ_C_GOLD);
        }
    }
}

/* Tokyo: Fuji far off, a torii, Tokyo Tower in its orange and white, the Skytree lit in its pale blue. */
static void tyo(int night) {
    static const uint8_t lit[][2] = {{4,74},{10,76},{38,78},{40,72},{90,76},{96,78},{110,74},{116,77}};
    const int s = CZ_C_DEEP;
    const int fuji = phase == CZ_PH_DAWN ? CZ_C_NAVY : CZ_C_DUSK; /* one step off the sky behind it */
    int y, w;
    for (y = 98; y < 164; y += 2) {                               /* Fuji: a flat crown, then slopes that spread */
        w = 8 + (y - 98) * (y - 98) / 40 + (y - 98) / 2;
        cz_rect(34 - w, y, 2 * w, 2, fuji);
        if (y < 110) cz_rect(34 - w + 2, y, 2 * w - 4, 2, CZ_C_CLOUD);
    }
    cz_rect(22, 110, 4, 2, CZ_C_CLOUD); cz_rect(32, 110, 6, 4, CZ_C_CLOUD); cz_rect(42, 110, 4, 2, CZ_C_CLOUD); /* the snow's ragged edge */
    cz_rect(0, 144, 14, 20, s); cz_rect(14, 150, 10, 14, s); cz_rect(72, 146, 12, 18, s); cz_rect(84, 152, 10, 12, s);
    cz_rect(102, 142, 10, 22, s); cz_rect(112, 148, 16, 16, s);   /* the low city */
    cz_rect(32, 144, 26, 2, CZ_C_CORAL); cz_rect(30, 142, 4, 2, CZ_C_CORAL); cz_rect(56, 142, 4, 2, CZ_C_CORAL); /* torii: kasagi */
    cz_rect(36, 148, 18, 2, CZ_C_CORAL);                                                                        /* nuki */
    cz_rect(38, 146, 2, 18, CZ_C_CORAL); cz_rect(50, 146, 2, 18, CZ_C_CORAL);                                   /* posts */
    for (y = 74; y < 164; y += 2) {                               /* Tokyo Tower: tapering lattice, bands of white */
        w = 1 + (y - 74) * (y - 74) / 600 + (y - 74) / 8;
        cz_rect(160 - w, y, 2 * w, 2, (y - 74) % 22 < 4 && y > 80 ? CZ_C_INK : CZ_C_CORAL);
    }
    cz_rect(159, 62, 2, 12, CZ_C_CORAL);                          /* antenna */
    cz_rect(153, 98, 14, 4, CZ_C_CORAL); cz_rect(148, 122, 24, 6, CZ_C_CORAL); /* top deck, main deck */
    for (y = 146; y < 164; y += 2) cz_rect(160 - (y - 144) / 2, y, y - 144, 2, band); /* the arch between the legs */
    cz_rect(180, 140, 14, 24, s); cz_rect(194, 148, 14, 16, s); cz_rect(230, 146, 10, 18, s);
    cz_rect(219, 64, 2, 16, s);                                   /* Skytree: antenna, Tembo Galleria, Tembo Deck, shaft */
    cz_rect(216, 80, 8, 6, s); cz_rect(218, 86, 4, 14, s); cz_rect(214, 100, 12, 8, s);
    cz_rect(217, 108, 6, 42, s); cz_rect(214, 150, 12, 14, s); cz_rect(212, 158, 16, 6, s);
    if (night) {
        px2(CZ_C_GOLD, lit, 8, 2);
        for (y = 86; y < 150; y += 8) cz_rect(219, y, 2, 2, CZ_C_TEAL); /* the Iki lighting */
        cz_rect(216, 103, 8, 2, CZ_C_TEAL); cz_rect(218, 82, 4, 2, CZ_C_TEAL);
    }
}

/* By time of day (PRD 2.7). Horizon at y=164. Pixel (20,20) is the top sky colour, distinct per phase. */
static void skyline(void) {
    static const uint8_t stars[][2] = {{20,15},{35,9},{75,12},{95,20},{55,6},{100,35},{15,35}};
    static const uint8_t top[4] = {CZ_C_NAVY, CZ_C_DUSK, CZ_C_DAYSKY, CZ_C_ROSE};
    static const uint8_t bands[4] = {CZ_C_DUSK, CZ_C_ROSE, CZ_C_DAYHORIZON, CZ_C_DUSK};
    const int night = phase == CZ_PH_NIGHT || phase == CZ_PH_DUSK;
    int x, y;
    cz_rect(0, 0, CZ_W, CZ_H, top[phase]);
    band = bands[phase];
    cz_rect(0, 128, CZ_W, 36, band);
    if (phase == CZ_PH_NIGHT) { px2(CZ_C_INK, stars, 7, 2); cz_icon(CZ_I_MOON, 180, 44, CZ_C_INK, 2); }
    if (phase == CZ_PH_DAWN) {             /* rising in the rose band, clear of the pet: on Fuji's crown, through the Flyer */
        static const uint8_t sun[CZ_SKIES][2] = {{12, 120}, {198, 104}, {26, 82}};
        cz_icon(CZ_I_SUN, sun[sky][0], sun[sky][1], CZ_C_GOLD, 2);
    }
    if (phase == CZ_PH_DAY) {
        cz_icon(CZ_I_SUN, 180, 44, CZ_C_GOLD, 2);
        cz_rect(30, 56, 16, 4, CZ_C_CLOUD); cz_rect(24, 60, 30, 6, CZ_C_CLOUD);
        cz_rect(192, 80, 14, 4, CZ_C_CLOUD); cz_rect(186, 84, 28, 6, CZ_C_CLOUD);
    }
    if (sky == CZ_SKY_SG) sg(night); else if (sky == CZ_SKY_TYO) tyo(night); else kl(night);
    cz_rect(0, 164, CZ_W, 2, CZ_C_GOLD);   /* horizon */
    cz_rect(0, 166, CZ_W, 74, CZ_C_DEEP);  /* ground */
    for (y = 168; y < 240; y += 4)
        for (x = (y / 4) % 2 ? 0 : 2; x < CZ_W; x += 4) cz_rect(x, y, 2, 2, CZ_C_NAVY);
    if (sky == CZ_SKY_SG && night) {       /* Marina Bay holds the lights: broken streaks that thin with distance */
        static const uint8_t ripple[][3] = {{14,170,6},{24,170,4},{36,170,6},{10,176,4},{30,176,4},{18,182,2},{40,182,2},{26,188,2},
                                            {200,170,4},{212,170,4},{206,176,2},{218,176,2},{210,182,2}};
        for (x = 0; x < 13; x++) cz_rect(ripple[x][0], ripple[x][1], ripple[x][2], 2, x < 8 && x % 3 == 1 ? CZ_C_TEAL : CZ_C_GOLD);
    }
}

static void hearts(int n) {
    int i, x0 = CX - (4 * 18 - 2) / 2;
    for (i = 0; i < 4; i++) cz_icon(i < n ? CZ_I_HEART : CZ_I_HEARTO, x0 + i * 18, 12, i < n ? CZ_C_CORAL : CZ_C_INK, 2);
}

static void sprite(const struct cz_view *v, int x, int y) { cz_blit(cz_sprite(v->creature, v->pose), x, y, 2, 0); }

/* ---- Animation (PRD 2.6): a 4-step cycle from the single sprite plus integer offsets, so every creature
 * animates the same way by construction. Bit layout per screen is private to this file. */
static const int BOB[4] = {0, 1, 2, 1};

int cz_anim(int screen, uint32_t t, uint32_t screen_ms) {
    int s = (int)(t / 250) & 3, c;
    switch (screen) {
    case CZ_S_HOME: return (int)(t / 600) & 3;                                  /* breath */
    case CZ_S_SULK: return ((int)(t / 600) & 3) | s << 2 |                      /* breath, shiver, drop 1..4 */
                           (t % 2000 < 1000 ? 1 + (int)(t % 1000 / 250) : 0) << 4;
    case CZ_S_FEED: c = 3 - (int)(screen_ms / 750); return s | (c < 0 ? 0 : c) << 2; /* bob, crumbs 3..0 */
    case CZ_S_PLAY: case CZ_S_CHECKIN: return s;
    case CZ_S_TALK: return (s & 1) | ((int)(t / 500) & 1) << 1;                 /* bob, cursor off */
    case CZ_S_SLEEP: return (int)(t / 1200) & 3;                                /* slow breath, zz drift */
    case CZ_S_QR: return (int)(t / 1200) & 1;
    case CZ_S_EGG: /* 0 still, 1-6 wobble (3 swings x 2 sides), 7-10 crack, 11-13 burst, 16-19 reveal bob */
        if (screen_ms >= 2800) return 16 | ((int)(t / 600) & 3);
        if (screen_ms >= 2400) return 11 + (int)((screen_ms - 2400) / 134);
        if (screen_ms >= 1800) return 7 + (int)((screen_ms - 1800) / 150);
        for (c = 0; c < 3; c++) {
            static const uint16_t at[3] = {0, 700, 1300};
            if (screen_ms >= at[c] && screen_ms < at[c] + 300u) return 1 + 2 * c + (int)((screen_ms - at[c]) / 75 & 1);
        }
        return 0;
    default: return 0;
    }
}

/* x0, y0, x1, y1 (exclusive): the sprite box (56,36)-(184,174) trimmed to what moves, plus overlays outside it */
static const uint8_t ANIM_BOX[CZ_SCREENS][4] = {
    [CZ_S_HOME] = {56, 44, 184, 174}, [CZ_S_SULK] = {56, 44, 184, 174}, [CZ_S_SLEEP] = {56, 44, 184, 174},
    [CZ_S_FEED] = {56, 42, 184, 174}, [CZ_S_PLAY] = {56, 36, 184, 174},
    [CZ_S_TALK] = {56, 36, 194, 174},    /* + the bubble interior, where the cursor blinks */
    [CZ_S_CHECKIN] = {48, 30, 196, 188}, /* sprite sits at y 60..187 above the band, + both confetti patterns */
    [CZ_S_QR] = {0, 120, 74, 186},       /* the 1x pet left of the panel, for every QR version */
    [CZ_S_EGG] = {0, 0, 240, 240},       /* every beat redraws the whole screen */
};

void cz_anim_box(int screen, int *x, int *y, int *w, int *h) {
    const uint8_t *b = ANIM_BOX[screen >= 0 && screen < CZ_SCREENS ? screen : CZ_S_HATCH];
    *x = b[0]; *y = b[1]; *w = b[2] - b[0]; *h = b[3] - b[1];
}

static void actions(int active) { /* active: icon id or -1 */
    static const int names[3] = {CZ_I_BOWL, CZ_I_BALL, CZ_I_BUBBLE};
    int i;
    for (i = 0; i < 3; i++) cz_icon(names[i], 68 + i * 40, 176, names[i] == active ? CZ_C_GOLD : CZ_C_INK, 3);
}

static void footer(const char *s) { cz_text(s, CX, 208, CZ_C_GOLD, 3, CZ_CENTER); }

static void banner(const char *a, const char *b) {
    cz_rect(0, 188, CZ_W, 52, CZ_C_GOLD);
    cz_text(a, CX, 194, CZ_C_DEEP, 3, CZ_CENTER);
    if (b && *b) cz_text(b, CX, 212, CZ_C_DEEP, 2, CZ_CENTER);
}

static void cat(char *dst, int cap, const char *a, const char *b) {
    int n = 0;
    while (*a && n < cap - 1) dst[n++] = *a++;
    while (*b && n < cap - 1) dst[n++] = *b++;
    dst[n] = 0;
}

/* Greedy word wrap of s[0..len) at `cols` characters. Writes up to `max` (start,len) pairs; returns the line count. */
static int wrap(const char *s, int len, int cols, uint8_t (*out)[2], int max) {
    int pos = 0, n = 0;
    while (pos < len && n < max) {
        int end, cut = -1, i;
        while (pos < len && s[pos] == ' ') pos++;
        if (pos >= len) break;
        if (len - pos <= cols) end = len;
        else {
            for (i = pos + cols; i > pos; i--) if (s[i] == ' ') { cut = i; break; }
            end = cut > 0 ? cut : pos + cols;
        }
        out[n][0] = (uint8_t)pos; out[n][1] = (uint8_t)(end - pos); n++;
        pos = end;
    }
    return n;
}

static void line_text(char *buf, const char *s, const uint8_t *ln) {
    int i;
    for (i = 0; i < ln[1]; i++) buf[i] = s[ln[0] + i];
    buf[i] = 0;
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void home(const struct cz_view *v) {
    skyline(); sprite(v, 56, 44 + BOB[v->anim & 3]);
    if (!v->portrait) { hearts(v->hearts); actions(-1); }
    footer(v->name);
}

static void eat(const struct cz_view *v) {
    int i;
    skyline(); sprite(v, 56, v->anim & 1 ? 42 : 44); hearts(v->hearts);
    /* the Eat sprite carries its own bowl; rice crumbs on the ground under it go 3, 2, 1, 0 over the 3 s */
    for (i = 0; i < v->anim >> 2; i++) cz_rect(110 + 8 * i, 170 + 2 * (i & 1), 2, 2, CZ_C_GOLD);
    cz_text("+1", 40, 100, CZ_C_GOLD, 3, CZ_CENTER);
    banner("NASI LEMAK", "+1 HEART");
}

static void play(const struct cz_view *v) {
    static const uint8_t spark[][2] = {{26,44},{32,38},{40,34}};
    static const int hop[4] = {0, -6, -10, -6};
    static const uint8_t arc[4][2] = {{64,120},{84,80},{144,80},{164,120}}; /* ball tossed over the pet */
    int s = v->anim & 3;
    skyline();
    cz_blit(cz_sprite(v->creature, v->pose), 56, 44 + hop[s], 2, s == 0); /* squash on landing */
    hearts(v->hearts); px2(CZ_C_GOLD, spark, 3, 2);
    cz_icon(CZ_I_BALL, arc[s][0], arc[s][1], CZ_C_GOLD, 2);
    actions(CZ_I_BALL); footer("PLAY");
}

static void talk(const struct cz_view *v) {
    uint8_t ln[CZ_MSG_MAX][2];
    char buf[CZ_MSG_MAX + 1], url[32];
    int n, i, w = 0;
    skyline(); sprite(v, 56, 44 - (v->anim & 1)); hearts(v->hearts);
    cz_rect(116, 34, 80, 38, CZ_C_INK); cz_rect(118, 36, 76, 34, CZ_C_NAVY);
    cz_rect(124,72,8,2,CZ_C_INK); cz_rect(122,74,6,2,CZ_C_INK); cz_rect(120,76,4,2,CZ_C_INK); cz_rect(118,78,2,2,CZ_C_INK);
    /* shortcut: bubble shows the first 2 lines (16 chars) of the message, scroll it if longer messages matter */
    n = wrap(v->msg, slen(v->msg), 8, ln, 2);
    for (i = 0; i < n; i++) { line_text(buf, v->msg, ln[i]); w = cz_text(buf, 124, 40 + i * 14, CZ_C_INK, 2, CZ_LEFT); }
    if (!(v->anim & 2)) cz_rect(124 + w, n > 1 ? 54 : 40, 2, 10, CZ_C_GOLD); /* cursor blinks at 500 ms */
    cat(url, sizeof url, "PRMPT.CITY/", v->name);
    banner("REPLY", url);
}

/* QR (PRD 2.4): 6 px modules for version <= 2, 5 px for version 3, a 2-module cream quiet zone, panel centred
 * across and resting on the gold band so the finder corners stay inside the round glass.
 * pet >= 0 peeks from behind the panel. Text too long for version 5 draws no code; the banner still shows. */
static void qr_panel(const char *text, int pet, int bob) {
    static uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(5)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
    skyline(); cz_dim(75);
    /* LOW + boost: the smallest version, with the ECC raised when it still fits; a lit screen does not smudge */
    if (qrcodegen_encodeText(text, tmp, code, qrcodegen_Ecc_LOW, 1, 5, qrcodegen_Mask_AUTO, true)) {
        int n = qrcodegen_getSize(code), m = n <= 25 ? 6 : n <= 29 ? 5 : 4; /* 4 px only for long custom base URLs */
        int p = (n + 4) * m, px = (CZ_W - p) / 2, py = 188 - p, ox = px + 2 * m, oy = py + 2 * m, r, c;
        if (pet >= 0) cz_blit(cz_sprite(pet, CZ_IDLE), px - 36, 120 + bob, 1, 0);
        cz_rect(px, py, p, p, CZ_C_INK);
        for (r = 0; r < n; r++)
            for (c = 0; c < n; c++)
                if (qrcodegen_getModule(code, c, r)) cz_rect(ox + c * m, oy + r * m, m, m, CZ_C_DEEP);
    }
}

static void qr(const struct cz_view *v) {
    char with[24];
    qr_panel(v->url, v->creature, v->anim);
    cat(with, sizeof with, "CHAT WITH ", v->name);
    banner(with, "CONNECT.PRMPT.CITY"); /* the brand and the fallback for anyone who cannot scan */
}

/* Portal: a phone camera joins the badge's own WPA2 access point from this code, or types the password shown */
static void portal(const struct cz_view *v) {
    char wifi[72], join[32], pass[24];
    cat(wifi, sizeof wifi, "WIFI:T:WPA;S:", v->ap);
    cat(wifi, sizeof wifi, wifi, ";P:");
    cat(wifi, sizeof wifi, wifi, v->ap_pass);
    cat(wifi, sizeof wifi, wifi, ";;");
    qr_panel(wifi, -1, 0);
    cat(join, sizeof join, "JOIN ", v->ap);
    cat(pass, sizeof pass, "PASS ", v->ap_pass);
    cz_rect(0, 188, CZ_W, 52, CZ_C_GOLD);
    cz_text(join, CX, 191, CZ_C_DEEP, 2, CZ_CENTER);
    cz_text(pass, CX, 204, CZ_C_DEEP, 2, CZ_CENTER);
    cz_text("192.168.4.1", CX, 217, CZ_C_DEEP, 2, CZ_CENTER);
}

/* Egg (hatch.md, Badge): an ink oval in 4 px rows that wobbles, cracks, bursts, then the new creature */
static void egg(const struct cz_view *v) {
    static const uint8_t half[20] = {10, 16, 20, 23, 25, 27, 28, 29, 30, 31, 32, 32, 32, 31, 30, 29, 27, 24, 20, 13};
    static const uint8_t spots[4][2] = {{102, 96}, {130, 88}, {112, 140}, {136, 128}};
    static const int8_t ray[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    int a = v->anim, dx = 0, i, x;
    skyline(); cz_dim(60);
    if (a >= 16) { /* reveal */
        sprite(v, 56, 44 + BOB[a & 3]);
        banner("NEW CITIZEN", v->name);
        return;
    }
    if (a >= 1 && a <= 6) dx = ((a - 1) / 2 + 1) * 2 * ((a - 1) & 1 ? -1 : 1); /* +-2, +-4, +-6 */
    for (i = 0; i < 20; i++) cz_rect(CX + dx - half[i], 80 + 4 * i, 2 * half[i], 4, CZ_C_INK);
    for (i = 0; i < 4; i++) cz_rect(spots[i][0] + dx, spots[i][1], 8, 6, i & 1 ? CZ_C_CORAL : CZ_C_GOLD);
    if (a >= 7) /* the crack: 8 zig-zag segments across the middle, 2 more per step */
        for (i = 0, x = 88; i < (a >= 11 ? 8 : 2 * (a - 6)); i++, x += 8) line2(x, i & 1 ? 124 : 116, x + 8, i & 1 ? 116 : 124, CZ_C_DEEP);
    if (a >= 11) { /* burst: 8 gold spokes growing in 3 steps, a ring flash */
        int len = 24 * (a - 10);
        for (i = 0; i < 8; i++) line2(CX, 120, CX + ray[i][0] * len, 120 + ray[i][1] * len, CZ_C_GOLD);
        ring2(CX, 120, len + 8, CZ_C_PALE);
    }
}

static void cheer(const struct cz_view *v) {
    static const uint8_t gold[][2] = {{20,24},{60,20},{24,40},{58,44},{34,18},{46,16}};
    static const uint8_t coral[][2] = {{16,30},{64,34},{40,12}};
    char city[32];
    int i, alt = v->anim & 2 ? 2 : 0; /* second pattern: gold falls, coral rises, 2 cells each */
    skyline(); sprite(v, 56, v->anim & 1 ? 60 : 64); hearts(v->hearts);
    for (i = 0; i < 6; i++) cz_rect(gold[i][0] * 3, (gold[i][1] + alt) * 3, 3, 3, CZ_C_GOLD);
    for (i = 0; i < 3; i++) cz_rect(coral[i][0] * 3, (coral[i][1] - alt) * 3, 3, 3, CZ_C_CORAL);
    cat(city, sizeof city, "PROMPT:", v->city);
    banner("CHECKED IN", city);
}

static void sulk(const struct cz_view *v) {
    static const int shiver[4] = {-1, 0, 1, 0};
    int drop = v->anim >> 4;
    skyline(); sprite(v, 56 + shiver[(v->anim >> 2) & 3], 44 + BOB[v->anim & 3]);
    if (drop) cz_icon(CZ_I_DROP, 144, 84 + 2 * drop, CZ_C_TEAL, 1); /* sweat drop slides down the head, every 2 s */
    hearts(v->hearts); actions(CZ_I_BOWL); footer("HUNGRY");
}

static void sleep_(const struct cz_view *v) {
    skyline(); cz_dim(60); sprite(v, 56, 44 + BOB[v->anim & 3]); cz_icon(CZ_I_ZZ, 168, 56 - 2 * (v->anim & 3), CZ_C_GOLD, 2); hearts(v->hearts); footer("ZZZ");
}

static void hatch(const struct cz_view *v) {
    static const uint8_t stars[][2] = {{20,15},{75,12},{100,35},{15,35}};
    uint8_t ln[CZ_PROMPT_MAX][2];
    char buf[11];
    int n, first, i, w = 0, y = 28;
    cz_rect(0, 0, CZ_W, CZ_H, CZ_C_DEEP);
    px2(CZ_C_INK, stars, 4, 2);
    n = wrap(v->prompt, v->typed, 10, ln, CZ_PROMPT_MAX);
    first = n > 5 ? n - 5 : 0; /* typewriter scroll: keep the last 5 lines */
    for (i = first; i < n; i++) {
        line_text(buf, v->prompt, ln[i]);
        y = 28 + (i - first) * 16;
        w = cz_text(buf, CX, y, CZ_C_INK, 2, CZ_CENTER);
    }
    cz_rect(CX - w / 2 + w + 5, y, 2, 10, CZ_C_GOLD);
    sprite(v, 56, cz_hatch_y); /* drawn before the band, so it rises from under it */
    banner("HATCHING", v->name);
}

/* Settings (PRD 2.8): previous and next items in ink at size 2, the selected item and its value in gold. */
static void tri(int y, int up) { int i; for (i = 0; i < 3; i++) cz_rect(CX - 1 - 2 * i, up ? y + 2 * i : y + 4 - 2 * i, 2 + 4 * i, 2, CZ_C_INK); }

/* a sideways triangle 5 px wide, pointing left (dir -1) or right (+1), top-left at x, y */
static void arrow(int x, int y, int dir) { int i; for (i = 0; i < 3; i++) cz_rect(dir < 0 ? x + 2 * i : x + 4 - 2 * i, y + 4 - 2 * i, 2, 2 + 4 * i, CZ_C_GOLD); }

static void settings(const struct cz_view *v) {
    cz_rect(0, 0, CZ_W, CZ_H, CZ_C_DEEP);
    tri(30, 1);
    cz_text(v->set_prev, CX, 48, CZ_C_INK, 2, CZ_CENTER);
    cz_text(v->set_label, CX, 84, CZ_C_GOLD, 3, CZ_CENTER);
    cz_text(v->set_value, CX, 108, CZ_C_GOLD, 2, CZ_CENTER);
    if (v->set_arrows & 1) arrow(30, 106, -1);
    if (v->set_arrows & 2) arrow(204, 106, 1);
    cz_text(v->set_next, CX, 140, CZ_C_INK, 2, CZ_CENTER);
    tri(160, 0);
    banner(v->screen == CZ_S_WIFI ? "WIFI" : "SETTINGS", 0);
}

void cz_draw(const struct cz_view *v) {
    phase = v->phase;
    sky = v->sky;
    switch (v->screen) {
    case CZ_S_SETTINGS: case CZ_S_WIFI: settings(v); break;
    case CZ_S_EGG: egg(v); break;
    case CZ_S_PORTAL: portal(v); break;
    case CZ_S_HATCH: hatch(v); break;
    case CZ_S_FEED: eat(v); break;
    case CZ_S_PLAY: play(v); break;
    case CZ_S_TALK: talk(v); break;
    case CZ_S_QR: qr(v); break;
    case CZ_S_CHECKIN: cheer(v); break;
    case CZ_S_SULK: sulk(v); break;
    case CZ_S_SLEEP: sleep_(v); break;
    default: home(v); break;
    }
}
