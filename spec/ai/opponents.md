# Race opponents: who they are, their cars, how they drive, finish and drop out

The **opponents** are the computer racers of the Checkpoint, Circuit and Crash Course modes. Each
one is a full car (the same simulation as the player's) driven by the AI driver (`driver.md`, with
its planner `driver-planner.md`) along a fixed list of intersections read from its route file
(`../formats/opp.md`). This page covers which opponents exist, their cars and start points, the
orders they drive with, their per-frame update, the finish-line test, what happens when they are
wrecked, drown or fall out of the world, their physics status, how the police and traffic deal
with them, and random numbers. The race itself (gates, laps counted by the race, standings,
results, countdown, timers) is the race-modes spec; "Interface with the race" below lists what
each side gives the other.

Units, precision and comparisons as in `driver.md` (float32, one rounding per operation, in the
order written; `atan2`, `sin`, `cos` in double). "Position" of a car is its body (rigid-body)
position, "rows" its body rotation rows (`../physics/vehicle-body.md`); the car faces −Z.

## The short version

- The race file `race/<RaceDir>/<race>.aimap` (`.aimap_p` at Professional) lists the opponents
  (`[Opponent]`: car, route file, four driving numbers, six switches; `../formats/aimap.md`). The
  race data's opponent number limits how many are created.
- An opponent starts at the first point of its route file, dropped onto the ground once when the
  race is set up, and drives the intersections of the file's middle points, then to its last
  point, where it stops. Its route is never searched: it is the file's list.
- Its speed is set only by its own numbers (throttle level, corner factor, brake threshold,
  planning distance). There is **no rubber band and no catch-up**: nothing reads the standings or
  the player's position to change how an opponent drives (the police have a catch-up, opponents do
  not).
- It avoids traffic, props, the player and the other opponents as its switches say (the other
  opponents only after it has passed two route intersections), never police cars.
- It is never put back on the road: not when stuck, lost, wrecked or in the water. In Circuit a
  wrecked opponent is repaired after 5 s and drives on; in the other modes it stays wrecked. One
  that falls below y = −200 is switched off.
- It is always simulated, at any distance from the player.
- Its finish is a line test near the race's finish point, made by the race logic each frame; after
  finishing it drives on to the end of its route file and stops there.

## Which opponents exist

At the AI set-up of a race (after the road network is loaded):

1. *c* = the `[Opponent]` count line of the race aimap (0 if the file or the section is missing).
2. *s* = the session's **opponent number** (a float; the race-data value for this race and skill
   level, race-modes spec).
3. Number of opponents *m* = trunc(*c* < *s* ? *c* : *s*), with *c* converted to float and the
   comparison made in float (a NaN *s* keeps *c*).
4. Opponent *i* (*i* = 0 … *m* − 1) uses row *i* of the list. *i* is its **index**: its paint, its
   racer number in the planner's opponent test, the order of updates, the race's results.

MM2 creates *m* opponents even when fewer rows were read (a `[` line ended the list early, or the
file ended): the missing rows are undefined. **Engine:** create only opponents whose row was read.
No shipped file has a count line that differs from its rows.

Creation, for *i* = 0, 1, … in order: the opponent's car and driver are created (`driver.md`,
"Creation and reset", including the driver's first reset), then its route file is read
(`../formats/opp.md`): the start point and heading become the car's **reset point** *q* and
**reset heading** θ, the destination *D* and the route *W*[0 … *n* − 1] are stored. The car is
placed only at the following world reset.

## The opponent's car

