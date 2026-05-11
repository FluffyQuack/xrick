/*
 * xrick/src/ents.c
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

#include <stdlib.h>

#include "system.h"
#include "config.h"
#include "env.h"

#include "ents.h"

#include "game.h"
#include "debug.h"

#include "e_bullet.h"
#include "e_bomb.h"
#include "e_rick.h"
#include "e_them.h"
#include "e_bonus.h"
#include "e_box.h"
#include "e_sbonus.h"
#include "rects.h"
#include "maps.h"
#include "draw.h"
#include "tiles.h"
#include "sprites.h"

/*
 * global vars
 */
ent_t ent_ents[ENT_ENTSNUM + 1];
rect_t *ent_rects = NULL;


/*
 * prototypes
 */
static void ent_addrect(U16, U16, U16, U16);
static U8 ent_creat1(U8 *);
static U8 ent_creat2(U8 *, U16);


/*
 * Reset entities
 *
 * ASM 2520
 */
void
ent_reset(void)
{
  U8 i;

  E_RICK_STRST(E_RICK_STSTOP);

  ent_ents[0].n = 0;
  for (i = 2; ent_ents[i].n != 0xff; i++)
    ent_ents[i].n = 0;

  /* Co-op: clear extra bullets too (Bullet 0 was already cleared above). */
  for (i = 0; i < RICK_MAX - 1; i++)
    extra_bullet_ents[i].n = 0;
  /* Co-op: same for extra bombs. */
  for (i = 0; i < RICK_MAX - 1; i++)
    extra_bomb_ents[i].n = 0;
}


/*
 * Create an entity on slots 4 to 8 by using the first slot available.
 * Entities of type e_them on slots 4 to 8, when lethal, can kill
 * other e_them (on slots 4 to C) as well as rick.
 *
 * ASM 209C
 *
 * e: anything, CHANGED to the allocated entity number.
 * return: TRUE/OK FALSE/not
 */
static U8
ent_creat1(U8 *e)
{
  /* look for a slot */
  for (*e = 0x04; *e < 0x09; (*e)++)
    if (ent_ents[*e].n == 0) {  /* if slot available, use it */
      ent_ents[*e].c1 = 0;
      return TRUE;
    }

  return FALSE;
}


/*
 * Create an entity on slots 9 to C by using the first slot available.
 * Entities of type e_them on slots 9 to C can kill rick when lethal,
 * but they can never kill other e_them.
 *
 * ASM 20BC
 *
 * e: anything, CHANGED to the allocated entity number.
 * m: number of the mark triggering the creation of the entity.
 * ret: TRUE/OK FALSE/not
 */
static U8
ent_creat2(U8 *e, U16 m)
{
  /* make sure the entity created by this mark is not active already */
  for (*e = 0x09; *e < 0x0c; (*e)++)
    if (ent_ents[*e].n != 0 && ent_ents[*e].mark == m)
      return FALSE;

  /* look for a slot */
  for (*e = 0x09; *e < 0x0c; (*e)++)
    if (ent_ents[*e].n == 0) {  /* if slot available, use it */
      ent_ents[*e].c1 = 2;
      return TRUE;
    }

  return FALSE;
}


/*
 * Process marks that are within the visible portion of the map,
 * and create the corresponding entities.
 *
 * absolute map coordinate means that they are not relative to
 * map_frow, as any other coordinates are.
 *
 * ASM 1F40
 *
 * frow: first visible row of the map -- absolute map coordinate
 * lrow: last visible row of the map -- absolute map coordinate
 */
