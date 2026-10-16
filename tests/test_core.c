/* Native tests for the Citizen core. Run with `make test`. Writes every screen to tests/out/<screen>.ppm. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "citizen.h"
#include "gfx.h" /* cz_anim_box, cz_blit for the animation tests */

static uint32_t now;
static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void run(uint32_t ms) { /* tick at 50 ms */
    uint32_t end = now + ms;
    while (now < end) { now += 50; cz_tick(now); }
}

static void quick_tap(void) { cz_touch(CZ_TOUCH_DOWN, 120, 120, now); now += 100; cz_touch(CZ_TOUCH_UP, 121, 120, now); }
/* a single tap, then a pause so the next tap is not a double tap */
static void tap(void) { quick_tap(); now += 400; }

static void boot(void) {
    now = 1000;
    cz_init(0, NULL, 0, 0);
    cz_tick(now);
    run(6000); /* hatch types the default prompt then holds */
}

static void check_and_write(const char *name) {
    const uint8_t *fb = cz_framebuffer();
    const uint16_t *pal = cz_palette();
    char path[128];
    FILE *f;
    int i, bad = 0;
    for (i = 0; i < CZ_W * CZ_H; i++) if (fb[i] == 0 || fb[i] >= CZ_C_COUNT) bad++;
    if (bad) printf("FAIL %s: %d pixels outside 1..%d\n", name, bad, CZ_C_COUNT - 1);
    fails += bad != 0;
    snprintf(path, sizeof path, "tests/out/%s.ppm", name);
    f = fopen(path, "wb");
    if (!f) { printf("FAIL cannot write %s\n", path); fails++; return; }
    fprintf(f, "P6\n%d %d\n255\n", CZ_W, CZ_H);
    for (i = 0; i < CZ_W * CZ_H; i++) {
        uint16_t c = pal[fb[i] < CZ_C_COUNT ? fb[i] : 0];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)((c >> 11) * 255 / 31);
        rgb[1] = (unsigned char)(((c >> 5) & 63) * 255 / 63);
        rgb[2] = (unsigned char)((c & 31) * 255 / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("  wrote %s (screen %d, pose %d)\n", path, cz_screen(), cz_pose());
}

static void test_flow(void) {
    int h;
    now = 1000;
    cz_init(0, NULL, 0, 0);
    CHECK(cz_screen() == CZ_S_HATCH);
    CHECK(cz_creature() == CZ_TAPIR);
    cz_tick(now);
    run(6000);
    CHECK(cz_screen() == CZ_S_HOME);
    CHECK(cz_hearts() == 3);

    /* tap feeds */
    tap();
    CHECK(cz_screen() == CZ_S_FEED);
    CHECK(cz_hearts() == 4);
    CHECK(cz_hunger_ms() == 0);
    run(3100);
    CHECK(cz_screen() == CZ_S_HOME);

    /* swipe plays */
    cz_touch(CZ_TOUCH_DOWN, 80, 120, now);
    now += 100; cz_touch(CZ_TOUCH_MOVE, 110, 121, now);
    now += 100; cz_touch(CZ_TOUCH_MOVE, 140, 122, now);
    now += 50; cz_touch(CZ_TOUCH_UP, 140, 122, now);
    CHECK(cz_screen() == CZ_S_PLAY);
    run(3100);
    CHECK(cz_screen() == CZ_S_HOME);

    /* hold 1000 ms shows QR, tap returns Home */
    cz_touch(CZ_TOUCH_DOWN, 120, 120, now);
    run(950);
    CHECK(cz_screen() == CZ_S_HOME);
    run(100);
    CHECK(cz_screen() == CZ_S_QR);
    cz_touch(CZ_TOUCH_UP, 120, 120, now);
    CHECK(cz_screen() == CZ_S_QR);
    tap();
    CHECK(cz_screen() == CZ_S_HOME);

    /* 30 s idle sleeps */
    CHECK(!cz_wants_sleep());
    run(29000);
    CHECK(cz_screen() == CZ_S_HOME);
    run(1100);
    CHECK(cz_screen() == CZ_S_SLEEP);
    CHECK(cz_wants_sleep());
    tap();
    CHECK(cz_screen() == CZ_S_HOME);
    CHECK(!cz_wants_sleep());

    /* 90 minutes without food loses a heart */
    tap(); /* feed: hunger 0 */
    h = cz_hearts();
    run(89UL * 60000);
    CHECK(cz_hearts() == h);
    run(2UL * 60000);
    CHECK(cz_hearts() == h - 1);

    /* 6 h without food: sulk (it is asleep by now; a tap wakes it into Sulk) */
    run(6UL * 3600000 - 91UL * 60000 + 1000);
    CHECK(cz_hunger_ms() >= 6UL * 3600000);
    CHECK(cz_screen() == CZ_S_SLEEP);
    tap();
    CHECK(cz_screen() == CZ_S_SULK);
    tap(); /* feeding a sulking pet still works */
    CHECK(cz_screen() == CZ_S_FEED);
}

static void test_save(void) {
    uint8_t blob[CZ_SAVE_SIZE];
    boot();
    tap(); /* hunger 0, hearts 4 */
    run(60UL * 60000);
    CHECK(cz_hearts() == 4);
    CHECK(cz_save(blob, 8) == 0);
    CHECK(cz_save(blob, sizeof blob) == CZ_SAVE_SIZE);
    now = 5; /* host rebooted: monotonic clock restarts */
    cz_init(0, blob, sizeof blob, 3600);
    CHECK(cz_screen() == CZ_S_HOME);           /* hatched survives */
    CHECK(cz_hunger_ms() == 2UL * 3600000);    /* 60 min awake + 60 min asleep */
    CHECK(cz_hearts() == 3);                   /* crossed 90 min */
    cz_tick(now);
    run(31UL * 60000);
    CHECK(cz_hearts() == 3);                   /* next loss at 180 min, not before */
    run(30UL * 60000);
    CHECK(cz_hearts() == 2);

    blob[20] ^= 1; /* corrupt: checksum must reject it and hatch a new pet */
    cz_init(0, blob, sizeof blob, 0);
    CHECK(cz_screen() == CZ_S_HATCH);
}

static void test_screens(void) {
    const uint8_t *fb;
    now = 1000;
    cz_init(0, NULL, 0, 0);
    cz_tick(now);
    run(1500);
    check_and_write("hatch_typing");
    run(2000);
    check_and_write("hatch");
    run(2000);
    CHECK(cz_screen() == CZ_S_HOME);
    CHECK(cz_dirty() == 1);
    CHECK(cz_dirty() == 0);
    check_and_write("home");
    tap();
    CHECK(cz_dirty() == 1);
    check_and_write("eat");
    run(3100);
    cz_touch(CZ_TOUCH_DOWN, 60, 120, now); now += 200; cz_touch(CZ_TOUCH_UP, 160, 120, now);
    CHECK(cz_screen() == CZ_S_PLAY);
    check_and_write("play");
    cz_message("What did you ship");
    CHECK(cz_screen() == CZ_S_TALK);
    check_and_write("talk");
    run(8100);
    cz_touch(CZ_TOUCH_DOWN, 120, 120, now); run(1100); cz_touch(CZ_TOUCH_UP, 120, 120, now);
    CHECK(cz_screen() == CZ_S_QR);
    check_and_write("qr");
    tap();
    cz_checkin("KL");
    CHECK(cz_screen() == CZ_S_CHECKIN);
    check_and_write("cheer");
    run(4100);
    run(31000);
    CHECK(cz_screen() == CZ_S_SLEEP);
    check_and_write("sleep");
    run(7UL * 3600000);
    tap();
    CHECK(cz_screen() == CZ_S_SULK);
    check_and_write("sulk");

    cz_set_creature(CZ_HORNBILL);
    tap(); run(3100); tap(); run(3100); /* two feeds lift it out of sulk */
    CHECK(cz_screen() == CZ_S_HOME);
    check_and_write("home_kenyalang");
    cz_set_creature(CZ_TIGER);
    check_and_write("home_harimau");

    /* scratch buffer path used by the wasm host */
    {
        uint8_t *s = cz_scratch();
        const char *n = "BUDI";
        int i;
        for (i = 0; n[i]; i++) s[i] = (uint8_t)n[i];
        s[i] = 0;
        cz_set_name((const char *)s);
        CHECK(cz_dirty() == 1);
    }
    fb = cz_framebuffer();
    CHECK(fb != NULL);
}

static int count_color(const uint8_t *fb, int x0, int y0, int x1, int y1, int color, int match) {
    int x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) n += (fb[y * CZ_W + x] == color) == match;
    return n;
}