| Item | Rule |
|---|---|
| Model | the row's car name (`geometry/<car>.pkg`; `../formats/car-model.md`) |
| Paint | colour number *c* = *i* mod 4 (for a `vpcop`: the police rule of `police.md`, "The cop's car": 0 if the session has cable cars, else 1); the paint set is then chosen by the car-model rule (*c* modulo the number of sets) |
| Physics | the full car simulation with the car's own tune files (`../physics/car-collision.md` §5): `tune/vehicle/<car>.vehCarSim`, `.vehGyro`, `.vehStuck`, `.vehCarDamage`. The `<car>_opp.vehcarsim` files are never read (`../physics/vehicle-tune.md`) |
| Bound | the box of the car's `.bnd` (`car-collision.md` 3.1), with the car's `BoundFriction` and `BoundElasticity`; a model the player's car loaded first keeps the player's polygon bound |
| Unstuck helper | the AI overrides: `Rotation` 0, `TimeThresh` 0.5, squared position threshold 1.0 (`driver.md`, "Creation") |
| Regenerate | on in Circuit (game mode 3), off in every other mode |
| Car kind, extents | as `driver.md` ("Driver state"); *front* = −min.z of the bound's box is also used by the finish test |
| Trailer | attached as for any car with a hitch (`car-model.md`, "Trailers"), but MM2 never declares an opponent's trailer as a mover, so it is not simulated; no shipped opponent drives a car with a trailer |
| Sound | the opponent sound set of the car (sound spec) |
| Damage | the car's `.vehCarDamage`; damage counting is switched off and on by the race (before the start and at "go", race-modes spec) |

The car is drivable from creation; the race switches it off and on around the countdown.

### Start position

1. **First placement.** At the world reset that follows the AI set-up the car is placed at *q*
   with heading θ by MM2's placement rule (`../physics/vehicle.md`, "Placing the car in a city"):
   body position *q* + CoG, rotation by θ about +Y, at rest.
2. **Ground snap**, once, when the game sets up its players after that reset (the same step
   places the player's car, race-modes spec): with *M* = the car's model origin after step 1
   (body position + R·CoG, `vehicle.md`, "Centre of mass"), probe the segment from
   (M.x, M.y + 2.0, M.z) down to (M.x, M.y − 10.0, M.z) (`../physics/collision.md`, "The query":
   a fresh state with room hints 0 and an empty polygon cache, the wheels' ground mask, ignoring
   nothing). On a hit at point *H*: *q* := (H.x, H.y + 0.9, H.z) (0.9 = 0.89999998) and the car
   is placed again by the rule of step 1 with the same θ. On no hit *q* stays.
3. Every later world reset (race start, restart) places the car at the current *q* with θ.

The probe starts from the model origin but the new reset point is again offset by CoG, so after
the snap the body sits horizontally at *q* + 2·CoG + R·CoG (x and z) instead of *q* + CoG: for a
car whose `CenterOfGravity` has a non-zero z (most shipped opponent cars, up to 0.4 m) the start
moves along the car by up to two such offsets. MM2 does this; reproduce.

## Orders

On its first update after every world reset (and only then) the opponent gives its driver one
set of orders (`driver.md`, "Orders" and "Receiving orders"):

| Order | Value |
|---|---|
| route | *W*[0 … *n* − 1] from the route file (never a route search) |
| destination, direction | *D*; direction (0, 0, 0) |
| laps | Circuit: the session's lap count; every other mode: 1 |
| arrival speed *s*₁, stop distance *s*₂ | 0.0, 0.0 |
| switches 1 … 6 | the row's switches in their order (`aimap.md`): 1 unused, 2 avoid traffic, 3 avoid props, 4 avoid players, 5 avoid opponents, 6 path preference |
| *k*_thr, *k*_c, *k*_b, *k*_p | the row's throttle level (field 3), corner factor (field 12), brake threshold (field 6), pad (field 5) |

Right after the orders, a one-off **AI-ahead mark** on the first route intersection: for every
road *Q* meeting *W*[0], if *Q*'s traffic control at its end that is *W*[0] is a stop sign (0) or a
traffic light (1), *Q*'s AI-ahead mark := 1 (the "set" form of `driver.md`, "Yield marks"). It is
cleared only as `driver.md` says (a clearing pass of a driver whose window leads to *W*[0], or a
road reset). **Engine:** with *n* = 0 there is no *W*[0]: skip the mark (MM2 reads an undefined
entry).

The orders are never changed during the race: no later code writes an opponent's numbers or
route.

## One frame

The opponents are updated in the AI update of every frame (`traffic.md`, "Frame order", step
1.4), in index order, after the yield marks of every AI driver have been cleared and before the
other AI racers and the police (`police.md`, "One frame of police"). An opponent has a state,
RACING or DISABLED, and a previous state (none after a reset). One update:

1. **RACING**:
   1. If the previous state is not RACING: give the orders above (with the mark); previous :=
      RACING.
   2. Run the driver's frame (`driver.md`, "One frame").
   3. If the car's position y < −200.0 (a NaN counts as below): state := DISABLED (MM2 logs that
      the opponent fell through the geometry).
