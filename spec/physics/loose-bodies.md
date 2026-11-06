# Loose bodies: props knocked loose, their pieces, and car parts thrown off

In MM2 a prop that breaks does not vanish. It, or the pieces it breaks into, becomes a rigid body
that falls, slides, hits the city, cars and other props, and comes to rest in the street, where
it can be hit again. Car parts thrown off by hard impacts (`damage.md`, "Parts thrown off")
become the same kind of body. This page covers these **loose bodies**: where they come from, how
they are set up, how they move and collide, when they stop, and when they go away.

Single precision, operations in the order written (`vehicle.md`, "Precision"). Words used here:
an **untouched prop** is a prop standing where it was placed (`collision-objects.md`); a **moved
prop** is an object made from a broken prop or a thrown part, which keeps its own position and
matrix.

## 1. Two pools

MM2 keeps two fixed pools for this. Both are emptied at every race reset (also at the start of a
check run and at a world-limit reset, `collision.md`).

**Bodies: 32.** The bodies are kept in a list; the first *n* are in use, in the order they were
taken, the others follow. Taking a body takes element *n* and makes it in use. When all 32 are in
use, the first element of the list is let go (2.3) and taken again for the new object (it keeps
its place at the front). Letting a body go swaps it with the last body in use and makes it free,
so the bodies in use keep their order except that the last one moves into the freed place. At a
reset every body is let go and the list returns to its original order.

**Moved-prop slots: 40**, handed out in a ring: slot 0, 1, …, 39, then slot 0 again — and the
next request after that **also** gets slot 0, after which the ring goes on with 1, 2, … (an
original quirk; keep it). A slot that is handed out first loses whatever it held: if it has a
body, the body is let go; if it is in a room, it leaves that room. So once more than 40 objects
have been made since the last reset, the oldest moved props disappear. At a reset every slot
leaves its room and loses its body, and the ring restarts at slot 0.

## 2. A body for an object

### 2.1 When

An object gets a body:

- an untouched prop, when a collision test with it gives impacts (before the response:
  `collision.md`, "Breakable props", uses it); if nothing breaks, the body is let go again at the
  end of that test;
- a moved prop lying still, when a collision test with it gives impacts (6.3);
- every piece of a breaking prop (3) and every thrown car part (4).

The object then stops being a **collidable** object: movers no longer gather it
(`collision-objects.md`, "Gathering") until its body is let go.

### 2.2 Setting the body up

1. Everything is at rest: momentum, angular momentum, velocities, all force, torque and impulse
   accumulators, the pushes and `lastPush` are 0 (`vehicle-body.md`, "State").
2. The body's rotation rows and position are the object's matrix. For an untouched prop that
   keeps only its turn about Y (`collision-objects.md`, "Props") the matrix is
   X = (m₀₀, 0, m₀₂), Y = (0, 1, 0), Z = (−m₀₂, 0, m₀₀) with position = the prop's stored position
   (its centre of gravity); a prop with a full matrix, and a moved prop, use their own matrix.
3. Mass and inertia from the object's prop data (`.dgBangerData` `Mass` m and `Size` (bx, by, bz),
   `collision-objects.md`): the box formula of `vehicle-body.md`, "Mass and inertia". Then no
   principal moment may be smaller than the largest one divided by 40: with (Ix, Iy, Iz),
   M = Iz if (Ix ≤ Iy and Iy < Iz) or (Ix > Iy and Ix < Iz), else M = Iy if Ix ≤ Iy, else M = Ix;
   f = M/40; each moment below f becomes f; then 1/m and the inverse moments are formed again
   (`vehicle-body.md`, same rules).
4. Limits: speed 500 m/s; angular speed 5 rad/s per body axis (`vehicle-body.md` `wmax`; not the
   car's 4π).
5. Its bound is the prop data's bound (`collision-objects.md`, "Props", in coordinates relative to
   the body position), with that one material.
6. The rest detector (`car-collision.md` 6.4) is reset, with thrV = 0.1 and thrW = 0.5 (the limits
   15 and 120 as there).
