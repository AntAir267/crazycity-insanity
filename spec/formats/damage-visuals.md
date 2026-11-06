# Damage visuals: how a damaged car looks

What MM2 draws differently on a damaged car: texture damage marks on the body, the effects driven
by the damage amount (engine smoke, wobbling wheels), what a wrecked car looks like, lights and
glass. The damage *amount*, impact counting and the parts thrown off are in
`../physics/damage.md`; particle effects (smoke, sparks, chips, shards) belong to the particle
spec, which this page hands numbers to. None of this changes the car's physics.

Formats used: `pkg.md`, `tex.md`, `car-model.md`. Single precision where formulas are given.

## Which cars

| Car | Texture damage | Smoke, wobbling wheels, chips, sparks |
|---|---|---|
| player, opponents, police, network cars (cars with a car simulation) | yes | yes |
| ambient traffic | no (traffic has no damage at all) | no |

All work the same way; there is no player-only visual. When the player's damage is switched off
(game option), no impact counts, so none of the impact-driven visuals happen either.

## Texture damage ("texel damage")

Each damaged body panel shows patches of a second, pre-painted "damaged" version of its texture
(dents, scratches, cracked glass). Every counted impact uncovers more patches near where the car
was hit. Patches stay until the damage is cleared.

### The data

A material slot is **damageable** if, in the car's paint set (`car-model.md`):

- its texture name ends in `_dmg` (case-insensitive; the text after the last `_`): its *damage
  image* is that texture, and its *clean image* is the texture with `_dmg` removed — or, if that
  does not exist, the `_dmg` texture itself; or
- otherwise, a texture named `<name>_dmg` exists: that is its damage image, and its own texture
  is the clean image.

Slots without a texture, or with neither, are not damageable and keep their texture as is.
Shipped: 379 `_dmg` textures; every one of the 20 menu cars has 4–12 damageable slots per paint
set (table in "Checked against"). The clean and damage images should be the same size; MM2 only
reports an error when they are not (see "Stamping" for what then happens).

`TextelDamageRadius` (metres) from `tune/vehicle/<car>.vehCarDamage`, default 0.4. Shipped 0.4 to
20 (vpbug 2.7, vpcop 3.0; vp4x4 and vppanozgt 20, which reaches every triangle of the car).

### Set-up when the car loads

1. Take the car's `BODY` mesh at level H (after the fill rule, `pkg.md`) and its current paint set.
2. For each batch of that mesh, in order, whose slot is damageable: make a **working copy** of
   the slot's clean image — the top mip level only, no mip chain. Each slot gets its own copy, even
   when two slots use the same clean texture (cars use this to give the left and right sides
   separate damage on shared artwork: `vpbugyellow_sd` and `vpbugyellow_sd_dmg` both copy
   `vpbugyellow_sd`).
3. If no copy was made, the car has no texture damage (MM2 warns "No damage textures found").
4. Otherwise record, for the whole mesh: every vertex position (model space) and texture
   coordinate, and every triangle as its three vertices plus its slot, in batch order and index
   order within each batch.
5. Fill every working copy with its clean image.

### Drawing

At levels **H and M** the whole car (body, wheels, hubs, fenders, breakable parts, decal) is
drawn with the working copies in place of the damageable slots' textures. At **L and VL** the
car is drawn with its normal, undamaged textures.

### When marks are made

- At a **counted** impact (`../physics/damage.md`, "Per impact report", step 5), if no mark
  request is pending, the car records a request with the impact slot's car-space contact point
  (the point defined in `../physics/damage.md`, "One body part per strong impact"). Further counted
  impacts while a request is pending are ignored for marking.
- The request is carried out at the end of the car's next damage update (the per-sample step of
  `../physics/damage.md`, after the slot timers and the wreck check), i.e. at the start of the next
  physics sample; then it is cleared (also when the car has no texture damage). So a car gets at
  most one round of marks per physics sample.
- A clear damage (below, "Repair") does **not** drop a pending request: the round is carried out
  at the next damage update on the refilled copies, at the stored car-space point. Only setting
  the car up starts it with no request.

### Marking (one round, at car-space point *p*, radius *r* = `TextelDamageRadius`)

For each recorded triangle, in order, whose slot is damageable:

1. If one of its three vertices *q* has `(dx·dx + dy·dy) + dz·dz < r·r` (d = p − q), the triangle
   is marked; otherwise go to the next one.
2. Pick a point inside it: draw three random numbers a, b, c in [0, 1) from the **effects
   generator** (MM2's second generator state, `../physics/collision.md`, "Random numbers"), in
   that order. n = 1 / ((c + b) + a); wa = n·a, wb = n·b, wc = n·c. With the triangle's vertex
   texture coordinates (u0, v0), (u1, v1), (u2, v2) in its stored order:
   - u = ((wc·u2 + wb·u1) + wa·u0)
   - v = ((wa·v0 + wb·v1) + wc·v2)
3. Texel: x = trunc(W·u), y = trunc(H·v), with W × H the working copy's size (no wrapping; row 0
   is v = 0 as in `tex.md`).
