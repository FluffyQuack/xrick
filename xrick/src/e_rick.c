/*
 * xrick/src/e_rick.c
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

#include "e_rick.h"

#include "game.h"
#include "ents.h"
#include "sounds.h"
#include "e_bullet.h"
#include "e_bomb.h"
#include "control.h"
#include "maps.h"
#include "util.h"

#include <string.h>


/*
 * Co-op refactor: every per-Rick variable that used to be a global or
 * file-static now lives inside ricks[i]. Rick 0 corresponds to the entity
 * stored at ent_ents[1] (E_RICK_NO), preserving the original slot map.
 * Stage 1 keeps rick_count == 1 so behavior is byte-for-byte identical to
 * the single-player version; the extra slots get wired up in Stage 2/3.
 */
rick_t ricks[RICK_MAX];
U8 rick_count = 1;
U8 rick_active[RICK_MAX] = { TRUE, FALSE, FALSE, FALSE };

/*
 * Co-op (Stage 3): backing entities for Ricks 1..3. Approach (b) keeps the
 * legacy ent_ents[] slot map untouched -- Rick 0 still lives at
 * ent_ents[E_RICK_NO]; the other Ricks live here. ent_action() / ents_paintAll()
 * iterate this array via the ricks_extra_* hooks.
 */
ent_t extra_rick_ents[RICK_MAX - 1];

/* Convenience: the entity for Rick i. Rick 0 is in ent_ents, the rest here. */
#define R_ENT(i) (*( (i) == 0 ? &ent_ents[E_RICK_NO] : &extra_rick_ents[(i)-1] ))

ent_t *
ricks_get_ent(U8 i)
{
	if (i == 0) return &ent_ents[E_RICK_NO];
	return &extra_rick_ents[i - 1];
}


void
ricks_init(void)
{
	U8 i;

	memset(ricks, 0, sizeof(ricks));
	memset(extra_rick_ents, 0, sizeof(extra_rick_ents));
	rick_count = 1;
	for (i = 0; i < RICK_MAX; i++)
	{
		rick_active[i] = (i == 0) ? TRUE : FALSE;
		/*
		 * ent_slot is now informational only -- Rick 0 -> ent_ents[E_RICK_NO],
		 * Ricks 1..3 -> extra_rick_ents[i-1]. All access goes through R_ENT().
		 */
		ricks[i].ent_slot = (i == 0) ? E_RICK_NO : 0;
	}
}


/*
 * Co-op (Stage 3): place every other active Rick at anchor Rick's current
 * position with a fresh state. Used when loading / restarting a submap so
 * all Ricks spawn together (per the locked-in design in COOP_ROADMAP.md).
 *
 * For fresh map entries the anchor is Rick 0. For submap chains the anchor
 * is whichever Rick walked off the edge -- their x has already been wrapped
 * to the new submap's entry edge by e_rick_action2, so anchoring on them
 * places everyone at the correct spot in the new submap. When anchor != 0,
 * Rick 0 also gets snapped to the anchor (the camera follows Rick 0, so
 * this is what makes the camera enter the new submap).
 */
void
ricks_spawn_at(U8 anchor)
{
	U8 i;
	ent_t *src = ricks_get_ent(anchor);
	U16 ax = src->x;
	U16 ay = src->y;

	for (i = 0; i < RICK_MAX; i++)
	{
		ent_t *e;

		if (i == anchor) continue;

		e = ricks_get_ent(i);

		if (!rick_active[i])
		{
			/* Inactive Ricks must not draw or collide. */
			if (i != 0)
			{
				e->n = 0;
				e->sprite = 0;
			}
			continue;
		}

		/* Mirror the per-frame fields init() sets up for ent_ents[1]. */
		e->x = ax;
		e->y = ay;
		e->w = 0x18;
		e->h = 0x15;
		e->n = 0x01;
		e->sprite = 0x01;
		e->front = FALSE;

		/* Per-Rick simulation state -- fresh spawn, exactly like a brand-new game. */
		R_STRST(i, E_RICK_STDEAD | E_RICK_STZOMBIE | E_RICK_STCRAWL |
		           E_RICK_STJUMP | E_RICK_STCLIMB | E_RICK_STSTOP |
		           E_RICK_STSHOOT);
		ricks[i].offsx = 0;
		ricks[i].offsy = 0;
		ricks[i].ylow  = 0;
		ricks[i].seq   = 0;
		ricks[i].trigger      = FALSE;
		ricks[i].scrawl       = FALSE;
		ricks[i].atExit       = FALSE;
		ricks[i].prev_stopped = FALSE;
	}
}


