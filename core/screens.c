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

/* KL by time of day (PRD 2.7). Horizon at y=164. Pixel (20,20) is the top sky colour, distinct per phase. */
static void skyline(void) {
    static const uint8_t stars[][2] = {{20,15},{35,9},{75,12},{95,20},{55,6},{100,35},{15,35}};
    static const uint8_t lit[][2] = {{150,102},{154,112},{170,108},{174,130},{60,148},{66,154},
                                     {6,144},{184,144},{228,154},{150,136},{172,148},{46,122}};
    static const uint8_t top[4] = {CZ_C_NAVY, CZ_C_DUSK, CZ_C_DAYSKY, CZ_C_ROSE};
    static const uint8_t band[4] = {CZ_C_DUSK, CZ_C_ROSE, CZ_C_DAYHORIZON, CZ_C_DUSK};
    const int s = CZ_C_DEEP;
    int x, y;
    cz_rect(0, 0, CZ_W, CZ_H, top[phase]);
    cz_rect(0, 128, CZ_W, 36, band[phase]);
    if (phase == CZ_PH_NIGHT) { px2(CZ_C_INK, stars, 7, 2); cz_icon(CZ_I_MOON, 180, 44, CZ_C_INK, 2); }
    if (phase == CZ_PH_DAWN) cz_icon(CZ_I_SUN, 12, 120, CZ_C_GOLD, 2);     /* rising in the rose band, clear of the pet */
    if (phase == CZ_PH_DAY) {
        cz_icon(CZ_I_SUN, 180, 44, CZ_C_GOLD, 2);
        cz_rect(30, 56, 16, 4, CZ_C_CLOUD); cz_rect(24, 60, 30, 6, CZ_C_CLOUD);
        cz_rect(192, 80, 14, 4, CZ_C_CLOUD); cz_rect(186, 84, 28, 6, CZ_C_CLOUD);
    }
    cz_rect(0,136,24,28,s); cz_rect(24,144,14,20,s);
    cz_rect(40,118,16,46,s); cz_rect(44,110,8,8,s); cz_rect(46,106,4,4,s);         /* Sultan Abdul Samad clock tower */
    cz_rect(56,140,20,24,s); cz_rect(76,148,10,16,s);
    cz_rect(148,92,10,72,s); cz_rect(150,82,6,10,s); cz_rect(152,72,2,10,s);       /* Petronas twin 1 */
    cz_rect(168,92,10,72,s); cz_rect(170,82,6,10,s); cz_rect(172,72,2,10,s);       /* Petronas twin 2 */
    cz_rect(158,122,10,2,s);                                                       /* skybridge */
    cz_rect(180,138,12,26,s); cz_rect(192,146,16,18,s);
    cz_rect(218,108,4,56,s); cz_rect(214,116,12,8,s); cz_rect(216,112,8,4,s); cz_rect(218,100,4,8,s); /* KL Tower */
    cz_rect(226,150,14,14,s);
    if (phase == CZ_PH_NIGHT || phase == CZ_PH_DUSK) px2(CZ_C_GOLD, lit, 12, 2);
    cz_rect(0, 164, CZ_W, 2, CZ_C_GOLD);   /* horizon */
    cz_rect(0, 166, CZ_W, 74, CZ_C_DEEP);  /* ground */
    for (y = 168; y < 240; y += 4)
        for (x = (y / 4) % 2 ? 0 : 2; x < CZ_W; x += 4) cz_rect(x, y, 2, 2, CZ_C_NAVY);
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

static void home(const struct cz_view *v) { skyline(); sprite(v, 56, 44 + BOB[v->anim & 3]); hearts(v->hearts); actions(-1); footer(v->name); }

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
 * across and resting on the gold band so the finder corners stay inside the round glass. */
static void qr(const struct cz_view *v) {
    static uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(5)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
    char with[24];
    skyline(); cz_dim(75);
    /* LOW + boost: the smallest version, with the ECC raised when it still fits; a lit screen does not smudge */
    if (qrcodegen_encodeText(v->url, tmp, code, qrcodegen_Ecc_LOW, 1, 5, qrcodegen_Mask_AUTO, true)) {
        int n = qrcodegen_getSize(code), m = n <= 25 ? 6 : n <= 29 ? 5 : 4; /* 4 px only for long custom base URLs */
        int p = (n + 4) * m, px = (CZ_W - p) / 2, py = 188 - p, ox = px + 2 * m, oy = py + 2 * m, r, c;
        cz_blit(cz_sprite(v->creature, CZ_IDLE), px - 36, 120 + v->anim, 1, 0); /* small pet peeks from behind the panel */
        cz_rect(px, py, p, p, CZ_C_INK);
        for (r = 0; r < n; r++)
            for (c = 0; c < n; c++)
                if (qrcodegen_getModule(code, c, r)) cz_rect(ox + c * m, oy + r * m, m, m, CZ_C_DEEP);
    } /* a URL too long for version 5 draws no code; the banner still shows */
    cat(with, sizeof with, "WITH ", v->name);
    banner("SCAN TO CHAT", with);
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

static void settings(const struct cz_view *v) {
    cz_rect(0, 0, CZ_W, CZ_H, CZ_C_DEEP);
    tri(30, 1);
    cz_text(v->set_prev, CX, 48, CZ_C_INK, 2, CZ_CENTER);
    cz_text(v->set_label, CX, 84, CZ_C_GOLD, 3, CZ_CENTER);
    cz_text(v->set_value, CX, 108, CZ_C_GOLD, 2, CZ_CENTER);
    cz_text(v->set_next, CX, 140, CZ_C_INK, 2, CZ_CENTER);
    tri(160, 0);
    banner("SETTINGS", 0);
}

void cz_draw(const struct cz_view *v) {
    phase = v->phase;
    switch (v->screen) {
    case CZ_S_SETTINGS: settings(v); break;
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
