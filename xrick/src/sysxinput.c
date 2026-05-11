/*
 * xrick/src/sysxinput.c
 *
 * XInput controller support. Reads each configured controller every
 * sysevt_poll, translates dpad / left stick / face buttons into the
 * CONTROL_* bitset for the owning player.
 *
 * Adapted from H:\projects\codework\forked-github\Colditz-Escape\eckbock-input.c.
 *
 * Coexistence with the keyboard path: dispatch_player_key in sysevt.c
 * still sets/clears bits on key events. We OR controller bits in on top
 * and remember which bits we contributed last frame so we can clear
 * exactly those next frame without stomping keyboard-held bits -- the
 * one edge case (same player using kbd+pad for the same direction)
 * follows whichever input releases last; acceptable for a fallback.
 */

#include "sysxinput.h"
#include "control.h"
#include "system.h"
#include "e_rick.h"  /* ricks[i].dir for B-button stick direction fallback */
#include "game.h"    /* LEFT / RIGHT */

#include <string.h>

/* Identity mapping: P0->xpad0, P1->xpad1, ... -1 disables a slot. */
int sysxinput_player[4] = { 0, 1, 2, 3 };

#if defined(_WIN32) && !defined(EMSCRIPTEN)

#include <windows.h>
#include <xinput.h>
#pragma comment(lib, "XInput.lib")

#define XPAD_COUNT 4

static char         xActive[XPAD_COUNT];
static XINPUT_STATE xStates[XPAD_COUNT];

/* Bits we OR'd onto control_status_p[i] on the previous apply(); used
 * to clear our own contribution before computing the new one. */
static U8 pad_owned[4];

/* Previous-frame X/Y state.
 *
 * X (bullet): we want the gun pose visible the whole time X is held, so
 * we emit FIRE|UP every frame. The bullet path latches
 * ricks[i].trigger=TRUE on first fire and only releases it on a frame
 * where FIRE is held but cs != FIRE|UP -- so on X release we emit FIRE
 * alone for one frame to clear the trigger, otherwise the second shot
 * would never fire.
 *
 * Y (bomb): edge-only. Otherwise holding Y would drop a new bomb every
 * time the previous one exploded. */
static U8 prev_X[4];
static U8 prev_Y[4];

static void
xinput_update(void)
{
	DWORD i, r;
	for (i = 0; i < XPAD_COUNT; i++)
	{
		ZeroMemory(&xStates[i], sizeof(XINPUT_STATE));
		r = XInputGetState(i, &xStates[i]);
		xActive[i] = (r == ERROR_SUCCESS) ? 1 : 0;
	}
}

/*
 * The keyboard scheme overloads CONTROL_FIRE with a direction to pick an
 * action (FIRE+UP = bullet, FIRE+DOWN = bomb, FIRE+LEFT/RIGHT = stop with
 * stick out). The gamepad gives each action its own face button:
 *   A = jump           -> CONTROL_UP
 *   B = use stick      -> CONTROL_FIRE + direction (stick if any, else facing)
 *   X = shoot bullet   -> CONTROL_FIRE | CONTROL_UP while held (gun visible),
 *                         FIRE for one frame on release to clear trigger latch
 *   Y = place dynamite -> CONTROL_FIRE | CONTROL_DOWN on press edge, then FIRE
 * X/Y priority over B so pushing the stick off-axis can't break the
 * bullet/bomb exact-equality check in e_rick.c.
 */
