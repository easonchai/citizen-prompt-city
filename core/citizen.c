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
#define SAVE_VERSION 5 /* 3 adds clock and timezone, 4 adds sky and screen, 5 owned creatures; versions 2-4 still restore */
#define DAY_MS 86400000UL
#define SETTINGS_IDLE_MS 20000UL
#define EGG_REVEAL_MS 2800
#define EGG_MS 4000
#define FORGET_ARM_MS 5000
enum { SET_BRIGHT, SET_SCREEN, SET_CLOCK, SET_TZ, SET_CITIZEN, SET_SKY, SET_WIFI, SET_SYNC, SET_UPDATE, SET_ABOUT, SET_EXIT, SET_N };

static const char *const DEFAULT_NAME[CZ_CREATURES] = {"TENUK", "KENYALANG", "HARIMAU"};
static const char DEFAULT_PROMPT[] = "YOU ARE A SKEPTICAL OPS LEAD WHO EATS CLEAN DATA";

static struct {
    int creature, hearts, hatched, lost; /* lost: hearts lost since the last feed */
    int screen;
    uint32_t hunger_ms, play_age_ms, screen_ms, idle_ms, anim_ms; /* anim_ms: free-running clock for idle animation */
    uint32_t last_now;
    int have_now;
    /* gesture */
    int down, consumed, x0, y0, maxd, tapped; /* tapped: last_tap is a tap that can start a double tap */
    int undo_ok, undo_hearts, undo_lost; uint32_t undo_hunger; /* the feed a first tap did, undone if a double tap follows */
    uint32_t t0, last_tap;
    int brightness;
    int always_on;   /* Settings SCREEN: never idle into Sleep */
    int sky; unsigned skies; /* chosen sky and the unlocked mask (bit per enum cz_sky) */
    unsigned creatures;      /* owned creatures, bit per enum cz_creature; always holds S.creature */
    int egg, egg_wait;       /* the creature hatching on Egg; egg_wait: queued until Hatch or Portal ends */
    int syncing;             /* Settings SYNC was tapped; cleared by cz_set_wifi or leaving Settings */
    char nets[CZ_WIFI_NETS][33]; int nets_n, wsel, armed, forget; uint32_t armed_ms; /* WiFi sub-screen; armed: row + 1 */
    char ap[24];             /* Portal access point name */
    char ap_pass[16];        /* and its WPA2 password */
    int tap_home; /* the last tap landed on Home or Sulk, so a quick second tap opens Settings */
    int tap_set;  /* the last tap landed on Settings, so a quick second tap leaves it */
    int portrait; /* the web page's portrait of the pet: not a device, so no hearts, touch or sleep */
    int pending, pending_dir; uint32_t pending_ms; /* a Settings value tap waits DOUBLE_TAP_MS so a double tap can cancel it */
    /* clock and settings */
    int clock_known, tz, sel, wifi_state;
    uint32_t day_ms, reqs; /* reqs: bitmask of pending enum cz_request */
    char version[16], update[16], ssid[33], set_value[48];
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
    if (screen != CZ_S_SETTINGS) S.syncing = 0;
    S.screen = screen;
    S.screen_ms = 0;
    S.idle_ms = 0;
}

static void age(uint32_t dt) {
    if (dt > AGE_CAP_MS) dt = AGE_CAP_MS;
    S.hunger_ms = S.hunger_ms + dt > AGE_CAP_MS ? AGE_CAP_MS : S.hunger_ms + dt;
    S.play_age_ms = S.play_age_ms + dt > AGE_CAP_MS ? AGE_CAP_MS : S.play_age_ms + dt;
    while (S.hearts > 0 && S.lost < 4 && S.hunger_ms >= (uint32_t)(S.lost + 1) * HEART_LOSS_MS) { S.hearts--; S.lost++; }
    S.anim_ms += dt;
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
    case CZ_S_EGG: return EGG_MS;
    default: return 0;
    }
}

static void clock_add(uint32_t ms) {
    uint32_t m = S.day_ms / MIN_MS;
    if (!S.clock_known) return;
    S.day_ms = (S.day_ms + ms % DAY_MS) % DAY_MS;
    if (S.screen == CZ_S_SETTINGS && S.day_ms / MIN_MS != m) S.stale = 1; /* the Clock row shows minutes */
}