static void test_telemetry(void) {
    int y0, y1, i, blinks = 0;
    uint32_t f, p, sp, g;
    boot();
    CHECK(cz_screen() == CZ_S_HOME);
    cz_set_name("BUDI"); /* stale: forces a full frame */
    CHECK(cz_dirty_rows(&y0, &y1) == 1);
    printf("  full frame dirty rows %d..%d (%d rows)\n", y0, y1, y1 - y0 + 1);
    CHECK(y0 == 0 && y1 == CZ_H - 1);
    CHECK(cz_dirty_rows(&y0, &y1) == 0);

    f = cz_stat(CZ_STAT_FRAMES); p = cz_stat(CZ_STAT_PIXELS_FILLED);
    sp = cz_stat(CZ_STAT_SPRITE_PIXELS); g = cz_stat(CZ_STAT_TEXT_GLYPHS);
    CHECK(f > 0 && p > 0 && sp > 0 && g > 0);
    CHECK(cz_stat(CZ_STAT_COUNT) == 0);

    /* idle Home: the only redraws are blinks, each confined to the sprite box (rows 44..171) */
    for (i = 0; i < 200; i++) { /* 10 s at 50 ms: at least two blinks and their un-blinks */
        now += 50; cz_tick(now);
        if (cz_dirty_rows(&y0, &y1)) {
            if (!blinks) printf("  blink frame dirty rows %d..%d (%d rows, pose %d)\n", y0, y1, y1 - y0 + 1, cz_pose());
            blinks++;
            CHECK(y0 >= 44 && y1 <= 174 && y1 >= y0);
        }
    }
    CHECK(cz_screen() == CZ_S_HOME);
    CHECK(blinks >= 4);
    CHECK(cz_stat(CZ_STAT_FRAMES) >= f + 4);
    CHECK(cz_stat(CZ_STAT_PIXELS_FILLED) > p);
    CHECK(cz_stat(CZ_STAT_SPRITE_PIXELS) > sp);
    CHECK(cz_stat(CZ_STAT_TEXT_GLYPHS) >= g);
    { /* the partial blink frame equals a full render of the same state */
        static uint8_t part[CZ_W * CZ_H];
        const uint8_t *fb = cz_framebuffer();
        int same = 1;
        for (i = 0; i < CZ_W * CZ_H; i++) part[i] = fb[i];
        cz_set_name("BUDI");
        fb = cz_framebuffer();
        for (i = 0; i < CZ_W * CZ_H; i++) same &= part[i] == fb[i];
        CHECK(same);
    }
}

