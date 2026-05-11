/*
 * xrick/src/game.c
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
#include "sysarg.h"
#include "sysvid.h"
#include "sysevt.h"
#include "env.h"

#include "game.h"

#include "draw.h"
#include "maps.h"
#include "ents.h"
#include "sounds.h"
#include "e_rick.h"
#include "e_sbonus.h"
#include "e_them.h"
#include "screens.h"
#include "rects.h"
#include "scroller.h"
#include "control.h"
#include "data.h"
#include "fb.h"
#include "tiles.h"
#include "sprites.h"
#include "draw.h"

#ifdef EMSCRIPTEN
#include "emscripten.h"
#endif

#ifdef ENABLE_DEVTOOLS
#include "devtools.h"
#endif


/*
 * local typedefs
 */
typedef enum {
#ifdef ENABLE_DEVTOOLS
  DEVTOOLS,
#endif
  XRICK, XRICK_CLR,
  MAIN_INTRO, MAP_INTRO,
  INIT,
  INIT_MAP, INIT_SUBMAP,
  FADEIN__CTRL_ACTION, FADEOUT__MAP_INTRO, FADEOUT__GAMEOVER,
  PAUSE_PRESSED1, PAUSE_PRESSED1B, PAUSED, PAUSE_PRESSED2,
  CTRL_ACTION, CTRL_PAUSE, CTRL_RICK, PAINT, CTRL_SCROLL,
  NEXT_SUBMAP, NEXT_MAP,
  SCROLL_UP, SCROLL_DOWN,
  RESTART, GAMEOVER, GETNAME, EXIT
} game_state_t;


/*
 * global vars
 */
U8 game_period = 0;
U8 game_waitevt = FALSE;
rect_t *game_rects = NULL;

/*
 * Render interpolation toggle. When TRUE, entities are painted at a
 * position lerped between (tick_prev_x,y) and (x,y) using the fraction of
 * the current tick that has elapsed -- the simulation still ticks at 25 fps
 * but the screen updates smoothly at the present rate. When FALSE the
 * legacy behavior (paint at the current tick position) is restored. Toggled
 * by F10.
 */
U8 game_interpolate = TRUE;

/*
 * Camera interpolation: the scroller shifts the world by 8 px per tick. We
 * keep the per-tick step here (+8 = scrolling up, -8 = scrolling down,
 * 0 = no scroll). The render path uses this to compute a visual offset
 * (1 - alpha) * step, applied by sysvid_update so the camera glides to
 * the post-shift position over the tick window instead of snapping.
 */
S8 game_scroll_step = 0;

#ifdef GFXST
hscore_t game_hscores[8] = {
  { 8000, "SIMES@@@@@" },
  { 7000, "JAYNE@@@@@" },
  { 6000, "DANGERSTU@" },
  { 5000, "KEN@@@@@@@" },
  { 4000, "ROB@N@BOB@" },
  { 3000, "TELLY@@@@@" },
  { 2000, "NOBBY@@@@@" },
  { 1000, "JEZEBEL@@@" }
};
#endif
#ifdef GFXPC
hscore_t game_hscores[8] = {
  { 8000, "DANGERSTU@" },
  { 7000, "SIMES@@@@@" },
  { 6000, "KEN@T@ZEN@" },
  { 5000, "BOBBLE@@@@" },
  { 4000, "GREG@LAA@@" },
  { 3000, "TELLY@@@@@" },
  { 2000, "CHIGLET@@@" },
  { 1000, "ANDYSPLEEN" }
};
#endif


/*
 * local vars
 */
static U8 save_map_row;
static game_state_t game_state;
static U32 tm, tmx;

/*
 * Co-op: index of the Rick that drove the current submap-exit transition.
 * Set when leaving CTRL_RICK with any Rick's atExit flag raised; consumed by
 * NEXT_SUBMAP (passed to map_chain) and INIT_SUBMAP (passed to ricks_spawn_at
 * so everyone snaps to the triggering Rick's new-submap edge position).
 */
static U8 exit_trigger;

/*
 * Pace patterns. Each entry is a game-frame duration in microseconds; the
 * pattern is cycled. Decoupled from render rate (see GAME_RENDER_FPS).
 */
