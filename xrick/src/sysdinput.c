/*
 * xrick/src/sysdinput.c
 *
 * DirectInput controller support. Mirrors sysxinput.c: enumerate up to
 * 4 attached game controllers, poll each once per sysevt_poll, and OR
 * the resulting CONTROL_* bitset into the owning player's slot.
 *
 * XInput devices are filtered out at enumeration time -- without that,
 * an Xbox-style pad would appear on both sides and either get double-
 * bound (if the user maps the same slot to both modules) or eat extra
 * inputs the user did not intend.
 *
 * Button layout matches sysxinput.c on a standard DirectInput gamepad
 * (buttons 0..3 = A/B/X/Y, 6/7 = Back/Start). Sticks and D-pad both
 * drive the directional bits; left stick range is rescaled to -1000..
 * 1000 via DIPROP_RANGE for symmetric deadzone math.
 */

#include "sysdinput.h"
#include "control.h"
#include "system.h"
#include "e_rick.h"
#include "game.h"
#include "inifile.h"

#include <string.h>

/* All disabled by default. The XInput module defaults to identity, so
 * if both modules saw the same pad we'd double-bind it. The user opts
 * in via DInputPlayerN in the ini. */
int sysdinput_player[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

#if defined(_WIN32) && !defined(EMSCRIPTEN)

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <wchar.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

#define DPAD_COUNT 4
#define DEADZONE   400   /* axis range is ±1000 after DIPROP_RANGE */

static LPDIRECTINPUT8       g_di = NULL;
static LPDIRECTINPUTDEVICE8 g_devs[DPAD_COUNT];
static int                  g_devCount = 0;
static char                 g_active[DPAD_COUNT];
static DIJOYSTATE2          g_states[DPAD_COUNT];

/* Bits we OR'd onto control_status_p[i] on the previous apply(); used
 * to clear our own contribution before computing the new one without
 * stomping keyboard or XInput bits. */
static U8 pad_owned[8];

/* See sysxinput.c for the rationale; same trigger-latch / edge-detect
 * semantics for the X (bullet) and Y (bomb) buttons. */
static U8 prev_X[8];
static U8 prev_Y[8];

/*
 * MS' recommended XInput-detection trick: every XInput-capable HID
 * device has "IG_" in its device interface path. Avoids pulling in WMI
 * just to filter the duplicate enumeration.
 */
static BOOL
is_xinput_device(LPDIRECTINPUTDEVICE8 dev)
{
	DIPROPGUIDANDPATH p;
	ZeroMemory(&p, sizeof(p));
	p.diph.dwSize       = sizeof(DIPROPGUIDANDPATH);
	p.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	p.diph.dwObj        = 0;
	p.diph.dwHow        = DIPH_DEVICE;
	if (FAILED(IDirectInputDevice8_GetProperty(dev, DIPROP_GUIDANDPATH, &p.diph)))
		return FALSE;
	return (wcsstr(p.wszPath, L"ig_") != NULL) || (wcsstr(p.wszPath, L"IG_") != NULL);
}

static BOOL CALLBACK
set_axis_range_cb(LPCDIDEVICEOBJECTINSTANCE inst, LPVOID ctx)
{
	LPDIRECTINPUTDEVICE8 dev = (LPDIRECTINPUTDEVICE8)ctx;
	DIPROPRANGE r;
	r.diph.dwSize       = sizeof(DIPROPRANGE);
	r.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	r.diph.dwHow        = DIPH_BYID;
	r.diph.dwObj        = inst->dwType;
	r.lMin              = -1000;
	r.lMax              =  1000;
	IDirectInputDevice8_SetProperty(dev, DIPROP_RANGE, &r.diph);
	return DIENUM_CONTINUE;
}

static BOOL CALLBACK
enum_devices_cb(LPCDIDEVICEINSTANCE inst, LPVOID ctx)
{
	LPDIRECTINPUTDEVICE8 dev = NULL;
	(void)ctx;

	if (g_devCount >= DPAD_COUNT)
		return DIENUM_STOP;

	if (FAILED(IDirectInput8_CreateDevice(g_di, &inst->guidInstance, &dev, NULL)))
		return DIENUM_CONTINUE;

	if (is_xinput_device(dev)) {
		IDirectInputDevice8_Release(dev);
		return DIENUM_CONTINUE;
	}

	if (FAILED(IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick2))) {
		IDirectInputDevice8_Release(dev);
		return DIENUM_CONTINUE;
	}

	/* Background + non-exclusive is what we want: input keeps flowing
	 * even when SDL's window doesn't have focus (Alt-Tabbed pause
	 * scenarios in particular). */
	IDirectInputDevice8_SetCooperativeLevel(dev, GetDesktopWindow(),
		DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);

	IDirectInputDevice8_EnumObjects(dev, set_axis_range_cb, dev, DIDFT_AXIS);
	IDirectInputDevice8_Acquire(dev);

	g_devs[g_devCount]   = dev;
	g_active[g_devCount] = 1;
	g_devCount++;
	return DIENUM_CONTINUE;
}

