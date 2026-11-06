# Particles: birth rules and the particle system

MM2 draws its puffs, drops and debris specks with one small particle system. An **emitter** owns a
fixed pool of particles and a texture sheet, and it usually has a **birth rule** that says how new
particles start. This page gives the rule file format, the emitter's state and the exact birth,
update and drawing steps. Which effects use the system, with which rule and when, is in
`../engine/effects.md`. Sparks, body chips and skid marks are separate systems (also there).

Particles are visual only: nothing here touches the physics or its random generator.

Single precision, one rounding per operation, in the order written (`../physics/vehicle.md`,
"Precision"). `trunc` is conversion to an integer toward zero. A "byte" is an 8-bit field that
wraps (arithmetic modulo 256) unless stated otherwise.

## The rule file (`.asBirthRule`)

Text in the tune-file syntax of `../physics/vehicle-tune.md`: an optional `type: a` line, then
`asBirthRule {`, one key per line followed by its value or values, then `}`. Keys may come in any
order or be missing (the default stays); unknown keys are ignored. Rule files live in `tune/`
(`rain.asBirthRule`, `snow.asBirthRule`) and `tune/effects/`. The same keys also appear in two
other files: as a `BirthRule { … }` block in prop data (`tune/banger/*.dgBangerData`,
`../physics/collision-objects.md`), which is read **by position** instead ("Prop data blocks"
below), and directly in `tune/vehicle/<car>.vehCarDamage` (`../physics/damage.md`), by key as
here.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `Position` | 3 floats | 0 0 0 | birth position (most users overwrite it before each burst, `../engine/effects.md`) |
| `PositionVar` | 3 floats | 0 0 0 | spread of the position, per axis |
| `Velocity` | 3 floats | 0 0 0 | birth velocity, m/s (some users scale or rotate it) |
| `VelocityVar` | 3 floats | 0 0 0 | spread of the velocity, per axis |
| `Life` | float | 1 | lifetime, seconds |
| `LifeVar` | float | 0 | spread |
| `Mass` | float | 1 | mass; only its inverse is kept, and it divides the drag |
| `MassVar` | float | 0 | spread |
| `Radius` | float | 1 | half the side of the drawn square, metres |
| `RadiusVar` | float | 0 | spread |
| `Drag` | float | 0 | air drag factor |
| `DragVar` | float | 0 | spread |
| `Damp` | float | 1 | bounce factor on the ground plane (BirthFlags 0x02) |
| `DampVar` | float | 0 | spread |
| `DRadius` | float | 0 | radius growth per 1/60 s |
| `DRadiusVar` | float | 0 | spread |
| `DAlpha` | int | 0 | alpha change per 1/60 s (alpha runs 0–255) |
| `DAlphaVar` | int | 0 | spread |
| `DRotation` | int | 0 | rotation change per 1/60 s, in 1/32 turns ("Drawing") |
| `DRotationVar` | int | 0 | spread |
| `InitialBlast` | int | 0 | particles per burst (its exact use depends on the user, `../engine/effects.md`) |
| `SpewRate` | float | 0 | continuous births per second |
| `SpewTimeLimit` | float | 0 | spewing stops this many seconds after the emitter's reset; 0 = never |
| `Gravity` | float | −9.8 | vertical acceleration, m/s² (positive = up) |
| `TexFrameStart` | int | 0 | first frame of the texture sheet |
| `TexFrameEnd` | int | 0 | last frame |
| `BirthFlags` | int | 0 | bits, table below |
| `Height` | float | 0 | world height (y) of the particle's ground plane |
| `Intensity` | float | 1 | read and never used |
| `Color` | int | −1 | colour and alpha, below |

These are the defaults of a freshly made rule. The car smoke rule starts from other values
(`../engine/effects.md`, "Engine smoke").

**Color** is a 32-bit integer, written in decimal (usually negative). As an unsigned number it is
A·2²⁴ + B·2¹⁶ + G·2⁸ + R: the lowest byte is red, then green, then blue, and the top byte is alpha
(255 = opaque). −1 is opaque white.

