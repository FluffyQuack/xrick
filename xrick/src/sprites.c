/*
 * xrick/src/sprites.c
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



#include "system.h"
#include "config.h"
#include "env.h"

#include "sprites.h"
#include "fb.h"
#include "maps.h"
#include "tiles.h"
#include "sysvid.h"
#include "inifile.h"
#include "e_rick.h" /* RICK_MAX */

#include <math.h>

/*
 * Per-Rick hat tinting.
 *
 * The original game palette holds 32 colours in fb's RED/GREEN/BLUE tables
 * (16 base + 16 highlight). For each non-anchor Rick we register 16 extra
 * palette slots holding hue-shifted versions of the 16 base colours, and a
 * remap table that translates a sprite's lower-nibble palette index into the
 * tinted palette index. The outline colour (RGB 32,36,32 = palette index 4)
 * is preserved so the silhouette stays consistent across Ricks.
 *
 * sprites_paintHat() re-draws the top HAT_ROWS pixel rows of the sprite
 * using full-byte palette indices (8-bit framebuffer entries past the
 * original 0..31 range), overwriting only non-outline hat pixels that the
 * normal paint just produced.
 */
/*
 * Per-Rick hat palette slots must not have bit 0x10 set: sprites_paint2
 * preserves that bit across writes (it's the "highlight"/invincibility
 * flag in the original palette). If a hat slot had bit 0x10 set, a later
 * sprite drawn on top of it would mistakenly inherit highlight and the
 * pixel would land in the bright half of the palette -- e.g. the outline
 * (index 4) would render as index 0x14 instead, which is the bug where
 * outlines turn whitish when Ricks overlap. We avoid that by spacing the
 * per-Rick blocks at 32 (instead of 16), so every slot we use is in a
 * range with bit 0x10 clear.
 */
#define HAT_PAL_BASE 32           /* first extra palette slot we own */
#define HAT_PAL_PER_RICK 32       /* stride per Rick (16 colours + 16-byte gap) */
#define HAT_OUTLINE_INDEX 4       /* palette index of the (32,36,32) outline */

/* remap[r][i] = palette index to write for sprite low-nibble i on Rick r.
 * 0 means "skip" (transparent / outline / no remap available). */
static U8 hat_remap[RICK_MAX][16];
static U8 hat_palette_ready = 0;



/*
 * sprites_paint
 *
 * paints sprite <spriteNumber> at the position indicated by <x>, <y>.
 * <x>, <y> are fb-coordinates.
 * simple paint: no clipping, no depth management, nothing.
 */
#ifdef GFXPC
void sprites_paint(U8 spriteNumber, U16 x, U16 y)
{
	U8 i, j, k, *f, *fb;
	U16 xm = 0, xp = 0;

	fb = fb_at(x, y);

	for (i = 0; i < 4; i++) /* 4 tile columns */
	{
		f = fb;
		for (j = 0; j < 0x15; j++) /* 0X15 pixel rows */
		{
			xm = sprites_data[spriteNumber][i][j].mask;  /* mask */
			xp = sprites_data[spriteNumber][i][j].pict;  /* picture */
			/* map CGA 2 bits to frame buffer 8 bits per pixels */
			for (k = 8; k--; xm >>= 2, xp >>= 2)
				f[k] = (f[k] & (xm & 3)) | (xp & 3);
			f += FB_WIDTH;
		}
		fb += 8;
	}
}
#endif

#ifdef GFXST
void sprites_paint(U8 spriteNumber, U16 x, U16 y)
{
	U8 i, j, k, *f, *fb;
	U16 g;
	U32 d;

	fb = fb_at(x, y);
	g = 0;

	for (i = 0; i < 0x15; i++) /* 0x15 pixel rows */
	{
		f = fb;
		for (j = 0; j < 4; j++) /* 4 tile columns */
		{
			d = sprites_data[spriteNumber][g++];
			/* map ST 4 bits per pixel to frame buffer 8 bits per pixels.
			 * Only preserve the highlight bit (0x10); leaving the rest of
			 * the high nibble in would smuggle stray indices from the
			 * per-Rick hat palette (slots 32+) back into the sprite, which
			 * shows up as Rick-A's outline turning white when drawn on top
			 * of Rick-B's tinted hat pixels. */
			for (k = 8; k--; d >>= 4)
				if (d & 0x0f) f[k] = (f[k] & 0x10) | (d & 0x0f);
			f += 8;
		}
		fb += FB_WIDTH;
	}
}
#endif



/*
 * sprites_paint2
 *
 * paints sprite <spriteNumber> at the position indicated by <x>, <y>.
 * <x>, <y> are map-coordinates, they are aligned to tile columns.
 * <front> when true indicates that the sprite must not be behind anything.
 * complex paint: manages highlight, depth.
 */
