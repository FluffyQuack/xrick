/*
 * xrick/src/e_bullet.c
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
#include "game.h"
#include "ents.h"
#include "sounds.h"
#include "e_bullet.h"
#include "e_rick.h"
#include "sysvid.h"

#include "maps.h"

/*
 * Backing storage for Bullets 1..3 (Bullet 0 lives at ent_ents[E_BULLET_NO]).
 * Same pattern as extra_rick_ents.
 */
ent_t extra_bullet_ents[RICK_MAX - 1];

ent_t *
bullets_get_ent(U8 i)
{
  if (i == 0) return &ent_ents[E_BULLET_NO];
  return &extra_bullet_ents[i - 1];
}

S8
bullets_get_offsx(U8 i)
{
  return (S8)bullets_get_ent(i)->c1;
}

/*
 * Step a single bullet: move it, deactivate if it leaves the screen,
 * hits a solid map tile, or strikes another Rick. Shared by Bullet 0
 * (slot 2) and the extras. `owner` is the firing Rick's slot index so
 * the bullet doesn't kill its own shooter.
 */
static void
bullet_step(ent_t *b, U8 owner)
{
  U16 xc, yc, tip;
  S8 offsx = (S8)b->c1;
  U8 r;

  b->x += offsx;

  if (b->x <= -0x10 || b->x > 0xe8) {
    b->n = 0;
    return;
  }

  xc = b->x + 0x0c;
  yc = b->y + 0x05;
  if (map_eflg[map_map[yc >> 3][xc >> 3]] & MAP_EFLG_SOLID) {
    b->n = 0;
    return;
  }

  /* Bullet vs. other Ricks: same "tip" point used against enemies. */
  tip = b->x + (offsx < 0 ? 0 : 0x18);
  for (r = 0; r < RICK_MAX; r++) {
    ent_t *rent;
    if (r == owner) continue;
    if (!rick_active[r]) continue;
    if (R_STTST(r, E_RICK_STDEAD | E_RICK_STZOMBIE)) continue;
    rent = ricks_get_ent(r);
    if (rent->x < tip && tip <= rent->x + rent->w &&
        rent->y < b->y && b->y <= rent->y + rent->h) {
      b->n = 0;
      e_rick_gozombie(r);
      return;
    }
  }
}

/*
 * Initialize bullet for the given owner Rick.
 */
void
e_bullet_init(U16 x, U16 y, U8 dir, U8 owner)
{
  ent_t *b = bullets_get_ent(owner);

  b->n = 0x02;
  b->x = x;
  b->y = y + 0x0006;
  if (dir == LEFT) {
    b->c1 = -0x08;
    b->sprite = 0x21;
  }
  else {
    b->c1 = 0x08;
    b->sprite = 0x20;
  }
  /* Spawn mid-tick: the start-of-tick snapshot captured stale (or zero)
   * tick_prev_* for this slot. Anchor it to the spawn position so later
   * sub-tick renders interpolate from the spawn point, not from garbage. */
  b->tick_prev_x = b->x;
  b->tick_prev_y = b->y;
#ifdef ENABLE_SOUND
  syssnd_play(WAV_BULLET, 1);
#endif
}


/*
 * Entity action for Bullet 0 (called from ent_actf[2] by ent_action()).
 *
 * ASM 1883, 0F97
 */
void
e_bullet_action(UNUSED(U8 e))
{
  bullet_step(&ent_ents[E_BULLET_NO], 0);
}


/*
 * Co-op: tick Bullets 1..3. Called from ent_action() after the main entity
 * loop (mirrors ricks_extra_action).
 */
void
bullets_extra_action(void)
{
  U8 i;
  for (i = 0; i < RICK_MAX - 1; i++) {
    if (extra_bullet_ents[i].n)
      bullet_step(&extra_bullet_ents[i], i + 1);
  }
}


/*
 * Co-op: clear prev_n for extra bullets (mirrors ricks_extra_clprev).
 */
void
bullets_extra_clprev(void)
{
  U8 i;
  for (i = 0; i < RICK_MAX - 1; i++) {
    extra_bullet_ents[i].prev_n = 0;
    /* See ent_clprev: anchor tick_prev_* to current pos. */
    extra_bullet_ents[i].tick_prev_x = extra_bullet_ents[i].x;
    extra_bullet_ents[i].tick_prev_y = extra_bullet_ents[i].y;
  }
}


/*
 * Co-op: translate extra bullets in y when the world scrolls. Same
 * "off the world -> hide" treatment scroller.c applies to ent_ents[].
 */
void
bullets_extra_scroll(S16 dy)
{
  U8 i;
  for (i = 0; i < RICK_MAX - 1; i++) {
    ent_t *b = &extra_bullet_ents[i];
    if (!b->n) continue;
    b->y += dy;
    if (b->y & 0x8000) {
      b->n = 0;
    }
    else if (b->y > 0x0140) {
      b->n = 0;
    }
  }
}


/* eof */