7. Its push source and previous matrix are those of a new body: no previous source; the previous
   matrix is set at the end of the first sample it is a mover in.

Each body has its own **identity** for push sources and damage slots (`car-collision.md` 3.3,
`damage.md`). The identity belongs to the body in the pool, not to the object, so two untouched
props tested one after the other in a sample usually share it (the second takes the body the first
one gave back).

### 2.3 Letting a body go

The body stops being a mover for the rest of the frame (1.1 of `car-collision.md`: all switches
off), the object becomes collidable again, and the body returns to the pool (1). The object stays
where it is with its last matrix: a moved prop then lies still in its room (5.4).

## 3. When a prop breaks

The break rule is `collision.md`, "Breakable props". When a test broke an untouched prop (after
all its impacts are resolved), with N = the prop data's `NumParts`:

**N = 0: the prop moves as one piece.** A moved-prop slot is taken and put at the front of the
prop's room (`collision-objects.md`, "Room lists"); it gets the prop's data, model and paint, and
the prop's matrix (2.2 step 2). The prop leaves its room (it comes back at the next reset). The
slot takes over the prop's body as it is, with everything the test's impacts gave it (impulses
and pushes), and becomes a mover (5.3, the replacing kind).

**N > 0: the prop breaks into N pieces.** The prop leaves its room. Let X, Y, Z and q be its
matrix (2.2 step 2) and c its data's `CG`; the model origin is o = q − R·c with
(R·c).k = (Z.k·c.z + Y.k·c.y) + X.k·c.x for each component k. From the prop's body (B, with its
impulse accumulator G, angular impulse accumulator H, inverse mass 1/m, position p_B):
Δv = (G.x·(1/m), G.y·(1/m), G.z·(1/m)) and Δω.k = ((H.z·W[2][k] + H.y·W[1][k]) + H.x·W[0][k]) with
W the body's world inverse inertia (below). Then for i = 1 … N, in order:

1. A moved-prop slot is taken and put at the front of the prop's room. Its data is the prop data
   `<prop>_BREAK<ii>` (ii = i as two digits, e.g. `sp_stop_f_BREAK01.dgBangerData`), its model the
   part `BREAK<ii>` of the prop's package (`../formats/pkg.md`), its paint the prop's.
2. Its matrix: rotation X, Y, Z; position o + d with d the piece data's `CG` e turned:
   d.x = (Z.x·e.z + Y.x·e.y) + X.x·e.x, d.y = (X.y·e.x + Z.y·e.z) + Y.y·e.y,
   d.z = (X.z·e.x + Z.z·e.z) + Y.z·e.y, each component o.k + d.k. Afterwards o becomes
   (o + d) − d per component (computed, so it can drift by a rounding step from piece to piece).
3. It gets a body (2.2) at that matrix.
4. Its share of the hit: r = p_B − p (p its body position), c = r × Δω (`collision.md` cross
   product order: (r.y·Δω.z − r.z·Δω.y, r.z·Δω.x − r.x·Δω.z, r.x·Δω.y − r.y·Δω.x));
   G.k = ((c.k + Δv.k)·m_i) + G.k (m_i its mass); with I_w its world inertia tensor
   (`vehicle-body.md`, "Mass and inertia"), L.k = ((I_w[2][k]·Δω.z + I_w[1][k]·Δω.y) +
   I_w[0][k]·Δω.x) and H.k = L.k + H.k.
5. It becomes a mover (5.3, the replacing kind; only the first piece replaces the prop in the
   gathered lists).

Then the prop's body is let go; its pushes are lost. The pieces start where the prop's parts were
drawn, with the velocity the hit would have given that point of the whole prop, and spin with it.

**World inverse inertia** W = Rᵀ·S, with S the rows X·(1/Ix), Y·(1/Iy), Z·(1/Iz) (each row times
its inverse moment) and a = Rᵀ (a[i][k] = R[k][i]); W[i][j] sums a[i][k]·S[k][j] over k in the
order (written as in `vehicle-body.md`, "Products"):

