/*
 * xrick/src/inifile.c
 *
 * Minimal "Key = Value" config loader. Whitespace around key and value
 * is trimmed; ';' and '#' begin a line comment; blank lines and lines
 * without '=' are ignored. Unknown keys are ignored too.
 */

#include "inifile.h"

#include "system.h"
#include "e_rick.h" /* RICK_MAX */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __MSVC__
#define strcasecmp _stricmp
#endif

int inifile_playerCount = 1;
int inifile_linearFilter = 1;

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
		else {
			sys_printf("xrick/inifile: unknown key '%s'\n", key);
		}
	}

	fclose(f);
	sys_printf("xrick/inifile: loaded '%s' (PlayerCount=%d, LinearFilter=%d)\n",
	           path, inifile_playerCount, inifile_linearFilter);
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

	f = fopen(path, "w");
	if (!f) {
		sys_printf("xrick/inifile: could not write '%s'\n", path);
		return;
	}

	fprintf(f, "PlayerCount = %d\n", inifile_playerCount);
	fprintf(f, "LinearFilter = %d\n", inifile_linearFilter);

	fclose(f);
	sys_printf("xrick/inifile: saved '%s'\n", path);
}

/* eof */
