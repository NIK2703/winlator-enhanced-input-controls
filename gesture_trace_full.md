# TS Main Finger Gesture State Machine — Full 64-combination Trace

Bit encoding: `S<<5 | L<<4 | D<<3 | Sd<<2 | Ld<<1 | Dd<<0`

Abbreviations:
- **h(X)** = `hold_actions(X)` (press-and-keep-held)
- **x(X)** = `execute_actions(X)` (fire-and-forget)
- **→LP** = state transitions to LONG_PRESSING
- **→DRG** = state transitions to DRAGGING
- **→DTW** = state transitions to DOUBLE_TAP_WAITING
- **→IDLE** = state transitions to IDLE
- **↻hold** = single-tap-hold timer fires
- **↻LP** = long-press timer fires
- **rel** = `release_held_actions()` called
- **𝒹** = deferred (saved for later execution)
- **Z-DTW** = zombie DT_WAITING (location only, no pending actions)
- **/** separates alternative paths depending on user action timing
- **`--`** = no action
- **⚠️** = potential bug or lost binding

### Combo mapping (S L D Sd Ld Dd):

| # | S L D Sd Ld Dd | Down | ↻LP | ↻hold | Drag | Finger-up | **Binding that fires** |
|---|---|---|---|---|---|---|---|
| 0 | 0 0 0 0 0 0 | -- | -- | -- | -- | -- | **none** |
| 1 | 0 0 0 0 0 1 | -- | -- | -- | -- | Z-DTW | **none** (location only) |
| 2 | 0 0 0 0 1 0 | -- | L=0→→LP | -- | Ld➤→DRG | LP: rel / TAP:-- | **none** unless drag→**Ld** |
| 3 | 0 0 0 0 1 1 | -- | L=0→→LP | -- | Ld➤→DRG | LP: rel / TAP: Z-DTW | **none** unless drag→**Ld** |
| 4 | 0 0 0 1 0 0 | -- | -- | -- | Sd➤→DRG | x(S)=none→IDLE | **none** unless drag→**Sd** |
| 5 | 0 0 0 1 0 1 | -- | -- | -- | Sd➤→DRG | Z-DTW | **none** unless drag→**Sd** |
| 6 | 0 0 0 1 1 0 | -- | L=0→→LP | -- | Sd‖Ld➤ | TAP:-- / LP: rel | **none** unless drag→**Sd/Ld** |
| 7 | 0 0 0 1 1 1 | -- | L=0→→LP | -- | Sd‖Ld➤ | TAP: Z-DTW / LP: rel | **none** unless drag→**Sd/Ld** |
| 8 | 0 0 1 0 0 0 | -- | -- | -- | -- | →DTW (D saved) | **D** on 2nd tap/DT timeout |
| 9 | 0 0 1 0 0 1 | -- | -- | -- | -- | →DTW (D saved) | 1st tap: **D** on 2nd tap → D𝒹 + Dd drag, x(D) on up |
| 10| 0 0 1 0 1 0 | -- | L=0→→LP | -- | Ld➤ | TAP: →DTW / LP: rel | **D** on 2nd tap (quick) or **Ld** drag |
| 11| 0 0 1 0 1 1 | -- | L=0→→LP | -- | Ld➤ | TAP: →DTW / LP: rel | **D** on 2nd tap or **Ld** drag |
| 12| 0 0 1 1 0 0 | -- | -- | -- | Sd➤ | TAP: →DTW / DRG: rel | **Sd** drag or **D** on 2nd tap |
| 13| 0 0 1 1 0 1 | -- | -- | -- | Sd➤ | TAP: →DTW / DRG: rel | **Sd** drag or **D** on 2nd tap |
| 14| 0 0 1 1 1 0 | -- | L=0→→LP | -- | Sd‖Ld➤ | TAP: →DTW / LP/DRG: rel | **drag** or **D** on 2nd tap |
| 15| 0 0 1 1 1 1 | -- | L=0→→LP | -- | Sd‖Ld➤ | TAP: →DTW / LP/DRG: rel | **drag** or **D** on 2nd tap |
| 16| 1 0 0 0 0 0 | **h(S)** | -- | -- | -- | D=0,Dd=0: h(S)+rel | **S** (press+release) |
| 17| 1 0 0 0 0 1 | S→𝒹, D(0)→pend | -- | h(S) | -- | Path4: D=0,Dd=1→h(S)+Z-DTW+rel | **S** (press+release) + Z-DTW |
| 18| 1 0 0 0 1 0 | **h(S)** | L=0→→LP | -- | Ld➤ | TAP: h(S)+rel / LP: rel | **S** (quick) or **Ld** drag |
| 19| 1 0 0 0 1 1 | S→𝒹, D(0)→pend | L=0→→LP | h(S) if TAP | Ld➤ | TAP: h(S)+Z-DTW+rel / LP: rel | **S** (hold then rel) or **Ld** drag ⚠️ S lost if LP fires before ↻hold |
| 20| 1 0 0 1 0 0 | -- (Sd=1) | -- | -- | Sd➤ | x(S)→IDLE | **x(S)** (fire-and-forget) or **Sd** drag |
| 21| 1 0 0 1 0 1 | -- | -- | -- | Sd➤ | x(S)+Z-DTW | **x(S)** + Z-DTW or **Sd** drag |
| 22| 1 0 0 1 1 0 | -- | L=0→→LP | -- | Sd‖Ld➤ | TAP: x(S) / LP: rel | **x(S)** (quick) or **drag** |
| 23| 1 0 0 1 1 1 | -- | L=0→→LP | -- | Sd‖Ld➤ | TAP: x(S)+Z-DTW / LP: rel | **x(S)**+Z-DTW or **drag** |
| 24| 1 0 1 0 0 0 | S→𝒹, D→pend | -- | h(S) →clear D | -- | Path4(D=1)→→DTW. S held→rel | **S** (press+rel) → **D** on 2nd tap/DT timeout |
| 25| 1 0 1 0 0 1 | S→𝒹, D→pend | -- | h(S) →clear D | -- | Path4(D=1)→→DTW. S held→rel | **S** → **D𝒹** on 2nd tap: x(D) on up |
| 26| 1 0 1 0 1 0 | S→𝒹, D→pend | L=0→→LP | h(S) if TAP | Ld➤ | TAP: →DTW+S+rel / LP: rel | **S** or **Ld** drag or **D** on 2nd tap ⚠️ S lost if LP before ↻hold |
| 27| 1 0 1 0 1 1 | S→𝒹, D→pend | L=0→→LP | h(S) if TAP | Ld➤ | TAP: →DTW+S+rel / LP: rel | **S** or **Ld** drag or **D** on 2nd tap ⚠️ S lost if LP before ↻hold |
| 28| 1 0 1 1 0 0 | -- (Sd=1) | -- | -- | Sd➤ | TAP: →DTW / DRG: rel | **Sd** drag or **D** on 2nd tap |
| 29| 1 0 1 1 0 1 | -- | -- | -- | Sd➤ | TAP: →DTW / DRG: rel | **Sd** drag or **D** on 2nd tap |
| 30| 1 0 1 1 1 0 | -- | L=0→→LP | -- | Sd‖Ld➤ | TAP: →DTW / LP/DRG: rel | **drag** or **D** on 2nd tap |
| 31| 1 0 1 1 1 1 | -- | L=0→→LP | -- | Sd‖Ld➤ | TAP: →DTW / LP/DRG: rel | **drag** or **D** on 2nd tap |

### Combinations 32–47 (L=1, S=0):

| # | S L D Sd Ld Dd | Down | ↻LP | ↻hold | Drag | Finger-up | **Binding that fires** |
|---|---|---|---|---|---|---|---|
| 32| 0 1 0 0 0 0 | -- | **h(L)**→LP | -- | L→L➤ | LP: rel / DRG: rel | **L** (held→rel) or **L** drag |
| 33| 0 1 0 0 0 1 | -- | **h(L)**→LP | -- | L→L➤ | TAP(early): Z-DTW / LP: rel | **L** or **L** drag (quick tap: Z-DTW, no action) |
| 34| 0 1 0 0 1 0 | -- | **h(L)**→LP | -- | Ld➤ | LP: rel | **L** or **Ld** drag |
| 35| 0 1 0 0 1 1 | -- | **h(L)**→LP | -- | Ld➤ | TAP(early): Z-DTW / LP: rel | **L** or **Ld** drag |
| 36| 0 1 0 1 0 0 | -- | **h(L)**→LP (no cancel: Sd=1) | -- | Sd➤ | TAP: x(S)=none / LP: rel | **L** or **Sd** drag |
| 37| 0 1 0 1 0 1 | -- | **h(L)**→LP | -- | Sd➤ | TAP: Z-DTW / LP: rel | **L** or **Sd** drag |
| 38| 0 1 0 1 1 0 | -- | **h(L)**→LP (Ld=1→no cancel) | -- | Sd‖Ld➤ | TAP: none / LP: rel | **L** or **Sd/Ld** drag |
| 39| 0 1 0 1 1 1 | -- | **h(L)**→LP | -- | Sd‖Ld➤ | TAP: Z-DTW / LP: rel | **L** or **Sd/Ld** drag |
| 40| 0 1 1 0 0 0 | -- | **h(L)**→LP | -- | L→L➤ | TAP(early): →DTW / LP: rel | **L** or **L** drag or **D** on 2nd tap |
| 41| 0 1 1 0 0 1 | -- | **h(L)**→LP | -- | L→L➤ | TAP: →DTW / LP: rel | **L** or **L** drag or **D** on 2nd tap |
| 42| 0 1 1 0 1 0 | -- | **h(L)**→LP | -- | Ld➤ | TAP: →DTW / LP: rel | **L** or **Ld** drag or **D** on 2nd tap |
| 43| 0 1 1 0 1 1 | -- | **h(L)**→LP | -- | Ld➤ | TAP: →DTW / LP: rel | **L** or **Ld** drag or **D** on 2nd tap |
| 44| 0 1 1 1 0 0 | -- | **h(L)**→LP | -- | Sd➤ | TAP: →DTW / LP: rel | **L** or **Sd** drag or **D** on 2nd tap |
| 45| 0 1 1 1 0 1 | -- | **h(L)**→LP | -- | Sd➤ | TAP: →DTW / LP: rel | **L** or **Sd** drag or **D** on 2nd tap |
| 46| 0 1 1 1 1 0 | -- | **h(L)**→LP | -- | Sd‖Ld➤ | TAP: →DTW / LP: rel | **L** or **drag** or **D** on 2nd tap |
| 47| 0 1 1 1 1 1 | -- | **h(L)**→LP | -- | Sd‖Ld➤ | TAP: →DTW / LP: rel | **L** or **drag** or **D** on 2nd tap |

---

### Combinations 48–63 (S=1, L=1) — Detailed trace

These are the most interesting because the interaction between S hold-timer and LP timer can cause S to be lost. The outcome depends critically on the relative timing of `double_tap_timeout_ms` (hold timer) vs `long_press_timeout_ms` (LP timer).

**Assumption for this trace:** `long_press_timeout_ms > double_tap_timeout_ms` (LP fires after hold), unless noted. L is "holdable".

#### 48: `1 1 0 0 0 0` — S=1, L=1, others 0

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=0, D=0, Dd=0 → **h(S)** immediately | TAP_WAITING | S held |
| **↻LP** | L=1, holdable, no cancel (Sd=0, Ld=0, and may/may not have moved — if moved: cancel_lp=!0&&!0&&moved=true → LP CANCELLED. If not moved: h(L), →LP) | →LP | **If not moved:** S+L both held. **If moved:** S still held, no LP. |
| **↻hold** | Not set (S was held immediately, no timer) | — | — |
| **Drag** | TAP_WAITING: Sd=0 → no drag. LONG_PRESSING: L→L drag. | →DRG | rel(S+L), h(L) |
| **Up** | TAP: D=0,Dd=0 → h(S) skip (already held), rel. →IDLE. LP: rel (S released). DRG: rel (L released). | — | — |
| **Final** | No-drag quick tap: **S fires** (hold+rel). Long-hold no-drag: **S then L** (both held, both released). Drag: **L drag**. | | |

**Binding that fires:** **S** (tap) or **L** (hold) or **L** drag. ✓ All reachable.

#### 49: `1 1 0 0 0 1` — S=1, L=1, Dd=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=0, has_dt=(D‖Dd)=true (Dd=1). Save S→deferred_tap, D(0)→pending_double. Set hold_timer=dt_timeout. S NOT held. | TAP_WAITING | def_tap=[S], pend_dbl=[] |
| **↻hold** | Timer fires first (dt_timeout < lp_timeout assumed): **h(S)**. Clear deferred_tap, pending_double. | TAP_WAITING | S held |
| **↻LP** | L=1, holdable, no cancel (Dd=1→irrelevant, Ld=0; Sd=0 → cancel_lp=!0&&!0&&moved → canceled if moved). If not moved: **h(L)**, →LP. | →LP | S held → S+L held |
| **Drag** | TAP_WAITING: Sd=0→no drag. LONG_PRESSING: L→L drag. | →DRG | rel(S+L), h(L) |

**⚠️ REVERSED TIMING:** If `lp_timeout < dt_timeout`:
| **↻LP** | Fires first: **h(L)**, →LP | →LP | L held |
| **↻hold** | state=LP→SKIP. S NOT held. | LP | L held (S lost) |

**Finger-up (TAP_WAITING, no LP) — quick tap:**
- D=0, Dd=1 → Path4: `has_dt=false`. Fire S: `!held && !Sd → h(S)`. Then zombie DTW.
- h(S), rel → **S fires** (press+release). + Z-DTW set.

**Finger-up (LONG_PRESSING, after LP):**
- L held → rel. **L fires**.

**Finger-up (DRAGGING, after L drag):**
- rel → **L drag released**.

**Final:** Quick tap → **S** + Z-DTW. Long-hold (dt < lp) → **S** (via hold timer) then **L** (via LP). Long-hold (lp < dt) → **L only, ⚠️ S lost!** Drag → **L** drag.

**Binding that fires:** **S** (quick tap), **S+L** (hold both), **L** (if LP wins race, S lost). **Z-DTW** on quick tap is useless (Dd-only, no D to confirm).

#### 50: `1 1 0 0 1 0` — S=1, L=1, Ld=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=0, has_dt=false (D=0, Dd=0). **h(S)** immediately. | TAP_WAITING | S held |
| **↻LP** | L=1, holdable. cancel_lp=!Sd&&!Ld&&moved = !0&&!1&&moved = **FALSE** (Ld=1 prevents cancel). **h(L)**, →LP. | →LP | S+L held |
| **↻hold** | Not set. | — | — |
| **Drag** | TAP: Sd=0 → no drag. LP: Ld→Ld drag. | →DRG | rel(S+L), h(Ld) |

**Up (TAP/early):** D=0,Dd=0 → h(S) skip (already held), rel. **S fires**.
**Up (LP):** rel → S+L released. **S and L fire**.
**Up (DRG):** rel → Ld released. **Ld drag**.

**Final:** **S** (tap), **S+L** (hold), **Ld** (drag). ✓ All reachable, no races.

#### 51: `1 1 0 0 1 1` — S=1, L=1, Ld=1, Dd=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=0, has_dt=true (Dd=1). S→deferred, D(0)→pend. hold_timer=dt_timeout. S NOT held. | TAP_WAITING | def=[S], pend=[] |
| **↻hold** (dt < lp) | **h(S)**. Clear def/pend. | TAP_WAITING | S held |
| **↻LP** | L=1, Ld=1→cancel_lp=FALSE. **h(L)**, →LP. | →LP | S+L held |
| **Drag** | TAP: no drag. LP: Ld→Ld drag. | →DRG | rel(S+L), h(Ld) |

**⚠️ REVERSED (lp < dt):**
| **↻LP** | **h(L)**, →LP | LP | L held |
| **↻hold** | state=LP→SKIP. S lost. | LP | L only |

**Up (TAP):** D=0,Dd=1 → h(S) (if not already held) + Z-DTW + rel.
**Up (LP):** rel → L released.
**Up (DRG):** rel → Ld released.

**Final:** Quick → **S** + Z-DTW. Hold dt<lp → **S** then **L**. Hold lp<dt → **L only, ⚠️ S lost**. Drag → **Ld**.

#### 52: `1 1 0 1 0 0` — S=1, L=1, Sd=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=1 → handle_ts_single_tap_hold returns immediately. No pre-hold. | TAP_WAITING | — |
| **↻LP** | L=1, holdable. cancel_lp=!Sd&&!Ld&&moved = !1&&!0&&moved = **FALSE** (Sd=1 prevents cancel). **h(L)**, →LP. | →LP | L held |
| **↻hold** | Not set. | — | — |
| **Drag** | TAP: Sd→Sd drag. LP: L→L drag. | →DRG | rel, h(Sd) or h(L) |

**Up (TAP):** D=0,Dd=0 → x(S) (Sd=1→execute) + rel. **x(S)**.
**Up (LP):** rel → L released. **L** fires.
**Up (DRG):** rel → Sd or L released.

**Final:** **x(S)** (tap), **L** (hold), **Sd** or **L** drag. ✓ All reachable.

#### 53: `1 1 0 1 0 1` — S=1, L=1, Sd=1, Dd=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=1 → no pre-hold. | TAP_WAITING | — |
| **↻LP** | L=1, Sd=1→no cancel. **h(L)**, →LP. | →LP | L held |
| **Drag** | TAP: Sd→Sd drag. LP: L→L drag. post-DT: Dd→Dd drag. | →DRG | varies |

**Up (TAP):** D=0,Dd=1 → x(S) (Sd=1, execute) + Z-DTW + rel.
**Up (LP):** rel → L.
**Up (DRG):** rel.

**Final:** **x(S)** + Z-DTW, **L** (hold), **Sd/L/Dd** drag. ✓

#### 54: `1 1 0 1 1 0` — S=1, L=1, Sd=1, Ld=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=1 → no pre-hold. | TAP_WAITING | — |
| **↻LP** | Ld=1, Sd=1 → cancel=FALSE. **h(L)**, →LP. | →LP | L held |
| **Drag** | TAP: Sd→Sd. LP: Ld→Ld. | →DRG | h(Sd) or h(Ld) |

**Up (TAP):** D=0,Dd=0 → x(S) (Sd=1) + IDLE.
**Up (LP):** rel → L.
**Up (DRG):** rel.

**Final:** **x(S)**, **L**, **Sd/Ld** drag. ✓

#### 55: `1 1 0 1 1 1` — S=1, L=1, all except D

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=1 → no pre-hold. | TAP_WAITING | — |
| **↻LP** | Ld=1→no cancel. **h(L)**, →LP. | →LP | L |
| **Drag** | Sd, Ld, or Dd (post-DT drag). | →DRG | varies |

**Up (TAP):** D=0,Dd=1 → x(S) + Z-DTW.
**Up (LP/DRG):** rel.

**Final:** **x(S)**+Z-DTW, **L**, **Sd/Ld/Dd** drag. ✓

#### 56: `1 1 1 0 0 0` — S=1, L=1, D=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=0, has_dt=true (D=1). S→deferred, D→pending. hold_timer=dt_timeout. S NOT held. | TAP_WAITING | def=[S], pend=[D] |
| **↻hold** (assume dt < lp) | **h(S)**. Clear def/pend. D IS LOST from pending. | TAP_WAITING | S held |
| **↻LP** | L holdable, no cancel (Sd=0,Ld=0, need NOT moved). **h(L)**, →LP. | →LP | S+L held |
| **Drag** | TAP: no drag (Sd=0). LP: L→L drag. | →DRG | rel(S+L), h(L) |

**Up (TAP — quick, before any timer):** Path4: D=1 → →DTW. def_tap=[S], pend=[D], pend_deferred=[D]. rel (nothing held). **→DTW**. On 2nd tap → D fires (D-only=hold D, then pos-dt-drag). On DT timeout → S fires.

**Up (TAP — after ↻hold, before LP):** S held. Path4: D=1 → →DTW. def_tap=[S], pend=[D] (re-saved from bindings). rel → S released. **→DTW + S fires**. Then timeout → S fires again ⚠️ double S.

**Up (LP):** S+L held. pending_deferred_double=0 (cleared by ↻hold). **rel**. S+L released.

**⚠️ REVERSED (lp < dt):**
| **↻LP** | **h(L)**, →LP. | LP | L held |
| **↻hold** | state=LP→SKIP. | LP | L only |
| **Up (LP):** rel → L. **S LOST**. |

**Up (DRG):** rel → L. Everything lost except L drag.

**Final:** 
- Quick tap → **D** on 2nd tap or **S** on DT timeout ✓
- Hold dt<lp → **S** (twice⚠️) + **L** ⚠️ D lost by ↻hold
- Hold lp<dt → **L only** ⚠️ S lost, D lost
- Drag → **L** drag

**Binding that fires:** Best case: **S then D** (quick tap → DT). But S double-fires. Worst case: **L only** (both S and D lost).

#### 57: `1 1 1 0 0 1` — S=1, L=1, D=1, Dd=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=0, has_dt=true. S→def, D→pend. hold_timer=dt. | TAP_WAITING | def=[S], pend=[D] |
| **↻hold** (dt < lp) | **h(S)**, clear def/pend. D lost. | TAP_WAITING | S held |
| **↻LP** | **h(L)**, →LP. | →LP | S+L held |

**Quick TAP Up:** Path4: D=1 → →DTW (re-saves S→def, D→pend+pend_deferred). →DTW.
- 2nd tap: D=1 + Dd=1 → D deferred to pending_deferred_double. Dd drag enabled. Up → x(D). **D fires on 2nd tap-up**.
- DT timeout: def_tap=[S] → **S fires**.

**Hold both→LP→no drag→Up:** S+L held, rel.

**Hold both→LP→Drag:** L→L drag (Ld=0, Dd irrelevant in LP state). rel(S+L), h(L).

**REVERSED (lp < dt):** LP fires first, S hold SKIP → S lost. L held. D in pending cleared by ↻hold... but wait, if lp<dt: LP fires (→LP), S hold never fires. D in pending is still there? Actually on_drag_start is called by check_start_drag which clears pending_double. But no drag yet. 

When ↻hold fires (state=LP): SKIP (line 225). So pending_double is **never cleared**. D survives in pending_double!

Then when does D fire? Only in Path4 (handle_tap_up). But if state=LP, finger-up goes to LONG_PRESSING case, which checks pending_deferred_double (not pending_double). And pending_deferred_double is 0 (never set in handle_ts_single_tap_hold, only set in Path4 or DT confirm).

So D in pending_double is **orphaned** — never fires! ⚠️

**Final (lp < dt):** **L only** (S lost, D orphaned in pending_double).

**Final (dt < lp, no drag):** **S** (via hold, rel on up) + **D** on 2nd tap or **S** on timeout.

**Final (dt < lp, drag):** **L** drag.

**Best path:** Quick tap → →DTW → 2nd tap: D deferred, Dd drag, D fires on up. **D and Dd both work.** ✓

#### 58: `1 1 1 0 1 0` — S=1, L=1, D=1, Ld=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | S→def, D→pend. hold_timer. | TAP_WAITING | def=[S], pend=[D] |
| **↻hold** (dt < lp) | h(S). Clear. | TAP_WAITING | S held |
| **↻LP** | Ld=1→no cancel. h(L), →LP. | →LP | S+L held |
| **Drag** | LP: Ld→Ld drag. or post-DT: D→D drag. | →DRG | rel, h(Ld) or h(D) |

**Quick Up:** →DTW. D on 2nd tap.
**Hold then LP Up:** rel (S+L). 
**Drag:** Ld or D drag.

**⚠️ lp < dt:** L fires, S hold SKIP. D orphaned.

**Drag race:** If in LP state before drag → Ld drag (Ld→L→null). D from post-DT drag not reachable because LP state transition precedes DT confirm.

**Final:** Quick → **D**. Hold → **L** (⚠️ S lost if lp<dt). Drag → **Ld**. ✓

#### 59: `1 1 1 0 1 1` — S=1, L=1, D=1, Ld=1, Dd=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | S→def, D→pend. hold_timer. | TAP_WAITING | def=[S], pend=[D] |
| **↻hold** (dt < lp) | h(S). Clear. | TAP_WAITING | S held |
| **↻LP** | Ld=1→no cancel. h(L), →LP. | →LP | S+L held |
| **Drag** | LP: Ld→Ld. post-DT: Dd→Dd→D→S. | →DRG | varying |

**Quick Up:** →DTW (D saved, Dd present). 2nd tap: D deferred, Dd drag, D executes on up. ✓ Best DT outcome.

**⚠️ lp < dt:** L only. S and D orphaned/lost.

**Drag (from LP):** Ld drag.
**Drag (from post-DT):** Dd drag.

**Both drag paths work but only ONE is reachable depending on state transition order.**

#### 60: `1 1 1 1 0 0` — S=1, L=1, D=1, Sd=1

| Phase | Action | State | Held/Deferred |
|---|---|---|---|
| **Down** | Sd=1→no pre-hold. | TAP_WAITING | — |
| **↻LP** | Sd=1→no cancel. h(L), →LP. | →LP | L held |
| **Drag** | TAP: Sd→Sd. LP: L→L. post-DT: D→D. | →DRG | varies |

**Quick Up:** D=1 → →DTW + D saved. **D on 2nd tap/DT timeout.**

**Hold Up:** L held → rel.

**Drag:** Sd (TAP_WAITING) or L (LP) or D (post-DT). All reachable depending on state.

**No timer races (Sd=1 prevents S deferral).** Clean. ✓

#### 61: `1 1 1 1 0 1` — S=1, L=1, D=1, Sd=1, Dd=1

| Phase | Same as 60 but Dd=1 adds post-DT Dd drag. |
|---|---|

**Quick Up:** →DTW. D saved, Dd=1 → on 2nd tap: D deferred, Dd drag, x(D) on up. ✓ Best DT.
**Drag:** Sd, L, or Dd (post-DT). All reachable.
**No races.** ✓

#### 62: `1 1 1 1 1 0` — S=1, L=1, D=1, Sd=1, Ld=1

| Phase | Sd=1, Ld=1 prevent cancel + pre-hold. |
|---|---|

**→LP:** L held. **Drag:** Sd, Ld, or post-DT D drag. Quick → **D on 2nd tap.**
**No races.** ✓

#### 63: `1 1 1 1 1 1` — ALL 1

| Phase | Sd=1→no pre-hold. Ld=1→no LP cancel. Full chain. |
|---|---|

**Quick Up:** →DTW. D deferred on confirm, Dd drag, x(D) on up.
**Hold:** L (held).
**Drag:** Sd (TAP), Ld (LP), or Dd (post-DT) with full fallback chains.
**No races.** ✓

---

## Summary of Bugs Found

| Bug | Combinations | Description |
|---|---|---|
| **⚠️ S lost when LP fires before hold timer** | 19, 26, 27, 49, 51, 56, 57, 58, 59 | When `lp_timeout < dt_timeout`, LP fires first (→LP), then the S hold timer checks `state==TAP_WAITING` → SKIP → S never fires |
| **⚠️ D orphaned in pending_double** | 56–59 | When LP fires before hold timer in D=1 case, D saved to `pending_double` by `handle_ts_single_tap_hold` is never cleared (↻hold SKIPs) and never fired (Path4 not reached). |
| **⚠️ S double-fires** | 24, 25, 56, 57 | ↻hold fires **S** (held+released on up). Then Path4 re-saves S→deferred_tap. DT timeout fires **S again**. |
| **Zombie DTW useless** | 1,3,5,7,9,17,19,21,23,25,27,29,31,33,35,37,39,49,51,53,55,57,59,61,63 | Dd-only (D=0, Dd=1): zombie DTW tracks location but has no D to confirm. Second tap within distance enables post-dt-drag (Dd) only. No action fires. |
| **L=0,Ld=1: L timer fires anyway** | 2,3,6,7,10,11,14,15 | LP timer fires with L=0: state→LP, nothing executed. This is harmless but wasteful — transitions to LP state for no reason, just to provide Ld drag. |
