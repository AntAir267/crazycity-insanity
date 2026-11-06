# AI driver: modes, route following, speed and steering

The **driver** is the controller that turns a set of orders (a route through the street network, a
destination, a few tuning numbers) into a car's controls, once per frame. MM2 uses one driver per
AI-driven car: cops (`police.md`), race opponents and the other AI racers. This page covers the
driver's state, its four modes, how it follows the route, how it chooses throttle and brake (speed
control) and how it steers. Picking the next target point around obstacles is the **planner**
(`driver-planner.md`); finding the route is the **route search** (`route-search.md`); the road graph
and its queries are `road-network.md` and `../formats/bai.md`.

Outputs: the car's **steering**, **throttle**, **brake** and **handbrake** inputs (the same inputs
as the player's, `../engine/controls.md`; the driver writes them directly, without quantisation or
automatic reverse), the car's **throttle scale** and **friction handling** values, and in a few
places direct changes to the car body's momentum or orientation (said explicitly where they happen).
A change to the momentum (*P*, *L*: scaled by 0.85, 0.95 or 0.25, or set to 0) acts only when the
car's next integration takes the explicit path; a car standing or driving on its wheels takes the
semi-implicit path, which carries the velocities and rebuilds *P* and *L* from them, so there the
change has no effect (`../physics/vehicle-body.md`, "Semi-implicit path", for the exact condition).

Units: metres, seconds, radians, m/s. All arithmetic is float32 with one rounding per operation,
in the order written (`../physics/vehicle.md`, "Precision"). `atan2`, `tan` and `acos` are computed
in double precision; where a result is "not rounded" it enters the next operation unrounded and that
operation's result is rounded to float once. Comparisons are written so that a NaN goes the way MM2
sends it. `clamp(x, lo, hi)` means: lo if **not** (x ≥ lo); otherwise hi if x > hi; otherwise x (a
NaN gives lo).

Car quantities: *p* the body position, *X*, *Y*, *Z* the rows of the body rotation *R* (the car
faces −*Z*, its right is +*X*), *P* and *L* the body's linear and angular momentum
(`../physics/vehicle-body.md`), *v* the car's speed (`../physics/vehicle.md` step 2), *F* the frame
time. "Scale *P* by *k*" means each component *P.c* := *P.c*·*k*.

## What the driver needs from the road network