2. **DISABLED**: if the previous state is not DISABLED: the driver's mode := HOLD; previous :=
   DISABLED. Nothing else: the driver's frame is no longer run, so the car keeps the inputs it was
   last given (HOLD never gets to run).

   Steps 1 and 2 are one choice made on the state at the start of the update (any other value:
   neither): a car that becomes DISABLED in step 1.3 is put in HOLD by the next frame's update,
   not in the same one.
3. **Physics status** (both states): *d*² = the smallest over all players of
   `(Δz·Δz + Δy·Δy) + Δx·Δx`, Δ = the opponent's position − the player car's position, starting
   from 9999999.0 and replacing only on a strictly smaller value (a NaN value replaces, and then
   every later value does). If there is a player and *d*² < 40000.0 (200 m; a NaN counts as
   below), the opponent is declared a mover of the **room-waking** kind; otherwise of the ordinary
   kind. Both have all four switches (U C V O, `car-collision.md` 1.1, row 4): an opponent is
   always simulated, whatever its distance. The room-waking kind also adds the opponent's room
   and its neighbours to the rooms that keep loose bodies alive (`../physics/loose-bodies.md`
   5.1, "The player's rooms": the same list, after the player's car's rooms, at most 20 in all).
   The rule is the player's: the entries are the opponent's room, then its neighbours in
   perimeter order; one "already listed" flag, set by the first entry found in the list (the room
   itself or a neighbour), stops the adding for all of this opponent's later entries (so an
   opponent in a room already listed adds nothing); an entry is added only while the list has
   fewer than 20 rooms.

**World reset** (race start and every restart; `police.md`, "Parking (reset)" for the order of the
world reset): state := RACING, previous := none, then the driver's reset (`driver.md`, "Creation
and reset"; it places the car at *q* with θ). The finish side value *S* (below) is **not** reset.

## How an opponent drives

Everything is the AI driver's (`driver.md`) and planner's (`driver-planner.md`) behaviour with the
orders above. What that means for a racer:

- **Following the race.** The opponent knows nothing of the race's gates: it drives through the
  intersections of its route in order (route progress advances *w* each time it reaches *W*[*w*])
  and then straight on toward *D*. The route files are made so that this passes the race's
  checkpoints. In Circuit, when it reaches the last entry before its last lap, *w* wraps to 1 and
  the lap counter goes up (`driver.md`, "Route progress"); a circuit route whose last entry is not
  its first (two shipped circuits, `opp.md`) makes the opponent drive from the last entry toward
  entry 1 directly.
- **Speed.** *k*_thr is the throttle whenever the speed control does not brake, and also the
  car's throttle scale (the unstuck helper's turn test uses 0.75 × *k*_thr); corner speed limits
  are multiplied by *k*_c; a braking demand acts only above *k*_b; *k*_p is how far ahead the
  planner plans and how early junction braking starts. The shipped rows use *k*_thr 0.57 … 1.00,
  *k*_c 0.89 … 2.29, *k*_b 0.07 … 1.0, *k*_p 50 … 150 ("Checked against").
- **Choosing targets.** The planner builds its paths through the bends and junction turns of the
  route window and picks one each frame; the steering aims at the chosen path's second point
  (`driver-planner.md`).