void
ent_actvis(U8 frow, U8 lrow)
{
	U16 m;
	U8 e;
	U16 y;

	/*
	* go through the list and find the first mark that
	* is visible, i.e. which has a row greater than the
	* first row (marks being ordered by row number).
	*/
	for (m = map_submaps[env_submap].mark;
		map_marks[m].row != 0xff && map_marks[m].row < frow;
		m++);

	if (map_marks[m].row == 0xff)  /* none found */
		return;

	/*
	* go through the list and process all marks that are
	* visible, i.e. which have a row lower than the last
	* row (marks still being ordered by row number).
	*/
	for (;
		map_marks[m].row != 0xff && map_marks[m].row < lrow;
		m++) {

		/* ignore marks that are not active */
		if (map_marks[m].ent & MAP_MARK_NACT)
			continue;

		/*
		 * allocate a slot to the new entity
		 *
		 * slot type
		 *  0   available for e_them (lethal to other e_them, and stops entities
		 *      i.e. entities can't move over them. E.g. moving blocks. But they
		 *      can move over entities and kill them!).
		 *  1   xrick
		 *  2   bullet
		 *  3   bomb
		 * 4-8  available for e_them, e_box, e_bonus or e_sbonus (lethal to
		 *      other e_them, identified by their number being >= 0x10)
		 * 9-C  available for e_them, e_box, e_bonus or e_sbonus (not lethal to
		 *      other e_them, identified by their number being < 0x10)
		 *
		 * the type of an entity is determined by its .n as detailed below.
		 *
		 * 1               xrick
		 * 2               bullet
		 * 3               bomb
		 * 4, 7, a, d      e_them, type 1a
		 * 5, 8, b, e      e_them, type 1b
		 * 6, 9, c, f      e_them, type 2
		 * 10, 11          box
		 * 12, 13, 14, 15  bonus
		 * 16, 17          speed bonus
		 * >17             e_them, type 3
		 * 47              zombie
		 */

		if (!(map_marks[m].flags & ENT_FLG_STOPRICK)) {
			if (map_marks[m].ent >= 0x10) {
				/* boxes, bonuses and type 3 e_them go to slot 4-8 */
				/* (c1 set to 0 -> all type 3 e_them are sleeping) */
				if (!ent_creat1(&e)) continue;
			}
			else {
				/* type 1 and 2 e_them go to slot 9-c */
				/* (c1 set to 2) */
				if (!ent_creat2(&e, m)) continue;
			}
		}
		else {
			/* entities stopping rick (e.g. blocks) go to slot 0 */
			if (ent_ents[0].n) continue;
			e = 0;
			ent_ents[0].c1 = 0;
		}

    /*
     * initialize the entity
     */
    ent_ents[e].mark = m;
    ent_ents[e].flags = map_marks[m].flags;
    ent_ents[e].n = map_marks[m].ent;

    /*
     * if entity is to be already running (i.e. not asleep and waiting
     * for some trigger to move), then use LETHALR i.e. restart flag, right
     * from the beginning
     */
    if (ent_ents[e].flags & ENT_FLG_LETHALR)
      ent_ents[e].n |= ENT_LETHAL;

    ent_ents[e].x = map_marks[m].xy & 0xf8;

    y = (map_marks[m].xy & 0x07) + (map_marks[m].row & 0xf8) - map_frow;
    y <<= 3;
    if (!(ent_ents[e].flags & ENT_FLG_STOPRICK))
      y += 3;
    ent_ents[e].y = y;

    ent_ents[e].xsave = ent_ents[e].x;
    ent_ents[e].ysave = ent_ents[e].y;

    /*ent_ents[e].w0C = 0;*/  /* in ASM code but never used */

    ent_ents[e].w = ent_entdata[map_marks[m].ent].w;
    ent_ents[e].h = ent_entdata[map_marks[m].ent].h;
    ent_ents[e].sprbase = ent_entdata[map_marks[m].ent].spr;
    ent_ents[e].sprite = (U8)ent_entdata[map_marks[m].ent].spr;
    ent_ents[e].step_no_i = ent_entdata[map_marks[m].ent].sni;
    ent_ents[e].trigsnd = (U8)ent_entdata[map_marks[m].ent].snd;

    /*
     * FIXME what is this? when all trigger flags are up, then
     * use .sni for sprbase. Why? What is the point? (This is
     * for type 1 and 2 e_them, ...)
     *
     * This also means that as long as sprite has not been
     * recalculated, a wrong value is used. This is normal, see
     * what happens to the falling guy on the right on submap 3:
     * it changes when hitting the ground.
     */
#define ENT_FLG_TRIGGERS \
(ENT_FLG_TRIGBOMB|ENT_FLG_TRIGBULLET|ENT_FLG_TRIGSTOP|ENT_FLG_TRIGRICK)
    if ((ent_ents[e].flags & ENT_FLG_TRIGGERS) == ENT_FLG_TRIGGERS
	&& e >= 0x09)
      ent_ents[e].sprbase = (U8)(ent_entdata[map_marks[m].ent].sni & 0x00ff);
#undef ENT_FLG_TRIGGERS

    ent_ents[e].trig_x = map_marks[m].lt & 0xf8;
    ent_ents[e].latency = (map_marks[m].lt & 0x07) << 5;  /* <<5 eq *32 */

    ent_ents[e].trig_y = 3 + 8 * ((map_marks[m].row & 0xf8) - map_frow +
				  (map_marks[m].lt & 0x07));

    ent_ents[e].c2 = 0;
    ent_ents[e].offsy = 0;
    ent_ents[e].ylow = 0;

    ent_ents[e].front = FALSE;

    /*
     * Co-op (Stage 4): newly-spawned enemies have no target until the
     * first AI tick acquires one. 0xff = unset sentinel.
     */
    ent_ents[e].target_rick = 0xff;
  }
}


