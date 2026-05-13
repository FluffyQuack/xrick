/*
 * xrick/include/scroller.h
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

#ifndef _SCROLLER_H
#define _SCROLLER_H

#include "system.h"

#define SCROLL_RUNNING 1
#define SCROLL_DONE 0

#define SCROLL_PERIOD 24

extern U8 scroll_up(void);
extern U8 scroll_down(void);

/* Realtime (follow-cam) scroll: perform up to one row-shift this tick in
 * the given direction (+1 = up, -1 = down, 0 = no shift). Does NOT pause
 * gameplay -- the caller is expected to fall through to CTRL_ACTION in
 * the same tick. Internally tracks the 8-row block accumulator so
 * map_expand / ent_actvis fire at the same boundaries as the legacy
 * 8-tick batch. */
extern void scroll_realtime_step(S8 dir);

/* Reset the realtime block accumulator. Call when entering a new submap
 * (where map_expand has just been run from scratch) or otherwise
 * resynchronising the camera to a known-aligned state. */
extern void scroll_reset(void);

#endif

/* eof */