/*
 * Co-op (Stage 3): translate every extra Rick's y by dy. Called from
 * scroll_up / scroll_down so Ricks 1..3 stay aligned to the world the same
 * way ent_ents[] does. We only touch active extras; inactive slots have
 * .n == 0 and are invisible anyway.
 */
void
ricks_extra_scroll(S16 dy)
{
	U8 i;
	for (i = 1; i < RICK_MAX; i++)
	{
		ent_t *e = &extra_rick_ents[i - 1];
		if (!rick_active[i] || !e->n) continue;
		e->y += dy;
		/*
		 * Mirror the "scrolled off the world" handling from scroller.c:
		 * if the Rick has been pushed off the top, hide him. We don't
		 * mark him DEAD -- this is a purely cosmetic clip, P1 can still
		 * scroll back to him.
		 */
		if (e->y & 0x8000) {
			e->n = 0;
			e->sprite = 0;
		}
		else if (e->y > 0x0140) {
			e->n = 0;
			e->sprite = 0;
		}
	}
}


/*
 * Co-op (Stage 3): clear prev_n on every extra Rick. ent_clprev() does the
 * same for ent_ents[]; calling this from there keeps the dirty-rect
 * bookkeeping in sync after a full screen redraw.
 */
void
ricks_extra_clprev(void)
{
	U8 i;
	for (i = 0; i < RICK_MAX - 1; i++)
		extra_rick_ents[i].prev_n = 0;
}


/*
 * Box test
 *
 * ASM 113E (based on)
 *
 * i: rick index to test against (corresponds to DI in asm code).
 * e: entity to test against (corresponds to SI in asm code).
 * ret: TRUE/intersect, FALSE/not.
 */
U8
e_rick_boxtest(U8 i, U8 e)
{
	ent_t *rent = &R_ENT(i);

	/*
	 * rick: x+0x05 to x+0x11, y+[0x08 if rick's crawling] to y+0x14
	 * entity: x to x+w, y to y+h
	 */

	if (rent->x + 0x11 < ent_ents[e].x ||
		rent->x + 0x05 > ent_ents[e].x + ent_ents[e].w ||
		rent->y + 0x14 < ent_ents[e].y ||
		rent->y + (R_STTST(i, E_RICK_STCRAWL) ? 0x08 : 0x00) > ent_ents[e].y + ent_ents[e].h - 1)
		return FALSE;
	else
		return TRUE;
}




/*
 * Go zombie
 *
 * ASM 1851
 */
void
e_rick_gozombie(U8 i)
{
	ent_t *rent;

	if (env_invicible) return;

	/* already zombie? */
	if (R_STTST(i, E_RICK_STZOMBIE)) return;

#ifdef ENABLE_SOUND
	syssnd_play(WAV_DIE, 1);
#endif

	rent = &R_ENT(i);
	R_STSET(i, E_RICK_STZOMBIE);
	ricks[i].offsy = -0x0400;
	ricks[i].offsx = (rent->x > 0x80 ? -3 : +3);
	ricks[i].ylow = 0;
	rent->front = TRUE;
}


/*
 * Action sub-function for e_rick when zombie
 *
 * ASM 17DC
 */
static void
e_rick_z_action(U8 i)
{
	U32 j;
	ent_t *rent = &R_ENT(i);

	/* sprite */
	rent->sprite = (rent->x & 0x04) ? 0x1A : 0x19;

	/* x */
	rent->x += ricks[i].offsx;

	/* y */
	j = (rent->y << 8) + ricks[i].offsy + ricks[i].ylow;
	rent->y = j >> 8;
	ricks[i].offsy += 0x80;
	ricks[i].ylow = j;

	/* dead when out of screen */
	if (rent->y < 0 || rent->y > 0x0140)
		R_STSET(i, E_RICK_STDEAD);
}


