# Car models: assembling a vehicle from its package

This file says which parts make up an MM2 vehicle, where each part goes, which material set
(paint) is used, and what to draw for a still "show the car" view. The file formats themselves
are in `pkg.md` (packages, `.mtx` pivots, coordinate system) and `tex.md` (textures).

Model space recap: right-handed, +Y up, metres, the vehicle faces −Z, +X is its right side.

## Files that make up a vehicle

For a vehicle with base name `<car>` (player cars are `vp…`, traffic cars `va_…`):

| File | Contents |
|---|---|
| `geometry/<car>.pkg` | all meshes and the material sets |
| `geometry/<car>_<PART>.mtx` | pivot of each separately placed part (wheels, breakables, …) |
| `geometry/<car>_trailer.pkg` + `geometry/<car>_trailer_<PART>.mtx` | optional trailer |
| `geometry/<car>_dash.pkg` + `_dash_*.mtx` | cockpit view (`dash.md`) |
| `tune/<car>.info` | menu data; its `Colors=` line names the paint sets in order, separated by `|` |

## Which player cars exist

MM2's car list comes from the `tune/` folder, not from `geometry/`:

1. First the 20 built-in cars, in this order: `vpCOOP`, `vpbug`, `vpcab`, `vpcaddie`, `vpford`,
   `vpmustang99`, `vpcop`, `vpbullet`, `vppanoz`, `vpbus`, `vpddbus`, `vpcentury`, `vpcoop2k`,
   `vpdune`, `vpvwcup`, `vp4x4`, `vpauditt`, `vpdb7`, `vppanozgt`, `vpsemi` — each read from
   `tune/<name>.info`.
2. Then every other file in `tune/` whose name ends in `.info` or `.INFO` (these two spellings
   only), so a mod adds a car by adding `tune/<car>.info` plus its geometry. The folder is walked
   archive by archive in priority order, then in each archive's entry order (`ar.md`).
3. A car is skipped if a car already in the list has exactly the same `BaseName` (plain,
   case-sensitive comparison; the file name does not matter), so the first one wins. A car is also
   skipped if its `.info` fails the reading rules below.

**Reading a `.info` file.** It is text, but MM2 does not read it as a free list of keys. It reads
twelve lines in a fixed order, each with a fixed pattern, and rejects the car if any of them does
not match:

1. For each of the twelve, skip every tab, line feed, space and carriage return (any number, so
   blank lines and indentation are fine), then take the rest of that line up to and including
   its line feed (at most 255 characters; a longer line is cut there and its rest counts as the
   next line). If the file ends first, the car is rejected.
2. Match the line against the pattern of its turn:

   | Turn | Pattern | Value |
   |---|---|---|
   | 1 | `BaseName=` word | spaces before it skipped, then up to the next space, tab, CR or LF (at most 39 characters) |
   | 2 | `Description=` text | everything up to the first CR (at most 79 characters) |
   | 3 | `Colors=` text | everything up to the first CR (at most 99 characters) |
   | 4 | `Flags=` integer | |
   | 5 | `Order=` integer | |
   | 6 | `ScoringBias=` number | decimal, may have a fraction and exponent |
   | 7 | `UnlockScore=` integer | |
   | 8 | `UnlockFlags=` integer | |
   | 9 | `Horsepower=` integer | |
   | 10 | `Top Speed=` integer | |
   | 11 | `Durability=` integer | |
   | 12 | `Mass=` integer | |

   The key text must match exactly, **case included** (`mass=` fails); the space in `Top Speed`
   matches any run of spaces or tabs, or none (`TopSpeed=` also works). An integer or number may
   have spaces before it and an optional sign; reading stops at the first character that does not
   fit, so `Mass=1500.5` gives 1500 and is accepted, but a value that does not start with a digit
   (`Mass=abc`, `Mass=.5`) rejects the car. A text value needs at least one character: an empty
   `Description=` or `Colors=` in a file with CR LF line ends rejects the car. With LF-only line
   ends the text values keep the trailing line feed (there is no CR to stop at). MM2 does not
   check the lengths; longer values overrun the fields after them, so treat them as invalid.