| | j = 0 | j = 1 | j = 2 |
|---|---|---|---|
| i = 0 | 0, 1, 2 | 1, 2, 0 | 1, 2, 0 |
| i = 1 | 1, 0, 2 | 2, 1, 0 | 2, 1, 0 |
| i = 2 | 0, 2, 1 | 2, 1, 0 | 2, 1, 0 |

Data: 80 of the shipped prop data files break into pieces (`NumParts` 1 … 8; e.g. `sp_stop_f` 2,
the pedestrian lights `sp_traflitsingle_ped_l` 3). Piece data are boxes (`CollisionPrim` 1) or
geometry bounds (0), never capsules. The pieces' data are found by name; MM2 itself looks them up
as the entries registered right after the prop's own when its first copy was set up, which is the
same for every shipped city. Traffic-light poles (`../ai/traffic-control.md`) break the same way.
If a piece's data file were missing, MM2's numbering would shift; no shipped prop lacks one.

## 4. Car parts thrown off

When `damage.md` throws a part ("Throwing a part"), MM2:

1. takes a moved-prop slot and puts it at the front of the car's room (the room of its reference
   point, `collision-objects.md`, "Gathering");
2. gives it the prop data `<car>_<PART>` (e.g. `vpcab_BREAK01`), the car's part model `<PART>`
   and the car's paint set;
3. sets its matrix to the part's own matrix a (rotation identity, position = its place p) times
   the car's world matrix b (`vehicle.md`; model origin and rows): rows r[i][j] = Σ a[i][k]·b[k][j]
   with the sum orders of `vehicle-body.md`, "Products" (matrix product), and position
   r.T.j = (Σ a.T.k·b[k][j]) + b.T.j with the k orders 1, 0, 2 for j = 0 and 1 and 2, 1, 0 for
   j = 2, b.T.j added last;
4. gives it a body (2.2): momentum 0;
5. draws the eight numbers of `damage.md` and **sets** the body's momentum P = dir·s and **adds**
   axis·k to its angular impulse accumulator H; then sets the body's rotation and position to the
   matrix of step 3 again (no change);
6. makes it a mover (5.3, the simple kind) — only if the car's room is not 0.

Car parts are boxes (`CollisionPrim` 1, `CollisionType` 16) whose `Size` and `Mass` come from the
part (a vpbug wheel: 0.26 × 0.67 × 0.67 m, 92.2 kg; a vpcab bumper `BREAK01`: 1.60 × 0.10 ×
0.37 m, 47.2 kg) with `CG` (0, 0, 0) for wheels; a body part's data `CG` is not used here (the
body sits at the part's place p).

## 5. Loose bodies as movers

### 5.1 Each frame

Loose bodies are declared as movers (`car-collision.md` 1.1) after all other movers of the frame,
in the order of the body list (1). First the pool drops, without letting go, every body in use
whose rest detector state is 0, whose position y is below −100, or whose object is in room 0
(each dropped one is replaced by the last in use, and the same place is checked again); such an
object stays wherever it is and is no longer simulated. It also stays marked as having a body, so
it is not collidable (not gathered) until its slot is handed out again or a reset, and it is drawn
where it lies while it is in a room. (MM2 also leaves the object pointing at that pool body, which
the pool soon hands to another object; when the dropped object's slot is handed out again, at a
reset, or when its car takes its parts back, MM2 lets go of whichever object holds that body by
then, and a slot reused as a piece or car part takes that body over without setting it up, which
can end in a crash. Do not reproduce this: treat the link as gone once the body is dropped.)
Then each body in use is declared by its
prop data's `CollisionType` t:

| t | Mover | Switches |
|---|---|---|
| has 0x40 | a mover of the second kind | U C V O |
| else has 0x10 (973 of the 992 shipped data files, counting a missing key as 0x10) | a mover of the first kind | U C V O |
| else has 4 (19 files: stop and give-way signs, bollards, a few others, and their pieces, e.g. `sp_stop_f`) | a mover of the first kind | U C |
| none of these | not declared: the body stays frozen where it is | — |

(t with 0x02 makes MM2 run the body's update once per frame without collisions; no shipped prop
has it. The mover list holds at most 32: further loose bodies are not declared that frame.)