typedef struct {
	const U32 *tick_us;
	int        count;
} pace_pattern_t;

/* Steady 30 fps. */
static const U32 pace_30fps_us[] = { 33333 };

/*
 * Steady 25 fps -- the average rate the Atari ST version ran at, but with a
 * uniform 40 ms tick instead of the original's 30 Hz / 15 Hz alternation.
 * (The ST's uneven cadence was a hardware artifact of running 25 Hz logic
 * on a 60 Hz output; we don't need to mimic the artifact, just the rate.)
 */
static const U32 pace_25fps_us[] = { 40000 };

static const pace_pattern_t pace_patterns[] = {
	{ pace_30fps_us, (int)(sizeof(pace_30fps_us) / sizeof(U32)) },
	{ pace_25fps_us, (int)(sizeof(pace_25fps_us) / sizeof(U32)) },
};

static U32 next_tick_us;    /* absolute deadline for next game tick, us */
static U32 next_render_us;  /* absolute deadline for next present, us */
static int pace_idx;        /* index into the active pace pattern */

/*
 * Interpolation: time of the most recent tick boundary (when tick_prev_*
 * was snapshotted) and the duration of that tick in microseconds. Renders
 * between ticks compute alpha = (now - last_tick_us) / last_tick_period_us.
 */
static U32 last_tick_us = 0;
static U32 last_tick_period_us = 40000;


/*
 * prototypes
 */
static void game_cycle(void);
static U8   game_state_is_play(void);
static void init(void);
static void restart(void);
static void loadData(void);
static void freeData(void);
static void game_paintEntities();
static void game_save(void);

/*
 * Co-op camera target.
 *
 * The scroll-trigger logic compares the returned y against the 0x60 / 0xCC
 * deadzone (>= 0xCC -> SCROLL_UP, <= 0x60 -> SCROLL_DOWN). With more than
 * one alive Rick we follow the "leader" -- whichever Rick has progressed
 * furthest from the submap's spawn point. Distance is measured in absolute
 * world coordinates (map_frow*8 + y, plus x), which scroll preserves, so
 * leader identity is stable across frames and the camera doesn't bounce.
 * Any teammate who can't keep up gets pushed off-world by the scroll and
 * retired by ricks_kill_oob() in scroller.c.
 */
#define CAMERA_NO_TARGET  0xFFFF

/*
 * Spawn anchor for the current submap, in absolute world coordinates.
 * Recorded by camera_record_spawn() after each ricks_spawn_at() call.
 * Absolute y = map_frow*8 + e->y is invariant under scroll (frow++ and
 * y-=8 cancel), so distances computed against this anchor remain valid
 * for the lifetime of the submap.
 */
static U16 camera_spawn_abs_x = 0;
static U16 camera_spawn_abs_y = 0;

static void camera_record_spawn(U8 anchor)
{
	ent_t *e = ricks_get_ent(anchor);
	camera_spawn_abs_x = e->x;
	camera_spawn_abs_y = (U16)(map_frow * 8) + e->y;
}

static U16 camera_target_y(void)
{
	U8  r;
	U8  best_idx = RICK_MAX;
	U16 best_y = 0;
	U32 best_dist = 0;

	for (r = 0; r < RICK_MAX; r++)
	{
		ent_t *e;
		U16 y, abs_y;
		U32 dx, dy, dist;

		if (!rick_active[r]) continue;
		if (R_STTST(r, E_RICK_STDEAD | E_RICK_STZOMBIE)) continue;

		e = ricks_get_ent(r);
		y = e->y;
		/* Skip Ricks already off the world (reachable only with the
		 * invincibility cheat, since ricks_kill_oob retires them
		 * otherwise). Their wrapped y would corrupt the distance calc. */
		if ((y & 0x8000) || y > 0x0140) continue;

		abs_y = (U16)(map_frow * 8) + y;
		dx = (e->x >= camera_spawn_abs_x)
		     ? (U32)(e->x - camera_spawn_abs_x)
		     : (U32)(camera_spawn_abs_x - e->x);
		dy = (abs_y >= camera_spawn_abs_y)
		     ? (U32)(abs_y - camera_spawn_abs_y)
		     : (U32)(camera_spawn_abs_y - abs_y);
		dist = dx + dy;

		/* Prefer lower-indexed Rick on ties so leader identity is
		 * deterministic at submap entry (everyone starts at distance 0). */
		if (best_idx == RICK_MAX || dist > best_dist)
		{
			best_idx  = r;
			best_dist = dist;
			best_y    = y;
		}
	}

	if (best_idx == RICK_MAX) return CAMERA_NO_TARGET;
	return best_y;
}


