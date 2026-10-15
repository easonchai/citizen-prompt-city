/* Citizen state machine: timers, hearts, gestures, screens, save/restore. See DESIGN.md. */
#include "citizen.h"
#include "gfx.h"

#define MIN_MS        60000UL
#define HEART_LOSS_MS (90UL * MIN_MS)
#define PLAY_GAP_MS   (30UL * MIN_MS)
#define SULK_MS       (360UL * MIN_MS)
#define AGE_CAP_MS    (7UL * 24 * 60 * MIN_MS)  /* keeps ms counters far from uint32 overflow */
#define IDLE_SLEEP_MS 30000UL
#define QR_TIMEOUT_MS 30000UL
#define TAP_MS 300
#define TAP_PX 12
#define SWIPE_MS 600
#define SWIPE_PX 40
#define HOLD_MS 1000
#define DOUBLE_TAP_MS 400
#define HATCH_CHAR_MS 60
#define HATCH_HOLD_MS 2000
#define BLINK_EVERY_MS 4000
#define BLINK_MS 150
#define NAME_MAX 10
#define CITY_MAX 16
#define URL_MAX 128

#define SAVE_MAGIC0 'C'
#define SAVE_MAGIC1 'Z'
#define SAVE_VERSION 2

static const char *const DEFAULT_NAME[CZ_CREATURES] = {"TENUK", "KENYALANG", "HARIMAU"};
static const char DEFAULT_PROMPT[] = "YOU ARE A SKEPTICAL OPS LEAD WHO EATS CLEAN DATA";

static struct {
    int creature, hearts, hatched, lost; /* lost: hearts lost since the last feed */
    int screen;
    uint32_t hunger_ms, play_age_ms, screen_ms, idle_ms;
    uint32_t last_now;
    int have_now;
    /* gesture */
    int down, consumed, x0, y0, maxd, tapped; /* tapped: last_tap is a tap that can start a double tap */
    uint32_t t0, last_tap;
    int brightness;
    /* content */
    char name[NAME_MAX + 1], prompt[CZ_PROMPT_MAX + 1], msg[CZ_MSG_MAX + 1], city[CITY_MAX + 1], url[URL_MAX + 1];
    /* rendering */
    int stale, dirty;
    uint32_t drawn_key;
} S;

static uint8_t scratch[256];