static void setting_change(int dir);

static void egg_reveal(void) { if (S.creature != S.egg) cz_set_creature(S.egg); }
/* back from the WiFi list to Settings, on its WIFI row */
static void wifi_back(void) { go(CZ_S_SETTINGS); S.sel = SET_WIFI; S.armed = 0; S.stale = 1; }

static void advance(uint32_t now) {
    uint32_t dt, limit;
    if (S.undo_ok && S.screen != CZ_S_FEED) S.undo_ok = 0;
    if (!S.have_now) { S.have_now = 1; S.last_now = now; return; }
    dt = now - S.last_now; /* wraps correctly */
    S.last_now = now;
    age(dt);
    clock_add(dt);
    S.screen_ms += dt;
    S.idle_ms += dt;
    if (S.screen == CZ_S_EGG && S.screen_ms >= EGG_REVEAL_MS) egg_reveal();
    limit = event_ms(S.screen);
    if (limit && S.screen_ms >= limit) {
        if (S.screen == CZ_S_HATCH) S.hatched = 1;
        go(S.egg_wait ? CZ_S_EGG : base_screen());
        S.egg_wait = 0;
    }
    if (S.screen == CZ_S_HOME && sulking()) S.screen = CZ_S_SULK;
    if (S.screen == CZ_S_SULK && !sulking()) S.screen = CZ_S_HOME;
    if ((S.screen == CZ_S_HOME || S.screen == CZ_S_SULK) && S.idle_ms >= IDLE_SLEEP_MS && !S.portrait && !S.always_on) go(CZ_S_SLEEP);
    if (S.screen == CZ_S_SETTINGS && S.idle_ms >= SETTINGS_IDLE_MS) go(base_screen());
    if (S.screen == CZ_S_WIFI && S.idle_ms >= SETTINGS_IDLE_MS) wifi_back();
    if (S.armed && now - S.armed_ms >= FORGET_ARM_MS) { S.armed = 0; S.stale = 1; }
    if (S.pending && (S.screen != CZ_S_SETTINGS || now - S.pending_ms >= DOUBLE_TAP_MS)) {
        S.pending = 0;
        if (S.screen == CZ_S_SETTINGS) setting_change(S.pending_dir);
    }
    /* hold fires while the finger is still down */
    if (S.down && !S.consumed && S.maxd < TAP_PX && now - S.t0 >= HOLD_MS) {
        S.consumed = 1;
        if (S.screen == CZ_S_HOME || S.screen == CZ_S_SULK) go(CZ_S_QR);
        else if (S.screen == CZ_S_SETTINGS) go(base_screen());
        else if (S.screen == CZ_S_WIFI) wifi_back();
    }
}

static void feed(void) {
    S.undo_hearts = S.hearts; S.undo_hunger = S.hunger_ms; S.undo_lost = S.lost; S.undo_ok = 1;
    if (S.hearts < 4) S.hearts++;
    S.hunger_ms = 0;
    S.lost = 0;
    go(CZ_S_FEED);
}

static void play(void) {
    if (S.play_age_ms >= PLAY_GAP_MS) {
        if (S.hearts < 4) S.hearts++;
        S.play_age_ms = 0;
        /* a heart earned by play lives a full window: rebase hunger to the start of the current one */
        if (S.lost > 0) { S.lost--; if (S.hunger_ms > (uint32_t)S.lost * HEART_LOSS_MS) S.hunger_ms = (uint32_t)S.lost * HEART_LOSS_MS; }
    }
    go(CZ_S_PLAY);
}

static void setting_change(int dir);

/* Settings is a vertical wheel: the row above at y 48, the selected row at 84-108, the row below at 140.
 * Tap the top third to move up, the bottom third to move down, the middle row's left side to change back
 * and its right side (or centre) to change forward or activate. */
#define SET_ROW_TOP 70
#define SET_ROW_BOTTOM 125
#define SET_LEFT_X 80
static void settings_tap(int x, int y, uint32_t now) {
    if (y < SET_ROW_TOP) S.sel = (S.sel + SET_N - 1) % SET_N;
    else if (y > SET_ROW_BOTTOM) S.sel = (S.sel + 1) % SET_N;
    else { S.pending = 1; S.pending_dir = x < SET_LEFT_X ? -1 : 1; S.pending_ms = now; }
    S.stale = 1;
}

