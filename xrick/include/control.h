/*
 * xrick/include/control.h
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

#ifndef _CONTROL_H
#define _CONTROL_H

#include "system.h"

#define CONTROL_UP 0x08
#define CONTROL_DOWN 0x04
#define CONTROL_LEFT 0x02
#define CONTROL_RIGHT 0x01
#define CONTROL_PAUSE 0x80
#define CONTROL_END 0x40
#define CONTROL_EXIT 0x20
#define CONTROL_FIRE 0x10

/*
 * Co-op (Stage 2): per-player input. Each player owns one byte of
 * CONTROL_* bits. Player 0 is P1 (camera follower). Defined here
 * (not in e_rick.h) so it stays a hard-coded constant -- control.h is
 * included almost everywhere and we don't want a cycle through e_rick.h.
 * Must equal RICK_MAX in e_rick.h; a static_assert isn't worth the C89
 * gymnastics, so just keep them in sync if either ever moves.
 */
#define CONTROL_PLAYERS 8

extern U8 control_status_p[CONTROL_PLAYERS];

/*
 * Legacy alias. The vast majority of call sites read "the player's input"
 * meaning P1's input -- menus, screens, pause/exit handling, devtools, the
 * cheat keys, etc. Keeping `control_status` as a macro for P1 means none of
 * those have to change. Per-player gameplay code (e_rick_action2) reads
 * control_status_p[i] directly.
 */
#define control_status control_status_p[0]

extern U8 control_last;
extern U8 control_active;

#endif

/* eof */