static void copy(char *dst, int cap, const char *src) {
    int n = 0;
    if (src) while (src[n] && n < cap - 1) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

static int sulking(void) { return S.hearts <= 1 || S.hunger_ms >= SULK_MS; }
static int base_screen(void) { return sulking() ? CZ_S_SULK : CZ_S_HOME; }

static void go(int screen) {
    S.screen = screen;
    S.screen_ms = 0;
    S.idle_ms = 0;
}

static void age(uint32_t dt) {
    if (dt > AGE_CAP_MS) dt = AGE_CAP_MS;
    S.hunger_ms = S.hunger_ms + dt > AGE_CAP_MS ? AGE_CAP_MS : S.hunger_ms + dt;
    S.play_age_ms = S.play_age_ms + dt > AGE_CAP_MS ? AGE_CAP_MS : S.play_age_ms + dt;
    while (S.hearts > 0 && S.hunger_ms >= (uint32_t)(S.lost + 1) * HEART_LOSS_MS) { S.hearts--; S.lost++; }
}

static int typed_len(void) {
    int n = 0, shown = (int)(S.screen_ms / HATCH_CHAR_MS);
    while (S.prompt[n] && n < shown) n++;
    return n;
}

static int prompt_len(void) { int n = 0; while (S.prompt[n]) n++; return n; }

/* Hatch sprite top: 188 (under the band) while typing, then quadratic ease-out to 100 over the hold. */
#define HATCH_Y_HIDDEN 188
#define HATCH_Y_REST 100
int cz_hatch_y = HATCH_Y_REST; /* read by screens.c */
static int hatch_y(void) {
    uint32_t t = (uint32_t)prompt_len() * HATCH_CHAR_MS, u;
    if (S.screen_ms <= t) return HATCH_Y_HIDDEN;
    u = S.screen_ms - t >= HATCH_HOLD_MS ? 0 : HATCH_HOLD_MS - (S.screen_ms - t);
    return HATCH_Y_REST + (int)((HATCH_Y_HIDDEN - HATCH_Y_REST) * u * u / ((uint32_t)HATCH_HOLD_MS * HATCH_HOLD_MS));
}

static uint32_t event_ms(int screen) {
    switch (screen) {
    case CZ_S_FEED: case CZ_S_PLAY: return 3000;
    case CZ_S_CHECKIN: return 4000;
    case CZ_S_TALK: return 8000;
    case CZ_S_QR: return QR_TIMEOUT_MS;
    case CZ_S_HATCH: return (uint32_t)prompt_len() * HATCH_CHAR_MS + HATCH_HOLD_MS;
    default: return 0;
    }
}

static void advance(uint32_t now) {
    uint32_t dt, limit;
    if (!S.have_now) { S.have_now = 1; S.last_now = now; return; }
    dt = now - S.last_now; /* wraps correctly */
    S.last_now = now;
    age(dt);
    S.screen_ms += dt;
    S.idle_ms += dt;
    limit = event_ms(S.screen);
    if (limit && S.screen_ms >= limit) {
        if (S.screen == CZ_S_HATCH) S.hatched = 1;
        go(base_screen());
    }
    if (S.screen == CZ_S_HOME && sulking()) S.screen = CZ_S_SULK;
    if (S.screen == CZ_S_SULK && !sulking()) S.screen = CZ_S_HOME;
    if ((S.screen == CZ_S_HOME || S.screen == CZ_S_SULK) && S.idle_ms >= IDLE_SLEEP_MS) go(CZ_S_SLEEP);
    /* hold fires while the finger is still down */
    if (S.down && !S.consumed && S.maxd < TAP_PX && now - S.t0 >= HOLD_MS) {
        S.consumed = 1;
        if (S.screen == CZ_S_HOME || S.screen == CZ_S_SULK) go(CZ_S_QR);
    }
}

static void feed(void) {
    if (S.hearts < 4) S.hearts++;
    S.hunger_ms = 0;
    S.lost = 0;
    go(CZ_S_FEED);
}

static void play(void) {
    if (S.play_age_ms >= PLAY_GAP_MS) {
        if (S.hearts < 4) S.hearts++;
        S.play_age_ms = 0;
    }
    go(CZ_S_PLAY);
}

static void gesture_tap(uint32_t now) {
    if (S.tapped && now - S.last_tap < DOUBLE_TAP_MS) { /* second tap: brightness only, no tap action */
        S.tapped = 0;
        S.brightness = S.brightness == 100 ? 40 : S.brightness == 40 ? 10 : 100;
        return;
    }
    S.tapped = 1;
    S.last_tap = now;
    if (S.screen == CZ_S_HOME || S.screen == CZ_S_SULK) feed();
    else if (S.screen != CZ_S_HATCH) go(base_screen()); /* Sleep, QR and every event screen */
}

static void gesture_swipe(void) {
    if (S.screen == CZ_S_HOME || S.screen == CZ_S_SULK) play();
}

void cz_touch(int kind, int x, int y, uint32_t now_ms) {
    int dx = x - S.x0, dy = y - S.y0, d;
    advance(now_ms);
    S.idle_ms = 0;
    if (kind == CZ_TOUCH_DOWN) {
        S.down = 1; S.consumed = 0; S.x0 = x; S.y0 = y; S.maxd = 0; S.t0 = now_ms;
        return;
    }
    if (!S.down) return;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    d = dx > dy ? dx : dy;
    if (d > S.maxd) S.maxd = d;
    if (!S.consumed && dx >= SWIPE_PX && now_ms - S.t0 <= SWIPE_MS) { S.consumed = 1; gesture_swipe(); }
    if (kind == CZ_TOUCH_UP) {
        if (!S.consumed && now_ms - S.t0 < TAP_MS && S.maxd < TAP_PX) gesture_tap(now_ms);
        S.down = 0;
    }
}

void cz_tick(uint32_t now_ms) { advance(now_ms); }

/* ---- save blob: magic(2) version creature hearts hatched lost brightness hunger_s(4) play_s(4) name(11) ... crc(4) at the end */
static uint32_t checksum(const uint8_t *p, int n) { /* FNV-1a */
    uint32_t h = 2166136261UL;
    while (n--) { h ^= *p++; h *= 16777619UL; }
    return h;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int cz_save(uint8_t *out, int cap) {
    int i;
    if (!out || cap < CZ_SAVE_SIZE) return 0;
    for (i = 0; i < CZ_SAVE_SIZE; i++) out[i] = 0;
    out[0] = SAVE_MAGIC0; out[1] = SAVE_MAGIC1; out[2] = SAVE_VERSION;
    out[3] = (uint8_t)S.creature; out[4] = (uint8_t)S.hearts; out[5] = (uint8_t)S.hatched; out[6] = (uint8_t)S.lost;
    out[7] = (uint8_t)S.brightness;
    put32(out + 8, S.hunger_ms / 1000);
    put32(out + 12, S.play_age_ms / 1000);
    for (i = 0; i < NAME_MAX && S.name[i]; i++) out[16 + i] = (uint8_t)S.name[i];
    put32(out + CZ_SAVE_SIZE - 4, checksum(out, CZ_SAVE_SIZE - 4));
    return CZ_SAVE_SIZE;
}

static int restore(const uint8_t *b, int len) {
    if (!b || len < CZ_SAVE_SIZE || b[0] != SAVE_MAGIC0 || b[1] != SAVE_MAGIC1 || b[2] != SAVE_VERSION) return 0;
    if (get32(b + CZ_SAVE_SIZE - 4) != checksum(b, CZ_SAVE_SIZE - 4)) return 0;
    if (b[3] >= CZ_CREATURES || b[4] > 4 || b[6] > 4 || (b[7] != 100 && b[7] != 40 && b[7] != 10)) return 0;
    S.creature = b[3]; S.hearts = b[4]; S.hatched = b[5] != 0; S.lost = b[6]; S.brightness = b[7];
    S.hunger_ms = get32(b + 8) > AGE_CAP_MS / 1000 ? AGE_CAP_MS : get32(b + 8) * 1000;
    S.play_age_ms = get32(b + 12) > AGE_CAP_MS / 1000 ? AGE_CAP_MS : get32(b + 12) * 1000;
    copy(S.name, NAME_MAX + 1, (const char *)b + 16); /* bytes 16..26 always end in 0 */
    return 1;
}

static void default_url(void) {
    static const char base[] = "https://prmpt.city/c/";
    int n = 0, i;
    for (i = 0; base[i]; i++) S.url[n++] = base[i];
    for (i = 0; S.name[i] && n < URL_MAX; i++) S.url[n++] = (S.name[i] >= 'A' && S.name[i] <= 'Z') ? (char)(S.name[i] + 32) : S.name[i];
    S.url[n] = 0;
}

void cz_init(uint32_t seed, const uint8_t *saved, int saved_len, uint32_t slept_seconds) {
    uint8_t *p = (uint8_t *)&S;
    unsigned i;
    for (i = 0; i < sizeof S; i++) p[i] = 0;
    copy(S.prompt, sizeof S.prompt, DEFAULT_PROMPT);
    copy(S.city, sizeof S.city, "KL");
    S.stale = 1;
    S.brightness = 100;
    for (i = 0; i < CZ_STAT_COUNT; i++) cz_stats[i] = 0;
    if (restore(saved, saved_len)) {
        if (slept_seconds > AGE_CAP_MS / 1000) slept_seconds = AGE_CAP_MS / 1000;
        age(slept_seconds * 1000);
    } else {
        S.creature = (int)(seed % CZ_CREATURES);
        S.hearts = 3;
        S.play_age_ms = PLAY_GAP_MS;
        copy(S.name, sizeof S.name, DEFAULT_NAME[S.creature]);
    }
    default_url();
    go(S.hatched ? base_screen() : CZ_S_HATCH);
}

void cz_set_creature(int creature) {
    if (creature < 0 || creature >= CZ_CREATURES) return;
    S.creature = creature;
    copy(S.name, sizeof S.name, DEFAULT_NAME[creature]); /* host calls cz_set_name afterwards for a custom name */
    S.stale = 1;
}
void cz_set_name(const char *name) { copy(S.name, sizeof S.name, name); S.stale = 1; }
void cz_set_prompt(const char *prompt) { copy(S.prompt, sizeof S.prompt, prompt); S.stale = 1; }
int cz_brightness(void) { return S.brightness; }
void cz_set_brightness(int pct) { if (pct == 100 || pct == 40 || pct == 10) S.brightness = pct; }
void cz_set_qr_url(const char *url) { copy(S.url, sizeof S.url, url); S.stale = 1; }

void cz_message(const char *text) {
    if (S.screen == CZ_S_HATCH) return;
    copy(S.msg, sizeof S.msg, text);
    go(CZ_S_TALK);
    S.stale = 1;
}

void cz_checkin(const char *city) {
    if (S.screen == CZ_S_HATCH) return;
    copy(S.city, sizeof S.city, city);
    go(CZ_S_CHECKIN);
    S.stale = 1;
}

int cz_pose(void) {
    switch (S.screen) {
    case CZ_S_FEED: return CZ_EAT;
    case CZ_S_PLAY: return CZ_PLAY;
    case CZ_S_TALK: return CZ_TALK;
    case CZ_S_CHECKIN: return CZ_CHEER;
    case CZ_S_SULK: return CZ_SULK;
    case CZ_S_SLEEP: return CZ_SLEEP;
    case CZ_S_HOME: return S.screen_ms % BLINK_EVERY_MS >= BLINK_EVERY_MS - BLINK_MS ? CZ_BLINK : CZ_IDLE;
    default: return CZ_IDLE;
    }
}

/* Everything the picture depends on that changes with time; setters mark S.stale instead. */
static uint32_t view_key(void) {
    return (uint32_t)S.screen | (uint32_t)S.hearts << 4 | (uint32_t)cz_pose() << 8 |
           (uint32_t)(S.screen == CZ_S_HATCH ? typed_len() : 0) << 12 |
           (uint32_t)(S.screen == CZ_S_HATCH ? hatch_y() : 0) << 20;
}

/* Home blink: only the pose changed, so redraw just the box where the idle and blink sprites differ. */
static void clip_blink(void) {
    const uint8_t *a = cz_sprite(S.creature, CZ_IDLE), *b = cz_sprite(S.creature, CZ_BLINK);
    int x, y, x0 = CZ_SPRITE, y0 = CZ_SPRITE, x1 = -1, y1 = -1;
    for (y = 0; y < CZ_SPRITE; y++)
        for (x = 0; x < CZ_SPRITE; x++)
            if (a[y * CZ_SPRITE + x] != b[y * CZ_SPRITE + x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    /* sprite drawn at (56, 44), scale 2 by home() in screens.c; an identical pair clips to nothing */
    cz_clip(56 + 2 * x0, 44 + 2 * y0, 2 * (x1 - x0 + 1), 2 * (y1 - y0 + 1));
}

static void render_if_needed(void) {
    struct cz_view v;
    uint32_t k = view_key();
    if (!S.stale && k == S.drawn_key) return;
    v.screen = S.screen; v.creature = S.creature; v.pose = cz_pose(); v.hearts = S.hearts; v.typed = typed_len();
    v.name = S.name; v.prompt = S.prompt; v.msg = S.msg; v.city = S.city; v.url = S.url;
    if (!S.stale && S.screen == CZ_S_HOME && (k ^ S.drawn_key) == (uint32_t)(CZ_IDLE ^ CZ_BLINK) << 8) clip_blink();
    cz_hatch_y = hatch_y();
    if (!S.stale && S.screen == CZ_S_HATCH && ((k ^ S.drawn_key) & 0xFFFFF) == 0) {
        /* only the rise moved: redraw the sprite box from the higher top down to the band (which hides the rest) */
        int y0 = (int)(S.drawn_key >> 20) < cz_hatch_y ? (int)(S.drawn_key >> 20) : cz_hatch_y;
        cz_clip(56, y0, 2 * CZ_SPRITE, HATCH_Y_HIDDEN - y0);
    }
    cz_draw(&v);
    cz_clip(0, 0, CZ_W, CZ_H);
    cz_stats[CZ_STAT_FRAMES]++;
    S.drawn_key = k;
    S.stale = 0;
    S.dirty = 1;
}

const uint8_t *cz_framebuffer(void) { render_if_needed(); return cz_fb; }
const uint16_t *cz_palette(void) { return cz_pal565; }
int cz_dirty_rows(int *y0, int *y1) {
    int d;
    render_if_needed();
    d = S.dirty;
    if (d) { *y0 = cz_dy0; *y1 = cz_dy1; }
    S.dirty = 0;
    cz_dy0 = CZ_H; cz_dy1 = -1;
    return d;
}
int cz_dirty(void) { int y0, y1; return cz_dirty_rows(&y0, &y1); }
uint32_t cz_stat(int which) { return which >= 0 && which < CZ_STAT_COUNT ? cz_stats[which] : 0; }
int cz_wants_sleep(void) { return S.screen == CZ_S_SLEEP; }

int cz_screen(void) { return S.screen; }
int cz_hearts(void) { return S.hearts; }
int cz_creature(void) { return S.creature; }
uint32_t cz_hunger_ms(void) { return S.hunger_ms; }
uint8_t *cz_scratch(void) { return scratch; }
