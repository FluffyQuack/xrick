/*
 * xrick/src/scroller.c
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

#include <stdlib.h>

#include "system.h"
#include "game.h"
#include "env.h"

#include "scroller.h"

#include "debug.h"
#include "draw.h"
#include "maps.h"
#include "ents.h"
#include "e_rick.h"
#include "e_bullet.h"
#include "e_bomb.h"
#include "sysvid.h"

static U8 period;

/* Net row-shift accumulator for realtime mode. +1 per up-shift, -1 per
 * down-shift. When |accum| hits 8 we've crossed a block boundary in the
 * current direction and need to refresh the off-screen hardbuffers via
 * map_expand + ent_actvis -- the same trigger the legacy 8-tick batch
 * uses at n==7. Reset by scroll_reset() on submap entry. */
static S8 rt_accum = 0;

/*
 * Shift the world up by one row.
 *
 * Mutates map_map (rows MAP_ROW_SCRTOP..MAP_ROW_HBBOT-1 take their values
 * from one row below), translates every entity by y -= 8 (retiring any
 * that fall off the top of the world), keeps co-op extras in step, and
 * advances map_frow by 1. Sets game_scroll_step = +8 and snapshots the
 * pre-shift framebuffer so the render path can lerp.
 */
static void
shift_up_one_row(void)
{
  U8 i, j;

  /* Camera interp: world is about to shift up by 8. Renders during this
   * tick will lag the camera by (1 - alpha) * 8 so the view glides up. */
  game_scroll_step = 8;

  /* Snapshot fb pre-shift so the render path can lerp the playfield
   * between OLD and NEW pixels (and not bleed HUD/black into the 8 px
   * uncovered by the camera each tick). Must run before maps_paint. */
  sysvid_snapshot_playfield();

  /* translate map */
  for (i = MAP_ROW_SCRTOP; i < MAP_ROW_HBBOT; i++)
    for (j = 0x00; j < 0x20; j++)
      map_map[i][j] = map_map[i + 1][j];

  /* translate entities */
  for (i = 0; ent_ents[i].n != 0xFF; i++) {
    if (ent_ents[i].n) {
      ent_ents[i].ysave -= 8;
      ent_ents[i].trig_y -= 8;
      ent_ents[i].y -= 8;
      if (ent_ents[i].y & 0x8000) {  /* map coord. from 0x0000 to 0x0140 */
	IFDEBUG_SCROLLER(
	  sys_printf("xrick/scroller: entity %#04X is gone\n", i);
	  );
	ent_ents[i].n = 0;
      }
    }
  }
  /* Co-op (Stage 3): keep extras in step with the world. */
  ricks_extra_scroll(-8);
  bullets_extra_scroll(-8);
  bombs_extra_scroll(-8);
  /* Co-op: any Rick the scroll pushed off the world dies. */
  ricks_kill_oob();

  /* display */
  maps_paint();
  ents_paintAll();
  env_paintGame();
  map_frow++;
}

/*
 * Shift the world down by one row. Mirror of shift_up_one_row.
 */
static void
shift_down_one_row(void)
{
  U8 i, j;

  /* Camera interp: world is about to shift down by 8. */
  game_scroll_step = -8;

  /* See shift_up_one_row: capture pre-shift fb for the smooth-scroll lerp. */
  sysvid_snapshot_playfield();

  /* translate map */
  for (i = MAP_ROW_SCRBOT; i > MAP_ROW_HTTOP; i--)
    for (j = 0x00; j < 0x20; j++)
      map_map[i][j] = map_map[i - 1][j];

  /* translate entities */
  for (i = 0; ent_ents[i].n != 0xFF; i++) {
    if (ent_ents[i].n) {
      ent_ents[i].ysave += 8;
      ent_ents[i].trig_y += 8;
      ent_ents[i].y += 8;
      if (ent_ents[i].y > 0x0140) {  /* map coord. from 0x0000 to 0x0140 */
	IFDEBUG_SCROLLER(
	  sys_printf("xrick/scroller: entity %#04X is gone\n", i);
	  );
	ent_ents[i].n = 0;
      }
    }
  }
  /* Co-op (Stage 3): keep extras in step with the world. */
  ricks_extra_scroll(+8);
  bullets_extra_scroll(+8);
  bombs_extra_scroll(+8);
  /* Co-op: any Rick the scroll pushed off the world dies. */
  ricks_kill_oob();

  /* display */
  maps_paint();
  ents_paintAll();
  env_paintGame();
  map_frow--;
}

