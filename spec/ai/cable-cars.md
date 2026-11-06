# Cable cars (and the network subway trains)

San Francisco's cable cars run on the road network (`../formats/bai.md`): some roads carry a
cable-car line on a side, a polyline of one point per section. A cable car follows these lines
from road to road at up to 15 m/s, turns round at the line ends, stops for lights, stop signs and
vehicles ahead, and is a nearly unbreakable prop for everything that hits it. This page is the
whole cable car; the network subway trains, which work the same way on subway lines but run in no
shipped city, are summarised at the end. The path-set objects (boats, drawbridges, Underground
trains, ferries, parked cars) are `../game/level-objects.md`; the cable car's sounds
`../audio/world-sounds.md`.

Terms (road, side, section, outgoing side, exit intersection, centre distances CD, section axes,
tangents T, start and end directions S and E, Tan(*i*)) are those of `road-network.md`, "Terms".
**Cable line** of side *s* of a road: its *n* points C[0 … *n* − 1] in travel order (`bai.md`: side
A's line is stored reversed, so C[0] is where the side enters the road). CL(*a*, *b*) =
CD[*b*] − CD[*a*] (centre-line distance, one subtraction). Arithmetic as in
`../physics/vehicle.md`, "Precision"; *F* is the frame time.

## Which cable cars exist

When the road network is loaded (after the pedestrians, before the subway trains and the police)
and only if the session's cable-car switch is on (it is, except in network games:
`../game/multiplayer.md`):

1. For each intersection in index order: count its roads whose side leaving this intersection (the
   road's outgoing side here) has a cable line. If the count is exactly 1, that road and side get a
   **cable car**, numbered in this order.
2. Each car: model `va_cablecar_f` (`geometry/va_cablecar_f.pkg`, parts `BODY_H` … `BODY_VL`,
   `SHADOW`, `HLIGHT`) and prop data `tune/banger/va_cablecar_f.dgBangerData`
   (`tune/vehicle/va_cablecar_f.aiVehicleData` is not read). Its **extents** come from the box of the
   prop data's bound (the `.bnd` points moved by −CG, `../physics/collision-objects.md`): front =
   −min.z, back = max.z, left = −min.x, right = max.x (the car faces −Z).
3. Random draws, from the game-wide generator: while all cars are constructed, one per car (a lane
   offset that cable cars never use); then, per car in order, the **acceleration**
   *a*ᶜ = (*r* + *r*) + 1.5 with *r* a random fraction: between 1.5 and 3.5 m/s².
   Between the level objects' last draw (`../game/level-objects.md`, "Randomness") and these,
   MM2's generator makes (in Cruise) only the traffic pool's creation draws and then the
   pedestrian pool's: 2 per pool car (lateral offset, reaction delay; made for the whole pool of
   *N* even when traffic is off), then, if traffic is on and a car mix has entries, per pool car
   1 type draw plus, for a car that got a type, 3 more (two paint, acceleration-and-gap)
   (`traffic-population.md`, "The pool"); then 2 per pedestrian of the pool (`pedestrians.md`,
   "The pool"). **Engine:** continue the level objects' generator after its last draw, first
   drawing and discarding that many numbers for the session's settings (e.g. Cruise defaults:
   300 cars × 6 + 25 pedestrians × 2 = 1,850), then the cable cars' draws.

The police car paint rule depends on whether any cable car exists (`police.md`, "The cop's car").
Shipped: SF has 21 roads with cable lines on both sides and 4 cable cars, at intersections 47
(road 238, side B), 129 (road 134, side A), 149 (road 161, side A) and 157 (road 178, side A);
London has none.

**Partner.** After creation each car looks for a partner, the car of the line end it is heading
to: starting from its road *R* and side *s*, it repeats (at most 99,999 times): X = the exit
intersection of *R* for side *s*; walk X's roads in list order and, for each road whose outgoing
side at X has a cable line, remember that road and set *s* := that outgoing side; if exactly one
was found, the partner is the car whose start road is the remembered road, and the search ends;
otherwise *R* := the next road from (*R*, *s*) ("Next road"; *R* stays if there is none) and the
side that rule returns is thrown away. With the shipped lines the walk always comes back to the
car's own start, so every car is its own partner. The partner only matters while a car is knocked
loose ("Hit hard").

## Reset

At every AI reset (session start and race reset, after the props are reset), each car in order:

1. Its prop goes back untouched into its room; nothing is knocked loose any more.
2. Registration in the AI lists cleared ("Being seen"); active; not queued; no permission;
   speed *v* = 0, acceleration 0, target speed 0; look-ahead λ = 25.
3. Road and side = its start road and side; next road and side from "Next road".
4. Piece 1 of the road ("Pieces"), distance into the piece *e* = 0, distance along the road ε = 0.
5. Position = C[0] of the start line; then the pose ("Pose"); room = Find(position, current room)
   (`../physics/city-collision.md`).

## Each frame

In the AI update after the police and before the subway trains and the light sets
(`traffic.md`, "Frame order", step 5), each car in order:

1. If its prop has been knocked loose: see "Hit hard"; nothing else.
2. If it is active: the speed control ("Speed"). Then, only if *v* ≠ 0: advance along the pieces,
   pose, registration, and room = Find(position, current room) (moved there if different).

A car standing still is not re-posed.

## Pieces

The car follows a chain of cubic pieces in the ground plane, like an ambient car's lane pieces
(`road-network.md`, "Pieces along a lane", "Evaluating a piece (a moving car)": coefficients for x
and z, y is 0; R = 1/*L*; at *e* metres into a piece *u* = *e*·R), but built on the cable line of its
side, with no lane offset, *f* = the car's **front** extent, and piece lengths from the centre
line. For road *R* (*n* sections), side *s* and piece number *k*:

| Piece | *L* | P0 | P1 | T0 | T1 |
|---|---|---|---|---|---|
| first (*k* = 1) | CL(0, 1) on side B, CL(*n*−2, *n*−1) on side A | C[0] | C[1] | *L*·S | *L*·Tan(1) |
| middle (2 ≤ *k* ≤ *n* − 2) | CL(*i*−1, *i*), *i* = *k* (B) or *n* − *k* (A) | C[*k*−1] | C[*k*] | *L*·Tan(*k*−1) | *L*·Tan(*k*) |
| last (*k* = *n* − 1) | CL(*k*−1, *k*) − *f* (B), CL(0, 1) − *f* (A) | C[*k*−1] | Q | *L*·Tan(*k*−1) | *L*·E |
| turn (into the next road *R*′, side *s*′) | \|Q.x − C′[0].x\| + \|Q.z − C′[0].z\| | Q | C′[0] | *L*·E | *L*·S′ |

- Q = (C[*n*−1] − C[*n*−2])·*w* + C[*n*−2] per component, *w* = (CL(*n*−2, *n*−1) − *f*) /
  CL(*n*−2, *n*−1) — measured with the road's **last** section on both sides (for side A this is a
  quirk: keep it).
- C′ is the next road's cable line of side *s*′ and S′ its start direction; the turn piece's length
  is the flat Manhattan distance.
- A road of 2 sections has only a first piece followed by the turn (MM2 builds its pieces as for any
  road; no shipped cable road has fewer than 3 sections).

**Advancing**: *e* and ε grow by the step of "Speed". When *e* > *L* (strictly):

- On the road (not turning): *k* = *k* + 1, *e* = *e* − *L*. If there is no next road, or
  *k* ≤ *n* − 1, set up piece *k* (middle or last). Otherwise (past the last piece, with a next
  road) set up the turn piece; the car is now **turning**.
- Turning: the turn is done: not queued, no permission, λ = 25; if the **end 0** approach of the road
  just left is a stop sign, the car leaves the go group of **end 0's** intersection (whatever its
  side: for a side-A car this is the wrong end, a quirk; `traffic-control.md`, "The stop-sign
  queue"). The next road and side become current, the following next road is chosen ("Next road"),
  *e* = ε = *e* − *L*, *k* = 1, the first piece of the new road is set up, and the car is on the road.
- Then R = 1/*L* (one division).

Only one piece change happens per frame.

## Next road

From road *R* and side *s*: X = *R*'s exit intersection for *s* (end 0's intersection for side B,
end 1's for side A). Count X's roads that have a cable line on **side B** (MM2 tests side B only;
every shipped cable road has both). Then:

- 1 (a line end): the same road, side −*s* (the car turns round across the street).
- 2: walk X's road list from *R*'s slot onward (wrapping) and take the first other road with a line.
- 4: the same walk, taking the **second** road with a line found (straight across).
- any other count: none (the car's next road stays as it was; never with shipped data).

The new side is +1 if the new road's end 1 is X, else −1 (its outgoing side at X). Shipped SF: 4
intersections with 1 cable road, 17 with 2, 1 with 4. Two lines result: cars 0 and 1 run the
long line in opposite directions (26 roads out and back), cars 2 and 3 the short one (16).

## Speed

Per frame, with *d*ₑ the distance to the stop point of the road: CL(0, *n*−1) − (ε + front) while
on the road, 9999 while turning.

1. **Blocked?** ("Something ahead") gives yes with a distance *b*, or no.
2. If blocked: if the target speed is not 0: target = 0, acceleration =
   −((*v*·*v*) / ((*b* − 2.5) + (*b* − 2.5))).
3. Else if *d*ₑ ≥ λ, or *d*ₑ ≤ 0, or turning: if the target is below 15: acceleration = *a*ᶜ, target =
   15 + 0.0001.
4. Else (approaching the end of the road): if the car has no permission yet, it asks the
   intersection ("May a car enter?" below) and keeps a yes until its turn is done. Still no
   permission: if the target is 0, *v* = 0 when *v* < 0.2 and *d*ₑ < 0.5 (nothing otherwise); if the
   target is not 0: target = 0, acceleration = −((*v*·*v*) / ((*d*ₑ − 0.25) + (*d*ₑ − 0.25))), stopping
   0.25 m before the stop point. With permission: as step 3.
5. *v* = *F*·acceleration + *v*. If (*v* < target + 0.05 and acceleration < 0) or (target < *v* and
   acceleration > 0): acceleration = 0, *v* = target.
6. Step = *F*·*v*; *e* = step + *e*; ε = step + ε.

So a cable car speeds up at its own constant rate to 15.0001 m/s and brakes at the constant rate
that stops it exactly at the stop point or 2.5 m short of the obstacle (a blocked car that is
already closer than 2.5 m stops at once). The braking rate is fixed when braking starts and kept
while the target stays 0.

**May a car enter?** The rule of `traffic-control.md` for the car's approach (its exit end:
end 0 for side B, end 1 for side A): always green → yes; held → no; stop sign → only when
*v* < 0.5 and *d*ₑ < 1.5: then the car joins the queue of that intersection (once) and may go when
it is in the go group (a cable car never brings the other cars of its road into the go group);
traffic light → yes when the approach's light is in state 3 (green); yield → no; none → yes.

## Something ahead

Only while on the road (not turning) and before the last piece (*k* < *n* − 1); otherwise nothing
blocks. The test uses the **block distance** of `driver-planner.md` §17.1 from the cable car's
position *o* to a target point, with margin 30 and width 2 (so half width 2.0), against the other
vehicle's box and extents; a vehicle blocks when −1 < distance < 30. In order, the first that
blocks gives *b*:

1. Each player car (player order) whose body position p (centre of mass) is within 30 m flat
   ((o.z − p.z)² + (o.x − p.x)² < 900): target C[*k*] (the end of the current piece).
2. The vehicles registered on road *R* for side *s* at entry *k* (the lists of `traffic-reactions.md`,
   "Being seen by other AI": traffic cars and cable cars, front to back), except the car itself:
   target C[*k*].
3. If *L* − *e* < 30: the vehicles at entry *k* + 1, target C[*k* + 1].

Opponents, police and pedestrians are not looked at.

## Pose

The car's matrix (rows X, Y, Z, the car facing −Z; origin = model origin):

1. Curve point P and derivative D of the current piece at *u* = R·*e* (y = 0 for both), D normalised:
   *g* = 1/√((D.x² + D.z²) + D.y²) (0 if that is 0), d = D·*g*.
2. **Flat path**: if the current road and the next road both have road flag 8: X = (−d.z, 0, d.x),
   Y = (0, 1, 0), Z = (−d.x, −d.y, −d.z), origin = (P.x, C[0].y, P.z) with C[0] the first point of
   the current cable line.
3. Otherwise the **probe path** of `traffic-visuals.md` ("The solve", steps 7–11: corners A front
   left, B front right, C rear left from P and d with the car's extents; X = B − A, Z = C − A;
   origin = wF·Z + (wL·X + A); normalise X then Z; Y = Z × X), except that each corner is probed
   **once**, from (c.x, *y*ₘ + 5, c.z) down to (c.x, *y*ₘ − 5, c.z), with *y*ₘ the car's current
   origin y (before this pose), and a miss gives *y*ₘ.

No shipped cable road has flag 8, so SF's cable cars always use the probe path (they climb the
hills on the city surface, and on any placed object that wheels drive on).

## Being seen by other AI

After each move the car registers itself exactly as a traffic car does (`traffic-reactions.md`,
"Being seen by other AI", including its slip (a)), except that the component is found with
"Component at a position" (`road-network.md`, "Room lists"), not the road-preferring form, with the
car's own network-room memory as the hint (initially the first room of its start road). A
shortcut component is not handled (MM2 prints an error; none on cable lines). So ambient traffic,
other cable cars, and the police and opponent planner (class 1 obstacles, `driver-planner.md` §16)
see cable cars in the lists, with the cable car's extents.

## Hit hard

A cable car is an untouched breakable prop in its room's list (`../physics/collision-objects.md`;
`../physics/collision.md`, "Breakable props"): car bodies collide with it (wheels do not drive on
it), the trivial test uses its `YRadius` (4.36) cylinder, the bound is the prop data's geometry
bound (`CollisionPrim` absent = 0: `bound/va_cablecar_f_BOUND.bnd` moved by −CG), and
`ImpulseLimit2` = 7,575,744,000 (an impulse of about 87,000 N·s; only a heavy, fast vehicle reaches
it). Its velocity plays no part in impacts. Because the cable car's matrix origin is its model
origin on the ground and not its centre of gravity, the bound (centred on the CG) sits `CG.y`
(1.645 m) lower than the body: it reaches 1.68 m above the ground and 1.65 m below it (MM2's quirk;
car bodies still meet it).

When an impact breaks it, the cable car becomes one loose prop with its own data (mass
7,575.7 kg; the loose-prop page, `../ROADMAP.md` milestone 5) and its prop leaves the room. Each
frame after that, instead of moving: the partner is made inactive; then, if the road the car is on
is not an active traffic road (`traffic-population.md`: populated with traffic), the partner is
made active again, the cable car's prop comes back untouched where its AI state left it (the loose
body is removed) and it resumes on the next frame. A race reset restores it too.

## Drawing

As a moving object of its room by the rule of breakable props (`../formats/inst.md`, "Drawing";
level by the distance thresholds with the radius of its `BODY` meshes, 5.60 m): only its `BODY`
mesh of that level, material set 0, with its pose matrix. `SHADOW` and `HLIGHT` are loaded but
never drawn, so cable cars have no shadow and no lights at night.

## Network subway trains

`bai.md` subway lines (London: side B of 28 roads). Trains are created only if the session's subway
switch is on **and** the city aimap has a `[Subway]` entry (`../formats/aimap.md`: model name and
cars per train): every intersection where exactly one road has a subway line on its outgoing side
starts a train of that many cars, each a vehicle like a cable car (its own prop from the named
model, extents from its bound, acceleration (*r* + *r*) + 1.5) linked to the car before and after
it; they run in the AI update after the cable cars. The shipped `london.aimap` has the entry
commented out (`#[Subway]`, `#va_ug_l 3`), so **no network subway train exists in the retail game**;
their driving is not specified here (a mod that enables them would need it). London's visible
Underground trains are the path-set trains of `../game/level-objects.md`.

## Checked against

`tools/game/level_objects.py survey` (SF part; dirty side) builds the cable cars from `city/sf.bai`
by this page:

- Extents of `va_cablecar_f` (bound box − CG): front 4.3551, back 3.9492, left 1.1470, right 1.1274;
  `BODY` radius 5.597; CG (0, 1.6451, 0); the bound has 8 points (a box).
- 21 cable roads, none with road flag 8; intersections with 1 / 2 / 4 cable roads: 4 / 17 / 1.
- 4 cars, starting at (−996.4, 21.85, −503.7), (−841.1, 23.85, 546.0), (−1294.4, 69.85, 165.7),
  (−363.4, 7.85, 160.7) (the first cable point of each start line).
- Running free (no stops, *a*ᶜ = 2.5, *F* = 1/60) a car is back on its start road and side after
  208.5 s (cars 0 and 1, 26 roads, turning round at both line ends) or 129.8 s (cars 2 and 3,
  16 roads); car 0's route begins 238 B, 266 B, 106 A, 107 B, 201 B, 202 B, 378 A, …
- Partner walk: every car ends at its own start intersection after 2 or 3 steps.