/*
 * game_toggleCheat
 *
 * toggles one of the three cheat options
 * FIXME weird dependencies here! + _state exclusion is not complete
 */
void game_toggleCheat(U8 nbr)
{
#ifdef ENABLE_CHEATS
	if (game_state != MAIN_INTRO && game_state != MAP_INTRO &&
		game_state != GAMEOVER && game_state != GETNAME &&
#ifdef ENABLE_DEVTOOLS
		game_state != DEVTOOLS &&
#endif
		game_state != XRICK && game_state != EXIT)
	{
		switch (nbr)
		{
			case 1:
				env_trainer = ~env_trainer;
				env_lives = 6;
				env_bombs = 6;
				env_bullets = 6;
				break;

			case 2:
				env_invicible = ~env_invicible;
				break;

			case 3:
				env_highlight = ~env_highlight;
			break;
		}

		env_paintXtra(); /* fixme -- shouldn't this be done elswhere? */

		/* FIXME this should probably only raise a flag ... */
		/* plus we only need to update INFORECT not the whole screen */
		sysvid_update(&draw_SCREENRECT);
	}
#endif
}

/* prototype */
static void game_loop(void);
static void game_tick(void);
static void game_exit(void);


/*
 * game_run
 *
 * main loop.
 */
void
game_run(char *path)
{
	sys_printf("xrick/game: path='%s'\n", path ? path : "");

	data_setpath(path);
	loadData(); /* load cached data */

	/*
	 * Plant the ent_ents end-sentinel early. ents_snapshot_tick() runs
	 * every tick (including during the splash/intro screens) and walks
	 * the array until it hits n == 0xff. init() normally plants it, but
	 * init() only runs after the user starts a game -- without this the
	 * pre-game snapshots run off the end of the array and trash memory.
	 */
	ent_ents[ENT_ENTSNUM].n = 0xFF;

	game_period = sysarg_args_period ? sysarg_args_period : GAME_PERIOD;
	tm = sys_gettime();
	{
		U32 now = sys_gettime_us();
		next_tick_us   = now;
		next_render_us = now;
		pace_idx       = 0;
	}
	game_state = XRICK;

	/* main loop */
#ifdef EMSCRIPTEN
	/*
	 * Emscripten path is unchanged: defer to requestAnimationFrame and run
	 * a single tick+present per callback. Browser pacing handles the rest.
	 */
	int fps = (24 * GAME_PERIOD) / game_period;
	emscripten_set_main_loop(game_loop, fps, 1);
#else
	{
		const pace_pattern_t *pat = &pace_patterns[GAME_PACE_MODE];
		const U32 render_period_us = 1000000U / GAME_RENDER_FPS;

		while (game_state != EXIT)
		{
			U32 now = sys_gettime_us();

			/*
			 * Advance game logic until caught up to the deadline.
			 * Cap catch-up at a few ticks per iteration so a long stall
			 * can't make us spin forever replaying frames.
			 */
			int catchup_budget = 4;
			while ((S32)(now - next_tick_us) >= 0)
			{
				game_tick();
				if (game_state == EXIT) break;

				next_tick_us += pat->tick_us[pace_idx];
				pace_idx = (pace_idx + 1) % pat->count;

				if (--catchup_budget <= 0) break;
				now = sys_gettime_us();
			}
			if (game_state == EXIT) break;

			now = sys_gettime_us();
			if (catchup_budget <= 0 && (S32)(now - next_tick_us) >= 0)
			{
				/* Still behind after the catch-up budget: resync. */
				next_tick_us = now + pat->tick_us[pace_idx];
			}

			/*
			 * Present. sysvid_update is a no-op when game_rects is NULL
			 * (no dirty regions), so this naturally throttles itself to
			 * "present once per game tick" -- the render-fps cap below
			 * just bounds the worst case.
			 *
			 * Interpolation: before presenting, compute the current alpha
			 * within the tick (0 at tick start, 1 at next tick) and let
			 * game_paintEntities() lerp entities to that fraction. The
			 * status bar is repainted as part of that call, which also
			 * re-establishes game_rects for sysvid_update.
			 */
			if ((S32)(now - next_render_us) >= 0)
			{
				/*
				 * Camera interpolation: during a scroll the world data
				 * has already been shifted by the current tick; visually
				 * lag the present by (1 - alpha) * step so the view glides
				 * to the post-shift position over the tick window.
				 */
				if (game_interpolate &&
				    (game_state == SCROLL_UP || game_state == SCROLL_DOWN))
				{
					S32 elapsed = (S32)(now - last_tick_us);
					S32 period  = (S32)last_tick_period_us;
					if (period <= 0) period = 1;
					if (elapsed < 0) elapsed = 0;
					if (elapsed > period) elapsed = period;
					sysvid_view_dy = (S16)
						((S32)game_scroll_step * (period - elapsed) / period);
					/* OLD playfield (pre-scroll snapshot) lerps in the
					 * opposite phase: same direction, offset by step. At
					 * tick start the OLD frame is at its rest position
					 * (dy_old = 0) and the NEW frame is shifted by step;
					 * at tick end OLD is shifted by -step (off the
					 * uncovered edge) and NEW is at rest. */
					sysvid_view_dy_old =
						(S16)(sysvid_view_dy - (S16)game_scroll_step);
				}
				else
				{
					sysvid_view_dy = 0;
					sysvid_view_dy_old = 0;
				}

				/*
				 * Entity interpolation. Only run when game_rects is NULL:
				 * if a full-screen refresh (SCREENRECT) or other rects are
				 * already pending (e.g. just entered a new submap, or the
				 * scroller painted), present those first -- our paint
				 * would otherwise clobber game_rects with a smaller
				 * STATUSRECT+ent_rects list and lose the full redraw.
				 */
				if (game_interpolate && game_state_is_play() &&
				    game_rects == NULL)
				{
					S32 elapsed = (S32)(now - last_tick_us);
					S32 period  = (S32)last_tick_period_us;
					if (period <= 0) period = 1;
					if (elapsed < 0) elapsed = 0;
					if (elapsed > period) elapsed = period;
					ents_set_alpha(elapsed, period);
					game_paintEntities();
					ents_set_alpha(1, 1);
				}
				sysvid_update(game_rects);
				draw_STATUSRECT.next = NULL;
				game_rects = NULL;
				do { next_render_us += render_period_us; }
				while ((S32)(now - next_render_us) >= 0);
			}

			/* Sleep until the earliest of the two deadlines. */
			U32 deadline = ((S32)(next_tick_us - next_render_us) < 0)
			               ? next_tick_us : next_render_us;
			S32 wait_us = (S32)(deadline - sys_gettime_us());
			if (wait_us > 1500)
				sys_sleep(wait_us / 1000); /* drop sub-ms remainder; spin handles it */
		}
	}
#endif

	game_exit();
}