/*
 * Add a tile-aligned rectangle containing the given rectangle (indicated
 * by its MAP coordinates) to the list of rectangles. Clip the rectangle
 * so it fits into the display zone.
 */
static void
ent_addrect(U16 x, U16 y, U16 width, U16 height)
{
  U16 x0, y0;
  U16 w0, h0;

  /*sys_printf("rect %#04x,%#04x %#04x %#04x ", x, y, width, height);*/

  /* align to tiles */
  x0 = x & 0xfff8;
  y0 = y & 0xfff8;
  w0 = width;
  h0 = height;
  if (x - x0) w0 = (w0 + (x - x0)) | 0x0007;
  if (y - y0) h0 = (h0 + (y - y0)) | 0x0007;

  /* clip */
  if (maps_clip(&x0, &y0, &w0, &h0)) {  /* do not add if fully clipped */
    /*sys_printf("-> [clipped]\n");*/
    return;
  }

  /*sys_printf("-> %#04x,%#04x %#04x %#04x\n", x0, y0, w0, h0);*/

#ifdef GFXST
  y0 += 8;
#endif

  /* get to screen */
  x0 -= DRAW_XYMAP_SCRLEFT;
  y0 -= DRAW_XYMAP_SCRTOP;

  /* add rectangle to the list */
  ent_rects = rects_new(x0, y0, w0, h0, ent_rects);
}


/*
 * Draw all entities onto the frame buffer.
 *
 * ASM 07a4
 *
 * NOTE This may need to be part of draw.c. Also needs better comments,
 * NOTE and probably better rectangles management.
 */
void ents_clearAll()
{
}

