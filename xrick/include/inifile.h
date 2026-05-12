/*
 * xrick/include/inifile.h
 *
 * Runtime config loader (xrick.ini).
 *
 * Reads simple "Key = Value" lines from a text file. Missing file or
 * missing keys leave the corresponding globals at their defaults, so
 * the game runs unchanged when no ini is present.
 */

#ifndef _INIFILE_H
#define _INIFILE_H

#include "system.h"

/* Number of active Ricks at game start (1..RICK_MAX). Default 1.
 * Equivalent to pressing 1/2/3/4 on the keyboard. */
extern int inifile_playerCount;

/* Texture scaling filter: 1 = bilinear (smooth), 0 = nearest-neighbour
 * (sharp pixels). Default 1 to preserve previous behaviour. */
extern int inifile_linearFilter;

/* Render interpolation toggle: 1 = entities drawn at interpolated
 * positions between 25 fps simulation ticks (smooth), 0 = legacy
 * 25 fps look. Toggled in-game by F10. Default 1. */
extern int inifile_interpolate;

/* Window scale multiplier for graphics/resolution. The window is sized
 * to fb_width*Scale by fb_height*Scale. Default 2 matches SYSVID_ZOOM. */
extern int inifile_scale;

/* Game audio volume (0..SYSSND_MAXVOL). U8 to match the audio system's
 * user-volume type. Default SYSSND_MAXVOL (full volume). */
extern U8 inifile_volume;

/* Per-Rick hat hue-shift in degrees (-360..360). Each Rick gets a hue shift
 * applied to their hat colours so the players can be told apart. A value of
 * 0 leaves the hat untouched (no extra paint pass). */
extern int inifile_hueShift[4];

/* Mapping of player slot -> xinput controller index (0..3), or -1 to
 * disable controller input for that player. Defaults to identity, so a
 * single pad plugged into port 0 drives P1. Stored here (vs. sysxinput.h)
 * so the ini loader doesn't need to depend on the xinput module. */
extern int inifile_xinputPlayer[4];

/* Load and apply the given ini file. Safe to call when the file does
 * not exist (silently no-op). */
void inifile_load(const char *path);

/* Write the current runtime state back out to the given ini file,
 * overwriting any existing file. */
void inifile_save(const char *path);

#endif

/* eof */