| Query | Meaning |
|---|---|
| road *R*: *n_R*, *V_R*[*i*], *X_R*[*i*], *Z_R*[*i*] | section count, centre points, section axes (X toward side A = left of the road direction; Z against the road direction) (`bai.md`) |
| road id, alley flag | the road's index; flag bit 0x02 |
| end 0 / end 1 of *R* | the intersection at section *n_R* − 1 / at section 0, and the traffic control there (0 stop sign, 1 light, 2 yield, 3 none) |
| sharp turns of *R* | the road's list of *m_R* bends in stored order (`driver-planner.md`, "Bends"); per record: section *s*, angle *θ*, radius *r*, tangent length *t*, corner point *C* (the planner's *v*, *A*, ρ, *dt*, *K*). *s*, *θ* and *C* are fixed at load; *r* and *t* are refreshed by the planner on every planning pass for the roads it plans on (`driver-planner.md`) |
| intersection *I*: id, centre, roads | id = index; centre point; the roads meeting there, in stored order |
| road between *A* and *B* | for each road of *A* in list order, for each road of *B* in list order: the first road found in both lists. Its direction flag is +1 if *A* is that road's end 1, else −1. If *A* or *B* is missing, or no road is shared: "none", and the caller's direction flag is left unchanged |
| section of a position | the "section of a point" query of `road-network.md`, counted in the road's own direction (+1) |
| index of a position | the "section index of a car on a road" query of `road-network.md` (the first section *i* ≥ 1 not yet reached in the road's direction, (*p* − *V*[*i*])·*Z*[*i*] > 0; *i* if the point is within the half width there, else *n_R* − 1; *n_R* − 1 if there is none); used only by "What others read" |
| position-on-road value | `road-network.md` ("Is a point on the road"), margin 0: 1 on the roadway, 2 on the sidewalk strip, 3 off the road |
| room lookup, room components | the room containing a point, from a hint room; each room's list of (kind, index) entries; kind 1 road, 2 shortcut, 3 intersection (`road-network.md`, "Room lists") |

A missing route entry (index outside 0 … *n* − 1) is "none", and so is its intersection.

## Driver state

| Name | Meaning |
|---|---|
| mode | DRIVE, TURN_BACK, RECOVER or HOLD |
| previous mode | the mode whose entry steps last ran; "none" after a reset |
| orders | see "Orders" |
| *w* | waypoint index: the route entry the car is heading for |
| lap | current lap (1 after orders) |
| route done | true when *w* ≥ *n* at the start of this frame's route step (read by the planner) |
| located kind, located index, room | where the car was last found in the road graph (kind 0 none, 1 road, 2 shortcut, 3 intersection) and its room |
| route window *S*₀, *S*₁, *S*₂, with directions *d*₀, *d*₁, *d*₂ | the road the car is on and the next two roads of its route; each may be none. *d* = +1: travelling in the road's own direction (toward end 0); *d* = −1: toward end 1 |
| *f* | the first window slot not taken by the route's own roads (0 … 3); the destination road goes there |
| last *S*₀ | *S*₀ when the junction geometry was last computed |
| junction angle *a*₀, *a*₁, turn sign τ₀, τ₁ | the turn from *S*₀ into *S*₁ and from *S*₁ into *S*₂ |
| junction corner *J*₀, *J*₁, radius *r*ⱼ₀, *r*ⱼ₁, tangent *t*ⱼ₀, *t*ⱼ₁ | geometry of the two junction turns (`driver-planner.md`, "Junction turns") |
| target *T* | the point the car steers at |
| brake′, throttle′, steering′ | the driver's own copies of its outputs |
| wrecked, wreck start | wreck flag and the clock time it was set |
| regenerate | repair a wrecked car after 5 s (set at creation; cops: no; opponents: in Circuit only, `opponents.md`) |
| car kind | index of the car's base name in: vppanoz 0, vpford 1, vpmustang99 2, vppanozgt 3, vpsemi 4, vpcaddie 5, vpbug 6, vppolice 7, vpbullet 8, vpbus 9; −1 for any other (exact, case-sensitive match; used by the planner) |
| turn-back counter | frames spent in TURN_BACK |
| stuck watch | state (IDLE, WATCH, STUCK), timer, start point (see "Stuck watch") |
| extents | front = −min.z, back = max.z, left = −min.x, right = max.x of the box of the car's collision bound (`../physics/car-collision.md` 3.1): for the box an AI car gets, min = c + (−h) and max = c + h as computed there, which can differ from the `.bnd` file's own min and max in the last bit (`vpbug` right 0.94275993, file 0.94276; 11 of the 14 shipped opponent cars have an extent that differs); for a model that already has the player's polygon bound, that bound's min and max (`../formats/bnd.md` item 6) |
| planner data | the planned paths, the active path, the reference points REF and REF0 … (`driver-planner.md`) |

The **intersection ahead** on window slot *k* is end 0 of *S_k* if *d_k* = +1, end 1 if *d_k* = −1.

### The planner's paths, as read by the driver

The planner leaves a set of candidate paths and picks one, the **active path**. A path is a list
of points; point 0 is the car's position at the planning pass raised by 1 m. Per point the driver
reads: its position, its distance along the path ℓ (ℓ of point 0 is 0; the planner stores 9999.0
in ℓ for a point that is the **destination itself**, added when the window's roads run out) and,
for point 0 only, its **arc** field, which the speed control uses as the first window slot to scan
for sharp turns, *k*₀ (`driver-planner.md`, "Records": 0 or 1 for a junction turn, 2 + *t* for bend
*t*; so *k*₀ ≥ 3 means no slot is scanned). *P*₀, *P*₁, *P*₂ below are the active path's first three
points and *c* its point count. If the planner chose no path, MM2 reads the planner's working path
in place of the active path, with *c* = 0 (`driver-planner.md`, "Choosing the path").

## Orders

The owner of the car (the cop logic, the race logic) gives orders whenever it wants; each set of
orders replaces the previous one:

| Order | Meaning |
|---|---|
| route *W*[0 … *n* − 1] | intersection ids from the route search; *n* may be 0 |
| destination *D*, direction *Dd* | where to go, and the heading wanted there |
| laps | number of laps of a circuit route; −1 = endless; cops: 0 |
| *s*₁ arrival speed | the speed to arrive at *D* with |
| *s*₂ stop distance | stop this far short of *D* (metres) |
| six switches | 1: not used by MM2; 2: avoid traffic; 3: avoid props; 4: avoid players; 5: avoid opponents (only once *w* > 2); 6: a planner path preference (all `driver-planner.md`). Cops: 0, 1, 1, 0, 1, 0 |
| *k*_thr throttle level | the throttle used when not braking; also the car's throttle scale |
| *k*_c corner factor | multiplies every corner speed limit |
| *k*_b brake threshold | a braking demand must exceed this to be acted on |
| *k*_p pad | added to the junction tangent lengths for the "near a junction" test; also a planner distance |

Cop values (`police.md`, "Driving orders"): switches 0, 1, 1, 0, 1, 0 (avoid traffic, props and
opponents, **not** players), *k*_thr = 1.0, *k*_c = 2.0 when following and 1.0 when blocking,
*k*_b = 0.7 (float 0.69999999), *k*_p = 75.0.

### Receiving orders

In this order:

1. Store the route *W* and *n*, laps, *D*, *Dd*, *s*₁, *s*₂, the switches and the four numbers;
   lap := 1.
2. *T* := *p*; the planner's reference points REF and REF0 := *p*; chosen path := none and the
   slot and progress of the planner's working record 0 := 0 (`driver-planner.md`, §3); window *S*₀,
   *S*₁, *S*₂ := none; last *S*₀ := none; *a*₀ = *a*₁ = 0 (the turn signs τ are left).
3. **Destination place** (kind and index): room := room lookup of *D* with hint 0; in that room's
   component list the first intersection; else the first road or shortcut whose position-on-road
   value for *D* is 1 or 2 **and** (*n* = 0, or one of its two ends is the intersection *W*[*n* − 1]);
   else kind none (index = the room).
4. **Locate** the car (basic lookup, room hint 0; "Locating the car"); the room found becomes the
   stored room.
5. By the located kind:
   - **road or shortcut *R***: *w* := 0, *S*₀ := *R*, *d*₀ := +1, *f* := 0; then the **route
     roads** (below). Mode := DRIVE.
   - **intersection *I***: *w* := 0, *f* := 0, *S*₀ := the first road of *I*'s list (the one
     merged, angle-ordered list of `../formats/bai.md` load step 4, so it may be a shortcut; MM2
     keeps no separate shortcut list), *d*₀ := +1 if
     *I* is that road's end 0, else −1; then the route roads. Mode := DRIVE.
   - **none** (off the network): *w* := 0, *f* := 0 (*S*₀ and *d*₀ as left by step 2 and earlier
     orders); then the route roads, except that *w* stays 0. Mode := RECOVER unless the mode is
     TURN_BACK (then unchanged).

   **Route roads**: if *n* ≥ 2: *S*₀ := road between *W*[0] and *W*[1] (with *d*₀), *f* := 1, *w* := 1;
   if *n* ≥ 3 also *S*₁ := road between *W*[1] and *W*[2] (with *d*₁), *f* := 2; if *n* ≥ 4 also
   *S*₂ := road between *W*[2] and *W*[3] (with *d*₂), *f* := 3. (The road between two route
   intersections and its direction: `route-search.md`, "The graph"; a direction flag 1 there is
   *d* = +1 here.)
6. **Start lateral offset** λ₀ (read by the planner): 0 if *S*₀ is none. Otherwise, with
   *n_R*, *V*, *X* of *S*₀: for a car on a road, *k* = the road's "section of a point" for *p* in
   direction *d*₀ (`road-network.md`), limited to 0 … *n_R* − 1, and *v* = *k* (*d*₀ = +1) or
   *n_R* − 1 − *k* (*d*₀ = −1); for a car at an intersection or off the network, *v* = 0
   (*d*₀ = +1) or *n_R* − 1 (*d*₀ = −1). Then
   ```
   d₀ = +1:  λ₀ = ((V[v].z − p.z)·X[v].z) + ((V[v].x − p.x)·X[v].x)
   d₀ = −1:  λ₀ = ((−X[v].x)·(V[v].x − p.x)) + ((−X[v].z)·(V[v].z − p.z))
   ```

The previous mode is not touched, so DRIVE's entry steps do not run again if the mode was already
DRIVE. Note that new orders replace HOLD: a car told to hold and then given orders drives (this is
why a cop reset to its parking spot ends up in DRIVE or RECOVER, `police.md`).

## Creation and reset

**Creation** (once per car): set up the car (car and paint rules: `police.md`, "The cop's car"),
extents from its bounding box, regenerate flag, car kind, attach the stuck watch, and override three
values of the car's unstuck helper (`../physics/vehicle-helpers.md`): `Rotation` = 0, `TimeThresh`
= 0.5 and the squared position threshold `P2` = 1.0 (whatever `PosThresh` the file gives). The
owner may override further (cops set `TimeThresh` = 0.75 afterwards). Then reset.

**Reset**: previous mode = none, mode = DRIVE, wrecked = false; the car is reset (vehicle spec);
route done = false, *w* = 1, lap = 1, *n* = 1, located kind = intersection, located index = 1;
no active path; brake′ = throttle′ = steering′ = 0; window empty; all planner points zeroed; stuck
watch IDLE with timer 0; wreck start = now. (The owner normally gives orders right after.)

## One frame

Each frame the owner either runs the driver's frame or mirrors another car instead
("Mirroring a car"; cops in BLOCK_WAIT). The driver's frame:

1. Car throttle scale := *k*_thr.
2. **Wrecked**: if **not** (car damage ≤ `MaxDamage`):
   1. If regenerate and wrecked and (now − wreck start) > 5.0 s: wrecked = false, repair the car
      (damage 0, broken parts restored; car damage spec). The frame ends (no inputs written).
   2. If not wrecked: wreck start = now, wrecked = true.
   3. Car steering, throttle and brake := 0 (the copies are not touched); scale *P* by 0.95.
      The frame ends.

   "Now" is real (wall-clock) time, not the sum of frame times: MM2 reads the operating system's
   high-resolution performance counter (its low 32 bits; the elapsed count is the difference
   taken modulo 2³² as an unsigned number, converted to float and multiplied by
   float(1/counter frequency); without such a counter, the millisecond system timer × 0.001), so
   the 5 s are real seconds whatever the frame times, and time spent with the game paused counts.
   The test is elapsed > 5.0 (strict). **Engine:** any monotonic real-time clock in seconds. (The
   difference only matters for cars that regenerate; cops do not.)
3. **Mode**: for DRIVE, TURN_BACK and RECOVER: if previous mode ≠ mode, run the mode's entry steps
   and set previous mode = mode; then run the mode's update. HOLD has no entry steps and does not
   change previous mode (so after HOLD → DRIVE the DRIVE entry only runs if previous mode was not
   already DRIVE). Any other mode value: nothing.

A mode change made during an update takes effect at the next frame.

## DRIVE

**Entry**: steering′ = 0; car steering, throttle, brake := 0; stuck watch state = IDLE; deactivate
the car's unstuck helper (`vehicle-helpers.md`: state INACTIVE, timer 0); *T* = *p*; the planner's
paths are cleared (no paths, no active path, all per-path flags and counts 0). If the car's gear
index is 0 (reverse), it becomes 2 (first) by a direct write: the gearbox's "set gear" bookkeeping
(timer, changed) is not done.

**Update**:

1. **Not drivable.** If the car's drivable switch is off (on from creation; game modes switch cars
   off, e.g. during a race start countdown): if the front-left wheel's grounded flag is set, car
   steering := 0, brake := 0, throttle := 1. The update ends.
