/*
 * xrick/include/syskbd.h
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

#ifndef _SYSKBD_H
#define _SYSKBD_H

#include "system.h"
#include "control.h"  /* CONTROL_PLAYERS */

/*
 * Co-op (Stage 2): per-player movement+fire scancodes. The legacy single-
 * player Z/X/K/O bindings are gone. Slot 0 is P1.
 *
 * P1: arrow keys + Space.
 * P2: WASD + Left Shift.
 * P3: IJKL + Return.
 * P4: 0 -- no controls reserved (slot is still rendered/simulated).
 *
 * Pause / end-game / exit / fullscreen / volume / cheat keys remain global
 * (single binding) and live as separate scalars below.
 */
typedef struct {
	U8 up, down, left, right, fire;
} player_kbd_t;

extern player_kbd_t syskbd_players[CONTROL_PLAYERS];

extern U8 syskbd_pause;
extern U8 syskbd_end;
extern U8 syskbd_xtra;

#endif /* _SYSKBD_H */

/* eof */
