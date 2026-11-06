# Effects: wheel dust and smoke, engine smoke, sparks, chips, prop debris, rain, skid marks

Everything MM2 emits around a car and in the city, apart from the damage marks on the car itself
(`../formats/damage-visuals.md`). The particle system these effects share (rule format, birth,
update, drawing) is in `../formats/particles.md`; this page says who owns emitters, which rule and
texture each uses, what triggers a burst and from where. Sparks, body chips and skid marks have
their own small systems, specified here.

All effects are visual: none changes physics, and none draws from the game-wide random generator
(the exceptions MM2 has, glass panes and the wreck throw, are named where they come up).
Single precision as in `../formats/particles.md`.

## Overview

| Effect | Owner | System, pool | Texture (sheet) | Rule | Updated |
|---|---|---|---|---|---|
| wheel dust, grass, smoke, splash | each car | emitter, 128 | `ptx_wheel` (8 × 8) | 8 shared rules from `tune/effects/` | per physics sample |
| engine smoke, exhaust puffs | each car | emitter, 64 | `fxpt8` (2 × 2) | one shared rule, keys from `.vehCarDamage` | per physics sample |
| sparks | each car | line sparks, 64 | colour table `spark` | built in | stepped at most 30 times a second |
| body chips | each car | ring of 16 | the car's own textures | built in | per physics sample |
| prop debris | each active prop | emitter, 64 | `fxpt<N>` (2 × 2) | the prop's `BirthRule` | per frame |
| rain | the city | emitter, 200 | `ptx_rain` (4 × 4) | `tune/rain.asBirthRule` | per frame |
| skid marks | each car, per wheel | strip ring, 64 entries | `tire_track` | — | per physics sample |

"Each car" means every car with a car simulation: the player, opponents, police and network
cars. Ambient traffic has none of these effects. "Per physics sample" means the call happens once
per physics step of `../physics/vehicle.md` with dt = that step's length; "per frame" uses the
frame time F.

Textures are looked up as any texture (`../formats/tex.md`), with the night and rain rules of
`../formats/night.md`. If an effect's texture is missing, that effect is off for the session
(sparks fall back to a built-in table instead).

## Wheel particles

**Rules.** Eight rules, loaded once and shared by all cars, from `tune/effects/<name>.asBirthRule`.
The index used in `city/materials.mtl` (`ptxindex`, `../physics/collision.md`, "Materials") picks
one:

| Index | Rule | Shipped frames | Look |
|---|---|---|---|
| 0 | `dirt` | 0–1 | clods |
| 1 | `dust` | 2–5 | dust puffs |
| 2 | `grass` | 6–9 | grass blades |
| 3 | `leaf` | 10–13 | leaves |
| 4 | `smoke` | 14 | tyre smoke |
| 5 | `snow` | 23–24 | (used by `sand`) |
| 6 | `splash` | 16–21 | water spray |
| 7 | `rock` | 14 | grit (used by roads) |

In **rain** (weather 3, `../formats/night.md`), when a game is set up (once, after its cars), every
value of the `splash` rule is copied over the `smoke` rule, so index 4 sprays water instead of
smoking for the rest of that game. The eight rules are read again from their files when the next
game's cars are set up (they are freed with the cars), so a dry game after a rainy one smokes
normally. MM2's weather never changes during a game. (The other files in `tune/effects/`,
`default` and `Engine Smoke Rule`, and `tune/snow.asBirthRule`, are never read.)

Shipped materials: `cobblestone` (all roads and pavements) 4, 7 with thresholds 0.25, 0.3;
`_default` 4, −1 (0.25, 0.5); `grass` 1, 2 (0.25, 0.5); `sand` 1, 5 (0.25, 0.5); `water` −1, 6
(0, 0); `deepwater`, `dirt`, `wood` and the built-in default −1, −1 (none).

**Emitter.** One per car: pool 128, texture `ptx_wheel` cut into 8 × 8 frames, no spew rule.
Two fractional counters `carry0`, `carry1` (one per material pair slot, shared by all four
wheels), both 0 at a car reset.

**Each physics sample**, after the car's own update (`../physics/vehicle.md` steps 1–11), the
car's wheels in the order front-left, front-right, rear-left, rear-right; for each wheel, using
the material of its current contact, first pair slot 0 (`ptxindex[0]`, `ptxthreshold[0]`), then
slot 1; a slot whose index is outside 0 … 7 is skipped. For a slot with rule R (index *i*) and
threshold T, with the wheel's values from `../physics/vehicle-wheels.md` (`slide`, `Fsusp`, `L0`,
`k`, `SuspensionLimit`, `width`, `radius`, spin `ω`, `vLat`, `sV`, contact point `x`, axes `Lat`,
`N`, `Back`):

