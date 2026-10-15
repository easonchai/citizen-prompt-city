/* Core cost per frame on this machine. Run with `make bench` (built -O2; the firmware is built -Os by Arduino).
 * Per screen: median of 5 runs of 2000 forced full redraws, dirty rows, and the SPI push time on the badge. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "citizen.h"

#define N 2000
#define RUNS 5
#define NAME "TENUK" /* seed 0 hatches the tapir */

static uint32_t now;

/* helpers copied from tests/test_core.c */
static void run(uint32_t ms) { uint32_t end = now + ms; while (now < end) { now += 50; cz_tick(now); } }
static void quick_tap(void) { cz_touch(CZ_TOUCH_DOWN, 120, 120, now); now += 100; cz_touch(CZ_TOUCH_UP, 121, 120, now); }
static void tap(void) { quick_tap(); now += 400; }
static void boot(void) { now = 1000; cz_init(0, NULL, 0, 0); cz_tick(now); run(6000); }

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median(double *v) { qsort(v, RUNS, sizeof *v, cmp); return v[RUNS / 2]; }

/* SPI push on the badge: rows * 240 px * 2 B * 8 bit at 40 MHz + 40 us per 8-row chunk */
static double push_us(int rows) { return rows * 240.0 * 2 * 8 / 40e6 * 1e6 + (rows + 7) / 8 * 40.0; }

static double slowdown;

static void row(const char *name, double us, int rows) {
    double est = us * slowdown / 1000, push = rows ? push_us(rows) / 1000 : 0, total = est + push;
    double fps = total > 0 ? 1000 / total : 30;
    printf("%-14s %9.3f %6d %9.3f %10.3f %10.3f %6.1f\n", name, us, rows, push, est, total, fps < 30 ? fps : 30);
}

static void enter(int screen) {
    switch (screen) {
    case CZ_S_HATCH: now = 1000; cz_init(0, NULL, 0, 0); cz_tick(now); run(1500); break;
    case CZ_S_HOME: boot(); break;
    case CZ_S_FEED: boot(); tap(); break;
    case CZ_S_PLAY:
        boot();
        cz_touch(CZ_TOUCH_DOWN, 80, 120, now);
        now += 100; cz_touch(CZ_TOUCH_MOVE, 110, 121, now);
        now += 100; cz_touch(CZ_TOUCH_MOVE, 140, 122, now);
        now += 50; cz_touch(CZ_TOUCH_UP, 140, 122, now);
        break;
    case CZ_S_TALK: boot(); cz_message("What did you ship"); break;
    case CZ_S_QR: boot(); cz_touch(CZ_TOUCH_DOWN, 120, 120, now); run(1100); cz_touch(CZ_TOUCH_UP, 120, 120, now); break;
    case CZ_S_CHECKIN: boot(); cz_checkin("KL"); break;
    case CZ_S_SULK: boot(); run(6UL * 3600000); tap(); break; /* asleep by then; the tap wakes it into Sulk */
    case CZ_S_SLEEP: boot(); run(30000); break;
    }
    if (cz_screen() != screen) { printf("bench: wanted screen %d, got %d\n", screen, cz_screen()); exit(1); }
}

/* idle Home at blink phase 0: feed first (a blink run spans 67 simulated minutes; hearts drop every 90),
 * wait for a blink to end, then flush */
static void blink_phase0(void) {
    int y0, y1;
    tap(); run(3100);
    while (cz_pose() != CZ_BLINK) { now += 1; cz_tick(now); }
    while (cz_pose() == CZ_BLINK) { now += 1; cz_tick(now); }
    cz_touch(CZ_TOUCH_MOVE, 0, 0, now); /* resets the idle timer without a gesture (no finger down) */
    cz_dirty_rows(&y0, &y1);
}

int main(void) {
    static const char *const names[CZ_SCREENS] = {"hatch", "home", "feed", "play", "talk", "qr", "checkin", "sulk", "sleep"};
    const char *env = getenv("CZ_SLOWDOWN");
    double t[RUNS], t0;
    int s, r, i, y0, y1, rows, frames;

    slowdown = env && atof(env) > 0 ? atof(env) : 120;
    printf("Citizen core bench: %d frames x %d runs, median; native -O2 (firmware is -Os)\n", N, RUNS);
    printf("ESP32-C3 est = native us x CZ_SLOWDOWN=%g. Placeholder: a 160 MHz single-issue RISC-V vs this Mac;\n"
           "replace with the firmware's measured frame_ms once hardware exists.\n", slowdown);
    printf("push = rows*240*2*8/40e6 s + 40 us per 8-row chunk. total = est + push; fps = min(30, 1000/total).\n\n");
    printf("%-14s %9s %6s %9s %10s %10s %6s\n", "case", "native_us", "rows", "push_ms", "esp_est_ms", "total_ms", "fps");

    for (s = 0; s < CZ_SCREENS; s++) {
        enter(s);
        rows = 0;
        for (r = 0; r < RUNS; r++) {
            t0 = now_us();
            for (i = 0; i < N; i++) { cz_set_name(NAME); cz_dirty_rows(&y0, &y1); }
            t[r] = (now_us() - t0) / N;
        }
        cz_set_name(NAME);
        if (cz_dirty_rows(&y0, &y1)) rows = y1 - y0 + 1;
        row(names[s], median(t), rows);
    }

    /* idle Home: every tick lands on a blink edge (on at 3850 ms, off at 4000 ms), so every tick draws a blink frame */
    boot();
    rows = 0;
    for (r = 0; r < RUNS; r++) {
        blink_phase0();
        frames = 0;
        t0 = now_us();
        for (i = 0; i < N; i++) {
            now += i % 2 ? 150 : 3850;
            cz_touch(CZ_TOUCH_MOVE, 0, 0, now); /* keeps it awake; ticks the core like cz_tick */
            if (cz_dirty_rows(&y0, &y1)) { frames++; if (y1 - y0 + 1 > rows) rows = y1 - y0 + 1; }
        }
        t[r] = (now_us() - t0) / N;
        if (frames != N || cz_screen() != CZ_S_HOME) { printf("bench: blink run drew %d of %d frames\n", frames, N); return 1; }
    }
    row("home_blink", median(t), rows);

    /* nothing changes: 1 ms ticks inside the idle phase, no redraw */
    for (r = 0; r < RUNS; r++) {
        blink_phase0();
        frames = 0;
        t0 = now_us();
        for (i = 0; i < N; i++) { now += 1; cz_tick(now); frames += cz_dirty_rows(&y0, &y1); }
        t[r] = (now_us() - t0) / N;
        if (frames) { printf("bench: idle run drew %d frames\n", frames); return 1; }
    }
    row("home_nochange", median(t), 0);
    return 0;
}