/*
 * Action sub-function for e_rick.
 *
 * ASM 13BE
 *
 * Co-op: every reference to state and motion bookkeeping that used to be a
 * file-static / global is now ricks[i].*. Stage 2: each Rick reads its own
 * input from control_status_p[i], cached into the local `cs` for brevity.
 */
void
e_rick_action2(U8 i)
{
	U8 env0, env1;
	U16 x, y;
	U32 j;
	ent_t *rent = &R_ENT(i);
	/*
	 * Co-op (Stage 2): read this Rick's own input slot. Snapshotted once
	 * at function entry -- matches the original single-player semantics
	 * where the global byte didn't change between event polls.
	 */
	U8 cs = control_status_p[i];

	R_STRST(i, E_RICK_STSTOP|E_RICK_STSHOOT);

	/* if zombie, run dedicated function and return */
	if (R_STTST(i, E_RICK_STZOMBIE)) {
		e_rick_z_action(i);
		return;
	}

	/* climbing? */
	if (R_STTST(i, E_RICK_STCLIMB))
		goto climbing;

	/*
	* NOT CLIMBING
	*/
	R_STRST(i, E_RICK_STJUMP);
	/* calc y */
	j = (rent->y << 8) + ricks[i].offsy + ricks[i].ylow;
	y = j >> 8;
	/* test environment */
	u_envtest(rent->x, y, R_STTST(i, E_RICK_STCRAWL), &env0, &env1);
	/* stand up, if possible */
	if (R_STTST(i, E_RICK_STCRAWL) && !env0)
		R_STRST(i, E_RICK_STCRAWL);
	/* can move vertically? */
	if (env1 & (ricks[i].offsy < 0 ?
					MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD :
					MAP_EFLG_VERT|MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP))
		goto vert_not;

	/*
	* VERTICAL MOVE
	*/
	R_STSET(i, E_RICK_STJUMP);
	/* killed? */
	if (env1 & MAP_EFLG_LETHAL) {
		e_rick_gozombie(i);
		return;
	}
	/* save */
	rent->y = y;
	ricks[i].ylow = j;
	/* climb? */
	if ((env1 & MAP_EFLG_CLIMB) &&
			(cs & (CONTROL_UP|CONTROL_DOWN))) {
		ricks[i].offsy = 0x0100;
		R_STSET(i, E_RICK_STCLIMB);
		return;
	}
	/* fall */
	ricks[i].offsy += 0x0080;
	if (ricks[i].offsy > 0x0800) {
		ricks[i].offsy = 0x0800;
		ricks[i].ylow = 0;
	}

	/*
	* HORIZONTAL MOVE
	*/
	horiz:
	/* should move? */
	if (!(cs & (CONTROL_LEFT|CONTROL_RIGHT))) {
		ricks[i].seq = 2; /* no: reset seq and return */
		return;
	}
	if (cs & CONTROL_LEFT) {  /* move left */
		ricks[i].dir = LEFT;
		if (rent->x < 2) {  /* prev submap (was: x < 0 with signed x) */
			ricks[i].atExit = TRUE;
			rent->x = 0xe2;
			return;
		}
		x = rent->x - 2;
	} else {  /* move right */
		x = rent->x + 2;
		ricks[i].dir = RIGHT;
		if (x >= 0xe8) {  /* next submap */
			ricks[i].atExit = TRUE;
			rent->x = 0x04;
			return;
		}
	}

	/* still within this map: test environment */
	u_envtest(x, rent->y, R_STTST(i, E_RICK_STCRAWL), &env0, &env1);

	/* save x-position if it is possible to move */
	if (!(env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP))) {
		rent->x = x;
		if (env1 & MAP_EFLG_LETHAL) e_rick_gozombie(i);
	}

	/* end */
	return;

  /*
   * NO VERTICAL MOVE
   */
 vert_not:
  if (ricks[i].offsy < 0) {
    /* not climbing + trying to go _up_ not possible -> hit the roof */
    R_STSET(i, E_RICK_STJUMP);  /* fall back to the ground */
    rent->y &= 0xF8;
    ricks[i].offsy = 0;
    ricks[i].ylow = 0;
    goto horiz;
  }
  /* else: not climbing + trying to go _down_ not possible -> standing */
  /* align to ground */
  rent->y &= 0xF8;
  rent->y |= 0x03;
  ricks[i].ylow = 0;

  /* standing on a super pad? */
  if ((env1 & MAP_EFLG_SPAD) && ricks[i].offsy >= 0X0200) {
    ricks[i].offsy = (cs & CONTROL_UP) ? 0xf800 : 0x00fe - ricks[i].offsy;
#ifdef ENABLE_SOUND
	syssnd_play(WAV_PAD, 1);
#endif
    goto horiz;
  }

  ricks[i].offsy = 0x0100;  /* reset*/

  /* standing. firing ? */
  if (ricks[i].scrawl || !(cs & CONTROL_FIRE))
    goto firing_not;

  /*
   * FIRING
   */
	if (cs & (CONTROL_LEFT|CONTROL_RIGHT)) {  /* stop */
		if (cs & CONTROL_RIGHT)
		{
			ricks[i].dir = RIGHT;
			ricks[i].stop_x = rent->x + 0x17;
		} else {
			ricks[i].dir = LEFT;
			ricks[i].stop_x = rent->x;
		}
		ricks[i].stop_y = rent->y + 0x000E;
		R_STSET(i, E_RICK_STSTOP);
		return;
	}

  if (cs == (CONTROL_FIRE|CONTROL_UP)) {  /* bullet */
    R_STSET(i, E_RICK_STSHOOT);
    /* not an automatic gun: shoot once only */
    if (ricks[i].trigger)
      return;
    else
      ricks[i].trigger = TRUE;
    /* already this Rick's bullet in the air ... that's enough */
    if (bullets_get_ent(i)->n)
      return;
    /* else use a bullet, if any available (ammo is shared) */
    if (!env_bullets)
      return;
    if (!env_trainer)
      env_bullets--;

    /* initialize bullet */
    e_bullet_init(rent->x, rent->y, ricks[i].dir, i);
    return;
  }

  ricks[i].trigger = FALSE; /* not shooting means trigger is released */
  ricks[i].seq = 0; /* reset */

  if (cs == (CONTROL_FIRE|CONTROL_DOWN)) {  /* bomb */
    /* already a bomb ticking for this Rick ... that's enough */
    if (bombs_get_ent(i)->n)
      return;
    /* else use a bomb, if any available */
    if (!env_bombs)
      return;
    if (!env_trainer)
      env_bombs--;

    /* initialize bomb */
    e_bomb_init(rent->x, rent->y, i);
    return;
  }

  return;

  /*
   * NOT FIRING
   */
 firing_not:
  if (cs & CONTROL_UP) {  /* jump or climb */
    if (env1 & MAP_EFLG_CLIMB) {  /* climb */
      R_STSET(i, E_RICK_STCLIMB);
      return;
    }
    ricks[i].offsy = -0x0580;  /* jump */
    ricks[i].ylow = 0;
#ifdef ENABLE_SOUND
    syssnd_play(WAV_JUMP, 1);
#endif
    goto horiz;
  }
  if (cs & CONTROL_DOWN) {  /* crawl or climb */
    if ((env1 & MAP_EFLG_VERT) &&  /* can go down */
	!(cs & (CONTROL_LEFT|CONTROL_RIGHT)) &&  /* + not moving horizontaly */
	(rent->x & 0x1f) < 0x0a) {  /* + aligned -> climb */
      rent->x &= 0xf0;
      rent->x |= 0x04;
      R_STSET(i, E_RICK_STCLIMB);
    }
    else {  /* crawl */
      R_STSET(i, E_RICK_STCRAWL);
      goto horiz;
    }

  }
  goto horiz;

	/*
	* CLIMBING
	*/
	climbing:
		/* should move? */
		if (!(cs & (CONTROL_UP|CONTROL_DOWN|CONTROL_LEFT|CONTROL_RIGHT))) {
			ricks[i].seq = 0; /* no: reset seq and return */
			return;
		}

		if (cs & (CONTROL_UP|CONTROL_DOWN)) {
			/* up-down: calc new y and test environment */
			y = rent->y + ((cs & CONTROL_UP) ? -0x02 : 0x02);
			u_envtest(rent->x, y, R_STTST(i, E_RICK_STCRAWL), &env0, &env1);
			if (env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP) &&
					!(cs & CONTROL_UP)) {
				/* FIXME what? */
				R_STRST(i, E_RICK_STCLIMB);
				return;
			}
			if (!(env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP)) ||
					(env1 & MAP_EFLG_WAYUP)) {
				/* ok to move, save */
				rent->y = y;
				if (env1 & MAP_EFLG_LETHAL) {
					e_rick_gozombie(i);
					return;
				}
				if (!(env1 & (MAP_EFLG_VERT|MAP_EFLG_CLIMB))) {
					/* reached end of climb zone */
					ricks[i].offsy = (cs & CONTROL_UP) ? -0x0300 : 0x0100;
#ifdef ENABLE_SOUND
					if (cs & CONTROL_UP)
						syssnd_play(WAV_JUMP, 1);
#endif
					R_STRST(i, E_RICK_STCLIMB);
					return;
				}
			}
		}
  if (cs & (CONTROL_LEFT|CONTROL_RIGHT)) {
    /* left-right: calc new x and test environment */
    if (cs & CONTROL_LEFT) {
      if (rent->x < 2) {  /* prev submap (was: x < 0 with signed x) */
	ricks[i].atExit = TRUE;
	/*6dbd = 0x00;*/
	rent->x = 0xe2;
	return;
      }
      x = rent->x - 0x02;
    }
    else {
      x = rent->x + 0x02;
      if (x >= 0xe8) {  /* next submap */
	ricks[i].atExit = TRUE;
	/*6dbd = 0x01;*/
	rent->x = 0x04;
	return;
      }
    }
    u_envtest(x, rent->y, R_STTST(i, E_RICK_STCRAWL), &env0, &env1);
    if (env1 & (MAP_EFLG_SOLID|MAP_EFLG_SPAD)) return;
    rent->x = x;
    if (env1 & MAP_EFLG_LETHAL) {
      e_rick_gozombie(i);
      return;
    }

    if (env1 & (MAP_EFLG_VERT|MAP_EFLG_CLIMB)) return;
    R_STRST(i, E_RICK_STCLIMB);
    if (cs & CONTROL_UP)
      ricks[i].offsy = -0x0300;
  }
}