4. Draw one more raw value from the same generator (the 15-bit value before scaling); its lowest
   bit picks the brush. Copy the brush's texels from the slot's damage image into its working
   copy, at the same coordinates (the images are aligned texel for texel):

   | Row | Large brush (bit 0) | Small brush (bit 1) |
   |---|---|---|
   | y − 3 | x − 4 … x + 5 | x − 2 … x + 2 |
   | y − 2 | x − 8 … x + 7 | x − 4 … x + 3 |
   | y − 1 | x − 8 … x + 9 | x − 4 … x + 4 |
   | y | x − 10 … x + 7 | x − 5 … x + 3 |
   | y + 1 | x − 8 … x + 11 | x − 4 … x + 5 |
   | y + 2 | x − 6 … x + 7 | x − 3 … x + 3 |
   | y + 3 | x − 4 … x + 5 | x − 2 … x + 2 |

   (inclusive column ranges). Rows outside 0 … H − 1 are skipped; columns are clipped to
   0 … W − 1.

Because every mark copies texels rather than blending, overlapping marks give the same result in
any order; only the random draws depend on the order. A round costs four draws per marked
triangle. There is no limit on marks: with a large radius one impact marks every triangle in
reach (vpbug's front corner: about 160 of its 346 body triangles), so a few hard hits cover most
of the car. Marks do not depend on how hard the impact was, only on the impact being counted.

**Mismatched sizes** (shipped: `vpf350black_sd`, `vpf350red_sd`, `vpmustangopal_sd`,
`vplafranceyel_bk`, `vplafrancerwd_BK` and their `_dmg` partners differ, e.g. 256 × 128 against
128 × 128): MM2 still copies with the working copy's coordinates; how much of a row then copies is
up to its graphics driver. Copy only the texels that lie inside both images.

### Repair

Texture damage never fades: `RegenerateRate` lowers the damage amount but leaves the marks. Every
"clear damage" (car reset, the cruise recovery after a wreck, opponents' recovery, network
resets; `../physics/damage.md`) refills all working copies from their clean images.

## What the damage amount drives

With `strength` = (`damage` − `MedDamage`)/(`MaxDamage` − `MedDamage`) clamped to [0, 1]
(`../physics/damage.md`, per-sample step), each damage update:

**Engine smoke level** = ceil(4·strength), 0 to 4. At 0 there is no smoke; otherwise the smoke
particles (particle spec: rule from the same `.vehCarDamage` file, texture `fxpt8`, a 2 × 2 atlas)
use one frame for the whole emission, by level:

| Level | strength | Frame | Look |
|---|---|---|---|
| 1 | (0, 0.25] | 1 (top right) | white |
| 2 | (0.25, 0.5] | 0 (top left) | light grey |
| 3 | (0.5, 0.75] | 3 (bottom right) | grey |
| 4 | (0.75, 1] | 2 (bottom left) | black |

Emission point, car space: `SmokeOffset`; if `SmokeOffset2` is not (0, 0, 0): with `DoublePivot`
0, a point on the segment from `SmokeOffset` (o1) to `SmokeOffset2` (o2) at a fraction f drawn
from the effects generator, q.c = ((o2.c − o1.c)·f) + o1.c per component; with `DoublePivot` 1,
`SmokeOffset` and `SmokeOffset2` alternately, starting with `SmokeOffset` when the car is set up
(a clear damage does not restart it). The point is chosen only in damage updates that emit smoke
(level 1–4 and the car has its smoke texture, `../engine/effects.md`, "Engine smoke"): only then
is f drawn or the alternation advanced. The emission itself is the particle spec's.

**Wobbling wheels** (always on): g = clamp((|ω|·F·2)·0.31830987, 0, 1) with ω the front-left
wheel's spin (rad/s, `vehicle-wheels.md`) and F the sample length; k = (1 − g)·strength. The drawn
wheels get a tilt angle (radians): front-left −0.15·k, front-right 0.35·k, rear-left 0.35·k,
rear-right −0.15·k. When the wheel's drawn matrix is built (spin about its own X axis), it is then
turned by this angle about its own (already spun) Z axis, so the tilt turns with the wheel and the
wheel visibly wobbles. Below `MedDamage` there is no wobble, and it fades out as the wheels spin
faster (gone at a quarter turn per sample). Drawing only. The hubs and the front fenders wobble
too: a fender's frame takes its X axis from the drawn wheel's matrix after this turn
(`car-model.md`, "Fenders"), so a wobbling front wheel tilts its fender.

**Wreck throw**: the running gear thrown at `damage` ≥ `MaxDamage` (`../physics/damage.md`).

**Dash needle**: `dash.md`.

## Per-impact effects (hand-off to the particle spec)

At every counted impact, besides the mark request and the body part throw of
`../physics/damage.md`:

The slot fields used here are the ones stored when the slot was **opened** (world contact point
and normal), plus its current `total` (`../physics/damage.md`, "Per impact report"). The normal is
the impact record's as filed (`../physics/collision.md`, "The impact record": out of side B into
side A), never turned toward the car. Against the city the car is side B, so it points from the
car into the wall or ground and the sparks fly into it; against objects and props the car is
side A and it points away from them; the two cars of a car pair store the same vector.

