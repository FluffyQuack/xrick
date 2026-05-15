/*
 * xrick/include/sysxinput.h
 *
 * XInput controller support (Windows). Polled once per event-pump from
 * sysevt_poll; bits are OR'd into control_status_p[] for each player
 * whose ini-configured controller index is connected.
 *
 * Non-Windows builds compile the .c file as no-op stubs.
 */

#ifndef _SYSXINPUT_H
#define _SYSXINPUT_H

#include "system.h"

/* Map of player index -> xinput controller index (0..3), or -1 to disable.
 * Defaults to identity {0,1,2,3,-1,-1,-1,-1} so plugging in a single pad
 * drives P1. Players 4..7 have no XInput slot by default (XInput tops out
 * at 4 controllers); they can be assigned via the ini if desired. */
extern int sysxinput_player[8];

void sysxinput_init(void);
void sysxinput_shutdown(void);

/* Poll all controllers and update control_status_p[] for each mapped
 * player. Called once per sysevt_poll, after SDL events are drained. */
void sysxinput_apply(void);

#endif /* _SYSXINPUT_H */

/* eof */
