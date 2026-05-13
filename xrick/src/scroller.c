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

/*
 * Approach A counter. Positive = shifts pending in the "up" direction
 * (world shifts up), negative = shifts pending in the "down" direction.
 * Zero = no batch in flight. A batch is always 8 shifts so block-aligned
 * boundary work (map_expand + ent_actvis) fires exactly once per batch,
 * on the shift that drives the counter back to zero -- matching the
 * cadence the original engine was designed around.
 */
static S8 scroll_pending = 0;

static void
shift_up_one_row(void)
{
  U8 i, j;

  /* translate map */
  for (i = MAP_ROW_SCRTOP; i < MAP_ROW_HBBOT; i++)
    for (j = 0x00; j < 0x20; j++)
      map_map[i][j] = map_map[i + 1][j];

  /* translate entities
   *
   * tick_prev_y must shift with y, otherwise the render-time
   * interpolation (ents_interp_y) would lerp from a pre-shift world
   * coord to a post-shift world coord -- a coordinate-system mismatch
   * that the camera lerp can't compensate for, manifesting as ~8 px of
   * jitter per render frame during the scroll. */
  for (i = 0; ent_ents[i].n != 0xFF; i++) {
    if (ent_ents[i].n) {
      ent_ents[i].ysave -= 8;
      ent_ents[i].trig_y -= 8;
      ent_ents[i].y -= 8;
      ent_ents[i].tick_prev_y -= 8;
      if (ent_ents[i].y & 0x8000) {
        IFDEBUG_SCROLLER(
          sys_printf("xrick/scroller: entity %#04X is gone\n", i);
          );
        ent_ents[i].n = 0;
      }
    }
  }
  ricks_extra_scroll(-8);
  bullets_extra_scroll(-8);
  bombs_extra_scroll(-8);
  ricks_kill_oob();

  /* Paint the shifted map only. Entities are painted later in the tick
   * (PAINT for the non-interpolate path, render-time game_paintEntities
   * for the interpolate path) at their post-action -- and, when
   * interpolating, lerped -- positions. Painting them here would freeze
   * their on-screen position at the pre-action y for the whole tick,
   * which reads as jitter against the smoothly-gliding camera. */
  maps_paint();
  map_frow++;
}

static void
shift_down_one_row(void)
{
  U8 i, j;

  /* translate map */
  for (i = MAP_ROW_SCRBOT; i > MAP_ROW_HTTOP; i--)
    for (j = 0x00; j < 0x20; j++)
      map_map[i][j] = map_map[i - 1][j];

  /* translate entities -- see shift_up_one_row for why tick_prev_y
   * must shift with y. */
  for (i = 0; ent_ents[i].n != 0xFF; i++) {
    if (ent_ents[i].n) {
      ent_ents[i].ysave += 8;
      ent_ents[i].trig_y += 8;
      ent_ents[i].y += 8;
      ent_ents[i].tick_prev_y += 8;
      if (ent_ents[i].y > 0x0140) {
        IFDEBUG_SCROLLER(
          sys_printf("xrick/scroller: entity %#04X is gone\n", i);
          );
        ent_ents[i].n = 0;
      }
    }
  }
  ricks_extra_scroll(+8);
  bullets_extra_scroll(+8);
  bombs_extra_scroll(+8);
  ricks_kill_oob();

  /* See shift_up_one_row: entities are painted later in the tick. */
  maps_paint();
  map_frow--;
}

static void
boundary_up(void)
{
  ent_actvis(map_frow + MAP_ROW_HBTOP, map_frow + MAP_ROW_HBBOT, 1);
  map_expand();
  maps_paint();
}

static void
boundary_down(void)
{
  ent_actvis(map_frow + MAP_ROW_HTTOP, map_frow + MAP_ROW_HTBOT, 1);
  map_expand();
  maps_paint();
}

void
scroll_realtime_request(S8 dir)
{
  if (scroll_pending != 0) return;  /* re-trigger guard */
  scroll_pending = (dir > 0) ? 8 : -8;
}

U8
scroll_realtime_in_progress(void)
{
  return scroll_pending != 0;
}

void
scroll_realtime_reset(void)
{
  scroll_pending = 0;
  game_scroll_step = 0;
}

void
scroll_realtime_tick(void)
{
  if (scroll_pending > 0) {
    game_scroll_step = 8;
    sysvid_snapshot_playfield();
    shift_up_one_row();
    if (--scroll_pending == 0)
      boundary_up();
    game_rects = &draw_SCREENRECT;
  }
  else if (scroll_pending < 0) {
    game_scroll_step = -8;
    sysvid_snapshot_playfield();
    shift_down_one_row();
    if (++scroll_pending == 0)
      boundary_down();
    game_rects = &draw_SCREENRECT;
  }
  else {
    /* Idle tick: clear stale lerp state from the just-finished batch. */
    game_scroll_step = 0;
  }
}

/*
 * Scroll up
 *
 */
U8
scroll_up(void)
{
  U8 i, j;
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

  /* loop */
  if (n++ == 7) {
    /* activate visible entities */
    ent_actvis(map_frow + MAP_ROW_HBTOP, map_frow + MAP_ROW_HBBOT, 0);

    /* prepare map */
    map_expand();

    /* display */
	maps_paint();
    ents_paintAll();
    env_paintGame();
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
  U8 i, j;
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

  /* Camera interp: world is about to shift down by 8. */
  game_scroll_step = -8;

  /* See scroll_up: capture pre-shift fb for the smooth-scroll lerp. */
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

  /* loop */
  if (n++ == 7) {
    /* activate visible entities */
    ent_actvis(map_frow + MAP_ROW_HTTOP, map_frow + MAP_ROW_HTBOT, 0);

    /* prepare map */
    map_expand();

    /* display */
	maps_paint();
    ents_paintAll();
    env_paintGame();
  }

  game_rects = &draw_SCREENRECT;

  return SCROLL_RUNNING;
}

/* eof */
