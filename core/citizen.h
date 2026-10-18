/* Citizen core. Platform independent, C99, no libc required at runtime.
 * One instance. The host owns time, input and storage; the core owns the pet and the pixels.
 *
 * Targets: ESP32-C3 (Arduino / PlatformIO), wasm32 freestanding (web simulator), native tests.
 * Framebuffer: 240x240, 8-bit palette indices (57,600 bytes, static). Host converts to RGB565.
 */
#ifndef CITIZEN_H
#define CITIZEN_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CZ_W 240
#define CZ_H 240
#define CZ_SPRITE 64
#define CZ_SAVE_SIZE 64          /* bytes needed by cz_save */
#define CZ_PROMPT_MAX 96
#define CZ_MSG_MAX 64

enum cz_creature { CZ_TAPIR = 0, CZ_HORNBILL = 1, CZ_TIGER = 2, CZ_CREATURES = 3 };
enum cz_pose { CZ_IDLE, CZ_BLINK, CZ_EAT, CZ_PLAY, CZ_SLEEP, CZ_SULK, CZ_TALK, CZ_CHEER, CZ_POSES };
enum cz_screen { CZ_S_HATCH, CZ_S_HOME, CZ_S_FEED, CZ_S_PLAY, CZ_S_TALK, CZ_S_QR, CZ_S_CHECKIN, CZ_S_SULK, CZ_S_SLEEP,
                 CZ_S_SETTINGS, CZ_S_EGG, CZ_S_WIFI, CZ_S_PORTAL, CZ_SCREENS };
enum cz_touch { CZ_TOUCH_DOWN = 0, CZ_TOUCH_MOVE = 1, CZ_TOUCH_UP = 2 };

/* Palette indices used by the framebuffer and the sprites. 0 is transparent in sprites only. */
enum cz_color { CZ_C_TRANSPARENT = 0, CZ_C_NAVY, CZ_C_DUSK, CZ_C_DEEP, CZ_C_INK, CZ_C_GOLD, CZ_C_CORAL, CZ_C_TEAL,
                CZ_C_CHAR, CZ_C_PINK, CZ_C_PALE, CZ_C_DAYSKY, CZ_C_DAYHORIZON, CZ_C_ROSE, CZ_C_CLOUD, CZ_C_COUNT };

/* Lifecycle.
 * saved may be NULL. If it is a valid blob from cz_save the pet resumes, including time that passed
 * while the host was asleep: pass the wall-clock seconds the host believes have elapsed (0 if unknown). */
void cz_init(uint32_t seed, const uint8_t *saved, int saved_len, uint32_t slept_seconds);
int  cz_save(uint8_t *out, int cap);          /* returns bytes written, 0 on failure */

/* Time and input. now_ms is monotonic. Call cz_tick often (every 16-50 ms). */
void cz_tick(uint32_t now_ms);
void cz_touch(int kind, int x, int y, uint32_t now_ms);   /* raw touch; core detects tap, swipe, hold */

/* Content the host sets. */
void cz_set_creature(int creature);            /* also owned from now on (before Hatch ends: the only one owned) */
void cz_set_portrait(int on);                 /* web page portrait: Home without hearts or actions, ignores touch, never sleeps */
void cz_set_name(const char *name);           /* shown in gold under the pet, uppercase, <= 10 chars */
void cz_set_prompt(const char *prompt);       /* the speaker's system prompt, typed out on Hatch */
void cz_set_qr_url(const char *url);          /* encoded on the QR screen */
void cz_message(const char *text);            /* a chat message arrived: shows Talk */
void cz_checkin(const char *city);            /* shows Check-in */

/* Output. */
const uint8_t  *cz_framebuffer(void);          /* CZ_W*CZ_H palette indices */
const uint16_t *cz_palette(void);              /* CZ_C_COUNT RGB565 values */
int  cz_dirty(void);                           /* 1 if the framebuffer changed since the last cz_dirty call */
int  cz_wants_sleep(void);                     /* 1 when the host may deep sleep; wake on touch and call cz_init with the save blob */