#ifdef GFXPC
void sprites_paint2(U8 spriteNumber, U16 x, U16 y, U8 front)
{
	U8 k, *f, *fb, c, r, dx;
	U16 mask, pict;
	U16 x_map, y_map;
	U16 x_fb, y_fb;
	U16 width, height;

	/* if depth is not managed then sprites are always in front of everything */
	if (!env_depth) front = TRUE;

	/* get map/px */
	x_map = x;
	y_map = y;

	/* align to tile column */
	x_map = x_map & 0xfff8;

	/* sprite dimension in px */
	width = 0x20; /* width = 4 tile columns, 8 pixels each */
	height = 0x15; /* height = 0x15 pixels */

	/* shift */
	dx = (x - x_map) * 2;

	/* clip */
	if (maps_clip(&x_map, &y_map, &width, &height))  /* return if not visible */
		return;

	/* convert to fb/px */
	x_fb = x_map - MAPS_FB_X;
	x_fb = y_map - MAPS_FB_Y;

	/* get buffer */
	fb = fb_at(x_fb, y_fb);

	/* convert from px to tl */
	x_map >>= 3;
	width >>= 3;

	/* draw */
	for (c = 0; c < width; c++) /* for each tile column */
	{
		f = fb;
		for (r = 0; r < height; r++) /* for each pixel row */
		{
			/*
			 * paint only if: <front> is true or env_highlight is true or the
			 * sprite is not behind foreground tiles.
			 */
			if (front || env_highlight ||
				!(map_eflg[map_map[(ymap + r) >> 3][xmap + c]] & MAP_EFLG_FGND))
			{
				pict = mask = 0;
				if (c > 0)
				{
					mask |= sprites_data[spriteNumber][c - 1][r].mask << (16 - dx);
					pict |= sprites_data[spriteNumber][c - 1][r].pict << (16 - dx);
				}
				else
				{
					mask |= 0xffff << (16 - dx);
				}
				if (c < cmax)
				{
					mask |= sprites_data[spriteNumber][c][r].mask >> dx;
					pict |= sprites_data[spriteNumber][c][r].pict >> dx;
				}
				else
				{
					mask |= 0xffff >> dx;
				}

				/* map CGA 2 bits to frame buffer 8 bits per pixels */
				for (k = 8; k--; xm >>= 2, xp >>= 2)
				{
					f[k] = ((f[k] & (mask & 3)) | (pict & 3));
					if (env_highlight) f[k] |= 4;
				}
			}
			f += FB_WIDTH;
		}
		fb += 8;
	}
}
#endif

#ifdef GFXST
void sprites_paint2(U8 spriteNumber, U16 x, U16 y, U8 front)
{
	U32 d = 0;	/* sprite data */
	U16 x0, y0;	/* clipped x, y */
	U16 width, height;
	S16 g;		/* sprite data offset*/
	S16 r, c;	/* row, column */ /* S/U: loop while >=0 */
	S16 i;		/* frame buffer shifter */
	S16 im;		/* tile flag shifter */
	U8 flg;		/* tile flag */
	U8 *fb;		/* frame buffer */
	U16 x_fb, y_fb;

	/* if depth is not managed then sprites are always in front of everything */
	if (!env_depth) front = TRUE;

	x0 = x;
	y0 = y;

	/* sprite dimension in px */
	width = 0x20; /* width = 4 tile columns, 8 pixels each */
	height = 0x15; /* height = 0x15 pixels */

	/* clip */
	if (maps_clip(&x0, &y0, &width, &height))  /* return if not visible */
		return;

	g = 0;


	/* convert to fb/px */
	x_fb = x0 - MAPS_FB_X;
	y_fb = y0 - MAPS_FB_Y+8; /* FIXME =8? */

	/* get buffer */
	fb = fb_at(x_fb, y_fb);

	/* draw */
	for (r = 0; r < 0x15; r++) /* for each pixel row */
	{
		if (r >= height || y + r < y0) continue;

		i = 0x1f;
		im = x - (x & 0xfff8);
		flg = map_eflg[map_map[(y + r) >> 3][(x + 0x1f)>> 3]];

#define LOOP(N, C0, C1) \
		d = sprites_data[spriteNumber][g + N]; \
		for (c = C0; c >= C1; c--, i--, d >>= 4, im--) \
		{ \
			if (im == 0) \
			{ \
				flg = map_eflg[map_map[(y + r) >> 3][(x + c) >> 3]]; \
				im = 8; \
			} \
			if (c >= width || x + c < x0) continue; \
			if (!front && !env_highlight && (flg & MAP_EFLG_FGND)) continue; \
			if (d & 0x0f) fb[i] = (fb[i] & 0x10) | (d & 0x0f); \
			if (env_highlight) fb[i] |= 0x10; \
		}

		LOOP(3, 0x1f, 0x18);
		LOOP(2, 0x17, 0x10);
		LOOP(1, 0x0f, 0x08);
		LOOP(0, 0x07, 0x00);

#undef LOOP

		fb += FB_WIDTH;
		g += 4;
	}
}
#endif



