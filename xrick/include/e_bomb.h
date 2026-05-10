/*
 * xrick/include/e_bomb.h
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

#ifndef _E_BOMB_H
#define _E_BOMB_H

#include "system.h"
#include "ents.h"   /* ent_t */
#include "e_rick.h" /* RICK_MAX */

/*
 * Co-op: one bomb per Rick. Bomb 0 stays at ent_ents[E_BOMB_NO] so the
 * existing ent_action() dispatch table (slot n==3 -> e_bomb_action) keeps
 * ticking it. Bombs 1..3 live in extra_bomb_ents[] and are ticked /
 * painted / scrolled via the bombs_extra_* hooks (same pattern as extra
 * Ricks and extra bullets).
 *
 * Per-bomb fuse ticker is stored in the entity's c1 field (the bomb code
 * never used c1 otherwise); it replaces the old singleton e_bomb_ticker
 * global. The lethal flag and explosion-center coords are derived on the
 * fly from the ticker / x / y at collision sites, which is what the old
 * e_bomb_lethal / e_bomb_xc / e_bomb_yc globals cached.
 */
#define E_BOMB_NO 3
#define E_BOMB_ENT ent_ents[E_BOMB_NO]
#define E_BOMB_TICKER (0x2D)

extern ent_t extra_bomb_ents[RICK_MAX - 1];

extern ent_t *bombs_get_ent(U8 i);
extern U8     bombs_is_lethal(U8 i);

extern void e_bomb_init(U16 x, U16 y, U8 owner);
extern void e_bomb_action(U8 e);  /* called from ent_actf[] for Bomb 0 */

/* lethal-bomb-vs-target tests that iterate all RICK_MAX bombs */
extern U8 bombs_any_hit(U8 e);   /* bomb explosion hits ent_ents[e] */
extern U8 bombs_any_trig(U8 e);  /* bomb explosion hits ent_ents[e] trigger box */

extern void bombs_extra_action(void);
extern void bombs_extra_clprev(void);
extern void bombs_extra_scroll(S16 dy);

#endif

/* eof */
