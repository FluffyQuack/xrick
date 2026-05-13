/*
 * xrick/src/e_bomb.c
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



#include "system.h"
#include "config.h"
#include "env.h"

#include "ents.h"
#include "sounds.h"
#include "e_bomb.h"
#include "e_rick.h"
#include "util.h"

/* fixme is this for sounds only? */
#include "game.h"


/*
 * Backing storage for Bombs 1..3 (Bomb 0 lives at ent_ents[E_BOMB_NO]).
 * Same pattern as extra_rick_ents / extra_bullet_ents.
 */
ent_t extra_bomb_ents[RICK_MAX - 1];

ent_t *
bombs_get_ent(U8 i)
{
	if (i == 0) return &ent_ents[E_BOMB_NO];
	return &extra_bomb_ents[i - 1];
}

/*
 * A bomb is lethal during ticks 0x01..0x09 (the "exploding" window).
 * Outside that range it's either still fizzing or already gone.
 */
U8
bombs_is_lethal(U8 i)
{
	ent_t *b = bombs_get_ent(i);
	U8 t;
	if (!b->n) return FALSE;
	t = (U8)b->c1;
	return (t > 0 && t < 0x0A) ? TRUE : FALSE;
}

/*
 * Bomb-vs-target hit test. Original e_bomb_hit() compared ent_ents[e]
 * against the singleton E_BOMB_ENT; this version takes both pointers so
 * the same comparison works for any of the RICK_MAX bombs and against
 * Ricks 1..3 (which don't live in ent_ents[]).
 */
static U8
bomb_hit_box(ent_t *bomb, ent_t *target)
{
	if (target->x > (bomb->x >= 0xE0 ? 0xFF : bomb->x + 0x20))
		return FALSE;
	if (target->x + target->w < (bomb->x > 0x04 ? bomb->x - 0x04 : 0))
		return FALSE;
	if (target->y > (bomb->y + 0x1D))
		return FALSE;
	if (target->y + target->h < (bomb->y > 0x0004 ? bomb->y - 0x0004 : 0))
		return FALSE;
	return TRUE;
}

U8
bombs_any_hit(U8 e)
{
	U8 i;
	for (i = 0; i < RICK_MAX; i++) {
		if (!bombs_is_lethal(i)) continue;
		if (bomb_hit_box(bombs_get_ent(i), &ent_ents[e]))
			return TRUE;
	}
	return FALSE;
}

U8
bombs_any_trig(U8 e)
{
	U8 i;
	for (i = 0; i < RICK_MAX; i++) {
		ent_t *b;
		if (!bombs_is_lethal(i)) continue;
		b = bombs_get_ent(i);
		if (u_trigbox(e, b->x + 0x0C, b->y + 0x000A))
			return TRUE;
	}
	return FALSE;
}

/*
 * Test this specific bomb against every active, alive Rick.
 */
static void
bomb_kill_ricks(ent_t *bomb)
{
	U8 r;
	for (r = 0; r < RICK_MAX; r++) {
		if (!rick_active[r]) continue;
		if (R_STTST(r, E_RICK_STDEAD | E_RICK_STZOMBIE)) continue;
		if (bomb_hit_box(bomb, ricks_get_ent(r)))
			e_rick_gozombie(r);
	}
}

/*
 * Initialize bomb for the given owner Rick.
 */
void
e_bomb_init(U16 x, U16 y, U8 owner)
{
	ent_t *b = bombs_get_ent(owner);

	b->n = 0x03;
	b->x = x;
	b->y = y;
	b->c1 = E_BOMB_TICKER;

	/*
	 * Atari ST dynamite sprites are not centered the
	 * way IBM PC sprites were ... need to adjust things a little bit
	 */
#ifdef GFXST
	b->x += 4;
	b->y += 5;
#endif
	/* Spawn mid-tick: anchor tick_prev_* to the (post-ST adjustment) spawn
	 * position so sub-tick renders don't lerp from the stale snapshot. */
	b->tick_prev_x = b->x;
	b->tick_prev_y = b->y;
}


/*
 * Step a single bomb through one tick. Shared by Bomb 0 (slot 3) and
 * the extras.
 *
 * ASM 18CA
 */
static void
bomb_step(ent_t *b)
{
	U8 ticker;

	/* tick */
	b->c1--;
	ticker = (U8)b->c1;

	if (ticker == 0)
	{
		/*
		 * end: deactivate
		 */
		b->n = 0;
	}
	else if (ticker >= 0x0A)
	{
		/*
		 * ticking
		 */
#ifdef ENABLE_SOUND
		if ((ticker & 0x03) == 0x02)
			syssnd_play(WAV_BOMBSHHT, 1);
#endif
#ifdef GFXST
		/* ST bomb sprites sequence is longer */
		if (ticker < 40)
			b->sprite = 0x99 + 19 - (ticker >> 1);
		else
#endif
		b->sprite = (ticker & 0x01) ? 0x23 : 0x22;
	}
	else if (ticker == 0x09)
	{
		/*
		 * explode
		 */
#ifdef ENABLE_SOUND
		syssnd_play(WAV_EXPLODE, 1);
#endif
#ifdef GFXPC
		b->sprite = 0x24 + 4 - (ticker >> 1);
#endif
#ifdef GFXST
		/* See above: fixing alignment */
		b->x -= 4;
		b->y -= 5;
		b->sprite = 0xa8 + 4 - (ticker >> 1);
#endif
		bomb_kill_ricks(b);
	}
	else
	{
		/*
		 * exploding
		 */
#ifdef GFXPC
		b->sprite = 0x24 + 4 - (ticker >> 1);
#endif
#ifdef GFXST
		b->sprite = 0xa8 + 4 - (ticker >> 1);
#endif
		/* exploding, hence lethal */
		bomb_kill_ricks(b);
	}
}

/*
 * Entity action for Bomb 0 (called from ent_actf[3] by ent_action()).
 */
void
e_bomb_action(UNUSED(U8 e))
{
	bomb_step(&ent_ents[E_BOMB_NO]);
}

/*
 * Co-op: tick Bombs 1..3. Called from ent_action() after the main entity
 * loop (mirrors ricks_extra_action / bullets_extra_action).
 */
void
bombs_extra_action(void)
{
	U8 i;
	for (i = 0; i < RICK_MAX - 1; i++) {
		if (extra_bomb_ents[i].n)
			bomb_step(&extra_bomb_ents[i]);
	}
}

/*
 * Co-op: clear prev_n for extra bombs.
 */
void
bombs_extra_clprev(void)
{
	U8 i;
	for (i = 0; i < RICK_MAX - 1; i++) {
		extra_bomb_ents[i].prev_n = 0;
		/* See ent_clprev: anchor tick_prev_* to current pos. */
		extra_bomb_ents[i].tick_prev_x = extra_bomb_ents[i].x;
		extra_bomb_ents[i].tick_prev_y = extra_bomb_ents[i].y;
	}
}

/*
 * Co-op: translate extra bombs in y when the world scrolls. Same
 * "off the world -> hide" treatment scroller.c applies to ent_ents[].
 */
void
bombs_extra_scroll(S16 dy)
{
	U8 i;
	for (i = 0; i < RICK_MAX - 1; i++) {
		ent_t *b = &extra_bomb_ents[i];
		if (!b->n) continue;
		b->y += dy;
		/* See ricks_extra_scroll: tick_prev_y must shift with y so
		 * the render-time interp stays in a single coordinate system. */
		b->tick_prev_y += dy;
		if (b->y & 0x8000) {
			b->n = 0;
		}
		else if (b->y > 0x0140) {
			b->n = 0;
		}
	}
}

/* eof */
