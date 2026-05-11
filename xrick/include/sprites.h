/*
 * xrick/include/sprites.h
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

/*
 * NOTES -- PC version
 *
 * A sprite consists in 4 columns and 0x15 rows of (U16 mask, U16 pict),
 * each pair representing 8 pixels (cga encoding, two bits per pixels).
 * Sprites are stored in 'sprites.bin' and are loaded by spr_init. Memory
 * is freed by spr_shutdown.
 *
 * There are four sprites planes. Plane 0 is the raw content of 'sprites.bin',
 * and planes 1, 2 and 3 contain copies of plane 0 with all sprites shifted
 * 2, 4 and 6 pixels to the right.
 */


#ifndef _SPRITES_H_
#define _SPRITES_H_

#include "system.h"

/*
 * methods
 */
void sprites_setDepth(U8);
void sprites_paint(U8, U16, U16);
void sprites_paint2(U8, U16, U16, U8);
void sprites_clear(U16, U16);

/*
 * Build per-Rick hue-shifted hat palette entries. Must be called after the
 * regular game palette is loaded (fb_initPalette). Idempotent.
 */
void sprites_initHatPalette(void);

/*
 * Co-op: overlay the top 6 rows of Rick's sprite using the per-Rick
 * hat-tinted palette. Call AFTER sprites_paint2 has drawn the Rick. The
 * outline colour (palette index 4 = RGB 32,36,32) is left untouched.
 * rickIndex 0 leaves Rick 0 unchanged.
 */
void sprites_paintHat(U8 rickIndex, U8 spriteNumber, U16 x, U16 y, U8 front, U8 hatRows);

#ifdef GFXPC

#define SPRITES_NBR_SPRITES (0x9b)

typedef struct {
  U16 mask;
  U16 pict;
} spriteX_t;

typedef spriteX_t sprite_t[4][0x15];   /* one sprite */

extern sprite_t sprites_data[SPRITES_NBR_SPRITES];

#endif

#ifdef GFXST

#define SPRITES_NBR_SPRITES (0xD5)

typedef U32 sprite_t[0x54];  /* 0x15 per 0x04 */

extern sprite_t sprites_data[SPRITES_NBR_SPRITES];

#endif

#endif

/* eof */