/* WiFi list: the same wheel; a centre/right tap on a network arms forget, a second one (not a double tap) confirms */
static void wifi_tap(int x, int y, uint32_t now) {
    int rows = S.nets_n + 2;
    S.stale = 1;
    if (y < SET_ROW_TOP) { S.wsel = (S.wsel + rows - 1) % rows; S.armed = 0; return; }
    if (y > SET_ROW_BOTTOM) { S.wsel = (S.wsel + 1) % rows; S.armed = 0; return; }
    if (x < SET_LEFT_X) { S.armed = 0; return; }
    if (S.wsel == S.nets_n) S.reqs |= 1u << CZ_REQ_PORTAL;
    else if (S.wsel > S.nets_n) wifi_back();
    else if (S.armed != S.wsel + 1) { S.armed = S.wsel + 1; S.armed_ms = now; }
    else if (now - S.armed_ms >= DOUBLE_TAP_MS) { S.forget = S.wsel; S.reqs |= 1u << CZ_REQ_WIFI_FORGET; S.armed = 0; }
}

static void gesture_tap(int x, int y, uint32_t now) {
    int home = S.screen == CZ_S_HOME || S.screen == CZ_S_SULK;
    int again = S.tapped && now - S.last_tap < DOUBLE_TAP_MS;
    if (again && S.tap_home) { /* second tap: open Settings, no tap action */
        S.tapped = 0;
        if (S.undo_ok && S.screen == CZ_S_FEED) { /* the first tap fed the pet: take that back, Settings is free */
            S.hearts = S.undo_hearts; S.hunger_ms = S.undo_hunger; S.lost = S.undo_lost;
        }
        S.undo_ok = 0;
        S.sel = SET_BRIGHT;
        go(CZ_S_SETTINGS);
        return;
    }
    if (again && S.tap_set && S.screen == CZ_S_SETTINGS) { /* double tap leaves Settings, the first tap's value change is dropped */
        S.tapped = 0; S.pending = 0;
        go(base_screen());
        return;
    }
    S.tapped = 1;
    S.tap_home = home;
    S.tap_set = S.screen == CZ_S_SETTINGS;
    S.last_tap = now;
    if (home) feed();
    else if (S.screen == CZ_S_SETTINGS) settings_tap(x, y, now);
    else if (S.screen == CZ_S_WIFI) wifi_tap(x, y, now);
    else if (S.screen == CZ_S_EGG) { if (S.screen_ms < EGG_REVEAL_MS) { S.screen_ms = EGG_REVEAL_MS; egg_reveal(); } else go(base_screen()); }
    else if (S.screen != CZ_S_HATCH && S.screen != CZ_S_PORTAL) go(base_screen()); /* Sleep, QR and every event screen */
}

/* Settings swipe: dir +1 (right) changes forward or activates, -1 (left) changes back. */
static void setting_change(int dir) {
    static const int bright[3] = {10, 40, 100}; /* right = brighter, stops at the ends */
    int i, m;
    switch (S.sel) {
    case SET_BRIGHT:
        for (i = 0; i < 2 && bright[i] != S.brightness; i++) {}
        if (dir > 0 && i < 2) i++; else if (dir < 0 && i > 0) i--;
        S.brightness = bright[i];
        break;
    case SET_CLOCK:
        m = S.clock_known ? (int)(S.day_ms / MIN_MS) : 0;
        m = dir > 0 ? (m / 15 + 1) * 15 : m % 15 ? m / 15 * 15 : m - 15;
        S.day_ms = (uint32_t)((m + 1440) % 1440) * MIN_MS;
        S.clock_known = 1;
        break;
    case SET_TZ: /* the clock is local time, so it moves with the zone */
        if (S.tz + dir < -12 || S.tz + dir > 14) return;
        S.tz += dir;
        if (S.clock_known) S.day_ms = (S.day_ms + (dir > 0 ? 60 : 1380) * MIN_MS) % DAY_MS;
        break;
    case SET_SCREEN: S.always_on = dir > 0; break;
    case SET_CITIZEN: /* step to the next owned creature, wrapping; with one owned it stays (and keeps its name) */
        m = S.creature;
        for (i = 0; i < CZ_CREATURES; i++) {
            m = (m + (dir > 0 ? 1 : CZ_CREATURES - 1)) % CZ_CREATURES;
            if (S.creatures >> m & 1) break;
        }
        if (m != S.creature) cz_set_creature(m);
        break;
    case SET_SKY: /* step to the next unlocked sky, wrapping */
        m = cz_sky();
        for (i = 0; i < CZ_SKIES; i++) {
            m = (m + (dir > 0 ? 1 : CZ_SKIES - 1)) % CZ_SKIES;
            if (S.skies >> m & 1) break;
        }
        S.sky = m;
        break;
    case SET_WIFI: if (dir > 0) { go(CZ_S_WIFI); S.wsel = 0; S.armed = 0; } break;
    case SET_SYNC: if (dir > 0) { S.reqs |= 1u << CZ_REQ_SYNC; S.syncing = 1; } break;
    case SET_UPDATE: if (dir > 0) S.reqs |= 1u << (S.update[0] ? CZ_REQ_INSTALL_UPDATE : CZ_REQ_CHECK_UPDATE); break;
    case SET_EXIT: if (dir > 0) { go(base_screen()); return; } break;
    default: break;
    }
    S.stale = 1;
}