**The player's rooms.** When the player's car is declared, the game notes its room and then that
room's neighbours (the distinct non-zero rooms across the edges of its perimeter, in perimeter
order, `city-collision.md`), at most 20 in all, stopping at the first one that is already in the
list. At the start of the frame's physics every loose mover of the **first kind** whose object is
not in one of these rooms is switched off and removed: its body is let go and its object leaves its
room. So a loose body that the player leaves behind (more than one room away) disappears. (With
several players or race opponents nearby, their rooms count too, `../ai/opponents.md` "One frame"
step 3; not in check runs.)

### 5.2 Once per frame, after the samples

Each loose mover that still has switch U, in mover order: if its rest detector's state is 0 (it
fell asleep, `car-collision.md` 6.4), or its position y is below −100 (a NaN too), its body is let
go (2.3). Otherwise nothing happens.

### 5.3 Becoming a mover during the physics

A body that an object gets during a sample (2.1) becomes a mover at once, at the end of the mover
list, with **no switches until the end of that sample**; at the end of the sample it gets all four
switches (U C V O, whatever its `CollisionType`) and takes part in the sample's end step (6.4). It
keeps them for the rest of the frame; from the next frame on it is declared by 5.1. Three kinds,
each refused (the body is then not a mover this frame, though it keeps the body) when the mover list
is full (32) or the object's room is 0:

