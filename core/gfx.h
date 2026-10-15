/* Drawing primitives over the static 240x240 palette framebuffer. Port of SCREEN_DESIGN_SOURCE.html. */
#ifndef CZ_GFX_H
#define CZ_GFX_H
#include "citizen.h"

enum cz_align { CZ_LEFT, CZ_CENTER, CZ_RIGHT };
enum cz_icon { CZ_I_HEART, CZ_I_HEARTO, CZ_I_BOWL, CZ_I_BALL, CZ_I_BUBBLE, CZ_I_ZZ, CZ_ICONS };

extern uint8_t cz_fb[CZ_W * CZ_H];
extern const uint16_t cz_pal565[CZ_C_COUNT];
extern const uint8_t cz_pal_rgb[CZ_C_COUNT][3];

void cz_rect(int x, int y, int w, int h, int color);                  /* clipped fill */
void cz_blit(const uint8_t *spr, int x, int y, int scale);            /* 64x64, 0 = transparent, nearest neighbour */
int  cz_text(const char *s, int x, int y, int color, int scale, int align); /* returns width in px */
int  cz_text_width(const char *s, int scale);
void cz_icon(int icon, int x, int y, int color, int scale);
void cz_dim(int alpha_pct);  /* 2x2 ordered dither toward DEEP: alpha_pct of pixels per 2x2 cell become DEEP */
void cz_clip(int x, int y, int w, int h);  /* every fill is clipped to this box; cz_clip(0, 0, CZ_W, CZ_H) resets */

/* Telemetry kept by the primitives: rows touched since the last read, and cz_stat counters. */
extern int cz_dy0, cz_dy1;   /* inclusive touched row span; cz_dy0 > cz_dy1 when nothing was touched */
extern uint32_t cz_stats[CZ_STAT_COUNT];

/* What screens.c needs to draw one frame; filled by citizen.c. */
struct cz_view {
    int screen, creature, pose, hearts, typed; /* typed: prompt bytes shown on Hatch */
    const char *name, *prompt, *msg, *city, *url;
};
void cz_draw(const struct cz_view *v);

#endif