/*
 * Per-Rick tick: action + post-action sprite selection.
 *
 * Co-op (Stage 3): factored out of e_rick_action so we can re-use it for
 * Ricks 1..3 (which live in extra_rick_ents and are NOT iterated by the
 * ent_action() main loop).
 *
 * Dead Ricks early-return with sprite cleared so they stop rendering and
 * stop colliding -- a dead Rick stays dead until the last surviving Rick
 * also dies (then CTRL_RICK in game.c restarts the submap).
 */
static void
e_rick_tick(U8 i)
{
	ent_t *rent;

	if (R_STTST(i, E_RICK_STDEAD))
	{
		R_ENT(i).sprite = 0;
		R_ENT(i).n = 0;  /* don't draw, don't collide */
		return;
	}

	e_rick_action2(i);

	ricks[i].scrawl = R_STTST(i, E_RICK_STCRAWL) ? TRUE : FALSE;

	/*
	 * If the action just transitioned the Rick to fully dead (zombie
	 * fell off the screen), hide him this frame too.
	 */
	if (R_STTST(i, E_RICK_STDEAD))
	{
		R_ENT(i).sprite = 0;
		R_ENT(i).n = 0;
		return;
	}

	if (R_STTST(i, E_RICK_STZOMBIE))
		return;

	rent = &R_ENT(i);

	/*
	 * set sprite
	 */

	if (R_STTST(i, E_RICK_STSTOP)) {
		rent->sprite = (ricks[i].dir ? 0x17 : 0x0B);
#ifdef ENABLE_SOUND
		if (!ricks[i].prev_stopped)
		{
			syssnd_play(WAV_STICK, 1);
			ricks[i].prev_stopped = TRUE;
		}
#endif
		return;
	}

	ricks[i].prev_stopped = FALSE;

	if (R_STTST(i, E_RICK_STSHOOT)) {
		rent->sprite = (ricks[i].dir ? 0x16 : 0x0A);
		return;
	}

	if (R_STTST(i, E_RICK_STCLIMB)) {
		rent->sprite = (((rent->x ^ rent->y) & 0x04) ? 0x18 : 0x0c);
#ifdef ENABLE_SOUND
		ricks[i].seq = (ricks[i].seq + 1) & 0x03;
		if (ricks[i].seq == 0) syssnd_play(WAV_WALK, 1);
#endif
		return;
	}

	if (R_STTST(i, E_RICK_STCRAWL))
	{
		rent->sprite = (ricks[i].dir ? 0x13 : 0x07);
		if (rent->x & 0x04) rent->sprite++;
#ifdef ENABLE_SOUND
		ricks[i].seq = (ricks[i].seq + 1) & 0x03;
		if (ricks[i].seq == 0) syssnd_play(WAV_CRAWL, 1);
#endif
		return;
	}

	if (R_STTST(i, E_RICK_STJUMP))
	{
		rent->sprite = (ricks[i].dir ? 0x15 : 0x06);
		return;
	}

	ricks[i].seq++;

	if (ricks[i].seq >= 0x14)
	{
#ifdef ENABLE_SOUND
		syssnd_play(WAV_WALK, 1);
#endif
		ricks[i].seq = 0x04;
	}
#ifdef ENABLE_SOUND
  else
  if (ricks[i].seq == 0x0C)
    syssnd_play(WAV_WALK, 1);
#endif

  rent->sprite = (ricks[i].seq >> 2) + 1 + (ricks[i].dir ? 0x0c : 0x00);
}


