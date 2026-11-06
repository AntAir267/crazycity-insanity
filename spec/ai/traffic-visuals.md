# Traffic visuals: placing the car in the world, drawing it, its lights

Part of the ambient traffic spec (`traffic.md`). The rail (`traffic-driving.md`) gives a point and a
direction in the horizontal plane; this page turns that into the car's world matrix (height, pitch,
roll), then says how the car is drawn: parts, wheels, paint, shadow and lights. Arithmetic as in
`../physics/vehicle.md`, "Precision" (float32, one rounding per operation, grouping as written).

The car's matrix has rows X (right), Y (up), Z (back; the car faces −Z, `../formats/car-model.md`)
and a position (the model origin).

## Position and orientation

### When it is computed

- When a car starts on its rail (`traffic-driving.md`, "Starting a car on its rail"). Before that
  solve the position is preset so that the ground probes start near the right height: on a lane,
  the linear interpolation between lane points *k* − 1 and *k* at fraction *u* / ℓ (all three
  components, lane points without the lateral offset); in a crossing, the centre of the
  intersection the car is driving into.
- Every frame in which the driving car's speed is above 0, after advancing along the rail.
- A stopped driving car (speed ≤ 0) is not re-solved, except: while its "ground-solved" flag is 0,
  each frame, for each player whose car's model origin p is less than 100 m away horizontally
  ((car.z − p.z)² + (car.x − p.x)² < 10000, using the car's current position), solve once (once per
  such player). The flag is set by the probe path below and cleared by the other two paths.
- A rejoining car (state 3) every frame (`traffic-reactions.md`); an avoiding car uses the
  free-heading solve below; a knocked car takes its body's matrix.

### The solve

1. τ = *u* × (1/ℓ) (the stored reciprocal; no clamping).
2. Position on the curve and its derivative, per axis *c* ∈ {x, z} with the piece's stored
   coefficients (`road-network.md`, "Evaluating a piece (a moving car)": a₃ = a, a₂ = b, a₁ = c,
   a₀ = P0):
   P.c = ((τ × a₃ + a₂) × τ + a₁) × τ + a₀;
   D.c = (((τ × a₃) × 3) + (a₂ + a₂)) × τ + a₁. P.y = D.y = 0.
3. Direction d = D normalised: l² = (0 × 0 + D.z × D.z) + D.x × D.x; g = 1 / √l² (g = 0 if l² = 0);
   d = (g × D.x, 0, g × D.z).
