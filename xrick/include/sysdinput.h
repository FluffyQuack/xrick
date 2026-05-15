/*
 * xrick/include/sysdinput.h
 *
 * DirectInput controller support (Windows). Polled once per event-pump
 * from sysevt_poll, after the XInput pass; bits are OR'd into
 * control_status_p[] for each player whose ini-configured DirectInput
 * slot is connected.
 *
 * XInput-capable devices are filtered out during enumeration (the
 * "IG_" trick on the device's HID path), so the same physical pad
 * can't drive a player twice when both modules are active.
 *
 * Non-Windows builds compile the .c file as no-op stubs.
 */

#ifndef _SYSDINPUT_H
#define _SYSDINPUT_H

#include "system.h"

/* Map of player index -> DirectInput device slot (0..3), or -1 to
 * disable. Defaults to all-disabled so plugging in an XInput pad does
 * not get double-bound via DI on systems where filtering misses. */
extern int sysdinput_player[8];

void sysdinput_init(void);
void sysdinput_shutdown(void);

/* Poll all DI controllers and update control_status_p[] for each
 * mapped player. Called once per sysevt_poll, after sysxinput_apply. */
void sysdinput_apply(void);

#endif /* _SYSDINPUT_H */

/* eof */
