# Moving level objects: boats, drawbridges, Underground trains, ferries, parked cars

Besides traffic, police and opponents, an MM2 city has a few objects that move on their own or
are scattered by the game: tugs, water taxis, sailboats, sailboards and ducks on the water,
drawbridge leaves (Tower Bridge and others in London, a gate in San Francisco that never moves),
three-car trains in London's Underground tunnels, car ferries, and parked cars along the kerbs.
MM2 calls them gizmos; each kind is read from its own path set. This page gives the files, what
each kind creates, how it moves, what it collides with and how it is drawn, and when it is created
and reset. San Francisco's cable cars run on the road network instead and are
`../ai/cable-cars.md` (with the network subway trains, which no shipped city has).

Related pages: path set layout and placement `../formats/pathset.md`; props, bounds and room lists
`../physics/collision-objects.md`; the breakable-prop rule `../physics/collision.md`; drawing
`../formats/inst.md`; the objects' sounds `../audio/world-sounds.md`; what multiplayer changes
`multiplayer.md`.

Arithmetic is float32 with one rounding per operation, left to right as written
(`../physics/vehicle.md`, "Precision"); where MM2 groups a sum differently the formula says so.
*F* is the frame time (`../physics/vehicle.md`, "Time step"). "Find(*p*, *h*)" is the room search
of `../physics/city-collision.md`. A **frame product** A·B is the one of
`../physics/vehicle-body.md`, "Products".

## The five kinds and their files

| Kind | Path set word | Default model | Created in |
|---|---|---|---|
| boats | `sailboat` | `giz_sailboat01_f` | every game |
| drawbridges | `bridge` | `giz_bridge01_l` | every game |
| Underground trains | `train` | `va_ug_l` | every game |
| ferries | `ferry` | `giz_carferry01_f` | single-player games only |
| parked cars | `parkedcar` | (`giz_pcar01_l`, `giz_pcar02_l`) | single-player games; in network games only Checkpoint, Circuit and Blitz (not Cruise, not Cops and Robbers) |

**Which file.** In folder `race/<city>/` (the city's race folder, `../formats/city-files.md`):

1. In any mode but Cruise: `<city>_<word>_<race>.pathset`, where `<race>` is the session's race name
   built from the mode (`race`*N*, `multicop`, `circuit`*N*, `blitz`*N*, `croam`, `crash`*N*,
   `../formats/aimap.md`, "Which race file"), if that file exists.
2. Otherwise (and always in Cruise): `<city>_<word>.pathset`.
3. If neither exists, that kind has no objects.

This is the same in single-player races and network games: e.g. a single-player Checkpoint race 0
in London reads `london_bridge_race0.pathset` and, for its other kinds, `london_sailboat`,
`london_train`, `london_ferry`, `london_parkedcar`; Crash Course 9 reads `london_ferry_crash9`.
Which kinds a game creates is the table's last column. (The race props `<race>.pathset` are a
separate path set, read with the city: `races.md`, "Path sets of a race".)

Names are matched without regard to case. No mode is called `multi`, so
`london_bridge_multi.pathset` is never read (Cops and Robbers asks for `london_bridge_multicop`,
which does not ship, and gets `london_bridge.pathset`, whose paths are the same).
`london_parkedcar_test.pathset` is never read either. A file is read with the layout of
`../formats/pathset.md` (including its rule for the damaged `london_bridge_blitz10.pathset`); a
file that cannot be read gives no objects of that kind.

Shipped:

| City | Kind | Cruise file | Race variants |
|---|---|---|---|
| London | boats | `london_sailboat` (16 paths) | — |
| London | drawbridges | `london_bridge` (4 paths, 7 leaves) | `race0`, `circuit0`, `blitz10`, `crash3`, `crash5` |
| London | trains | `london_train` (8 paths) | — |
| London | ferries | `london_ferry` (6 paths) | `crash9`, `crash11`, `crash12` (6 docked ferries each) |
| London | parked cars | `london_parkedcar` (91 paths) | `crash0`, `crash3` |
| SF | boats | `sf_sailboat` (16 paths) | — |
| SF | drawbridges | `sf_bridge` (1 path: `inactive:giz_chinagate_f`) | — |
| SF | trains | none | — |
| SF | ferries | `sf_ferry` (1 path, 30 points) | — |
| SF | parked cars | `sf_parkedcar` (55 paths) | — |