- **Obstacles.** With "avoid traffic" the traffic cars in the window's road and intersection
  lists; with "avoid props" the registered props (`road-network.md`, "Obstacles"); with "avoid
  players" a player's car tracked on one of the window's roads; with "avoid opponents" the other
  opponents whose current road is in the window, but only once the opponent's own *w* > 2
  (`driver-planner.md` §7, §16). An opponent never treats itself as an obstacle (its racer number
  is its index). Police cars are in none of these lists: opponents never steer around cops.
- **At the end of the route** (*w* ≥ *n*, last lap): the destination approach with *s*₁ = 0 and
  *s*₂ = 0 brakes it to a stop at *D* and holds the brake within 2.5 m (`driver.md`, "Speed
  control", step 3; "Destination behind" brakes it if it is pushed past *D*). This is what an
  opponent does after it has finished: it drives on to *D* and stays there.
- **Off the network** (orders given where no road, shortcut or intersection is found): RECOVER,
  straight at the next route intersection (`driver.md`).
- **Stuck.** The driver's stuck watch and TURN_BACK, and the car's unstuck helper
  (`../physics/vehicle-helpers.md`; with `Rotation` 0 a car stuck off its wheels is lifted upright
  by `Translation`, never rolled).
- **Before the start.** While the race keeps the car not drivable (countdown), the driver's DRIVE
  step 1 sets steering 0, brake 0, throttle 1 whenever the front-left wheel is on the ground, and
  the car's not-drivable handling holds the brake at 1 with the gearbox in neutral (race-modes
  spec; `../physics/damage.md`, "What damage drives"): the opponents rev their engines on the
  grid. At "go" the race makes them drivable again (gear forward).
- **Wrecked** (damage above `MaxDamage`): the driver's wreck branch (`driver.md`, "One frame"
  step 2): no inputs and the linear momentum × 0.95 every frame. In Circuit the car is repaired
  after 5 s of real time and drives on from where it stopped; in the other modes it stays wrecked
  for the rest of the race. The race keeps testing it (a wrecked car that rolls across the finish
  line finishes).
