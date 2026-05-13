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
 * Realtime scroll (Approach A): batch-of-8 shifts driven one shift per
 * tick, with gameplay running on the same ticks. Request once when the
 * camera target leaves the deadzone; the batch then ticks itself to
 * completion. New requests are ignored while a batch is in flight.
 *
 * dir is the sign of the desired scroll: positive scrolls up
 * (world shifts up, camera moves down through the map), negative
 * scrolls down.
 */
extern void scroll_realtime_request(S8 dir);
extern void scroll_realtime_tick(void);
extern U8   scroll_realtime_in_progress(void);
extern void scroll_realtime_reset(void);

#endif

/* eof */