static void gesture_swipe(int dir) {
    if (S.screen == CZ_S_HOME || S.screen == CZ_S_SULK) play();
    else if (S.screen == CZ_S_SETTINGS) setting_change(dir);
}

/* vertical swipe: on Settings the wheel follows the finger (up = next row), elsewhere it plays like any swipe */
static void gesture_vswipe(int down) {
    if (S.screen == CZ_S_HOME || S.screen == CZ_S_SULK) play();
    else if (S.screen == CZ_S_SETTINGS) { S.sel = (S.sel + (down ? SET_N - 1 : 1)) % SET_N; S.stale = 1; }
    else if (S.screen == CZ_S_WIFI) { S.wsel = (S.wsel + (down ? S.nets_n + 1 : 1)) % (S.nets_n + 2); S.armed = 0; S.stale = 1; }
}

void cz_touch(int kind, int x, int y, uint32_t now_ms) {
    int dx = x - S.x0, dy = y - S.y0, d, dir = dx > 0 ? 1 : -1, down = dy > 0;
    advance(now_ms);
    if (S.portrait) return;
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
    if (!S.consumed && now_ms - S.t0 <= SWIPE_MS) {
        if (dx >= SWIPE_PX && dx >= dy) { S.consumed = 1; gesture_swipe(dir); }
        else if (dy >= SWIPE_PX) { S.consumed = 1; gesture_vswipe(down); }
    }
    if (kind == CZ_TOUCH_UP) {
        if (!S.consumed && now_ms - S.t0 < TAP_MS && S.maxd < TAP_PX) gesture_tap(S.x0, S.y0, now_ms);
        S.down = 0;
    }
}

void cz_tick(uint32_t now_ms) { advance(now_ms); }

/* ---- save blob: magic(2) version creature hearts hatched lost brightness hunger_s(4) play_s(4) name(11)
 * tz+12(1) day_s(4, 0xFFFFFFFF = clock unknown) sky(1) always_on(1) skies(1) creatures(1, byte 35) ... crc(4) at the end */
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
    out[27] = (uint8_t)(S.tz + 12);
    put32(out + 28, S.clock_known ? S.day_ms / 1000 : 0xFFFFFFFFUL);
    out[32] = (uint8_t)S.sky; out[33] = (uint8_t)S.always_on; out[34] = (uint8_t)S.skies;
    out[35] = (uint8_t)(S.creatures | 1u << S.creature); /* during an egg the new mask may not hold the creature still shown; restore() needs it */
    put32(out + CZ_SAVE_SIZE - 4, checksum(out, CZ_SAVE_SIZE - 4));
    return CZ_SAVE_SIZE;
}

