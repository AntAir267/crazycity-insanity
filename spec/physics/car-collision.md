# Car against car, and how computer-driven vehicles move physically

This page says how MM2's physics treats several vehicles at once: which bodies take part in a
physics sample and in what order, how two vehicles find their contacts and push each other apart,
and how the computer's vehicles are simulated: opponents and police cars are full cars, while
ambient traffic cars follow their lanes without physics until something hits them and then become
simple rigid bodies for a while.

Companion pages:
- `car-collision-boxes.md` — contacts between two box-shaped bounds (computer car against computer
  car).
- `collision.md` — the physics sample, materials, the impact record and the collision response,
  which this page reuses unchanged for two moving bodies.
- `collision-object-contacts.md` — contacts between a car's polygon bound and a polygonal object;
  section 2 there is the contact method for every pair of vehicles in which at least one bound is
  not a box (below, "Polygonal pairs").
- `vehicle.md` and its pages — the car simulation, used unchanged by opponents and police.

Who decides where traffic and police *want* to go is not here: the traffic and police specs
(`../engine/traffic.md`) own lane following, the moments a traffic car is driven out of its lane,
and the controls the police give their cars. This page owns everything from the moment two
vehicles touch, and the physical model of every computer vehicle.

Single precision throughout, every operation rounded to float32, evaluated in the order written
(`vehicle.md`, "Precision"). Sums of three terms are written with their grouping.

## 1. Moving bodies ("movers")

### 1.1 The mover list

The physics keeps a list of up to 32 **movers**: the objects that take part in the physics samples
of the current rendered frame. The list is emptied at the end of every frame's physics and refilled
during the next frame's game update, so its order is the order in which the game update meets the
vehicles. Each mover has four switches: **U** (it updates its own physics in each sample),
**C** (its bound is tested against the city, `collision-contacts.md`), **V** (it is tested against
the other V movers) and **O** (it gathers placed objects and is tested against them,
`collision-objects.md`, "Gathering"). An object declared twice in a frame keeps its first place in
the list; its switches are combined.

Order of declaration in a frame, with switches:

| # | Mover | Switches |
|---|---|---|
| 1 | the player's car | U C V O |
| 2 | the player's trailer (`trailer.md`; `vpsemi`, `vpftruck`, `vplafrance`) | U C V O |
| 3 | traffic cars without a body that their lane logic has put in the *avoiding the player* or *getting back into the lane* states (`../engine/traffic.md`), in the order the traffic update visits them | C O |
| 3 | traffic cars without a body that were left *wrecked* (6.5), in the same visiting order | O |
| 4 | race opponents (other modes; at any distance, `../ai/opponents.md`) | U C V O |
| 5 | police cars closer than 250 m to the player (distance below; farther ones are not movers at all) | U C V O |
| 6 | physical traffic cars, in the order of the body pool's list (4.2) | U C V O |
| 7 | loose bodies (broken props, their pieces, thrown car parts), in their pool's list order (`loose-bodies.md`, 5.1) | by their prop data: U C V O, or U C |

The distance test for a police car: with Δ = (its rigid-body position) − (the player car's
rigid-body position), d² = (Δz·Δz + Δy·Δy) + Δx·Δx (the smallest over all players, starting from
9999999 and replacing on strictly smaller), compared with 40000 (strictly below: within 200 m) and
62500 (strictly below: within 250 m). Within
250 m it is a mover with all four switches (MM2 distinguishes 200 m and 250 m with switch sets that
behave identically). At 250 m or more it is **not a mover in that frame**: it does not update, does
not move and is not collided with — it stays frozen until the player comes back within 250 m. (In
modes with several players the nearest player counts.)

A traffic car on its lane (no body, not in one of the states of row 3) is not a mover: it is a
placed object with a box bound that movers with the O switch gather like a prop (2.3).

### 1.2 One physics sample with several movers

This replaces the single-car order of `collision.md` ("The physics step"); for a lone car it is the
same.

1. **Update.** Every mover with switch U, in list order, runs its own physics update: a car runs
   `vehicle.md` "One physics step" steps 1–11 (and its room update); a physical traffic car runs
   6.3.
