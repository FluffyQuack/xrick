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

/* Number of active Ricks at game start (1..RICK_MAX). Default 1.
 * Equivalent to pressing 1/2/3/4 on the keyboard. */
extern int inifile_playerCount;

/* Texture scaling filter: 1 = bilinear (smooth), 0 = nearest-neighbour
 * (sharp pixels). Default 1 to preserve previous behaviour. */
extern int inifile_linearFilter;

/* Window scale multiplier for graphics/resolution. The window is sized
 * to fb_width*Scale by fb_height*Scale. Default 2 matches SYSVID_ZOOM. */
extern int inifile_scale;

/* Load and apply the given ini file. Safe to call when the file does
 * not exist (silently no-op). */
void inifile_load(const char *path);

/* Write the current runtime state back out to the given ini file,
 * overwriting any existing file. */
void inifile_save(const char *path);

#endif

/* eof */