static int restore(const uint8_t *b, int len) {
    if (!b || len < CZ_SAVE_SIZE || b[0] != SAVE_MAGIC0 || b[1] != SAVE_MAGIC1 || b[2] < 2 || b[2] > SAVE_VERSION) return 0;
    if (get32(b + CZ_SAVE_SIZE - 4) != checksum(b, CZ_SAVE_SIZE - 4)) return 0;
    if (b[3] >= CZ_CREATURES || b[4] > 4 || b[6] > 4 || (b[7] != 100 && b[7] != 40 && b[7] != 10)) return 0;
    if (b[2] >= 3 && (b[27] > 26 || (get32(b + 28) >= 86400 && get32(b + 28) != 0xFFFFFFFFUL))) return 0;
    if (b[2] >= 4 && (b[32] >= CZ_SKIES || b[33] > 1 || b[34] >= 1u << CZ_SKIES)) return 0;
    if (b[2] >= 5 && (b[35] >= 1u << CZ_CREATURES || !(b[35] >> b[3] & 1))) return 0;
    if (b[2] >= 4) { S.sky = b[32]; S.always_on = b[33]; S.skies = b[34] | 1u << CZ_SKY_KL; }
    if (b[2] >= 3) {
        S.tz = b[27] - 12;
        S.clock_known = get32(b + 28) != 0xFFFFFFFFUL;
        S.day_ms = S.clock_known ? get32(b + 28) * 1000 : 0;
    }
    S.creature = b[3]; S.creatures = b[2] >= 5 ? b[35] : 1u << b[3]; S.hearts = b[4]; S.hatched = b[5] != 0; S.lost = b[6]; S.brightness = b[7];
    S.hunger_ms = get32(b + 8) > AGE_CAP_MS / 1000 ? AGE_CAP_MS : get32(b + 8) * 1000;
    S.play_age_ms = get32(b + 12) > AGE_CAP_MS / 1000 ? AGE_CAP_MS : get32(b + 12) * 1000;
    copy(S.name, NAME_MAX + 1, (const char *)b + 16); /* bytes 16..26 always end in 0 */
    return 1;
}

/* the host always sets the real URL; this only shows in tests and the simulator */
static void default_url(void) { copy(S.url, sizeof S.url, "https://connect.prmpt.city/setup"); }

void cz_init(uint32_t seed, const uint8_t *saved, int saved_len, uint32_t slept_seconds) {
    uint8_t *p = (uint8_t *)&S;
    unsigned i;
    for (i = 0; i < sizeof S; i++) p[i] = 0;
    copy(S.prompt, sizeof S.prompt, DEFAULT_PROMPT);
    copy(S.city, sizeof S.city, "KL");
    S.stale = 1;
    S.brightness = 100;
    S.skies = 1u << CZ_SKY_KL;
    S.tz = 8;
    S.forget = -1;
    for (i = 0; i < CZ_STAT_COUNT; i++) cz_stats[i] = 0;
    if (restore(saved, saved_len)) {
        clock_add((slept_seconds % 86400) * 1000); /* before the 7-day cap so the time of day stays exact */
        if (slept_seconds > AGE_CAP_MS / 1000) slept_seconds = AGE_CAP_MS / 1000;
        age(slept_seconds * 1000);
    } else {
        S.creature = (int)(seed % CZ_CREATURES);
        S.creatures = 1u << S.creature;
        S.hearts = 3;
        S.play_age_ms = PLAY_GAP_MS;
        copy(S.name, sizeof S.name, DEFAULT_NAME[S.creature]);
    }
    default_url();
    go(S.hatched ? base_screen() : CZ_S_HATCH);
}

void cz_set_portrait(int on) {
    S.portrait = on;
    if (on) { S.hatched = 1; S.hearts = 4; go(CZ_S_HOME); }
    S.stale = 1;
}
void cz_set_creature(int creature) {
    if (creature < 0 || creature >= CZ_CREATURES) return;
    S.creature = creature;
    /* the shown creature is always owned, so the save blob stays valid; before Hatch ends the host is picking it */
    S.creatures = (S.hatched ? S.creatures : 0) | 1u << creature;
    copy(S.name, sizeof S.name, DEFAULT_NAME[creature]); /* host calls cz_set_name afterwards for a custom name */
    S.stale = 1;
}
void cz_set_name(const char *name) { copy(S.name, sizeof S.name, name); S.stale = 1; }
void cz_set_prompt(const char *prompt) { copy(S.prompt, sizeof S.prompt, prompt); S.stale = 1; }
int cz_always_on(void) { return S.always_on; }
void cz_set_always_on(int on) { S.always_on = on != 0; S.idle_ms = 0; S.stale = 1; }
void cz_set_skies(unsigned mask) {
    unsigned gained, i;
    mask = (mask | 1u << CZ_SKY_KL) & ((1u << CZ_SKIES) - 1);
    gained = mask & ~S.skies;
    S.skies = mask;
    for (i = 0; i < CZ_SKIES; i++) if (gained >> i & 1) S.sky = (int)i; /* a fresh unlock is the gift: show it */
    S.stale = 1;
}
int cz_sky(void) { return S.skies >> S.sky & 1 ? S.sky : CZ_SKY_KL; }