2. Update the stuck watch ("Stuck watch").
3. If the car's unstuck helper is in **TURN**: run the route progress ("Route progress"), mode :=
   TURN_BACK, *P* := 0 and *L* := 0. The update ends.
4. If the stuck watch is **STUCK**: car steering := 1, brake := 0, throttle := 1, and the unstuck
   helper's state := INACTIVE (state only: its timer and start point are left). The update ends.
5. **Locate** ("Locating the car") with a preferred road: if *w* < *n*, the road between the
   intersections of *W*[*w* − 1] (of *W*[*n* − 1] when *w* = 0) and *W*[*w*] — its id, or none;
   otherwise the destination's index if the destination kind is road or shortcut, else none.
6. **Route step**:
   1. route done := (*w* ≥ *n*).
   2. Route progress ("Route progress").
   3. If *S*₀ ≠ last *S*₀: the planner's reference points REF and REF0 := *p*; last *S*₀ := *S*₀;
      compute the junction angles ("Junction angles") and the junction turns (`driver-planner.md`,
      "Junction turns").
   4. Run the planner (`driver-planner.md`).
   5. *T* := position of *P*₁.
   6. Speed control ("Speed control"): sets brake′ and throttle′.
7. **Destination behind.** If ℓ of *P*₁ = 9999.0, and `(p.x − D.x)² + (p.z − D.z)²` < 625.0, and
   `(−Z.z)·Dd.z + (−Z.x)·Dd.x` > 0.0, and `(D.z − p.z)·(−Z.z) + (D.x − p.x)·(−Z.x)` < 0.0 (the car
   is within 25 m, roughly facing the wanted direction, and *D* is behind it): brake′ := 1,
   throttle′ := 0 and *g* = −(*T* − *p*) (each difference rounded, then negated). Otherwise
   *g* = *T* − *p* (each component rounded). (The sums here have two terms; order is free.)
