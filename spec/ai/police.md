# Police: who the cops are, when they chase, how they pursue and when they stop

MM2's police in Cruise and the race modes. This page covers the cops themselves: where they come
from, their cars, the offence test, the pursuit tactics, coordination between cops, how a chase
ends, their effect on traffic, the light bar and siren, and their random draws. How a cop actually
drives (turning a destination into steering, throttle and brake) is the **AI driver**
(`driver.md`, with its target planner `driver-planner.md`); how it finds its way through the streets
is the **route search** (`route-search.md`). The road graph itself (roads, intersections, lanes,
the position look-up) is `road-network.md`, with the file formats in `../formats/bai.md` and
`../formats/aimap.md`; traffic terms (`rnd()`, frame order) are those of `traffic.md`.
Car-versus-car collisions are the car-to-car physics spec.

Units: metres, seconds, radians, m/s. All arithmetic is float32 with one rounding per operation,
left to right as written (`../physics/vehicle.md`, "Precision"); `atan2` is computed in double and
compared without rounding where this page says so. "Speed" of a car is the car simulation's speed
value (`../physics/vehicle.md` step 2: |velocity along the body's Z axis|, never negative).
"Position" of a car is its **body position** and "rows X, Y, Z" are its body rotation rows
(`../physics/vehicle-body.md`); the car faces −Z, so its forward direction is −Z.

## The short version

- Cops are **parked**: each one sits at a fixed spot from the race file, brakes held, and never
  patrols. There are 19–20 spots per city in Cruise.
- A parked (idle) cop starts chasing a player who comes **within 75 m and anywhere in the half-plane
  in front of it**. That is the whole offence test: speed, collisions, red lights or driving on the
  wrong side play no part (MM2 contains tests for those, but nothing calls them).
- At most **three cops** chase one car at a time. All of them drive toward the player along a route
  through the street network. The registered cop nearest the player, when within 25 m, switches
  from following to **blocking**: it aims 12 m in front of the player's car (or alongside its rear
  bumper when it is behind), and once it is there it holds the player's heading and speed.
- A cop gives up when it is more than **250 m** (horizontally) from the car it chases. It then
  brakes to a stop where it is and stays there, light bar still turning, until something comes in
  front of it again. Cops that are wrecked or fall into deep water drop out until the next reset.
- There is no "busted" in Cruise: being caught has no consequence beyond the collisions.
- Traffic does not pull over. Any AI-driven car (cops included) makes traffic on the roads into the
  next intersections of its route wait at their stop signs and lights.

## Where the cops come from

### The police list in the race file

The cops of a game are listed in the race file `race/<city>/<race>.aimap`; Cruise uses
`<race>` = `roam`. At the "professional" skill level MM2 reads `<race>.aimap_p` instead (defaults
apply if it is missing). The file is read as described in `traffic-population.md` ("Traffic
sections of the `.aimap` files": lines of at most 127 characters, a section starts at a line that
begins with its name, its value on the next line). Two sections concern the police:

| Section | Value | Default |
|---|---|---|
| `[CopChaseDistance]` (18 characters compared) | float: the give-up distance *Dg*, metres | 250.0 |
| `[Police]` (8 characters compared) | a count line (integer *N*), then the cop entries | no cops |

After the count line, entries are read one line at a time: a line starting with `#` is skipped and
does not count; a line starting with `[` ends the list early; the list also ends after *N* entries.
Each entry is read with the fields below, separated by white space:

| # | Field | Type | If missing | Use |
|---|---|---|---|---|
| 1 | car | name (≤ 31 characters) | — | the car the cop drives (`vpcop` in every shipped file) |
| 2–4 | x, y, z | float | — | the parking spot *q* |
| 5 | rotation | float, degrees | (see below) | heading θ = rotation × −0.017444445 (float product; the constant is MM2's, not exactly −π/180) |
| 6 | (unused) | integer | 0 | read, never used |
| 7 | behaviours | integer | 15 | only whether bits 1, 2, 4 or 8 are set matters (below) |
| 8 | chase chance | float | 0.5 | chance of chasing an **opponent** (race modes) |
| 9 | opponent range | float | 50.0 | distance from the **player** within which an opponent can be chased |

The comment lines in the shipped files (`Geo File, StartLink, Start Dist, …`) are stale and do not
describe these fields. The number of cops MM2 creates is *N* from the count line even when fewer
entries follow (the missing ones are garbage in MM2), and an entry with fewer than five values has an
undefined rotation. **Engine choice:** create only the entries actually read, and skip entries with
fewer than five values (no shipped file has either case).

**Behaviour flags.** MM2 turns bits 1, 2, 4, 8 of field 7 into a list of up to four tactics, but the
list is never consulted except for its length: a cop whose behaviours have none of these bits set
never blocks (it only follows, see "Following or apprehending"). Every shipped entry has 15.

### How many cops

With cop density *d* the game creates the first *n* entries of the list:

```
n = trunc(clamp(d, 0, 1) × N)          (float product, truncated toward zero)
```

- **Cruise**: *d* is the "Cop density" setting (a 0–1 slider in MM2's Cruise options). Choosing
  Cruise in the race menu, and every return to the menus with Cruise as the last mode, sets it to
  1.0, so every listed cop exists unless the player has moved the slider since (that value lasts
  for the next game only, `../game/menus.md`, "The cop control"). The engine's "Cops off" is
  *d* = 0, "Cops on" is *d* = 1.
- **Race modes**: *d* = the race's `Cops` number from `race/<city>/mm<mode>data.csv` (amateur or
  professional column), so 0 means no cops and any value ≥ 1 means every listed cop.

| City | `roam.aimap` (amateur) | `roam.aimap_p` (professional) | car |
|---|---|---|---|
| San Francisco | 19 | 20 (adds one at (−1793.0, 30.4, −2550.5), heading 180°) | `vpcop` |
| London | 20 | 20 (same list) | `vpcop` |

Every Cruise entry has fields 6–9 = `0 15 0.5 50`. `[CopChaseDistance]` is absent from both roam
files, so *Dg* = 250 m in Cruise (SF `crash5` uses 150).

### The cop's car

- **Model and paint**: the car named in field 1 (`geometry/vpcop.pkg`). For `vpcop` the paint set
  depends on the city: **paint 0 ("Police Special", the SF livery) when the city has cable cars
  running, otherwise paint 1 ("London Patrol")**. MM2 decides this by the number of cable cars it
  created for the session: one per intersection at which **exactly one** of its roads has a
  cable-car line on the side that leaves that intersection (side B if the intersection is the road's
  end 1, side A if it is its end 0; `../formats/bai.md`, side block "cable"), and none at all when
  cable cars are switched off (MM2 switches them off only while setting up a multiplayer game). So:
  paint 0 if at least one such intersection exists, else paint 1 (SF: paint 0; London, which has no
  cable lines: paint 1; a mod whose cable lines form closed loops only would get paint 1). For any
  other car the paint is the cop's index in the list modulo 4. The car is drawn like a player or opponent car
  (`../formats/car-model.md`).
- **Physics**: the full vehicle simulation of `../physics/vehicle*.md` with the car's own tune files
  (`tune/vehicle/vpcop.vehcarsim`, `.vehGyro`, `.vehStuck`, `.vehCarDamage`). Unlike the player's
  `vpcop`, an AI cop does **not** take its handling from `vpmustang99`. `vpcop_cop.vehcarsim` and
  `vpcop_opp.vehcarsim` are never read. The car's unstuck helper (`../physics/vehicle-helpers.md`,
  "Unstuck helper") gets the AI driver's overrides (`Rotation` 0, squared position threshold 1.0;
  `driver.md`, "Creation and reset") and then `TimeThresh` = **0.75** instead of the file's 2.0.
- **Damage**: the car's damage value counts up with impacts as for every car (car-to-car physics
  spec, damage);
  `vpcop.vehCarDamage` has `MaxDamage` 456300. A cop whose damage exceeds its maximum is
  **wrecked** (below).
- **Collision bound**: an AI car collides as a **box** — the bounding box (min and max corners) of
  its `<car>_BOUND` geometry — not as the bound geometry itself. Exception kept from MM2: the bound
  is shared by all cars of the same model, so when the player drives `vpcop` (loaded first) the cops
  use the player's full geometry bound. (Car-to-car physics spec; recorded here because it changes
  how cops hit.)
- **Extents** used by the tactics below, from that bounding box (min, max): front = −min.z,
  back = max.z, left = −min.x, right = max.x.

### Parking (reset)

At the start of a game and at every world reset, and for one cop when it falls through the world
(below), the cop is reset. (A world reset — game start or restart of the session, the same reset that
restarts traffic and sets the random generator to 1 — first empties the police roster, then resets
every cop in list order. The player's own respawn after falling or sinking is not a world reset and
touches no cop.)

1. The car is placed at its spot *q* with heading θ by MM2's placement rule
   (`../physics/vehicle.md`, "Placing the car in a city"): body position *q* + CoG, rotation by θ
   about +Y, everything else at rest.
2. Its state (next section) becomes: idle, no previous state, tactic "ram" (never used), all
   opponent refusals cleared, light bar off, siren sound off.
3. Its driver is reset (`driver.md`, "Creation and reset"), its mode set to HOLD, and it is then
   given parking orders (row "Parked" of "Driving orders": empty route, destination *q*). Receiving
   orders replaces HOLD (`driver.md`, "Receiving orders"): the driver ends up in **DRIVE** when the
   car's place (the driver's basic look-up of its body position *q* + CoG with no room hint) is a
   road, shortcut or intersection and in **RECOVER** otherwise. Cruise spots: London 5 DRIVE /
   15 RECOVER, SF 8 DRIVE / 11 RECOVER (amateur list; the professional list's extra SF spot is on
   road 0, so 9 / 11). Most RECOVER spots are in block rooms whose component list is empty or holds
   only shortcuts the spot is off. Either way the destination
   approach of its speed control (arrival speed 0, stop distance 5 m, the car already within 5 m of
   the destination) holds the brake fully, so the cop stays parked.

