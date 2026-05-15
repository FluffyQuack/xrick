/*
 * xrick/src/sysevt.c
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

/*
 * 20021010 SDL_SCANCODE_n replaced by SDL_SCANCODE_Fn because some non-US keyboards
 *          requires that SHIFT be pressed to input numbers.
 *
 * Co-op (Stage 2): processEvent() now dispatches each scancode to the owning
 * player's control_status_p[i] slot, instead of OR-ing every key into one
 * shared global. The 1/2/3/4 keys toggle rick_count.
 */

#include <SDL.h>

#include "system.h"
#include "syskbd.h"
#include "sysvid.h"
#include "game.h"
#include "debug.h"

#include "control.h"
#include "draw.h"
#include "e_rick.h"
#include "ents.h"
#include "sysxinput.h"
#include "sysdinput.h"

#define SYSJOY_RANGE 3280

#define SETBIT(x,b) x |= (b)
#define CLRBIT(x,b) x &= ~(b)

static SDL_Event event;

/*
 * Walk the per-player scancode tables; if `key` matches one of P0..P3's
 * direction/fire keys, set or clear the corresponding bit on that player's
 * control_status_p[]. Returns TRUE if the key was claimed by a player.
 *
 * Each scancode is owned by at most one player by construction (we picked
 * non-overlapping layouts after dropping the Z/X/K/O legacy bindings).
 */
static U8
dispatch_player_key(U16 key, U8 down)
{
	U8 i, bit;
	const player_kbd_t *k;

	for (i = 0; i < CONTROL_PLAYERS; i++) {
		k = &syskbd_players[i];
		bit = 0;
		if (k->up    && key == k->up)    bit = CONTROL_UP;
		else if (k->down  && key == k->down)  bit = CONTROL_DOWN;
		else if (k->left  && key == k->left)  bit = CONTROL_LEFT;
		else if (k->right && key == k->right) bit = CONTROL_RIGHT;
		else if (k->fire  && key == k->fire)  bit = CONTROL_FIRE;
		if (!bit) continue;

		if (down)
			control_status_p[i] |= bit;
		else
			control_status_p[i] &= ~bit;

		/*
		 * control_last drives a few P1-only menu interactions (e.g. the
		 * "press fire to continue" gate on the map intro). Only update
		 * it for P1 so P2/P3 inputs don't leak into menu flow.
		 */
		if (i == 0)
			control_last = bit;
		return TRUE;
	}
	return FALSE;
}

/*
 * Activate/deactivate Rick slots in response to a 1/2/3/4 keypress.
 *
 * Stage 3: newly-activated Ricks get their backing ent_t (in
 * extra_rick_ents) wired up here so they immediately tick, render and
 * collide. Deactivated Ricks have their entity zeroed so they vanish.
 */