/*
 * Predicate: are we in a state where entity painting / interpolation makes
 * sense? Used by the render path to gate the extra game_paintEntities()
 * call so it doesn't smear stale entities over intros, fades, menus, etc.
 */
static U8 game_state_is_play(void)
{
	switch (game_state) {
		case CTRL_ACTION:
		case CTRL_PAUSE:
		case CTRL_RICK:
		case PAINT:
		case CTRL_SCROLL:
		case PAUSED:
		case PAUSE_PRESSED1:
		case PAUSE_PRESSED1B:
		case PAUSE_PRESSED2:
			return TRUE;
		default:
			return FALSE;
	}
}

static void game_exit(void)
{
	freeData(); /* free cached data */
	data_closepath();
}

/*
 * One game tick: process events, advance simulation by one frame, mark
 * dirty rects. Does NOT present -- the desktop loop in game_run handles
 * presentation on its own schedule. Used directly by the desktop pacer
 * and (via game_loop below) by the emscripten path.
 */
static void game_tick(void)
{
	/*
	 * Interpolation: snapshot positions and timing at the start of every
	 * tick. Captures (x,y) -> (tick_prev_x,y) for entities before anything
	 * in game_cycle moves them (ent_action, scroll, etc.) and records the
	 * tick start time + duration so the render path can compute alpha.
	 * Doing this unconditionally keeps tick_prev_* consistent even when
	 * game_interpolate is toggled mid-frame.
	 */
	ents_snapshot_tick();
	last_tick_us = sys_gettime_us();
	last_tick_period_us = pace_patterns[GAME_PACE_MODE].tick_us[
		pace_idx % pace_patterns[GAME_PACE_MODE].count];

	/* events */
	if (game_waitevt)
		sysevt_wait();  /* wait for an event, stop doing anything */
	else
		sysevt_poll();  /* process events (non-blocking) */

	/*
	 * game_cycle: depending on the game state
	 * - process events
	 * - run the game logic, AI, ...
	 * - paints a new frame onto the frame buffer
	 * - updates fb_updatedRects
	 */
	game_cycle();
}