static void
dinput_update(void)
{
	int i;
	HRESULT hr;
	for (i = 0; i < g_devCount; i++) {
		if (!g_devs[i]) { g_active[i] = 0; continue; }

		hr = IDirectInputDevice8_Poll(g_devs[i]);
		if (FAILED(hr)) {
			/* DIERR_INPUTLOST / DIERR_NOTACQUIRED -- try to reacquire
			 * after window focus changes. */
			IDirectInputDevice8_Acquire(g_devs[i]);
			IDirectInputDevice8_Poll(g_devs[i]);
		}

		ZeroMemory(&g_states[i], sizeof(DIJOYSTATE2));
		hr = IDirectInputDevice8_GetDeviceState(g_devs[i],
			sizeof(DIJOYSTATE2), &g_states[i]);
		g_active[i] = SUCCEEDED(hr) ? 1 : 0;
		if (!g_active[i])
			IDirectInputDevice8_Acquire(g_devs[i]);
	}
}

/*
 * Same logical button assignment as sysxinput.c -- see that file for
 * the FIRE-overload / trigger-latch reasoning. Button index choice
 * (0=A, 1=B, 2=X, 3=Y, 6=Back, 7=Start) matches the de-facto layout
 * of generic XInput-style pads when accessed via DirectInput, which
 * is what most third-party PC pads expose.
 */
static U8
read_pad_bits(int i, int c, U8 include_global)
{
	U8 bits = 0;
	DIJOYSTATE2 *s;
	int dpad_left, dpad_right, dpad_up, dpad_down;
	int press_X, press_Y, press_B;
	int edge_Y, release_X;
	DWORD pov;

	if (c < 0 || c >= g_devCount || !g_active[c]) {
		prev_X[i] = 0;
		prev_Y[i] = 0;
		return 0;
	}

	s = &g_states[c];

	dpad_left  = (s->lX < -DEADZONE);
	dpad_right = (s->lX >  DEADZONE);
	/* DI Y axis: negative = up, positive = down (opposite of XInput). */
	dpad_up    = (s->lY < -DEADZONE);
	dpad_down  = (s->lY >  DEADZONE);

	pov = s->rgdwPOV[0];
	if (LOWORD(pov) != 0xFFFF) {
		/* POV is in hundredths of a degree, 0 = up, clockwise. */
		if (pov >= 31500 || pov <=  4500) dpad_up    = 1;
		if (pov >=  4500 && pov <= 13500) dpad_right = 1;
		if (pov >= 13500 && pov <= 22500) dpad_down  = 1;
		if (pov >= 22500 && pov <= 31500) dpad_left  = 1;
	}

	/* Mirror of the XInput "DisableUp" behaviour -- see sysxinput.c. */
	if (inifile_dinputDisableUp[i]) dpad_up = 0;

	/* Per-player customizable face buttons. inifile_dinputBtn[i] holds
	 * 0-based DI button indices for jump/stick/shoot/bomb. -1 disables.
	 * Defaults are 0/1/2/3 = standard A/B/X/Y on most pads. */
	press_X = (inifile_dinputBtn[i][2] >= 0 && (s->rgbButtons[inifile_dinputBtn[i][2]] & 0x80)) ? 1 : 0;
	press_Y = (inifile_dinputBtn[i][3] >= 0 && (s->rgbButtons[inifile_dinputBtn[i][3]] & 0x80)) ? 1 : 0;
	press_B = (inifile_dinputBtn[i][1] >= 0 && (s->rgbButtons[inifile_dinputBtn[i][1]] & 0x80)) ? 1 : 0;
	release_X = !press_X && prev_X[i];
	edge_Y    = press_Y && !prev_Y[i];
	prev_X[i] = (U8)press_X;
	prev_Y[i] = (U8)press_Y;

	if (!inifile_smartPadMapping[i]) {
		/* Generic mode: A/B/X/Y all behave like the keyboard fire button.
		 * Direction comes from the dpad/stick; see sysxinput.c. */
		if (dpad_left)  bits |= CONTROL_LEFT;
		if (dpad_right) bits |= CONTROL_RIGHT;
		if (dpad_up)    bits |= CONTROL_UP;
		if (dpad_down)  bits |= CONTROL_DOWN;
		if ((inifile_dinputBtn[i][0] >= 0 && (s->rgbButtons[inifile_dinputBtn[i][0]] & 0x80)) ||
		    press_B || press_X || press_Y)
			bits |= CONTROL_FIRE;
		if (include_global) {
			if (s->rgbButtons[7] & 0x80) bits |= CONTROL_PAUSE;
			if (s->rgbButtons[6] & 0x80) bits |= CONTROL_EXIT;
		}
		return bits;
	}

	if (press_X) {
		bits = CONTROL_FIRE | CONTROL_UP;
	}
	else if (release_X) {
		bits = CONTROL_FIRE;
	}
	else if (edge_Y) {
		bits = CONTROL_FIRE | CONTROL_DOWN;
	}
	else if (press_Y) {
		bits = CONTROL_FIRE;
	}
	else if (press_B) {
		bits = CONTROL_FIRE;
		if (dpad_left)       bits |= CONTROL_LEFT;
		else if (dpad_right) bits |= CONTROL_RIGHT;
		else {
			if (ricks[i].dir == LEFT) bits |= CONTROL_LEFT;
			else                      bits |= CONTROL_RIGHT;
		}
	}
	else {
		if (dpad_left)  bits |= CONTROL_LEFT;
		if (dpad_right) bits |= CONTROL_RIGHT;
		if (dpad_up)    bits |= CONTROL_UP;
		if (dpad_down)  bits |= CONTROL_DOWN;
		if (inifile_dinputBtn[i][0] >= 0 && (s->rgbButtons[inifile_dinputBtn[i][0]] & 0x80))
			bits |= CONTROL_UP; /* jump */
	}

	if (include_global) {
		if (s->rgbButtons[7] & 0x80) bits |= CONTROL_PAUSE; /* Start */
		if (s->rgbButtons[6] & 0x80) bits |= CONTROL_EXIT;  /* Back  */
	}

	return bits;
}