8. **Steering**:
   ```
   α = atan2(g.z·X.z + g.x·X.x, −(g.z·Z.z + g.x·Z.x))       (double, not rounded)
   s = float(float(α × 1.33) × 1.428)
   steering′ = clamp(s, −1, 1)
   ```
9. **Handbrake**: 1 if *v* > 30.0 and (*s* < −1 or *s* > 1) (the unclamped *s*), else 0.
10. Car steering := steering′, brake := brake′, throttle := throttle′, handbrake as in step 9.
11. Car friction handling := 2.0 if the car touched the **player's car** during the last physics
    update, else 1.0. (The mark is set by the collision pass when this car's body hits the player's
    car and cleared at the start of every physics update; friction handling: `vehicle-wheels.md`.)

## Route progress

Run in DRIVE step 6.2 and when DRIVE or RECOVER switches to TURN_BACK. It uses the located kind and
index of the last lookup.

1. If *w* < *n*, the located kind is intersection and the located index = *W*[*w*] (the car has
   reached the intersection it was heading for):
   1. If *w* > 0: *R* = road between *W*[*w* − 1] and *W*[*w*]; if *R* ≠ *S*₀, **enter road** *R*.
      If no road joins the two intersections (*R* none) while *S*₀ is not none, MM2 enters the
      missing road and crashes (it reads the missing road's ends). **Engine:** skip this step, as
      if *R* = *S*₀. (A car can also pass such a *W*[*w*] without being located there: "enter
      road" step 2 moves *w* on as soon as it joins a road touching *W*[*w* + 1].) Shipped route
      files have such pairs: London `crash10-0.opp` 242 → 254 and
      `race6-a-8.opp` 141 → 307, SF `race10-a-6.opp` 52 → 15 and `race7-p-6.opp`, `race7-p-7.opp`
      188 → 47 (and the route points the engine leaves out, `../formats/opp.md`, make 15 more).
   2. *w* := *w* + 1.
   3. If *w* = *n* and (lap < laps or laps = −1): *w* := 1, lap := lap + 1.
2. Then, by the located kind:
   - road or shortcut: **enter road** (the road with the located index);
   - intersection, with *f* < 3 and destination kind = road (kind 1 only): look for the
     destination's road among the roads of the located intersection; if found, *S_f* := it and
     *d_f* := +1 if its end 1 is the located intersection, else −1;
   - otherwise nothing.

**Enter road *R*** (the car is on *R*):

1. If *w* ≥ *n* (route finished): *S*₀ := *R*, *S*₁ := *S*₂ := none; *i* := section of *p* on *R*,
   clamped to 0 … *n_R* − 1; *d*₀ := +1 if `Z_R[i].x·Z.x + Z.z·Z_R[i].z` ≥ 0.0, else −1. Done.
2. If end 0 or end 1 of *R* is the intersection *W*[*w* + 1], or (*w* = *n* − 1 and the
   destination index equals the located index — kinds are not compared): *w* := *w* + 1.
3. Let *I* = *W*[*w*] (with the new *w*).
   - End 0 of *R* is *I*: *S*₀ := *R*, *d*₀ := +1, *f* := 0, then **fill the window**.
   - Else end 1 of *R* is *I*: *S*₀ := *R*, *d*₀ := −1, *f* := 0, then fill the window.
   - Else (the car is on a road that does not touch *I*): for each road *Q* of end 0's intersection
     of *R*, in list order: if end 0 of *Q* is *I* → *S*₀ := *R*, *d*₀ := +1, *S*₁ := *Q*,
     *d*₁ := +1; else if end 1 of *Q* is *I* → *S*₀ := *R*, *d*₀ := +1, *S*₁ := *Q*, *d*₁ := −1.
     If none matched, the same over the roads of end 1's intersection of *R* with *d*₀ := −1. On a
     match: *S*₂ := none if *w* + 1 ≥ *n*, else the road between *W*[*w*] and *W*[*w* + 1]; done
     (*f* unchanged). No match: nothing changes (MM2 logs "lost").

**Fill the window** (*k* = *w*; "final lap" means lap ≥ laps and laps ≠ −1):

| Case | *A*, *B*, *C* | *f* |
|---|---|---|
| *k* = *n* − 1, not final lap | *W*[*k*], *W*[1], *W*[2] | 3 |
| *k* = *n* − 1, final lap | *W*[*k*], none, none | 1 |
| *k* = *n* − 2, final lap | *W*[*k*], *W*[*k* + 1], none | 2 |
| *k* = *n* − 2, not final lap | *W*[*k*], *W*[*k* + 1], *W*[1] | 3 |
| otherwise | *W*[*k*], *W*[*k* + 1], *W*[*k* + 2] | 3 |

*S*₁ := road between *A* and *B* (with *d*₁), *S*₂ := road between *B* and *C* (with *d*₂). Then,
if the destination kind is road or shortcut and 1 ≤ *f* ≤ 2: look for the destination's road among
the roads of the intersection *W*[*n* − 1]; if found, *S_f* := it, *d_f* := +1 if its end 1 is
*W*[*n* − 1], else −1.

(The two lap wraps differ: route progress wraps *w* to 1, RECOVER's target selection to 0.
Wrapping to 1 suits a circuit route whose last intersection repeats its first.)

## Locating the car

Room := room lookup of *p* with the stored room as the hint; then, in the room's component list:

- **With a preferred road** (DRIVE): if a preference exists, the first entry (road, preferred) and
  else the first entry (shortcut, preferred); else the first intersection entry; else the first
  road or shortcut entry whose position-on-road value for *p* is < 3; else located kind := none
  (the located index is kept).
- **Basic** (RECOVER, orders): the first entry that is a road or an intersection (roads are taken
  without a position test); else the first shortcut entry with position-on-road value < 3; else
  located kind := none and located index := the room.

## Junction angles

For *k* = 0, 1 with *A* = *S_k*, *B* = *S_k*₊₁: if either is none, *a_k* = 0 and τ*_k* = 0.
Otherwise let *u* be *B*'s first segment in travel order (*d_B* = +1: *V_B*[1] − *V_B*[0];
*d_B* = −1: *V_B*[*n_B* − 2] − *V_B*[*n_B* − 1]; x and z rounded) and:

```
d_A = +1:  e = n_A − 1;  y = u.z·(−X_A[e].z) + u.x·(−X_A[e].x);  x = (−Z_A[e].z)·u.z + (−Z_A[e].x)·u.x
d_A = −1:                y = u.z·X_A[0].z + u.x·X_A[0].x;        x = u.z·Z_A[0].z + u.x·Z_A[0].x
a_k = float(atan2(y, x))          (positive: B turns right of A's travel direction)
τ_k = +1 if atan2 ≥ 0.7;  −1 if atan2 ≤ −0.7 or NaN;  0 otherwise   (tested before rounding)
```

They are computed only when *S*₀ changes (DRIVE step 6.3) and when RECOVER enters the route
(below). A window change that keeps *S*₀ leaves them as they were.

## Speed control

"**Brake with b**" means: brake′ := clamp(*b*, 0, 1), throttle′ := 0 and scale *L* by 0.85.

```
need(vmax, e) = (v − vmax) / (((e / v) × 1.2) × 19.8)     (each operation rounded)
```

1. **Corner of the path** (only if *c* > 2): θ = the vector angle between *u* = *P*₁ − *P*₀ and
   *w* = *P*₂ − *P*₁ (3D, components rounded; vector angle below). If θ > 0.7:
   ```
   h = float(float(3.14 − |θ|) × 0.5)          (θ not rounded)
   vmax = float(√(float(float(float(tan(h) × 10.0) × 1.2) × 19.8)) × k_c)   (tan not rounded)
   b = need(vmax, ℓ of P₁) if v > vmax, else 0
   ```
   If *b* > *k*_b: brake with *b*; speed control ends here.
2. brake′ := 0, throttle′ := the car's throttle scale (= *k*_thr).
   *q* = `(p.x − D.x)² + (p.z − D.z)²` (rounded). If *q* < 5000.0 (a NaN counts as yes) and
   *w* ≥ *n* and lap ≥ laps: **destination approach** (3), otherwise **sharp turns** (4) and then
   **junctions** (5).
3. **Destination approach**: *e* = float(√*q*) − *s*₂; *b* = need(*s*₁, *e*) if *v* > *s*₁, else 0.
   If float(*e* × 0.014) < *b* (0.014 is the **double** constant; the product is rounded to float),
   brake with *b*. Then, if *e* < 2.5 and *s*₁ ≤ 0.0: brake′ := 1, throttle′ := 0. Done.
   (When *v* ≤ *s*₁ and *e* < 0 — inside the stop distance — the test 0.014·*e* < 0 holds: brake
   with 0, i.e. throttle 0 and *L* scaled.)
4. **Sharp turns**: if *k*₀ < 3, for slots *k* = *k*₀ … 2 with *S_k* not none, *R* = *S_k*, and
   for each sharp turn *i* = 0 … *m_R* − 1 of *R*:
   - *s* = the section of record *i* in stored order; the other fields (θ, *r*, *t*, *C*) come from
     record *i* if *d_k* = +1 and from record *m_R* − 1 − *i* if *d_k* = −1 (for *d_k* = −1 and
     *m_R* > 1 the section and the other fields come from different records; MM2 does this);
   - vmax = float(√(float(float(*r* × 1.2) × 19.8)) × *k*_c);
   - *cx* = *P*₀.x − *C*.x, *cz* = *P*₀.z − *C*.z;
   - *d_k* = +1: *e* = float(*cz*·*Z_R*[*s*].z + *cx*·*Z_R*[*s*].x) − *t*;
   - *d_k* = −1: *j* = 1; if *s* + 2 < *n_R*: *g* = *V_R*[*s* + 1] − *V_R*[*s*] (x, z rounded) and
     if float(atan2((−*X_R*[*s*].x)·*g*.x + (−*X_R*[*s*].z)·*g*.z, (−*Z_R*[*s*].x)·*g*.x +
     (−*Z_R*[*s*].z)·*g*.z)) ≠ θ (exact float comparison), *j* = 2. With *Z′* = *Z_R*[*s* + *j*]:
     *e* = float((−*Z′*.x)·*cx* + (−*Z′*.z)·*cz*) − *t*;
   - *b* = need(vmax, *e*) if *v* > vmax, else 0. If *b* > *k*_b: brake with *b* and leave both
     loops (continue with 5).

   *e* is how far ahead of *P*₀ the turn's arc begins; turns behind give *e* < 0 and a negative *b*.
5. **Junctions**. Junction 0 is **near** if *S*₀ is not none, **not** (−0.7 ≤ *a*₀ ≤ 0.7) (a NaN
   counts as near), and `(p.x − J₀.x)² + (p.z − J₀.z)²` < `(t_j0 + k_p)·(t_j0 + k_p)` (car position).
   - If junction 0 is near: vmax = float(√(float(float(1.2 × *r*ⱼ₀) × 19.8)) × *k*_c), and vmax ×
     0.5 if *S*₁ exists and is an alley. *b* = 0; if *v* > vmax: with *cx* = *P*₀.x − *J*₀.x,
     *cz* = *P*₀.z − *J*₀.z,
     ```
     d₀ = +1:  e = float(cz·Z_S0[n−1].z + cx·Z_S0[n−1].x) − t_j0
     d₀ = −1:  e = float((−Z_S0[0].z)·cz + (−Z_S0[0].x)·cx) − t_j0
     b = need(vmax, e)
     ```
     If *b* > *k*_b and *b* > brake′: brake with *b*. Speed control ends (junction 1 is not looked
     at while junction 0 is near, whatever *b* was).
   - Otherwise, junction 1 is near if *S*₁ is not none, not (−0.7 ≤ *a*₁ ≤ 0.7), and
     `(p.x − J₁.x)² + (p.z − J₁.z)²` < `(t_j1 + k_p)²` (same form). If so: vmax from *r*ⱼ₁ as above,
     × 0.5 if *S*₂ exists and is an alley; *b* = 0 (so for *v* ≤ vmax the test below asks 0 > *k*_b,
     false for every shipped row); if *v* > vmax: *e* = float(√((*P*₀.x − *J*₁.x)² +
     (*P*₀.z − *J*₁.z)²)) − *t*ⱼ₁ (straight-line distance) and *b* = need(vmax, *e*). If *b* > *k*_b
     and *b* > brake′: brake with *b*.

Speed control may scale *L* by 0.85 twice in one frame (a sharp turn, then a junction).

**Vector angle** of *a*, *b*: `A = (a.x² + a.y²) + a.z²`, `B = (b.x² + b.y²) + b.z²`,
`k = 1/float(√(B·A))`; if *k* = 0 the angle is 0; `c = float(((b.x·a.x + b.z·a.z) + b.y·a.y)·k)`;
if *c* > 0.99999988 the angle is 0; if *c* < −1.0 it is 3.1415927; otherwise acos(*c*) (double,
not rounded). A zero-length segment gives NaN, which fails the θ > 0.7 test.

## TURN_BACK (backing round)

Entered when the car's unstuck helper reports TURN (the car is stuck nose-in while steering).

**Entry**: counter := 0; *T* := position of *P*₁ (all three coordinates, as stored); if the gear
index is not 0, set gear 0 (reverse) with the gearbox's "set gear" rule
(`../physics/vehicle-powertrain.md`).

**Update**:

```
g = T − p                                              (3D, components rounded)
y = ((g.z·X.z) + (g.y·X.y)) + (g.x·X.x)
x = (((−Z.z)·g.z) + ((−Z.y)·g.y)) + ((−Z.x)·g.x)
α = float(atan2(y, x))
```

- If −0.1 ≤ α ≤ 0.1 (the nose points at *T* to within 0.1 rad): car steering := 0; **turn the
  car body** by α about its own *Y* row, *R* := *R*·M(*Y*, α) (matrix product of
  `vehicle-body.md`), then **finish**. M(axis, α) is MM2's axis-angle matrix:
  - α = 0: identity.
  - Two axis components exactly 0 (any length of the third): a coordinate rotation by α if the
    remaining component is > 0, by −α otherwise (an all-zero axis counts as the Z case), with
    *c* = float(cos), *s* = float(sin): about X rows (1, 0, 0), (0, c, s), (0, −s, c); about Y
    (c, 0, −s), (0, 1, 0), (s, 0, c) (`vehicle-helpers.md`, "RotY"); about Z (c, s, 0),
    (−s, c, 0), (0, 0, 1). A car standing exactly level (*Y* = (0, *y*, 0)) takes the Y case.
  - Otherwise the axis is normalised (`l2 = (z·z + y·y) + x·x`, `k = 1/√l2`, 0 if l2 = 0, axis
    components × *k*) and `Rot(axis, α)` of `vehicle-body.md` is used.
- Otherwise: car steering := clamp(float(α × −2.857143), −1, 1). If counter > 65: **finish**.
  Else car throttle := 0.85, car brake := 0, counter := counter + 1.

So the car reverses at throttle 0.85 with opposite lock for at most 66 frames, then gives up.

**Finish**: mode := DRIVE if *S*₀ is not none, else RECOVER; deactivate the unstuck helper; scale
*P* by 0.25 and *L* by 0.25; car throttle := 0, car brake := 1.

## RECOVER (off the road network)

Used when the car is not on a road, shortcut or intersection (orders given there) or when a
turn-back finishes with an empty window. The car drives straight at the next route intersection.

**Entry**: the planner's paths are cleared (as DRIVE entry); stuck watch IDLE; deactivate the
unstuck helper; reverse → first gear as in DRIVE's entry. Inputs and *T* are not touched.

**Update**:

1. Not drivable: as DRIVE step 1.
2. Stuck watch update; unstuck helper TURN → as DRIVE step 3 (route progress, TURN_BACK, *P* = *L*
   = 0); stuck watch STUCK → as DRIVE step 4.
3. Locate (basic).
4. **Target**:
   - If *w* < *n*: *T* := centre of *W*[*w*]. If the located kind is intersection and the located
     index = *W*[*w*] (the car has reached the route): *S*₀ := road between *W*[*w*] and
     *W*[*w* + 1], *S*₁ := road between *W*[*w* + 1] and *W*[*w* + 2], *S*₂ := road between
     *W*[*w* + 2] and *W*[*w* + 3] (each with its direction); *w* := *w* + 1; compute the junction
     angles (not the corners); mode := DRIVE; if *w* = *n* and (lap < laps or laps = −1): *w* := 0,
     lap := lap + 1.
   - Else (route finished): if *s*₂ = 0, *T* := *D*. Otherwise *e* = √((*p*.z − *D*.z)² +
     (*p*.x − *D*.x)²); if *e* ≥ *s*₂: *h* = (*e* − *s*₂)/*e* and *T.c* := (*D.c* − *p.c*)·*h* +
     *p.c* for c = x, y, z; else *T* := *p*.
   - Path 0 becomes the active path with *c* = 0, and its point 0 gets *k*₀ = 0 and road slot 0
     (its position is whatever the planner last left there, so the sharp-turn and junction
     distances below are measured from a stale point).
   - *T*.y := *T*.y + 1.0.
   - Speed control (step 1 is skipped since *c* = 0).
5. **Steering**: with *g* = *T* − *p*: `α = atan2(g.z·X.z + g.x·X.x, −(g.z·Z.z + g.x·Z.x))`,
   steering′ := clamp(float(α × 1.33), −0.75, 0.75) (α not rounded).
6. Car steering := steering′, brake := brake′, throttle := throttle′. (Handbrake and friction
   handling are not touched.)

## HOLD (stopped)

Every frame: *T* := *D*; with *g* = *D* − *p*, `α = atan2(g.z·X.z + g.x·X.x, −(g.z·Z.z + g.x·Z.x))`,
steering′ := clamp(float(α × 1.33), −1, 1); throttle′ := 0, brake′ := 1; car steering := steering′,
throttle := 0, brake := 1; the planner's path count and the point count of path 0 := 0. Nothing else
(no stuck checks, no drivable test). Cops use HOLD when parked and when a chase ends (`police.md`).

## Mirroring a car

Instead of the driver's frame, the owner may make the car copy another car *Q* for a frame (cops in
BLOCK_WAIT; `police.md`, "Holding the block", has the formulas). It sets *T* := *Q*'s position,
the outputs and their copies. It does not set the throttle scale and does not run the wreck test.

## Stuck watch

The driver's own stuck detector, separate from the car's unstuck helper (whose WATCH state it
uses to start). Constants: time 0.3 (float 0.30000001), squared start radius *P*2w =
float(0.6 × 0.6) = 0.36000001 (0.6 as float, squared once), squared move radius *M*2w = 1.0, turn
rate 1.0 rad/s. "Wants to go" = `0.75 × (throttle scale) < throttle` and `|steering| < 0.5`, using
the car's current throttle and steering inputs (those of the previous frame); in STUCK only the
throttle part is tested.

Update (DRIVE and RECOVER step 2), with *e* = start − *p* recomputed for every test:

1. If `((e.z·e.z) + (e.y·e.y)) + (e.x·e.x)` > *P*2w: state := IDLE.
2. If the unstuck helper is in WATCH and state = IDLE: timer := 0, start := *p*, state := WATCH.
3. WATCH: timer := *F* + timer. If `e.x·e.x + e.z·e.z` < *P*2w and timer ≥ 0.3 and the car wants
   to go: state := STUCK; done. Otherwise, if `((e.z·e.z) + (e.y·e.y)) + (e.x·e.x)` > *M*2w,
   state := IDLE.
4. STUCK: if that 3D sum > *M*2w: state := IDLE. Else if `0.75 × (throttle scale) < throttle`:
   turn the body about world +Y by −float(float(σ × 1.0) × *F*), σ = +1 if steering > 0, −1 if
   steering < 0 (or NaN), 0 if 0 — the same rotation and product as the unstuck helper's TURN
   (`vehicle-helpers.md`). Else state := IDLE.

While STUCK, DRIVE and RECOVER hold steering +1 and full throttle and keep the unstuck helper
INACTIVE, so from the second STUCK frame on the body is turned by −*F* about +Y every frame (1 rad/s)
until the car is more than 0.6 m (3D, step 1) from where the watch started; the 1 m tests can
never fire first.

The stuck watch is reset (IDLE, timer 0) with the driver.

## Yield marks: the intersections ahead

Before the AI cars update in a frame, and again after (`police.md`, "Effect on traffic"), each
driver marks the intersections ahead on its window: for every slot *k* with *S_k* not none, let
*I* be the intersection ahead on slot *k*; for every road *Q* meeting *I*, take *Q*'s traffic
control at its end that is *I* (end 0 if *Q*'s end 0 is *I*, else end 1); if it is a stop sign (0)
or a traffic light (1), *Q*'s **AI-ahead** mark := 0 (before) or 1 (after). Traffic does not enter
from a marked road (`traffic-driving.md`). No list of marked roads is kept: a mark set after the
update is cleared only by a clearing pass of a driver whose window still leads to that intersection,
or by a road reset; a window changed between the setting and the clearing pass (a driver reset
outside the AI update) leaves its old marks set.

## What others read from a driver

Other AI and the planner query each driver for: its position *p* and speed *v*; its extents; its
current road id (the id of window road *S_j*, *j* being the road slot recorded in the planner's
working point 0); its
section on *S*₀ (the road's "index of a position" query, mirrored to *n* − 1 − index when
*d*₀ = −1) together with which of three given roads is *S*₀ (0, 1, 2 or −1 when not listed or *S*₀
is none); current lane −1 and current section 0 (fixed answers). The planner's obstacle scan
(`driver-planner.md` §16 step 4) and `window_place` (§18.5) ask the last of these, so for them an
AI car's road is its *S*₀ (window slot 0) and its section is its section on *S*₀; the current road
id above is a different answer and is not used there.

## Interface with the planner

The planner runs once per DRIVE frame (step 6.4), after the window, the waypoint, route done and
(when *S*₀ changed) the junction angles, junction turns and reference points are up to date. It reads the
whole driver state (*p*, the car rows, the window and *f*, *w*, *n*, route list, lap, *D*, *s*₂,
the switches, *k*_p, the extents, car kind, route done, the destination component, the lateral
offset recorded with the orders). The junction-corner routine (`driver-planner.md`) runs in every
planning pass (before the paths are built) and also in DRIVE step 6.3. The driver uses from the planner:

| Planner output | Used for |
|---|---|
| active path index; points *P*₀, *P*₁, *P*₂, count *c* | target *T* = *P*₁ (DRIVE; TURN_BACK entry); path-corner check (*c* > 2); distances from *P*₀ |
| ℓ of *P*₁ | path-corner braking distance; the 9999.0 destination marker (DRIVE step 7) |
| *k*₀ of *P*₀ | first window slot scanned for sharp turns |
| sharp-turn *r*, *t* of the window's roads | sharp-turn speeds and distances |
| junction corners *J*, *r*ⱼ, *t*ⱼ | junction speeds and distances (also computed by the driver when *S*₀ changes) |

The driver itself never draws random numbers (none in any mode, the stuck watch, the wreck
repair, or the lookups).

## Checked against

Machine code of `midtown2.exe` (retail English, Nov 3 2000); every grouping above read from the x87
instruction order.

- Steering: 1.33 = 1.33000004 and 1.428 = 1.42799997 (DRIVE, mirror), clamps ±1 (DRIVE, HOLD) and
  ±0.75 (RECOVER); handbrake above 30.0; friction handling 2.0 / 1.0.
- Destination behind: 9999.0 marker, 625.0, the two dot products with their zero tests.
- Speed control: 1.2 = 1.20000005, 19.8 = 19.7999992, 0.7 = 0.69999999, 3.14 = 3.14000010, 0.5,
  10.0, 5000.0, 0.014 as a double (0x3F8CAC083126E979), 2.5, brake damping 0.85 = 0.85000002;
  junction "near" pad squared as (t + pad)·(t + pad); alley = road flag 0x02; junction 1 skipped
  when junction 0 is near; both junctions' *b* start at 0 before the speed test; vector angle
  cut-offs 0.99999988 and 3.1415927.
- Wreck: 0.95 = 0.94999999 per frame, 5.0 s (strict, elapsed counter difference as an unsigned
  32-bit number × float(1/frequency)); cops created with regenerate off.
- Extents: read from the bound the car model keeps for collisions (the AI box's derived min and
  max); the 14 opponent cars' boxes against their `.bnd` boxes: 11 differ in at least one extent
  in the last bit.
- Route progress 1.1: no test for a missing road before entering it.
- TURN_BACK: ±0.1 = ±0.100000001, −2.857143 = −2.85714293, 65 (66 throttle frames), throttle 0.85,
  finish damping 0.25 on *P* and *L*.
- Stuck watch: 0.3 = 0.30000001, 0.6² = 0.36000001 (0x3EB851EC), 1.0, 1.0 rad/s, 0.75.
- Unstuck-helper overrides at creation: Rotation 0, TimeThresh 0.5, squared position threshold 1.0
  (cops then set TimeThresh 0.75).
- Orders: the six switches and four numbers are stored as listed; the throttle level is copied to
  the car's throttle scale at the start of every frame; MM2's engine never reads that scale itself.
- Mode dispatch: HOLD has no entry and leaves the previous mode; unknown modes do nothing.
- Lap wraps: route progress → *w* = 1, RECOVER → *w* = 0.
- No random draws in the driver (all callees checked).