1. Nothing unless `slide` > T. (A wheel in the air has `slide` = 0.)
2. Load factor: if `Fsusp` < `L0`, f = ((`Fsusp`/`L0`) + 1)·0.25; otherwise
   f = ((((`Fsusp` − `L0`)/(`k`·`SuspensionLimit`)) + 1)·0.5, and 1 if that is more than 1.
3. Particle radius ρ = ((f·`width`)·R.Radius)·0.5.
4. Birth point: σ = sign(−ω) (1, −1, or 0 when ω = 0); c = σ·`radius`;
   P.c = (c·Back.c) + x.c for each component, then P.y = P.y + ρ. (The point sits on the ground
   at the front or back edge of the tyre, raised by the particle radius.)
5. Birth velocity: a = vLat·R.Velocity.x, b = (|ω|·`radius`)·R.Velocity.y,
   e = (−sV)·R.Velocity.z; V.c = ((e·Back.c + b·N.c) + a·Lat.c). So the rule's x, y, z velocity
   factors scale the sideways slide speed, the tread speed (upwards along the ground normal) and
   the longitudinal slip speed (forwards).
6. For this burst the rule's Position is P, its Velocity V and its Radius ρ (the spreads stay as
   in the file).
7. q = ((R.InitialBlast·dt)·f) + carry(slot); n = trunc(q); carry(slot) = q − n. If n ≥ 1,
   Blast(n, R). So `InitialBlast` is a rate here (particles per second at full load).
8. The rule's Velocity and Radius are put back (its Position keeps P).

After the four wheels the emitter is updated with dt. The emitter's brightness stays 1 (MM2 asks
the city for a lighting value each frame, and the city always answers 1).

**Draw order of the random numbers**: wheel by wheel, slot 0 then slot 1, each burst's births in
order.

## Engine smoke and exhaust

**When and from where**: the smoke level (0–4), its frame of `fxpt8` and the car-space emission
point (`SmokeOffset`, the point between it and `SmokeOffset2`, or the two alternately) are in
`../formats/damage-visuals.md`, "What the damage amount drives". That page's point choice draws
one effects-generator number when it picks a point between the two offsets.

**The rule.** One rule object is shared by **all** cars. Each car, when it is set up, first sets
these values on it: Velocity (0, 1, 0), VelocityVar (1, 2, 1), Life 0.8, LifeVar 0.4, Mass 0.2,
Drag 1, Radius 0.3, RadiusVar 0.1, DAlpha −15, DRadius 0.03, Gravity 3 (all other keys keep
whatever the rule held), and then reads its own `tune/vehicle/<car>.vehCarDamage`, whose rule keys
(`Position` … `Color`, `../formats/particles.md`) overwrite them. So **the car set up last decides
the smoke of every car** (only the frame and the birth point and direction are per car, below).
Every shipped file lists all the keys, so in practice the last file wins completely.
`tune/effects/Engine Smoke Rule.asBirthRule` is not read (MM2 only uses the name).

**Emitter.** One per car: pool 64, texture `fxpt8` (2 × 2 frames), no spew rule, and a shared
counter `acc` (0 at reset and at every clear damage). If `fxpt8` is missing the car has no smoke
and no exhaust, and step 1 below is skipped whole (no frame change, no point draw).

**Puff(point *q*, rate *w*)** (car space):

1. `acc` = w + `acc`; n = trunc(`acc`). If n = 0, stop.
2. For this burst the rule's Velocity becomes the rule Velocity turned into world axes by the car
   body's rotation (rows X, Y, Z of the body matrix, `../physics/vehicle-body.md`):
   V.c = ((v.z·Z.c + v.y·Y.c) + v.x·X.c); its Position becomes *q* placed with the **body**
   matrix: q.x·X + q.y·Y + q.z·Z + p_body, where p_body is the body position (the centre of mass,
   not the model origin of `../physics/vehicle.md` step 5: MM2 measures the smoke offsets from
   the centre of mass).
3. `acc` = `acc` − n; Blast(n, rule); the rule's Velocity is put back.

The spreads (VelocityVar, PositionVar) stay along the world axes.

