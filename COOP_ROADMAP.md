# Co-op Multiplayer Roadmap

A staged plan for converting xrick from single-player to up to 4-player local co-op.
Players are referred to as "Ricks." Player 1 = Rick #0 (index), Player 2 = Rick #1, etc.

## Design summary (locked-in scope)

- Up to **4 Ricks**, stored in an array. Each tick, every active Rick is updated and rendered.
- **Controls** (single-player keyboard layout removes the legacy Z/X/K/O bindings):
  - P1: arrow keys + Space (fire) — same as today.
  - P2: WASD + Left Shift (fire).
  - P3: IJKL + Return (fire).
  - P4: no controls (slot reserved, uncontrolled).
- **Player count**: defaults to 1. Pressing `1`/`2`/`3`/`4` sets the active player count. Increasing the count spawns the new player(s) immediately at the current location. Decreasing should instantly despawn corresponding Rick entities.
- **Room transitions**: on entering a new submap, all active Ricks spawn at the same point.
- **Shared resources**: ammo, dynamite, lives are shared (no change to `env_bullets`, `env_bombs`, `env_lives`).
- **Death**: a dead Rick stays dead. When *all* active Ricks are dead, lose one life and restart the current submap (same as today's restart path).
- **Collision**: no player↔player collision for now.
- **Camera**: continues to follow Rick #0 (P1) only.

---

## Stage 1 — Refactor: turn `e_rick` into an array of Ricks  *(DONE)*

Goal: structurally support N Ricks without changing gameplay (still 1 active player).

**Status:** complete. `rick_t` array, `R_*` macros, per-Rick `e_rick_*` signatures landed. Approach (b) chosen — extra Rick entities will live in a separate array (still to be added in Stage 3; Ricks 1–3 currently have `ent_slot == 0`). Gameplay at `rick_count == 1` is unchanged. Build clean (verified by user).

### 1.1 Introduce a Rick struct and array
- Add `rick_t` containing the per-Rick state currently held as file-scope globals in `xrick/src/e_rick.c`:
  - `state` (was `e_rick_state`)
  - `stop_x`, `stop_y` (was `e_rick_stop_x`, `e_rick_stop_y`)
  - `atExit` (was `e_rick_atExit`)
  - `scrawl`, `trigger`, `offsx`, `ylow`, `offsy`, `seq` (currently file-static in `e_rick.c`)
  - `save_x`, `save_y`, `save_crawl` (used by `e_rick_save` / `e_rick_restore`)
  - `ent_slot` — index into `ent_ents[]` for this Rick's entity (P1 keeps slot 1; new slots needed for P2–P4, see 1.2).
- Add `#define RICK_MAX 4` and `extern rick_t ricks[RICK_MAX];` in `e_rick.h`.
- Add `extern U8 rick_count;` (active player count, default 1) and `extern U8 rick_active[RICK_MAX];` (per-slot active flag).
- Replace the macros `E_RICK_STSET/STRST/STTST(X)` with versions that take a Rick index (`R_STSET(r, X)`), and add a thin compat wrapper for any call sites that should *only* affect P1 (e.g. cheat / init code that resets rick state).

### 1.2 Allocate entity slots for additional Ricks
- `ENT_ENTSNUM` is `0x0c` (12) and slot 1 is reserved for Rick today. Either:
  - **(a)** Bump `ENT_ENTSNUM` to make room for slots `1`–`4` reserved for Ricks, then renumber the bullet/bomb/them slot ranges referenced in `xrick/src/ents.c` (`ent_creat1` uses 0x04–0x09, `ent_creat2` uses 0x09–0x0c).
  - **(b)** *Recommended*: keep the existing slot map, add a separate `extra_rick_ents[3]` array, and have `ent_action` / `ents_paintAll` iterate over those in addition to `ent_ents[]`. This avoids touching the slot range constants and the entity creation logic that depends on them.
- Decide and document the choice. Sub-steps below assume **(b)**.

### 1.3 Generalize `e_rick_action` / `e_rick_action2` / `e_rick_z_action`
- Change signatures to take a Rick index (or pointer to `rick_t` + the entity to operate on).
- Replace every `E_RICK_ENT` / `E_RICK_STTST(...)` with the per-Rick equivalent.
- Replace direct reads of `control_status` with `ricks[i].control_status` (fed in Stage 2).
- `e_rick_save` / `e_rick_restore`: take Rick index. Called per-Rick by `game_save` / `restart`.
- `e_rick_gozombie(U8 i)`.
- `e_rick_boxtest`: the entity loop in `ents.c` already passes a slot. For each "is X colliding with a Rick?" call site (search for `e_rick_boxtest` and any direct `ent_ents[1]` reads in `e_them.c`, `e_bomb.c`, `e_bullet.c`, `e_box.c`, `e_bonus.c`, `e_sbonus.c`), iterate over all active Ricks and trigger if any matches.

### 1.4 Update `game.c` for per-Rick state
- `init()` (`xrick/src/game.c:822`): initialize all 4 entity slots used by Ricks (only Rick 0 active by default). Reset all `rick_t` state.
- `CTRL_RICK` state (`xrick/src/game.c:604`): change "is Rick dead?" from `E_RICK_STTST(E_RICK_STDEAD)` to "are all active Ricks dead?" Submap-exit check (`e_rick_atExit`) keys off Rick 0 only (see 1.5).
- `CTRL_SCROLL` (`xrick/src/game.c:641`): reads `ent_ents[1].y` to decide scroll. Keep using Rick 0 → camera follows P1.
- `restart()` (`xrick/src/game.c:895`): clear DEAD/ZOMBIE for *all* Ricks; call `e_rick_restore` for all active Ricks.
- `game_save()` (`xrick/src/game.c:923`): call `e_rick_save` for all active Ricks.

### 1.5 Submap transition: spawn all Ricks together
- Only Rick 0 drives `e_rick_atExit` (camera-follower). When transitioning (`NEXT_SUBMAP` / `NEXT_MAP` in `game.c`), copy Rick 0's spawn position to all active Ricks and clear their per-Rick state appropriately. This satisfies "all players spawn in the same spot on new room entry."
- Decide what other Ricks do while P1 is mid-screen-transition: simplest is to freeze them (skip their action) until the scroll completes.

### 1.6 Rendering
- `ents_paintAll` (`xrick/src/ents.c:341`) iterates `ent_ents[]`. With approach (b), add a follow-up loop that paints each extra Rick using `sprites_paint2`, mirroring the existing draw + dirty-rect bookkeeping. With (a), no extra loop needed.
- Confirm sprite numbers used per Rick. For now all 4 use the same Rick sprites; palette differentiation is out of scope.

### Stage 1 acceptance
- Build clean. With `rick_count == 1`, gameplay is byte-for-byte identical to before. P1 plays as today. ✓

---

## Stage 2 — Per-player input

Goal: deliver one `control_status` per Rick rather than one global.

### 2.1 Per-player control state
- Replace global `control_status` / `control_last` with arrays indexed by player (e.g. `U8 control_status[RICK_MAX];`). Or keep `control_status` as P1's status and add `control_status_p[]` for all — pick one and apply consistently.
- All non-gameplay UI code that reads `control_status` (pause, exit, end, fire-on-menus, screens in `scr_*.c`, `screen_xrick`, `screen_pause`, `screen_introMain`, `screen_introMap`, `screen_gameover`, `screen_getname`) keeps reading P1's status.
- Per-Rick gameplay code (`e_rick_action2`) reads the slot's own `control_status`.

### 2.2 Keyboard mapping in `syskbd.c` / `sysevt.c`
- Drop the `Z`/`X`/`K`/`O` legacy bindings from `xrick/src/syskbd.c:25-32`. P1 = arrows + Space only.
- Add per-player scancode tables:
  - P2: `W`/`A`/`S`/`D` + Left Shift (proposed) for fire.
  - P3: `I`/`J`/`K`/`L` + Right Shift (or Enter) for fire.
  - P4: none.
  - Note: P3's K conflicts with the removed legacy P1 down-key — fine because the legacy bindings are gone.
- Rewrite `processEvent()` in `xrick/src/sysevt.c` so each scancode SET/CLR targets the right player's `control_status` slot. Pause/end/exit/F-keys remain global (set on P1 / global flags as appropriate).

### 2.3 Joystick
- Out of scope for Stage 2. Either disable `ENABLE_JOYSTICK` for the multi-player build or hard-route joystick to P1.

### 2.4 Player-count toggle
- In `processEvent()` (KEYDOWN), handle `SDL_SCANCODE_1`..`_4`:
  - Set `rick_count` to the chosen value.
  - For any newly-activated Rick: mark active, copy P1's current `(x, y)` into its entity, reset per-Rick state (clear DEAD/ZOMBIE, set sprite, clear `offsx/offsy/ylow/seq`).
  - Deactivating (e.g. user presses `1` while 3 are alive): mark inactive and zero its entity `n` so it stops rendering and being processed. Decide whether deactivation is allowed mid-game or only between rooms (recommend: allowed any time, no resource refund).
- Suppress these hotkeys outside of active gameplay states (XRICK, MAIN_INTRO, GAMEOVER, GETNAME) to avoid surprising behavior on menus.

### Stage 2 acceptance
- P1 can play with arrows + Space.
- Pressing `2` spawns P2 at P1's location; WASD + Shift controls them. Same for `3` (IJKL).
- Pressing `4` spawns a stationary P4.
- Pressing `1` removes P2–P4.

---

## Stage 3 — Per-player gameplay loop

Goal: every active Rick is simulated and rendered each tick.

### 3.1 Action loop
- In `ent_action()` (`xrick/src/ents.c:481`), invoke `e_rick_action(slot)` for every active Rick slot. With approach (b), iterate the extra Rick array explicitly.
- Order: P1 first (preserves existing scroll/camera behavior, since `CTRL_SCROLL` and `e_rick_atExit` key off P1).

### 3.2 Death handling
- A Rick that goes DEAD stays DEAD (its action function early-returns; sprite cleared so it isn't drawn).
- The "all active Ricks dead?" check lives in `CTRL_RICK` (Stage 1.4). When true: `--env_lives`, restart submap. Restart re-spawns all *active* Ricks at the saved spawn point with cleared DEAD/ZOMBIE state.
- Inactive Ricks (count was lowered) do not count toward "all dead."

### 3.3 Bullet / bomb / enemy interactions with multiple Ricks
- For each call site that does collision tests against Rick (search `e_rick_boxtest` and any direct comparisons with `ent_ents[1].x`/`.y`/`.w`/`.h`), iterate over active Ricks. Examples to verify:
  - `e_them.c` — enemies killing the player on contact.
  - `e_bomb.c` — explosions can kill Rick.
  - `e_bullet.c` — bullets are Rick-owned; treat them as belonging to whichever Rick fired (track owner if needed for sound/feedback; otherwise leave as a single shared bullet pool which already matches the "shared ammo" rule).
  - `e_bonus.c` / `e_sbonus.c` / `e_box.c` — pickups: any Rick can collect; resources are shared so the existing single-pickup logic still works, just gated on per-Rick collision.
- Bullets/bombs are still single-instance globals (`E_BULLET_ENT.n`, `E_BOMB_ENT.n`). Per the design, ammo is shared, so this is acceptable for now — only one bullet and one bomb in the air at a time, regardless of which player fired. Document this as an intentional MVP limitation.

### 3.4 Stop (sticky) state
- `E_RICK_STSTOP` is set when P1 fires while moving and triggers stop-marks. With multiple Ricks, decide: each Rick has its own stop state (recommended, already implied by Stage 1.1), and stop-mark triggers fire when *any* Rick stops on the trigger box. Verify by grepping for `E_RICK_STTST(E_RICK_STSTOP)` and `ENT_FLG_TRIGSTOP` usage in `ents.c` / `e_them.c`.

### Stage 3 acceptance
- 2–3 players can run, jump, climb, crawl, and shoot independently in the same submap.
- Killing one player leaves the others playing.
- Last player dying restarts the submap with all active players respawned and one life lost.

---

## Stage 4 — Polish & known gaps

These are explicitly out-of-scope for the initial co-op implementation but worth tracking.

- **Player↔player collision**: none for now (per design). Consider adding optional pass-through vs. solid mode later.
- **Camera**: P1-only. Players can wander off-screen and become unreachable / undeath-able until P1 scrolls back. Acceptable for MVP.
- **Per-player sprite tinting / numbering**: not in MVP. Add palette-swap or a small overhead "P2/P3" tag later if players can't tell themselves apart.
- **Per-player sounds**: jump/walk/die SFX will play on every Rick; may overlap. Consider deduping or limiting to P1 if it's annoying.
- **Bullet/bomb instancing**: shared global instance limits co-op feel. Future work: one bullet + one bomb per Rick.
- **Joystick / second controller support**: future work.
- **Ammo/life pickup feedback**: with shared resources, status bar updates already work. Make sure `env_paintGame` still renders correctly when multiple Ricks pick up bonuses on the same frame.
- **Devtools / cheats**: re-test F7/F8/F9 cheats with multiple Ricks (invincibility should apply to all; trainer fine since resources are shared).
- **Pause / focus loss**: confirm `control_active == FALSE` still pauses correctly when per-player input arrays are introduced.
- **Save/restore on death**: `e_rick_save` is currently called on entering each submap; with N Ricks, save P1's spawn and re-spawn all active Ricks from it (per design).

---

## File-by-file summary of expected changes

| File | Stage | What changes |
| --- | --- | --- |
| `xrick/include/e_rick.h` | 1 | `rick_t` struct, `RICK_MAX`, `ricks[]`, per-Rick state-bit macros, updated function prototypes. |
| `xrick/src/e_rick.c` | 1 | Move file-static state into `rick_t`; rewrite `e_rick_action*` to take a Rick index. |
| `xrick/include/control.h` | 2 | Per-player `control_status[]` (or parallel array). |
| `xrick/src/control.c` | 2 | Storage for per-player state. |
| `xrick/src/syskbd.c` | 2 | Drop Z/X/K/O; add WASD + IJKL scancode tables. |
| `xrick/src/sysevt.c` | 2 | Route key events to the correct player's slot; handle `1`/`2`/`3`/`4` player-count keys. |
| `xrick/src/game.c` | 1, 3 | `init`, `restart`, `game_save`, `CTRL_RICK`, `CTRL_SCROLL` updated for multiple Ricks; spawn all on submap entry. |
| `xrick/src/ents.c` | 1, 3 | `ent_action` runs each active Rick; `ents_paintAll` paints each (or iterates an extra array). |
| `xrick/src/e_them.c`, `e_bomb.c`, `e_bullet.c`, `e_box.c`, `e_bonus.c`, `e_sbonus.c` | 3 | Replace single-Rick collision/trigger checks with a loop over active Ricks. |

---

## Quick test plan per stage

- **Stage 1**: launch, complete the first 2 submaps as P1 only. No regressions vs. master.
- **Stage 2**: launch, press `2`, drive P2 with WASD across a screen; press `1` to drop back to solo; verify pause/exit still work.
- **Stage 3**: launch, press `3`, run all three around the same screen, kill them one by one, verify last-death restart resets to spawn point, lose one life. Repeat with `4` (P4 sits where spawned).
- **Stage 4**: regression pass on cheats, fullscreen toggle, sound mute, scrolling at top/bottom of submap.