static void game_loop(void)
{
	/*
	 * Used as the emscripten requestAnimationFrame callback. The desktop
	 * loop in game_run() does its own pacing and does NOT call this.
	 */
#ifdef EMSCRIPTEN
	game_tick();
	sysvid_update(game_rects);
	draw_STATUSRECT.next = NULL;
#else
	/* Desktop path no longer routes through game_loop; left for safety. */
	game_tick();
	sysvid_update(game_rects);
	draw_STATUSRECT.next = NULL;
#endif

#ifdef EMSCRIPTEN
	if (game_state == EXIT)
	{
		game_exit();
		sys_shutdown();
		emscripten_cancel_main_loop();
	}
#endif
}


//static game_state_t game_state2;

/*
 * game_cycle
 *
 * This function loops forever: use 'return' when a frame is ready.
 * When returning, game_rects must contain every parts of the buffer
 * that have been modified.
 */
static void game_cycle(void)
{
	while (1) {

		//if (game_state != game_state2)
		//{
		//	sys_printf("xrick/game: state = %d", (U8) game_state);
		//	game_state2 = game_state;
		//}

		switch (game_state) {



#ifdef ENABLE_DEVTOOLS
		case DEVTOOLS:

			switch (devtools_run()) {
			case SCREEN_RUNNING:
				return;
			case SCREEN_DONE:
				game_state = INIT_GAME;
				break;
			case SCREEN_EXIT:
				game_state = EXIT;
				return;
			}
		break;
#endif


		case XRICK:

			switch(screen_xrick())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_state = XRICK_CLR;
					return;
				case SCREEN_EXIT:
					game_state = EXIT;
					return;
			}
		break;



		case XRICK_CLR:

			/* this step is required to force a screen update (clear) before changing the palette */
			fb_initPalette();
			sprites_initHatPalette();
#ifdef ENABLE_DEVTOOLS
			game_state = DEVTOOLS;
#else
			game_state = MAIN_INTRO;
#endif
			break;



		case MAIN_INTRO:

			switch (screen_introMain())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_state = INIT;
					break;
				case SCREEN_EXIT:
					game_state = EXIT;
					return;
			}
			break;



		case INIT:

			init();
			if (env_submap == map_maps[env_map].submap)
			{
				game_state = MAP_INTRO;
			}
			else
			{
				game_state = INIT_MAP; /* no intro if not first submap */
			}
			break;



		case MAP_INTRO:

			switch (screen_introMap())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_waitevt = FALSE;
					game_state = INIT_MAP;
					break;
				case SCREEN_EXIT:
					game_state = EXIT;
					return;
			}
			break;



		case INIT_MAP:

			if (env_map >= 0x04) /* reached end of game */
			{
				sysarg_args_map = 0; // FIXME game completed, start all over. fine, but... ack...
				sysarg_args_submap = 0;
				game_state = FADEOUT__GAMEOVER;
			}
			else
			{
				map_init();
				/*
				 * Co-op (Stage 3): every active Rick spawns at the same
				 * spot on map entry. Must run before game_save() so the
				 * snapshot used by restart() captures the shared spawn.
				 */
				ricks_spawn_at(0);
				camera_record_spawn(0);
				game_save();
				fb_clear();                 /* clear buffer */
				//ent_clprev();
				maps_paint();                     /* draw the map onto the buffer */
				//ents_paintAll();
				env_paintGame();              /* draw the status bar onto the buffer */
				env_paintXtra();                   /* draw the info bar onto the buffer */
				game_rects = &draw_SCREENRECT;  /* request full buffer refresh */
				game_state = FADEIN__CTRL_ACTION;
			}
			break;



		case FADEIN__CTRL_ACTION:

			if (fb_fadeIn())
			{
				game_state = CTRL_ACTION;
			}
			return;



		case PAUSE_PRESSED1:

			screen_pause(TRUE);
			game_state = PAUSE_PRESSED1B;
			break;



		case PAUSE_PRESSED1B:

			if (control_status & CONTROL_PAUSE)
				return;
			game_state = PAUSED;
			break;



		case PAUSED:

			if (control_status & CONTROL_PAUSE)
			{
				game_state = PAUSE_PRESSED2;
			}
			if (control_status & CONTROL_EXIT)
			{
				game_state = EXIT;
			}
			return;



		case PAUSE_PRESSED2:

			if (!(control_status & CONTROL_PAUSE)) 
			{
				game_waitevt = FALSE;
				screen_pause(FALSE);
#ifdef ENABLE_SOUND
				syssnd_pause(FALSE, FALSE);
#endif
				game_state = CTRL_RICK;
			}
		return;



		case CTRL_ACTION:

			if (control_status & CONTROL_END) /* request to end the game */
			{
				game_state = FADEOUT__GAMEOVER;
			}
			else
			if (control_last == CONTROL_EXIT) /* request to exit the game */
			{
				game_state = EXIT;
			}
			else
			{
				ent_action();      /* run entities */
				e_them_rndseed++;  /* (0270) */
				/* Co-op: retire any Rick who walked off the world
				 * under their own power (the scroller's kill_oob
				 * only fires during scroll). Without this a
				 * laggard wandering past y > 0x140 stays "alive"
				 * with corrupt coords and crashes later. */
				ricks_kill_oob();
				game_state = CTRL_PAUSE;
			}
			break;



		case CTRL_PAUSE:

			if (control_status & CONTROL_PAUSE)
			{
#ifdef ENABLE_SOUND
				syssnd_pause(TRUE, FALSE);
#endif
				game_waitevt = TRUE;
				game_state = PAUSE_PRESSED1;
			}
			else
			if (control_active == FALSE)
			{
#ifdef ENABLE_SOUND
				syssnd_pause(TRUE, FALSE);
#endif
				game_waitevt = TRUE;
				screen_pause(TRUE);
				game_state = PAUSED;
			}
			else
			{
				game_state = CTRL_RICK;
			}
			break;



		case CTRL_RICK:
			{
				/*
				 * Co-op: lose a life only when *every* active Rick is dead.
				 * With rick_count == 1 this collapses to the original
				 * single-player check. Any active Rick can drive a submap
				 * exit -- the first one we find with atExit set becomes the
				 * trigger for map_chain / ricks_spawn_at.
				 */
				U8 r;
				U8 all_dead = TRUE;
				U8 exiter = RICK_MAX; /* sentinel: no Rick exiting */
				for (r = 0; r < RICK_MAX; r++)
				{
					if (!rick_active[r]) continue;
					if (!R_STTST(r, E_RICK_STDEAD)) all_dead = FALSE;
					if (exiter == RICK_MAX && ricks[r].atExit) exiter = r;
				}

			if (all_dead)
			{
				if (env_trainer || --env_lives)
				{
					game_state = RESTART;
				}
				else
				{
					game_state = FADEOUT__GAMEOVER;
				}
			}
			else
			if (exiter != RICK_MAX) /* a rick is exiting the submap, must chain */
			{
				ricks[exiter].atExit = FALSE;
				exit_trigger = exiter;
				game_state = NEXT_SUBMAP;
			}
			else
			{
				game_state = PAINT;
			}
			}
			break;



		case PAINT:

			/*
			 * With interpolation on, defer entity painting to render time
			 * so each presented frame can lerp between tick_prev_* and the
			 * post-action (x,y). The render path in game_run() calls
			 * game_paintEntities() itself; doing it here too would just
			 * waste work (the render would immediately erase + redraw).
			 */
			if (!game_interpolate)
				game_paintEntities();
			game_state = CTRL_SCROLL;
			return;



		case CTRL_SCROLL:
			{
				U16 cam_y = camera_target_y();
				if (cam_y == CAMERA_NO_TARGET)
				{
					game_state = CTRL_ACTION;
				}
				else if (cam_y >= 0xcc)
				{
					game_state = SCROLL_UP;
				}
				else if (cam_y <= 0x60)
				{
					game_state = SCROLL_DOWN;
				}
				else
				{
					game_state = CTRL_ACTION;
				}
			}
			break;



		case NEXT_SUBMAP:

			if (map_chain(exit_trigger))
			{
				/* next submap, now initialize */
				game_state = INIT_SUBMAP;
			}
			else
			{
				/* end of submap, chain to next map */

				env_bullets = 0x06;
				env_bombs = 0x06;
				env_map++;

				if (env_map == 0x04)
				{
					/* reached end of game */
					/* FIXME @292?*/
				}

				game_state = NEXT_MAP;
			}
			break;



		case NEXT_MAP:

			ent_ents[1].x = map_maps[env_map].x;
			ent_ents[1].y = map_maps[env_map].y;
			map_frow = (U8)map_maps[env_map].row;
			env_submap = map_maps[env_map].submap;
			/* Fresh map start: Rick 0 owns the spawn from map_maps[], so the
			 * subsequent ricks_spawn_at(exit_trigger) in INIT_SUBMAP must
			 * anchor on Rick 0, not on the (now stale) submap-edge trigger. */
			exit_trigger = 0;
			game_state = FADEOUT__MAP_INTRO;
			break;



		case FADEOUT__MAP_INTRO:

			if (fb_fadeOut())
			{
				game_state = MAP_INTRO;
			}
			return;



		case INIT_SUBMAP:

			map_init();                     /* initialize the map */
			/*
			 * Co-op: anchor all Ricks on whichever one triggered the exit.
			 * Their x has been wrapped to the new submap's entry edge by
			 * e_rick_action2; non-trigger Ricks (including Rick 0 if a
			 * non-P1 player drove the exit) snap to that position.
			 */
			ricks_spawn_at(exit_trigger);
			camera_record_spawn(exit_trigger);
			exit_trigger = 0;
			game_save();                        /* save data in case of a restart */
			fb_clear();
			ent_clprev();                   /* cleanup entities */
			maps_paint();                     /* draw the map onto the buffer */
			ents_paintAll();
			env_paintGame();              /* draw the status bar onto the buffer */
			env_paintXtra();
			game_rects = &draw_SCREENRECT;  /* request full screen refresh */
			game_state = CTRL_ACTION;
			return;



		case SCROLL_UP:

			switch (scroll_up())
			{
				case SCROLL_RUNNING:
					return;
				case SCROLL_DONE:
					game_state = CTRL_ACTION;
					break;
			}
			break;



		case SCROLL_DOWN:

			switch (scroll_down())
			{
				case SCROLL_RUNNING:
					return;
				case SCROLL_DONE:
					game_state = CTRL_ACTION;
					break;
			}
			break;



		case RESTART:

			restart();
			game_state = CTRL_ACTION;
			return;



		case FADEOUT__GAMEOVER:

			if (fb_fadeOut())
				game_state = GAMEOVER;
			return;



		case GAMEOVER:

			switch (screen_gameover())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_state = GETNAME;
					break;
				case SCREEN_EXIT:
					game_state = EXIT;
					break;
			}
			break;



		case GETNAME:

			switch (screen_getname())
			{
				case SCREEN_RUNNING:
					return;
				case SCREEN_DONE:
					game_state = XRICK_CLR;
					return;
				case SCREEN_EXIT:
					game_state = EXIT;
					break;
			}
			break;



		case EXIT:
			return;
    }
  }
}