3. After the twelfth line every remaining line (read 127 characters at a time) is looked at only
   for `UIDist`: the text before the line's first `=` must be exactly `UIDist` (no spaces); the
   value is the text after the `=` up to the next space, read as a decimal number (leading
   whitespace skipped, 0 if it is not a number). The last such line wins; without one,
   `UIDist` = 6.0. (It is a menu value; shipped cars have 5 to 14.) Every other line is ignored.

All 20 shipped `.info` files follow this layout (CR LF line ends). For models only these values
matter: `BaseName` (the `<car>` used for every file name above), `Description` (display name) and
`Colors` (paint names, `|`-separated, one per material set in order). The other keys
(`Horsepower`, `Mass`, unlock rules, …) belong to the menu and vehicle specs.

Shipped `vp*.pkg` packages that are not in the menu list: `vpftruck`, `vplafrance`,
`vpmoonrover`, `vpvwdune`, and the leftovers `Vpvw_cup`, `vpvw_dune` (truncated) and `vpdb731`
(PKG2, broken wheel pivots). They follow the same rules when something does load them. `*_dash`
and `*_trailer` packages are never cars by themselves.

## Part lists

MM2 asks for a vehicle's parts in a fixed order, and (as `pkg.md` rule 3 explains) a package
entry that is out of this order is not just ignored: it stays unread and hides every later part
of the package. Each name is looked up as `<NAME>_H`, `_M`, `_L`, `_VL`. "Pivot" means the part
is drawn translated by the origin from its `.mtx`; parts without it are drawn at the car's
origin.

### Player cars

| # | Part | Pivot | What it is / when MM2 draws it |
|---|---|---|---|
| 0 | `BODY` | — | the car body; required |
| 1 | `SHADOW` | — | flat shadow at y≈0, projected onto the ground; unlit, alpha-blended |
| 2 | `HLIGHT` | — | loaded, never drawn (headlights are billboards, `night.md`) |
| 3 | `TLIGHT` | — | tail-light glow; once while braking and once more while headlights are on (glow pass, `night.md`) |
| 4 | `RLIGHT` | — | reversing-light glow; in reverse gear (glow pass) |
| 5–6 | `SLIGHT0`, `SLIGHT1` | — | indicator glows; player cars never draw them, traffic blinks them (`night.md`) |
| 7 | `BLIGHT` | — | brake-light glow; while braking (glow pass) |
| 8 | `BODYDAMAGE` | — | loaded, never drawn by the car |
| 9–10 | `SIREN0`, `SIREN1` | — | loaded, never drawn (siren lights are glows at `SRN0`–`3`) |
| 11 | `DECAL` | — | drawn right after the body, alpha-blended |
| 12 | `DRIVER` | — | loaded, never drawn by the car |
| 13–16 | `SHOCK0`–`SHOCK3` | live (`.mtx` is a matrix) | suspension parts (none ship) |
| 17–20 | `ARM0`–`ARM3` | live (`.mtx` is a matrix) | suspension parts (none ship) |
| 21–22 | `SHAFT2`, `SHAFT3` | live (`.mtx` is a matrix) | drive shafts (none ship) |
| 23–24 | `AXLE0`, `AXLE1` | live (`.mtx` is a matrix) | axles (none ship) |
| 25 | `ENGINE` | live | engine block, can break off (none ship) |
| 26–29 | `WHL0`–`WHL3` | yes | wheels; `WHL0` required |
| 30–37 | `BREAK0`, `BREAK1`, `BREAK2`, `BREAK3`, `BREAK01`, `BREAK12`, `BREAK23`, `BREAK03` | yes | parts that break off in crashes (mirrors, bumpers, roof racks) |
| 38–41 | `HUB0`–`HUB3` | wheel's | hub caps, drawn with the matching wheel |
| 42 | `TRAILER_HITCH` | yes | not drawn; its pivot is the towing point |
| 43–46 | `SRN0`–`SRN3` | yes | not drawn as meshes; each pivot places a siren light and the diffuse colour of its first batch's material colours it |
| 47–48 | `HEADLIGHT0`, `HEADLIGHT1` | yes | not drawn as meshes; pivot = headlight glow position (0 = right, 1 = left), first batch's material diffuse = glow colour (`night.md`) |
| 49–50 | `FNDR0`, `FNDR1` | `FNDR0`'s, relative to the wheel | front fenders that turn with the front wheels ("Fenders") |
| 51–52 | `WHL4`, `WHL5` | yes | extra rear wheels of six-wheelers |
| 53–62 | `VARIANT0`–`VARIANT9` | yes | extra part shown only with paint set *n*; can break off (none ship) |