void ents_paintAll()
{
	U8 i;
	U16 dx, dy;
	static U8 prev_h = FALSE;

	tiles_setBank(map_tilesBank);

	/* reset rectangles list */
	rects_free(ent_rects);
	ent_rects = NULL;

	/* background loop : erase all entities that were visible */
	for (i = 0; ent_ents[i].n != 0xff; i++)
	{
	    if (ent_ents[i].prev_n && (prev_h || ent_ents[i].prev_s))
	    {
			/* if entity was active, then erase it (redraw the map) */
			maps_paintRect(ent_ents[i].prev_x, ent_ents[i].prev_y, 0x20, 0x15);
		}
	}
	/*
	 * Co-op (Stage 3): erase the extra Ricks (1..3) the same way. Their
	 * backing entities live in extra_rick_ents, not ent_ents, so the loop
	 * above doesn't see them.
	 */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		if (extra_rick_ents[i].prev_n && (prev_h || extra_rick_ents[i].prev_s))
			maps_paintRect(extra_rick_ents[i].prev_x, extra_rick_ents[i].prev_y, 0x20, 0x15);
	}
	/* Co-op: erase extra bullets (1..3) the same way. */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		if (extra_bullet_ents[i].prev_n && (prev_h || extra_bullet_ents[i].prev_s))
			maps_paintRect(extra_bullet_ents[i].prev_x, extra_bullet_ents[i].prev_y, 0x20, 0x15);
	}
	/* Co-op: erase extra bombs (1..3) the same way. */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		if (extra_bomb_ents[i].prev_n && (prev_h || extra_bomb_ents[i].prev_s))
			maps_paintRect(extra_bomb_ents[i].prev_x, extra_bomb_ents[i].prev_y, 0x20, 0x15);
	}

	/* foreground loop : draw all entities that are visible */
	for (i = 0; ent_ents[i].n != 0xff; i++)
	{
		if (ent_ents[i].n && (env_highlight || ent_ents[i].sprite))
		{
			/* if entitiy is active, draw the sprite. */
			sprites_paint2(ent_ents[i].sprite,
				ent_ents[i].x, ent_ents[i].y,
				ent_ents[i].front);
			/* Co-op: Rick 0 lives in ent_ents[E_RICK_NO]; tint his hat
			 * if a hue shift was configured. Hat height tracks pose:
			 * crouching extends the mask downwards (the hat sits lower
			 * on the body), zombie/flying-into-foreground shrinks it. */
			if (i == E_RICK_NO && rick_active[0])
			{
				/* Hat band: 6 rows from the first opaque row of the sprite,
				 * trimmed to 5 when the Rick is flying-into-foreground
				 * (zombie pose), since that sprite shows less of the head. */
				U8 rows = R_STTST(0, E_RICK_STZOMBIE) ? 5 : 6;
				sprites_paintHat(0, ent_ents[i].sprite,
					ent_ents[i].x, ent_ents[i].y,
					ent_ents[i].front, rows);
			}
		}
	}
	/* Co-op (Stage 3): foreground draw for the extra Ricks. */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		ent_t *e = &extra_rick_ents[i];
		if (rick_active[i + 1] && e->n && (env_highlight || e->sprite))
		{
			U8 ri = (U8)(i + 1);
			U8 rows = R_STTST(ri, E_RICK_STZOMBIE) ? 5 : 6;
			sprites_paint2(e->sprite, e->x, e->y, e->front);
			sprites_paintHat(ri, e->sprite, e->x, e->y, e->front, rows);
		}
	}
	/* Co-op: foreground draw for extra bullets. */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		ent_t *e = &extra_bullet_ents[i];
		if (e->n && (env_highlight || e->sprite))
			sprites_paint2(e->sprite, e->x, e->y, e->front);
	}
	/* Co-op: foreground draw for extra bombs. */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		ent_t *e = &extra_bomb_ents[i];
		if (e->n && (env_highlight || e->sprite))
			sprites_paint2(e->sprite, e->x, e->y, e->front);
	}

	/*
	* rectangles loop : figure out which parts of the screen have been
	* impacted and need to be refreshed, then save state
	*/
	for (i = 0; ent_ents[i].n != 0xff; i++)
	{
		if (ent_ents[i].prev_n && (prev_h || ent_ents[i].prev_s))
		{
			/* (1) if entity was active and has been painted... */
			if (ent_ents[i].n && (env_highlight || ent_ents[i].sprite))
			{
				/* (1.1) ... and is still active now and still needs to be */
				/*       painted, then check if rectangles intersect */
				dx = abs(ent_ents[i].x - ent_ents[i].prev_x);
				dy = abs(ent_ents[i].y - ent_ents[i].prev_y);
				if (dx < 0x20 && dy < 0x16)
				{
					/* (1.1.1) if they do, then create one rectangle */
					ent_addrect((ent_ents[i].prev_x < ent_ents[i].x) ?
						ent_ents[i].prev_x : ent_ents[i].x,
						(ent_ents[i].prev_y < ent_ents[i].y) ?
						ent_ents[i].prev_y : ent_ents[i].y,
						dx + 0x20, dy + 0x15);
				}
				else
				{
					/* (1.1.2) else, create two rectangles */
					ent_addrect(ent_ents[i].x, ent_ents[i].y, 0x20, 0x15);
					ent_addrect(ent_ents[i].prev_x, ent_ents[i].prev_y, 0x20, 0x15);
				}
			}
			else
			{
				/* (1.2) ... and is not active anymore or does not need to be */
				/*       painted then create one single rectangle */
				ent_addrect(ent_ents[i].prev_x, ent_ents[i].prev_y, 0x20, 0x15);
			}
		}
		else
		if (ent_ents[i].n && (env_highlight || ent_ents[i].sprite))
		{
			/* (2) if entity is active and needs to be painted, */
			/*     then create one rectangle */
			ent_addrect(ent_ents[i].x, ent_ents[i].y, 0x20, 0x15);
		}

		/* save state */
		ent_ents[i].prev_x = ent_ents[i].x;
		ent_ents[i].prev_y = ent_ents[i].y;
		ent_ents[i].prev_n = ent_ents[i].n;
		ent_ents[i].prev_s = ent_ents[i].sprite;
	}

	/*
	 * Co-op (Stage 3): same dirty-rect bookkeeping for the extra Ricks.
	 * Inlined rather than refactored because it's a small, localized loop
	 * and the original logic is tangled enough that extracting it would
	 * obscure intent.
	 */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		ent_t *e = &extra_rick_ents[i];
		U8 active = rick_active[i + 1] && e->n;
		U8 was_active = e->prev_n;

		if (was_active && (prev_h || e->prev_s))
		{
			if (active && (env_highlight || e->sprite))
			{
				dx = abs(e->x - e->prev_x);
				dy = abs(e->y - e->prev_y);
				if (dx < 0x20 && dy < 0x16)
				{
					ent_addrect((e->prev_x < e->x) ? e->prev_x : e->x,
					            (e->prev_y < e->y) ? e->prev_y : e->y,
					            dx + 0x20, dy + 0x15);
				}
				else
				{
					ent_addrect(e->x, e->y, 0x20, 0x15);
					ent_addrect(e->prev_x, e->prev_y, 0x20, 0x15);
				}
			}
			else
			{
				ent_addrect(e->prev_x, e->prev_y, 0x20, 0x15);
			}
		}
		else if (active && (env_highlight || e->sprite))
		{
			ent_addrect(e->x, e->y, 0x20, 0x15);
		}

		e->prev_x = e->x;
		e->prev_y = e->y;
		e->prev_n = e->n;
		e->prev_s = e->sprite;
	}

	/* Co-op: same dirty-rect bookkeeping for extra bullets. */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		ent_t *e = &extra_bullet_ents[i];
		U8 active = e->n;
		U8 was_active = e->prev_n;

		if (was_active && (prev_h || e->prev_s))
		{
			if (active && (env_highlight || e->sprite))
			{
				dx = abs(e->x - e->prev_x);
				dy = abs(e->y - e->prev_y);
				if (dx < 0x20 && dy < 0x16)
				{
					ent_addrect((e->prev_x < e->x) ? e->prev_x : e->x,
					            (e->prev_y < e->y) ? e->prev_y : e->y,
					            dx + 0x20, dy + 0x15);
				}
				else
				{
					ent_addrect(e->x, e->y, 0x20, 0x15);
					ent_addrect(e->prev_x, e->prev_y, 0x20, 0x15);
				}
			}
			else
			{
				ent_addrect(e->prev_x, e->prev_y, 0x20, 0x15);
			}
		}
		else if (active && (env_highlight || e->sprite))
		{
			ent_addrect(e->x, e->y, 0x20, 0x15);
		}

		e->prev_x = e->x;
		e->prev_y = e->y;
		e->prev_n = e->n;
		e->prev_s = e->sprite;
	}

	/* Co-op: same dirty-rect bookkeeping for extra bombs. */
	for (i = 0; i < RICK_MAX - 1; i++)
	{
		ent_t *e = &extra_bomb_ents[i];
		U8 active = e->n;
		U8 was_active = e->prev_n;

		if (was_active && (prev_h || e->prev_s))
		{
			if (active && (env_highlight || e->sprite))
			{
				dx = abs(e->x - e->prev_x);
				dy = abs(e->y - e->prev_y);
				if (dx < 0x20 && dy < 0x16)
				{
					ent_addrect((e->prev_x < e->x) ? e->prev_x : e->x,
					            (e->prev_y < e->y) ? e->prev_y : e->y,
					            dx + 0x20, dy + 0x15);
				}
				else
				{
					ent_addrect(e->x, e->y, 0x20, 0x15);
					ent_addrect(e->prev_x, e->prev_y, 0x20, 0x15);
				}
			}
			else
			{
				ent_addrect(e->prev_x, e->prev_y, 0x20, 0x15);
			}
		}
		else if (active && (env_highlight || e->sprite))
		{
			ent_addrect(e->x, e->y, 0x20, 0x15);
		}

		e->prev_x = e->x;
		e->prev_y = e->y;
		e->prev_n = e->n;
		e->prev_s = e->sprite;
	}

	prev_h = env_highlight;
}