Cops never move off their spot until they chase. (A parked cop shoved more than about 70 m from its
spot would leave the destination approach and drive back toward it; MM2 behaves the same.)

## The cop's state

| Name | Values | Meaning |
|---|---|---|
| state | IDLE, APPREHEND, FOLLOW, UNKNOWN, OUT | IDLE: parked or stopped, looking for someone to chase. FOLLOW: chasing. APPREHEND: chasing as the nearest cop, close in. UNKNOWN: the chased car is not in the roster (only reachable through MM2 bugs). OUT: wrecked or drowned, out until reset |
| previous | a state or "none" | the state the "on entry" actions last ran for |
| tactic | BLOCK, BLOCK_WAIT (and three unused values) | APPREHEND sub-state |
| perp | a car or none | the car being (or last) chased |
| range | float | horizontal distance to the perp, updated each pursuit frame |
| perp place | (index, kind) | the perp's road-graph component (`road-network.md`, position look-up) |
| refused | one flag per opponent | opponents this cop decided not to chase |
| route buffer | up to 100 intersection ids, a count | filled by the route search each pursuit frame |

## The police roster

One shared roster per game records who chases whom: up to **3 chased cars**, each with up to
**3 chasing cops** in order of registration, plus the time of the last new chased car (unused).

- **Register** (cop *c*, car *p*): if *p* is in the roster: fail when it already has 3 cops,
  otherwise append *c* to its cops and succeed. If *p* is not in the roster: fail when 3 cars are
  already listed, otherwise list *p* in slot "number of cars" with *c* as its only cop and succeed.
  (Nothing checks whether *c* is already listed.)
- **Unregister** (cop *c*, car *p*): find *p* among the first "number of cars" slots and *c* among
  its cops; remove *c* (later cops move up one place). If *p* has no cops left, its slot is
  emptied and the number of cars decreases by one. Slots are **not** compacted: with two chased
  cars, emptying slot 0 hides slot 1 from every later search and the next new car overwrites it
  (MM2 behaviour; impossible in single-player Cruise, where the player is the only chased car).
- **Rank** (cop *c*, car *p*, range *r*): if *p* is not among the listed cars → UNKNOWN. Otherwise
  find *p*'s cop nearest to *p*, by `((dz·dz + dy·dy) + dx·dx)` with (dx, dy, dz) = *p*'s position −
  the cop's position, starting from 1e9 and taking a cop only when strictly nearer (ties go to the
  earlier cop). If that cop is *c* and *r* ≤ 25.0 → APPREHEND, otherwise → FOLLOW.

## One frame of police

The police run in step 3 of the AI frame (`traffic.md`, "Frame order"). MM2's order inside that
step: pedestrians; the yield marks of every AI-driven car are **cleared** ("Effect on traffic");
race opponents; other AI racers; **every cop in list order, each completely**; the yield marks are
**set** again; cable cars; subway trains. One cop's update:

1. **If OUT**: go to step 5.
2. **If IDLE**: look for a perp ("Detection"); whether or not one is found, continue at step 4.
3. **Pursuit** (any other state):
   1. Look up the perp's place in the road graph (index, kind) from its position, using its current
      room as a hint (`road-network.md`, position look-up).
   2. range = √(dx·dx + dz·dz) with (dx, dz) = this cop's position − the perp's (x and z only).
   3. state = roster rank (this cop, perp, range).
   4. Force FOLLOW if any of: the **first player's** car is in reverse gear (gear index 0;
      MM2 tests player 1 even when the perp is someone else); this cop's driver is in TURN_BACK
      mode; this cop has no behaviours (flags field without bits 1, 2, 4, 8); the perp's speed
      < 10.0.
   5. If FOLLOW: give following orders ("Driving orders"); otherwise (APPREHEND or UNKNOWN):
      apprehend ("Blocking").
   6. If *Dg* < range: **the perp escapes** (below, without explosion) and this cop's update ends
      here for this frame.
   7. If this cop is wrecked (driver's wrecked flag, `driver.md`): **the perp escapes** with
      explosion, and the update ends here.
4. **Every non-OUT cop**:
   1. **Rubber band**: if the driver's throttle output of the previous frame is exactly 1.0 and the
      car's speed < 50.0, multiply each component of the car's linear momentum by 1.03 (float
      constant 1.03 = 1.0299999714). This happens once per frame during the AI update, whatever
      the frame time. The driver's throttle output is its throttle level (1.0 for cops) whenever
      its speed control is not braking (`driver.md`, "Speed control"), so a chasing cop below
      50 m/s gains 3 % momentum every frame it is not braking.
   2. (Siren sound update — sound spec.)
   3. If the cop's current room is a **deep-water** room (`../formats/psdl.md`, "Deep water";
      the room alone decides, not the water height): the perp escapes (no explosion; for a cop
      that was idle this only stops its sounds and sets HOLD) and state = OUT.