/* Telemetry for hosts (firmware pushes only dirty rows; the simulator models SPI cost from them). */
int  cz_dirty_rows(int *y0, int *y1);          /* like cz_dirty, but also returns the inclusive row span touched; resets */
enum cz_stat { CZ_STAT_FRAMES = 0, CZ_STAT_PIXELS_FILLED, CZ_STAT_SPRITE_PIXELS, CZ_STAT_TEXT_GLYPHS, CZ_STAT_COUNT };
uint32_t cz_stat(int which);                   /* monotonic counters since init */

/* Screen: AUTO lets the badge sleep after 30 s idle on Home; ALWAYS ON never asks the host to sleep
 * (for power banks that cut out at a sleeping badge's few microamps). Chosen in Settings, saved. */
int  cz_always_on(void);
void cz_set_always_on(int on);

/* Skies: the city drawn behind the pet. The host passes the skies the badge has unlocked (bit per enum cz_sky,
 * KL always on); Settings picks among them and the choice is saved. A newly unlocked sky is shown at once. */
enum cz_sky { CZ_SKY_KL = 0, CZ_SKY_SG = 1, CZ_SKY_TYO = 2, CZ_SKIES };
void cz_set_skies(unsigned mask);
int  cz_sky(void);                             /* the sky drawn: the chosen one if unlocked, else KL */

/* Brightness: 100, 40 or 10, chosen in Settings. The host drives the backlight. */
int  cz_brightness(void);
void cz_set_brightness(int pct);

/* Clock (v1.1). Minutes since local midnight, 0..1439; -1 = unknown. Persisted. */
void cz_set_clock(int minute_of_day);
int  cz_clock(void);
int  cz_timezone(void);                        /* UTC offset in hours, from Settings, default 8 */
void cz_set_timezone(int hours);

/* Settings (v1.1). The host feeds what the screen shows and polls for actions it must perform. */
enum cz_wifi_state { CZ_WIFI_OFF = 0, CZ_WIFI_CONNECTING, CZ_WIFI_OK, CZ_WIFI_FAIL };
void cz_set_version(const char *running);      /* e.g. "1.1.0" */
void cz_set_update(const char *available);     /* "" or NULL = up to date */
void cz_set_wifi(const char *ssid, int state);
enum cz_request { CZ_REQ_NONE = 0, CZ_REQ_PORTAL, CZ_REQ_CHECK_UPDATE, CZ_REQ_INSTALL_UPDATE, CZ_REQ_FACTORY_RESET,
                  CZ_REQ_SYNC, CZ_REQ_WIFI_FORGET };
int  cz_host_request(void);                    /* returns one pending request and clears it */

/* Creatures the citizen owns (bit per enum cz_creature), from the server's /state. 0 is ignored (nothing known).
 * A newly owned creature hatches from an egg (CZ_S_EGG) and becomes the shown one; Settings > Citizen steps
 * through owned creatures only. The mask is saved in the blob, so a boot re-apply is not new. */
void     cz_set_creatures(unsigned mask);
unsigned cz_creatures(void);

/* WiFi sub-screen. The host passes its saved networks, '\n'-separated, at most CZ_WIFI_NETS SSIDs of <= 32 bytes;
 * the core lists them, then ADD NETWORK, then BACK. Forget asks CZ_REQ_WIFI_FORGET; read which with cz_wifi_forget(). */
#define CZ_WIFI_NETS 5
void cz_set_wifi_list(const char *ssids);
int  cz_wifi_forget(void);                 /* index into the list last passed, -1 if none pending */

/* Portal screen: a QR to join the badge's own access point, "WIFI:T:nopass;S:<ap>;;", plus "192.168.4.1".
 * ap = "" or NULL leaves it. */
void cz_portal(const char *ap);

/* Introspection for the simulator and tests. */
int  cz_screen(void);
int  cz_hearts(void);                          /* 0..4 */
int  cz_creature(void);
int  cz_pose(void);
uint32_t cz_hunger_ms(void);                   /* ms since last feed */

/* 256-byte scratch buffer inside the core, for hosts that cannot pass pointers (wasm):
 * write a NUL-terminated string here, then call cz_set_name(cz_scratch()) etc. */
uint8_t *cz_scratch(void);

/* Sprites, generated by tools/pack_sprites.py into core/sprites.c */
const uint8_t *cz_sprite(int creature, int pose); /* CZ_SPRITE*CZ_SPRITE palette indices, 0 transparent */

#ifdef __cplusplus
}
#endif
#endif
