# Realtime camera scroll — approach options

The current camera scroll pauses gameplay for 8 ticks while the world
shifts 64 px (one 8-row block). The "pause" is not a real pause: while
`game_state` is `SCROLL_UP`/`SCROLL_DOWN`, the cycle returns before
`CTRL_ACTION` runs, so Rick / enemies / bullets / bombs all stop
updating. The existing pixel-level interpolation (`game_scroll_step` +
`sysvid_snapshot_playfield` + the OLD/NEW playfield lerp around
game.c:404-426) is orthogonal to that — it only smooths the render.

Both approaches below preserve the gameplay-state machinery untouched:
the row shift still mutates `map_map`, translates every entity's y,
kills any that fall off the world, advances `map_frow`, and on the
8th cumulative shift calls `map_expand` + `ent_actvis` to refresh the
hardbuffers. That is the level-loading. Neither approach changes those
mechanics — only **when and around what other work** the shifts happen.

---

## Approach A — Per-tick batch, gameplay not gated (NOT YET TRIED)

Keep the 8-shift batch as a unit of work, but stop using a dedicated
state to gate CTRL_ACTION.

- Drop the `SCROLL_UP` / `SCROLL_DOWN` states (or keep them but never
  enter them).
- When the trigger fires, store a `scroll_pending` counter (`±8`, sign =
  direction).