5. **Drive**: if state is APPREHEND and the tactic is BLOCK_WAIT, the cop **mirrors** the perp
   ("Holding the block") instead of driving; otherwise the driver runs its frame (`driver.md`).
6. If the cop is wrecked: the perp escapes with explosion, and state = OUT. (A wrecked cop repeats
   this every frame, OUT included, since step 1 sends OUT cops to step 5. For the police nothing
   more changes (the unregister finds nothing; state OUT and driver mode HOLD again), but its sound
   part runs each time: the explosion plays again whenever it has ended,
   `../audio/car-sounds.md` "Explosion". The police update makes this call; the sound layer does
   not repeat it.)
7. **Physics activity**: with *m* = the smallest `((dz·dz + dy·dy) + dx·dx)` over all players
   ((dx, dy, dz) = cop − player position; start value 9999999.0), the cop is declared a moving
   physics object for this frame (kind 2, an ordinary car; the player's car is kind 4) with
   collision set "everything" (update, city, props, cars; MM2 flags 0x1B) if *m* < 40000.0
   (200 m), with "everything except props" (0x13) if 40000.0 ≤ *m* < 62500.0 (250 m), and not at
   all beyond (car-to-car physics spec, "movers"). A cop more than 250 m from every player is
   therefore not simulated (it stands still).
8. **Fell through**: if the cop's position y < −200.0, reset it (as at game start).

## Detection (IDLE cops)

Each frame an idle cop checks the players, then (only if no player was registered) the opponents:

1. For each player car *p*, in player order: if `((dz·dz + dy·dy) + dx·dx)` < 5625.0 (75 m;
   (dx, dy, dz) = cop − *p*) and *p* is **in front** of the cop (field of view below) and
   registering (this cop, *p*) succeeds → **start chasing *p***, stop.
2. For each opponent *i* (race modes) not refused by this cop: if its squared distance from the cop
   (same formula) < 5625.0 and it is in front of the cop, and the horizontal squared distance
   between the cop and **the first player** (`dx·dx + dz·dz`) < opponent range² (field 9): draw
   *r* = `prnd()`; if *r* ≤ chase chance (field 8), register and chase as described next, otherwise
   set refused[*i*]. (If the player-range test fails nothing happens and nothing is drawn.)

   MM2 registers the opponent the wrong way round: it lists the **opponent** as a chasing cop of
   the car **this cop chased last** (none at the start of a game). If that succeeds the cop starts
   chasing the opponent; the roster then does not know the opponent as a chased car, so the cop's
   rank is UNKNOWN (it apprehends unless forced to follow), and when the chase ends unregistering
   finds nothing, so the false entry stays in the roster. Reproduce as is (race modes only).

**Start chasing *p***: perp = *p*; the driver's mode = DRIVE; state = FOLLOW; range = horizontal
distance as in pursuit step 3.2; perp place = position lookup of *p* with no room hint; then give
following orders immediately (same frame).

**Field of view.** With *d* = (*p* − cop) position, X and Z the cop's rows:

```
side = (d.z·X.z + d.y·X.y) + d.x·X.x
fwd  = ((−Z.z)·d.z + (−Z.y)·d.y) + (−Z.x)·d.x
in front  ⇔  −1.57 < atan2(side, fwd) < 1.57        (1.57 as float = 1.5700000525; atan2 not rounded)
```

So *p* is seen when it is ahead of the cop's front–back line (a half-plane, minus a sliver of
0.0008 rad at each side), at any height. There is no line-of-sight test: walls do not hide the
player. A player directly beside the cop is not seen; one 1 m ahead of the line is.

## Pursuit

### Following or apprehending

Rank decides each frame (step 3.3): the registered cop nearest the perp, when it is within 25 m
horizontally, apprehends; everyone else follows. Step 3.4 sends the apprehender back to following
while the perp is slow (< 10 m/s), while the player reverses, or while the cop is busy turning
round.

"On entry" actions run when a state's handler finds state ≠ previous (then previous = state):
entering FOLLOW (from IDLE or APPREHEND) **switches the light bar on** (if the car has one) and
starts the siren sound; entering APPREHEND (from FOLLOW or UNKNOWN) draws one random number
(`prnd()`, the value is not used) and sets tactic = BLOCK.

### Driving orders

Every pursuit frame the cop runs a **route search** from its position to a destination
(`route-search.md`, free start, its own body position and Z row, its current room and the perp's
current room as hints, its 100-entry route buffer) and hands the result to its driver as
**orders** (`driver.md`, "Orders"): the route, destination *D*, direction *Dd*, laps 0, arrival
speed *s*₁, stop distance *s*₂, the six switches, throttle level *k*_thr, corner factor *k*_c,
brake threshold *k*_b and pad *k*_p. The cop does this **only when its driver is in DRIVE or
RECOVER mode**; in TURN_BACK or HOLD it gives no new orders that frame.