static void
set_rick_count(U8 n)
{
	U8 i;
	U8 anchor;
	U8 anchor_found;
	ent_t *src;

	if (n < 1) n = 1;
	if (n > RICK_MAX) n = RICK_MAX;

	/*
	 * Block while any Rick is mid-death animation (zombie = "fall into
	 * the screen"). Activating/deactivating slots mid-zombie leaves the
	 * engine in a weird state.
	 */
	for (i = 0; i < RICK_MAX; i++) {
		if (rick_active[i] && R_STTST(i, E_RICK_STZOMBIE) && !R_STTST(i, E_RICK_STDEAD))
			return;
	}

	/*
	 * Find an alive Rick to anchor new spawns on. Slot 0 may be dead, so
	 * we can't just use P1's position. If nobody is alive there is also
	 * no sensible place to spawn -- bail out.
	 */
	anchor_found = FALSE;
	anchor = 0;
	for (i = 0; i < RICK_MAX; i++) {
		if (rick_active[i] && !R_STTST(i, E_RICK_STDEAD | E_RICK_STZOMBIE)) {
			anchor = i;
			anchor_found = TRUE;
			break;
		}
	}
	if (!anchor_found) return;

	src = ricks_get_ent(anchor);

	for (i = 1; i < RICK_MAX; i++) {  /* slot 0 (P1) always active */
		if (i < n) {
			if (!rick_active[i]) {
				ent_t *e = &extra_rick_ents[i - 1];

				rick_active[i] = TRUE;
				/* Clear all action-state bits; new Rick is a fresh spawn. */
				R_STRST(i, E_RICK_STDEAD | E_RICK_STZOMBIE | E_RICK_STCRAWL |
				           E_RICK_STJUMP | E_RICK_STCLIMB | E_RICK_STSTOP |
				           E_RICK_STSHOOT);
				ricks[i].offsx = 0;
				ricks[i].offsy = 0;
				ricks[i].ylow  = 0;
				ricks[i].seq   = 0;
				ricks[i].trigger = FALSE;
				ricks[i].scrawl  = FALSE;
				ricks[i].atExit  = FALSE;
				ricks[i].prev_stopped = FALSE;
				/*
				 * The live entity (e->x/e->y below) drops in next to the
				 * anchor so the new Rick appears where the action is, but
				 * the per-Rick restart snapshot must point at the submap
				 * spawn -- otherwise a death-restart would send this Rick
				 * to wherever P0 happened to be when the count was bumped.
				 * Rick 0's save_* always holds the current submap spawn
				 * (game_save writes it at every INIT_MAP / INIT_SUBMAP and
				 * Rick 0 is always active), so mirror it here.
				 */
				ricks[i].save_x     = ricks[0].save_x;
				ricks[i].save_y     = ricks[0].save_y;
				ricks[i].save_crawl = ricks[0].save_crawl;
				/* Forget any stale input that might have been buffered. */
				control_status_p[i] = 0;

				/*
				 * Stage 3: arm the backing entity so the Rick is real
				 * starting next tick. Mirrors the field set init() does
				 * for ent_ents[1].
				 */
				e->x = src->x;
				e->y = src->y;
				e->w = 0x18;
				e->h = 0x15;
				e->n = 0x01;
				e->sprite = 0x01;
				e->front = FALSE;
			}
		} else {
			if (rick_active[i]) {
				ent_t *e = &extra_rick_ents[i - 1];
				rick_active[i] = FALSE;
				control_status_p[i] = 0;
				/* Stop drawing / colliding immediately. */
				e->n = 0;
				e->sprite = 0;
			}
		}
	}
	rick_count = n;
}

/*
 * Process an event
 */
