# Collision and the physical world

How a car touches the world in Midtown Madness 2: what is solid, the surface materials, the
ground query the wheels use, how the car body collides with the city and with placed objects,
how the collision response changes the car's motion, and what happens at the edges of the world.

Companion files:
- `spec/formats/bnd.md` — bound files (collision shapes of cars and objects).
- `city-collision.md` — the collision polygons made from the PSDL, room lookups.
- `collision-contacts.md` — how the car body's contacts with the city are found.
- `collision-objects.md` — placed objects: which collide, bounds, gathering, probing.
- `collision-object-contacts.md` — how the car body's contacts with placed objects are found.
- `loose-bodies.md` — broken props, their pieces and thrown car parts as rigid bodies.
- The vehicle spec (`vehicle.md` and its pages: suspension, tyres, the rigid body) uses the ground
  query and the impulse/push interface defined here (`vehicle-body.md`, "Contact interface").

Coordinates: right-handed, +Y up, metres (`pkg.md`). Single-precision arithmetic throughout.

## The physics step

The physics runs in **samples** (`vehicle.md`, "Time step": n samples of dt = F/n per rendered
frame). Collision happens inside each sample, in this order:

1. Every moving object updates its own physics (for the car: `vehicle.md` "One physics step",
   steps 1–11, including the wheels' ground queries, then its room is updated from its reference
   point, `collision-objects.md` "Gathering").