| Situation | *D* | *Dd* | *s*₁ | *s*₂ | *k*_c |
|---|---|---|---|---|---|
| Follow | perp position | −perp.Z (its forward) | (perp speed + range) − 12.5 | 5.0 | 2.0 |
| Block, cop level or ahead | 12 m ahead of the perp | −perp.Z | perp speed | 0.0 | 1.0 |
| Block, cop behind | beside the perp's rear bumper | −perp.Z | perp speed + 25.0 | 0.0 | 1.0 |
| Parked (reset; no search, empty route) | spot *q* | (0, 0, 0) | 0.0 | 5.0 | 2.0 |

In every row the switches are 0, 1, 1, 0, 1, 0 (avoid traffic, props and opponents — the last one
practically without effect, see "Effect on traffic and other cars" — **not** the player), *k*_thr = 1.0, *k*_b = 0.7 and *k*_p = 75.0. "range" is the horizontal distance of pursuit
step 3.2. (The tactic "ram" — *D* = the perp, *s*₁ = perp speed + 15.0, *s*₂ = 0, *k*_c = 1.0 — and
two "push" tactics exist in MM2 but can never be selected: entering APPREHEND always picks BLOCK.
They need not be implemented. MM2's search also receives a goal direction it never reads.)

### Blocking

Run in APPREHEND every frame (only when the driver is in DRIVE or RECOVER mode; otherwise nothing
happens this frame). With *P* the perp's position, X and Z its rows, *v* = cop position − *P*:

```
ahead = (((−Z.x)·v.x + (−Z.z)·v.z) + (−Z.y)·v.y)      how far the cop is in front of the perp
```

**BLOCK_WAIT** (the cop got in front, see below): if ahead < 0.0 (the perp has got past), tactic =
BLOCK. Nothing else happens in this step (the cop mirrors the perp, step 5).

**BLOCK**: with the perp's extents *back* (max.z) and *left* (−min.x) and the cop's extent
*right*:

1. If **not** (ahead < −back) — the cop is level with or in front of the perp's rear bumper —
   the destination is 12 m ahead of the perp: `T.c = P.c − (12.0·Z.c)` for c = x, y, z.
2. Otherwise the cop is behind the perp. Let
   `side = (v.z·X.z + v.y·X.y) + v.x·X.x` (positive: the cop is on the perp's right),
   `w = (cop.right + perp.left) + 1.0`,
   `B.c = (back·Z.c + P.c)` (centre of the perp's rear bumper),
   `R.c = B.c + w·X.c` (right of it), `L.c = B.c − w·X.c` (left of it).
   - If ahead ≤ −20.0 **and the perp's place index is 1**: test both points against a road:
     `t_R` = "position on road" of *R* with margin = the cop's **left** extent, `t_L` = of *L* with
     margin = the cop's **right** extent, both on **road number = the perp's place kind**
     (`road-network.md`, "Is a point on the road": 1 roadway, 2 sidewalk strip, 3 off the road). Choose *L* if
     t_R = t_L and side ≤ 0, *R* if t_R = t_L and side > 0, *L* if t_R > t_L, *R* if t_R < t_L.
     (MM2 swapped the two fields of the perp's place here: the test was meant for "the perp is on
     a road, use that road". Reproduce the swap: index 1 and road number = kind, 0–3.) MM2 keeps
     the perp's place index just before the route buffer, and a route search that starts off the
     network writes 9999 into it (`route-search.md`, step 9). This never reaches the test: the
     place is looked up again at the start of every pursuit frame (step 3.1) and read here before
     this frame's search, so an engine need not reproduce the write.
   - Otherwise: *L* if side ≤ 0.0, *R* if side > 0.0.
3. Run the route search and give orders (table above; "behind" is the case of step 2).
4. If `(dx·dx + dz·dz)` < 9.0 with (dx, dz) = destination − cop position: tactic = BLOCK_WAIT.

The destination of step 1 is not adjusted to the road; the driver and its planner cope
(`driver-planner.md`).

### Holding the block (BLOCK_WAIT)

Instead of its driver, the cop sets its car's controls directly each frame (step 5), copying the
perp's heading and keeping just below its speed. With *u* = the perp's speed, *c* = the cop's speed,
F = −(perp Z row) and the cop's rows X, Z:

```
target point (driver's) = P                      (kept for the driver's next frames)
s = u − 3.0
if s < c:  b = (c − s) / k,  k = 1.2 × 19.8 as a float product (= 23.76, bits 0x41BE147B)
           if b ≤ 0.3:  throttle 0, brake 0
           else:        throttle 0, brake = clamp(b, 0, 1), and the car's angular momentum
                        (each component) × 0.85
else:      throttle 0.5, brake 0
a = atan2(F.z·X.z + F.x·X.x, −(F.z·Z.z + F.x·Z.x))     (only x and z; sums in this order)
steering = clamp((a × 1.33) × 1.428, −1, 1)       (a not rounded before the products)
```

The values go to the car's steering, brake and throttle inputs, and to the driver's own copies of
them (the rubber band reads the throttle copy). Handbrake is not touched.

### Several cops

Up to three cops register for the same car; each follows its own route. Only the nearest one within
25 m blocks; the others keep following, so in practice one cop cuts in front and the others arrive
from behind and hit or box the player in through the physics. There is no other coordination and no
roadblock. A fourth cop that sees the player while three are registered is refused and keeps
checking every frame (it never gets a free slot until one of the three gives up).

## End of a chase

### The perp escapes

"The perp escapes" (with or without explosion) means:

1. Sound: with explosion, the cop's car plays its explosion sound; without, it sets a siren fade
   flag that the siren stop clears at once (there is no "lost him" voice); either way the siren
   **sound** stops (`../audio/car-sounds.md`, "Siren"). The **light bar keeps turning**:
   MM2 switches it off only when the cop is reset.
2. Unregister (this cop, perp) from the roster.
3. state = IDLE; the driver's mode = HOLD (brake fully, steer toward the destination of the last
   orders: the perp's position or block point at that moment).

Causes:

| Cause | Test | Explosion | Afterwards |
|---|---|---|---|
| Too far | horizontal range > *Dg* (250 m in Cruise), checked after the orders of the frame | no | IDLE: the cop stops where it is and can start a new chase |
| Wrecked | the driver found the car's damage above `MaxDamage` | yes | OUT until reset |
| Deep water | the cop's room is deep water | no | OUT until reset |

An IDLE cop does not return to its spot or patrol: it stays where it stopped, facing wherever it
faced, and chases again when a player passes in front of it within 75 m. Only a world reset (or
falling below y = −200) puts it back on its spot.

### Caught

Cruise has **no** arrest, fine or "busted" state: MM2's Cruise game loop never looks at the police.
The cops only push, block and damage the player's car through the collisions; a player car
damaged beyond its maximum is handled by Cruise's own rule (undrivable for 3 s, then repaired;
Cruise game spec). The only other uses of the police outside the AI are:

- the HUD map (not in this milestone) draws an icon for every cop that is **not IDLE** (OUT
  included);
- one **crash-course** test (milestone 4): when the player reaches the finish of a crash-course
  event of kind 3 (`copchase`) or kind 4 (`corner`), the event is failed if **any cop is in
  sight**; no other event kind and no other game mode runs it. A cop is in sight when, taking the
  cops in list order and stopping at the first that passes:
  1. its state is anything but IDLE (FOLLOW, APPREHEND, UNKNOWN **or OUT**: a wrecked or drowned
     cop still counts);
  2. with P = the player car's position and Q = the cop's position, each raised by 3.5 m
     (P.y + 3.5 and Q.y + 3.5, rounded), the probe from P to Q (`../physics/collision.md`, "The
     query": a fresh state with room hints 0 and an empty polygon cache, the wheels' "ground" mask,
     ignoring nothing; so city polygons and the solid placed objects the wheels see block it,
     cars do not) finds **no** hit;
  3. d = √(((Δy·Δy) + (Δz·Δz)) + Δx·Δx) < 200, with Δ = P − Q per component (each rounded; Δy
     is taken between the raised heights), a 3D distance; a NaN d also passes (MM2's comparison
     treats "unordered" as "less").

## Effect on traffic and other cars

- **No pulling over.** Ambient traffic never looks at sirens or police states.
- **Yield marks (every AI-driven car, cops included).** Each AI car's driver keeps a window of the
  road it is on and the next two roads of its route, and for each of them the intersection it is
  heading for (`driver.md`, "Yield marks"). Just before the AI cars update each frame, for each AI
  car and each of those intersections, every road meeting that intersection whose traffic control
  **at that end** (`../formats/bai.md`, end block) is a stop sign (0) or a traffic light (1) gets
  its *AI-ahead* mark cleared; just after the AI cars update the same is done with the mark set.
  Traffic waiting at a stop sign or light on a marked road does not enter the intersection
  (`traffic-driving.md`, intersection rules). The ambient traffic updates before the AI cars, so it
  sees the marks set at the end of the previous frame. A parked cop on a road keeps marking the
  intersection ahead of it.
- **Obstacles.** The cop's planner steers around traffic and street props, but **not** around the
  player it chases (its "avoid players" switch is off) and in practice not around race opponents
  either: its "avoid opponents" switch is on, but the planner only tests opponents once the route
  index *w* is above 2 (`driver-planner.md` §16), and a chasing cop gets new orders every frame
  that set *w* to 0 or 1 (`driver.md`, "Receiving orders"); the route progress before planning
  raises it to at most 2 in normal cases (3 only when a two-intersection route's destination index
  happens to equal the index of the intersection being reached). See also `opponents.md`. Traffic
  treats cops like other AI cars (`traffic-driving.md`, `traffic-reactions.md`).
- **Collisions** between cops, traffic and the player are ordinary car-to-car collisions
  (car-to-car physics spec), with the box bound noted above.

## Light bar and siren

### Lights

Every car with any of the parts `SRN0`…`SRN3` (`../formats/car-model.md`) has a light bar of one
light per part present, in the order SRN0, SRN1, SRN2, SRN3; light *k* (*k* = 0, 1, … in that
order, counting only the parts present) has:

- position: the part's pivot origin (its `.mtx`), in the car's model space;
- colour: the diffuse RGB of the material of the first batch of the part's `H` mesh (after the
  usual fill from lower levels), in the paint set the car was created with;
- beam direction (model space at first, see below): (cos(k·π/2), 0, −sin(k·π/2)), i.e. the start
  vector (1, 0, 0) turned by k × 1.5707964 about Y.

`vpcop`: SRN0 (−0.190, 1.539, 0.181) and SRN1 (0.582, 1.539, 0.181) blue (0.0706, 0, 1);
SRN2 (−0.565, 1.539, 0.181) and SRN3 (0.194, 1.539, 0.181) red (1, 0.102, 0.102); the same in both
paint sets. Start directions +X, −Z, −X, +Z. `SIREN0`/`SIREN1` meshes are never drawn.

The light bar is **on** from the moment the cop first enters FOLLOW until the cop is reset (see
"End of a chase"); the player switches the light bar of his own police car on and off with the horn
control (each press toggles it, with the siren sound; `../engine/controls.md`, Enter = horn).

While the light bar is on, once per frame in the car's post-physics update (which MM2 runs for cars
that are active physics objects that frame), every beam direction turns about Y by
`a = (F × 2.5) × 3.1415927` (F = frame time): `x' = s·z + c·x`, `z' = c·z − s·x` with s = sin a,
c = cos a (y unchanged). So the lights spin at 1.25 turns per second, each a quarter turn apart.
The directions are never re-aligned with the car: they are treated as world-space directions from
the first frame on (MM2 never turns them with the car), and they keep their angle while the bar is
off.

### Drawing

In the glow pass (`../formats/night.md`), at any time of day, for a car whose light bar is on:

1. Each light's world position = model position × the car's model matrix.
2. **Glow**: a billboard at that position with texture `lt_glow`, as for headlights: with *d* the
   vector from the light to the camera, r² = |d|², c = (d · beam) / |d| (0 if |d| = 0): nothing if
   c < 0, else intensity I = (25 / r²) × c³ and half-size = 0.2 × √(r² × I) (= c^1.5 m; 1 m looking
   straight into the beam); colour 0.95 × the light's colour, alpha 1. No line-of-sight test.
3. **Headlights flash**: the car's headlight glows (`night.md`) are drawn too, day or night, but
   instead of pointing forward their beam directions spin about Y in opposite directions,
   HEADLIGHT0's by +F × 42.411503 and HEADLIGHT1's by −F × 42.411503 radians per drawn frame
   (6.75 turns per second; same turn formula as above), starting from wherever they last pointed
   (forward the last time headlights were drawn normally, or world −Z if never). Their glow uses
   the same formula as the siren glows (intensity 25, exponent 3).
4. **Lens flares** (one set per car), after all glows, for each light with
   `I' = 0.5 × flare intensity` where the flare intensity is computed as in step 2 but with a
   cut-off of 0.05: 0 if 25 < r² × 0.05 (beyond 22.4 m), else max(0, (25 / r²) × c³ − 0.05):
   - nothing if I' < 0.05;
   - project the light position with the current camera to normalised device coordinates
     (x, y in −1…1, depth z in 0…1); nothing if z > 1 (behind the camera or beyond the far plane);
     x' = 1.33 × x; ρ² = x'² + y²; nothing if ρ² > 4.0 (2.0²);
   - if ρ² > 0.25 (0.5²): fade = (√ρ² − 2.0) / (0.5 − 2.0); I' = fade × I'; nothing if I' < 0.05;
   - draw the flare's 20 elements in order: element *e* (position *pₑ*, size *zₑ*, colour *cₑ*,
     brightness *bₑ*, reach *mₑ*) is drawn if ρ² ≤ *mₑ*², as a square from
     (x'·*pₑ* − *zₑ*, y·*pₑ* − *zₑ*) to (+2*zₑ*) in a screen space spanning −1.33…1.33 × −1…1
     (texture `lt_flare`, additive, u/v 0…1), colour = trunc(255 × light colour × *cₑ* ×
     min(I' × *bₑ*, 1)) per channel (alpha 255).
   The 20 elements are made when the car is created, from 6 draws each of a generator of the
   `rnd()` kind, in element order: r₁, r₂, r₃ give the colour *cₑ* = ((r₂+1)·0.5, (r₁+1)·0.5,
   (r₃+1)·0.5); r₄ gives *pₑ* = (r₄·1.5)², *bₑ* = min(0.25 / *pₑ*, 1), *zₑ* = √*pₑ* × 0.1; r₅ gives
   *mₑ* = r₅·0.5 + 1.5; r₆ < 0.5 makes *pₑ* negative. Then element 0 gets *pₑ* = 1.0, *zₑ* = 0.3
   and element 1 *pₑ* = −1.0, *zₑ* = 0.25. (MM2 uses the game-wide generator here, at car creation
   and seeded from the clock, so the flare pattern differs between sessions; every car, police or
   not, makes these 120 draws. **Engine:** use a separate generator started with x = 1 for each
   car.)

The siren **sound** (`londonpolicesiren` / `sfpolicesiren`) starts when a cop enters FOLLOW and
stops when the perp escapes; it is the sound spec's (`../audio/car-sounds.md`, "Siren").

## Randomness

MM2's police draw from the game-wide generator (`traffic.md`, "Randomness": x ← x·214013 +
2531011 mod 2³², value ((x >> 16) & 32767) × 3.0517578e-05, restarted with x = 1 at every world
reset), between the draws of traffic, pedestrians and effects. **Engine:** as for traffic, the
police get their own generator of the same kind, `prnd()`, restarted with x = 1 at every world
reset; every draw below must still be made (also the discarded one) so that the sequence matches a
reference that follows this page. Draws by the police during play, in the order they occur within
the AI update (cops in list order, each completely, see "One frame of police"):

| Where | When | Draws | Used for |
|---|---|---|---|
| Entering APPREHEND | each time a cop's state becomes APPREHEND or UNKNOWN from another state | 1 | nothing (discarded) |
| Detection of opponents | per (idle cop, unrefused opponent) passing the 75 m, front and player-range tests | 1 | chase chance |

In single-player Cruise only the first row happens. The route search and the driver make no draws
(`route-search.md`, `driver.md`); the cop's car simulation may (`../physics/vehicle-wheels.md`,
bumps). At creation each car makes 120 draws for its lens flares and the car setup makes others
(car-model and physics specs); these all happen before the world reset that seeds the generator,
so they do not affect check runs that start at the reset.

## For check runs

A deterministic city run with cops needs: the cop list of `roam.aimap` (amateur), cop density 1,
the generator at 1 after the reset, the AI update order above, and the driver and route search of
the other pages. Useful observable events per frame: each cop's state, tactic, the first entries
of its route buffer, its car inputs, and roster contents.

## Checked against

All values read from `midtown2.exe` (retail English, Nov 3 2000) machine code and the shipped data
with `tools/ai/police.py`:

- `police.py data`: Cruise lists SF 19 / 20 (pro), London 20 / 20, all `vpcop` with fields
  `0 15 0.5 50`; at density 0.1 … 1.0 SF amateur gets 0, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19 cops (float
  truncation: 0.1 × 19 = 1.9 → 1). London spot #13 has rotation 535° → heading −9.3327780 rad.
  Only SF `crash5` sets `[CopChaseDistance]` (150).
- Constants: 75 m = 5625.0, field of view ±1.5700000525, rank distance 25.0, apprehend speed 10.0,
  give-up default 250.0, rubber band 1.03 below 50.0, block 12.0 / 1.0 / −20.0 / 9.0, follow
  −12.5, speeds +25.0 and +15.0, mirror 3.0, 1.2 × 19.8, 0.3, 0.85, 0.5, 1.33, 1.428, movers 40000.0
  / 62500.0 / 9999999.0, fall −200.0, aimap rotation −0.017444445, siren 2.5 × 3.1415927, light
  step 1.5707964, headlight spin 42.411503, flare 0.5 / 0.05 / 0.5 / 2.0 / 1.33 / 255.
- The three offence tests in MM2 (collision flag, "hit me" flag, speed < 1 m/s) and the roster's
  search function have no callers; the "Ram" and "Push" tactics are unreachable (entering APPREHEND
  always writes BLOCK).
- `police.py siren`: `vpcop` light colours and pivots above; glow half-size 1.0 m on the beam, 0.81 m
  at 30°, 0.35 m at 60°.
- `police.py zones` draws the detection half-disc and the block targets
  (`work/check/police_zones.png`).
- `police.py route sf|london` runs the route search (`route-search.md`) from every Cruise spot to the
  next spot and to three fixed road points and draws the routes (`work/check/police_routes_sf.png`,
  `police_routes_london.png`): 75 + 78 routes, 1 + 2 empty (goal next to the start); the routes
  follow the streets and cut through parks on shortcuts.
- Grouping of the distance sums checked in the machine code: detection and movers sum
  `(z² + y²) + x²` of cop − target; rank sums `(z² + y²) + x²` of perp − cop; range and
  block-arrival use `x² + z²` (two terms, order irrelevant).