**BirthFlags.** Only the lowest 8 bits reach the particles.

| Bit | Meaning |
|---|---|
| 0x01 | props only: births sit at the prop's position and do not follow the prop (`../engine/effects.md`, "Props"); every other user ignores it |
| 0x02 | bounce on the ground plane y = `Height` |
| 0x04 | animated frames: every particle starts at `TexFrameStart` and steps one frame per update |
| 0x08 | stop on the ground plane y = `Height`, then die |
| 0x10 | also draw a flat "shadow" copy on the ground plane |

Shipped rules use only 0, 1 and 8.

**Prop data blocks.** The rule block inside prop data is read in a fixed order, each key token
skipped without being checked: the block's name and `{` (also not checked), then the values of
`Position`, `PositionVar`, `Velocity`, `VelocityVar` (3 floats each), `Life`, `Mass`, `MassVar`,
`Radius`, `RadiusVar`, `Drag`, `DragVar`, `DRadius`, `DRadiusVar` (floats), `DAlpha`,
`DAlphaVar`, `DRotation`, `DRotationVar`, `InitialBlast` (ints), `SpewRate`, `SpewTimeLimit`,
`Gravity` (floats), `TexFrameStart`, `TexFrameEnd`, `BirthFlags` (ints), then one more token
(the `}`). `LifeVar`, `Damp`, `DampVar`, `Height`, `Intensity` and `Color` are never read there
and keep the defaults. Every shipped prop data file has exactly these 24 lines in this order, so
a key reader gives the same values, except that a key reader would miss `sp_tree1_s_BREAK06`'s
block, which is named `asBirthRule`: MM2 reads it like the others. The program also has a rule
writer; nothing uses it.

## Emitter state

| Field | Start | Meaning |
|---|---|---|
| `capacity` | set up once | pool size *N* (per user, `../engine/effects.md`) |
| `count` | 0 | live particles, kept in pool slots 0 … count − 1 |
| `sheet` | set up once | texture and its frame grid, `cols` × `rows` frames |
| `rule` | none | the rule used for spewing (some users pass a rule with each burst instead) |
| `follow` | none | an optional matrix that births are placed with (props) |
| `t` | 0 | seconds since the last reset |
| `carry` | 0 | fraction of a spew birth carried to the next update |
| `bright` | 1 | multiplies red, green and blue of every particle at each update; no user changes it |
| `wind` | 0 0 0 | air velocity seen by the drag; always 0 in MM2 |

**Reset**: `count` = 0, `follow` = none, `t` = 0. `carry` is kept.

Each particle has a velocity **v**, `life`, `im` (inverse mass), `drag`, `damp`, `g` (gravity),
`dr` (radius growth), `da` (alpha step, signed byte), `drot` (rotation step, signed byte), `f0`
and `f1` (frame range, bytes) and its base colour R0, G0, B0 and alpha A (bytes). For drawing it
also has `frame` (signed byte), `rot` (byte), `flags` (byte), `radius`, the current colour
(A, R, G, B), `h` (ground height) and a position **p**.

## Random numbers

Every random number of a birth is r = the next value of the **effects generator**, in [0, 1).
This is the second state of MM2's generator (`../physics/collision.md`, "Random numbers"): the
same formula as the game-wide generator with its own 32-bit state, which is 0 when the program
starts and is never restarted. "raw" means the 15-bit integer before scaling. Particles never
touch the game-wide state. A spread value is

    vary(base, var) = ((r − 0.5)·var) + base

so it lies in [base − var/2, base + var/2).

## Birth of one particle

With rule R, into the slot at index `count`:

1. **v** = (vary(R.Velocity.x, R.VelocityVar.x), vary(….y), vary(….z)), in that order.
2. `im` = 1 / vary(R.Mass, R.MassVar).
3. `life` = vary(R.Life, R.LifeVar).
4. `drag` = vary(R.Drag, R.DragVar), then `damp` = vary(R.Damp, R.DampVar).
5. `f0` = R.TexFrameStart, `f1` = R.TexFrameEnd (as bytes). `frame`: if BirthFlags has 0x04,
   `f0`; otherwise f0 + (raw mod (TexFrameEnd − TexFrameStart + 1)), one raw draw, integer
   remainder, the sum kept as a byte.