void cz_set_creatures(unsigned mask) {
    unsigned gained;
    int i;
    mask &= (1u << CZ_CREATURES) - 1;
    if (!mask) return;
    gained = mask & ~S.creatures;
    S.creatures = mask;
    if (gained) {
        for (i = 0; i < CZ_CREATURES; i++) if (gained >> i & 1) S.egg = i; /* the highest gained hatches */
        if (S.screen == CZ_S_HATCH || S.screen == CZ_S_PORTAL) S.egg_wait = 1; else go(CZ_S_EGG);
    } else if (!(mask >> S.creature & 1)) {
        for (i = 0; !(mask >> i & 1); i++) {}
        cz_set_creature(i);
    }
    S.stale = 1;
}
unsigned cz_creatures(void) { return S.creatures; }

void cz_set_wifi_list(const char *ssids) {
    int n = 0, k;
    while (ssids && *ssids && n < CZ_WIFI_NETS) {
        for (k = 0; *ssids && *ssids != '\n'; ssids++) if (k < 32) S.nets[n][k++] = *ssids;
        S.nets[n][k] = 0;
        if (k) n++;
        if (*ssids) ssids++;
    }
    S.nets_n = n;
    if (S.wsel > n + 1) S.wsel = n + 1;
    S.armed = 0;
    if (S.screen == CZ_S_WIFI) S.stale = 1;
}
int cz_wifi_forget(void) { int i = S.forget; S.forget = -1; return i; }

void cz_portal(const char *ap, const char *pass) {
    if (ap && *ap) { copy(S.ap, sizeof S.ap, ap); copy(S.ap_pass, sizeof S.ap_pass, pass ? pass : ""); go(CZ_S_PORTAL); }
    else if (S.screen == CZ_S_PORTAL) { go(S.egg_wait ? CZ_S_EGG : base_screen()); S.egg_wait = 0; }
    S.stale = 1;
}
int cz_brightness(void) { return S.brightness; }
void cz_set_brightness(int pct) { if (pct == 100 || pct == 40 || pct == 10) S.brightness = pct; }
void cz_set_qr_url(const char *url) { copy(S.url, sizeof S.url, url); S.stale = 1; }

void cz_set_clock(int minute_of_day) {
    if (minute_of_day >= 1440) return;
    S.clock_known = minute_of_day >= 0;
    S.day_ms = S.clock_known ? (uint32_t)minute_of_day * MIN_MS : 0;
    if (S.screen == CZ_S_SETTINGS) S.stale = 1;
}
int cz_clock(void) { return S.clock_known ? (int)(S.day_ms / MIN_MS) : -1; }
int cz_timezone(void) { return S.tz; }
void cz_set_timezone(int hours) { if (hours >= -12 && hours <= 14) { S.tz = hours; if (S.screen == CZ_S_SETTINGS) S.stale = 1; } }

/* Host-fed Settings values: only a visible Settings screen needs a redraw, so idle frames stay small. */
void cz_set_version(const char *running) { copy(S.version, sizeof S.version, running); if (S.screen == CZ_S_SETTINGS) S.stale = 1; }
void cz_set_update(const char *available) { copy(S.update, sizeof S.update, available); if (S.screen == CZ_S_SETTINGS) S.stale = 1; }
void cz_set_wifi(const char *ssid, int state) {
    copy(S.ssid, sizeof S.ssid, ssid);
    S.wifi_state = state >= CZ_WIFI_OFF && state <= CZ_WIFI_FAIL ? state : CZ_WIFI_OFF;
    S.syncing = 0;
    if (S.screen == CZ_S_WIFI) S.stale = 1;
    if (S.screen == CZ_S_SETTINGS) S.stale = 1;
}
int cz_host_request(void) {
    int r;
    for (r = CZ_REQ_PORTAL; r <= CZ_REQ_WIFI_FORGET; r++)
        if (S.reqs & 1u << r) { S.reqs &= ~(1u << r); return r; }
    return CZ_REQ_NONE;
}