/*
 * Clear entities previous state
 *
 */
void
ent_clprev(void)
{
  U8 i;

  for (i = 0; ent_ents[i].n != 0xff; i++)
    ent_ents[i].prev_n = 0;

  /* Co-op (Stage 3): also reset the extra Ricks' dirty-rect state. */
  ricks_extra_clprev();
  /* Co-op: same for extra bullets. */
  bullets_extra_clprev();
  /* Co-op: same for extra bombs. */
  bombs_extra_clprev();
}

/*
 * Table containing entity action function pointers.
 */
void (*ent_actf[])(U8) = {
  NULL,        /* 00 - zero means that the slot is free */
  e_rick_action,   /* 01 - 12CA */
  e_bullet_action,  /* 02 - 1883 */
  e_bomb_action,  /* 03 - 18CA */
  e_them_t1a_action,  /* 04 - 2452 */
  e_them_t1b_action,  /* 05 - 21CA */
  e_them_t2_action,  /* 06 - 2718 */
  e_them_t1a_action,  /* 07 - 2452 */
  e_them_t1b_action,  /* 08 - 21CA */
  e_them_t2_action,  /* 09 - 2718 */
  e_them_t1a_action,  /* 0A - 2452 */
  e_them_t1b_action,  /* 0B - 21CA */
  e_them_t2_action,  /* 0C - 2718 */
  e_them_t1a_action,  /* 0D - 2452 */
  e_them_t1b_action,  /* 0E - 21CA */
  e_them_t2_action,  /* 0F - 2718 */
  e_box_action,  /* 10 - 245A */
  e_box_action,  /* 11 - 245A */
  e_bonus_action,  /* 12 - 242C */
  e_bonus_action,  /* 13 - 242C */
  e_bonus_action,  /* 14 - 242C */
  e_bonus_action,  /* 15 - 242C */
  e_sbonus_start,  /* 16 - 2182 */
  e_sbonus_stop  /* 17 - 2143 */
};