/*
 * Action function for e_rick (entity-action callback for slot E_RICK_NO).
 *
 * Co-op (Stage 3): only Rick 0 lives in ent_ents[]; Ricks 1..3 live in
 * extra_rick_ents and are ticked by ricks_extra_action() (called from
 * ent_action() right after the main entity loop). Keeping this wrapper
 * means the actf table dispatch is unchanged.
 */
void e_rick_action(UNUSED(U8 e))
{
	if (!rick_active[0]) return;
	e_rick_tick(0);
}


/*
 * Co-op (Stage 3): tick every active extra Rick.
 *
 * Called from ent_action() AFTER the main loop (which already ticked Rick 0
 * and the enemies/bullets/etc.). Order matters: roadmap 3.1 says "P1 first"
 * to preserve scroll/camera behavior, and enemies must read Rick positions
 * from this frame before the extras move next frame.
 */
void ricks_extra_action(void)
{
	U8 i;
	for (i = 1; i < RICK_MAX; i++)
		if (rick_active[i])
			e_rick_tick(i);
}


/*
 * Save status
 *
 * ASM part of 0x0BBB
 */
void e_rick_save(U8 i)
{
	ent_t *rent = &R_ENT(i);
	ricks[i].save_x = rent->x;
	ricks[i].save_y = rent->y;
	ricks[i].save_crawl = R_STTST(i, E_RICK_STCRAWL) ? TRUE : FALSE;
	/* FIXME
	 * save_C0 = rent->b0C;
	 * plus some 6DBC stuff?
	 */
}


/*
 * Restore status
 *
 * ASM part of 0x0BDC
 */
void e_rick_restore(U8 i)
{
	ent_t *rent = &R_ENT(i);
	rent->x = ricks[i].save_x;
	rent->y = ricks[i].save_y;
	rent->front = FALSE;
	/*
	 * Co-op (Stage 3): re-arm the extra Rick's entity in case it was
	 * cleared by Stage 3 death-handling (sprite=0, n=0). Rick 0's entity
	 * is already kept armed by game.c init(); doing it for everyone is a
	 * no-op for Rick 0 and the right thing for Ricks 1..3.
	 */
	rent->n = 0x01;
	rent->sprite = 0x01;
	rent->w = 0x18;
	rent->h = 0x15;
	if (ricks[i].save_crawl)
		R_STSET(i, E_RICK_STCRAWL);
	else
		R_STRST(i, E_RICK_STCRAWL);
	/* FIXME
	 * rent->b0C = save_C0;
	 * plus some 6DBC stuff?
	 */
}




/* eof */