static void test_brightness(void) {
    uint8_t blob[CZ_SAVE_SIZE];
    boot();
    CHECK(cz_brightness() == 100);
    quick_tap(); quick_tap();            /* double tap: Settings; the first tap's feed is undone, so hearts stay at 3 */
    CHECK(cz_brightness() == 100);
    CHECK(cz_hearts() == 3);
    CHECK(cz_screen() == CZ_S_SETTINGS);
    cz_set_brightness(55);
    CHECK(cz_brightness() == 100);
    cz_set_brightness(10);
    CHECK(cz_save(blob, sizeof blob) == CZ_SAVE_SIZE);
    CHECK(blob[2] == 3);
    cz_init(0, blob, sizeof blob, 0);
    CHECK(cz_brightness() == 10);
    CHECK(cz_screen() != CZ_S_HATCH);
    cz_init(0, NULL, 0, 0);
    CHECK(cz_brightness() == 100);
}

static void swipe(int dir) {
    cz_touch(CZ_TOUCH_DOWN, 120 - 30 * dir, 120, now);
    now += 100; cz_touch(CZ_TOUCH_MOVE, 120 + 30 * dir, 121, now);
    now += 50; cz_touch(CZ_TOUCH_UP, 120 + 30 * dir, 121, now);
    now += 400;
}

static void hold(void) { cz_touch(CZ_TOUCH_DOWN, 120, 120, now); run(1100); cz_touch(CZ_TOUCH_UP, 120, 120, now); now += 400; }