4. **Flat-road path.** If the car's road has road flag 8 and either it has no next road, or the car
   is driving (state 0) and the next road also has flag 8:
   X = (−d.z, 0, d.x), Y = (0, 1, 0), Z = (−d.x, −0, −d.z); position = (P.x, *h*, P.z) with *h* = the
   height (y) of the **current** road's first centre-line point. Ground-solved flag = 0. Done.
   (Flag 8 marks roads that are flat to within 0.15 m in London and 0.24 m in SF: 469 of London's
   540 roads, 67 of SF's 379.)
5. **Is a player near?** Near = any player car whose body position (centre of mass,
   `../physics/vehicle.md`) is less than 100 m away horizontally from the car's **previous**
   position ((p.x − car.x)² + (p.z − car.z)² < 10000).
6. **Section path** (no player near, the car is not rejoining (state 3), its rail kind is not 2
   (changing lanes), its current room belongs to a **road** in the road network's room map
   (as found by the car's last obstacle update, not an intersection), and *k* < *n* − 1): the
   matrix rows are the road's section axes, ignoring the curve's direction:
   - side +1: X = −X(*k*), Y = Y(*k*), Z = Z(*k*) (section *k*, the end of the current segment);
   - side −1: X = X(*n* − *k*), Y = Y(*n* − *k*), Z = −Z(*n* − *k*) (the section of the segment's
     **start** point, an asymmetry of MM2);
   - position = (P.x, *h*, P.z), *h* = ((*P*(*k*).y − *P*(*k* − 1).y) × τ) + *P*(*k* − 1).y with the
     lane points of the current lane.
   Ground-solved flag = 0. Done. The car's heading jumps at each lane point; nobody is near enough
   to see it.
7. **Probe path.** With Xx = −d.z, Xz = d.x and the car's bumper distances front, back and side
   distances left, right (`../formats/aivehicledata.md`), three corner points in the horizontal
   plane:
   - A (front left): A.x = ((P.x − left × Xx) + front × d.x), A.z = ((P.z − left × Xz) + front × d.z)
   - B (front right): B.x = ((right × Xx + P.x) + front × d.x), B.z = ((right × Xz + P.z) + front × d.z)
   - C (rear left): C.x = ((P.x − left × Xx) − back × d.x), C.z = ((P.z − left × Xz) − back × d.z)
   For each corner, with *y*ₚ = the car's previous position y: for *i* = 1, 2, 3: probe the ground
   from (cx, *i* × 2.5 + *y*ₚ, cz) down to (cx, *y*ₚ − *i* × 2.5, cz) (`../physics/collision.md`,
   "Ground queries": Probe with the "ground" mask, no object ignored; each corner keeps its own
   probe state for the car's lifetime); at the first hit the corner's y = the hit point's y and the
   search stops. If all three miss, the corner's y = *y*ₚ.
8. X = B − A, Z = C − A (all three components).
9. wF = front / (back + front), wL = left / (right + left); position.c = (wL × X.c + A.c) + wF × Z.c
   for c = x, y, z (with the unnormalised X and Z).
10. Normalise X, then Z: l² = (x × x + y × y) + z × z, g = 1 / √l² (0 if l² = 0), each component × g.
11. Y = Z × X: Y.x = Z.y × X.z − Z.z × X.y, Y.y = X.x × Z.z − Z.x × X.z, Y.z = Z.x × X.y − X.x × Z.y.
    Y is not normalised and X, Z are not made perpendicular: on ground that both pitches and rolls
    the matrix is slightly sheared. Ground-solved flag = 1.

There is no smoothing, no limit and no suspension: each solve starts from scratch, and the body
sits rigidly on the three probed corners. Ground probes hit the city surfaces and placed objects
flagged as ground (bridges, ferries, ramps), never cars.

### The free-heading solve

Used while a car avoids a player (`traffic-reactions.md`), when it moves by its own heading instead
of the rail: the same three corners, but from the current matrix (d = −Z row, X = X row, P = the
current position, all three components): A = (P − left × X) + front × d, B = (right × X + P) +
front × d, C = (P − left × X) − back × d. One probe per corner from (c.x, c.y + 5, c.z) to
(c.x, c.y − 5, c.z); a miss gives the corner the car's current position y. Then steps 8–11
(position = wF × Z.c + (wL × X.c + A.c), the same value). Ground-solved flag = 1.

### Room

After each update in which the car's speed is not 0 (and after starting on the rail), the car's
room = Find(position + Y row, its room) (`../physics/city-collision.md`, "Finding the room of a
point": the point one Y-row length above the origin), and the car is moved to that room for
drawing. A car in the free list is in room 0 (not drawn).

## Drawing

### Parts and levels

A traffic car is drawn from `geometry/<name>.pkg` with the parts of `../formats/car-model.md`,
"Traffic cars" (only these: `BODY`, `SHADOW`, `HLIGHT`, `TLIGHT`, `SLIGHT0`, `SLIGHT1`,
`WHL0`–`WHL3`, `BREAK0`–`BREAK3`, `HEADLIGHT0`, `HEADLIGHT1`, `WHL4`, `WHL5`; a car lacking `BODY` or
any of `WHL0`–`WHL3` cannot be used), always with its own paint set (below).

- **Which cars**: a traffic car is a moving object of its room: it is drawn only if its room's
  distance is at most the room limit **and** its own depth is at most the room limit (the rule of
  breakable props, `../formats/inst.md`, "Drawing"), and it is in view.
- **Level** (VL, L, M, H) by the rule of `../formats/inst.md` with the car's **body** radius (largest
  over its `BODY` levels) and the depth of the point **one Y-row length above** the car's origin
  (position + Y row), not of the origin itself.
- **At every level**: the `BODY` mesh of that level, then every attached breakable part with its
  `H` mesh at its pivot (none of the shipped traffic cars has breakable parts).
- **At H only**, after that: the reflection pass over `BODY_H` (`car-model.md`, "Reflection pass";
  body only), then the wheels.
- Below H no wheels are drawn. (`WHL*_M`/`_L` meshes are never used.)

### Wheels (H only)

- One spin matrix for all wheels, from the car's wheel angle α: rows (1, 0, 0), (0, cos α, −sin α),
  (0, sin α, cos α) (MM2 row-vector convention: wheel point × spin × car matrix), so the top of the
  wheel moves toward the car's front as α grows. No steering, no hub caps, no fenders.
- Each wheel `WHL0`…`WHL3` at its pivot (`../formats/aivehicledata.md`, "Wheel positions"); then
  `WHL4` at its pivot if the car has a `WHL4_H` mesh, and then `WHL5` if it also has a `WHL5_H` mesh
  (`WHL5` is never drawn without `WHL4`). Only `va_bus_f` has six wheels.
- **Wheel angle** α: starts at 0 when the car is placed; after every update of a traffic
  car, in any state (`traffic.md`, "One traffic car update"): α = (*F* × *v*) + α, then if α > 6.28,
  α = α − 6.28 (once). That is one radian per metre whatever the wheel's size (a real wheel of
  0.3 m radius would turn 3.3 times faster). A knocked car's wheels: `traffic-reactions.md`.

### Paint

The car's paint set index is drawn twice when the pool is created (`traffic-population.md`, "The
pool"; the second value is kept): *p* = trunc(rnd() × (*n* − 1)), *n* = the number of material sets of
the car's package (`car-model.md`, "Paint"). So the last set is never used and a type with one or two
sets always uses set 0. A pool car keeps its type and paint for the whole session, however often it
is placed.

### Shadow

In the shadow pass (`car-model.md`, "Shadow": every car in a room closer than the room limit, whether
in view or not), only if the car's ground-solved flag is 1 (its last solve used the probe path,
"The solve" step 7; so cars that nobody is near, and cars on flat-flagged roads, have **no**
shadow): the `SHADOW` part's H slot (after the fill rule) with the car's paint, alpha-blended, drawn
with the **car's own matrix** (the mesh lies under the car). It is projected onto the ground (as for
other cars, renderer spec) only when the car is knocked (physical) or upside down (its Y row's y < 0).

## Lights

Drawn in the glow pass (`../formats/night.md`, section 4: additive, no fog, depth test without
writes) for **every** traffic car in a room closer than the room limit, whether or not the car is in
view and at any level. All glow meshes use their H slot after the fill rule (the shipped glow parts
only have `_L` meshes) with the car's paint set and the car's matrix.

| Part / glow | Drawn when |
|---|---|
| `TLIGHT` | once if the car's acceleration *a* < 0 (braking) **or** its speed is exactly 0; and once more if headlights are on. Braking or standing at night therefore draws it twice (double brightness, additive) |
| `SLIGHT0` (left lamps, at −x) | indicator bit 0 set (values 1 and 3) and the blink is on |
| `SLIGHT1` (right lamps, at +x) | indicator bit 1 set (values 2 and 3) and the blink is on |
| headlight glows | headlights on |
| `HLIGHT` | never |

- **Headlights on**: time of day evening or night, or fog (`night.md`), for the whole session.
- **Blink**: on while (*k* + trunc(16 × *t*)) & 8 ≠ 0. *t* = a clock that adds *F* every frame
  (MM2 starts it when the program starts and never resets it; **engine:** start it at 0 with the
  session); *k* = a fixed per-car number 0 … 15 (MM2 derives it from the car's memory address;
  **engine:** car index × 7 mod 16, or any fixed choice). One second period, half on.
- **Indicator values**: 1 = left, 2 = right, 3 = both (hazard lights; set when the car is knocked,
  `traffic-reactions.md`). Set by the driving rules before a turn (`traffic-driving.md`, "The speed
  decision", step 5) and at the start of a lane change; cleared when the turn piece or the lane
  change ends and when a knocked car rejoins traffic. A pool car's indicator is **not** cleared when
  it is placed again (MM2 keeps the last value; it is cleared again at its next turn or lane change).
- **Headlight glows** (as `night.md` describes for traffic): two billboards, at the `HEADLIGHT0`
  pivot and at the same point with x negated (the second only if the car has a `HEADLIGHT1` mesh;
  a car without a `HEADLIGHT0` mesh has none). Both are moved by the same offset 0.2 m toward the
  camera, along the direction from the **car's origin** to the camera. Size and colour as `night.md`
  (half-size c^1.5 m from the angle between the car's forward axis and the direction to the camera,
  colour 0.95 × white, texture `lt_glow`).

## Checked against

- `traffic.md` lists the per-frame order these steps sit in. Shipped traffic packages
  (`work/data`, 20 types): all have `BODY` VL/L/M/H, `SHADOW` M/H, `HLIGHT`, `TLIGHT`, `SLIGHT0`,
  `SLIGHT1` only at L, `WHL0`–`WHL3` L/M/H, `HEADLIGHT0`/`1` at H; none has `BREAK` parts; only
  `va_bus_f` has `WHL4`/`WHL5`. `SLIGHT0` meshes sit at −x (left), `SLIGHT1` at +x; the `HEADLIGHT0`
  pivot is at +x (right headlight).
- Flat-flagged roads (flag 8): London 469 of 540 (centre-line height range at most 0.15 m),
  SF 67 of 379 (at most 0.24 m); no road without the flag is flat (up to 18 m of rise in London,
  52 m in SF). So most London traffic keeps a constant height per road and, never probed, draws
  no shadow even close to the player (code reading; worth confirming in the game).
