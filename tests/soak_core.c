/* 7 days of badge life in simulated time. Run with `make soak`.
 * Ticks once per simulated second, every 50 ms within 5 s of an input. Random gestures from a seeded LCG at an
 * event-day rate, chat messages, a check-in, an hourly save/init round trip and a nightly 8 h deep sleep.
 * Every rendered frame is checked; the first failure stops the run with its simulated timestamp. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "citizen.h"

#define DAYS 7
#define END_MS ((uint32_t)DAYS * 24 * 3600000)
#define HOUR_MS 3600000UL
#define FAST_MS 5000
#define NAME "TENUK" /* seed 0 hatches the tapir */

static uint32_t now;        /* simulated ms since boot; also the host's monotonic clock */
static uint32_t fast_until; /* tick every 50 ms until then */
static uint32_t rng = 12345;
static unsigned long frames, ticks, rows_total, gestures, messages, checkins, roundtrips, deepsleeps;
static unsigned long long bytes_total;
static int hmin = 4, hmax = 0, visited;

static uint32_t lcg(void) { rng = rng * 1664525UL + 1013904223UL; return rng >> 8; } /* 24 random bits */

static const char *stamp(void) {
    static char b[32];
    snprintf(b, sizeof b, "day %u %02u:%02u:%02u.%03u", now / 86400000, now / 3600000 % 24, now / 60000 % 60,
             now / 1000 % 60, now % 1000);
    return b;
}

#define ASSERT(c) do { if (!(c)) { printf("SOAK FAILED at %s (t=%u ms): %s\n", stamp(), now, #c); exit(1); } } while (0)

static void check_fb(void) {
    const uint8_t *fb = cz_framebuffer();
    int i;
    for (i = 0; i < CZ_W * CZ_H; i++) ASSERT(fb[i] < CZ_C_COUNT);
}

/* after every tick or input: account the frame if one was drawn, then check it */
static void frame(void) {
    static uint8_t a[CZ_W * CZ_H];
    int y0, y1, s = cz_screen(), h = cz_hearts();
    ASSERT(h >= 0 && h <= 4);
    ASSERT(s >= 0 && s < CZ_SCREENS);
    visited |= 1 << s;
    if (h < hmin) hmin = h;
    if (h > hmax) hmax = h;
    if (!cz_dirty_rows(&y0, &y1)) return;
    ASSERT(y0 >= 0 && y1 <= CZ_H - 1 && y0 <= y1);
    frames++;
    rows_total += (unsigned long)(y1 - y0 + 1);
    bytes_total += (unsigned long long)(y1 - y0 + 1) * CZ_W * 2;
    check_fb();
    /* determinism: two consecutive full redraws of the same state are identical */
    cz_set_name(NAME);
    memcpy(a, cz_framebuffer(), sizeof a);
    cz_set_name(NAME);
    ASSERT(memcmp(a, cz_framebuffer(), sizeof a) == 0);
    cz_dirty_rows(&y0, &y1); /* the forced redraws are not frames the badge would push */
}

static void tick_to(uint32_t target) {
    while (now < target) {
        uint32_t step = now < fast_until ? 50 : 1000;
        now = now + step < target ? now + step : target;
        cz_tick(now);
        ticks++;
        frame();
    }
}

static void touch(int kind, int x, int y, uint32_t after_ms) {
    tick_to(now + after_ms);
    cz_touch(kind, x, y, now);
    frame();
}

static void gesture(void) {
    fast_until = now + FAST_MS;
    switch (lcg() % 4) {
    case 0: touch(CZ_TOUCH_DOWN, 120, 120, 0); touch(CZ_TOUCH_UP, 121, 120, 100); break;                    /* tap */
    case 1: touch(CZ_TOUCH_DOWN, 60, 120, 0); touch(CZ_TOUCH_MOVE, 110, 121, 100);
            touch(CZ_TOUCH_MOVE, 160, 122, 100); touch(CZ_TOUCH_UP, 160, 122, 50); break;                 /* swipe */
    case 2: touch(CZ_TOUCH_DOWN, 120, 120, 0); touch(CZ_TOUCH_UP, 120, 120, 1100); break;                  /* hold */
    default: touch(CZ_TOUCH_DOWN, 120, 120, 0); touch(CZ_TOUCH_UP, 120, 120, 80);
             touch(CZ_TOUCH_DOWN, 120, 120, 100); touch(CZ_TOUCH_UP, 120, 120, 80); break;                 /* double tap */
    }
    gestures++;
    fast_until = now + FAST_MS;
}

/* interactions per hour: 60 for 12 h, 2 for 36 h, none for 48 h, then 10 */
static int rate(uint32_t h) { return h < 12 ? 60 : h < 48 ? 2 : h < 96 ? 0 : 10; }

static void reinit(uint32_t slept_s) {
    uint8_t blob[CZ_SAVE_SIZE];
    ASSERT(cz_save(blob, sizeof blob) == CZ_SAVE_SIZE);
    now += slept_s * 1000;
    cz_init(0, blob, sizeof blob, slept_s);
    ASSERT(cz_screen() != CZ_S_HATCH); /* the blob kept the hatched flag */
    cz_tick(now);
    frame();
}

static double wall_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(void) {
    static const char *const names[CZ_SCREENS] = {"hatch", "home", "feed", "play", "talk", "qr", "checkin", "sulk", "sleep", "settings"};
    double t0 = wall_s();
    uint32_t sec, hour = 0, next_msg = 1200000;
    int s;

    now = 0;
    cz_init(0, NULL, 0, 0);
    cz_tick(now);
    frame();
    fast_until = 8000; /* hatch typing and rise */
    while (now < END_MS) {
        sec = now / 1000;
        if (now / HOUR_MS != hour) { /* a gesture can carry now past the boundary, so compare hours */
            hour = now / HOUR_MS;
            if (hour % 24 == 16) { reinit(8 * 3600); deepsleeps++; hour = now / HOUR_MS; } /* nightly deep sleep, 16:00 to 24:00 */
            reinit(0);
            roundtrips++;
        }
        if (now >= next_msg && sec < 12 * 3600) { cz_message("What did you ship"); frame(); messages++; next_msg += 1200000; fast_until = now + FAST_MS; }
        if (!checkins && sec >= 2 * 3600) { cz_checkin("KL"); frame(); checkins++; fast_until = now + FAST_MS; }
        if (lcg() % 3600 < (uint32_t)rate(sec / 3600)) gesture();
        tick_to((now / 1000 + 1) * 1000); /* next whole simulated second */
    }

    printf("Citizen soak: %d simulated days, LCG seed 12345, wall %.2f s\n", DAYS, wall_s() - t0);
    printf("  ticks %lu, frames %lu, dirty rows %lu, bytes pushed %llu (%.1f MB)\n", ticks, frames, rows_total,
           bytes_total, bytes_total / 1e6);
    printf("  events: gestures %lu, messages %lu, check-ins %lu, hourly round trips %lu, deep sleeps %lu\n",
           gestures, messages, checkins, roundtrips, deepsleeps);
    printf("  hearts min %d max %d; screens visited:", hmin, hmax);
    for (s = 0; s < CZ_SCREENS; s++) if (visited & 1 << s) printf(" %s", names[s]);
    printf("\nSOAK PASSED\n");
    return 0;
}