static void test_settings(void) {
    int i;
    boot();
    cz_set_clock(12 * 60);
    quick_tap(); quick_tap();
    CHECK(cz_screen() == CZ_S_SETTINGS);
    check_and_write("settings");
    swipe(1); CHECK(cz_brightness() == 40);  /* brightness applies at once */
    swipe(1); CHECK(cz_brightness() == 10);
    swipe(-1); CHECK(cz_brightness() == 40);
    swipe(1); swipe(1); CHECK(cz_brightness() == 100);
    tap();                                   /* Clock */
    swipe(1); CHECK(cz_clock() == 12 * 60 + 15);
    swipe(-1); swipe(-1); CHECK(cz_clock() == 11 * 60 + 45);
    CHECK(cz_brightness() == 100);
    check_and_write("settings_clock");
    tap();                                   /* Timezone: +1 h moves the local clock */
    swipe(1); CHECK(cz_timezone() == 9); CHECK(cz_clock() == 12 * 60 + 45);
    swipe(-1); CHECK(cz_timezone() == 8);
    tap(); tap();                            /* Citizen, WiFi */
    CHECK(cz_host_request() == CZ_REQ_NONE);
    swipe(1); CHECK(cz_host_request() == CZ_REQ_PORTAL); CHECK(cz_host_request() == CZ_REQ_NONE);
    tap();                                   /* Update: check, then install once the host reports one */
    swipe(1); CHECK(cz_host_request() == CZ_REQ_CHECK_UPDATE);
    cz_set_version("1.1.0"); cz_set_update("1.2.0");
    swipe(1); CHECK(cz_host_request() == CZ_REQ_INSTALL_UPDATE); CHECK(cz_host_request() == CZ_REQ_NONE);
    check_and_write("settings_update");
    CHECK(cz_screen() == CZ_S_SETTINGS);
    hold(); CHECK(cz_screen() == CZ_S_HOME); /* hold exits */

    quick_tap(); quick_tap(); CHECK(cz_screen() == CZ_S_SETTINGS);
    run(19900); CHECK(cz_screen() == CZ_S_SETTINGS);
    run(200); CHECK(cz_screen() == CZ_S_HOME); /* 20 s idle exits */

    quick_tap(); quick_tap(); CHECK(cz_screen() == CZ_S_SETTINGS);
    for (i = 0; i < 7; i++) tap();           /* tap cycles to Exit */
    CHECK(cz_screen() == CZ_S_SETTINGS);
    swipe(1); CHECK(cz_screen() == CZ_S_HOME);

    /* a double tap that starts off Home (QR dismiss) is two plain taps */
    hold(); CHECK(cz_screen() == CZ_S_QR);
    quick_tap(); quick_tap(); CHECK(cz_screen() == CZ_S_FEED);
}

static void test_clock(void) {
    uint8_t blob[CZ_SAVE_SIZE];
    static const int at[4] = {3 * 60, 7 * 60, 12 * 60, 18 * 60 + 30};
    static const char *const names[4] = {"home_0300", "home_0700", "home_1200", "home_1830"};
    int sky[4], i, j;
    boot();
    CHECK(cz_clock() == -1);
    CHECK(cz_timezone() == 8);
    sky[0] = cz_framebuffer()[20 * CZ_W + 20];
    cz_set_clock(3 * 60);
    CHECK(cz_framebuffer()[20 * CZ_W + 20] == sky[0]); /* unknown renders night */
    cz_set_clock(23 * 60 + 59);
    run(60000);
    CHECK(cz_clock() == 0);                  /* minutes advance with ticks and wrap at midnight */
    cz_set_clock(12 * 60);
    run(61000);
    CHECK(cz_clock() == 12 * 60 + 1);
    cz_set_timezone(-3); cz_set_timezone(15);
    CHECK(cz_timezone() == -3);
    CHECK(cz_save(blob, sizeof blob) == CZ_SAVE_SIZE);
    now = 5;
    cz_init(0, blob, sizeof blob, 2 * 3600 + 30); /* deep sleep of 2 h 30 s */
    CHECK(cz_clock() == 14 * 60 + 1);
    CHECK(cz_timezone() == -3);
    cz_init(0, blob, sizeof blob, 8 * 86400 + 60); /* longer than the 7-day age cap: time of day still exact */
    CHECK(cz_clock() == 12 * 60 + 2);
    blob[2] = 2; /* a version 2 blob (pre-clock firmware) still restores, clock unknown */
    { uint32_t h = 2166136261UL; int k; for (k = 0; k < 27; k++) { h ^= blob[k]; h *= 16777619UL; }
      for (k = 27; k < CZ_SAVE_SIZE - 4; k++) { h ^= 0; h *= 16777619UL; }
      for (k = 27; k < CZ_SAVE_SIZE; k++) blob[k] = 0;
      blob[60] = (uint8_t)h; blob[61] = (uint8_t)(h >> 8); blob[62] = (uint8_t)(h >> 16); blob[63] = (uint8_t)(h >> 24); }
    cz_init(0, blob, sizeof blob, 0);
    CHECK(cz_screen() != CZ_S_HATCH);
    CHECK(cz_clock() == -1);
    CHECK(cz_timezone() == 8);

    boot();
    for (i = 0; i < 4; i++) {
        cz_set_clock(at[i]);
        sky[i] = cz_framebuffer()[20 * CZ_W + 20];
        check_and_write(names[i]);
    }
    printf("  sky at (20,20): 03:00=%d 07:00=%d 12:00=%d 18:30=%d\n", sky[0], sky[1], sky[2], sky[3]);
    for (i = 0; i < 4; i++) for (j = i + 1; j < 4; j++) CHECK(sky[i] != sky[j]);
}