- Each tick, at the top of `CTRL_ACTION` (or at the same point in the
  flow today's CTRL_SCROLL sits), if the counter is non-zero, do one
  row-shift, decrement, **then fall through into the normal entity
  action.**
- Drop the `game_period = SCROLL_PERIOD` override. SCROLL_PERIOD was
  speeding the scroll up to compensate for the pause; in realtime you
  want regular tick rate.
- While a batch is in flight, ignore new triggers (re-trigger guard) —
  finish the current 8-shift block before considering whether Rick is
  still outside the deadzone.
- Boundary work (`map_expand` + `ent_actvis`) fires once per batch, on
  the 8th shift, exactly as today.

**Pros**
- Minimal behavioural change. Camera moves in fixed 64-px lurches just
  like the original, only without pausing gameplay.
- No accumulator state across direction changes — each batch is a
  self-contained 8-shift unit, identical in shape to today's batch.
- Re-trigger guard is trivial (a single "are we in a batch?" check).
- Block-boundary alignment is automatic: every batch is exactly 8
  shifts so `map_expand` always fires at the same cadence the engine
  was originally designed for.

**Cons**
- Visually chunkier than true follow-cam: once a batch starts it
  always finishes, even if Rick darts back inside the deadzone or
  reverses direction immediately. The camera commits to a full 64-px
  pan whether or not Rick is still asking for it.
- Rick can run ahead of or behind the batch (he's no longer frozen
  during the pan), which can place him on the wrong side of the
  deadzone when the batch ends and immediately trigger another batch
  in the opposite direction.

---

## Approach B — True follow-cam, one shift per tick (IMPLEMENTED AND THEN REVERTED)

No batch state at all. Every tick, look at Rick's y; if he is outside
the deadzone, shift one row toward him; otherwise don't.

- Replace the trigger + 8-shift batch with a per-tick check.
- Each tick that needs to shift: do one row-shift, then fall through
  into CTRL_ACTION.
- Track a `rt_accum` counter that goes `+1` per up-shift, `-1` per
  down-shift, so `map_expand` + `ent_actvis` fire when `|rt_accum| ==
  8` (then reset to 0). Direction reversals naturally cancel without
  spurious expands.
- No `game_period` override.
- Reset `rt_accum` to 0 on every submap entry (`map_init` →
  `scroll_reset()`), where the freshly-expanded `map_frow` is the
  alignment reference.

**Pros**
- Smoothest visual feel. Camera follows Rick row-by-row instead of in
  64-px lurches.
- Stateless trigger: no "batch in flight" concept, no re-trigger
  guard, no period swap. The follow-cam runs on a single boolean
  ("is Rick outside the deadzone right now?").
- Reverses cleanly: if Rick changes direction mid-pan the camera
  immediately follows.

**Cons (and probably why it's buggy right now)**
- Direction reversals within a partial block (e.g., shift up 4, then
  shift down 4) rely on the assertion that the off-screen hardbuffer
  rows are still valid for the rows being shifted back in. This is
  true only for the rows you originally came from — but the moment
  you cross a block boundary in either direction, an `map_expand`
  overwrites *all* of `map_map`, including the hardbuffer rows on the
  far side. A subsequent reversal across that boundary would need a
  fresh expand at the **previous** alignment, which the `|rt_accum| ==
  8` rule does fire — but only *after* the 8th shift in the reversed
  direction, so the 1st–7th rows shifted back come from stale buffer
  data. Likely cause of map-tile corruption / wrong-tile glitches.
- Block-boundary alignment is *not* automatic. The original engine
  assumes shifts always come in 8-row aligned bursts starting from a
  freshly-expanded alignment. Per-row shifts that can stop and
  reverse anywhere can leave the engine in alignments it was never
  designed to handle (e.g., entity activation bands computed off
  `map_frow + MAP_ROW_HBTOP` may overlap an already-active band, or
  miss a band, after a partial reversal).
- Rick can hover on the deadzone boundary and cause the camera to
  flip on/off every tick (jitter). No hysteresis was added.
- Entity activation (`ent_actvis`) was designed to fire once per
  8-row band. With realtime, if you scroll up 8 (activate band A),
  back down 8 (activate band B at the original `map_frow` —
  re-activating entities that were already active), then up 8 again
  (activate band A again — re-activating *those*), you may double-
  activate entities or activate them at wrong y positions. Likely
  cause of enemy-duplication / enemy-respawn bugs.
- Bullets/bombs/co-op-extras are translated every shift via
  `*_extra_scroll(±8)`. With many partial-reversal shifts in a
  short window, accumulated rounding or off-by-one in those
  translations could compound.

---

## Recommendation if Approach B keeps misbehaving

Revert and try **Approach A**. It is much closer to the engine's
original assumptions:

- Shifts always come in 8-row aligned bursts.
- `map_expand` always fires after exactly 8 cumulative shifts in one
  direction.
- `ent_actvis` activates each band exactly once per crossing.
- No partial-block reversal state to reason about.

The only thing it gives up is the row-by-row visual smoothness. The
camera still lurches 64 px at a time, but gameplay no longer freezes.
That alone is probably the more interesting upgrade — and the pixel-
level interpolation you already have (`game_scroll_step` lerp) still
makes the 64-px lurch glide visually.

---

## A possible Approach C (if you want smoothness AND stability)

Hybrid: per-tick shifts (B's visual feel) but only commit to
direction reversals at 8-row boundaries (A's alignment guarantee).

- Per-tick: if a batch is in flight, do the next shift in that
  direction. Otherwise, if Rick is outside the deadzone, start a new
  batch in his direction.
- A batch reads "8 shifts in the current direction." If Rick reverses
  mid-batch, the batch still completes (so block boundaries stay
  aligned), then the next batch goes the other way.

This gives you row-by-row smoothness *within* a batch and locks
boundary alignment *across* batches. Costs are the same re-trigger
guard as A and a small camera-lag when Rick reverses sharply.

---

## File map (for revert / re-attempt)

Approach B touched these files:

- `xrick/include/scroller.h` — added `scroll_realtime_step`, `scroll_reset`
- `xrick/src/scroller.c` — extracted helpers, added realtime path
- `xrick/include/game.h` — added `game_realtime_scroll` extern
- `xrick/src/game.c` — defined `game_realtime_scroll`, branched CTRL_SCROLL
- `xrick/include/inifile.h` — added `inifile_realtimeScroll`
- `xrick/src/inifile.c` — parse/save `RealtimeScroll`
- `xrick/src/maps.c` — `scroll_reset()` from `map_init`, include
- `data/xrick.ini` — `RealtimeScroll = 1` line

The helper extraction in `scroller.c` (the four static helpers
`shift_up_one_row`, `shift_down_one_row`, `boundary_up`,
`boundary_down`) is independent of which approach is taken and could
be kept as a refactor regardless — Approach A would call the same
helpers from a counter-driven path, just gated by `scroll_pending`
instead of by Rick's instantaneous deadzone position.