/*
 * init
 *
 * FIXME some dirty hacks here, plus we should not manage sysargs_ this way
 */
static void
init(void)
{
  U8 i;

  /*
   * Reset all per-Rick state. Rick 0 stays active; Ricks 1-3 are
   * inactive until Stage 2 wires up the player-count keys.
   */
  ricks_init();

  env_lives = 6;
  env_bombs = 6;
  env_bullets = 6;
  env_score = 0;

  env_map = sysarg_args_map;

  if (sysarg_args_submap == 0) {
    env_submap = map_maps[env_map].submap;
    map_frow = (U8)map_maps[env_map].row;
  }
  else {
    /* dirty hack to determine frow by chaining submaps...*/
    env_submap = sysarg_args_submap;
    i = 0;
    while (i < 4 && map_maps[i++].submap <= env_submap);
    env_map = i - 1;
    i = 0;
    while (i < MAP_NBR_CONNECT &&
	   (map_connect[i].submap != env_submap ||
	    map_connect[i].dir != RIGHT))
      i++;
    map_frow = map_connect[i].rowin - 0x10; // WHY 0x10??
    ent_ents[1].y = 0x10 << 3; // FIXME?
  }

  ent_ents[1].x = map_maps[env_map].x;
  ent_ents[1].y = map_maps[env_map].y;
  ent_ents[1].w = 0x18;
  ent_ents[1].h = 0x15;
  ent_ents[1].n = 0x01;
  ent_ents[1].sprite = 0x01;
  ent_ents[1].front = FALSE;
  ent_ents[ENT_ENTSNUM].n = 0xFF;

  /*
   * Co-op (Stage 3): position the extra Ricks at Rick 0's spawn. At INIT
   * only Rick 0 is active, so this is a no-op today; it becomes
   * load-bearing as soon as the player presses 2/3/4 mid-game.
   */
  ricks_spawn_at(0);
  camera_record_spawn(0);

  map_resetMarks();
}



