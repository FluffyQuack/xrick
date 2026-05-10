/*
 * xrick/src/syskbd.c
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#include <SDL.h>

#include "system.h"
#include "syskbd.h"

/*
 * Using the SDL_SCANCODE_xxx keysyms, which map to a QWERTY keyboard.
 * We get them via SDL_KEYDOWN.
 * We do *not* use SDL_TEXTINPUT nor SDLK_ to get true key mappings, so
 * for instance left on an AZERTY keyboard will be 'w' instead of 'z'.
 */

/*
 * Co-op (Stage 2): per-player keyboard layouts. Order matches rick index:
 *   [0] P1  : arrows + space
 *   [1] P2  : WASD + LSHIFT
 *   [2] P3  : IJKL + RETURN
 *   [3] P4  : no bindings (all zero)
 *
 * The legacy P1 alternates (Z/X/K/O) were removed -- they collided with
 * P3's IJKL layout and the original codebase only kept them for ports
 * where arrows weren't reachable.
 */
player_kbd_t syskbd_players[CONTROL_PLAYERS] = {
	/* P1 */ { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_SPACE },
	/* P2 */ { SDL_SCANCODE_W,  SDL_SCANCODE_S,    SDL_SCANCODE_A,    SDL_SCANCODE_D,     SDL_SCANCODE_LSHIFT },
	/* P3 */ { SDL_SCANCODE_I,  SDL_SCANCODE_K,    SDL_SCANCODE_J,    SDL_SCANCODE_L,     SDL_SCANCODE_RETURN },
	/* P4 */ { 0,               0,                 0,                 0,                  0 }
};

/* Global (non-per-player) keys. */
U8 syskbd_pause = SDL_SCANCODE_P;
U8 syskbd_end   = SDL_SCANCODE_E;
U8 syskbd_xtra  = SDL_SCANCODE_ESCAPE;

/* eof */