static void
processEvent()
{
	U16 key;
#ifdef ENABLE_FOCUS
	SDL_ActiveEvent *aevent;
#endif

	switch (event.type) {
	case SDL_KEYDOWN:
		key = event.key.keysym.scancode;

		/* Per-player movement / fire. */
		if (dispatch_player_key(key, TRUE))
			break;

		/* Global keys -- always read off P1's slot via the macro. */
		if (key == syskbd_pause) {
			SETBIT(control_status, CONTROL_PAUSE);
			control_last = CONTROL_PAUSE;
		}
		else if (key == syskbd_end) {
			SETBIT(control_status, CONTROL_END);
			control_last = CONTROL_END;
		}
		else if (key == syskbd_xtra) {
			SETBIT(control_status, CONTROL_EXIT);
			control_last = CONTROL_EXIT;
		}
		/* Player-count hotkeys. SDL ignores keyboard layout, so SCANCODE_1..8
		 * are the digit row regardless of locale. */
		else if (key == SDL_SCANCODE_1) {
			set_rick_count(1);
		}
		else if (key == SDL_SCANCODE_2) {
			set_rick_count(2);
		}
		else if (key == SDL_SCANCODE_3) {
			set_rick_count(3);
		}
		else if (key == SDL_SCANCODE_4) {
			set_rick_count(4);
		}
		else if (key == SDL_SCANCODE_5) {
			set_rick_count(5);
		}
		else if (key == SDL_SCANCODE_6) {
			set_rick_count(6);
		}
		else if (key == SDL_SCANCODE_7) {
			set_rick_count(7);
		}
		else if (key == SDL_SCANCODE_8) {
			set_rick_count(8);
		}
		else if (key == SDL_SCANCODE_F1) {
			sysvid_toggleFullscreen();
		}
		else if (key == SDL_SCANCODE_F2) {
			sysvid_zoom(-1);
		}
		else if (key == SDL_SCANCODE_F3) {
			sysvid_zoom(+1);
		}
#ifdef ENABLE_SOUND
		else if (key == SDL_SCANCODE_F4) {
			syssnd_toggleMute();
		}
		else if (key == SDL_SCANCODE_F5) {
			syssnd_vol(-1);
		}
		else if (key == SDL_SCANCODE_F6) {
			syssnd_vol(+1);
		}
#endif
		else if (key == SDL_SCANCODE_F7) {
			game_toggleCheat(1);
		}
		else if (key == SDL_SCANCODE_F8) {
			game_toggleCheat(2);
		}
		else if (key == SDL_SCANCODE_F9) {
			game_toggleCheat(3);
		}
		else if (key == SDL_SCANCODE_F10) {
			/* Toggle render interpolation. With it off the game looks like
			 * the original 25 fps; with it on the simulation still ticks
			 * at 25 fps but entities are drawn at an interpolated position
			 * each rendered frame. */
			game_interpolate = !game_interpolate;
		}
		else if (key == SDL_SCANCODE_F11) {
			/* Toggle real-time scrolling. ON: scroll is atomic in one
			 * tick and the camera catches up visually while gameplay
			 * continues. OFF: original engine behavior -- gameplay
			 * pauses for 8 ticks while the world shifts one row per
			 * tick. */
			game_realtime_scroll = !game_realtime_scroll;
		}
		break;

	case SDL_KEYUP:
		key = event.key.keysym.scancode;

		if (dispatch_player_key(key, FALSE))
			break;

		if (key == syskbd_pause) {
			CLRBIT(control_status, CONTROL_PAUSE);
			control_last = CONTROL_PAUSE;
		}
		else if (key == syskbd_end) {
			CLRBIT(control_status, CONTROL_END);
			control_last = CONTROL_END;
		}
		else if (key == syskbd_xtra) {
			CLRBIT(control_status, CONTROL_EXIT);
			control_last = CONTROL_EXIT;
		}
		break;

	case SDL_QUIT:
		/* player tries to close the window -- this is the same as pressing ESC */
		SETBIT(control_status, CONTROL_EXIT);
		control_last = CONTROL_EXIT;
		break;
#ifdef ENABLE_FOCUS
	case SDL_ACTIVEEVENT: {
		aevent = (SDL_ActiveEvent *)&event;
		IFDEBUG_EVENTS(
			sys_printf("xrick/events: active %x %x\n", aevent->gain, aevent->state);
		);
		if (aevent->gain == 1)
			control_active = TRUE;
		else
			control_active = FALSE;
	}
	break;
#endif
#ifdef ENABLE_JOYSTICK
	/*
	 * Co-op (Stage 2): joystick is hard-routed to P1. Per-player joystick
	 * support is Stage 4 work.
	 */
	case SDL_JOYAXISMOTION:
		IFDEBUG_EVENTS(sys_printf("xrick/events: joystick\n"););
		if (event.jaxis.axis == 0) {  /* left-right */
			if (event.jaxis.value < -SYSJOY_RANGE) {  /* left */
				SETBIT(control_status, CONTROL_LEFT);
				CLRBIT(control_status, CONTROL_RIGHT);
			}
			else if (event.jaxis.value > SYSJOY_RANGE) {  /* right */
				SETBIT(control_status, CONTROL_RIGHT);
				CLRBIT(control_status, CONTROL_LEFT);
			}
			else {  /* center */
				CLRBIT(control_status, CONTROL_RIGHT);
				CLRBIT(control_status, CONTROL_LEFT);
			}
		}
		if (event.jaxis.axis == 1) {  /* up-down */
			if (event.jaxis.value < -SYSJOY_RANGE) {  /* up */
				SETBIT(control_status, CONTROL_UP);
				CLRBIT(control_status, CONTROL_DOWN);
			}
			else if (event.jaxis.value > SYSJOY_RANGE) {  /* down */
				SETBIT(control_status, CONTROL_DOWN);
				CLRBIT(control_status, CONTROL_UP);
			}
			else {  /* center */
				CLRBIT(control_status, CONTROL_DOWN);
				CLRBIT(control_status, CONTROL_UP);
			}
		}
		break;
	case SDL_JOYBUTTONDOWN:
		SETBIT(control_status, CONTROL_FIRE);
		break;
	case SDL_JOYBUTTONUP:
		CLRBIT(control_status, CONTROL_FIRE);
		break;
#endif
	default:
		break;
	}
}

/*
 * Process events, if any, then return
 */
void
sysevt_poll(void)
{
	while (SDL_PollEvent(&event))
		processEvent();
	sysxinput_apply();
	sysdinput_apply();
}

/*
 * Wait for an event, then process it and return
 */
void
sysevt_wait(void)
{
	/* SDL_WaitEvent locks emscripten; this is only for pause really */

#ifdef EMSCRIPTEN
	if (SDL_PollEvent(&event))
#else
	SDL_WaitEvent(&event);
#endif
	processEvent();
}

/* eof */