/*
 * Block-boundary work for an upward scroll: activate entities entering
 * the newly-exposed band at the bottom and refill all hardbuffer rows
 * from map data via map_expand. Called once every 8 up-shifts.
 */
static void
boundary_up(void)
{
  ent_actvis(map_frow + MAP_ROW_HBTOP, map_frow + MAP_ROW_HBBOT);
  map_expand();
  maps_paint();
  ents_paintAll();
  env_paintGame();
}

/*
 * Block-boundary work for a downward scroll. Mirror of boundary_up,
 * activating entities in the band entering from the top.
 */
static void
boundary_down(void)
{
  ent_actvis(map_frow + MAP_ROW_HTTOP, map_frow + MAP_ROW_HTBOT);
  map_expand();
  maps_paint();
  ents_paintAll();
  env_paintGame();
}

/*
 * Scroll up
 *
 */
U8
scroll_up(void)
{
  static U8 n = 0;

  /* last call: restore */
  if (n == 8) {
    n = 0;
    game_period = period;
    game_scroll_step = 0;  /* camera back to its rest position */
    return SCROLL_DONE;
  }

  /* first call: prepare */
  if (n == 0) {
    period = game_period;
    game_period = SCROLL_PERIOD;
  }

  shift_up_one_row();

  /* loop */
  if (n++ == 7) {
    boundary_up();
  }

  game_rects = &draw_SCREENRECT;

  return SCROLL_RUNNING;
}

/*
 * Scroll down
 *
 */
U8
scroll_down(void)
{
  static U8 n = 0;

  /* last call: restore */
  if (n == 8) {
    n = 0;
    game_period = period;
    game_scroll_step = 0;  /* camera back to its rest position */
    return SCROLL_DONE;
  }

  /* first call: prepare */
  if (n == 0) {
    period = game_period;
    game_period = SCROLL_PERIOD;
  }

  shift_down_one_row();

  /* loop */
  if (n++ == 7) {
    boundary_down();
  }

  game_rects = &draw_SCREENRECT;

  return SCROLL_RUNNING;
}

/*
 * Realtime follow-cam: do at most one row-shift per tick, then let the
 * caller continue with normal CTRL_ACTION the same tick. dir is +1
 * (camera follows Rick up), -1 (down), or 0 (no shift this tick).
 *
 * The accumulator tracks net displacement since the last block-boundary
 * crossing so map_expand / ent_actvis fire on the same 8-row cadence as
 * the legacy batch. A direction reversal that brings the accumulator
 * back toward 0 does NOT trigger a boundary -- shifting back into a
 * region whose hardbuffer was never refilled is the desired behaviour
 * (the data is still there from the original expand at this position).
 */
void
scroll_realtime_step(S8 dir)
{
  if (dir == 0) {
    game_scroll_step = 0;
    return;
  }

  if (dir > 0) {
    shift_up_one_row();
    rt_accum++;
    if (rt_accum >= 8) {
      boundary_up();
      rt_accum = 0;
    }
  }
  else {
    shift_down_one_row();
    rt_accum--;
    if (rt_accum <= -8) {
      boundary_down();
      rt_accum = 0;
    }
  }

  game_rects = &draw_SCREENRECT;
}

void
scroll_reset(void)
{
  rt_accum = 0;
  game_scroll_step = 0;
}

/* eof */