**Each physics sample**, in the car's damage step (`../physics/damage.md`):

1. If the smoke level is 1–4 and the car has `fxpt8`: set the shared rule's TexFrameStart and
   TexFrameEnd to the level's frame (`../formats/damage-visuals.md`), pick the point *q* (the only
   time its draw or alternation happens), and Puff(*q*, 1.0). So a smoking
   car makes one particle per physics sample, however strong the smoke (the level changes only
   the frame).
2. Update the emitter with dt.
3. **Exhaust**: if the car's model has `<car>_exhaust0.mtx` and/or `<car>_exhaust1.mtx`
   (`../formats/car-model.md`; shipped: only `vpcentury`), with
   w = clamp((rpm − 2000)/(`MaxRPM` − 2000), 0, 1) (rpm and `MaxRPM` of
   `../physics/vehicle-powertrain.md`): Puff(exhaust0 origin, w), then Puff(exhaust1 origin, w).
   The puffs use whatever frame the shared rule holds at that moment (the last smoking car's
   level, or frame 0 after a clear damage), and they share `acc` with the damage smoke.

A wrecked car has no extra effect: it smokes at level 4 (black) as soon as strength passes 0.75.
There is no fire.

A clear damage (reset, recovery) empties the emitter, sets `acc` = 0 and sets the shared rule's
frames to 0, 0.

The rule has BirthFlags 0, so each smoke birth makes one raw frame draw (on a range of one frame):
15 draws per particle.

## Sparks

Short glowing line segments, not textured squares. One spark system per car, capacity 64.

**Colour table.** Read once per program from `texture/spark.tga`, shared by all cars: accepted
only if it is an uncompressed true-colour TGA (image type 2, colour-map type 0, no ID field) with
width W and height H both powers of two and W·H ≤ 256. The table has H rows of W colours, read in
file order (rows as stored, no flip). A 24-bit pixel gives colour (A 128, R, G, B); a 32-bit pixel
its own alpha. With `shift` = 8 − log2 W. If the file is missing or refused, a built-in table of 4
rows of 8 colours (`shift` 5) is used:

| Row | Colours (R, G, B), all alpha 255 |
|---|---|
| 0 | (51, 0, 0) (76, 0, 0) (102, 0, 0) (127, 0, 0) (153, 0, 0) (178, 0, 0) (204, 0, 0) (229, 0, 0) |
| 1 | (51, 0, 25) (76, 0, 25) (102, 0, 25) (127, 25, 51) (153, 51, 76) (178, 76, 127) (204, 102, 178) (229, 153, 204) |
| 2 | (51, 0, 25) (76, 0, 25) (102, 0, 25) (127, 51, 51) (153, 76, 102) (178, 102, 127) (204, 127, 153) (229, 153, 178) |
| 3 | (76, 0, 0) (102, 0, 0) (127, 0, 0) (153, 0, 0) (178, 0, 0) (204, 76, 76) (229, 153, 153) (255, 255, 255) |

The shipped `spark.tga` is 8 × 8, 24-bit (pale yellow to orange; alpha 128 throughout).

**State per spark**: head position **p**, tail position **t**, velocity **v**, colour row offset
`c` (byte), `life` (byte), current colour. Per system: `count`, a step timer `acc` (0 at start).

**Burst** (from a counted impact, below), n sparks at world point P with unit normal N:

1. Helper axis: if |N.y| ≥ 0.95 use (1, 0, 0), otherwise (0, 1, 0); call it H.
   T1 = N × H, T2 = T1 × N (the standard cross product of `../physics/vehicle-wheels.md`; T1 and
   T2 are not normalised).
2. For each of the n sparks while `count` < 64 (the rest are dropped), with r = effects-generator
   numbers:
   1. r1, r2, r3: w = ((r1 − 0.5)·0.1, r3·0.1, (r2 − 0.5)·0.1).
   2. **p** = **t** = w + P (per component).
   3. w is normalised: L2 = (w.z·w.z + w.y·w.y) + w.x·w.x; m = 0 if L2 = 0, else 1/sqrt(L2);
      w = w·m.
   4. r4: s = ((7 − 6)·r4) + 6; w.x = s·w.x. r5: s2 = ((5 − 4)·r5) + 4; w.y = s2·w.y;
      w.z = s·w.z.
   5. **v**.c = ((N.c·w.y + T2.c·w.z) + T1.c·w.x) for each component: the spark flies out on the
      normal's side at 6–7 m/s along the surface and 4–5 m/s along the normal (before the
      unnormalised T1, T2 scale it).
   6. `c` = raw & ((H − 1)·W) (one raw draw: a random row, times W). `life` = (raw & 63) − 64 as a
      byte, i.e. 192 … 255 (a second raw draw).
   7. `count` += 1.

   Seven draws per spark: r1, r2, r3, r4, r5, raw, raw.