static void test_qr(void) {
    const uint8_t *fb;
    int px, py, p, x, ox, run_len, m, cx, cy;
    boot();
    cz_set_qr_url("https://prmpt.city/c/tenuk-d2e8");
    hold();
    CHECK(cz_screen() == CZ_S_QR);
    check_and_write("qr_slug");
    fb = cz_framebuffer();
    for (py = 0; py < 188 && fb[py * CZ_W + 120] != CZ_C_INK; py++) {}  /* panel top at the centre column */
    for (px = 120; px > 0 && fb[(py + 1) * CZ_W + px - 1] == CZ_C_INK; px--) {}
    for (x = 120; x < CZ_W && fb[(py + 1) * CZ_W + x] == CZ_C_INK; x++) {}
    p = x - px;
    for (ox = 0; fb[(py + ox) * CZ_W + px + ox] != CZ_C_DEEP; ox++) {}  /* diagonal to the finder corner: quiet zone */
    for (run_len = 0; fb[(py + ox) * CZ_W + px + ox + run_len] == CZ_C_DEEP; run_len++) {}
    m = run_len / 7;
    printf("  qr \"https://prmpt.city/c/tenuk-d2e8\": %d px modules, %d modules, panel %d px at (%d,%d), quiet %d px\n",
           m, p / m - 4, p, px, py, ox);
    CHECK(run_len == 7 * m && ox == 2 * m);  /* finder is 7 modules, quiet zone 2 */
    CHECK(m == 6);                           /* version 2 fits */
    CHECK(p == (p / m) * m);
    CHECK(px + p + px == CZ_W);              /* centred across */
    CHECK(py + p == 188 && fb[188 * CZ_W + 120] == CZ_C_GOLD); /* rests on the gold band */
    /* every code corner inside the round glass (radius 120 around (120,120)) */
    cx = px + ox - 120; cy = py + ox - 120;
    CHECK(cx * cx + cy * cy <= 121 * 121);

    cz_set_qr_url("https://prmpt.city/c/kenyalang-7f3a9c");
    fb = cz_framebuffer();
    for (py = 0; py < 188 && fb[py * CZ_W + 120] != CZ_C_INK; py++) {}
    for (px = 120; px > 0 && fb[(py + 1) * CZ_W + px - 1] == CZ_C_INK; px--) {}
    for (ox = 0; fb[(py + ox) * CZ_W + px + ox] != CZ_C_DEEP; ox++) {}
    printf("  qr longer slug: quiet %d px -> %d px modules\n", ox, ox / 2);
    CHECK(ox == 10);                          /* version 3: 5 px */
}

static void test_dismiss(void) {
    boot();
    tap(); CHECK(cz_screen() == CZ_S_FEED); tap(); CHECK(cz_screen() == CZ_S_HOME);
    cz_touch(CZ_TOUCH_DOWN, 60, 120, now); now += 200; cz_touch(CZ_TOUCH_UP, 160, 120, now);
    CHECK(cz_screen() == CZ_S_PLAY); now += 400; tap(); CHECK(cz_screen() == CZ_S_HOME);
    cz_message("HI"); CHECK(cz_screen() == CZ_S_TALK); tap(); CHECK(cz_screen() == CZ_S_HOME);
    cz_checkin("KL"); CHECK(cz_screen() == CZ_S_CHECKIN); tap(); CHECK(cz_screen() == CZ_S_HOME);
}