void
sysdinput_init(void)
{
	HRESULT hr;

	memset(g_devs,    0, sizeof(g_devs));
	memset(g_active,  0, sizeof(g_active));
	memset(pad_owned, 0, sizeof(pad_owned));
	memset(prev_X,    0, sizeof(prev_X));
	memset(prev_Y,    0, sizeof(prev_Y));
	g_devCount = 0;

	hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
		&IID_IDirectInput8, (void**)&g_di, NULL);
	if (FAILED(hr)) {
		g_di = NULL;
		sys_printf("xrick/sysdinput: DirectInput8Create failed (0x%lx)\n", (unsigned long)hr);
		return;
	}

	IDirectInput8_EnumDevices(g_di, DI8DEVCLASS_GAMECTRL,
		enum_devices_cb, NULL, DIEDFL_ATTACHEDONLY);

	sys_printf("xrick/sysdinput: %d DirectInput controller(s) found\n", g_devCount);
}

void
sysdinput_shutdown(void)
{
	int i;
	for (i = 0; i < DPAD_COUNT; i++) {
		if (g_devs[i]) {
			IDirectInputDevice8_Unacquire(g_devs[i]);
			IDirectInputDevice8_Release(g_devs[i]);
			g_devs[i] = NULL;
		}
	}
	g_devCount = 0;
	if (g_di) {
		IDirectInput8_Release(g_di);
		g_di = NULL;
	}
}

void
sysdinput_apply(void)
{
	int i, c;
	U8 newbits;

	if (!g_di) return;

	dinput_update();

	for (i = 0; i < CONTROL_PLAYERS; i++) {
		c = sysdinput_player[i];
		newbits = read_pad_bits(i, c, (i == 0) ? 1 : 0);

		control_status_p[i] = (U8)((control_status_p[i] & ~pad_owned[i]) | newbits);
		pad_owned[i] = newbits;

		if (i == 0 && newbits != 0)
			control_last = newbits;
	}
}

#else /* not Windows: stubs */

void sysdinput_init(void)     { }
void sysdinput_shutdown(void) { }
void sysdinput_apply(void)    { }

#endif

/* eof */