- **In deep water**: there is no opponent rule. The car's water effect works as for every car
  (`../physics/collision.md`, "World limits": it floats), the driver keeps driving, and nothing
  resets it (the five-second "hit water" reset is the player's).
- **Fallen out of the world** (y < −200): DISABLED (above) until the next world reset. The −50 m
  "dropped through the city" reset is the player's only.

## The finish test

The race logic asks each opponent that has not finished yet, once per frame while the race runs,
whether it has crossed the finish line (race-modes spec, "Interface with the race" below). The
**finish line** is a point *F* and a horizontal direction *U* set when a Checkpoint or Circuit
race is set up (race-modes spec: *F* = the last waypoint (Checkpoint) or waypoint 0 (Circuit), *U*
= (float(sin *a*), 0, float(cos *a*)) with *a* = float(waypoint heading in degrees × −0.017453292);
`sin`, `cos` in double). It is one value for the whole program run: Blitz and Crash Course do not
set it, so there the test uses the line of the last Checkpoint or Circuit race played, or *F* =
*U* = (0, 0, 0) if none (then it never fires). Each opponent keeps a **side value** *S* (float),
created 0.0 and never reset (MM2: undefined at creation, then never reset; **engine:** 0.0).

With *p* the opponent's position, *w*, *n*, lap and laps its driver's waypoint index, route
length, lap and lap count, and *front* its front extent:

```
dz = p.z − F.z ;  dx = p.x − F.x
q  = dx·dx + dz·dz
if not (q < 900.0):           not finished          (30 m; a NaN q goes on)
if lap ≠ laps:                not finished
if w < n − 1:                 not finished
a  = U.z·dz + dx·U.x
if not (q < 400.0):           S := a ;  not finished                (20 m; the ring arms the test)
σS = +1 if S > 0;  −1 if S < 0 or NaN;  0 if S = 0
v  = a − ((front + 1.0)·σS)
σv = +1 if v > 0;  −1 if v < 0 or NaN;  0 if v = 0
finished  ⇔  σv ≠ σS
```

So between 20 and 30 m from *F* the test records on which side of the line (through *F*,
perpendicular to *U*) the car is; inside 20 m it reports the finish as soon as the car's front
bumper plus 1 m reaches the line: from the +*U* side when *a* ≤ *front* + 1.0, from the −*U* side
when *a* ≥ −(*front* + 1.0). Inside 20 m *S* is not updated. If the car comes within 20 m without
having been in the ring with the lap and waypoint conditions met, *S* is whatever it was (0.0 from
the start: then the test fires at once unless *a* = 0); with the shipped races every opponent's
second-to-last route intersection is at least 30 m from *F*, so the ring is always passed with
the conditions met on the final lap.

The test only reads the opponent; what the race does on a finish (place, time, results) is the
race-modes spec. Nothing happens to the opponent's driving when it finishes.

## Interface with the race

**The race gives the opponents:** the session's opponent number and lap count and the game mode
(at set-up); drivable on/off and damage counting on/off (countdown and "go"); the finish line *F*,
*U*; a world reset at the start and at every restart. Crash Course also changes opponent 0's
`MaxDamage` and `MedDamage` for some events and makes it not drivable (race-modes spec).

**The race reads from each opponent:** its body position and body matrix, its `InertiaBox` (car
simulation tune) and its current room (for the race's own gate counting, which the race-modes spec
defines and which runs every frame for each opponent before its finish test), the finish test
above, its damage and `MaxDamage` (Crash Course), its index (results use the race's name list).
The HUD map and icons show the opponents (HUD spec), the drawbridges watch their positions
(moving level objects spec), and the sound spec gives them their engine sounds.

## Police and traffic in races

- **Cops chasing opponents.** Race-mode cops come from the race aimap's `[Police]` list
  (`police.md`). An idle cop may start chasing an opponent: `police.md`, "Detection" step 2 (75 m,
  in front of the cop, the first player within the cop's opponent range, one `prnd()` draw against
  the chase chance per unrefused opponent; MM2 registers the opponent the wrong way round, so the
  cop's rank is UNKNOWN and it blocks unless forced to follow). The chased opponent does not react:
  its driving knows nothing of cops.
- **Cops and the opponent test.** A chasing cop has "avoid opponents" on, but it receives new
  orders every frame it drives, which set its *w* to 0 or 1, and before it plans its route
  progress raises *w* by one at most (a second step needs a repeated route intersection or the
  "destination index equals located index" coincidence of `driver.md`, "Enter road" step 2). The
  planner's opponent test needs *w* > 2 (`driver-planner.md` §7 step 7), so chasing cops
  practically never steer around opponents; this follows from the rules as written, nothing
  extra is needed. (The racer-number quirk of §16 step 4, cop *i* never seeing opponent *i*,
  therefore hardly ever shows.)
- **Traffic.** Opponents mark the intersections ahead of their window like every AI driver
  (`driver.md`, "Yield marks"; they are the first drivers of both passes), plus the one-off mark
  on *W*[0]. Traffic is never placed within 50 m of an opponent (`traffic-population.md`) and
  otherwise ignores them (`traffic-reactions.md`). Collisions are ordinary car-to-car collisions.
- **Touching the player.** An opponent's `CarFrictionHandling` is 2.0 in every frame after its car
  touched the player's car in the previous frame's physics, else 1.0 (`driver.md`, DRIVE step 11).

## Random numbers

The opponent logic draws no random numbers: not at creation (beyond what every car's creation
draws, `police.md` "Randomness", `collision.md` "Random numbers"), not in the route file, the
orders, the update, the physics status or the finish test. During play an opponent's wheels draw
bump numbers from the physics generator like every car (`../physics/vehicle-wheels.md`), in mover
order; the police draw for opponents in their detection (`police.md`). A fixed-seed run therefore
needs nothing beyond the generators' restarts at the world reset.

## Engine choices (MM2 undefined)

| Case | MM2 | Engine |
|---|---|---|
| Fewer rows read than the count line | undefined opponents | create only the rows read |
| Route file missing | route and start undefined | no points (`opp.md`) |
| Route point with no intersection in its room | entry unwritten | point left out (`opp.md`) |
| Fewer than two points | negative route length | `opp.md` |
| Side value *S* before it is first set | left-over memory | 0.0 |
| AI-ahead mark with an empty route | reads an undefined entry | skipped |

## Checked against

Machine code of `midtown2.exe` (retail English, Nov 3 2000), and the shipped data with
`tools/ai/opponents.py` (dirty side):

- Constants: count rule trunc(min(count line, setting)) with the float comparison; paint index
  & 3; regenerate only in game mode 3; laps = the session lap count in mode 3, else 1; *s*₁ = *s*₂
  = 0.0; direction (0, 0, 0); heading × 0.017444445 (float); ground snap +2.0 / −10.0 with the
  ground mask, +0.9 (0.89999998); fall-through −200.0 (the update branches once on the state it
  starts with); mover distance 9999999.0 start, 40000.0, sum grouping (Δz² + Δy²) + Δx²; finish
  900.0 and 400.0, front extent + 1.0, sign values ±1/0 with NaN → −1, finish direction
  (0, 0, 1) turned about Y by heading × −0.017453292; no random draws reachable from the opponent's update, orders, finish test or the race's per-opponent
  update.
- `opponents.py rows`: 536 opponent rows in the Checkpoint, Circuit, Blitz and Crash Course
  aimaps of both cities (`roam` and the other race files have none, apart from the unused
  `stunt0`), all with twelve fields, every count line equal to its rows, every named file
  present. Cars:
  `vppanoz` 95, `vpbullet` 68, `vpdb7` 57, `vpauditt` 48, `vpbug` 40, `vpvwcup` 38, `vpcoop2k` 36,
  `vpcab` 35, `vppanozgt` 35, `vpcoop` 28, `vpcaddie` 23, `vpmustang99` 16, `vpford` 15, `vp4x4` 2.
  Throttle 0.57 … 1.00 (335 rows 1.00), pad 50 (407 rows) … 150, brake 0.07 … 1.0 (mostly 0.68,
  0.69, 0.70), corner 0.89 … 2.29 (297 rows 1.0). Switch patterns (switches 1 … 6): all off 226
  rows; 0 1 1 1 1 0 128; 0 0 1 0 0 0 52; 0 1 0 0 1 0 45; 0 1 0 0 0 0 29; 0 1 1 0 1 0 20; 0 0 0 0 1 0
  14; 0 0 1 0 1 0 11; 0 1 1 0 0 0 6; 1 1 0 1 1 1 3; 0 0 1 1 1 0 1; 0 1 0 1 0 0 1.
- Opponents created with the race data's opponent numbers: Checkpoint London 76 (amateur; the
  row of `race12.aimap` has no race-data line) / 78 (pro) in 12 + 12 races, SF 67 / 69; Circuit London 58 / 59, SF 55 / 55 in 10 +
  10 races; Blitz lists are empty; Crash Course aimaps hold one-row lists in 5 London and 4 SF
  courses (both skill levels; how many a course creates is the race-modes spec's).
- `opponents.py finish`: in all 271 (London) and 246 (SF) Checkpoint and Circuit opponents the
  second-to-last route intersection is at least 30 m from the race's finish point, so the finish
  test is armed before the opponent comes within 20 m; 236 / 191 destinations lie within 20 m of
  the finish point; 4 London (`circuit3` pro, 75–85 m) and 36 SF (circuits, 30–45 m) lie farther
  than 30 m.
- `opponents.py start sf race0` (with the reference simulator's city probe): opponent 0 (`vpbug`,
  CoG (0, −0.1, 0)) at point 0 (−1152.0404, 64.850098, −395.00256), model origin y 64.65, ground
  hit at y 64.85, new reset point (−1152.0404, 65.75, −395.00256); opponent 4 starts on a slope,
  reset y 63.367 → 64.285. `opponents.py draw sf race0` draws the routes over the road network
  with the race waypoints (`work/check/opp_sf_race0.png`).
