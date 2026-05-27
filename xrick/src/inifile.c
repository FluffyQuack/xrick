/*
 * xrick/src/inifile.c
 *
 * Minimal "Key = Value" config loader. Whitespace around key and value
 * is trimmed; ';' and '#' begin a line comment; blank lines and lines
 * without '=' are ignored. Unknown keys are ignored too.
 */

#include "inifile.h"

#include "system.h"
#include "syssnd.h" /* SYSSND_MAXVOL */
#include "e_rick.h" /* RICK_MAX */
#include "sysxinput.h" /* sysxinput_player */
#include "sysdinput.h" /* sysdinput_player */
#include "syskbd.h"    /* syskbd_players (we mutate it at load time) */
#include "game.h" /* game_interpolate */

#include <SDL.h>       /* SDL_SCANCODE_* used by the VK->scancode table */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __MSVC__
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

int inifile_playerCount = 1;
int inifile_linearFilter = 1;
int inifile_interpolate = 1;
int inifile_scale = 2;
U8 inifile_volume = SYSSND_MAXVOL;
int inifile_hueShift[8] = { 0, 175, 255, 60, 220, 300, 30, 330 };
int inifile_xinputPlayer[8] = { 0, 1, 2, 3, -1, -1, -1, -1 };
int inifile_dinputPlayer[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
int inifile_smartPadMapping[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
int inifile_realtimeScroll = 1;
int inifile_scrollCatchupFrames = 8;
int inifile_fixFallLanding = 1;
int inifile_xinputDisableUp[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
int inifile_dinputDisableUp[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

/* Defaults: XInput A/B/X/Y for jump/stick/shoot/bomb on every player. */
unsigned int inifile_xinputBtn[8][4] = {
	{ 0x1000, 0x2000, 0x4000, 0x8000 },
	{ 0x1000, 0x2000, 0x4000, 0x8000 },
	{ 0x1000, 0x2000, 0x4000, 0x8000 },
	{ 0x1000, 0x2000, 0x4000, 0x8000 },
	{ 0x1000, 0x2000, 0x4000, 0x8000 },
	{ 0x1000, 0x2000, 0x4000, 0x8000 },
	{ 0x1000, 0x2000, 0x4000, 0x8000 },
	{ 0x1000, 0x2000, 0x4000, 0x8000 }
};

/* Defaults: DInput button indices 0/1/2/3 for jump/stick/shoot/bomb. */
int inifile_dinputBtn[8][4] = {
	{ 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 },
	{ 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }
};

/* Defaults mirror the static syskbd_players[] table, but expressed as
 * Windows VK codes so the save round-trips cleanly:
 *   P0: arrow keys + Space        (VK_UP/DOWN/LEFT/RIGHT/SPACE)
 *   P1: WASD + Left Shift         ('W','S','A','D'/VK_LSHIFT)
 *   P2: IJKL + Return             ('I','K','J','L'/VK_RETURN) */
int inifile_keyBinding[3][5] = {
	{ 0x26, 0x28, 0x25, 0x27, 0x20 },
	{ 'W',  'S',  'A',  'D',  0xA0 },
	{ 'I',  'K',  'J',  'L',  0x0D }
};
/* Default hat row count (hat covers the top of Rick's sprite). Crouching
 * extends it by +5; flying-into-foreground (zombie) trims it by -2. */

/*
 * Convert a Windows Virtual-Key code to the SDL scancode the game's
 * keyboard dispatcher actually matches against. Returns
 * SDL_SCANCODE_UNKNOWN (0) for codes we don't have in our table -- the
 * binding then acts as "disabled", which is the safe behaviour. The VK
 * codes used here are the Win32 standard values (documented by MS); we
 * inline them rather than pulling in <windows.h>.
 */
static int
vk_to_sdl_scancode(int vk)
{
	if (vk >= 'A' && vk <= 'Z') return SDL_SCANCODE_A + (vk - 'A');
	if (vk >= '1' && vk <= '9') return SDL_SCANCODE_1 + (vk - '1');
	if (vk == '0')              return SDL_SCANCODE_0;
	if (vk >= 0x70 && vk <= 0x7B) return SDL_SCANCODE_F1 + (vk - 0x70);   /* F1..F12 */
	if (vk == 0x60)             return SDL_SCANCODE_KP_0;                  /* NUMPAD0 */
	if (vk >= 0x61 && vk <= 0x69) return SDL_SCANCODE_KP_1 + (vk - 0x61); /* NUMPAD1..9 */
	switch (vk) {
		case 0x08: return SDL_SCANCODE_BACKSPACE;
		case 0x09: return SDL_SCANCODE_TAB;
		case 0x0D: return SDL_SCANCODE_RETURN;
		case 0x10: return SDL_SCANCODE_LSHIFT;     /* SHIFT (generic) */
		case 0x11: return SDL_SCANCODE_LCTRL;      /* CONTROL */
		case 0x12: return SDL_SCANCODE_LALT;       /* MENU/ALT */
		case 0x13: return SDL_SCANCODE_PAUSE;
		case 0x14: return SDL_SCANCODE_CAPSLOCK;
		case 0x1B: return SDL_SCANCODE_ESCAPE;
		case 0x20: return SDL_SCANCODE_SPACE;
		case 0x21: return SDL_SCANCODE_PAGEUP;
		case 0x22: return SDL_SCANCODE_PAGEDOWN;
		case 0x23: return SDL_SCANCODE_END;
		case 0x24: return SDL_SCANCODE_HOME;
		case 0x25: return SDL_SCANCODE_LEFT;
		case 0x26: return SDL_SCANCODE_UP;
		case 0x27: return SDL_SCANCODE_RIGHT;
		case 0x28: return SDL_SCANCODE_DOWN;
		case 0x2D: return SDL_SCANCODE_INSERT;
		case 0x2E: return SDL_SCANCODE_DELETE;
		case 0x5B: return SDL_SCANCODE_LGUI;
		case 0x5C: return SDL_SCANCODE_RGUI;
		case 0x6A: return SDL_SCANCODE_KP_MULTIPLY;
		case 0x6B: return SDL_SCANCODE_KP_PLUS;
		case 0x6D: return SDL_SCANCODE_KP_MINUS;
		case 0x6E: return SDL_SCANCODE_KP_PERIOD;
		case 0x6F: return SDL_SCANCODE_KP_DIVIDE;
		case 0x90: return SDL_SCANCODE_NUMLOCKCLEAR;
		case 0x91: return SDL_SCANCODE_SCROLLLOCK;
		case 0xA0: return SDL_SCANCODE_LSHIFT;
		case 0xA1: return SDL_SCANCODE_RSHIFT;
		case 0xA2: return SDL_SCANCODE_LCTRL;
		case 0xA3: return SDL_SCANCODE_RCTRL;
		case 0xA4: return SDL_SCANCODE_LALT;
		case 0xA5: return SDL_SCANCODE_RALT;
		case 0xBA: return SDL_SCANCODE_SEMICOLON;
		case 0xBB: return SDL_SCANCODE_EQUALS;
		case 0xBC: return SDL_SCANCODE_COMMA;
		case 0xBD: return SDL_SCANCODE_MINUS;
		case 0xBE: return SDL_SCANCODE_PERIOD;
		case 0xBF: return SDL_SCANCODE_SLASH;
		case 0xC0: return SDL_SCANCODE_GRAVE;
		case 0xDB: return SDL_SCANCODE_LEFTBRACKET;
		case 0xDC: return SDL_SCANCODE_BACKSLASH;
		case 0xDD: return SDL_SCANCODE_RIGHTBRACKET;
		case 0xDE: return SDL_SCANCODE_APOSTROPHE;
		default:   return SDL_SCANCODE_UNKNOWN;
	}
}

/* Parse an XInput face-button name (A/B/X/Y/LB/RB) into the matching
 * XINPUT_GAMEPAD_* WORD mask. Returns 0 for unknown names. */
static unsigned int
parse_xinput_btn_name(const char *s)
{
	if (!strcasecmp(s, "A"))    return 0x1000;
	if (!strcasecmp(s, "B"))    return 0x2000;
	if (!strcasecmp(s, "X"))    return 0x4000;
	if (!strcasecmp(s, "Y"))    return 0x8000;
	if (!strcasecmp(s, "LB"))   return 0x0100;
	if (!strcasecmp(s, "RB"))   return 0x0200;
	return 0;
}

/* Inverse of parse_xinput_btn_name(). Returns "?" if the mask is not
 * one we recognise (we never write that from defaults, so it indicates
 * a corrupted runtime value). */
static const char *
xinput_btn_name(unsigned int mask)
{
	switch (mask) {
		case 0x1000: return "A";
		case 0x2000: return "B";
		case 0x4000: return "X";
		case 0x8000: return "Y";
		case 0x0100: return "LB";
		case 0x0200: return "RB";
		default:     return "?";
	}
}

/* Match keys of the form "<prefix><digit>" where digit is in
 * [0, max_index]. Returns the digit index, or -1 if no match. */
static int
match_indexed_key(const char *key, const char *prefix, int max_index)
{
	size_t n = strlen(prefix);
	if (strncasecmp(key, prefix, n) != 0) return -1;
	if (strlen(key) != n + 1) return -1;
	if (key[n] < '0' || key[n] > '0' + max_index) return -1;
	return key[n] - '0';
}

/* Apply one of the inifile_keyBinding values to syskbd_players[] by
 * converting the stored VK code to the SDL scancode the dispatcher
 * compares against. */
static void
apply_kbd_binding(int player_idx, int field_idx, int vk)
{
	U8 sc = (U8)vk_to_sdl_scancode(vk);
	switch (field_idx) {
		case 0: syskbd_players[player_idx].up    = sc; break;
		case 1: syskbd_players[player_idx].down  = sc; break;
		case 2: syskbd_players[player_idx].left  = sc; break;
		case 3: syskbd_players[player_idx].right = sc; break;
		case 4: syskbd_players[player_idx].fire  = sc; break;
		default: break;
	}
}

static void
trim(char *s)
{
	char *p, *end;
	size_t len;

	p = s;
	while (*p && isspace((unsigned char)*p)) p++;
	if (p != s) {
		len = strlen(p) + 1;
		memmove(s, p, len);
	}
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1])) {
		end--;
		*end = '\0';
	}
}

void
inifile_load(const char *path)
{
	FILE *f;
	char line[256];

	f = fopen(path, "r");
	if (!f) {
		sys_printf("xrick/inifile: '%s' not found, using defaults\n", path);
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		char *eq, *key, *val, *cmt;
		int n;

		cmt = strchr(line, ';');
		if (cmt) *cmt = '\0';
		cmt = strchr(line, '#');
		if (cmt) *cmt = '\0';

		eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		key = line;
		val = eq + 1;
		trim(key);
		trim(val);
		if (*key == '\0') continue;

		if (!strcasecmp(key, "PlayerCount")) {
			n = atoi(val);
			if (n < 1) n = 1;
			if (n > RICK_MAX) n = RICK_MAX;
			inifile_playerCount = n;
		}
		else if (!strcasecmp(key, "LinearFilter")) {
			inifile_linearFilter = atoi(val) ? 1 : 0;
		}
		else if (!strcasecmp(key, "Interpolate")) {
			inifile_interpolate = atoi(val) ? 1 : 0;
			game_interpolate = (U8)inifile_interpolate;
		}
		else if (!strcasecmp(key, "Scale")) {
			n = atoi(val);
			if (n < 1) n = 1;
			if (n > 16) n = 16;
			inifile_scale = n;
		}
		else if (!strcasecmp(key, "Volume")) {
			n = atoi(val);
			if (n < 0) n = 0;
			if (n > SYSSND_MAXVOL) n = SYSSND_MAXVOL;
			inifile_volume = (U8)n;
		}
		else if (strlen(key) == 9 && !strncasecmp(key, "HueShift", 8) &&
		         key[8] >= '0' && key[8] <= '7') {
			int idx = key[8] - '0';
			n = atoi(val);
			while (n < 0) n += 360;
			n %= 360;
			inifile_hueShift[idx] = n;
		}
		else if (strlen(key) == 13 && !strncasecmp(key, "XinputPlayer", 12) &&
		         key[12] >= '0' && key[12] <= '7') {
			int idx = key[12] - '0';
			n = atoi(val);
			if (n < -1) n = -1;
			if (n > 3)  n = 3;
			inifile_xinputPlayer[idx] = n;
			sysxinput_player[idx] = n;
		}
		else if (strlen(key) == 13 && !strncasecmp(key, "DinputPlayer", 12) &&
		         key[12] >= '0' && key[12] <= '7') {
			int idx = key[12] - '0';
			n = atoi(val);
			if (n < -1) n = -1;
			if (n > 3)  n = 3;
			inifile_dinputPlayer[idx] = n;
			sysdinput_player[idx] = n;
		}
		else if (strlen(key) == 16 && !strncasecmp(key, "SmartPadMapping", 15) &&
		         key[15] >= '0' && key[15] <= '7') {
			int idx = key[15] - '0';
			inifile_smartPadMapping[idx] = atoi(val) ? 1 : 0;
		}
		else if (!strcasecmp(key, "RealtimeScroll")) {
			inifile_realtimeScroll = atoi(val) ? 1 : 0;
			game_realtime_scroll = (U8)inifile_realtimeScroll;
		}
		else if (!strcasecmp(key, "ScrollCatchupFrames")) {
			n = atoi(val);
			if (n < 1)  n = 1;
			if (n > 30) n = 30;
			inifile_scrollCatchupFrames = n;
		}
		else if (!strcasecmp(key, "FixFallLanding")) {
			inifile_fixFallLanding = atoi(val) ? 1 : 0;
		}
		else if ((n = match_indexed_key(key, "XinputDisableUp", 7)) >= 0) {
			inifile_xinputDisableUp[n] = atoi(val) ? 1 : 0;
		}
		else if ((n = match_indexed_key(key, "DinputDisableUp", 7)) >= 0) {
			inifile_dinputDisableUp[n] = atoi(val) ? 1 : 0;
		}
		else if ((n = match_indexed_key(key, "XinputJumpBtn",  7)) >= 0 ||
		         (n = match_indexed_key(key, "XinputStickBtn", 7)) >= 0 ||
		         (n = match_indexed_key(key, "XinputShootBtn", 7)) >= 0 ||
		         (n = match_indexed_key(key, "XinputBombBtn",  7)) >= 0) {
			/* Figure out which action this key configured (we lost which
			 * branch matched -- re-check the prefix to find the slot). */
			int slot = -1;
			if      (!strncasecmp(key, "XinputJumpBtn",  13)) slot = 0;
			else if (!strncasecmp(key, "XinputStickBtn", 14)) slot = 1;
			else if (!strncasecmp(key, "XinputShootBtn", 14)) slot = 2;
			else if (!strncasecmp(key, "XinputBombBtn",  13)) slot = 3;
			if (slot >= 0)
				inifile_xinputBtn[n][slot] = parse_xinput_btn_name(val);
		}
		else if ((n = match_indexed_key(key, "DinputJumpBtn",  7)) >= 0 ||
		         (n = match_indexed_key(key, "DinputStickBtn", 7)) >= 0 ||
		         (n = match_indexed_key(key, "DinputShootBtn", 7)) >= 0 ||
		         (n = match_indexed_key(key, "DinputBombBtn",  7)) >= 0) {
			int slot = -1;
			int b;
			if      (!strncasecmp(key, "DinputJumpBtn",  13)) slot = 0;
			else if (!strncasecmp(key, "DinputStickBtn", 14)) slot = 1;
			else if (!strncasecmp(key, "DinputShootBtn", 14)) slot = 2;
			else if (!strncasecmp(key, "DinputBombBtn",  13)) slot = 3;
			if (slot >= 0) {
				b = atoi(val);
				if (b < -1) b = -1;
				if (b > 31) b = 31;
				inifile_dinputBtn[n][slot] = b;
			}
		}
		else if (!strncasecmp(key, "KeyP", 4) && strlen(key) > 5 &&
		         key[4] >= '0' && key[4] <= '2') {
			int idx = key[4] - '0';
			const char *suffix = key + 5;
			int field = -1;
			int vk;
			if      (!strcasecmp(suffix, "Up"))     field = 0;
			else if (!strcasecmp(suffix, "Down"))   field = 1;
			else if (!strcasecmp(suffix, "Left"))   field = 2;
			else if (!strcasecmp(suffix, "Right"))  field = 3;
			else if (!strcasecmp(suffix, "Action")) field = 4;
			if (field >= 0) {
				vk = atoi(val);
				if (vk < 0)   vk = 0;
				if (vk > 255) vk = 255;
				inifile_keyBinding[idx][field] = vk;
				apply_kbd_binding(idx, field, vk);
			} else {
				sys_printf("xrick/inifile: unknown KeyP suffix '%s'\n", suffix);
			}
		}
		else {
			sys_printf("xrick/inifile: unknown key '%s'\n", key);
		}
	}

	fclose(f);
	sys_printf("xrick/inifile: loaded '%s' (PlayerCount=%d, LinearFilter=%d, Scale=%d, Volume=%d)\n",
	           path, inifile_playerCount, inifile_linearFilter, inifile_scale, (int)inifile_volume);
}

void
inifile_save(const char *path)
{
	FILE *f;
	int pc;
	int i;
	static const char * const kbd_field_name[5] = { "Up", "Down", "Left", "Right", "Action" };

	/* Snapshot live state into the ini-backed globals so the file we
	 * write matches what the player just had on screen. */
	pc = (int)rick_count;
	if (pc < 1) pc = 1;
	if (pc > RICK_MAX) pc = RICK_MAX;
	inifile_playerCount = pc;
	inifile_interpolate = game_interpolate ? 1 : 0;
	inifile_realtimeScroll = game_realtime_scroll ? 1 : 0;

	f = fopen(path, "w");
	if (!f) {
		sys_printf("xrick/inifile: could not write '%s'\n", path);
		return;
	}

	fprintf(f, "; Quantity of active players (change in-game with 1..8 keys)\n");
	fprintf(f, "PlayerCount = %d\n\n", inifile_playerCount);
	fprintf(f, "; 1 = Bilinear filter, 0 = Nearest-neighbour filter\n");
	fprintf(f, "LinearFilter = %d\n\n", inifile_linearFilter);
	fprintf(f, "; 1 = Smooth interpolated rendering, 0 = Original 25 fps look (toggle in-game with F10)\n");
	fprintf(f, "Interpolate = %d\n\n", inifile_interpolate);
	fprintf(f, "; Scale multiplier for graphics and window\n");
	fprintf(f, "Scale = %d\n\n", inifile_scale);
	fprintf(f, "; Overall sound volume\n");
	fprintf(f, "Volume = %d\n\n", (int)inifile_volume);
	fprintf(f, "; Colour hue shift for each player's hat\n");
	fprintf(f, "HueShift0 = %d\n", inifile_hueShift[0]);
	fprintf(f, "HueShift1 = %d\n", inifile_hueShift[1]);
	fprintf(f, "HueShift2 = %d\n", inifile_hueShift[2]);
	fprintf(f, "HueShift3 = %d\n", inifile_hueShift[3]);
	fprintf(f, "HueShift4 = %d\n", inifile_hueShift[4]);
	fprintf(f, "HueShift5 = %d\n", inifile_hueShift[5]);
	fprintf(f, "HueShift6 = %d\n", inifile_hueShift[6]);
	fprintf(f, "HueShift7 = %d\n\n", inifile_hueShift[7]);
	fprintf(f, "; Player xinput/Xbox controller mappings (-1 disables, 0..3 selects XInput slot)\n");
	fprintf(f, "XinputPlayer0 = %d\n", inifile_xinputPlayer[0]);
	fprintf(f, "XinputPlayer1 = %d\n", inifile_xinputPlayer[1]);
	fprintf(f, "XinputPlayer2 = %d\n", inifile_xinputPlayer[2]);
	fprintf(f, "XinputPlayer3 = %d\n", inifile_xinputPlayer[3]);
	fprintf(f, "XinputPlayer4 = %d\n", inifile_xinputPlayer[4]);
	fprintf(f, "XinputPlayer5 = %d\n", inifile_xinputPlayer[5]);
	fprintf(f, "XinputPlayer6 = %d\n", inifile_xinputPlayer[6]);
	fprintf(f, "XinputPlayer7 = %d\n\n", inifile_xinputPlayer[7]);
	fprintf(f, "; Player DirectInput controller mappings (-1 disables, 0..3 selects DI device slot\n");
	fprintf(f, "DinputPlayer0 = %d\n", inifile_dinputPlayer[0]);
	fprintf(f, "DinputPlayer1 = %d\n", inifile_dinputPlayer[1]);
	fprintf(f, "DinputPlayer2 = %d\n", inifile_dinputPlayer[2]);
	fprintf(f, "DinputPlayer3 = %d\n", inifile_dinputPlayer[3]);
	fprintf(f, "DinputPlayer4 = %d\n", inifile_dinputPlayer[4]);
	fprintf(f, "DinputPlayer5 = %d\n", inifile_dinputPlayer[5]);
	fprintf(f, "DinputPlayer6 = %d\n", inifile_dinputPlayer[6]);
	fprintf(f, "DinputPlayer7 = %d\n\n", inifile_dinputPlayer[7]);
	fprintf(f, "; Per-player gamepad face-button mapping: 1 = smart (A=jump, B=stick, X=shoot, Y=bomb), 0 = generic (all face buttons act as fire, like keyboard)\n");
	fprintf(f, "SmartPadMapping0 = %d\n", inifile_smartPadMapping[0]);
	fprintf(f, "SmartPadMapping1 = %d\n", inifile_smartPadMapping[1]);
	fprintf(f, "SmartPadMapping2 = %d\n", inifile_smartPadMapping[2]);
	fprintf(f, "SmartPadMapping3 = %d\n", inifile_smartPadMapping[3]);
	fprintf(f, "SmartPadMapping4 = %d\n", inifile_smartPadMapping[4]);
	fprintf(f, "SmartPadMapping5 = %d\n", inifile_smartPadMapping[5]);
	fprintf(f, "SmartPadMapping6 = %d\n", inifile_smartPadMapping[6]);
	fprintf(f, "SmartPadMapping7 = %d\n\n", inifile_smartPadMapping[7]);
	fprintf(f, "; 1 = Atomic shift + camera catch-up (gameplay never pauses), 0 = Original 8-tick paused scroll (toggle in-game with F11)\n");
	fprintf(f, "RealtimeScroll = %d\n\n", inifile_realtimeScroll);
	fprintf(f, "; Number of ticks the camera takes to visually catch up after a real-time scroll (1..30)\n");
	fprintf(f, "ScrollCatchupFrames = %d\n\n", inifile_scrollCatchupFrames);
	fprintf(f, "; 1 = Fix legacy bug where falling fast could cause a brief mid-air landing just before the ground, 0 = Original behaviour\n");
	fprintf(f, "FixFallLanding = %d\n\n", inifile_fixFallLanding);

	fprintf(f, "; 1 = ignore Up direction from this player's XInput pad (dpad + stick); 0 = enabled\n");
	for (i = 0; i < 8; i++)
		fprintf(f, "XinputDisableUp%d = %d\n", i, inifile_xinputDisableUp[i]);
	fprintf(f, "\n");

	fprintf(f, "; 1 = ignore Up direction from this player's DirectInput pad (dpad + stick); 0 = enabled\n");
	for (i = 0; i < 8; i++)
		fprintf(f, "DinputDisableUp%d = %d\n", i, inifile_dinputDisableUp[i]);
	fprintf(f, "\n");

	fprintf(f, "; Per-player XInput face-button assignment. Valid names: A, B, X, Y, LB, RB.\n");
	fprintf(f, "; D-pad and analog stick are always movement.\n");
	for (i = 0; i < 8; i++) {
		fprintf(f, "XinputJumpBtn%d  = %s\n", i, xinput_btn_name(inifile_xinputBtn[i][0]));
		fprintf(f, "XinputStickBtn%d = %s\n", i, xinput_btn_name(inifile_xinputBtn[i][1]));
		fprintf(f, "XinputShootBtn%d = %s\n", i, xinput_btn_name(inifile_xinputBtn[i][2]));
		fprintf(f, "XinputBombBtn%d  = %s\n", i, xinput_btn_name(inifile_xinputBtn[i][3]));
	}
	fprintf(f, "\n");

	fprintf(f, "; Per-player DirectInput face-button assignment (button index 0..31, -1 disables).\n");
	fprintf(f, "; D-pad and analog stick are always movement.\n");
	for (i = 0; i < 8; i++) {
		fprintf(f, "DinputJumpBtn%d  = %d\n", i, inifile_dinputBtn[i][0]);
		fprintf(f, "DinputStickBtn%d = %d\n", i, inifile_dinputBtn[i][1]);
		fprintf(f, "DinputShootBtn%d = %d\n", i, inifile_dinputBtn[i][2]);
		fprintf(f, "DinputBombBtn%d  = %d\n", i, inifile_dinputBtn[i][3]);
	}
	fprintf(f, "\n");
	fprintf(f, "; Per-player keyboard bindings for P0..P2 (Windows Virtual-Key codes in decimal).\n");
	fprintf(f, "; Default controls:\n");
	fprintf(f, "; Player0 = Directional keys + Space\n");
	fprintf(f, "; Player1 = WASD + Shift\n");
	fprintf(f, "; Player2 = IJKL + Return\n\n");
	fprintf(f, "; 0 disables a binding. Common codes: Up=38 Down=40 Left=37 Right=39 Space=32\n");
	fprintf(f, "; Enter=13 LShift=160 RShift=161 Tab=9 Esc=27 Letters: 'A'=65..'Z'=90 Digits: '0'=48..'9'=57\n");
	fprintf(f, "; https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes\n");
	for (i = 0; i < 3; i++) {
		int j;
		for (j = 0; j < 5; j++)
			fprintf(f, "KeyP%d%s = %d\n", i, kbd_field_name[j], inifile_keyBinding[i][j]);
	}
	fprintf(f, "\n");

	fclose(f);
	sys_printf("xrick/inifile: saved '%s'\n", path);
}

/* eof */