/*
 * Run entities action function
 *
 */
void
ent_action(void)
{
  U8 i, k;

  IFDEBUG_ENTS(
    sys_printf("xrick/ents: --------- action ----------------\n");
    for (i = 0; ent_ents[i].n != 0xff; i++)
      if (ent_ents[i].n) {
	sys_printf("xrick/ents: slot %#04x, entity %#04x", i, ent_ents[i].n);
	sys_printf(" (%#06x, %#06x), sprite %#04x.\n",
		   ent_ents[i].x, ent_ents[i].y, ent_ents[i].sprite);
      }
    );

  for (i = 0; ent_ents[i].n != 0xff; i++) {
    if (ent_ents[i].n) {
      k = ent_ents[i].n & 0x7f;
      if (k == 0x47)
	e_them_z_action(i);
      else if (k >= 0x18)
        e_them_t3_action(i);
      else
	ent_actf[k](i);
    }
  }

  /*
   * Co-op (Stage 3): tick Ricks 1..3 after the main entity loop. They
   * live in extra_rick_ents (approach (b)), so the loop above never
   * sees them. Order is "P1 first, then enemies, then extras", which
   * matches the existing scroll/camera logic that keys off Rick 0.
   */
  ricks_extra_action();
  /* Co-op: tick Bullets 1..3 (Bullet 0 is dispatched via ent_actf[2] above). */
  bullets_extra_action();
  /* Co-op: tick Bombs 1..3 (Bomb 0 is dispatched via ent_actf[3] above). */
  bombs_extra_action();
}


/* eof */