/*
 * sprites_clear
 *
 * repaints the map behind a sprite at position <x>, <y>.
 * <x>, <y> are map-coordinates, they are aligned to tile columns and clipped.
 */
void
sprites_clear(U16 x, U16 y)
{
	U8 r, c;
	U16 rmax, cmax;
	U16 xmap, ymap;
	U16 xs, ys;
	U8 *fb;

  	/* align to column and row */
	xmap = x & 0xFFF8;
	ymap = y & 0xFFF8;

	cmax = (x - xmap == 0 ? 0x20 : 0x28);  /* width, 4 tl cols, 8 pix each */
	rmax = (y & 0x04) ? 0x20 : 0x18;  /* height, 3 or 4 tile rows */

	/* clip */
	if (maps_clip(&xmap, &ymap, &cmax, &rmax))  /* return if not visible */
		return;

	/* convert to fb-coordinates */
	xs = xmap - MAPS_FB_X;
	ys = ymap - MAPS_FB_Y;
	xmap >>= 3;
	ymap >>= 3;
	cmax >>= 3;
	rmax >>= 3;

	/* draw */
	for (r = 0; r < rmax; r++) /* for each row */
	{
#ifdef GFXPC
		fb = fb_at(xs, ys + r * 8);
#endif
#ifdef GFXST
		fb = fb_at(xs, 8 + ys + r * 8);
#endif
		for (c = 0; c < cmax; c++) /* for each column */
		{
			fb = tiles_paint(map_map[ymap + r][xmap + c], fb);
		}
	}
}

/*
 * Hue-shift one RGB colour by `deg` degrees. Uses HSV in [0,1] space.
 */