static void test_dither(void) {
    const uint8_t *fb;
    boot();
    run(31000);
    CHECK(cz_screen() == CZ_S_SLEEP);
    fb = cz_framebuffer();
    /* left of the pet: clock tower and low blocks stay solid DEEP, the sky around them keeps navy/dusk */
    CHECK(count_color(fb, 40, 118, 56, 164, CZ_C_DEEP, 1) == 16 * 46);
    CHECK(count_color(fb, 0, 100, 56, 164, CZ_C_DEEP, 1) >= 200);
    CHECK(count_color(fb, 0, 40, 40, 128, CZ_C_DEEP, 0) >= 200);
}

/* topmost row in the sprite columns (56..183, below the prompt line) that is not the DEEP background */
static int sprite_top(void) {
    const uint8_t *fb = cz_framebuffer();
    int x, y;
    for (y = 40; y < 188; y++)
        for (x = 56; x < 184; x++) if (fb[y * CZ_W + x] != CZ_C_DEEP) return y;
    return 188;
}

static void test_hatch_rise(void) {
    int top[3], y0, y1;
    now = 1000;
    cz_init(0, NULL, 0, 0);
    cz_set_prompt("HI"); /* one short line keeps the text clear of the sprite box; typing ends at 120 ms */
    cz_tick(now);
    now += 120; cz_tick(now);
    CHECK(cz_screen() == CZ_S_HATCH);
    top[0] = sprite_top();                     /* hold start */
    check_and_write("hatch_rise_start");
    cz_dirty_rows(&y0, &y1);
    now += 1000; cz_tick(now);
    CHECK(cz_screen() == CZ_S_HATCH);
    CHECK(cz_dirty_rows(&y0, &y1) == 1);       /* mid frame: only the sprite box above the band */
    printf("  hatch rise mid frame dirty rows %d..%d\n", y0, y1);
    CHECK(y0 >= 100 && y1 <= 187);
    top[1] = sprite_top();                     /* mid */
    check_and_write("hatch_rise_mid");
    now += 999; cz_tick(now);
    CHECK(cz_screen() == CZ_S_HATCH);
    top[2] = sprite_top();                     /* end */
    check_and_write("hatch_rise_end");
    printf("  hatch rise sprite top rows: start %d, mid %d, end %d\n", top[0], top[1], top[2]);
    CHECK(top[0] > top[1] && top[1] > top[2]);
    CHECK(top[1] - top[2] == 122 - 100);       /* ease-out: y 122 at mid, 100 at end */
}

/* PRD 2.6: four consecutive animation steps on one screen. Each differs from the one before, changes nothing
 * outside the box the core declares for the screen, and equals a full redraw of the same state. */
static void check_anim(const char *name, int screen) {
    static uint8_t prev[CZ_W * CZ_H], cur[CZ_W * CZ_H];
    int bx, by, bw, bh, step, i, y0, y1, changed, outside, same, rows = 0;
    uint32_t t;
    CHECK(cz_screen() == screen);
    cz_anim_box(screen, &bx, &by, &bw, &bh);
    CHECK(bw > 0 && bh > 0);
    cz_tick(now);         /* catch the core up to the test clock first */
    cz_set_name("TENUK"); /* full frame as the baseline */
    memcpy(prev, cz_framebuffer(), sizeof prev);
    cz_dirty_rows(&y0, &y1);
    for (step = 0; step < 4; step++) {
        for (t = 0; t < 2000 && !cz_dirty_rows(&y0, &y1); t += 10) { now += 10; cz_tick(now); }
        CHECK(t < 2000);
        CHECK(cz_screen() == screen);
        memcpy(cur, cz_framebuffer(), sizeof cur);
        changed = outside = 0;
        for (i = 0; i < CZ_W * CZ_H; i++)
            if (cur[i] != prev[i]) {
                int x = i % CZ_W, y = i / CZ_W;
                changed++;
                outside += x < bx || x >= bx + bw || y < by || y >= by + bh;
            }
        if (!changed || outside) printf("  anim %s step %d: %d px changed, %d outside the box\n", name, step, changed, outside);
        CHECK(changed > 0);
        CHECK(outside == 0);
        CHECK(y0 >= by && y1 < by + bh); /* the partial frame pushes only box rows */
        if (y1 - y0 + 1 > rows) rows = y1 - y0 + 1;
        cz_set_name("TENUK");            /* full redraw of the same state */
        same = memcmp(cur, cz_framebuffer(), sizeof cur) == 0;
        CHECK(same);
        cz_dirty_rows(&y0, &y1);
        memcpy(prev, cur, sizeof prev);
    }
    printf("  anim %-7s box (%d,%d)-(%d,%d), 4 steps, max dirty rows %d\n", name, bx, by, bx + bw, by + bh, rows);
}