What each kind takes from a path: boats, drawbridges, trains and ferries use the path's name and
points (boats also its spacing); parked cars use the points, the placement mode and the spacing,
not the name. Every object is created whatever the path's placement mode, except as the parked-car
rule says.

## The kinds of object

| | Collides with car bodies | Wheels drive on it | Breaks | Matrix kept | Drawn |
|---|---|---|---|---|---|
| boat | no | no | no | full | room pass |
| drawbridge leaf | yes | yes | no | full | own rule ("Drawing by distance") |
| train car | yes | yes | no | full | room pass |
| ferry | yes | yes | no | position and turn only | own rule |
| parked car | yes | `giz_pcar02_l` only | yes | full | room pass |

- **Boats** are pure scenery: they are not collidable at all (not with car bodies, not with wheel
  probes or other probes, and the AI does not see them), even though their models have bounds.
- **Leaves, train cars and ferries** use the bound of their prop data
  (`tune/banger/<model>.dgBangerData`, `CollisionPrim`; `../physics/collision-objects.md`, "Props":
  a geometry bound is the `.bnd` moved by −CG, in coordinates of the object's stored matrix). They
  collide exactly as a landmark does: they are gathered from their room's list like any object, the
  trivial test is the plain sphere test with the model radius (never the `YRadius` cylinder), and
  each impact is resolved with the ordinary rule against an immovable object
  (`../physics/collision.md`, "Collision response"), never the breakable rule: they cannot be knocked
  over whatever the impulse. Wheels drive on them **whatever** their `CollisionType` (SF's gate has
  16). They are also solid for the other bodies that collide with landmarks (loose props, knocked
  traffic cars). They have no velocity: when a leaf rises or a ferry moves, a car touching it is
  moved only by the penetration push of the collision rule and by its wheels finding the new
  surface; a car standing on a ferry is not carried along (its tyres see a ground that does not
  move) and the ferry slides away under it until a wall of the deck pushes it.
- **Parked cars** are ordinary breakable props ("Parked cars" below).
- "Matrix kept": what the object stores and draws and collides with. A ferry keeps only its
  position and its turn about the vertical (X.x, X.z), like a compact `.inst` prop
  (`../formats/inst.md`); the others keep the full matrix.
- "Room pass": drawn as a moving object of its room, by the rule of breakable props
  (`../formats/inst.md`, "Drawing": only in rooms within the room limit, level by distance, missing
  levels filled from below). Leaves and ferries are not drawn in the room pass but by
  "Drawing by distance".

## Following a path

Boats, train cars and ferries move along a closed curve through their path's points
p₀ … p*ₙ*₋₁ (x, y, z of each point). A **follower** has a speed *s* (set per kind), a time *t*, a
current segment *i* (from p*ᵢ* to p*ⱼ*, *j* = *i* + 1, or 0 after the last point: the curve closes
from p*ₙ*₋₁ back to p₀), four coefficients per axis and a segment length *L*.

**Setting up segment *i***, with *h* = *i* − 1 (or *n* − 1 when *i* = 0), *j* as above and
*k* = *j* + 1 (or 0 when *j* = *n* − 1):

1. P0 = p*ᵢ*, P1 = p*ⱼ*; T0 = (P1 − p*ₕ*) · 0.5 and T1 = (p*ₖ* − P0) · 0.5 per component (the
   difference rounded, then the product).
2. Coefficients per axis (products by the constants rounded):
   - a = ((P1·(−2) + T0) + T1) + P0·2 for x and y, but a = ((T1 + T0) + P1·(−2)) + P0·2 for z;
   - b = ((−T1 + T0·(−2)) + P1·3) + P0·(−3);
   - c = T0; d = P0.
3. M = the curve point at *u* = 0.5 (below); *L* = |M − P1| + |P0 − M| with
   |v| = √((v.z² + v.y²) + v.x²).

The curve point and direction at parameter *u*, per axis:

```
point(u)     = ((u·a + b)·u + c)·u + d
direction(u) = ((u + u)·b + ((u·u)·3)·a) + c
```

This is a Catmull-Rom curve: it passes through every point, its tangent at a point is half the
chord between the neighbouring points, and *L* (two chords through the segment's middle) only
approximates the arc length.

**Restart** (creation and reset): *t* = 0, segment 0 set up (with *h* = *n* − 1).

**Update by a time step** τ (negative steps occur for trains):

1. *t* = τ + *t*; *u* = (*s*·*t*) / *L*.
2. If the path has exactly 2 points: the point is p₀ and the direction p₁ − p₀ (per component);
   stop (the follower stands still).
3. If *u* > 1: set up the next segment (*i* := *j*), *u* = *u* − 1, *t* = (*u*·*L*)/*s* with the new
   segment's *L*.
4. If *u* < 0 (checked after step 3): set up the previous segment (*i* := *i* − 1, or *n* − 1 when
   *i* = 0), *u* = *u* + 1, *t* = (*u*·*L*)/*s* with that segment's *L*.
5. The point and direction at *u*.

At most one segment change happens per update; the remainder carried into the new segment is a
fraction of the old segment, applied to the new one (a quirk: keep it). A path with fewer than
2 points makes MM2 divide by a zero length (none ships; reject such a path). Each segment takes
*L*/*s* seconds, so the follower covers about *s* metres of curve per second (exactly *s* only where
*L* equals the arc length).

**Matrix from a direction** D: start from rows X = (1, 0, 0), Y = (0, 1, 0), Z = D and renormalise
as for spanning objects (`../physics/collision-objects.md`, "Static objects spanning rooms"):
X′ = normalise(Y × Z) with Y × Z = (Z.z·Y.y − Z.y·Y.z, Z.x·Y.z − Z.z·Y.x, Z.y·Y.x − Y.y·Z.x) and
squared length (X.z² + X.y²) + X.x²; Y′ = normalise(Z × X′) with
Z × X′ = (X′.z·Z.y − X′.y·Z.z, X′.x·Z.z − X′.z·Z.x, X′.y·Z.x − X′.x·Z.y) and squared length
(x² + y²) + z²; Z′ = normalise(Z), squared length (x² + y²) + z². Each normalise multiplies by
1/√(squared length), or by 0 when it is 0. The object's +Z points along the direction of the curve
(the way the points run), whichever way it is moving.

**Lift.** Boats, ferries and train cars add their prop data's `CG.y` to the position's y
(`tune/banger/<model>.dgBangerData`; for boats too, although boats use nothing else of it), so the
stored origin sits `CG.y` above the curve (only the y of the CG is applied, not its x and z).

## Boats

One boat per path of the boat file, in path order.

- **Model**: the path's name if `geometry/<name>.pkg` exists, else `giz_sailboat01_f` (SF's paths
  `PATH03` … `PATH06` get it). Its `H`/`M`/`L`/`VL` meshes, no bound.
- **Paint**: a random integer *x* (15 bits) at creation; material set *x* mod (the package's number
  of material sets).
- **Speed**: *s* = ((σ + 1) − (σ − 1))·*r* + (σ − 1), with σ the path's spacing
  (`../formats/pathset.md`: byte × 0.25, 5 if 0) and *r* a random fraction, i.e. evenly between
  σ − 1 and σ + 1 m/s. Shipped: London tugs and water taxis σ = 5, ducks 2 (one path 5); SF
  sailboards 30 or 20, sailboats 5, ducks 2.
- **Created**: follower restarted with speed 4, updated by 0, matrix from the direction, position
  = point + lift; filed in Find(position, 0). Then the speed above replaces 4.
- **Every frame** (not while the game is paused): update by *F*, matrix from the direction, position
  = point + lift, full matrix stored; room = Find(position, current room), moved there if different.
- **Reset**: the follower restarts; the matrix follows at the next frame.

## Drawbridges

### Leaves

Each path of the bridge file makes one **leaf**, or two if it has more than 2 points:

- 2 points: a leaf at p₀ facing p₁.
- more than 2 points: a leaf at p₀ facing p₂, then a second leaf at p₂ facing p₀ (p₁ and any later
  points are not used); the two are **partners**.
- fewer than 2 points: undefined in MM2 (it reads past the points); reject.

**Model.** If the name contains `:`, the part after the first `:` if `geometry/<it>.pkg` exists;
otherwise the name up to the first `:` (the whole name if there is none) if that package exists;
otherwise the default `giz_bridge01_l`. If `geometry/giz_bridge01_l.pkg` itself does not exist, the
city gets no leaves at all. Both leaves of a path use the same model.

**Mode**, from the first four characters of the name up to its first `:`, compared without regard to
case: `inac` → inactive (never moves, closed), `time` → timed, `prox` → proximity, `open` → open
(raised for good); anything else → timed. Shipped: London `giz_bridge01_l` (Tower Bridge, two
leaves), two two-leaf `giz_waterloo_l` bridges and one single `open:giz_bridge02_l`; race variants
add `OPEN:` and `inactive:`; SF's `inactive:giz_chinagate_f` is a gate made of two leaves that never
move. No shipped name uses `prox` or `time`.

**Placement** of a leaf at P facing Q (H = the hinge point P):

1. Z = P − Q per component; *k* = 1/√((Z.x² + Z.y²) + Z.z²) (0 if that is 0); Z = Z·*k*.
2. X = U × Z with U = (0, 1, 0), as (U.y·Z.z − U.z·Z.y, U.z·Z.x − U.x·Z.z, U.x·Z.y − U.y·Z.x).
3. Y = Z × X as (Z.y·X.z − X.y·Z.z, X.x·Z.z − Z.x·X.z, X.y·Z.x − X.x·Z.y).
4. Placement = rows X, Y, Z, origin H (X and Y are not normalised: a sloping P–Q shortens them).

So the placement's −Z points from the hinge toward the facing point.

**Leaf matrix** at angle α, with *h* = `Size.z`·0.5 of the leaf's prop data:

1. If α = 0, B = rows (1, 0, 0), (0, 1, 0), (0, 0, 1), origin (0, 0, 0 − *h*). Otherwise, with
   *c* = float(cos α) and *s* = float(sin α) (computed in double, rounded once each),
   B = rows (1, 0, 0), (0, *c*, *s*), (0, −*s*, *c*) and origin (0, *h*·*s*, −(*h*·*c*)) (each product
   rounded once).
2. The leaf matrix is the frame product B·Placement, then its origin is lowered: origin.y =
   −0.3 + origin.y (x and z unchanged).

The leaf model is centred on its own origin with its length along Z, so this puts the leaf's +Z end
at the hinge, lying from the hinge toward the facing point, turned up by α about its X axis
through the hinge, 0.3 m below the path height. Two leaves of a path meet in the middle
(`giz_bridge01_l` is 30 m long; Tower Bridge's points are 60 m apart).

The stored matrix is the leaf matrix itself (no CG offset); the bound is placed with it.

### Movement

Each leaf has a state, a timer and an angle. Constants: open angle Ω = 0.4712389 rad (27°), turning
rate 0.05 rad/s, both waits 10 s. **Approach**(v, target, step): if v ≥ target: if v > target then
v = v − step, and if v < target then v = target; otherwise v = step + v, and if v > target then
v = target.

**Reset** (creation and every race reset): state *closed*, timer 0, angle = Ω for open leaves, else
0; leaf matrix from the angle. At creation the leaf is reset before its mode is known, so an open
leaf starts closed and is raised by the reset MM2 makes before the first frame of every game.

**Every frame** (not while paused), for each leaf in creation order:

1. A proximity leaf that is *closed*: if any registered car is close (below), it is triggered:
   state *opening*, timer 0, and the same for its partner if that one is *closed*.
2. Room = Find(leaf origin, current room); moved there if different.
3. By state:
   - *closed*: if timed: timer = *F* + timer; if timer > 10: timer 0, state *opening*. (Other modes
     stay closed; an open leaf stays at Ω in this state forever.) Matrix unchanged.
   - *open*: timer = *F* + timer; if timer > 10: timer 0, state *closing*. Matrix unchanged.
   - *closing*: angle = Approach(angle, 0, 0.05·*F*); if the angle is 0: timer 0, state *closed*.
     New leaf matrix.
   - *opening*: angle = Approach(angle, Ω, *F*·0.05); if the angle is Ω: timer 0, state *open*. New
     leaf matrix.

So a timed bridge waits 10 s closed, opens for 9.42 s, stays open 10 s, closes for 9.42 s
(38.85 s a cycle), all leaves of the city in step since they start together at every reset.
The sound rows start when a leaf starts moving and stop when it arrives (`../audio/world-sounds.md`).

**Proximity.** The cars registered when the bridges are created: the player's car, then each
opponent existing at that moment (in MM2 none: opponents are created later, with the road network).
A car is close when ((b.z − o.z)² + (b.y − o.y)²) + (b.x − o.x)² < 10000 (100 m), b = the car's
body position (centre of mass), o = the leaf origin.

**Network games in London**: every leaf is made open and reset (`multiplayer.md`).

### Rooms marked as drawbridge rooms

At creation each leaf marks two rooms: Find(H, 0) and Find(H + (0, 5, 0), 0) (H the hinge). The
marks stay for the session, whatever the mode, and only the skid marks read them: no skid marks are
laid by a car whose room is marked (`../engine/effects.md`, "Skid marks"). Shipped: London rooms
333, 857, 939, 970, 971; SF 34, 35.

### The AI and the bridges

Nothing in the traffic, police or opponent logic knows about bridges: traffic does not wait for a
raised leaf. Ambient cars' ground probes can hit leaves like any surface flagged for wheels
(`../ai/traffic-visuals.md`), and the road-network load registers leaves as props by the rules of
`../ai/road-network.md`, "Obstacles", in the room each is in at that moment (closed), although
they are not breakable ("AI obstacles" below): London's leaves have `CollisionType` 48 and none
lies in an intersection's room, so none is registered; SF's two gate leaves (`CollisionType` 16)
are.

## Underground trains

One train of three cars per path of the train file; model = the path's name if its package exists,
else `va_ug_l` (all shipped paths are named `PATH0`… and get `va_ug_l`, a 15 m car). Each car has
its own follower on the path, speed 40.

**Moving car *k* by a step τ**: update its follower by τ; with the follower's current segment
from p*ᵢ* to p*ⱼ*: *y*₀ = p*ᵢ*.y, Δ = p*ⱼ*.y − *y*₀, *u*ₕ = (40·*t*)/*L* recomputed after the update
(after a segment change it may differ in the last bit from the *u* the point was taken at);
height *y* = *u*ₕ·Δ + *y*₀ (a straight slope between the points, not the curve's height);
direction (D.x, Δ, D.z) with D the curve direction (its y replaced by Δ); matrix from that
direction; position (point.x, *y* + `CG.y`, point.z); full matrix stored; room = Find(position,
current room), moved there if different.

**Train state**: state, direction (forward or backward), timer, factor *f*.

**Reset** (creation and every race reset): state *waiting*, timer 0, *f* = 1, forward; for
*k* = 0, 1, 2: car *k*'s follower restarts and the car is moved by *k*·0.44 (*k* converted to
float). So the cars stand about 17.6 m of curve apart, car 2 in front when going forward.

**Every frame** (not while paused), with σ = *F* when forward and *F*·(−1) when backward:

- *waiting*: timer = *F* + timer; if timer > 10: timer 0, state *accelerating* (no move).
- *running*: move cars 0, 1, 2 by σ; then if the train is **in a station**, state *braking*.
- *braking*: *f* = Approach(*f*, 0, 0.51·*F*); if *f* = 0: state *waiting* and the direction flips;
  otherwise move cars 0, 1, 2 by σ·*f*.
- *accelerating*: *f* = Approach(*f*, 1, 0.51·*F*); if *f* = 1: state *running* (no move this frame);
  otherwise move cars 0, 1, 2 by σ·*f*.

**In a station**: forward, car 0's current segment is at least *n* − 3; backward, car 2's current
segment is below 2. So the train brakes when its last car enters a station stretch (going forward
segments *n* − 3 and *n* − 2, the last two; going backward segments 1 and 0), comes to rest about
39 m later after 1.96 s, waits 10 s and returns. The closing segment of the curve (from the last point back to the first) is never
reached on the shipped paths.

The train's sound sits at car 1 (`../audio/world-sounds.md`). Cars are drawn in the room pass with
material set 0.

## Ferries

One ferry per path of the ferry file; model = the path's name if its package exists, else
`giz_carferry01_f` (every shipped path, in both cities, is named `giz_carferry01_l`; the SF default
has only an `L` mesh).

- **Speed**: ((0.75 + 0) − (0.75 − 0))·*r* + (0.75 − 0) with a random fraction *r* drawn for each
  ferry after all ferries are created, in order: always 0.75 m/s (MM2's variation is 0).
- **Created**: follower restarted with speed 1, updated by 0, matrix from the direction; the ferry
  is created at the point plus R·CG like a prop (`../physics/collision-objects.md`, "Props"), which
  lasts only until the first frame.
- **Every frame** (not while paused): update by *F*; matrix from the direction; position = point +
  lift; it keeps position, X.x and X.z; room = Find(position, current room).
- **Two-point paths** stand still at p₀, facing along p₁ − p₀ (London `london_ferry` paths 0 and 3,
  and all six ferries of `crash9`, `crash11`, `crash12`); the model's centre is at p₀.
- **Reset**: the follower restarts; room re-found; the matrix follows at the next frame.

## Parked cars

For each path of the parked-car file, in order, with spacing σ′ = the path's spacing, or 5 if that
is below 5 (only `london_parkedcar_crash0` path 11 and the empty paths would differ): the
placements of `../formats/pathset.md`, "Placing a path's objects", with σ′ and the path's mode, in
their order. For each placement (axes X, Y, Z, point P):

1. A random integer *x* (15 bits). If *x* mod 3 = 0 the slot stays **empty**. Otherwise the model is
   `giz_pcar01_l` (*x* mod 3 = 1) or `giz_pcar02_l` (2); if its prop data is missing, nothing.
2. The axes are turned a quarter turn about the world vertical: with *c* = float(cos 1.5707964) =
   −4.371139e−8 and *s* = float(sin 1.5707964) = 1, each axis v becomes
   (v.x·*c* + v.z·*s*, v.y, v.x·(−*s*) + v.z·*c*) (each product rounded, then the sum). So the car's
   Z (length) axis runs along the path and its X axis across it. On a sloping path the slope stays in
   the turned X axis (MM2 keeps the path's tilted X axis and turns it), so the car is rolled
   sideways by the slope instead of pitched (up to 3.8° in London, 0.3° in SF).
3. The car is a breakable prop with the **full** turned matrix (whatever the path's mode) at
   P + R·CG (`../physics/collision-objects.md`, "Props").
4. Paint: a random integer, modulo the package's material sets (9 for both models).
5. Room: Find(P, 0) (the placement point, not the CG), at the front of the room's list.

From then on a parked car is an ordinary prop: it collides with its prop data's bound (both models
a geometry bound), wheels drive on `giz_pcar02_l` (`CollisionType` 48) but not on `giz_pcar01_l`,
the trivial test uses the `YRadius` cylinder, impacts use the breakable rule
(`../physics/collision.md`, "Breakable props") with `ImpulseLimit2` 8,000,000 (`giz_pcar01_l`,
broken by about 2,800 N·s, a family car at 2 m/s) or 363,904 (`giz_pcar02_l`, about 600 N·s).
A broken parked car turns into its break-off parts as loose props (`NumParts`: `giz_pcar01_l`
`BREAK01` the body and `BREAK02` a wheel, `giz_pcar02_l` `BREAK01`; their prop data
`giz_pcar0N_l_BREAK0M.dgBangerData`): the loose-prop physics are the page on props after they break
(`../ROADMAP.md`, milestone 5). Parked cars are registered as AI obstacles when the road network
loads (`../ai/road-network.md`, "Obstacles": `giz_pcar01_l` only, by its `CollisionType`), are drawn
like props with their paint, and come back untouched at every reset like all props. The manager
itself does nothing per frame.

## Creation order, frames, resets

**Creation**, once per game, after the city (street props, `.inst` objects, path-set props, decals,
race path set: `../formats/street-props.md`, "Creation order") and the player's car, and before the
road network (traffic-light poles, AI obstacles, opponents, traffic, cable cars, police): boats,
then drawbridge leaves, trains, ferries, parked cars, each kind in path order.

**Room lists** (`../physics/collision-objects.md`, "Room lists"): all of them are moving objects:
each is filed at the front of its room's list when it is created and whenever it changes room.
Parked cars belong with the props: a race reset takes every prop and pole out of its room and puts
it back (so they end up in front of the other level objects in the same room), while leaves, train
cars, ferries and boats stay where they are in their lists unless a reset moves them to another
room. After the city's own objects, the room lists therefore hold, in creation order, the parked
cars between the race path-set props and the traffic-light poles.

**AI obstacles.** The road-network load (right after the level objects) registers obstacles by the
rules of `../ai/road-network.md`, "Obstacles". Those walks take every prop object (an object that
collides through its prop data), breakable or not, so besides the props (parked cars among them)
they also take the drawbridge leaves, the Underground train cars and the ferries, at their
positions at that moment (leaves closed, trains after their reset placement); not the boats,
which are scenery and not prop objects. Shipped
Cruise results: SF's two gate leaves on road 33, section 1, sides −1 and +1; London: 15 of the 24
train cars (`va_ug_l`, `CollisionType` 16, in the rooms of the tunnel roads) on road sections
(road 21 sections 1, 2, 6, 7, 11 and 13; roads 147, 162, 224, 225, 229, 243, 336, 391) and cars
1 and 2 of train 0 (`ImpulseLimit2` 1e30) at intersection 59; no leaf and no ferry
(`CollisionType` 48, none in an intersection's room). A registration keeps the object, so the
planners and pedestrians that read these lists test a train car where it is at that time.

**Each frame**, the level objects move once (not per physics step), after the AI update (traffic,
police, opponents, cable cars) and before the frame's physics steps, in the order boats, leaves,
trains, ferries (parked cars do nothing). While the game is paused nothing moves. The ferries and
leaves are drawn by "Drawing by distance"; everything else is drawn in the room pass. (*Engine*:
the viewer, with no drive running, moves them by its own frame time.)

**Race reset** (every race start and the reset key): props (with the parked cars) first, then the
AI (with the cable cars), then the level objects in creation order: boats, leaves, trains, ferries.

## Drawing by distance (leaves and ferries)

Leaves and ferries are skipped by the room pass. Instead, each frame, every leaf and ferry is drawn
at a level chosen by its straight-line distance *d* from the camera (the eye point) to its
**reference point** (a leaf's hinge H, a ferry's position), with D = 200 m:

| *d* | Level |
|---|---|
| below D | `H` |
| D to below 2D | `M` |
| 2D to below 3D | `L` |
| 3D to 4D (800 m) | `VL` |
| beyond 4D | not drawn |

whatever the rooms that are visible, with material set 0 and the rest of the prop drawing rules
(`../formats/inst.md`; `BillFlags`, lighting, missing levels filled from below: `giz_bridge02_l` and
SF's gate have no `VL`, so they disappear beyond 600 m).

## Randomness

MM2 draws from its game-wide generator (`../physics/vehicle-wheels.md`, "Random generator"):

| Kind | Draws, in order |
|---|---|
| boats, per boat in path order | paint (integer), speed (fraction) |
| drawbridges, trains | none |
| ferries, after all are created, per ferry | speed (fraction) |
| parked cars, per placement | the slot (integer); then, for a car, its paint (integer) |

The generator is not restarted for them. When the boats are created it holds whatever the loading
left: the street props restart it at 1 for every AI road (`../formats/street-props.md`), so after
the city it holds the state reached by the last AI road's draws; the player car's set-up then draws
192 numbers (its water-splash particles), and nothing else draws in between as far as the code
shows. **Engine**: start the level objects' own generator of the same kind from that state (in
practice from the street props' final state, then 192 draws), so boats' paints and speeds and the
parked cars come out as in MM2. The values for the shipped cities are in "Checked against".

## Other moving things

MM2 has no helicopters, blimps, planes, birds or other flying objects (no models, no code). Besides
this page and `../ai/cable-cars.md`, what moves is traffic, police, opponents, pedestrians, props
knocked loose, particles and the cloud-shadow texture. Some race path sets place gizmo models as
ordinary props (`giz_carferry01_l` in `crash3`, `giz_pcar01_l`/`giz_pcar03_l` in several crash
courses): those are props of `../formats/pathset.md` and never move.

## Checked against

`tools/game/level_objects.py` (dirty side) reads every gizmo path set, model and prop data file and
builds every object of both cities' Cruise sets by this page (`survey`), writes them with plots
(`place <city>` → `work/check/level_objects_<city>.csv`, `level_objects_<city>.png`: water paths blue
(boats), yellow (ferries), magenta (trains), leaves red, parked cars green/white, SF cable lines
orange with the cable-car starts) and simulates bridges and trains at *F* = 1/60 (`sim`).

- Files: London's 16 gizmo files are all read by some mode except `london_bridge_multi` and
  `london_parkedcar_test`; SF's 4 are read in every mode (no variants).
- Models (all have prop data, a package and a bound): `giz_bridge01_l` 30 m, `Size.z` 30, CG 0,
  type 48, limit 1e30; `giz_waterloo_l` 23.18 m, CG z 11.59; `giz_bridge02_l` 20 m, CG
  (−0.03, −0.5, −10), no `VL`; `giz_chinagate_f` type 16, no `VL`; `giz_carferry01_l` 91.5 m,
  CG (−10.25, 5.42, 45.45), mass 29,993,656; `giz_carferry01_f` only `L`; `va_ug_l` CG.y 1.75,
  15 m; `giz_pcar01_l`/`02_l` 9 paints, CG.y 0.657 / 0.755, `NumParts` 2 / 1; boats: `giz_tug01_l`,
  `giz_watertaxi01_l` (1 paint), `giz_sailboat01_f`, `giz_sailboard01_f` (5 paints, only `L`/`VL`),
  `giz_duck_s`. In every case `.bnd` − CG has the extent of the model's meshes, so collision matches
  what is drawn (e.g. `giz_bridge02_l` bound z −20…0 − (−10) = −10…10, mesh −10…10).
- London leaves (Cruise), hinge → leaf centre (closed) and free-end height closed/open:
  Tower Bridge leaf 0 (1247.1, 8.00, −148.5) → (1242.95, 7.70, −134.11), 7.70 / 21.32 m; leaf 1 at
  (1230.5, 8.00, −90.9); `giz_waterloo_l` pairs at (677.1, 4.85, −223.7)/(697.0, 4.85, −181.8)
  (free end 4.55 / 15.07) and (499.8, 0, 618.5)/(543.7, 0, 632.7); `open:giz_bridge02_l` at
  (1165.2, 1.0, −302.9), raised (free end 9.05). Marked rooms 333, 857, 939, 970, 971. SF gate:
  two leaves at (−758.5, 44, 30.3) and (−734.8, 44, 29.3), rooms 34, 35.
- Bridge cycle at *F* = 1/60: opens on frame 600, open on 1166, closes on 1766, closed on 2332,
  opens again on 2932 (2,332 frames a cycle).
- Trains (London, 8): first stops forward after 16.9–33.5 s, then every 16.6–31.5 s at alternate
  ends (e.g. train 0, 9 points: stops at 27.37 s with car 0 in segment 6, 52.70 s back in segment 1,
  then 78.02 s); no train reaches the closing segment.
- AI obstacles (`obstacles <city>`, after `place`): SF 2 objects, 2 registrations (the gate leaves,
  road 33 section 1); London 17 train cars, 23 registrations (21 on road sections, 2 at
  intersection 59); no London leaf and no ferry in either city.
- Ferries: London 4 moving (loops of 477–815 m, 636–1,087 s a lap at 0.75 m/s) and 2 docked; SF one
  ferry on a 5,392 m loop (2 hours a lap). Boats: London loops 21–1,036 m, SF sailboards
  246–1,042 m at 19.6–30.5 m/s. (SF's boat paths spell "CAS" on the bay, see the plot.)
- Generator state at the first gizmo draw (street props' final state, then 192 draws): London
  0x3a2d7dc1, SF 0xef3c16d1 (not verified against the running game). With it: London 730 parked-car
  slots, 234 empty, 258 `giz_pcar01_l`, 238 `giz_pcar02_l`; SF 138, 28, 62, 48; none in room 0. First
  London car: path 0, `giz_pcar02_l` paint 8, P (−1359.612, 10.040, −612.549), X
  (−0.72682, 0.02803, 0.68626), Z (−0.68626, 0, −0.72682), stored position (−1359.597, 10.794,
  −612.564), room 427. First SF car: `giz_pcar01_l` paint 5 at (−137.185, 13.044, 450.446), room 95.
  Gizmo draws: London 1,264, SF 281.