/*
 * game_paintEntities
 *
 * paints the entities.
 */
static void game_paintEntities()
{
	static rect_t *r;

	env_clearGame();  /* clear the status bar */
	ents_paintAll();  /* draw all entities onto the buffer */
	env_paintGame();  /* draw the status bar onto the buffer*/

	/* fixme: rectangle management!!*/
	// should just do: fb_touchRect(env_GameRect)
	r = &draw_STATUSRECT; r->next = ent_rects;  /* refresh status bar too */
	game_rects = r;   /* take care to cleanup draw_STATUSRECT->next later! */
}



/*
 * restart
 *
 * restarts the game after rick died. just come back to the beginning
 * of the current submap, restore positions and flags and...
 */
static void restart(void)
{
	U8 r;

	/* clear DEAD/ZOMBIE for every active Rick (collapses to Rick 0 at rick_count == 1) */
	for (r = 0; r < RICK_MAX; r++)
		if (rick_active[r])
			R_STRST(r, E_RICK_STDEAD|E_RICK_STZOMBIE);

	env_bullets = 6;
	env_bombs = 6;

	ent_ents[1].n = 1; // FIXMEwhy??

	/* restore the spawn position of every active Rick from its saved state */
	for (r = 0; r < RICK_MAX; r++)
		if (rick_active[r])
			e_rick_restore(r);

	map_frow = save_map_row;

	map_init(); // see INIT_MAP check that everything is OK here
	game_save();
	ent_clprev();
	maps_paint();
	env_paintGame(); // and Xtra???
	game_rects = &draw_SCREENRECT; //fb_touchFb();
}



/*
 * game_save
 *
 * save game state so it can be restored when rick dies, by <restart>.
 * it is NOT a "save game" option!
 */
static void game_save(void)
{
  U8 r;
  /* snapshot spawn for every active Rick — at rick_count == 1 only Rick 0 */
  for (r = 0; r < RICK_MAX; r++)
    if (rick_active[r])
      e_rick_save(r);
  save_map_row = map_frow;
}



/*
 * loadData
 *
 * loads data into cache.
 */
static void loadData()
{
#ifdef ENABLE_SOUND
	sounds_load();
#endif
}



/*
 * freeData
 *
 * free cached data
 */
static void freeData()
{
#ifdef ENABLE_SOUND
	sounds_free();
#endif
}



/* eof */
