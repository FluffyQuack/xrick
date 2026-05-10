/*
 * xrick/include/e_bullet.h
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

#ifndef _E_BULLET_H
#define _E_BULLET_H

#include "system.h"
#include "ents.h"   /* ent_t */
#include "e_rick.h" /* RICK_MAX */

/*
 * Co-op: one bullet per Rick. Bullet 0 stays in ent_ents[E_BULLET_NO] so the
 * existing ent_action() dispatch table (slot 2 -> e_bullet_action) keeps
 * ticking it. Bullets 1..3 live in extra_bullet_ents[] and are ticked /
 * painted / scrolled via the bullets_extra_* hooks (same pattern as the
 * extra Ricks).
 *
 * Per-bullet horizontal velocity is stored in the entity's c1 field (which
 * the bullet code never used otherwise); it replaces the old singleton
 * e_bullet_offsx global. Bullet center coords are derived on the fly at
 * collision sites: xc = x + 0x0c, yc = y + 0x05.
 */
#define E_BULLET_NO 2
#define E_BULLET_ENT ent_ents[E_BULLET_NO]

extern ent_t extra_bullet_ents[RICK_MAX - 1];

extern ent_t *bullets_get_ent(U8 i);
extern S8     bullets_get_offsx(U8 i);

extern void e_bullet_init(U16 x, U16 y, U8 dir, U8 owner);
extern void e_bullet_action(U8 e);  /* called from ent_actf[] for Bullet 0 */

extern void bullets_extra_action(void);
extern void bullets_extra_clprev(void);
extern void bullets_extra_scroll(S16 dy);

#endif

/* eof */