2. **Gather.** Every mover with switch C or O gathers placed objects (`collision-objects.md`,
   "Gathering"; the room, reference point and radius are the mover's, 2.2), at most 32: landmarks
   and spanning objects if it has any of switches C, O and V, props, moved props lying still
   (`loose-bodies.md`, 5.4) and traffic cars without a body (2.3) if it has switch O or V.
3. **Collide.** For each mover i in list order:
   a. if it has switch C: its bound against the city (`collision-contacts.md`);
   b. if it has switch V: against every later mover j (j > i) that also has switch V, unless the
      two are a car and its own trailer, and only if the broad test (2.1) passes for the pair
      (i, j): the pair test of 3, A = mover i, B = mover j;
   c. if it has switch C or O: against each object it gathered in step 2, in gathering order (a
      gathered traffic car without a body is tested as in 4.1).
   Each test's impacts are resolved at once (3.4).
4. A traffic car that became physical during step 3 (4.1), and a loose body made during the
   sample (`loose-bodies.md`, 5.3), is appended to the mover list with no switches for the rest of
   this sample; at the end of the sample it gets all four switches.
5. **End.** Every mover with switch U, in list order, applies its push and keeps its previous
   matrix and push source (3.3); a car as in `vehicle.md` step 13; a traffic body as in 6.3 step 6.
   (A traffic car appended in step 4 takes part in this step.)

The flat world of the check runs has no rooms: there every traffic car without a body is gathered
by every mover that gathers, in run-file order (`../engine/drive-check.md`).

## 2. Broad tests

### 2.1 Mover against mover

The pair (A, B) is tested only if their spheres touch: with centre c and radius r of each,
d = c_A − c_B, |d|² = (d.z·d.z + d.y·d.y) + d.x·d.x, tested only if |d|² ≤ (r_B + r_A)·(r_B + r_A)
(a NaN fails). This is the trivial test of `collision-objects.md` ("Gathering", step 4) without
the prop cylinder rule.

### 2.2 Centres and radii

| Vehicle | Centre c | Radius r |
|---|---|---|
| car (player, opponent, police) | rigid-body position + body Y axis row (per component) | radius of its `BODY` part (`collision-objects.md`, "Gathering", step 1) |
| traffic car, with or without a body | origin of its matrix + its Y axis row | radius of the `BODY` part of its model (`geometry/<type>.pkg`, same rule) |

The traffic car's matrix is its lane matrix while it has no body and its body frame while it has
one (6.3). The car's room for gathering is Find(centre, previous room) as for the player
(`collision-objects.md`); a physical traffic car updates its room from its centre once per sample
(6.3 step 5) and once more after the frame (6.5).

### 2.3 A traffic car as a gathered object

A traffic car without a body is gathered by movers with switch O or V like a prop: it is listed in the
room of its centre and passes the trivial test with its centre and radius from 2.2. Like every
non-static object it is filed at the **front** of its room's list when it is created and whenever
it moves into another room (also while it is physical, when it is not gathered; releasing it does
not move it in the list) (`collision-objects.md`, "Room lists"). So a mover gathers it before the
props and static objects of that room and before any moving object that entered the room earlier,
within the limit of 32. (The flat world of the check runs uses run-file order instead, 1.2.) A traffic car
with a body is never gathered (it is a mover). Cars (player, opponents, police) are never gathered:
cars meet cars only through 1.2 step 3b.

## 3. The pair test

### 3.1 Bounds

| Vehicle | Bound | Placed with |
|---|---|---|
| player's car | its polygon bound `bound/<car>_BOUND.bnd` (`collision.md`, "The car's bound") | its model frame (`vehicle.md`, step 5) |
| opponent or police car | a **box**: the box of the same `.bnd` file (`bnd.md` item 6), size = max − min, centre offset = size·0.5 + min (per component, in this form) | its model frame |
| traffic car | a **box** of size `Size` with centre offset `CG` from its `.aiVehicleData` (6.1) | its lane matrix, or its body frame once physical |

A box with size S and offset c has the points, edges, quads, edge normals and edge "cosines" of
`collision-object-contacts.md` 4.1 with every point moved by c: point k = t_k·S + c per component
(t_k = ±0.5, so the product is exact and the sum rounds once). Its polygon normals and inward edge
normals are computed from these points (4.1's formulas). Its box min = c + (−h) and max = c + h
with h = S·0.5, and its **radius is √((h.x·h.x + h.y·h.y) + h.z·h.z), measured about the bound's
origin, not about c** — the cull sphere of `collision-object-contacts.md` 1.3 stays centred on the
model origin.

One bound object exists per model name: the first vehicle of a model to be set up decides whether
that model gets the polygon bound (player) or the box (computer car). So if the player drives the
same model as a police car, the police car collides with the player's polygon bound, and the other
way round if a computer car of that model was set up first. (MM2 sets up the player first.)

### 3.2 Which contact method

- Both bounds are boxes (computer car against computer car, traffic against traffic):
  `car-collision-boxes.md`.
- Otherwise (at least one polygon bound): **polygonal pairs**, below.

**Polygonal pairs.** `collision-object-contacts.md` section 2 run with A = the earlier mover and
B = the later mover (or the gathered object), with these changes because B moves too:

1. The object matrix O is B's current matrix and B has its own previous matrix O₀ (3.3). The
   separating test (1.1 there) uses the current matrices of both.
2. The previous relative matrices are built from both previous matrices: car pass
   last = M₀·inv(O₀), object pass last = O₀·inv(M₀) (matrix product and transpose inverse as
   defined there).
3. The displacement of a point x of B is computed with B's own matrices exactly as for the car:
   d_B(x) = displacement of x between O₀ and O (`collision-contacts.md`, "Terms"), and its velocity
   v_B(x) = d_B(x)·(1/dt) per component; likewise d_A, v_A with M and M₀.
4. Corner groups (2.5 step 3 there): car-list groups use vel = v_B(e) − v_A(e), object-list groups
   use vel = v_A(e) − v_B(e), per component, with e = the end point of the first record of that
   list.
5. Single-list pass (2.5 step 6 there): vel = (d_mover(x) − d_owner(x))·(1/dt) per component, the
   subtraction first, x = the record's point; for the car list the mover is A and the owner B, for
   the object list the other way round.
6. Two-list pass (2.5 step 7.4 there): rv = (d_A(ptP) − d_B(ptQ))·(1/dt), as written there.
7. Materials (2.5 step 11 there): side A names A's bound material, side B names B's.

With B fixed (O₀ = O, so every d_B is exactly +0) these rules give bit for bit the fixed-object
results of that page; a traffic car without a body is tested exactly so (4.1).

Every impact of a polygonal pair has A = the earlier mover, B = the other vehicle, and its normal
points out of B into A (`collision.md`, "The impact record"). A box pair files some impacts the
other way round (`car-collision-boxes.md`, 7: impacts of its second face pass have the later mover
as their first side and their normal out of the earlier mover into the later one); such an impact
is resolved with its own sides (3.4).

### 3.3 Previous matrices and push sources

Each side's previous matrix is:

| Side | Previous matrix |
|---|---|
| a car | the model frame kept at the end of the previous sample, before its push was applied (`vehicle-body.md`, "Contact interface") |
| a physical traffic car | its body frame at the end of the previous sample, **after** its push was applied (6.3 step 6) |
| a traffic car without a body | its current matrix (it has no previous one) |
| a loose body | its body frame at the end of the previous sample, after its push (`loose-bodies.md`, 6.4) |

Before the test, a side's previous matrix origin is moved by that side's `lastPush`
(`vehicle-body.md`) when the largest push it received in the previous sample came from the other
side of this test. Sides are told apart by identities (`collision-object-contacts.md`, "Push
source and the previous matrix"). The identity that counts is the one the side has **when the
impact is applied** (3.4), and the identity used for the shift is the one it has when the test
starts:

- every car and every physical traffic car has its own;
- the city has one;
- landmarks and spanning objects, which never get a body, share one ("fixed objects");
- a breakable prop, and a traffic car without a body, are handed a body of their own before the
  response of a test that touches them (`collision-object-contacts.md` 5.10 for props; 4.1 here),
  so pushes from them are booked under that body's own identity, never under the shared one
  (for a prop the identity belongs to the pool body it is handed, `loose-bodies.md` 2.2, so
  consecutive props can share one).
  While such a side is being *tested* without a body it is the shared fixed-object stand-in: the
  other side's previous matrix is shifted for that test only if the other side's largest push of
  the previous sample came from a landmark or spanning object.
- (A traffic car without a body that is itself the first side of a test, row 3 of 1.1, is tested
  through a second shared stand-in; it too gets its body before the response.)

So after hitting a parked traffic car, the player's car books that push under the traffic car's
new body; in the next sample the two are a pair of movers (3.5) and the player's previous matrix
is shifted for that pair test if that push was its largest. And a physical traffic car's previous
matrix, which already contains its push, is shifted once more when the push came from the side it
is tested against (keep this).

### 3.4 Response

The impacts of one test (N of them) are resolved in list order with weight 1/N by
`collision.md` "Resolving one impact", which already covers two rigid bodies: vrel = (point
velocity of A) − (point velocity of B); K = K_A + K_B (element by element, K_A first); the push
shares from (n·K_A, n·K_B); impulse J on A and −J on B. The two bodies are the vehicles' rigid
bodies (a car's `vehicle-body.md` body, a traffic car's body of 6.3).

**Materials.** Friction μ = f_B·f_A and elasticity e = e_B·e_A (taken as 1 unless e_B·e_A < 1;
a NaN gives 1), with:

| Bound | friction | elasticity |
|---|---|---|
| player's car | `BoundFriction` | `BoundElasticity` (`collision.md`, "The car's bound") |
| opponent / police box | that car's `BoundFriction` | that car's `BoundElasticity` (re-applied at every reset, as for the player) |
| traffic box | 1.0 | 0.5 |

The traffic box keeps the built-in default material values: the `Friction` and `Elasticity` keys
of `.aiVehicleData` are read but nothing in MM2 applies them. A model's one bound has one material
object: if two cars share a model's bound (3.1) the values of the car set up or reset last apply to
both.

**Applying.** For each impact, with A and B the impact's own first and second side (for a box
pair possibly the later mover first, 3.2): first side A (impulse J at P, push pushA with the merge
rule, then A's push-source record with B's identity, then A's impact report), then side B (−J,
pushB, A's identity, B's report). Impact reports: a car's report wakes its unstuck helper
(`vehicle-helpers.md`) and is then offered to its damage (`damage.md`); a traffic body's report
has no physical effect (it plays an impact sound scaled by |J.x| + |J.y| + |J.z| and starts a
particle effect when |J| > 100 — not physics). A side without a body ignores impulses and pushes;
but see 4.1: a traffic car of a test is made physical before the test's response.

### 3.5 The order of everything, once more

For mover i: city test and its responses; then for each later mover j (list order) the broad test,
the pair test and its responses; then each gathered object's test and responses. A traffic car
made physical in i's tests is not tested again in this sample except as a gathered object of a
later mover that had gathered it before (it is then tested as a body: its current matrix and the
previous matrix it got when it became physical, which is the same matrix).

## 4. When a traffic car becomes physical

### 4.1 The rule

A traffic car without a body becomes physical **at the first collision test that produces at
least one impact and in which it is a side**. Such tests are:

- a mover's test against it as a gathered object (1.2 step 3c) — the usual case: the player, a
  police car or a physical traffic car runs into it;
- while it is itself a mover of row 3 of 1.1: its own city test (1.2 step 3a), and its tests
  against the objects it gathered (props, other traffic cars without a body: then both become
  physical).

Wheel probes never make it physical, and there is no speed or impulse threshold: a single touching
impact is enough. Landmarks and spanning objects never become physical.

The contacts of that test are computed with the traffic car as a fixed shape (its current matrix
as its previous matrix, 3.3). Then, **before the response**, the car gets its body (4.2) and every
impact of the test now names the new body as that side (the normal is not changed). The response
(3.4) then uses the body's velocity and inverse-mass matrix: the traffic car takes its share of
the impulses and of the push in this very sample.

If a mover had gathered the car and an earlier mover's test made it physical in this sample, the
later mover still tests it, now as a body (3.5).

### 4.2 Taking a body

There are 32 traffic bodies. Taking one:

1. If fewer than 32 are in use, the next free body is taken and the car is added at the end of
   the pool's list. If all 32 are in use, the car at the **first** place of the list is released
   (6.5 steps 1–4, including the test whether it goes back to its lane; it stops being a mover at
   once) and the new car takes its body and its first place in the list.
2. The car stops being a gathered object (it is a mover from now on).
3. The body is reset (`vehicle-body.md` "State": everything zero, rotation identity), then:
   rotation rows and position = the car's lane matrix (its rows and origin: the **model origin**,
   so the body's centre of mass is the model origin, not the `CG` point); mass and inertia =
   `Mass` and the box inertia of `Size` (`vehicle-body.md`, "Mass and inertia", with
   (bx, by, bz) = `Size`); speed limit 500 m/s; angular speed limit **5 rad/s** per body axis
   (not the cars' 4π).
4. Velocity: with s = −(the car's current lane speed, m/s), v = (s·Z.x, s·Z.y, s·Z.z) where Z is
   the lane matrix's Z row (so v = speed × forward); momentum P = (m·v.x, m·v.y, m·v.z). Angular
   velocity and angular momentum 0.
5. The rest detector is reset (6.4), the four wheels are set up (6.2) and reset (compression 0, both
   deflections 0).
6. The previous matrix is the current matrix; push source: none.
7. The lane logic is told: the car's lane speed becomes 0 and it enters the *crashed* state
   (`../engine/traffic.md`). (MM2 reads the lane speed in step 4 before this.)

## 5. Opponents and police

Opponents and police cars are **full cars**: the same simulation as the player's car (`vehicle.md`
and all its pages: engine, gearbox, drive trains, wheels with probes and tyres, aerodynamics,
anti-roll bars, gyro, unstuck helper, water effect, the world collision of 1.2), with these
differences:

1. Tune files: `tune/vehicle/<car>.vehCarSim`, `.vehGyro`, `.vehStuck` of their own model. The
   player-only exception of `vehicle-tune.md` (a player's `vpcop` set up from `vpmustang99`) does
   **not** apply: a police `vpcop` uses `vpcop.vehCarSim`.
2. Bound: the box of 3.1 (unless the model's bound already exists, 3.1), with the car's own
   `BoundFriction` and `BoundElasticity`.
3. Unstuck helper: after setting up, MM2 overwrites three of its values for computer cars:
   `Rotation` = 0 (so a stuck computer car is lifted, never rolled), `TimeThresh` = 0.5, and the
   squared position threshold (`P2` of `vehicle-helpers.md`) = 1.0 (i.e. `PosThresh` 1 m); `M2`
   stays the file's. Police cars then get `TimeThresh` = 0.75 instead of 0.5. The throttle scale of
   the helper's turn condition (`vehicle-helpers.md`, turnWanted: throttle scale × 0.75)
   is, for computer cars, the computer driver's throttle scale, written every frame (`../ai/driver.md`; 1.0 for police
   cars); it has no other use.
4. Controls: the computer sets the car inputs directly — throttle, brake, steering, handbrake — as
   float values, every frame before the physics, and selects gears through "set gear"
   (`vehicle-powertrain.md`) while the automatic gearbox stays on. None of the player's control
   step applies (no quantisation, no throttle/brake swap, no automatic reverse, no standstill
   handbrake). What values the police choose is the police spec's. While the car is wrecked
   (`damage.md`) the computer driver gives throttle 0, brake 0 and steering 0 every frame. The
   driver also writes the car's `CarFrictionHandling` (`vehicle-wheels.md`, "Surface") every
   frame: 2.0 if the car is marked as having touched the player's car, else 1.0.
   **Touched the player**: a mark on a vehicle that is set whenever a collision test between the
   player's car and that vehicle (either side) produces at least one impact, and cleared for every
   mover at the start of each frame's physics, before the first sample. So the driver sees the
   mark from the previous frame's physics.
5. Mover rules: 1.1 (the police distance rule).
6. The placement and reset rules are the police spec's (MM2 resets a police car that falls below
   y = −200).
7. Police catch-up: once per frame, before the physics, if a police car's driver commands
   throttle exactly 1.0 and the car's speed (`vehicle.md` step 2, m/s) is below 50, its rigid
   body's linear momentum is multiplied by 1.03 (each component P.c = P.c·1.03; the velocity
   follows at the next integration). When the police logic is in that mode is the police spec's.

Everything else — every formula and order of `vehicle.md` — is the same as for the player's car.

## 6. Physical traffic cars

### 6.1 Data

`tune/vehicle/<type>.aiVehicleData` (same text syntax as the other tune files, `vehicle-tune.md`
"Syntax", root name not checked), keys (case-sensitive):

| Key | Type | Used for |
|---|---|---|
| `Mass` | float | body mass (kg) |
| `Size` | vector | box size (m) and the box inertia |
| `CG` | vector | box centre offset from the model origin |
| `Spring` | float | wheel spring rate |
| `Damping` | float | wheel damping |
| `Limit` | float | wheel travel |
| `RubberSpring` | float | tyre deflection spring |
| `RubberDamp` | float | tyre deflection damping |
| `MaxAng`, `Elasticity`, `Friction`, `MaxDamage`, `PtxThresh` | | read but without physical effect |

A key missing from the file leaves its value as it was before the file was read, which MM2 never
initialises; treat it as 0 (three shipped types lack `CG`: `va_cablecar_f`, `va_garbagetruck`,
`va_ug_l`). From the model's geometry: wheel pivots `geometry/<type>_WHL0.mtx` … `_WHL3.mtx`
(origin, floats 9–11 of the `.mtx`, as for cars) and the wheel radius = |(max.y − min.y)·0.5| of
`WHL0` (floats 4 and 1), used for all four wheels. Types with six wheel files (`va_bus_f`) still
use only the first four. A type is set up once and shared by every car of that type.

### 6.2 The four wheels

One per pivot WHL0 (front left), WHL1 (front right), WHL2 (back left), WHL3 (back right). Each has:
offset o (its pivot), radius r, `Spring` k, `Damping` c, `Limit` l, `RubberSpring` K, `RubberDamp`
D, a static load L = (m·(−19.6))·(−0.25) (m = the body mass; a quarter of the weight), and state:
compression `comp`, lateral deflection `dLat`, longitudinal deflection `dLong`, and a probe state
(room hints kept from the previous use of the same body slot; **no polygon cache**: a cached
polygon is never used).

Each sample (6.3 step 4), with the body's rotation rows X, Y, Z, position p, velocity v and angular
velocity w (all after this sample's integration), dt and 1/dt:

1. Wheel centre: C.c = ((Z.c·o.z + X.c·o.x) + Y.c·o.y) + p.c for c = x, y, z.
2. h = r + l. Probe segment: S = (h·Y) + C, E = C − (h·Y) (per component, h·Y.c first).
3. Probe S → E (`collision.md`, "The query") with the ground mask, ignoring nothing, with this
   wheel's probe state. If there is no hit, or the hit normal n has
   √((n.x·n.x + n.y·n.y) + n.z·n.z) = 0: `comp` = −l, `dLat` = `dLong` = 0, and the wheel adds
   nothing. Stop.
4. With the hit point P, normal n and fraction T:
   - r⃗ = P − p; point velocity u = q + v (per component q.c + v.c) with
     q = (r⃗.z·w.y − r⃗.y·w.z, r⃗.x·w.z − r⃗.z·w.x, r⃗.y·w.x − r⃗.x·w.y);
   - vn = (u.z·n.z + u.y·n.y) + u.x·n.x; tangential velocity t = u − n·vn (per component
     u.c − vn·n.c);
   - D = S − E, len = √((D.z·D.z + D.y·D.y) + D.x·D.x).
5. Suspension: old = `comp`; `comp` = ((r + r) + l) − len·T; rate = (`comp` − old)·(1/dt), then
   clamped: if not (rate ≥ −3) rate = −3 (a NaN becomes −3), else if rate > 3 rate = 3.
   F = (`comp`·k + rate·c) + L; if not (F ≥ 0), F = 0.
   (With the car at rest F = L at `comp` = 0: the wheel centre then rests one radius above the
   ground.)
6. Normal force Fn = ((n.z·Y.z + n.y·Y.y) + n.x·Y.x)·F; force a = (Fn·n.x + 0, 0 + Fn·n.y,
   0 + Fn·n.z).
7. Slip speeds: sLat = (t.x·X.x + X.z·t.z) + X.y·t.y; with f = −Z (each component negated),
   sLong = (f.z·t.z + f.y·t.y) + f.x·t.x.
8. Deflection limit: m = ((Fn·g)·0.4)/K, g = the weather friction scale (`vehicle-wheels.md`,
   1.0 in dry weather; the surface material plays no part). Targets: tLat = sgn(sLat)·m,
   tLong = sgn(sLong)·m, with sgn(x) = 1 if x > 0, −1 if x < 0 or x is NaN, else 0.
9. Deflections, for (d, s, τ) = (`dLat`, sLat, tLat) and then (`dLong`, sLong, tLong), keeping the
   old values dLat₀, dLong₀: step = s·dt; if not (step ≥ 0): lo = step + d, hi = d; else lo = d,
   hi = step + d. New d = lo if not (τ ≥ lo); else τ if τ ≤ hi; else hi.
   (The deflection follows the slip but stops at the target when the target lies inside the
   swept range; it never moves toward a target that lies behind it. So once a deflection is large
   — e.g. from a hard landing that briefly raised Fn — it stays there while the slip keeps its
   direction: a hit traffic car keeps braking hard until it stops or turns. Keep this.)
10. Tyre forces: fLat = −(K·dLat) − ((dLat − dLat₀)·(1/dt))·D;
    fLong = −(K·dLong) − ((dLong − dLong₀)·(1/dt))·D. Tyre force e = X·fLat − Z·fLong per
    component (X.c·fLat − Z.c·fLong).
11. a = (e.x + a.x, a.y + e.y, a.z + e.z). Add to the body: F.c = a.c + F.c; torque
    τ⃗ = (P − p) × a (standard cross: (y·a.z − z·a.y, z·a.x − x·a.z, x·a.y − a.x·y) with (x, y, z) =
    P − p), T.c = τ⃗.c + T.c.

So a traffic car's tyres are a friction model of coefficient 0.4 (times weather), equal in all
directions and independent of the surface, with no rolling: after a hit it slides to a stop.

### 6.3 One sample of a physical traffic car

Update (1.2 step 1), in this order:

1. Gravity: F.y = m·(−19.6) + F.y.
2. The rest detector (6.4).
3. Body integration (`vehicle-body.md`, "Integration"; the explicit path, since nothing adds
   implicit contacts), with its 5 rad/s angular limit. While the body is frozen (6.4 `state` ≠ 1)
   MM2 skips only the motion (that page's step 2); steps 1 and 3 still run, so `lastPush` =
   `pushSum` = 0 (6.4 has just cleared it) and `pushSum` = 0 — the same as integrating as usual.
   Steps 4 and 5 run every sample, frozen or not: the wheels probe, update their compression and
   deflections and add their forces (cleared again by the next sample's detector if the car stays
   still, used by the next integration if it wakes), and the lane matrix takes the body frame,
   which the end-of-sample push may have moved since the last sample. The release (6.5) keeps that
   lane matrix; it does not copy the body frame again.
4. The four wheels, FL, FR, BL, BR (6.2).
5. The car's lane matrix becomes its body frame (rows and position) — this is what is drawn and
   what the traffic logic sees — and its room is updated from its centre (2.2).

Collisions (1.2 step 3) as for any mover: its box against the city with `collision-contacts.md`
(previous matrix and shift as in 3.3, using the city identity), material of the box (3.4) against
the city material; pairs; gathered objects.

6. End of sample (1.2 step 5): the push is applied (`vehicle-body.md`, "Pushes": p = p + push,
   pushSum = pushSum + push, push = 0), **then** the previous matrix is set to the body frame (so it
   includes the push), the push source of this sample becomes the previous source and is cleared.

### 6.4 The rest detector

State: `state` (1 awake, 0 asleep, 2 long asleep), counters `still` and `still2`, vectors `Wsum`,
`Usum`; thresholds thrV = 0.01, thrW = 0.01 (squared speeds); limits 15 and 120. Reset:
`state` = 1, both counters 0, both vectors 0.

Each sample (6.3 step 2), with the body's angular velocity w, velocity v, push p, push sum
pushSum (both from `vehicle-body.md`; at this point p is 0 and pushSum holds the push applied at
the end of the previous sample), impulse accumulator G and 1/m:

1. w2 = (w.x·w.x + w.y·w.y) + w.z·w.z. If w2 > thrW: `Wsum` = w + `Wsum` (per component
   w.c + Wsum.c); if not (1.8·thrW ≥ (Wsum.x² + Wsum.y²) + Wsum.z²) (a NaN counts as larger):
   `Wsum` = w; **moving**.
2. `Wsum` = w.
3. u.c = ((p.c + pushSum.c)·(1/dt)) + v.c; u2 = (u.z·u.z + u.y·u.y) + u.x·u.x. If u2 > thrV:
   `Usum` = u + `Usum`; if not (1.8·thrV ≥ (Usum.x² + Usum.y²) + Usum.z²): `Usum` = u; **moving**.
4. `Usum` = u.
5. g.c = G.c·(1/m) + v.c; if ((g.x² + g.y²) + g.z²) − ((v.x² + v.y²) + v.z²) > thrV: **moving**.
   (The squares summed (x² + y²) + z².)
6. Still: if `state` = 1: `still` += 1; if `still` ≥ 15: `state` = 0, `still2` = 0, and the body is
   frozen: P, L, v, w set to 0 and every accumulator cleared (F, T, the post accumulators, G, H, the
   implicit-contact data, the turn), with lastPush = p + pushSum, pushSum = 0, p = 0. Otherwise
   (`state` ≠ 1): the same clearing of the accumulators without touching P, L, v, w; `still2` += 1;
   if `still2` ≥ 120: `state` = 2.
7. Moving: if `state` = 1: `still` = `still2` = 0; else the detector is reset (it wakes).

(1.8·thr and the squares compare as written; "w2 > thrW" and "u2 > thrV" are false for a NaN.)

So a traffic body that has been nearly still for 15 consecutive samples falls asleep. It is then
released after the frame (6.5). Within the frame it stays frozen (its accumulators are cleared
again in every further sample) unless an impact wakes it.

### 6.5 Leaving physics

Once per rendered frame, **after** all physics samples, each physical traffic car in the pool's
list order: if its rest detector's `state` is not 0 and −100 ≤ its body position y (a NaN fails),
nothing happens. Otherwise it is released:

1. It stops being a mover (for the rest of the frame).
2. **Back to the lane?** It goes back if all of: (a) a probe from S = (Y·0.5) + p to
   E = p − (Y·3.0) (Y = body Y row, p = body position, per component; ground mask, ignoring
   nothing, no probe state: both rooms are searched from the car's current room) hits, (b) the hit
   normal n has (n.y·Y.y + n.z·Y.z) + n.x·Y.x ≥ 0.9 (compared with the double 0.9: a single-precision
   0.9 counts as below), and (c) it was not left wrecked before. (MM2 also compares a damage value
   with the type's `MaxDamage`, but nothing ever raises that damage, so that part always passes.)
3. If it goes back: its lane logic is told so (lane speed 0, state *getting back into the lane*,
   or *given up* after 3 tries, `../engine/traffic.md`). Otherwise it is marked **wrecked** for good.
4. It becomes a gathered object again (2.3) at its last body frame, which stays its lane matrix,
   and leaves the pool's list: the last car of the list takes its place.

A wrecked car stays where it is; its lane logic declares it a mover with switch O only (1.1
row 3), so it still tests the objects it gathers. Any car that hits it again (or that it touches)
makes it physical again, with lane speed 0 (4.2 step 4 gives v = 0); when it then comes to rest it
is wrecked again.

Before the physics of each frame (when the pool declares its movers) a physical car is also dropped
from the pool's list if its rest state is 0, its body y is below −100, or its room is 0 — without
the release steps. The first two cannot happen there (6.5 already released such cars after the
previous frame); a car whose room is 0 (it left the city) is simply no longer simulated: it stays
frozen where it is, is not a mover and not a gathered object, and its body slot is free for the
next car that becomes physical. (MM2 leaves the car pointing at that slot; an implementation can
treat such a car as gone.)

## 7. Heavy vehicles and trailers

- **Traffic buses and trucks** use exactly the rules of 6 with their own data (`va_bus_f`: mass
  11761.6 kg, box 2.6 × 3.27 × 13.85 m). Only four wheels carry a traffic car, at the pivots WHL0–3,
  even for six-wheel models. Their box inertia, their 5 rad/s limit and the 0.4 friction make them
  heavy but not special.
- **Player and computer buses** (`vpbus`, `vpddbus`) are ordinary cars (5); a computer bus uses the
  box of its `.bnd`.
- **Trailers** (`vpsemi`, the unlisted `vpftruck` and `vplafrance`): `trailer.md` — a second rigid
  body with its own wheels, joined by a hitch joint, declared as a mover right after its car and
  never collided against its own car while joined; while joined, the inverse-mass matrix of either
  body in a collision is the joint's (`trailer.md` 6.2). Computer cars of these models get a
  trailer that never moves (`trailer.md` 10).

## 8. Things that look like mistakes (keep them)

- A police car 250 m or more from the player is frozen: no physics at all.
- The traffic box's material is the built-in default (friction 1, elasticity 0.5); the
  `.aiVehicleData` `Friction` and `Elasticity` are ignored.
- A traffic body rotates about its model origin (at road level), not about its centre of gravity;
  `CG` only places the box.
- The traffic body's angular limit is 5 rad/s, the cars' 4π.
- A traffic car becomes physical on any touching impact, with no threshold, and the test that makes
  it physical treats it as fixed while its response treats it as moving.
- A physical traffic car's previous matrix already contains the push and can be shifted by it a
  second time.
- The tyre deflection of a traffic car never relaxes toward a smaller target while the slip keeps
  its direction.
- A NaN suspension rate becomes −3; a NaN slip has sign −1.
- `CG`, `Spring` … missing from a type file are undefined in MM2.
- One bound per model name: a police car of the player's model uses the player's polygon bound.

## 9. Evaluation orders (summary)

Everything not listed is written above in its exact form.

- Broad test: d = c_A − c_B; (d.z² + d.y²) + d.x² ≤ (r_B + r_A)².
- AI box from the `.bnd` box: size.c = max.c − min.c; offset.c = size.c·0.5 + min.c.
- Box radius: √((h.x² + h.y²) + h.z²), h = S·0.5; box min/max: c.c + (−h.c), c.c + h.c.
- Becoming physical: v.c = (−speed)·Z.c; P.c = m·v.c.
- Wheel: see 6.2 (every sum written in its order); rest detector: 6.4.
- Release probe: S.c = Y.c·0.5 + p.c, E.c = p.c − Y.c·3.0; normal test (n.y·Y.y + n.z·Y.z) + n.x·Y.x.
- Response: `collision.md` with both K; the pushes' n·K orders there (uA and uB differ).
