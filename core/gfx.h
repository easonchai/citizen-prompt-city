/* Drawing primitives over the static 240x240 palette framebuffer. Port of SCREEN_DESIGN_SOURCE.html. */
#ifndef CZ_GFX_H
#define CZ_GFX_H
#include "citizen.h"

enum cz_align { CZ_LEFT, CZ_CENTER, CZ_RIGHT };
enum cz_icon { CZ_I_HEART, CZ_I_HEARTO, CZ_I_BOWL, CZ_I_BALL, CZ_I_BUBBLE, CZ_I_ZZ, CZ_I_SUN, CZ_I_MOON, CZ_I_DROP, CZ_ICONS };
enum cz_phase { CZ_PH_NIGHT, CZ_PH_DAWN, CZ_PH_DAY, CZ_PH_DUSK };

extern uint8_t cz_fb[CZ_W * CZ_H];
extern const uint16_t cz_pal565[CZ_C_COUNT];
extern const uint8_t cz_pal_rgb[CZ_C_COUNT][3];

void cz_rect(int x, int y, int w, int h, int color);                  /* clipped fill */
void cz_blit(const uint8_t *spr, int x, int y, int scale, int squash); /* 64x64, 0 = transparent, nearest neighbour;
                                                                         squash: skip source row 32, feet stay planted */
int  cz_text(const char *s, int x, int y, int color, int scale, int align); /* returns width in px */
int  cz_text_width(const char *s, int scale);
void cz_icon(int icon, int x, int y, int color, int scale);
void cz_dim(int alpha_pct);  /* 2x2 ordered dither toward DEEP inside the clip: alpha_pct of pixels per 2x2 cell become DEEP */
void cz_clip(int x, int y, int w, int h);  /* every fill is clipped to this box; cz_clip(0, 0, CZ_W, CZ_H) resets */

/* Telemetry kept by the primitives: rows touched since the last read, and cz_stat counters. */
extern int cz_dy0, cz_dy1;   /* inclusive touched row span; cz_dy0 > cz_dy1 when nothing was touched */
extern uint32_t cz_stats[CZ_STAT_COUNT];

/* What screens.c needs to draw one frame; filled by citizen.c. */
struct cz_view {
    int screen, creature, pose, hearts, typed, phase; /* typed: prompt bytes shown on Hatch; phase: enum cz_phase */
    int anim;                                         /* animation state from cz_anim */
    const char *name, *prompt, *msg, *city, *url;
    const char *set_prev, *set_label, *set_value, *set_next; /* Settings rows */
    int set_arrows;                                   /* Settings value row: bit 0 draws the left arrow, bit 1 the right */
    int portrait;                                     /* Home as a portrait: no hearts, no action icons */
    int sky;                                          /* enum cz_sky: the city behind the pet */
};
void cz_draw(const struct cz_view *v);

/* Animation (PRD 2.6). cz_anim packs everything that moves on a screen at free-running time t (and screen time
 * for Feed crumbs) into a small integer: equal values draw equal frames. cz_anim_box gives the rectangle
 * [x0,x1) x [y0,y1) that holds every pixel an animation step can change; w == 0 on screens that do not animate. */
int  cz_anim(int screen, uint32_t t, uint32_t screen_ms);
void cz_anim_box(int screen, int *x, int *y, int *w, int *h);

#endif