static U8
read_pad_bits(int i, int c, U8 include_global)
{
	U8 bits = 0;
	WORD wb;
	SHORT lx, ly;
	int dpad_left, dpad_right, dpad_up, dpad_down;
	int press_X, press_Y, press_B;
	int edge_Y, release_X;

	if (c < 0 || c >= XPAD_COUNT || !xActive[c]) {
		prev_X[i] = 0;
		prev_Y[i] = 0;
		return 0;
	}

	wb = xStates[c].Gamepad.wButtons;
	lx = xStates[c].Gamepad.sThumbLX;
	ly = xStates[c].Gamepad.sThumbLY;

	dpad_left  = (wb & XINPUT_GAMEPAD_DPAD_LEFT)  || lx < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
	dpad_right = (wb & XINPUT_GAMEPAD_DPAD_RIGHT) || lx >  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
	dpad_up    = (wb & XINPUT_GAMEPAD_DPAD_UP)    || ly >  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
	dpad_down  = (wb & XINPUT_GAMEPAD_DPAD_DOWN)  || ly < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;

	press_X = (wb & XINPUT_GAMEPAD_X) ? 1 : 0;
	press_Y = (wb & XINPUT_GAMEPAD_Y) ? 1 : 0;
	press_B = (wb & XINPUT_GAMEPAD_B) ? 1 : 0;
	release_X = !press_X && prev_X[i];
	edge_Y    = press_Y && !prev_Y[i];
	prev_X[i] = (U8)press_X;
	prev_Y[i] = (U8)press_Y;

	if (press_X) {
		/* Hold: keep the gun pose visible. Trigger is cleared on
		 * release_X (next branch). */
		bits = CONTROL_FIRE | CONTROL_UP;
	}
	else if (release_X) {
		/* One-frame FIRE-only pulse so e_rick.c:515 clears the trigger
		 * latch; without this a second tap on X would never fire. */
		bits = CONTROL_FIRE;
	}
	else if (edge_Y) {
		bits = CONTROL_FIRE | CONTROL_DOWN;     /* place bomb, one frame */
	}
	else if (press_Y) {
		/* Held Y: emit plain FIRE so the bomb path's trigger-equivalent
		 * (the bombs_get_ent guard) coexists with eventual re-fire. */
		bits = CONTROL_FIRE;
	}
	else if (press_B) {                         /* use stick (stop pose) */
		bits = CONTROL_FIRE;
		if (dpad_left)       bits |= CONTROL_LEFT;
		else if (dpad_right) bits |= CONTROL_RIGHT;
		else {
			/* No stick input -> use facing direction so B alone still
			 * triggers the stop pose. */
			if (ricks[i].dir == LEFT) bits |= CONTROL_LEFT;
			else                      bits |= CONTROL_RIGHT;
		}
	}
	else {
		if (dpad_left)  bits |= CONTROL_LEFT;
		if (dpad_right) bits |= CONTROL_RIGHT;
		if (dpad_up)    bits |= CONTROL_UP;
		if (dpad_down)  bits |= CONTROL_DOWN;
		if (wb & XINPUT_GAMEPAD_A) bits |= CONTROL_UP; /* jump */
	}

	/* Pause / end / exit are P1-only globals in the rest of the codebase;
	 * we only emit them for the player slot that owns those globals. */
	if (include_global)
	{
		if (wb & XINPUT_GAMEPAD_START) bits |= CONTROL_PAUSE;
		if (wb & XINPUT_GAMEPAD_BACK)  bits |= CONTROL_EXIT;
	}

	return bits;
}

void
sysxinput_init(void)
{
	memset(xActive,   0, sizeof(xActive));
	memset(pad_owned, 0, sizeof(pad_owned));
	memset(prev_X,    0, sizeof(prev_X));
	memset(prev_Y,    0, sizeof(prev_Y));
}

void
sysxinput_shutdown(void)
{
}

void
sysxinput_apply(void)
{
	int i, c;
	U8 newbits;

	xinput_update();

	for (i = 0; i < CONTROL_PLAYERS; i++)
	{
		c = sysxinput_player[i];
		newbits = read_pad_bits(i, c, (i == 0) ? 1 : 0);

		/* Clear exactly the bits we set last frame, then OR in this
		 * frame's. Keyboard bits we didn't set are left untouched. */
		control_status_p[i] = (U8)((control_status_p[i] & ~pad_owned[i]) | newbits);
		pad_owned[i] = newbits;

		/* Mirror what sysevt does for keyboard input: a P1 fire/pause/etc.
		 * also drives the menu-progress sentinel. */
		if (i == 0 && newbits != 0)
			control_last = newbits;
	}
}

#else /* not Windows: stubs */

void sysxinput_init(void)     { }
void sysxinput_shutdown(void) { }
void sysxinput_apply(void)    { }

#endif

/* eof */