static void test_anim(void) {
    static uint8_t base[CZ_W * CZ_H];
    int i, x, y, y0, y1, outside = 0, frames = 0, rows = 0, top[2], bot[2], sq;
    boot(); check_anim("home", CZ_S_HOME);
    boot(); tap(); check_anim("feed", CZ_S_FEED);
    boot(); swipe(1); check_anim("play", CZ_S_PLAY);
    boot(); cz_message("What did you ship"); check_anim("talk", CZ_S_TALK);
    boot(); hold(); check_anim("qr", CZ_S_QR);
    boot(); cz_checkin("KL"); check_anim("checkin", CZ_S_CHECKIN);
    boot(); run(6UL * 3600000); tap(); check_anim("sulk", CZ_S_SULK);
    boot(); run(31000); check_anim("sleep", CZ_S_SLEEP);

    /* Home across 10 s: no pixel outside the sprite box (56,36)-(184,174) changes, idle frames <= 130 rows */
    boot();
    cz_set_name("TENUK");
    memcpy(base, cz_framebuffer(), sizeof base);
    cz_dirty_rows(&y0, &y1);
    for (i = 0; i < 200; i++) {
        now += 50; cz_tick(now);
        if (!cz_dirty_rows(&y0, &y1)) continue;
        frames++;
        if (y1 - y0 + 1 > rows) rows = y1 - y0 + 1;
        for (y = 0; y < CZ_H; y++)
            for (x = 0; x < CZ_W; x++)
                if ((x < 56 || x >= 184 || y < 36 || y >= 174) && cz_fb[y * CZ_W + x] != base[y * CZ_W + x]) outside++;
    }
    printf("  home 10 s: %d frames, max %d rows, %d px changed outside the sprite box\n", frames, rows, outside);
    CHECK(cz_screen() == CZ_S_HOME && frames >= 16 && rows <= 130 && outside == 0);

    /* squash: the same sprite 2 px shorter at 2x, top lowered, feet where they were */
    for (sq = 0; sq < 2; sq++) {
        cz_rect(0, 0, CZ_W, CZ_H, CZ_C_NAVY);
        cz_blit(cz_sprite(CZ_TAPIR, CZ_PLAY), 0, 0, 2, sq);
        for (top[sq] = 0; top[sq] < CZ_H && count_color(cz_fb, 0, top[sq], CZ_W, top[sq] + 1, CZ_C_NAVY, 0) == 0; top[sq]++) {}
        for (bot[sq] = CZ_H - 1; bot[sq] > 0 && count_color(cz_fb, 0, bot[sq], CZ_W, bot[sq] + 1, CZ_C_NAVY, 0) == 0; bot[sq]--) {}
    }
    CHECK(top[1] == top[0] + 2 && bot[1] == bot[0]);
    cz_set_name("TENUK"); /* repaint the screen the blit test scribbled on */
}


static void test_double_tap_undoes_feed(void) {
    int h, hunger_before;
    boot();
    run(1000);
    h = cz_hearts(); hunger_before = (int)cz_hunger_ms();
    quick_tap(); CHECK(cz_screen() == CZ_S_FEED);
    run(100); quick_tap();
    CHECK(cz_screen() == CZ_S_SETTINGS);
    CHECK(cz_hearts() == h);
    CHECK((int)cz_hunger_ms() >= hunger_before);
    printf("  double tap: feed undone, hearts stay %d, settings open\n", cz_hearts());
}

int main(void) {
    test_flow();
    test_save();
    test_screens();
    test_telemetry();
    test_brightness();
    test_settings();
    test_clock();
    test_qr();
    test_dismiss();
    test_dither();
    test_hatch_rise();
    test_anim();
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    test_double_tap_undoes_feed();
    printf("all core tests passed\n");
    return 0;
}