6. `radius` = vary(R.Radius, R.RadiusVar).
7. **p** = (vary(R.Position.x, R.PositionVar.x), vary(….y), vary(….z)).
8. Base colour and alpha from `Color`; the current colour is set to the base colour (`bright`
   is applied only from the first update on).
9. `g` = R.Gravity; `dr` = vary(R.DRadius, R.DRadiusVar).
10. `da` = trunc(R.DAlpha + ((r − 0.5)·R.DAlphaVar)), kept as a signed byte; `rot` = 0;
    `drot` = trunc(R.DRotation + ((r − 0.5)·R.DRotationVar)), kept as a signed byte.
11. `h` = R.Height; `flags` = the low byte of R.BirthFlags.

A birth takes 14 draws, or 15 when step 5 draws.

## Burst

**Blast(n, rule)** uses the rule passed in, else the emitter's `rule`; with neither, nothing
happens. n is cut to `capacity` − `count`. Then, n times: give birth into slot `count`; if
`follow` is set, replace **p** by **p** placed with that matrix (row vectors:
p.x·X + p.y·Y + p.z·Z + P, with X, Y, Z its rows and P its position; the position only, not the
velocity); `count` += 1.

## Update

One call advances the emitter by a step time dt. Users call it once per physics sample or once
per frame (`../engine/effects.md` says which); dt is that sample's or frame's length. Let
s = dt·60.

1. **Spew.** `t` = `t` + dt. If there is a `rule` and (`t` < SpewTimeLimit or SpewTimeLimit = 0):
   a = (dt·SpewRate) + `carry`; n = trunc(a); `carry` = a − n; if a ≥ 1, Blast(n). A rule with
   SpewRate 0 never spews.
2. **Each particle**, slot i from 0 while i < `count`:
   1. `life` = `life` − dt.
   2. If `life` ≤ 0, or p.y < −50, or its current alpha (from the previous update or its birth) is
      0, it dies: `count` −= 1, the particle in the last slot moves into slot i, and i is **not**
      advanced, so the moved particle is updated next. Continue with the loop.
   3. Drag and gravity, with **u** = **v** + `wind`:

          k   = −(sqrt((u.y·u.y + u.z·u.z) + u.x·u.x)·drag)
          v.x = (((u.x·k)·im)·dt) + v.x
          v.y = ((((u.y·k)·im) + g)·dt) + v.y
          v.z = (((u.z·k)·im)·dt) + v.z

      then p.c = (dt·v.c) + p.c for each component c, with the new **v**. Gravity is not divided
      by the mass; the drag is (heavier particles feel less drag).
   4. Colour: R = trunc(R0·`bright`), G = trunc(G0·`bright`), B = trunc(B0·`bright`).
   5. Alpha: if `da` ≠ 0 and A > 0: q = trunc(`da`·s); if A + q < 0 then A = 0, else A = A + q
      kept as a byte (a positive `da` wraps past 255 back to small values).
   6. `rot` = `rot` + trunc(`drot`·s), as a byte.
   7. `radius` = (s·`dr`) + `radius`.
   8. Ground:
      - flags has 0x08 and p.y < h: p.y = h; **v** = 0, g = 0, im = 0, `life` = 0. The particle
        is drawn once more, lying on the plane, and dies at the next update.
      - otherwise, flags has 0x02 and p.y < `radius` + h: p.y = h (the centre goes **onto** the
        plane, so half the square is below it); then if v.y < 0: v.x = damp·v.x,
        v.y = −(damp·v.y), v.z = damp·v.z.
   9. Frames: if flags has 0x04 and f0 < f1, `frame` = ((frame − f0 + 1) mod (f1 − f0)) + f0. The
      frames run f0 … f1 − 1, so the frame `TexFrameEnd` itself never shows.
   10. i += 1.

Consequences of the arithmetic (keep them):

