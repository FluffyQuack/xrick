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

#define SCROLL_RUNNING 1
#define SCROLL_DONE 0

#define SCROLL_PERIOD 24

extern U8 scroll_up(void);
extern U8 scroll_down(void);

/*
 * Real-time scroll: perform all 8 row-shifts atomically in one call.
 * Snapshots the pre-shift playfield once, shifts map+entities 8x,
 * fires ent_actvis + map_expand at the end, repaints. Gameplay returns
 * to CTRL_ACTION on the next tick instead of stalling for 8 ticks.
 * The visual slide is handled by game.c's catch-up state.
 */
extern void scroll_up_atomic(void);
extern void scroll_down_atomic(void);

#endif

/* eof */