**Update.** Each physics sample (in the car's damage step, after the smoke): `acc` = `acc` + dt;
if `acc` ≥ 0.033333335, run one step with T = `acc`, then `acc` = 0. A step:

1. gT = T·(−20); k = T·(−0.036); dl = trunc(T·(−650)).
2. For each spark i while i < `count`:
   - l = `life` + dl (as an integer). If l < 0 the spark dies: the last spark is moved into slot
     i and i is not advanced.
   - Otherwise `life` = l; colour = table[(l >> `shift`) + `c`] (so the column moves from the
     bright end toward column 0 as the spark ages); **t**.c = (k·v.c) + p.c with the old **p**
     (0.036·T·|v| behind where the head was); v.y = gT + v.y; **p**.c = (T·v.c) + p.c with the
     new **v**, so the head moves on by T·v and the drawn line is about 1.036·T·|v| long (up to
     about 25 cm for a new spark at 30 steps a second: sparks show as short streaks); if p.y < 0
     and v.y < 0, v.y = v.y·(−0.8). The bounce plane is world height 0: MM2's spark floor is never
     set to the ground, so sparks fall through raised ground and bounce only at y = 0.

A spark lives about 9–12 steps (0.3–0.4 s at 30 steps a second).

**Drawing**: each live spark as one line from **p** to **t**, both ends in its colour, untextured,
in the effects pass (alpha blending on, so the shipped table's alpha 128 makes sparks
half-transparent).

**Trigger** (`../formats/damage-visuals.md`, "Per-impact effects"): at every counted impact
(`../physics/damage.md`, step 5) while the car's `speedMPH` > 15: n = trunc((16·`total`)·dt), with
`total` the slot's current total and dt the physics sample length, from the slot's world contact
point along its contact normal, both stored when the slot was opened. The normal is the impact
record's, not turned toward the car, so against the city the sparks fly into the wall or ground
(`../formats/damage-visuals.md`, "Per-impact effects").

## Body chips

Small flying triangles cut from the car's own textures ("shards" in MM2). One ring of 16 per car.

**Set-up.** The car's material list for its paint set (`../formats/car-model.md`): S = its number
of materials and their textures in order. Each of the 16 chips starts with age = the largest
float (so it is not drawn), velocity 0, spin axis (0, 1, 0), spin 0, an identity orientation and
texture window corner (0.5, 0.5). A ring index `next` starts at 0.

**Trigger** (`../formats/damage-visuals.md`): at every counted impact (as for sparks; there is no
speed-in-mph condition) if the slot's `total` > 500 and the car's `speed` (m/s,
`../physics/vehicle.md` step 2) > 5: n = trunc(`total`/300), at most 2; for n ≥ 1, n chips from
the slot's world contact point P.

**One chip** (all draws from the effects generator, nine per chip, in this order), with v = the
car's speed and X, Y, Z the rows of the car body's rotation:

1. r1: a = (((r1 + r1) − 1)·0.3)·v. r2: b = (((r2 + 1)·0.5)·0.3)·v.
   r3: e = ((r3·0.9 + 0.1)·0.2)·v.
2. r4, r5, r6: w = (r4 − 0.5, r5, r6 − 0.5); m = 0 if L2 = 0, else 1/sqrt(L2) with
   L2 = (w.x·w.x + w.y·w.y) + w.z·w.z; the spin axis is (m·w.z, m·w.y, m·w.x) (components in that
   reversed order).
3. r7: spin = ((10.8·5 − 10.8)·r7) + 10.8 (rad/s, 10.8 to 54).
4. `next` = (`next` + 1) mod 16; the chip in slot `next` is (re)started: velocity
   V.c = ((a·X.c + b·Y.c) + e·Z.c), the spin axis and spin above, age 0, position P (its
   orientation is **not** reset: it keeps the turned orientation of that slot's last chip, the
   identity at first), and r8, r9: texture window corner (u0, v0) = (r8, r9).

So a chip flies mostly along the car's own up and forward axes in proportion to the car's speed
(sideways ±0.3·v, up 0.15–0.3·v, along the car's Z 0.02–0.2·v).

**Update**, each physics sample in the car's damage step (after the sparks), for each chip with
age < 1.8: V.y = V.y − dt·20; turn its orientation rows by the angle dt·spin about the spin axis
(the rows multiplied by the rotation, as `Rot` of `../physics/vehicle-body.md`); P.c = (dt·V.c) +
P.c with the new V; age = age + dt.

**Drawing**, in the effects pass, every chip with age < 1.8 (no other test): one triangle with
world matrix = the chip's orientation and position, vertices (model space, with texture
coordinates) (0, 0, 0.1) at (u0, v0 + 0.3), (0, 0, 0) at (u0, v0), (0.1, 0, 0) at (u0 + 0.3, v0),
colour opaque white, back faces not culled. Its texture: chip *i* (ring slot order 0 … 15) uses
material *j* of the car (its texture as named in the paint set, never the texture-damage working
copy, `../formats/damage-visuals.md`), where *j* starts at 0 for slot 0 and goes up by one per
slot, returning to 0 when it reaches 16 div S (integer division); if 16 div S is 0 it never
returns, so slot *i* uses
material *i*. (So with 4 materials the chips use materials 0–3, with 6 only 0 and 1, with 9 to
16 only material 0, and with 17 or more slot *i* uses material *i*.) A chip never collides and never stops; after 1.8 s it is simply no longer
drawn.

## Props

A prop (banger) whose data has `TexNumber` *N* > 0 puffs particles from its data's rule block
(`../physics/collision-objects.md`, prop data; read by position, `../formats/particles.md`) while
it has a **body** (`../physics/loose-bodies.md`). The emitter belongs to the body, one per body
of the pool of 32 (not to the object): pool 64, 2 × 2 frames, texture `fxpt<N>`.

**Textures.** MM2 loads `fxpt1` … `fxpt20` once per session. *N* = 1 … 20 uses `fxpt<N>`; if that
texture does not exist (shipped: `fxpt15`, `fxpt17`–`fxpt20`), the prop emits nothing. (*N* > 20
reads past MM2's table; treat it as no texture.) `fxpt5_fall` and `fxpt5_win` are never used.

**On activation**, every time an object gets a body (`../physics/loose-bodies.md` 2.1: an untouched
prop at the start of a test that gives impacts, whether or not it then breaks; each piece of a
broken prop; a moved prop lying still that is hit again; a thrown car part), with R = that
object's prop data rule:

1. Nothing (beyond an emitter reset) if *N* = 0 or the texture is missing.
2. Reset the emitter and give it the texture.
3. If R.BirthFlags has 0x01: only a prop that was untouched before this hit emits; R.Position is
   set (permanently, in the shared prop data) to the prop's current world position, and births
   do not follow the prop.
   Otherwise births **follow** the prop: R.Position is in the prop's own frame and every birth is
   placed with the prop's current matrix (`../formats/particles.md`, "Burst").
4. The emitter's spew rule is R; Blast(R.InitialBlast).

**Each frame**, in the after-samples step of `../physics/loose-bodies.md` 5.2, for each loose
mover with switch U whose body that step does not let go (rest detector awake, height at least
−100): update the emitter with the frame time F (its rule spews `SpewRate` per second, until
`SpewTimeLimit` if not 0, counted from the activation), and register it for the effects pass if
it has live particles. There is no fixed lifetime: the particles show exactly as long as the body
is such a mover, and they go (no longer drawn) as soon as the body is let go or dropped (at rest,
below −100, taken for a newer object, or removed outside the player's rooms; `loose-bodies.md` 1,
5.1, 5.2). So a touch that does not break the prop makes a burst that is never shown (the body is
let go at the end of that test) but still takes its effects-generator draws; a prop that breaks
into pieces shows only its pieces' particles, each with its own data's `TexNumber` and rule (the
prop's own body is let go once the pieces exist); a prop that moves as one piece keeps its body
and its burst.

Shipped: 38 prop data files have *N* > 0 (textures 1–7 and 16), all with BirthFlags 0. For
example `sp_hotdogcart_f` (*N* = 3): 16 particles at the hit, then 5 a second, frames 0–3;
`sp_mailbox_f` (*N* = 7): 10, then 2 a second, frames 1–2; `sp_tree1_s_BREAK05` (*N* = 16): one.
`sp_tree1_s_BREAK06` (*N* = 5) names its block `asBirthRule`; MM2 does not check the name and
reads it like the others (25, then 1.24 a second), but no shipped prop loads that file
(`sp_tree1_s` has `NumParts` 5, so only its pieces 1–5 are used). `sp_tree2_s`,
`sp_tree2_s_BREAK01`, `sp_tree3_s` (*N* = 4) and `sp_tree2_s_BREAK02` (*N* = 5), like
`sp_tree1_s_BREAK06`, ask for frames 0–4 on their 2 × 2 sheets: about one particle in five gets
frame 4, which lies outside the sheet and is not drawn (`../formats/particles.md`, "Frames outside
the sheet").
Prop rules have no `Damp`, `LifeVar`, `Height`, `Intensity` or `Color` lines (the prop data reader
would not read them anyway), so those keep the defaults (opaque white, ground plane y = 0, unused
without flags 2 and 8).

## Rain

Only in weather 3 (`../formats/night.md`, section 6): one emitter for the city, pool 200, texture
`ptx_rain` cut into 4 × 4 frames, spew rule `tune/rain.asBirthRule` (shipped: 200 drops a second
forever, life 1 s, falling at about 35 m/s, spread 25 m around the emitter point in x and z,
random frames 0–15, BirthFlags 8 with Height 0).

- Updated once per frame with F, before the city is drawn, whatever the camera sees.
- Where it is drawn and the emitter point (written into the rule's Position each frame the rain is
  drawn, 10 m above and 10 m in front of the camera) are in `../formats/night.md`, section 6. The
  spew uses the point set by the previous frame's drawing.
- With BirthFlags 8 and Height 0 a drop stops and dies at world height 0, not at the street, so
  drops pass through streets above y = 0 and end at their lifetime or below y = −50 instead.

## Effects MM2 does not have

- **Water splash**: driving into deep water makes no particles of its own (the water effect of
  `../physics/collision.md` is buoyancy only). The spray seen on the `water` material is the wheel
  rule 6 above; MM2 plays no splash sound (`../audio/tyre-impact-sounds.md`, "Scraping, splashes and what makes no sound").
- **Fire, explosions**: none, also not for a wreck.
- **Glass and light breaking on cars**: no particles. Lights never break
  (`../formats/damage-visuals.md`, "Lights").
- **Glass panes** (`BillFlags` 0x100): a separate shard burst drawn with `s_win_trans_01` that uses
  the game-wide generator; no shipped prop uses it (`../formats/damage-visuals.md`, "Glass panes").
  Not specified further.
- **Snow**: `tune/snow.asBirthRule` ships, but there is no snow weather.
- Traffic cars (no car simulation) make no wheel particles, smoke, sparks, chips or skid marks.

## Skid marks

Each car has four mark rings, one per wheel (front-left, front-right, rear-left, rear-right).

**Set-up per wheel**: width w = the wheel's `width` (`../physics/vehicle-wheels.md`, from the
`.mtx` set-up), hw = w·0.5, iw = 1/w; a ring of 64 entries, each (left point L, right point R,
texture, v); `head` = `tail` = 0; the current texture `cur` = none; `pending` = false; an anchor
point A, a direction D and two pending edge points.

**Texture**: `tire_track` (shipped: a 32 × 32 TGA, dark grey speckle with alpha up to about 62),
clamped across the mark (u) and repeating along it (v).

**When a wheel marks.** Each physics sample, after the car's own update and before its wheel
particles, for each wheel in the order above: the mark texture is `tire_track` if the wheel is
"majorly slipping" (`slipping`: `slide` > 0.5, `../physics/vehicle-wheels.md`) and its contact
material is not the one named `water` and the car's room is not a drawbridge room (the rooms MM2
marks for each drawbridge leaf: the room holding the leaf's reference point and the room holding
the point 5 m above it; drawbridges: `../game/level-objects.md`); otherwise there is no texture. Then
**Mark**(point *p*, texture) with *p* the wheel's ground hit point (`x` while grounded).

**Mark(p, tex)**:

1. If there is no texture: `pending` = false, `cur` = none; stop.
2. Edges across the wheel: with X the wheel's world X axis (its axle direction, including
   steering; first row of the wheel's matrix), L.c = (X.c·(−hw)) + p.c and R.c = (X.c·hw) + p.c.
3. If `cur` is none and not `pending`: `pending` = true; keep L, R as the pending edges; A = p;
   stop. (A mark starts only after the wheel has moved on.)
4. If `pending` (whatever `cur` is): e = p − A; if (e.z·e.z + e.y·e.y) + e.x·e.x < 0.010000001
   (moved less than 0.1 m), stop. Otherwise push (pending L, pending R, tex, v = 0) — the start of
   a strip — and set `pending` = false; D = e·m with m = 1/sqrt((e.z·e.z + e.y·e.y) + e.x·e.x);
   `cur` = tex; push (L, R, tex, v = sqrt((e.z·e.z + e.y·e.y) + e.x·e.x)·iw). A stays where it
   was. Stop.
5. Otherwise (`cur` set, not pending): e = p − A; len = sqrt((e.z·e.z + e.y·e.y) + e.x·e.x);
   e = e·(1/len); v = len·iw.
   - If tex = `cur`, e·D ≥ 0.99 and len ≤ 10: the newest entry (index `head` − 1, mod 64) is
     overwritten with L, R and v: a straight run stretches its last vertex pair.
   - Otherwise (a bend, a run longer than 10 m, or len = 0, where MM2's direction is undefined and
     the test fails): push (L, R, tex, v); `cur` = tex; D = e; A = p.

**Push(entry)**: nx = (`head` + 1) mod 64. If nx = `tail`: `tail` = (`tail` + 1) mod 64, and if the
entry at (`tail` + 1) mod 64 has v = 0, `tail` = that index (a strip cut down to one entry is
dropped). Write the entry at `head`; `head` = nx. So at most 63 entries are kept per wheel, and
the oldest marks vanish as new ones are laid. Marks never fade.

Note on v: it is the distance from the current anchor divided by the width, not a running total,
so the texture runs back to small v after every bend (with the speckled texture this is not
visible). The vertex at a bend is itself overwritten by the next straight stretch (step 5 replaces
the newest entry, which is the bend's), so the drawn line cuts each bend slightly.

A car reset empties all four rings (`head` = `tail` = 0, `cur` = none, `pending` = false).

**Drawing**: once per frame, in the decal pass (see "Drawing order"), each car's four rings in
wheel order. For a ring, from index `tail` up to `head`: start a triangle strip with that entry's
texture; add its L (u = 0, v) and R (u = 1, v); go to the next index; continue the strip while
the index is not `head` and its entry's v ≠ 0; then end the strip; repeat until `head`. Vertex
colour opaque white. **Depth test and depth writes are both off** while marks are drawn, so marks
show through any static geometry drawn before them (walls, props, raised roads between the camera
and the mark); cars are drawn after them and cover them.

## Order of work and of the random numbers

All effect births draw from the effects generator (`../formats/particles.md`, "Random numbers");
the order below decides which particle gets which numbers. Within one physics sample, for each car
in the order the physics updates cars:

1. the car's own update (`../physics/vehicle.md` steps 1–11);
2. skid marks, wheel by wheel (no draws);
3. wheel particles: wheel by wheel, slot 0 then slot 1, then the wheel emitter's update;
4. the car's damage step (`../physics/damage.md`): the smoke point draw (if any), the smoke burst,
   the smoke emitter's update, the exhaust bursts, the spark step, the chip update, then the slot
   timers, the wreck check and the texture-mark round (`../formats/damage-visuals.md`, which also
   draws from the effects generator).

Then the collisions of the sample: each counted impact draws its sparks (7 per spark) and then its
chips (9 per chip); an object that gets a body draws its activation burst at that moment (an
untouched prop before the responses of the test that touches it, so before that test's sparks
and chips; a broken prop's pieces after them, `../physics/loose-bodies.md` 2.1, 3).

Once per frame, after the physics: each active prop's emitter update (spew); then, before the city
is drawn, the rain emitter's update.

## Drawing order

Per frame, inside the city drawing (`../formats/night.md` for the passes it names):

1. City surfaces and static objects.
2. **Decal pass** (skipped entirely in MM2's software-renderer mode): world matrix identity, alpha
   blending on, alpha test as in the effects pass (below); every car's skid marks (depth test and
   writes off, above), then the ground overlay of `../formats/night.md`.
3. Cars and other moving objects, trees, light glows (fog is switched off from the glows on).
4. **Effects pass**: world matrix identity, alpha blending source·α + destination·(1 − α), alpha
   test on with reference 0 and test "greater" (a pixel is dropped only when its alpha is 0; the
   tree pass just before leaves these values, and the decal pass, which uses the values in force
   before the city drawing began, gets the same ones), depth test on, depth writes off, no fog.
   The items drawn are those registered this frame, in registration order: each car, in update order, registers its wheel emitter (if it has
   live particles), its smoke emitter (if live), its sparks (if any) and its chips; each active
   prop with live particles registers its emitter. At most 63 items are drawn per frame; items
   beyond that are dropped for the frame. (Skid marks are registered the same way, one item per
   car, with the same limit of 63.)
5. Rain, if it is drawn this frame (`../formats/night.md`), with the same state.

Particles are not sorted, within an emitter or between emitters.

## Sounds (for the sound milestone)

No effect on this page plays a sound itself (sounds: `../audio/events.md`). Related sounds, all from other systems:

- impacts: an impact sound at every impact with s > 0.001 (`../physics/damage.md`), loudness from
  the impulse's components (|J.z| + |J.y|) + |J.x|, sample from the other side's impact sound id; props
  use their `ColliderId` (`AudioId` is not used by any sound, `../audio/tyre-impact-sounds.md`);
- tyre squeal and surface sounds come from the wheels' slip and the material `sound` field, not
  from the skid marks or particles;
- there is no water splash sound (impact id 22 plays only for a prop whose `ColliderId` is 22);
- rain has its own ambient loop and thunder.

## Checked against

- Static reading of the game binary: wheel particle set-up, per-wheel emission and the rain rule
  copy; the car damage step (smoke level, point, puffs, exhaust, spark step, chip update); the
  impact handler's spark and chip calls; the spark and chip systems; prop activation and its
  per-frame update; the rain emitter in the city loader and drawer; the skid-mark rings, their
  per-wheel update and drawing; the deferred draw lists and the render states of each pass.
- `tools/formats/particles.py` (dirty side):
  - 12 `.asBirthRule` files, all keys parsed (30 keys each in `tune/effects/`, 18 in
    `rain.asBirthRule`, 24 in `snow.asBirthRule`). Decoded colours: `smoke` (230, 240, 250)
    alpha 240, `splash` the same colour, alpha 255; all others opaque white. BirthFlags: `smoke`
    and `splash` 1, `rain` 8, the rest 0. Burst sizes (particles per second at full load for the
    wheel rules): `dirt` 100, `dust`, `grass`, `leaf`, `snow`, `splash` 64, `smoke`, `rock` 32.
  - 992 prop data files, all with the 24 rule lines in the reader's order (991 blocks named
    `BirthRule`, one `asBirthRule`). `TexNumber`: 954 × 0, then 1 × 1, 3 × 2, 8 × 3, 3 × 4, 6 × 5,
    8 × 6, 8 × 7, 1 × 16 (38 props emit). BirthFlags: 965 × 0, 26 × 112 and 1 × 17352956
    (`default`); all 27 non-zero ones have `TexNumber` 0, so they never emit.
  - 20 `.vehCarDamage` files with all 30 rule keys; e.g. `vpbug`: Life 0.8, Mass 0.591, Radius
    0.361, DAlpha −10, Gravity 15, colour white alpha 245; `vp4x4`: Mass 9.435, Radius 0.85,
    DAlpha −15, Gravity 17.99.
  - First three births of the `smoke` rule from effects state 0: v = (0.1002, 0.4000, 0.2296),
    life 4.8851, inverse mass 10, frame 14, radius 1.5, `da` −5, alpha 240; then (0.1972, 0.4,
    0.2604), life 4.9740, `da` −5; then (0.1149, 0.4, 0.1051), life 5.0080, `da` −4.
- `work/check/particles_check.png`: six emitters simulated with these rules (single precision,
  effects generator from 0, 60 steps a second), side view: tyre smoke and road grit behind a
  sliding wheel moving at 12 m/s, dust on grass, `vpbug` level-4 engine smoke (black puffs rising
  behind the car), the `sp_hotdogcart_f` burst (bottles, cans and paper from `fxpt3`) 0.5 s after
  the hit, and 2 s of rain dying at y = 0. Textures decoded: `ptx_wheel` 256 × 256 (8 × 8 frames:
  clods, dust, grass, leaves, smoke, spray, flowers), `ptx_rain` 64 × 64 (4 × 4 streaks), `fxpt8`
  128 × 128 (2 × 2 smoke shades), `fxpt1`–`fxpt7`, `fxpt16` (debris sheets), `tire_track`
  32 × 32, `spark` 8 × 8 (`work/check/tex/`).