- Alpha and rotation change by whole steps per update, so fading speed depends on the update rate.
  At 1/60 s a particle steps by exactly `da`; at 1/144 s a `da` of −2 gives trunc(−0.83) = 0 and
  the particle never fades.
- The frame animation steps once per update, however long the update is.
- A particle dies of alpha one update after its alpha reached 0.

## Drawing

**Texture sheet.** The texture is cut into `cols` × `rows` equal frames, numbered row by row:
frame k is column k mod cols, row k div cols, with row 0 at v = 0 (`tex.md`, "Row order").

**The square.** Four corners (x, y, u, v): (−1, −1, 0, 0), (1, −1, 1, 0), (1, 1, 1, 1),
(−1, 1, 0, 1), drawn as a triangle fan in this order. Rotation has 32 steps. Step k turns a
corner by θk, with θ0 = 0 and θ(k+1) = θk + 0.19634955 (2π/32):
(x, y) → (cos θ·x − sin θ·y, cos θ·y + sin θ·x). The step used is `rot` mod 32, so `rot` counts
1/32 turns and wraps every 8 turns. Frame k's texture coordinates are
u = (column + corner u)/cols, v = (row + corner v)/rows.

**Frames outside the sheet.** MM2 keeps these coordinates in a table of cols·rows frames and
never checks `frame` against it. A frame outside 0 … cols·rows − 1 (a rule whose `TexFrameEnd` is
past the sheet's last frame; shipped: the tree rules with frames 0–4 on 2 × 2 sheets,
`../engine/effects.md`, "Props") reads memory beside the table. For frame cols·rows that is the
next heap block's 8-byte header (an address and a size, both about 0 as floats) and the first
24 bytes of that block, which depend on the program's whole allocation history and change during
play. There is nothing to reproduce. *Engine choice:* such a particle is born, updated and counted
as usual (its random draws are unchanged), but neither it nor its shadow is drawn. For the shipped
sheets (`fxpt4`, `fxpt5`) this is what MM2 shows whenever those bytes are near 0: the corners then
all sample the sheet's corner, whose texels have alpha 0, and the alpha test drops the square.

**Billboard pass.** Every live particle, in slot order (no sorting, no culling). With the turned
corner (cx, cy):

    vertex.c = ((X.c·(radius·cx)) + (Y.c·(radius·cy))) + p.c

where X and Y are the camera's right and up axes (rows of the camera's world matrix,
`../engine/cameras.md`), so the square always lies in the screen plane. The vertex colour is the
particle's current (A, R, G, B), and the texture is modulated by it.

**Shadow pass.** Drawn before the billboards, only for particles with flag 0x10 and p.y − h > 0.01:
a flat square at y = h with vertex = (p.x + radius·cx, h, p.z + radius·cy), same corners, frame
and rotation. Its colour keeps alpha A, halves each channel by dropping the lowest bit, and swaps
red and blue (red channel = B/2, green = G/2, blue = R/2), as MM2 does.

**Render state** for every particle pass (`../engine/effects.md`, "Drawing order"): world matrix
identity, alpha blending source·α + destination·(1 − α), alpha test on (reference 0, test
"greater": only pixels with alpha 0 are dropped), depth test on, depth writes off, **no fog**.
Particle textures are dimmed at night like every texture (`night.md`).

## Limits

- An emitter never holds more than its `capacity`. Births beyond it are dropped silently (the
  spew carry is still used up).
- MM2 has a "software renderer" mode (no 3D card) that quarters every pool, multiplies every
  lifetime by 0.333 and skips the skid-mark pass. The hardware behaviour on these pages is the
  reference.

## Checked against

- Static reading of the game binary: the rule reader, the prop data block reader, the emitter,
  the birth, burst, update and drawing routines, the random-generator switch around each birth,
  and the alpha-test state of the effects pass.
- `tools/formats/particles.py` (dirty side) parses the 12 shipped rule files, the rule blocks of
  the 992 prop data files by position (all 992 have the 24 lines in the reader's order; only
  `sp_tree1_s_BREAK06` would differ under a key reader) and the 20 `.vehCarDamage` files.
  Results and decoded samples are in `../engine/effects.md`, "Checked against".
