/*
 * xrick/include/e_rick.h
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

#ifndef _E_RICK_H
#define _E_RICK_H

#include "system.h"
#include "ents.h"  /* ent_t for extra_rick_ents */

/*
 * Co-op refactor (Stage 1): turn the single global Rick into an array of
 * up to RICK_MAX Ricks. Rick #0 is Player 1 (camera follower). For now
 * only Rick 0 is active; Stages 2-3 wire up input and the per-Rick action
 * loop. See COOP_ROADMAP.md.
 */
#define RICK_MAX 4

/*
 * Per-Rick state. All of these fields used to be file-scope globals or
 * file-statics in e_rick.c, which meant the game could only ever model one
 * Rick. They are now per-Rick so the engine can hold up to RICK_MAX of them.
 */
typedef struct {
	U8  state;        /* E_RICK_ST* bitfield */
	U16 stop_x, stop_y;
	U8  atExit;       /* TRUE when this Rick is exiting the submap */
	U8  scrawl;       /* prev-frame crawl flag (used to defer stand-up firing) */
	U8  trigger;      /* fire-button edge tracker (semi-auto gun) */
	S8  offsx;
	U8  ylow;
	S16 offsy;
	U8  seq;          /* animation sequence counter */
	U16 save_x, save_y;
	U8  save_crawl;
	U8  ent_slot;     /* index into ent_ents[] backing this Rick (Rick 0 -> 1) */
	U8  prev_stopped; /* one-shot sound flag for the stop-mark stick sfx */
} rick_t;

extern rick_t ricks[RICK_MAX];
extern U8 rick_count;             /* how many Ricks are active (defaults to 1) */
extern U8 rick_active[RICK_MAX];  /* per-slot active flag */

/* Reset all Rick state to defaults. Called from game init(). */
extern void ricks_init(void);

/*
 * Legacy entity-slot constants. Rick 0 lives in ent_ents[1]; the other
 * three Ricks will eventually live in a separate array (see Stage 1.2 in
 * COOP_ROADMAP.md, approach (b)). E_RICK_NO and E_RICK_ENT therefore only
 * refer to P1 and should stop being used as the simulation goes multi-Rick.
 */
#define E_RICK_NO 1
#define E_RICK_ENT ent_ents[E_RICK_NO]

/*
 * State-bit values (unchanged).
 */
#define E_RICK_STSTOP   0x01
#define E_RICK_STSHOOT  0x02
#define E_RICK_STCLIMB  0x04
#define E_RICK_STJUMP   0x08
#define E_RICK_STZOMBIE 0x10
#define E_RICK_STDEAD   0x20
#define E_RICK_STCRAWL  0x40

/*
 * Per-Rick state-bit macros. Prefer these in any new / multi-Rick code.
 */
#define R_STSET(r, X) (ricks[r].state |= (X))
#define R_STRST(r, X) (ricks[r].state &= ~(X))
#define R_STTST(r, X) (ricks[r].state &  (X))

/*
 * Legacy single-Rick macros. Kept as thin wrappers so call sites that only
 * concern P1 (cheats, screens, anything camera-bound) don't have to be
 * touched. New code should use R_STSET/R_STRST/R_STTST.
 */
#define E_RICK_STSET(X) R_STSET(0, (X))
#define E_RICK_STRST(X) R_STRST(0, (X))
#define E_RICK_STTST(X) R_STTST(0, (X))

/*
 * Legacy global aliases for Rick 0's fields. These let the rest of the
 * codebase keep reading "the Rick state" without caring that it's now a
 * per-Rick struct under the hood.
 */
#define e_rick_state  (ricks[0].state)
#define e_rick_atExit (ricks[0].atExit)
#define e_rick_stop_x (ricks[0].stop_x)
#define e_rick_stop_y (ricks[0].stop_y)

extern void e_rick_save(U8 i);
extern void e_rick_restore(U8 i);
extern void e_rick_action(U8);     /* entity-action callback; takes ent slot */
extern void e_rick_gozombie(U8 i);
extern U8   e_rick_boxtest(U8 i, U8 e);

/*
 * Co-op (Stage 3): Ricks 1..3 do not live in ent_ents[] (approach (b) in
 * COOP_ROADMAP.md). They get their own ent_t records here. Use
 * ricks_get_ent(i) to access either backing store transparently.
 */
extern ent_t extra_rick_ents[RICK_MAX - 1];
extern ent_t *ricks_get_ent(U8 i);

/*
 * Co-op (Stage 3): tick / paint / scroll hooks for the extra Ricks.
 * - ricks_extra_action: called from ent_action() right after the main
 *   entity loop has run (Rick 0 is part of that loop).
 * - ricks_extra_paint_*: called from ents_paintAll() so the extras get
 *   the same erase/draw/dirty-rect bookkeeping as ent_ents[].
 * - ricks_extra_clprev: called from ent_clprev() to reset prev_n.
 * - ricks_extra_scroll: called from scroll_up/scroll_down to keep extras
 *   tracking the scrolled world.
 * - ricks_spawn_at_p1: places every active extra Rick at Rick 0's current
 *   (x,y) with a fresh state. Called on every submap entry / restart.
 */
extern void ricks_extra_action(void);
extern void ricks_extra_clprev(void);
extern void ricks_extra_scroll(S16 dy);
extern void ricks_spawn_at_p1(void);

#endif

/* eof */
