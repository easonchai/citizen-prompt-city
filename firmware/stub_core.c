/* Stand-in for the Citizen core so the firmware links before core/citizen.c exists.
 * Compiles to nothing once the real core lands (see platformio.ini). cz_sprite comes from core/sprites.c. */
#if !__has_include("citizen.c")
#include <string.h>
#include "citizen.h"

static uint8_t fb[CZ_W * CZ_H];
static uint8_t scratch[256];
static const uint16_t pal[CZ_C_COUNT] = { 0x0000, 0x0010, 0x3166, 0x18E3, 0x0841, 0xFEA0, 0xFB2C, 0x0679, 0x4208, 0xFB9E, 0xFFDE };
static uint32_t last_dirty_s = 0xFFFFFFFF, now_s, frame;
static int creature;

void cz_init(uint32_t seed, const uint8_t *saved, int saved_len, uint32_t slept_seconds) { (void)seed; (void)saved; (void)saved_len; (void)slept_seconds; }
int cz_save(uint8_t *out, int cap) { if (cap < CZ_SAVE_SIZE) return 0; memset(out, 0, CZ_SAVE_SIZE); memcpy(out, "CZST", 4); out[4] = (uint8_t)creature; return CZ_SAVE_SIZE; }
void cz_tick(uint32_t now_ms) { now_s = now_ms / 1000; }
void cz_touch(int kind, int x, int y, uint32_t now_ms) { (void)kind; (void)x; (void)y; (void)now_ms; }
void cz_set_creature(int c) { creature = c; }
void cz_set_name(const char *s) { (void)s; }
void cz_set_prompt(const char *s) { (void)s; }
void cz_set_qr_url(const char *s) { (void)s; }
void cz_message(const char *s) { (void)s; }
void cz_checkin(const char *s) { (void)s; }
const uint8_t *cz_framebuffer(void) { return fb; }
const uint16_t *cz_palette(void) { return pal; }
int cz_dirty(void) {
    if (now_s == last_dirty_s) return 0;
    last_dirty_s = now_s; frame++;
    for (int y = 0; y < CZ_H; y++)
        for (int x = 0; x < CZ_W; x++)
            fb[y * CZ_W + x] = (uint8_t)(1 + ((x + y) / 48 + frame) % (CZ_C_COUNT - 1));
    return 1;
}
int cz_wants_sleep(void) { return 0; }
int cz_screen(void) { return CZ_S_HOME; }
int cz_hearts(void) { return 4; }
int cz_creature(void) { return creature; }
int cz_pose(void) { return CZ_IDLE; }
uint32_t cz_hunger_ms(void) { return 0; }
uint8_t *cz_scratch(void) { return scratch; }
#endif