2. Each moving object gathers the placed objects near it (`collision-objects.md`).
3. For each moving object in order (the player's car is one of them):
   a. its bound against the city polygons ("Body against the city");
   b. against every later moving object (cars against cars: `car-collision.md`);
   c. against each of its gathered placed objects.
   Each test produces impacts that are resolved immediately ("Collision response"). Resolving adds
   impulses (they act at the next sample's integration) and pushes to the rigid bodies.
4. For every moving object: its accumulated push is applied to its position (`vehicle.md` step 13;
   `vehicle-body.md` "Pushes"), and the body matrix as it was during this sample's collision is
   kept as the "previous matrix" for the next sample (`collision-contacts.md`).

The player's car takes part in all four steps; for a lone car step 3 is: city, then objects.

## Materials

A **material** describes a surface:

| Field | Type | Used by |
|---|---|---|
| name | up to 31 chars | lookup |
| elasticity | f32 | body collisions (bounce) |
| friction | f32 | body collisions (sliding along walls) and the wheel tyre grip |
| effect | name | particle effect index (not physics) |
| sound | int (`none` = 0) | surface sound (not physics) |
| drag | f32 | wheels: rolling drag on the surface |
| width, height | f32 | wheels: bumpiness (bump spacing and height) |
| depth | f32 | wheels: how far a wheel sinks in (soft ground) |
| ptxindex, ptxthreshold | 2 × int, 2 × f32 | wheel dust/spray particles (not physics) |

What the wheels do with drag, width, height and depth is in the vehicle spec; this file says
which material a wheel stands on.

**The built-in default material**: name `default`, elasticity 0.5, friction 1.0, drag 0,
width 1.0, height 0, depth 0, ptxindex −1 −1, ptxthreshold 0.25 0.5, effect index none,
sound 0. It is entry 0 of the city's material list.

**City materials** come from `city/materials.mtl` (`city-files.md`; syntax in `bnd.md`,
"Material blocks"), added to the list in file order after the default; a block whose name is
already in the list does not add or change anything. A block that stops after `sound:` keeps
the defaults above for the remaining fields. The shipped file:

| Name | Elasticity | Friction | Drag | Width | Height | Depth | Sound |
|---|---:|---:|---:|---:|---:|---:|---:|
| `deepwater` | 0.5 | 0.65 | 0.5 | 0.55 | 0.06 | 100 | 0 |
| `_default` | 0.9 | 0.9 | 0 | 0 | 0 | 0 | 0 |
| `grass` | 0.9 | 0.9 | 0 | 0.45 | 0.04 | 0.1 | 2 |
| `water` | 0.19 | 0.68 | 0.119 | 2.42 | 0 | 0.1 | 1 |
| `dirt` | 0 | 0.75 | 0 | 0.2 | 0.01 | 0 | 0 |
| `sand` | 0.9 | 0.9 | 0 | 0.45 | 0.05 | 0.1 | 0 |
| `cobblestone` | 0.9 | 0.9 | 0 | 0 | 0 | 0 | 0 |
| `wood` | 0.03 | 0.95 | 0 | 0.01 | 0 | 0 | 0 |

`_default` is an ordinary entry under that name; it is **not** the built-in default. (The game
has one special case: a second block named `_default` copies its values into the existing
`_default` entry and renames that entry `default`; no shipped file has two.) City polygons get
their material through their texture slot (`city-collision.md`, "Materials of texture slots");
bound polygons through their bound (`bnd.md`; a bound may add a new name to the list). The list
belongs to the loaded city and is rebuilt when a city loads.

**Material of a contact.** Every contact or probe hit names a material:
- probes: a city polygon by its material code (0 → the built-in default; *c* ≥ 1 → list entry
  *c* − 1); a polygon of an object's bound by the bound's material with the polygon's material
  index;
- body impacts against the city: the material code of the first intersection of that test, for
  all its impacts (`collision-contacts.md`, 3f); against a terrain bound likewise the material of
  the first intersection's polygon; against any other bound (geometry landmark, prop, car) the
  bound's **first** material, whatever polygon was hit.

## Ground queries

The wheels (vehicle spec) find the ground with a **probe**: a straight segment from a start point
S to an end point E, asking for the first solid surface the segment crosses, starting from S.

### The query

Probe(S, E, state, ignore, mask) → hit or no hit.

- `state` belongs to the caller (one per wheel) and persists between calls: the room of S and the
  room of E found last time (both 0 initially) and a **polygon cache** (empty initially).
- `ignore`: an object to skip (the car itself); `mask`: which kinds of placed objects take part
  (the wheels use the "ground" mask; see "Static objects").

On a hit it fills in:

| Result | Meaning |
|---|---|
| point | where the segment crosses the polygon plane: S + T·(E − S) |
| normal | the polygon's unit normal (as built; it may point away from S for a polygon crossed from behind) |
| T | fraction along S→E, 0 ≤ T ≤ 1 |
| penetration | how far the far end lies beyond the plane: if S is behind the plane (d_S ≤ 0) it is −d_S, else −d_E, with d = signed distance to the plane along the normal |
| front | whether S is on the normal's side (d_S > 0) |
| material | see "Material of a contact" |

Steps:
1. Room of S = Find(S, state.roomS); room of E = Find(E, state.roomE) (`city-collision.md`,
   "Finding the room of a point"); store both back in `state`.
2. Best T starts at 2.0 (no hit).
3. **City.** If room S or room E has PSDL bit 0x80, mark the cache invalid. If the cache holds a
   valid polygon, test it first (step 6 rule); if the segment crosses it, the city part is done:
   the hit is that polygon, **but only point, normal, T, penetration and front are set — the
   material and object of the result keep whatever the previous query of this `state` left there**
   (the cache stores no material; normally it is the same polygon as last time, so the material is
   right). If the cached polygon is missed, mark the cache invalid and continue:
   test, in this order, the polygons of room S, then of room E if different, then of each
   neighbour of room S (perimeter neighbours in perimeter order, each once, at most 10) that has
   PSDL bit 0x80 and is neither room S nor room E. A room of 0 is skipped. The rooms' polygons
   are those passing the culling of `city-collision.md` for the sphere centred at the segment's
   midpoint with radius 0.51 × the segment length. Exactly (single precision): centre
   m.c = (E.c − S.c)·0.5 + S.c per component (not (S + E)·0.5 as for objects); radius
   r = (1/λ)·0.51, with λ the segment's inverse length as defined in `collision-objects.md`,
   "Probing an object" (1/|S.y − E.y| for a vertical segment, else 1/√L2), so the length is
   1/λ rounded, not √L2; 0.51 is the single-precision constant 0.50999999. Each polygon is tested
   with step 6; a crossing with T ≤ best T becomes the new best (so on equal T the later polygon
   wins). If the city gave a hit, its material is the polygon's material code, and the polygon is
   copied into the cache (its corners and normals) and the cache marked valid.
4. **Placed objects** in room S and, if different, room E, in the room's list order
   (`collision-objects.md`, "Room lists"), whose kind
   matches the mask and that are not `ignore`: see "Static objects", "Probing an object". A hit
   there with T ≤ best T replaces the result.
5. **Hard-coded extra rooms.** If room S has the *extra rooms* bit (`city-collision.md`; only in
   cities whose name contains `sf`), the placed objects of one more room are also tested as in
   step 4: room 102 for room 411, 122 for 412, 96 for 423 and 1 for 625.
6. **Segment against one polygon** (corners c0…, normal n, inward edge normals eₖ):
   1. d_S = (S − c0)·n, d_E = (E − c0)·n. If both have the same sign → no crossing (the sign is
      the sign bit: +0 counts as positive, −0 as negative).
   2. T = d_S / (d_S − d_E). If T > best T → no crossing (equal is accepted).
   3. D = E − S. For each edge k (from corner k to corner k+1, wrapping): g = (c(k+1) − ck) × D,
      s = g·(S − ck). If s ≠ 0 and sign(s) ≠ sign(eₖ·g) (sign bits, as in 1.; s = −0 counts as
      0) → no crossing. (The segment's line passes inside every edge.)
   4. Otherwise it crosses at T; the hit point is p.c = (E.c − S.c)·T + S.c per component.
   Polygons are two-sided for probes: a segment crossing a floor from below also hits.
   Evaluation order (float32, left to right): w = S − c0 per component,
   d_S = (w.y·n.y + w.x·n.x) + w.z·n.z; d_E the same with w = E − c0; D = E − S per component;
   g = a × D = (a.y·D.z − a.z·D.y, a.z·D.x − a.x·D.z, a.x·D.y − a.y·D.x) with a = c(k+1) − ck;
   s = (g.z·w.z + g.y·w.y) + g.x·w.x with w = S − ck; eₖ·g = (g.z·eₖ.z + g.y·eₖ.y) + g.x·eₖ.x.
   City polygons are in world coordinates, so S, E, the point and n need no transform.

### What the wheels do with it (summary for the vehicle spec)

For reference, MM2's wheel uses the probe like this (details belong to the vehicle spec): the
segment runs along the wheel's up axis u from the wheel centre C raised by
(SuspensionLimit + 0.3) down to C lowered by (SuspensionExtent + wheel radius); mask "ground",
ignoring the car. A hit whose normal has n·u < 0.02 counts as no ground. A hit normal with
|n.y| < 0.001 gives zero tyre friction. (Single precision: S.c = (SuspensionLimit + 0.3)·u.c + C.c,
E.c = C.c − (SuspensionExtent + radius)·u.c; n·u = (n.z·u.z + n.y·u.y) + n.x·u.x; 0.3, 0.02 and
0.001 are single-precision constants; both tests are strict.) If there is no hit object the
default material is used. The wheel then reads friction, drag, width, height, depth of the
material.

## The car's bound

The player's car collides with its geometry bound `bound/<car>_BOUND.bnd` (text form) where
`<car>` is the car's base name (`car-model.md`). If the file is missing the car has no bound
(the game reports an error). The bound is placed with the car body's world matrix (the matrix
that draws the `BODY` mesh, origin at the model origin, not at the centre of gravity).

- All of the bound's polygons use **one material owned by the car**, whatever the file says: the
  built-in default values with friction = the car's `BoundFriction` and elasticity = the car's
  `BoundElasticity` (from `tune/vehicle/<car>.vehcarsim`, `vehCarSim` block). E.g. `vpbug`:
  friction 0.5, elasticity 0.5.
- Cars driven by the computer (opponents, police) use instead a **box** bound made from the box
  of the same `.bnd` file (min … max; `car-collision.md` 3.1). One bound is shared by all cars of
  the same model.
- The bound's derived data (`bnd.md`, "After loading") are used: edges, edge normals and
  cosines, box and radius.

## Body against the city

Each physics sample, after its own update, the car's bound is tested against the city polygons of
its room and the rooms its bound touches: `collision-contacts.md` gives the whole procedure (which
polygons, corner paths and edge crossings, and how they become impacts). The impacts are resolved
as in "Collision response" below, each with weight 1/N for the N impacts of that test.

## Static objects

Which placed objects are solid, their bounds and placement, how the car gathers the objects near
it and how wheel probes test them are in `collision-objects.md`; the contact generation against
them is in `collision-object-contacts.md`. After the city test, the car is tested against each gathered object in
turn; each test produces its own impact list, resolved with weight 1/N for that object's N impacts.

## Collision response

Every body collision (against the city, against a placed object, against another car) ends in a
list of **impacts**. One collision test of one object pair produces N impacts (N ≥ 1); each impact
is resolved independently, in list order, with weight w = 1/N. All impacts of all pairs in one
physics sample see the same velocities: the impulses only collect in the bodies' impulse
accumulators and act at the next integration (vehicle spec, `vehicle-body.md`), so the result does
not depend on an iterative order, only on summation order. There is no iterative solver and no
separate resting-contact model: a car resting against a wall or on its roof is held by the same
per-sample impulses and pushes. (MM2 contains a persistent spring-damper contact model, but the
game switches it off when a race starts; it is not part of the behaviour.)

### The impact record

| Field | Meaning |
|---|---|
| A, B | the two colliding things. In the city test A is the city and B the car; in an object test A is the car and B the object (`collision-object-contacts.md`) |
| P | contact point (world) |
| n | unit contact normal, pointing **out of B into A** (city test, car on a floor: pointing down into the floor) |
| depth | penetration depth (≥ 0), metres |
| material A, material B | the materials of the two sides ("Material of a contact") |

A side without a rigid body (the city, a fixed object) has an inverse-mass matrix of all zeros and
velocity 0; applying an impulse or push to it does nothing.

### Quantities

- **Point velocity** of a body at P: v + ω × r with r = P − x (x = centre of mass), the cross
  product evaluated as (r.z·ω.y − ω.z·r.y, ω.z·r.x − r.z·ω.x, ω.x·r.y − ω.y·r.x) and then v added.
  (Not the wheels' push-corrected point velocity.)
- **Inverse-mass matrix** K of a body at P (3×3, row-vector convention: an impulse J at P changes
  the point's velocity by J·K): r = P − x; for each body axis row Rₖ (k = X, Y, Z):
  aₖ = Rₖ × r (as (R.y·r.z − r.y·R.z, r.x·R.z − R.x·r.z, r.y·R.x − R.y·r.x)) and wₖ = aₖ·(1/Iₖ);
  K[i][j] = Σₖ aₖ[i]·wₖ[j], summed per element with tₖ = aₖ[i]·wₖ[j] as: (t₀ + t₁) + t₂ for
  K[0][0] and K[1][0]; (t₀ + t₂) + t₁ for K[2][0]; (t₁ + t₂) + t₀ for the other six; then 1/m is
  added to each diagonal element. (1/m and 1/Iₖ as in `vehicle-body.md`.)
- **Impact material**: friction μ = f_B·f_A; elasticity e = e_B·e_A, but at most 1.0.

### Resolving one impact (weight w)

1. vrel = (point velocity of A) − (point velocity of B), per component.
2. K_A, K_B = inverse-mass matrices of A and B at P.
3. vn = (n.z·vrel.z + n.y·vrel.y) + vrel.x·n.x.
4. **Impulse.** If vn ≤ 0.01 (single-precision 0.01; a NaN vn also takes this branch):
   1. K = K_A + K_B (element by element).
   2. Solve x·K = −vrel for x ("Solving" below): the impulse that would stop all relative motion
      at P.
   3. jn = (x.z·n.z + x.y·n.y) + x.x·n.x; tangential part xt = x − jn·n (per component
      x.c − jn·n.c); L = √((xt.z² + xt.y²) + xt.x²).
   4. If L > jn·μ (strictly) the contact slides: s = μ/L; d = (xt·s) + n (each component
      xt.c·s + n.c); u = n·K with u_j = ((n.z·K[2][j] + n.y·K[1][j]) + K[0][j]·n.x);
      den = (u.z·d.z + d.y·u.y) + d.x·u.x; if −0.00001 < den < 0.00001 then x = 0, else
      x = d·(−(vn/den)). Otherwise (sticking) x stays as solved.
   5. x = x·(e + 1) (e + 1 computed once).
   Otherwise (vn > 0.01, separating) x = 0.
   (Contacts with 0 < vn ≤ 0.01 still get an impulse that brings vn to 0; the friction direction
   comes from the sticking impulse, not from the slip velocity; and (1 + e) also scales the
   friction part. Keep all three; they are MM2's behaviour.)
5. **Push.** If depth > 0:
   uA = n·K_A as above, a2 = (uA.z² + uA.y²) + uA.x²;
   uB = n·K_B computed as uB.x = (K_B[2][0]·n.z + K_B[1][0]·n.y) + K_B[0][0]·n.x,
   uB.y = (K_B[2][1]·n.z + K_B[0][1]·n.x) + K_B[1][1]·n.y,
   uB.z = (K_B[2][2]·n.z + K_B[0][2]·n.x) + K_B[1][2]·n.y; b2 = (uB.y² + uB.z²) + uB.x².
   Shares: if a2 > b2: wA = 1/(√(b2/a2) + 1), wB = 1 − wA; if b2 > a2: wB = 1/(√(a2/b2) + 1),
   wA = 1 − wB; otherwise wA = wB = 0.5.
   pushA = n·(wA·depth); pushB = n·(−(wB·depth)).
   If pushA.y < 0 and b2 > 0: pushB.y = pushB.y − pushA.y, pushA.y = 0; else if pushB.y < 0 and
   a2 > 0: pushA.y = pushA.y − pushB.y, pushB.y = 0. (Nobody is pushed down if the other side can
   take it. Against the city or a fixed object the car takes the whole push, even downwards.)
   If depth ≤ 0 both pushes are 0. The push is **not** multiplied by w. (With one side fixed, its
   K is 0 and the moving side takes the whole depth along the normal, away from the fixed side.)
6. J = x·w.
7. Apply to A: impulse J at P and add push pushA; then to B: impulse −J at P and add push pushB.
   "Impulse at a point" and "add push" are the rigid-body operations of `vehicle-body.md`
   ("Contact interface"; the push merge rule there makes pushes in the same direction count once).
   A side without a rigid body ignores both.

The full depth is pushed out (MM2 has a "skin" allowance that would leave some penetration, but
the game sets it to 0 when a race starts; only the static initial value is 0.035).

### Solving x·K = b

K is 3×3 with elements m0 … m8 row by row.
1. maxAbs = the largest |mₖ| (scan m0 to m8, replacing only on strictly greater); remember its
   row r (1-based). If maxAbs = 0, x = 0.
2. ε = maxAbs·0.0001.
3. Cofactors: C00 = m8·m4 − m7·m5; C01 = −(m8·m3 − m6·m5); C02 = m7·m3 − m6·m4;
   C10 = −(m8·m1 − m2·m7); C11 = m8·m0 − m6·m2; C12 = −(m7·m0 − m6·m1);
   C20 = m5·m1 − m4·m2; C21 = −(m5·m0 − m2·m3); C22 = m4·m0 − m3·m1.
   Rᵢ = largest |Cᵢⱼ| in row i; Rmax = the largest Rᵢ.
4. det = (C02·m2 + C01·m1) + C00·m0.
5. If |det| > ε², |det| > Rmax·ε, and (for r = 1: R1 > ε and R2 > ε; for r = 2 or 3: R0 > ε and
   R2 > ε): xᵢ = ((Cᵢ2·b2 + Cᵢ1·b1) + Cᵢ0·b0)·(1/det).
6. Otherwise K is (nearly) singular and the game takes a least-squares solution. This never
   happens for a car (its K always has full rank); an implementation may use any pseudo-inverse.

### Breakable props

A breakable prop that has not been hit yet (`collision-objects.md`) stands fixed, but when a
collision test with it gives impacts it gets a rigid body for that test (`loose-bodies.md`, 2: at
rest, with its own mass and inertia), and each of its impacts is resolved by this rule instead of
"Resolving one impact" (A = the car or other mover, B = the prop's body, n out of the prop into
A, weight w = 1/N for the N impacts of the test):

1. vrel and K_A as above; vn = (n.z·vrel.z + n.y·vrel.y) + n.x·vrel.x.
2. If vn ≤ 0.01 (NaN too):
   1. x = the solution of x·K_A = −vrel ("Solving"): the sticking impulse against an immovable
      prop.
   2. If |x|² ≤ ImpulseLimit2 (the prop's `.dgBangerData` value; |x|² = (x.z² + x.y²) + x.x²;
      a NaN |x|² counts as ≤): **not broken by this impact**: K = K_A, K_B = 0, x_cap = 0.
      Otherwise **broken**: f = √(ImpulseLimit2/|x|²); x_cap = x·f (per component); in MM2 the
      rest is resolved against the prop's own rigid body: vrel′ = x_cap·K_A + vrel, per component
      vrel′_j = ((x_cap.z·K_A[2][j] + x_cap.y·K_A[1][j]) + x_cap.x·K_A[0][j]) + vrel_j,
      K_B = the prop body's inverse-mass matrix at P, K = K_A + K_B, x = the solution of
      x·K = −vrel′.
   3. Friction cone as in "Resolving one impact" step 4.3–4.4 on this x with this K and μ,
      but with vn from step 1 (the original vrel, also in the broken case), and with the
      denominator summed in a different order: den = (d.y·u.y + d.x·u.x) + u.z·d.z. (L is summed
      (xt.y² + xt.z²) + xt.x² here, which gives the same value as step 4.3.)
   4. x = x·(e + 1).
   Otherwise (vn > 0.01): x = 0, x_cap = 0, K_B = 0.
3. Push, if depth > 0: uA = n·K_A, uB = n·K_B (as in step 5 above), but the shares come from the
   vector lengths: LA = |uA|, LB = |uB| (each √((u.x² + u.y²) + u.z²)), S = LA + LB; if S = 0 then
   S = 1 and LA = LB = 0.5. f = depth/S; pushA = n·(f·LA), pushB = n·(−(f·LB)). Then if
   pushA.y < 0: pushB.y = pushB.y − pushA.y, pushA.y = 0; else if pushB.y < 0: pushA.y = pushA.y −
   pushB.y, pushB.y = 0 — **without** the a2, b2 > 0 conditions, so an untouched prop never pushes
   the car downwards (that part of the push goes to the prop, which ignores it). If depth ≤ 0, no
   push.
4. J_A = (x + x_cap)·w, J_B = (−x)·w. Apply impulse J_A and push pushA to A, J_B and pushB to the
   prop's body (with the push sources of `car-collision.md` 3.3: each side records the other's
   identity; the prop's is its body's).

Every impact of the test is judged separately by step 2 (same velocities, same limit); the prop
is broken if **any** impact of the test broke it, and the break takes effect after the whole
impact list of that test has been resolved (impacts below the limit in the same test are resolved
as "not broken", but their J_B and pushB still go to the prop's body). Then: if nothing broke, the
prop's body is let go and the prop stands as before; if it broke, the prop moves as one piece or
breaks into its pieces with everything its body collected (`loose-bodies.md`, 3).

A race reset, a world-limit reset (below) and the start of a check run bring every prop back
untouched, as MM2's reset restores all placed objects, and remove all loose bodies
(`loose-bodies.md`, 1). Props with ImpulseLimit2 = 0 (9 shipped types) break on any approaching
contact.

## World limits

There are no invisible walls. The city ends where its rooms end: beyond the last room edge (and
over deep water) there is nothing solid.

- **Outside the city.** A point in no room has room 0. A probe whose start and end are in room 0
  finds nothing in the city; a car whose room is 0 gets no terrain collision at all (the game
  reports "entry in room 0" and skips it). The car keeps falling.
- **Falling through.** Once per rendered frame the game checks the player car's rigid-body
  position (centre of mass): if its Y is below −50, the game's "dropped through the city"
  handling runs. In single-player modes, including cruise, that is a full reset of the race: the
  car goes back to its starting position and state (as after the reset key).
- **Deep water** ("water of death"). Fans whose texture slot has material code 2 (`s_thames`,
  `s_ocean`) make no polygons (`city-collision.md`), so cars fall into the water there; rooms with
  the *deep water* bit are where the water effect below switches on.
  Water level L = the first number of `city/<city>.water` (London −3.8, SF −1.9; without the
  file the previous city's value stays, 0 at start-up). After every car
  update inside a physics sample: if the car's room has the deep-water bit and the car body's
  world position Y (origin of the body matrix) is below L, the car's **water effect** is switched
  on (once on, it stays on until the car is reset) and remembers L. While on, every physics
  sample applies buoyancy and drag (below). Each rendered frame, while the player car's water
  effect is on, a timer runs (timer ← frame length + timer, in single precision, like everything
  else here: with a fixed frame of 1/30 s it passes 5 s on the 151st frame, not the 150th); when it
  exceeds 5 s (timer > 5.0, strictly) the game's "hit water" handling runs: in cruise
  that is the same full reset as above (other modes have their own rules: `../game/races.md`, "Water and falling through"). The
  timer starts at 0 on reset. Both checks (falling through and the water timer) also run while
  the game is paused (`../game/races.md`, "Water and falling through").
- **Water effect** (per physics sample, after the car's own update). 64 sample points in the
  rigid-body frame (origin at the centre of mass, axes = body rows) are fixed when the car is set
  up: with Size = the tune's `InertiaBox` and CoG = the tune's `CenterOfGravity` (both as written
  in the tune file, used in the rigid-body frame), q.c = CoG.c − Size.c·0.5 and
  Q.c = Size.c·0.5 + CoG.c per component; for i = 0 … 3 (outer loop), j = 0 … 3, k = 0 … 3 (inner
  loop), the point is p = (((Q.x − q.x)·i)·0.33333334 + q.x, ((Q.y − q.y)·j)·0.33333334 + q.y,
  ((Q.z − q.z)·k)·0.33333334 + q.z), stored in that order (i, j, k as floats). So the grid is
  centred at body position + R·CoG (the model origin), not at the centre of mass. While setting
  up each point MM2 also draws three numbers from the game's random generator and throws them
  away (192 draws per car); see "Random numbers" below. b (buoyancy factor) is set to 0.7 and k
  (drag factor) to 0.08 once, when the car is created. A reset of the car does **not** restore b:
  it only switches the water effect off, so the next time the effect switches on, b goes on from
  the value the previous dunk left (in cruise the "hit water" reset comes after about 5 s in the
  water, so a second dunk starts near 0.7 − 5·0.03 = 0.55; b never drops below 0.4). Only a car
  created anew (a new game) starts again at 0.7. With m = the car's mass, x = the rigid body's
  position, v its velocity, ω its angular velocity (world frame): for each point in that order,
  w = the point in world space, w.x = ((p.z·Z.x + p.y·Y.x) + p.x·X.x) + x.x,
  w.y = ((p.z·Z.y + p.x·X.y) + p.y·Y.y) + x.y, w.z = ((p.z·Z.z + p.x·X.z) + p.y·Y.z) + x.z
  (X, Y, Z = body rows); if w.y ≤ L (NaN too):
  r = w − x; u = v + ω × r; d = x·(1/√l2) with l2 = (x.z² + x.y²) + x.x² (the unit vector of the
  body's **world position** — an original oddity; zero if l2 = 0);
  F = (−(((u.x + d.x)·k)·m), mb − ((u.y + d.y)·k)·m, −(((u.z + d.z)·k)·m)), where mb = m·b is
  formed once per sample before the 64 points (so F.y is **not** m·(b − k·(u.y + d.y)): the
  product m·b is rounded on its own);
  add F to the body's force and r × F to its torque (force/torque accumulators of the rigid body,
  vehicle spec). After the 64 points: if b > 0.4, then b ← b − 0.03·dt (dt = the sample length)
  and, inside that same branch, if b < 0.4, b = 0.4, else if b > 0.7, b = 0.7 (b ≤ 0.4 is left
  as it is).
  Note b is an acceleration per point, not multiplied by gravity: a fully submerged car gets
  64·0.7·m upwards against its weight m·9.8, so cars float.
- **Random numbers.** MM2 has one game-wide random generator (state x, a 32-bit integer:
  x = x·214013 + 2531011 with wrap-around, value ((x >> 16) & 0x7FFF)·3.0517578e-05; the same one
  `vehicle-wheels.md` uses for bumps). The game restarts it (x = 1) when a city loads and when a
  race is reset, which in MM2 happens after the car is set up, so the 192 water-point draws never
  affect driving. For check runs and the engine: set up the car (with or without these draws),
  then restart the generator as `drive-check.md` says; a world-limit reset restarts it too.
  MM2 also keeps a **second state** for the same generator formula and switches to it around the
  draws of most visual effects: particle bursts (wheel dust, grass and spray, smoke), sparks, the
  texture damage marks on a car body, the engine-smoke emitter point, and the network games'
  respawn choice. Those draws therefore never advance the game-wide sequence. The second state
  starts at 0 when the program starts and is never restarted (x = 1 restarts only the game-wide
  state). Other draws made during play do use the game-wide state: traffic, police and
  pedestrians, car parts thrown off (`damage.md`), breaking glass panes, the flicker of glowing
  props at night (one draw per drawn glow), and force-feedback effects on a force-feedback
  controller. So the game-wide sequence of the real game still cannot be matched number for
  number. An implementation must give visual effects their own generator (MM2's second state is
  the model for that) so that only the draws that change the physics use this one: the bumps and
  the car parts thrown off (their eight launch draws and the wreck throw's choices, `damage.md`).
  Breaking glass panes (`loose-bodies.md`, 8) also draw from it in MM2; no shipped prop is one.
- **Resets in check runs.** A fall-through or water reset during a check run puts the car back at
  the run's `start` with the run's own placement rule (`drive-check.md`), at rest, with its water
  effect off (timer 0; b keeps its value, as in MM2: it is 0.7 only at the start of the run);
  every prop comes back untouched and the random generator restarts with x = 1. The run's frame and step counters and its control timeline keep going (MM2 also
  restarts its race clock; check runs have none).
- **Upside down / stuck.** Righting a car that is stuck or on its roof is part of the vehicle
  spec (MM2's "stuck" helper), not of the collision.

## Flat world for check runs

`world flat` in `drive-check.md` is one horizontal polygon at y = 0, normal (0, 1, 0), of
unlimited size (every edge test of the probe passes; every point is inside it for the body
collision), with the material named `cobblestone` in `city/materials.mtl` — the material of every
road, sidewalk and crosswalk texture in MM2. With the shipped file: friction 0.9, elasticity 0.9,
drag 0, width 0, height 0, depth 0 (ptxindex 4 7, ptxthreshold 0.25 0.3). If no `cobblestone`
material is loaded, use the built-in default. There are no rooms, no placed objects, no deep
water and no fall-through limit; the probe cache and room hints have no effect. Gravity and the
time step are the vehicle spec's.