"live" parts are moved with the car simulation every physics sample ("Live suspension parts"
below; the engine block's own movement belongs to the vehicle spec). For a still view put
`ENGINE` at its `.mtx` origin and the suspension parts at their rule below with travel 0.

### Traffic cars

`BODY` (required), `SHADOW`, `HLIGHT`, `TLIGHT`, `SLIGHT0`, `SLIGHT1`, `WHL0`–`WHL3` (required),
`BREAK0`–`BREAK3`, `HEADLIGHT0`, `HEADLIGHT1`, `WHL4`, `WHL5` — same meanings as above, except
that traffic draws `SLIGHT0`/`SLIGHT1` as blinking indicators and lights white headlight glows
(`night.md`). Wheel pivots come from `WHL0`…`WHL5.mtx`.

### Trailers

`<car>_trailer.pkg`: `TRAILER` (the body, required), `SHADOW`, `TLIGHT`, `TWHL0`, `TWHL1`
(required), `TWHL2`, `TWHL3`, `TRAILER_HITCH`. `TWHL4`/`TWHL5` exist in one package but MM2 never
loads them. A trailer is attached only if the car package has a `TRAILER_HITCH` mesh (any level,
after the fill rule) **and** the origin in `<car>_TRAILER_HITCH.mtx` is not exactly (0, 0, 0). The
`.mtx` alone does nothing, and the tune files (`.vehtrailer`, `.dgtrailerjoint`) only adjust a
trailer that exists. So of the shipped cars only `vpsemi` (and the unlisted `vpftruck`,
`vplafrance`) get a trailer; **`vpcentury` has no `TRAILER_HITCH` mesh and gets none**, although
`vpcentury_trailer.pkg` ships. (In multiplayer Cruise and Cops 'n' Robbers the player gets no
trailer.)

## Where each part goes

- **Body, shadow, glow meshes, decal**: at the car origin, as stored. When and how the light
  glows are drawn (additive, at night, braking, …) is in `night.md`.
- **Pivoted parts**: mesh translated by the `.mtx` origin (floats 9–11). No rotation is stored
  (except for the live suspension parts, below).
- **Wheels**: the mesh is centred on its own axle; the axle is the X axis. A wheel spins about
  X and the front wheels steer about Y, both around the wheel's own origin, before the
  translation. Radius = (box max.y − min.y) / 2, width = max.x − min.x (used by physics).
  Usual layout: `WHL0` front-left, `WHL1` front-right, `WHL2` rear-left, `WHL3` rear-right — but
  always use the `.mtx` positions: two traffic cars (`va_cooper_l`, `va_limo_f`) have the rear
  pair swapped.
- **Hubs**: same transform as the wheel with the same number.
- **Six-wheelers**: for a still view put `WHL4`/`WHL5` at their own `.mtx` origins (traffic cars
  do this when moving too). Player cars in motion draw them at the live `WHL2`/`WHL3` transform
  moved along the car's +Z axis (toward the rear) by 2.2 × that wheel's radius.
- **Fenders** (`FNDR0`, `FNDR1`): placed relative to the drawn front wheels, not at their own
  pivots; drawn only at level H and only while attached (`../physics/damage.md`, "Parts thrown
  off"). At load, and only if `FNDR0` has an H mesh (after the fill rule): o = (origin of
  `FNDR0.mtx`) − (origin of `WHL0.mtx`, the wheel's centre), per component, then
  o.y = o.y + 0.025. `FNDR1` uses (−o.x, o.y, o.z); `FNDR1.mtx` is never read. (MM2 does not
  check that `FNDR0.mtx` exists: require it. If only `FNDR1` exists, its offset stays (0, 0, 0).)
  Each frame `FNDR0` is drawn with the frame X = the drawn `WHL0`'s X axis (world), Y = the
  car's Y axis (world), Z = X × Y with Z.x = Y.z·X.y − Y.y·X.z, Z.y = X.z·Y.x − Y.z·X.x,
  Z.z = Y.y·X.x − X.y·Y.x, and origin p.c = ((W.c + o.x·X.c) + o.y·Y.c) + o.z·Z.c, W = the drawn
  `WHL0`'s world position; `FNDR1` the same with the drawn `WHL1` and its mirrored offset. So a
  fender turns with its wheel's steering and rides up and down with it, does not spin, and sits
  2.5 cm higher than its pivot says. Still view: `FNDR0` at its own pivot + (0, 0.025, 0),
  `FNDR1` at the `WHL1` origin + (−o.x, o.y, o.z).
- **Breakables and variant parts**: at their pivot while attached (what throws them off:
  `../physics/damage.md`, "Parts thrown off").
- **Live suspension parts**: see below.
- **Trailer**: the trailer's own origin goes at `car hitch origin − trailer hitch origin`
  (`<car>_TRAILER_HITCH.mtx` and `<car>_trailer_TRAILER_HITCH.mtx`, floats 9–11), with no turn
  when standing straight. Its wheels `TWHL0`–`TWHL3` are placed by their own `.mtx` origins in
  trailer space. Example: `vpsemi` hitch (−0.074, 1.576, 2.249), trailer hitch
  (−0.066, 1.576, −5.861) → trailer origin at (−0.008, 0, 8.110), behind the cab. In a game the trailer
  is placed and moved by `../physics/trailer.md` (its `.vehTrailer` may replace both hitch points;
  `vpsemi`'s does).

### Live suspension parts

None ship; these are MM2's rules for mods. `SHOCK<n>` and `ARM<n>` belong to wheel *n*,
`SHAFT2`/`SHAFT3` to wheel 2/3, `AXLE0` to wheels 0 and 1, `AXLE1` to wheels 2 and 3. Each reads
its `<car>_<PART>.mtx` (48 bytes) as a matrix L with rows X, Y, Z and origin P (`pkg.md`,
"Coordinate system"), in car space. A part without its `.mtx` is never placed (MM2 then draws it
at the world origin); do not draw it. Drawn at levels L, M and H (not VL), lit as any car part.

For a wheel *w*: C = its centre (origin of `WHL<w>.mtx`), t_w = comp − visualDisp of that wheel
(the drawn wheel's lift, `../physics/vehicle-helpers.md`). With D = C − P per component and a row
A, "reach along A" is ((D.z·A.z + D.y·A.y) + D.x·A.x).

- **Shock, arm, shaft.** At load: k = 1 / (reach along Z); mode 1 if |Z.y| ≥ 0.5, else mode 0.
  Every physics sample (as part of the car simulation, after the anti-roll bars): Z.y = t·k in
  mode 0, Z.y = t·k + 1 in mode 1 (the file's Z.y is replaced, the rest of L kept). So the part
  is sheared (mode 0) or stretched (mode 1) so that its point at the wheel follows the wheel's
  lift.
- **Axle.** At load, with C of its first wheel: kz = 1 / (reach along Z), kx = 1 / (reach along
  X). Every sample, in that axle's anti-roll bar step (`../physics/vehicle-helpers.md`), a = first
  wheel, b = second: d = t_a − t_b; X.y = (d·kx)·0.5; Z.y = ((t_b + t_a)·kz)·0.5.
- **Placement**: L (as changed) followed by the car's world frame: rotation rows
  M[i][j] = Σₖ L[i][k]·Car[k][j], origin O.c = (Σₖ P.k·Car[k][c]) + Car.origin.c, the sums in
  the order of k below (as in `../physics/vehicle-body.md`, "Order a, b, c"):

  | | j = 0 | j = 1 | j = 2 |
  |---|---|---|---|
  | i = 0 | 0, 1, 2 | 1, 2, 0 | 1, 2, 0 |
  | i = 1 | 1, 0, 2 | 2, 1, 0 | 2, 1, 0 |
  | i = 2 | 0, 2, 1 | 2, 1, 0 | 2, 1, 0 |
  | origin | 1, 0, 2 | 2, 1, 0 | 2, 1, 0 |

  For a still view use t = 0 (mode 0: Z.y = 0; mode 1: Z.y = 1; axle: X.y = Z.y = 0).

(MM2's axle step also tilts the drawn wheels of its pair about the axle's Z axis, for every car,
with or without `AXLE` parts. That drawn-wheel tilt is not specified yet; see "Wheels" above for
what is.)

## Paint (material set) choice

Every vehicle package has one or more material sets (`pkg.md`); each set is one paint scheme.
All parts of the car and its trailer use the same set index.

- **Player and opponent cars**: the chosen colour number *c* is the position in the `.info`
  `Colors=` list (first = 0). Set = *c* modulo the number of sets (a *c* above 255 counts as 0).
  Example: `vpbug` `Colors=Yellow|Blue|Silver|Red`, 4 sets whose body textures are
  `vpbugyellow_*`, `vpbugblue_*`, `vpbuggrey_*`, `vpbugred_*`.
- **Trailer**: the car's set index, modulo the trailer's own set count.
- **Traffic**: set = `floor(r × (n − 1))` where *r* is a random number in [0, 1) and *n* the set
  count. So traffic never uses the last set, and a car with one or two sets is always set 0.
  (MM2's random source is a 15-bit value / 32768.)
- Only the chosen set's textures need loading.

## Damage texture slots (`_dmg`)

Car bodies use two material slots for the same painted surface: one named like
`vpbugyellow_sd` and one named `vpbugyellow_sd_dmg`, usually covering the two sides of the car.
MM2 paints crash damage into a private copy of each texture, mixing in the `_dmg` image (the
damage itself belongs to the vehicle-damage spec). What matters for drawing an undamaged car:

- A slot whose texture name ends in `_dmg` (any case) shows the texture **without** the `_dmg`
  suffix, if that one exists; otherwise the `_dmg` texture itself.
- A slot whose name does not end in `_dmg` shows its own texture; `<name>_dmg` (if it exists) is
  the image the damage reveals.

Showing the raw `_dmg` texture would draw one side of the car already wrecked. How impacts then
uncover the damage images is in `damage-visuals.md`.

## Levels of detail on cars

MM2 picks one level per car, player and traffic alike, with the same rule as city objects
(`inst.md`, "Drawing"): *d* = the depth of the car's origin along the camera's viewing direction
(not the straight-line distance) minus the body's radius (largest over its levels); VL if *d* > T3,
L if *d* > T2, M if *d* > T1, else H, with T1–T3 from the "Object Detail" setting (`inst.md`
table; at the highest setting 70 / 130 / 200 m). Player cars are not limited by the room limit;
they are culled only by the view and the far plane. **Traffic cars** differ (`../ai/traffic-visuals.md`,
"Drawing"): they are drawn only if their room's distance and their own depth are within the room
limit, and their level uses the depth of the point one Y-row length above the origin. What MM2 draws
at each level:

| Level | Player car | Traffic car |
|---|---|---|
| VL | body only | body, breakables (H meshes), no wheels |
| L, M | body, decal, attached breakables and the `VARIANT<s>` part, wheels and hubs, six-wheel extras, live suspension parts — all at that level | body at the current level, breakables (H meshes), no wheels |
| H | as L/M at H, plus fenders and the reflection pass | body, breakables, wheels `WHL0`–`WHL5` at H turning, reflection pass |

Missing levels are filled upward from lower ones (`pkg.md`), so a part with only `_L` shows at L,
M and H. (The glow parts `TLIGHT`, `BLIGHT`, `RLIGHT` are not drawn with the car at all: the glow
pass draws their H slot whatever the car's level, `night.md`.) `VARIANT<s>` is used only if it has an `_H` mesh, and then appears at
H and, if it has `_M`/`_L` meshes, at M/L; never at VL. Traffic cars have no variant parts. MM2
has an internal switch to draw traffic wheels (from their `_L` meshes) below H, but nothing ever
turns it on, so traffic `WHL*_M`/`_L` meshes are never used.

**Shadow.** The `SHADOW` part is not drawn with the car. A separate pass after the city draws it
for every car whose room is closer than the room limit (`inst.md`), whatever the car's level and
even if the body itself is out of view, using `SHADOW`'s H slot (after the fill rule: `_H`, else
`_M`, `_L`, `_VL`) with the car's paint set, alpha-blended, projected onto the ground (projection
belongs to the renderer spec). Traffic shadows differ: they are drawn only after the car's last
placement probed the ground, and with the car's own matrix rather than projected while the car
drives on its rails (`../ai/traffic-visuals.md`, "Shadow").

### Reflection pass

At H, after the body, MM2 draws each batch of the `BODY_H` mesh a second time with the
environment texture `texture/refl_dc`, added on top (source × 1 + destination × 1), player and
traffic cars alike:

- Colour = texture × *g*, a grey *g* = trunc(255 × **power**) / 255 from the batch's material
  (the "current environment strength" is always 1.0: it does not depend on city, room, time of
  day or weather). Batches with *g* = 0 are skipped. Shipped powers are 0–0.95 (paint typically
  0.4–0.5, undersides and tyres 0).
- Texture coordinates from the vertex normal *n* turned into world space by the car's rotation:
  u = 0.5 + 0.5·*n.x*, v = 0.5 − 0.5·*n.y*. They do not depend on the camera; the reflection only
  changes as the car turns.
- Only when the "Vehicle Reflections" option is on (MM2's setup turns it on whenever a 3D card is
  present); off means no pass at all.
- The menu's car showroom uses `refl_showroom` the same way, regardless of the option.

A still viewer should use `refl_showroom` (or `refl_dc` if missing) with these coordinates.

## Showing a still car

This recipe is our viewer's choice: it shows every part a driving car shows at level H, standing
still. (MM2's own menu showroom draws less; see the note after the list.) To draw a complete
undamaged car standing on the ground:

1. Load `<car>.pkg`, pick paint set *s* (above).
2. Draw at the car origin: `BODY_H`, then `DECAL` if present (blended).
3. For each of `WHL0`…`WHL5` that exists: draw it translated by its `.mtx` origin; draw `HUB<n>`
   (n < 4) with it.
4. Draw `FNDR0`/`FNDR1` as "Fenders" says for a still view, all `BREAK…` parts and `VARIANT<s>`
   (if present) at their pivots, and live suspension parts with travel 0.
5. Do **not** draw the glow parts (`HLIGHT`, `TLIGHT`, `RLIGHT`, `BLIGHT`, `SLIGHT0/1`,
   `SIREN0/1`), `HEADLIGHT0/1`, `SRN0`–`3`, `TRAILER_HITCH`, `BODYDAMAGE`, `DRIVER`.
6. Ground height: `g = min over wheels (origin.y − radius)`. Draw `SHADOW`'s H slot (`SHADOW_H`,
   else `_M`) flattened to y = g + a few millimetres, unlit and blended, before the car.
7. Trailer: if `<car>_trailer.pkg` exists, the car has a `TRAILER_HITCH` mesh and its hitch
   `.mtx` origin is not (0, 0, 0), place it as above with the same paint index.
8. Texture slots ending in `_dmg` use the clean texture (above). Light the body with its
   materials; the shadow is unlit.

**MM2's showroom** (the car-select menu) differs; a viewer may copy it but need not:

- It reads `BODY_H` only if it is the package's first entry, then jumps forward (by entry sizes)
  to `SHADOW_H`, `WHL0_H`, `WHL1_H`, `WHL2_H`, `WHL3_H` in turn and reads each. Then it walks on
  to `shaders`, taking `_H` meshes of `BREAK0`, `BREAK1`, `BREAK2`, `BREAK3`, `BREAK01`,
  `BREAK12`, `BREAK23`, `BREAK03`, `FNDR0`, `FNDR1`, `WHL4`, `WHL5` (at most 12): for each
  entry whose name has `H` right after its first `_`, it goes through that list in order and
  reads the entry if it is the current list name + `_H` (the next entry then becomes current and
  the list goes on from there); after the list it steps over one entry. So a wanted entry right
  after one just read is missed if its list position is not later (`BREAK1_H` followed by
  `BREAK0_H`). If any of `SHADOW_H`, `WHL0_H` … `WHL3_H` is missing or out of this order, the
  jump runs to the end of the package and the walk never meets `shaders`: MM2's showroom then
  hangs. Shipped cars all pass and lose nothing.
- It draws, at the car's origin, `SHADOW_H` as stored (not flattened), then `BODY_H`, then the
  reflection pass on `BODY_H` (`refl_showroom`), then `WHL0`–`WHL3` and the extra parts, each
  translated by its `.mtx` origin ((0, 0, 0) for an extra part without one), not turned. No
  `HUB`, `DECAL`, `VARIANT`, live part or glow, and no night rules (`night.md`).
- Materials: all sets loaded; for every textured material a trailing `_dmg` is cut off the
  texture name (with no fall-back if the clean texture is missing), and diffuse, ambient and
  specular alpha are set to 1 and emissive alpha to 0, so material alpha never makes a showroom
  car translucent.

## Checked against

`tools/formats/car.py` builds cars exactly by the recipe above and renders them with textures
to `work/check/car_*.png`; all looked right:

- `vpbug` (4 paint sets, yellow and silver tried): body 7 batches / 338 vertices at H; wheels at
  (∓0.754/0.793, 0.307, −1.184) front and z = 1.381 rear, radius 0.336; ground g = −0.029.
- `vpcop` set 1 (London police livery), `vp4x4` (roof rack and bull bar = `BREAK01/03/12/23`),
  `vpcab` (`BREAK01`, `BREAK2`, `BREAK23` with box-in-parent-space `.mtx`), `vppanoz` (fenders),
  `vpauditt`, `va_bus_f` and `vpcentury` (six wheels), `va_limo_f` (untextured paint),
  `vpsemi` and `vpcentury` with trailers (the vpcentury trailer render predates the trailer rule
  above; MM2 does not attach it).
- The order rule: every shipped `vp*.pkg`, `va_*.pkg` and trailer lists its parts in the order
  above (only `vpcentury_trailer`'s `TWHL4/5` fall outside it).
- Ground heights over the 47 vehicles with wheel `.mtx`: −0.106 (`vp4x4`) to +0.011 (`vpford`),
  most about −0.02; the exception is the unused `vpdb731` (−1.79, broken wheel pivots).
- `vpbug` body: the batches using `vpbugyellow_sd`, `_ft`, `_bk` lie at x ≤ 0.017 (left half),
  the ones using `vpbugyellow_sd_dmg`, `_ft_dmg`, `_bk_dmg` at x ≥ 0.017 (right half); same
  vertex and triangle counts pairwise.
- Material power over the first paint set of all player cars: 0 (293 slots), 0.25 (99),
  0.5 (68), 0.4 (59), 0.45 (32), the rest between 0.05 and 0.7.
- The `vppanoz` render predates the fender rule ("Fenders"): it shows the fenders at their own
  pivots, 2.5 cm lower than MM2 draws them.
- `.info`: all 20 shipped files pass the twelve-line reading in order (CR LF ends); their
  `UIDist` lines give 5 to 14.
- MM2's showroom walk, simulated over every shipped `vp*.pkg` car: all reach `shaders` and take
  every `BREAK…`, `FNDR…`, `WHL4`/`WHL5` `_H` mesh they have.
