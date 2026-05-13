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
    ent_actvis(map_frow + MAP_ROW_HBTOP, map_frow + MAP_ROW_HBBOT);

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
    ent_actvis(map_frow + MAP_ROW_HTTOP, map_frow + MAP_ROW_HTBOT);

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
 * scroll_up_atomic
 *
 * Real-time scroll-up. All 8 row-shifts happen in one call. The pre-shift
 * playfield is snapshotted once into fb_prev; the visual catch-up across
 * the next N render frames is handled by game.c (sysvid_view_dy state).
 *
 * Gameplay state stays in CTRL_ACTION before/after this call -- there is
 * no SCROLL_UP state and CTRL_ACTION is NOT gated, so entities continue
 * to update during the visual catch-up.
 */
void
scroll_up_atomic(void)
{
  U8 i, j, k;

  /* Snapshot pre-shift fb once. The render path will lerp OLD (this
   * snapshot, frozen) against the live NEW fb across multiple ticks. */
  sysvid_snapshot_playfield();

  for (k = 0; k < 8; k++) {
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
          ent_ents[i].n = 0;
        }
      }
    }
    ricks_extra_scroll(-8);
    bullets_extra_scroll(-8);
    bombs_extra_scroll(-8);
    ricks_kill_oob();

    map_frow++;
  }

  /* End-of-batch: activate the band that scrolled into view, then
   * expand the new hardbuffer rows. Same call sites as the legacy
   * 8th-tick branch in scroll_up(). */
  ent_actvis(map_frow + MAP_ROW_HBTOP, map_frow + MAP_ROW_HBBOT);
  map_expand();

  /* Re-snapshot tick_prev for all entities so the render-time entity
   * interpolation (lerp tick_prev_y -> y) sees zero motion from the
   * scroll itself. Without this, tick_prev_y would still hold the
   * pre-shift y and the lerp would slide entities 64 px on top of the
   * camera composite, double-counting the shift. The catch-up slide
   * comes from the OLD/NEW composite alone. */
  ents_snapshot_tick();

  /* Paint the post-shift state once. The visual slide is composed by
   * the renderer using fb_prev (pre-shift, frozen) + this fb (post-
   * shift, live). */
  maps_paint();
  ents_paintAll();
  env_paintGame();

  game_rects = &draw_SCREENRECT;
}

/*
 * scroll_down_atomic
 *
 * Mirror of scroll_up_atomic for the downward direction.
 */
void
scroll_down_atomic(void)
{
  U8 i, j, k;

  sysvid_snapshot_playfield();

  for (k = 0; k < 8; k++) {
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
        if (ent_ents[i].y > 0x0140) {
          ent_ents[i].n = 0;
        }
      }
    }
    ricks_extra_scroll(+8);
    bullets_extra_scroll(+8);
    bombs_extra_scroll(+8);
    ricks_kill_oob();

    map_frow--;
  }

  ent_actvis(map_frow + MAP_ROW_HTTOP, map_frow + MAP_ROW_HTBOT);
  map_expand();

  /* See scroll_up_atomic: zero out the scroll's contribution to the
   * entity-interp lerp by re-snapshotting tick_prev. */
  ents_snapshot_tick();

  maps_paint();
  ents_paintAll();
  env_paintGame();

  game_rects = &draw_SCREENRECT;
}

/* eof */
