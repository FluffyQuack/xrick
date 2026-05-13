/*
 * xrick/include/game.h
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#ifndef _GAME_H
#define _GAME_H

#include <stddef.h> /* NULL */

#include "system.h"
#include "syssnd.h"

#include "rects.h"
#include "data.h"

#define LEFT 1
#define RIGHT 0

/*
 * Frame pacing.
 *
 * GAME_PACE_MODE selects the cadence used to advance game logic. Each entry
 * in a cadence pattern is the duration of one game frame in microseconds;
 * the pattern is cycled. The list-of-durations form is overkill for steady
 * rates but keeps the door open for non-uniform cadences if we ever want
 * them.
 *
 *   PACE_30FPS : steady 30 fps (33,333 us tick)
 *   PACE_25FPS : steady 25 fps (40,000 us tick) -- matches the Atari ST
 *                version's average gameplay rate
 *
 * GAME_RENDER_FPS bounds how often the framebuffer is presented. The render
 * path is content-driven (sysvid_update only presents when there are dirty
 * rects), so this is effectively a present-rate cap rather than a continuous
 * redraw rate.
 *
 * GAME_PERIOD is kept for legacy callers (the menu/scroller scripts that
 * stash and restore game_period); it is no longer the master clock.
 */
#define PACE_30FPS  0
#define PACE_25FPS  1

#define GAME_PACE_MODE   PACE_25FPS  /* <-- toggle between PACE_30FPS and PACE_25FPS */
#define GAME_RENDER_FPS  60

#define GAME_PERIOD 75

#define GAME_BOMBS_INIT 6
#define GAME_BULLETS_INIT 6

typedef struct {
  U32 score;
  U8 name[10];
} hscore_t;

extern hscore_t game_hscores[8];  /* highest scores (hall of fame) */

extern U8 game_waitevt;    /* wait for events (TRUE, FALSE) */
extern U8 game_period;     /* time between each frame, in millisecond */
extern U8 game_interpolate; /* render interpolation toggle (F10) */
extern U8 game_realtimeScroll; /* realtime camera scroll toggle */
extern S8 game_scroll_step; /* +8/-8 during a scroll tick, 0 otherwise */

extern rect_t *game_rects; /* rectangles to redraw at each frame */

extern void game_run(char *path);

extern void game_toggleCheat(U8);

#endif

/* eof */


