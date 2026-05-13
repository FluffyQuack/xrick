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
#include "game.h" /* game_interpolate */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __MSVC__
#define strcasecmp _stricmp
#endif

int inifile_playerCount = 1;
int inifile_linearFilter = 1;
int inifile_interpolate = 1;
int inifile_realtimeScroll = 1;
int inifile_scale = 2;
U8 inifile_volume = SYSSND_MAXVOL;
int inifile_hueShift[4] = { 0, 120, 240, 60 };
int inifile_xinputPlayer[4] = { 0, 1, 2, 3 };
/* Default hat row count (hat covers the top of Rick's sprite). Crouching
 * extends it by +5; flying-into-foreground (zombie) trims it by -2. */

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
		else if (!strcasecmp(key, "RealtimeScroll")) {
			inifile_realtimeScroll = atoi(val) ? 1 : 0;
			game_realtimeScroll = (U8)inifile_realtimeScroll;
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
		else if (!strcasecmp(key, "HueShift0") ||
		         !strcasecmp(key, "HueShift1") ||
		         !strcasecmp(key, "HueShift2") ||
		         !strcasecmp(key, "HueShift3")) {
			int idx = key[8] - '0';
			n = atoi(val);
			while (n < 0) n += 360;
			n %= 360;
			inifile_hueShift[idx] = n;
		}
		else if (!strcasecmp(key, "XinputPlayer0") ||
		         !strcasecmp(key, "XinputPlayer1") ||
		         !strcasecmp(key, "XinputPlayer2") ||
		         !strcasecmp(key, "XinputPlayer3")) {
			int idx = key[12] - '0';
			n = atoi(val);
			if (n < -1) n = -1;
			if (n > 3)  n = 3;
			inifile_xinputPlayer[idx] = n;
			sysxinput_player[idx] = n;
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

	/* Snapshot live state into the ini-backed globals so the file we
	 * write matches what the player just had on screen. */
	pc = (int)rick_count;
	if (pc < 1) pc = 1;
	if (pc > RICK_MAX) pc = RICK_MAX;
	inifile_playerCount = pc;
	inifile_interpolate = game_interpolate ? 1 : 0;
	inifile_realtimeScroll = game_realtimeScroll ? 1 : 0;

	f = fopen(path, "w");
	if (!f) {
		sys_printf("xrick/inifile: could not write '%s'\n", path);
		return;
	}

	fprintf(f, "; Quantity of active players (change in-game with 1,2,3,4 keys)\n");
	fprintf(f, "PlayerCount = %d\n\n", inifile_playerCount);
	fprintf(f, "; 1 = Bilinear filter, 0 = Nearest-neighbour filter\n");
	fprintf(f, "LinearFilter = %d\n\n", inifile_linearFilter);
	fprintf(f, "; 1 = Smooth interpolated rendering, 0 = Original 25 fps look (toggle in-game with F10)\n");
	fprintf(f, "Interpolate = %d\n\n", inifile_interpolate);
	fprintf(f, "; 1 = Camera glides while gameplay keeps running, 0 = Original 8-tick gameplay freeze per scroll\n");
	fprintf(f, "RealtimeScroll = %d\n\n", inifile_realtimeScroll);
	fprintf(f, "; Scale multiplier for graphics and window\n");
	fprintf(f, "Scale = %d\n\n", inifile_scale);
	fprintf(f, "; Overall sound volume\n");
	fprintf(f, "Volume = %d\n\n", (int)inifile_volume);
	fprintf(f, "; Colour hue shift for each player's hat\n");
	fprintf(f, "HueShift0 = %d\n", inifile_hueShift[0]);
	fprintf(f, "HueShift1 = %d\n", inifile_hueShift[1]);
	fprintf(f, "HueShift2 = %d\n", inifile_hueShift[2]);
	fprintf(f, "HueShift3 = %d\n\n", inifile_hueShift[3]);
	fprintf(f, "; Player xinput/Xbox controller mappings\n");
	fprintf(f, "XinputPlayer0 = %d\n", inifile_xinputPlayer[0]);
	fprintf(f, "XinputPlayer1 = %d\n", inifile_xinputPlayer[1]);
	fprintf(f, "XinputPlayer2 = %d\n", inifile_xinputPlayer[2]);
	fprintf(f, "XinputPlayer3 = %d\n\n", inifile_xinputPlayer[3]);

	fclose(f);
	sys_printf("xrick/inifile: saved '%s'\n", path);
}

/* eof */
