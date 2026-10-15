/* Native tests for the Citizen core. Run with `make test`. Writes every screen to tests/out/<screen>.ppm. */
#include <stdio.h>
#include <stdlib.h>
#include "citizen.h"

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
            CHECK(y0 >= 44 && y1 <= 171 && y1 >= y0);
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
    quick_tap(); quick_tap();            /* first tap feeds, second is the double tap */
    CHECK(cz_brightness() == 40);
    CHECK(cz_screen() == CZ_S_FEED);     /* second tap did not dismiss Feed */
    now += 400; quick_tap(); quick_tap();
    CHECK(cz_brightness() == 10);
    now += 400; quick_tap(); quick_tap();
    CHECK(cz_brightness() == 100);
    now += 400; quick_tap(); now += 450; quick_tap(); /* too slow: two single taps */
    CHECK(cz_brightness() == 100);
    now += 400; quick_tap(); quick_tap();
    CHECK(cz_brightness() == 40);
    cz_set_brightness(55);
    CHECK(cz_brightness() == 40);
    cz_set_brightness(10);
    CHECK(cz_save(blob, sizeof blob) == CZ_SAVE_SIZE);
    CHECK(blob[2] == 2);
    cz_init(0, blob, sizeof blob, 0);
    CHECK(cz_brightness() == 10);
    CHECK(cz_screen() != CZ_S_HATCH);
    cz_init(0, NULL, 0, 0);
    CHECK(cz_brightness() == 100);
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

int main(void) {
    test_flow();
    test_save();
    test_screens();
    test_telemetry();
    test_brightness();
    test_dismiss();
    test_dither();
    test_hatch_rise();
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("all core tests passed\n");
    return 0;
}