static void
hat_hueShift(U8 r_in, U8 g_in, U8 b_in, int deg, U8 *r_out, U8 *g_out, U8 *b_out)
{
	float r = r_in / 255.0f;
	float g = g_in / 255.0f;
	float b = b_in / 255.0f;
	float mx = r; if (g > mx) mx = g; if (b > mx) mx = b;
	float mn = r; if (g < mn) mn = g; if (b < mn) mn = b;
	float d = mx - mn;
	float h = 0.0f, s = (mx == 0.0f) ? 0.0f : d / mx;
	float v = mx;
	float c, x, m, rp, gp, bp;

	if (d != 0.0f) {
		if (mx == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
		else if (mx == g) h = (b - r) / d + 2.0f;
		else              h = (r - g) / d + 4.0f;
		h *= 60.0f;
	}
	h = fmodf(h + (float)deg + 360.0f * 10.0f, 360.0f);

	c = v * s;
	x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
	m = v - c;
	if      (h <  60.0f) { rp = c; gp = x; bp = 0; }
	else if (h < 120.0f) { rp = x; gp = c; bp = 0; }
	else if (h < 180.0f) { rp = 0; gp = c; bp = x; }
	else if (h < 240.0f) { rp = 0; gp = x; bp = c; }
	else if (h < 300.0f) { rp = x; gp = 0; bp = c; }
	else                 { rp = c; gp = 0; bp = x; }

	*r_out = (U8)((rp + m) * 255.0f + 0.5f);
	*g_out = (U8)((gp + m) * 255.0f + 0.5f);
	*b_out = (U8)((bp + m) * 255.0f + 0.5f);
}


void
sprites_initHatPalette(void)
{
	U8 r, g, b;
	U8 nr, ng, nb;
	U8 ricki, ci;
	U16 slot;

	for (ricki = 0; ricki < RICK_MAX; ricki++)
	{
		int deg = inifile_hueShift[ricki];

		/* Hue shift of 0 means "vanilla": leave the remap table empty so
		 * sprites_paintHat() short-circuits and nothing gets overdrawn. */
		if (deg == 0)
		{
			for (ci = 0; ci < 16; ci++)
				hat_remap[ricki][ci] = 0;
			continue;
		}

		for (ci = 0; ci < 16; ci++)
		{
			if (ci == HAT_OUTLINE_INDEX)
			{
				/* Outline: never remap, never overwrite. */
				hat_remap[ricki][ci] = 0;
				continue;
			}

			fb_getPaletteRGB(ci, &r, &g, &b);

			/* Index 0 in the base palette is transparent/background black --
			 * sprites_paint2 skips it anyway, but be defensive. */
			if (ci == 0)
			{
				hat_remap[ricki][ci] = 0;
				continue;
			}

			hat_hueShift(r, g, b, deg, &nr, &ng, &nb);

			slot = (U16)HAT_PAL_BASE + (U16)ricki * HAT_PAL_PER_RICK + ci;
			sysvid_setPaletteEntry(slot, nr, ng, nb);
			hat_remap[ricki][ci] = (U8)slot;
		}
	}

	hat_palette_ready = 1;
}


/*
 * Overlay the top HAT_ROWS rows of the sprite with the Rick-specific
 * tinted palette. Logic mirrors sprites_paint2 (GFXST path) but with full
 * 8-bit palette index writes and an early row cutoff.
 */
#ifdef GFXST
void
sprites_paintHat(U8 rickIndex, U8 spriteNumber, U16 x, U16 y, U8 front, U8 hatRows)
{
	U32 d = 0;
	U16 x0, y0;
	U16 width, height;
	S16 g;
	S16 r, c;
	S16 i;
	S16 im;
	U8 flg;
	U8 *fb;
	U16 x_fb, y_fb;
	U8 *remap;
	U8 idx, outIdx;
	U8 ci;
	U8 has_remap = 0;
	S16 firstRow;
	S16 lastRow; /* exclusive */
	S16 scan;

	if (rickIndex >= RICK_MAX) return;
	if (!hat_palette_ready) return;
	if (hatRows == 0) return;
	if (hatRows > 0x15) hatRows = 0x15;

	remap = hat_remap[rickIndex];
	/* Skip the overdraw entirely if this Rick is using a 0 hue shift. */
	for (ci = 0; ci < 16; ci++) if (remap[ci]) { has_remap = 1; break; }
	if (!has_remap) return;

	/*
	 * Find the first row of the sprite that contains any non-transparent
	 * pixel. The hat band runs from that row downward, so different poses
	 * (standing, crouching, climbing, falling) all keep the tint locked
	 * to the top of the actually-drawn silhouette instead of an absolute
	 * pixel offset that would slide off in some poses.
	 *
	 * sprites_data is U32[0x54] = 0x15 rows * 4 columns; each U32 packs 8
	 * 4-bit pixels. A row is empty iff all 4 of its U32s are zero.
	 */
	firstRow = -1;
	for (scan = 0; scan < 0x15; scan++)
	{
		U32 *row = &sprites_data[spriteNumber][scan * 4];
		if (row[0] | row[1] | row[2] | row[3]) { firstRow = scan; break; }
	}
	if (firstRow < 0) return; /* fully transparent sprite */

	lastRow = firstRow + hatRows;
	if (lastRow > 0x15) lastRow = 0x15;

	if (!env_depth) front = TRUE;

	x0 = x;
	y0 = y;
	width = 0x20;
	height = 0x15;

	if (maps_clip(&x0, &y0, &width, &height))
		return;

	g = 0;

	x_fb = x0 - MAPS_FB_X;
	y_fb = y0 - MAPS_FB_Y + 8;

	fb = fb_at(x_fb, y_fb);

	for (r = 0; r < 0x15; r++)
	{
		if (r >= lastRow) break;
		if (r < firstRow) { g += 4; fb += FB_WIDTH; continue; }
		if (r >= height || y + r < y0) { g += 4; fb += FB_WIDTH; continue; }

		i = 0x1f;
		im = x - (x & 0xfff8);
		flg = map_eflg[map_map[(y + r) >> 3][(x + 0x1f) >> 3]];

#define HAT_LOOP(N, C0, C1) \
		d = sprites_data[spriteNumber][g + N]; \
		for (c = C0; c >= C1; c--, i--, d >>= 4, im--) \
		{ \
			if (im == 0) \
			{ \
				flg = map_eflg[map_map[(y + r) >> 3][(x + c) >> 3]]; \
				im = 8; \
			} \
			if (c >= width || x + c < x0) continue; \
			if (!front && !env_highlight && (flg & MAP_EFLG_FGND)) continue; \
			idx = (U8)(d & 0x0f); \
			if (idx == 0) continue;             /* transparent */ \
			if (idx == HAT_OUTLINE_INDEX) continue; /* keep outline */ \
			outIdx = remap[idx]; \
			if (outIdx == 0) continue; \
			fb[i] = outIdx; \
		}

		HAT_LOOP(3, 0x1f, 0x18);
		HAT_LOOP(2, 0x17, 0x10);
		HAT_LOOP(1, 0x0f, 0x08);
		HAT_LOOP(0, 0x07, 0x00);

#undef HAT_LOOP

		fb += FB_WIDTH;
		g += 4;
	}
}
#endif

#ifdef GFXPC
void
sprites_paintHat(U8 rickIndex, U8 spriteNumber, U16 x, U16 y, U8 front, U8 hatRows)
{
	/* Hat tinting is only implemented for the GFXST data layout. */
	(void)rickIndex; (void)spriteNumber; (void)x; (void)y; (void)front; (void)hatRows;
}
#endif


/* eof */