- **Sparks** (`spark` texture, a pool of 64 per car), when the car's `speedMPH` > 15:
  trunc(16 · `total` · dt) sparks (dt = the sample length) from the world contact point along the
  contact normal.
- **Body chips** ("shards"): when `total` > 500 and the car's speed > 5 m/s,
  min(2, trunc(`total`/300)) chips from a ring of 16 per car. A chip is a small triangle (legs 0.1 m)
  textured with a random 0.3 × 0.3 window of one of the car's own textures (as named in its paint
  set, never a working copy: a `_dmg`-named slot's chip shows the damage image, and chips never
  show marks), launched from the world contact point with a velocity built from the car's axes
  and its speed (factors 0.3, 0.3, 0.2 and random spreads), spinning about a random axis,
  falling at 20 m/s², drawn without culling, gone after 1.8 s. Its draws use the effects
  generator.

## What a wrecked car looks like

- No darkening, no fire and no special texture: the car keeps the marks it has.
- Black engine smoke (level 4 is reached before the wreck, once strength > 0.75).
- Wheels wobbling at their largest tilt (k = 1 − g).
- Running gear thrown off by the wreck throw if the car was fast enough (`../physics/damage.md`).
- The lights still work: see below.

## Lights

Lights never break in MM2. The game contains a routine that would switch off the front-left,
front-right, rear-left or rear-right lamp group by the quadrant of an impact point, but nothing
ever calls it; all four groups are on after every reset. The tail and brake lights' "both rear
groups intact" condition (`night.md`) is therefore always true.

## Glass panes

Props whose prop data sets `BillFlags` 0x100 (`street-props.md`) shatter instead of falling over.
**No shipped prop sets it**, so a viewer of the shipped data never sees one. What MM2 does with one:
when hit, the pane's own mesh is hidden and a burst of glass shards (texture `s_win_trans_01`,
using the prop data's particle rule) is released from the pane and falls to the ground found by a
vertical probe 100 m down from a point next to the pane on the side it was hit from; at most two
panes shatter at a time (a third reuses the oldest burst). The shard particles and their game-wide
random draws are the particle spec's. A reset restores the pane.

## Exhaust (note)

Cars with `<car>_EXHAUST0.mtx` / `_EXHAUST1.mtx` (shipped: only vpcentury) also puff smoke from
those points with intensity clamp((rpm − 2000)/(MaxRPM − 2000), 0, 1), using the same smoke rule
and frame as the engine smoke. Particle spec.

## Checked against

- Static reading of the game binary: the slot opening (normal copied from the impact record
  unchanged), the clear-damage and set-up paths (only set-up drops a pending mark request), the
  smoke point's arithmetic order and its place inside the smoking branch, the brush choice, and
  the fender frame built from the wheel matrix after the wobble turn.
- `tools/formats/damage.py survey` (dirty side), damageable slots per paint set / triangles marked
  possible / body triangles, and `TextelDamageRadius`:

  | Car | Slots | Tris | Radius |
  |---|---|---|---|
  | vp4x4 | 5 | 194 / 326 | 20 |
  | vpauditt | 7 (6 in set 4) | 450 / 600 | 2.0 |
  | vpbug | 6 | 284 / 346 | 2.7 |
  | vpbullet | 10 | 214 / 298 | 3.0 |
  | vpbus | 12 | 252 / 320 | 3.3 |
  | vpcab | 8 | 266 / 309 | 4.3 |
  | vpcaddie | 8 | 184 / 256 | 0.4 |
  | vpcentury | 6 | 266 / 348 | 0.4 |
  | vpcoop | 8 | 236 / 300 | 2.7 |
  | vpcoop2k | 8 | 285 / 344 | 2.9 |
  | vpcop | 6 | 188 / 296 | 3.0 |
  | vpdb7 | 8 | 191 / 246 | 3.4 |
  | vpddbus | 5 | 185 / 351 | 10 |
  | vpdune | 8 | 284 / 346 | 3.0 |
  | vpford | 8 | 240 / 356 | 6.3 |
  | vpmustang99 | 5 | 178 / 260 | 3.5 |
  | vppanoz | 10 | 312 / 370 | 4.0 |
  | vppanozgt | 6 (3 in sets 1 and 3) | 318 / 442 | 20 |
  | vpsemi | 5 | 193 / 299 | 6.4 |
  | vpvwcup | 4 | 278 / 340 | 3.7 |

  (vppanozgt's purple and yellow sets name six `_dmg` textures that do not exist; those slots are
  not damageable.)
- `tools/formats/damage.py render vpbug`: two hits at the front-left corner and one at the right
  rear, effects generator from 0: 159, 159 and 211 marked triangles;
  `work/check/contact_damage.png` (clean vs. damaged, left and rear views) and
  `contact_damage_tex.png` (working copies of `vpbugyellow_sd` and its `_dmg` slot) show scuffed
  paint and cracked glass spread over the hit areas.
- `fxpt8.tex`: 128 × 128, 2 × 2 frames: white-grey, white, black, grey
  (`work/check/fxpt8_view.png`).