/* Hatch, Egg and Portal own the screen until they end: content that arrives meanwhile is dropped */
static int busy(void) { return S.screen == CZ_S_HATCH || S.screen == CZ_S_EGG || S.screen == CZ_S_PORTAL; }

void cz_message(const char *text) {
    if (busy()) return;
    copy(S.msg, sizeof S.msg, text);
    go(CZ_S_TALK);
    S.stale = 1;
}

void cz_checkin(const char *city) {
    if (busy()) return;
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

/* ---- Settings text */
static const char *const SET_LABEL[SET_N] = {"BRIGHTNESS", "SCREEN", "CLOCK", "TIMEZONE", "CITIZEN", "SKY", "WIFI", "SYNC", "UPDATE", "ABOUT", "EXIT"};

static char *put(char *d, const char *s, int max) { while (*s && max--) *d++ = *s++; *d = 0; return d; }
static char *num(char *d, int v, int width) { /* decimal, zero padded to width */
    char t[8];
    int n = 0;
    if (v < 0) { *d++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v || n < width);
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}

static const char *const SKY_NAME[CZ_SKIES] = {"KUALA LUMPUR", "SINGAPORE", "TOKYO"};

static const char *setting_value(void) {
    static const char *const wifi[4] = {"OFF", "CONNECTING", "OK", "FAIL"};
    char *d = S.set_value;
    const char *slug = S.url;
    int i, m = cz_clock();
    *d = 0;
    switch (S.sel) {
    case SET_BRIGHT: num(d, S.brightness, 1); break;
    case SET_CLOCK: if (m < 0) put(d, "--:--", 5); else { d = num(d, m / 60, 2); d = put(d, ":", 1); num(d, m % 60, 2); } break;
    case SET_TZ: d = put(d, S.tz < 0 ? "UTC" : "UTC+", 4); num(d, S.tz, 1); break;
    case SET_SCREEN: put(d, S.always_on ? "ALWAYS ON" : "AUTO OFF", 9); break;
    case SET_CITIZEN: put(d, DEFAULT_NAME[S.creature], 10); break;
    case SET_SKY: put(d, SKY_NAME[cz_sky()], 12); break;
    case SET_WIFI: if (!S.ssid[0]) put(d, "NOT SET", 7); else { d = put(d, S.ssid, 12); d = put(d, " ", 1); put(d, wifi[S.wifi_state], 10); } break;
    case SET_SYNC: put(d, S.syncing ? "SYNCING" : "TAP TO SYNC", 11); break;
    case SET_UPDATE:
        if (S.version[0]) { d = put(d, "V", 1); d = put(d, S.version, 15); d = put(d, " ", 1); }
        if (S.update[0]) { d = put(d, "NEW ", 4); put(d, S.update, 15); } else put(d, "UP TO DATE", 10);
        break;
    case SET_ABOUT:
        for (i = 0; S.url[i] && S.url[i] != '?'; i++) if (S.url[i] == '/') slug = S.url + i + 1;
        if (S.version[0]) { d = put(d, "V", 1); d = put(d, S.version, 15); d = put(d, " ", 1); }
        for (i = 0; slug[i] && slug[i] != '?' && i < 16; i++) {} /* the slug without the QR's "?ref=qr" */
        put(d, slug, i);
        break;
    default: break;
    }
    return S.set_value;
}

/* WiFi list rows: saved SSIDs (cut to 12), ADD NETWORK, BACK */
static const char *wifi_row(int i) {
    static char cut[3][13];
    static int k;
    int rows = S.nets_n + 2;
    i = (i + rows) % rows;
    if (i == S.nets_n) return "ADD NETWORK";
    if (i > S.nets_n) return "BACK";
    k = (k + 1) % 3;
    copy(cut[k], sizeof cut[k], S.nets[i]);
    return cut[k];
}
static const char *wifi_value(void) {
    int i;
    if (S.wsel == S.nets_n) return "PHONE OR USB";
    if (S.wsel > S.nets_n) return "";
    if (S.armed == S.wsel + 1) return "TAP TO FORGET";
    for (i = 0; S.nets[S.wsel][i] == S.ssid[i] && S.ssid[i]; i++) {}
    return S.wifi_state == CZ_WIFI_OK && !S.nets[S.wsel][i] && !S.ssid[i] ? "CONNECTED" : "SAVED";
}

/* Everything the picture depends on that changes with time; setters mark S.stale instead. */
static int phase(void) {
    int m = cz_clock();
    if (m < 0 || m >= 1200 || m < 360) return CZ_PH_NIGHT; /* unknown time renders night */
    return m < 480 ? CZ_PH_DAWN : m < 1050 ? CZ_PH_DAY : CZ_PH_DUSK;
}

/* Bits: screen 0-3, hearts 4-7, pose 8-11, Hatch typed 12-19 and rise 20-27 or else the animation state 12-27,
 * phase 30-31. */
#define KEY_ANIM 0x0FFFF000u
#define KEY_POSE 0x00000F00u
#define KEY_RISE 0x0FF00000u
static int anim(void) { return cz_anim(S.screen, S.anim_ms, S.screen_ms); }
static uint32_t view_key(void) {
    uint32_t k = (uint32_t)phase() << 30 | (uint32_t)S.screen | (uint32_t)S.hearts << 4 | (uint32_t)cz_pose() << 8;
    if (S.screen == CZ_S_HATCH) return k | (uint32_t)typed_len() << 12 | (uint32_t)hatch_y() << 20;
    return k | ((uint32_t)anim() << 12 & KEY_ANIM);
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
    /* sprite drawn at (56, 44 + breath 0..2), scale 2 by home() in screens.c; an identical pair clips to nothing */
    cz_clip(56 + 2 * x0, 44 + 2 * y0, 2 * (x1 - x0 + 1), 2 * (y1 - y0 + 1) + 2);
}

static void render_if_needed(void) {
    struct cz_view v;
    uint32_t k = view_key(), diff = k ^ S.drawn_key;
    int bx, by, bw, bh;
    if (!S.stale && !diff) return;
    v.screen = S.screen; v.creature = S.creature; v.pose = cz_pose(); v.hearts = S.hearts; v.typed = typed_len();
    v.name = S.name; v.prompt = S.prompt; v.msg = S.msg; v.city = S.city; v.url = S.url; v.phase = phase(); v.sky = cz_sky();
    v.anim = S.screen == CZ_S_HATCH ? 0 : anim();
    v.portrait = S.portrait; v.ap = S.ap; v.ap_pass = S.ap_pass;
    if (S.screen == CZ_S_SETTINGS) {
        v.set_prev = SET_LABEL[(S.sel + SET_N - 1) % SET_N]; v.set_label = SET_LABEL[S.sel];
        v.set_next = SET_LABEL[(S.sel + 1) % SET_N]; v.set_value = setting_value();
        v.set_arrows = S.sel == SET_ABOUT ? 0 : S.sel >= SET_WIFI ? 2 : 3; /* actions only go one way */
    }
    if (S.screen == CZ_S_WIFI) {
        v.set_prev = wifi_row(S.wsel - 1); v.set_label = wifi_row(S.wsel); v.set_next = wifi_row(S.wsel + 1);
        v.set_value = wifi_value(); v.set_arrows = S.wsel > S.nets_n ? 0 : 2;
    }
    cz_hatch_y = hatch_y();
    cz_anim_box(S.screen, &bx, &by, &bw, &bh);
    if (S.stale) {}
    else if (S.screen == CZ_S_HATCH) {
        if ((diff & ~KEY_RISE) == 0) {
            /* only the rise moved: redraw the sprite box from the higher top down to the band (which hides the rest) */
            int old = (int)(S.drawn_key >> 20 & 0xFF), y0 = old < cz_hatch_y ? old : cz_hatch_y;
            cz_clip(56, y0, 2 * CZ_SPRITE, HATCH_Y_HIDDEN - y0);
        }
    }
    else if (S.screen == CZ_S_HOME && diff == (uint32_t)(CZ_IDLE ^ CZ_BLINK) << 8) clip_blink();
    else if (bw && (diff & ~(KEY_ANIM | KEY_POSE)) == 0) cz_clip(bx, by, bw, bh); /* only animation moved */
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