- **simple** (a thrown car part): also refused if the object is already in the mover list;
- **replacing** (a broken prop's moved copy or piece): the untouched prop it replaces is removed
  from every mover's gathered list (5.4) so no later test of the sample meets it; refused if the
  mover whose test broke the prop is not in the list;
- **lying still** (a moved prop hit again, 6.3): if the object is already in the mover list, that
  entry is reused with no switches; else it is appended, and removed from the gathered list of the
  mover whose test hit it (other movers that gathered it test it as a moving body, 6.3).

### 5.4 Moved props in rooms and gathering

A moved prop is a non-static object of its room's list (`collision-objects.md`, "Room lists"):
it goes to the front whenever it is put into a room or changes room. That list is the **same** one
that holds the room's traffic cars without a body and its pedestrians, so they all stand in filing
order, newest first, whatever their kind: a moved prop filed after a lane car comes before it in
gathering, and a lane car that enters the room after the prop comes before the prop. Gathering takes a moved prop
only while it is collidable (no body), with the props (movers with switch O or V); its reference
point is its matrix position and its radius the radius of its model (`collision-objects.md`,
"Gathering", step 1: for a broken prop's copy the prop model's radius; for a piece the radius of the
piece part `BREAK<ii>` of the prop's package; for a car part the radius of that part's meshes in
the car's package, `BREAK01_H` … `BREAK01_VL`). No `YRadius` cylinder test (that is for untouched
props only). Wheels never probe moved props.

A loose mover gathers for itself as a car does (`collision-objects.md`, "Gathering", and
`car-collision.md` 1.2 step 2) with its room, reference point = its matrix position, radius = its
model radius as above, and its switches: props and moved props with switch O or V, landmarks and
spanning objects with any of C, O and V. It never gathers its own object.

## 6. One sample of a loose body

### 6.1 Update (switch U)

1. Gravity: F.y = m·(−19.6) + F.y.
2. The rest detector (`car-collision.md` 6.4, thresholds 0.1 and 0.5).
3. Body integration (`vehicle-body.md`, "Integration", the explicit path; limits 2.2 step 4).
   While the body is frozen (the detector's state is not 1 after step 2), MM2 skips the motion
   part (that page's step 2) but still runs steps 1 and 3, so `lastPush` = `pushSum`, which the
   detector has just cleared, i.e. 0, and `pushSum` = 0. Integrating as usual gives the same
   result (everything it would use is 0). Skipping the whole step is wrong: `lastPush` would keep
   the value the detector's clearing gave it (`car-collision.md` 6.4 step 6). Steps 4 and 5 run
   every sample, frozen or not.
4. Its object's matrix := the body's rows and position.
5. Its room = Find(position, its room) (`city-collision.md`); if that changed, the object moves to
   the front of the new room's list.

### 6.2 City (switch C)

Its bound against the city polygons exactly as the car body (`collision.md`, "Body against the
city"; `collision-contacts.md`): rooms = its room and Touched(room, bound centre, bound radius, 8),
the previous matrix moved by `lastPush` when the previous sample's push source was the city, the
impacts resolved with A = the city and B = the loose body, its material (`Friction`,
`Elasticity` of its prop data) as the body's side. Box and geometry bounds are tested as polygon
bounds (a box as its 8 corners and 6 faces). A capsule or sphere bound (a prop with
`CollisionPrim` 2 or 3 and `NumParts` 0) is tested against the city as the box of its bounding box
(size max − min, offset (max − min)·0.5 + min). For a capsule (r = `YRadius`, R = `Size.y`·0.5 + r,
one rounding per operation) the bounding box is (−r, −R, −r) … (r, R, r), so the box is 2r × 2R × 2r
centred on the body position; for a sphere (radius `YRadius`) it is (−r, −r, −r) … (r, r, r). Only
the city test uses this box; the other tests are 6.3. Shipped: seven such props (`sp_parkmtr_f`,
`sp_bollard_stone_l`, `sp_treepalm2_f`, `sp_treepalm4_f`, `sp_fwsupport_f`, `sp_fwsupport_tee_s`,
`sp_cenotaph_charing_l`, all capsules); no shipped check run has one.

### 6.3 Other movers and gathered objects (switches V and O)

- **Against a later mover** (switch V, broad test `car-collision.md` 2.1 with the loose body's
  centre = its position and radius = its model radius): the pair test of `car-collision.md` 3: a
  car's polygon bound against a box or geometry bound: the polygonal method; two boxes: the box
  method (`car-collision-boxes.md`); each impact resolved with both bodies (`car-collision.md` 3.4),
  friction μ = f_B·f_A, elasticity e = e_B·e_A at most 1, with the loose body's material from its
  prop data. The player's car is the first mover, so a car always meets a loose body as side A.
- **Against a gathered object**, in gathering order (`collision-objects.md`, "Contacts"; the loose
  body as side A instead of the car):
  - a landmark or spanning object: the fixed-object response;
  - an untouched prop: it gets a body (2.1) and `collision.md` "Breakable props" decides, with
    side A = the loose body; a break follows 3;
  - a moved prop lying still: it gets a body (2.1), becomes a mover (5.3, lying still), and the
    impacts are resolved with both bodies (`car-collision.md` 3.4);
  - an object that got a body earlier in this sample (another mover's test): it is now a moving
    body and the test is a pair test as above.

  A car meets gathered objects the same way: an untouched prop with the breakable rule, a moved
  prop lying still with both bodies, as just described.

**Capsules in these tests.** The box of 6.2 is only for the city. When one side of a pair test or a
gathered-object test is a capsule (a capsule prop, untouched or a moved copy, with or without a
body), MM2 uses its capsule contacts:

- capsule against a polygon side (a car, a box or geometry body, a landmark or spanning object, a
  box or geometry prop): `collision-object-contacts.md` 5, with the polygon side in the car's role
  (its points, edges, polygons, matrix M and previous matrix M₀) and H the capsule's matrix. In
  5.2 step 5, w and l are taken with the capsule's **previous** matrix H₀: w = H₀.origin −
  M₀.origin, l = w in H₀'s coordinates, then q = l turned to world by H's rows, disp = d + q (H₀ =
  H for a capsule without a body). Both previous matrices are shifted by the push-source rule as in
  the other tests (`car-collision.md` 3.3, `collision-object-contacts.md`). The records then become impacts with A and B the two
  sides of the test as this section orders them, the normal pointing out of B into A, resolved as
  above for that kind of object (kind, part and feature do not matter);
- capsule against capsule, a sphere against anything, and a capsule against a terrain landmark
  (`.ter`): MM2 has its own tests for these, not specified. *Engine choice* until they are: the box
  of 6.2 for the capsule (or sphere) side.

No shipped check run has a capsule copy. (Using the box of 6.2 for every test of a capsule copy is
an acceptable interim engine choice; the rule above is MM2's.)

Every impact applied to a car is also offered to that car's damage (`damage.md`): a loose body,
an untouched prop's test body and a moved prop all have a rigid body, so the mass ratio applies
and the 10 mph rule does not.

### 6.4 End of the sample

As a physical traffic car (`car-collision.md` 6.3 step 6): the push is applied, then the previous
matrix is set to the body frame (so it includes the push), and this sample's push source becomes
the previous one. The object's matrix keeps the value from 6.1 step 4 (it does not include the
push); a moved prop that is let go keeps that matrix, and a body it gets later starts from it.

## 7. Removal

A loose body or moved prop goes away when:

- its slot is handed out again (1: the ring of 40);
- it is a first-kind mover outside the player's rooms at the start of a frame's physics (5.1):
  removed from its room;
- the pool drops it (5.1) or lets it go below y = −100 (5.2): it stays where it is, out of the
  simulation (in practice under the city);
- a race reset (also a check-run start and a world-limit reset): everything goes, and the untouched
  props come back.

A car's reset or "clear damage" (`damage.md`) also takes back the car's own thrown parts: each
thrown part's slot loses its body and leaves its room, and the part is attached again.

## 8. Glass panes

A prop whose data sets `BillFlags` 0x100 is a **glass pane** (`../formats/street-props.md`; no
shipped prop data sets it). It is tested and broken like any untouched prop (it gets a body for the
test, `collision.md` "Breakable props" decides). When it breaks:

- its body is let go, and it is neither collidable nor in place any more, but it **stays in its
  room** (it does not move and nothing replaces it); there is no loose body;
- the shard burst is set up for drawing: the point next to the pane on the side it was hit from
  (the pane's position minus its Z row if the hitting object's velocity v has
  (Z.x·v.x + Z.y·v.y) + Z.z·v.z > 0, else plus it), a vertical probe from there 100 m down (`collision.md`,
  "Ground queries", ground mask) gives the height the shards fall to (the probe's hit height, or
  100 m below if nothing is hit); at most two panes have bursts at a time, the third reusing the
  older one. The shards' arithmetic and their random draws (game-wide generator, at the break and
  then every drawn frame) are `../engine/effects.md`'s and `../formats/damage-visuals.md`'s.

A reset restores the pane.

## 9. Drawing (hand-off)

A moved prop is drawn with its own matrix (6.1 step 4) and its model and paint (2.2, 3, 4). For a
broken prop's copy, the matrix position is the centre of gravity, as for the untouched prop
(`collision-objects.md`, "Props"); for a piece likewise its own data's `CG` was added (3 step 2).
For a car part the matrix position is the part's place: it is drawn with its meshes as they are in
the car's package. A thrown car part is no longer drawn with its car (`../formats/car-model.md`).
Particles a prop emits when it gets loose are `../engine/effects.md`, "Props".

## 10. Things that look like mistakes (keep them)

- The ring of moved-prop slots hands out slot 0 twice in a row after its first round.
- A piece that comes to rest is still a full physical object; one that the player drives away from
  (beyond the neighbouring rooms) disappears, while one lying still stays until its slot is reused.
- Bodies resting on the ground hold a steady downward velocity (about 1 m/s for a pole or bumper
  lying flat): each contact impulse is solved for its own point and weighted by 1/N, so with
  contacts at both ends only part of the fall is stopped each sample and the pushes hold the body
  up; it still falls asleep when the push-corrected speed is small.
- Pieces of `sp_stop_f` (t = 4) collide only with the city and landmarks after their first frame,
  so the car drives through them while they move.
- The test body an untouched prop gets is the pool's next free body; its identity is shared by
  consecutive props (2.2).

## 11. Check runs

City check runs include all of this (`../engine/drive-check.md`); the flat world has no rooms, so a
car part thrown there is made in room 0: it takes its eight random draws and does not move or
collide. Trace records: `../engine/drive-trace.md`.
