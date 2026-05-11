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

static U8
read_pad_bits(int c, U8 include_global)
{
	U8 bits = 0;
	WORD wb;
	SHORT lx, ly;

	if (c < 0 || c >= XPAD_COUNT || !xActive[c])
		return 0;

	wb = xStates[c].Gamepad.wButtons;
	lx = xStates[c].Gamepad.sThumbLX;
	ly = xStates[c].Gamepad.sThumbLY;

	if ((wb & XINPUT_GAMEPAD_DPAD_LEFT)  || lx < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) bits |= CONTROL_LEFT;
	if ((wb & XINPUT_GAMEPAD_DPAD_RIGHT) || lx >  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) bits |= CONTROL_RIGHT;
	if ((wb & XINPUT_GAMEPAD_A)    || ly >  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) bits |= CONTROL_UP;
	if ((wb & XINPUT_GAMEPAD_DPAD_DOWN)  || ly < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) bits |= CONTROL_DOWN;
	if (wb & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y))
		bits |= CONTROL_FIRE;

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
		newbits = read_pad_bits(c, (i == 0) ? 1 : 0);

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
