# Traffic reactions: players, horns, police, obstacles, being hit

Part of the ambient traffic spec (`traffic.md`). Normal driving is `traffic-driving.md`; this page
covers the car's other states and what makes it enter them. Arithmetic as in
`../physics/vehicle.md`, "Precision".

## States

| State | Name | Behaviour each frame |
|---|---|---|
| 0 | driving | `traffic-driving.md` |
| 2 | knocked | physical body (`Being hit`) |
| 3 | rejoining | drives a merge curve back onto a lane (`Rejoining traffic`) |
| 4 | avoiding | swerves around a player (`Avoiding a player`) |
| 6 | out | nothing (left standing until its road is emptied) |

A car "in accident" for the intersection checks of `traffic-driving.md` is any car not in state 0.
A state change stores the previous state. Each state's behaviour (driving, knocked, rejoining,
avoiding) has its own tick counter, a **start** and a **per-frame part**. In a car's update, if the
current state's counter is 0 the start runs, and then, **in the same update**, the per-frame part
(driving: "Starting a car on its rail", then one driving update). The per-frame part increments the
counter (driving at its end, unless it switched to avoiding; knocked and avoiding at their
beginning; rejoining after its move, unless it switched to avoiding). Counters are set to 0 when a
car is placed, when a car is knocked or ends its physics (all four), and for the state being left
on driving → avoiding, avoiding → rejoining, rejoining → avoiding and rejoining → driving. A state change made during an update (or by a collision during the physics
steps) takes effect at the car's next update, whose start then runs.

Player positions: **P_m** = the player car's model origin (its matrix position), **P_c** = its body
position (centre of mass; `../physics/vehicle.md`, "Centre of mass"); MM2 uses both in different
tests, as noted. **R_p** = the player car's model radius (`BODY` radius,
`../physics/collision-objects.md`, "Gathering"). Players are visited in the order they were added.
The traffic car's matrix rows X (right), Z (back), position V; front, back, left, right as in
`../formats/aivehicledata.md`; *v* its speed, *a* its acceleration, *A* and *G* its per-car constants;
vmax = road limit + *E* (without the 0.0001 of the driving target).

## Noticing a player

In the driving state, in a frame where the car moved (after its solve, `traffic-driving.md`, "One
driving update", step 3): for each player, if (dx × dx + dz × dz) < 625 with dx = V.x − P_m.x,
dz = V.z − P_m.z, and **in path** and not **screened**: the car switches to avoiding that player
(state 4) and stops its update for this frame (no react increment). The first player that qualifies
wins. A standing car (*v* ≤ 0) never starts avoiding. When the car later returns to driving, the
driving state starts again from "Starting a car on its rail".

**In path** (the player's body position within the car's corridor along its route): A = V; for
*i* = 1, 2, 3: B = the point of the car's route at distance (*i* × 10) + *x* (`road-network.md`, "Point
at a route distance", with this car's offset and front; height 0); dx = B.x − A.x, dz = B.z − A.z;
s = dz × dz + dx × dx; g = 1 / √s (0 if s = 0); ux = dx × g, uz = g × dz; rx = P_c.x − A.x,
rz = P_c.z − A.z; fwd = rz × uz + rx × ux; lat = ux × rz + rx × (−uz); if fwd < 11 and fwd > front
and lat > −left and lat < right: in path. Otherwise A = B and continue. (Three 10 m steps; only the
player's centre is tested, not its size.)

**Screened** (another car is between): *C* = the car ahead in this car's list lane; none: not
screened. Otherwise f_C = (Z.z × (V.z − C.z) + Z.y × (V.y − C.y)) + Z.x × (V.x − C.x) with C's
position, f_P the same with P_c; screened if f_C < f_P.

## Avoiding a player (state 4)

### Start

1. **Horn event** (`Horns and voices`).
2. *a*: if vmax ≤ *v*: *a* = *A* (a car cruising at its target speed is above vmax, so it starts by
   **accelerating**); else *a* = −((*v* × *v*) / ((*D* − *S*) + (*D* − *S*))) with
   *D* = √((d.z × d.z + d.y × d.y) + d.x × d.x), d = V − P_c, *S* = (R_p + *G*) + front.
3. Clearance *R* = (R_p + right) + 2.5.
4. Heading θ = atan2(Z.x, Z.z).
5. centred = 1 if −0.4 ≤ x₀ ≤ 0.4, else 0, with x₀ = (d.z × X.z + d.y × X.y) + d.x × X.x,
   d = P_m − V.

### Each frame

1. **Steer and move.** d = P_m − V; x = (d.z × X.z + d.y × X.y) + d.x × X.x (the player's offset to
   the car's right); z = ((−Z.z) × d.z + (−Z.y) × d.y) + (−Z.x) × d.x (its distance ahead);
   *r* = rnd() (drawn every frame, used or not); H = the player is holding the horn (a car with a
   siren never counts as honking).
   - if the car's road has road flag 0x01 (divided) and the car's lane is 0: if *r* < 0.5: *v* = *v* − 0.5 and
     φ = 0.4; else φ = atan2(x + *R*, z);
   - else if centred: *a* = *a* − 10; φ = atan2(x + *R*, z) if x < 0, else atan2(x − *R*, z);
   - else if x < −0.4: φ = atan2(x + *R*, z) if H or *r* < 0.7, else atan2(x − *R*, z);
   - else if x ≤ 0.4: φ = 0.4;
   - else: φ = atan2(x − *R*, z) if H or *r* < 0.7, else atan2(x + *R*, z).
   *m* = *v* / vmax; if centred and *v* > 5: *m* = 1. φ is clamped to −0.02 … 0.02;
   θ = ((φ × 1.5) × *m*) + θ. The car's rotation becomes the turn by θ about the vertical:
   X = (cos θ, 0, −sin θ), Y = (0, 1, 0), Z = (sin θ, 0, cos θ) (position kept). A growing θ turns the
   nose toward the car's −X (left), so these targets make the car turn **toward** the player:
   roughly 70 % of frames when the player is more than 0.4 m to a side (every frame while the player
   honks), 30 % away; with the player within ±0.4 m it turns left. (MM2's own side test is the
   reverse of an avoiding manoeuvre; keep it.) At most 0.03 rad per frame.
   Then *v* = (*F* × *a*) + *v*; if *v* < 0.25: *v* = 0; if vmax < *v*: *v* = vmax; step = *F* × *v*;
   *x* = step + *x*; V = V − step × Z (per component); then the free-heading solve
   (`traffic-visuals.md`).
2. **Braking.** If *v* < vmax: *a* = −((*v* × *v*) / ((*D*′ − *S*) + (*D*′ − *S*))) with *D*′ from
   d = V − **P_m** (not P_c). If *v* ≥ vmax, *a* is left as it is (the centred −10 accumulates per
   frame until the car is below vmax). A stopped car stays stopped while the player stays in front.
3. **Distance along the lane** is re-derived from the new position, so the cars behind keep a
   sensible distance to it. Room = Find(V, the car's last room); its component (`road-network.md`,
   "Room lists", "Component of a room"):
   - an intersection: *x* = λ + the overshoot past the road's end: side B
     (−Z[*n*−1].x) × (V.x − C[*n*−1].x) + (−Z[*n*−1].z) × (V.z − C[*n*−1].z), side A
     (V.x − C[0].x) × Z[0].x + (V.z − C[0].z) × Z[0].z (C = centre points, λ = the lane's length);
   - a road, or nothing (then the car's current road): **project** V on that road for the car's
     side: first with lane 0 to get the lateral offset lat; then lane *j* = the first line *j* (lanes
     and sidewalks) of the side with e[2*j*] < lat < e[2*j* + 1] in the band edges of the car's
     **current** road (`bai.md`, "Side block"), else the side's last lane; then project again with
     lane *j*, and *x* = the projected distance. The car's road, side and lane do not change.
   **Project** V on road *r*, side *s*, lane *j* (*j* is limited to the lane count): for
   *i* = 1 … *n* − 1, with P = lane point *i* of that lane: fwd = ((V.x − P.x) × Z[*i*].x +
   (V.y − P.y) × Z[*i*].y) + (V.z − P.z) × Z[*i*].z (Z of section *i* in road order, also on side A,
   an MM2 quirk); if fwd > 0: lat = −(((V.x − C[*i*].x) × X[*i*].x + (V.y − C[*i*].y) × X[*i*].y) +
   (V.z − C[*i*].z) × X[*i*].z); if |lat| < half width + 5: distance = D(*i*) − fwd, stop. If no *i*
   qualifies the distance is left unchanged.
4. **Exit test.** Q = the route point at distance 25 + *x* (as in "In path"); zQ =
   (Z.z × (V.z − Q.z) + Z.y × (V.y − Q.y)) + Z.x × (V.x − Q.x), lQ = ((−X.z) × (V.z − Q.z) + (−X.y) ×
   (V.y − Q.y)) + (−X.x) × (V.x − Q.x); lo = min(lQ, −3), hi = max(lQ, 3); zP, lP the same with P_c.
   **Corridor** = zP < zQ and zP > front and lo < lP < hi. **Ahead** = (Z × (V − P_c)) > front and
   |V − P_c|² < 625 (3D). If neither: switch to rejoining (state 3). There is no time limit.

While avoiding, the car keeps its place in its lane list (cars behind follow it by the re-derived
distance) but itself ignores other cars, lights and the lane.

## Rejoining traffic (state 3)

Used after avoiding a player and after a knocked car came to rest upright.

### Start

1. **Attempts**: if |V − (the position remembered at the last rejoin start)|² ≤ 1 (3D): attempts + 1;
   otherwise attempts = 1 and remember V. (Placing a car sets attempts = 1 and the remembered
   position to (0, 0, 0).) A knocked car whose attempts reached 3 gives up when it next rests
   (`End of physics`).
2. **Where is the car**: room = Find(V, the car's last room); in that room's component list
   (`road-network.md`, "Room lists"): the car's current road if listed as a road, else the first
   intersection, else the first road on which V is not off the road (on-road classification < 3,
   `road-network.md`), else nothing.
3. **On a road**: take the car out of every lane list of its current road on its current side.
   On the found road, the first section *i* (0 … *n* − 1) with fwd = d.x × Z[*i*].x + d.z × Z[*i*].z
   ≥ 0 and −hw < lat < hw, where d = V − C[*i*], lat = d.z × X[*i*].z + d.x × X[*i*].x and hw = the
   road's half width (`bai.md`): side = −1 if (the car's Z row · Z[*i*], x and z) < 0, else +1 (the
   side whose direction matches the car's heading: a car resting backwards rejoins the other
   direction); lane = the first lane *j* of that side with e[2*j*] < lat < e[2*j* + 1], e = that side's
   band edges (`bai.md`, "Side block"), else 0; base = D_lane(*i*) − fwd on side B, D_lane(*n* − *i* − 1) + fwd on side A (if no section
   qualifies, MM2 keeps the previous values). The car's road becomes the found road, its side and
   lane these; merge length *L* = 30. If (the road is not a freeway, or the side equals the car's
   previous next side) and the side is open: choose the next road, list lane = lane, sorted-insert
   the car into that lane's list at base + 30, and build the merge curve. Otherwise: state 6, and
   the car is now in no lane list.
4. **In an intersection**: the car stays in its lane list. Next road = the exit whose first centre
   segment (in its leaving direction, unnormalised) has the largest (d.y × f.y + d.z × f.z) + f.x ×
   d.x with the car's forward f = −Z row, among the open outgoing sides (only freeway exits if the
   current road is a freeway, or if its side has one lane and the previous next road was a freeway);
   next side by which end touches the intersection; the next lane is not recomputed. With Ps = the
   first point of lane 0 of the next road's side, Pe = the turn piece's start point of the car's
   current list lane (`road-network.md`, with its offset), *c* = |Pe.x − Ps.x| + |Pe.z − Ps.z|,
   dirN = Z[0] of the next road for side B, −Z[*n* − 1] for side A, and λ = the current lane's length:
   base = ((λ − front) + *c*) − ((dirN.x × (V − Ps).x + dirN.y × (V − Ps).y) + dirN.z × (V − Ps).z);
   total = (length of the next lane + λ) + *c*; *L* = 30 if base + 30 ≤ total, else total − base.
   If (the current road is not a freeway or its side equals the next side) and the next side is
   open: build the curve; otherwise state 6 (the curve is still built).
5. **Nowhere**: state 6 (the curve is still built from the old values).
6. **Merge curve**: *u* = 0; Q, *t*_Q = the route point and direction at distance base + *L*
   (`road-network.md`, "Point at a route distance", the rejoin variant); *t*_Q normalised then × *L*;
   1/*L* stored; one piece from P0 = V with T0 = (−*L*) × Z row to Q with T1 = *t*_Q; rail kind 3;
   go = 0; *v*ₜ = limit + *E*.

### Each frame

1. If the car is out (state 6), nothing.
2. In-path test for each player (no 25 m test, no screening): if a player is in path, switch to
   avoiding it.
3. If *v* > *v*ₜ: *v* = *v*ₜ; else if *v* < *v*ₜ: *v* = (*F* × *A*) + *v*.
4. *u* = (*v* × *F*) + *u*; *x* = *u* + base; solve (`traffic-visuals.md`; the section path never applies while rejoining).
5. If *u* > *L*: back to driving (state 0, which starts on its rail), indicator = 0.
6. Room update.

It does not follow other cars during the merge, and it collides with the city and objects (a contact
knocks it again).

## Horns and voices

Traffic sound belongs to the sound spec (`../audio/traffic-sounds.md`); the gameplay events it needs are:

- **"Honk on avoiding"**: when a car starts avoiding a player (only then). MM2 honks with about 50 %
  chance, a pattern picked at random from the car's `aud/cardata/ambient/<car>_horn` file (never its
  last pattern, which is kept for impacts; a file with one pattern never honks here); if it honked, a
  driver voice reaction may follow.
- **"Honk on impact"**: when a knocked car's body takes an impact with |Jx| + |Jy| + |Jz| at least
  the horn file's minimum impact force: 25 % chance, the file's last pattern.

These chances use the sound system's own random numbers (MM2 seeds them from the wall clock), never
the traffic generator. Traffic never honks when blocked, queued, at lights or at the player's horn,
and only cars near the listener (holding a 3D sound slot) honk at all.

## Police, sirens and other cars

- Traffic does **not** react to police cars, sirens or opponents: nothing in its driving reads them.
  Police and opponents affect traffic only through the AI-ahead mark at intersections
  (`traffic-driving.md`, "May the car enter?") and by colliding with it. A player driving a car with
  a siren gets no reaction either (the horn button runs the siren).
- In the driving state a car sees only other traffic cars (through the lane lists, the intersection
  checks) and intersection control. Props, pedestrians, opponents, police and players (other than by
  "Noticing a player") are met only by collision.
- **Being seen by other AI** (`driver-planner.md` §16 reads these lists, §18.5 the route place).
  At the end of each update of a car in any state (`traffic.md`, "One traffic car update", step 3),
  and once more at the end of "Starting a car on its rail", the car **registers** itself where it
  is, unless its room is 0:
  1. Component = `road-network.md`, "Component at a position, preferring a road", **second form**
     (only roads of kind 1), with *p* = the car's position (model origin), *q* = the car's current
     (rail) road and the hint = the car's own network-room memory (0 when placed; the room found is
     kept as the next hint; the avoiding and rejoining rules use the same memory).
  2. **A road** *R*: side *s*′ = the car's side if *R* is its current road, else its next side;
     section *v* = `section(p, s′)` of `road-network.md`, "Section of a point on a road" (so side B
     gives the raw section and side A *n* − raw), then clamped to 1 … *n* − 1. If (road, *v*, *s*′)
     equal the car's current registration, nothing changes. Otherwise the car is removed from its
     current registration and put at the **front** of road *R*'s traffic list for section *v* of side
     *s*′ (a singly linked list ending with an empty link).
  3. **An intersection**: if the car is already registered in an intersection, nothing changes
     (MM2 does not move it to another intersection's list when it passes straight from one to the
     next); otherwise it is removed from its road registration and put at the front of the
     intersection's traffic list.
  4. **Nothing**: the car is removed from its registration.
  Removing searches the list it was filed in (a car not found is left alone). Two MM2 slips:
  (a) when the car leaves a road for "nothing", MM2 searches the list of the car's **current** side
  rather than the side it was filed with; (b) when a car returns to the free list and is placed
  again, MM2 forgets its old registration without removing it (emptying a road clears all of that
  road's section lists, and a reset clears every list, so only a registration on a road that stays
  active is left behind). **Engine:** always remove from the list the car was filed in, and remove
  a car's registration when it returns to the free list.
  A car in its turn piece is normally registered in the intersection (intersection rooms list only
  the intersection), and on the next road (with the next side) as soon as its room's component is
  that road, whatever its rail kind.

  **Route place** (for other drivers' `window_place`, §18.5): the car's current (rail) road, its
  travel direction (+1 for side B) and its piece index *sx* = *k* in its own driving order: 1 …
  *n* − 1 on the lane, *n* while in its turn piece (the counter has passed the last piece). This is
  a different number from the registration section above, and a knocked or avoiding car keeps its
  last values. (A "current section" query of the car answers 0 while *k* = *n*, else *k*.)

  Avoidance by those other cars uses the car's box corners: front-left = V − front × Z − left × X,
  front-right = V − front × Z + right × X, rear-left = V + back × Z − left × X, rear-right =
  V + back × Z + right × X. Opponent and police driving and the intersection accident check use
  the registrations (`road-network.md`, "Obstacles"; `police.md`).
- **Re-routing and getting stuck.** A car about to enter an intersection that holds, or whose next
  road holds, a car not in state 0 chooses another next road (`traffic-driving.md`). There is no
  timer for being stuck: a car waits as long as its leader, the light or a player in front makes it
  wait; it gives up only through state 6 (rejoining failed or knocked too often).

## Being hit

### Who collides with what

The physics side (`../physics/collision.md`, the car-to-car collision spec) works with **movers**
(bodies that move and look for contacts) and **collidable objects** (things movers can hit).

- A traffic car that is **driving** (state 0) is never a mover: it never touches the city, props or
  another driving traffic car (two driving cars can overlap). It is a collidable object: its box
  (`../formats/aivehicledata.md`, "Derived numbers") placed with its current matrix.
- Movers that can hit it: the player's car (and trailer), opponents and police, other knocked
  traffic, moving props, and rejoining or avoiding traffic.
- A **rejoining** (state 3) or **avoiding** (state 4) traffic car is a mover that collides with the
  city and with collidable objects (not a full body yet).
- A **wreck** (below) is a mover that only looks for collidable objects.

**Any** contact (one impact or more) between a mover and a traffic car that has no body yet makes
the traffic car physical, whatever the speed or force: there is no threshold. For the contact the
scripted car is a static object at its current matrix (no sweep); before the impulse response is
applied it gets its body (below), so the response uses its mass and its rail velocity. A rejoining
or avoiding car that touches the city or any object becomes physical too. (`MaxDamage`,
`PtxThresh`, `MaxAng`, `Friction` and `Elasticity` of the data file play no part: MM2 compares
`MaxDamage` with a damage value that nothing increases.)

When the car becomes physical: speed *v* = 0 (after its body took the old speed), *v*ₜ = limit +
*E* + 0.0001, the tick counters of all its behaviours = 0, state = 2 (knocked), indicator = 3
(hazard lights, set when the knocked behaviour starts).

### The body

At most **32** traffic cars have a body at a time. A new body takes a free slot; if all 32 are in
use, the car that has held its slot longest (the first in the list of bodies; removing a body moves
the last one into its place) ends its physics (`End of physics`) and its slot is reused.

Setting up the body (the vehicle body of `../physics/vehicle-body.md`, without the car simulation):

- mass m = `Mass`; inertia from the box formula of `vehicle-body.md`, "Mass and inertia", with
  InertiaBox = `Size`; angular speed limit wmax = 5 rad/s per body axis (not 4π); speed limit 500;
- position and rotation = the car's matrix: the **centre of mass is the model origin** (`CG` only
  places the collision box);
- velocity v = ((−*s*) × Z.x, (−*s*) × Z.y, (−*s*) × Z.z) with *s* the car's speed and Z its matrix
  Z row; momentum P = m × v per component; angular velocity and momentum 0 (no spin, even in a
  turn);
- the "previous" matrix for the first sweep = the car's matrix with position − *F* × v;
- collision box material: elasticity 0.5, friction 1.0 for every traffic type (the file's values are
  not used);
- four **wheels** at the pivots of `WHL0` (front left), `WHL1` (front right), `WHL2` (rear left),
  `WHL3` (rear right), each with compression 0, deflections 0 and its own ground-probe state;
  `WHL4`/`WHL5` have no physics.

Each **physics step** (the steps of `../physics/vehicle.md`, "Time step", with step time *h*; knocked
traffic bodies are stepped with the other movers), in this order:

1. Gravity: F.y = m × (−19.6) + F.y.
2. **Rest check** (below).
3. Integration: `vehicle-body.md`, "Integration", explicit path, with the push and post-force
   handling as for the player's body.
4. The four wheels in order (forces for the next step's integration).
5. The car's matrix = the body's; its room = Find(position + Y row, its room).

**A wheel** (pivot o, wheel radius *r*, *k* = `Spring`, *c* = `Damping`, *K* = `RubberSpring`,
*D* = `RubberDamp`, λ = `Limit`; state: compression *q*, deflections *e*_lat and *e*_long; body rows
X, Y, Z, position p, velocity v, angular velocity ω; W₀ = (m × −19.6) × −0.25):

1. Attach point A.c = ((Z.c × o.z + X.c × o.x) + Y.c × o.y) + p.c.
2. *t* = *r* + λ; probe the ground (`../physics/collision.md`, "Ground queries", ground mask, nothing
   ignored) from S = *t* × Y + A to E = A − *t* × Y (per component).
3. No hit (or a zero normal): *q* = −λ, *e*_lat = *e*_long = 0, no force. Done.
4. Hit at point H with normal n and fraction τ: ρ = H − p;
   ω×ρ = (ρ.z × ω.y − ρ.y × ω.z, ρ.x × ω.z − ρ.z × ω.x, ρ.y × ω.x − ρ.x × ω.y);
   u.c = (ω×ρ).c + v.c; *u*_n = ((u.z × n.z) + u.y × n.y) + u.x × n.x; w.c = u.c − *u*_n × n.c.
5. *q*_old = *q*; *q* = ((*r* + *r*) + λ) − √((d.z × d.z + d.y × d.y) + d.x × d.x) × τ with d = S − E
   (so *q* = 0 when the wheel centre is *r* above the ground); *q*′ = (*q* − *q*_old) × (1/*h*),
   clamped to −3 … 3.
6. *f*_s = (*q* × *k* + *q*′ × *c*) + W₀, 0 if negative; *f*_n = ((n.z × Y.z + n.y × Y.y) + n.x × Y.x)
   × *f*_s.
7. Slip speeds: α = ((w.x × X.x + X.z × w.z) + X.y × w.y) (sideways), β = ((−Z.z × w.z + −Z.y × w.y)
   + −Z.x × w.x) (forward).
8. Deflection limit *m* = ((*f*_n × G) × 0.4) / *K*, G = the weather grip factor of the player's
   wheels (`../physics/vehicle-wheels.md`, "Surface": 1 normally, in rain 0.8 or 0.75 at night,
   and MM2 never puts it back to 1 after a rainy game); target ±*m* with the sign of the slip (0
   for no slip).
9. For each direction (sideways with α, forward with β): *e*_old = *e*; lo = min(*e*, *e* + slip × *h*),
   hi = max(*e*, *e* + slip × *h*); *e* = lo if target < lo, the target if target ≤ hi, else hi
   (the deflection follows the slip by at most |slip| × *h* per step, toward the limit);
   force = −(*K* × *e*) − ((*e* − *e*_old) × (1/*h*)) × *D*.
10. Total force G.c = (X.c × *f*_lat − Z.c × *f*_long) + *f*_n × n.c; add it to the body's force and
    (H − p) × G to its torque.

The wheels never roll: a knocked car slides like a car with locked wheels (about 0.4 × 19.6 =
7.8 m/s² of deceleration on flat ground) and is held up on springs.

**Rest check** (per step, before the integration; counters start at 0, "awake"):

1. If |ω|² > 0.01 and |ω_prev + ω|² > 0.018: moving (ω_prev = ω). Otherwise ω_prev = ω.
2. u = ((push + push sum) × (1/*h*)) + v per component; the same test with u_prev and 0.01 / 0.018
   (u_prev = u when not moving).
3. Pending collision impulse J: if |J × (1/m) + v|² − |v|² > 0.01: moving.
4. Not moving: "still" + 1; at 15 the body **sleeps** (velocities, momenta and forces 0).
   Moving: still = 0.
(|x|² = (x.x² + x.y²) + x.z².) So a body at rest for 15 steps in a row (0.25 s at 60 steps per
second) sleeps. A sleeping body that something moves again in a later step of the same frame wakes
(counters 0) and is not ended.

Once per frame, after all steps: if the body sleeps or its position y < −100, **end of physics**
for it; otherwise its room is updated.

### End of physics

1. Probe the ground from p + 0.5 × Y to p − 3 × Y (per component Y.c × 0.5 + p.c and p.c − Y.c × 3;
   ground mask, the car's room as hint).
2. **Upright** if the probe hits, ((n.y × Y.y + n.z × Y.z) + n.x × Y.x) ≥ 0.9 and the car is not a
   wreck: the car switches to **rejoining** (state 3), or to **out** (state 6) if its rejoin
   attempt counter is 3 (three rejoin starts within 1 m of the same spot); speed 0, *v*ₜ = limit +
   *E* + 0.0001, tick counters 0.
3. Otherwise the car becomes a **wreck**: it stays knocked (state 2) where it lies, frozen, for the
   rest of the session; a later hit gives it a body again, but when it rests it stays a wreck. MM2
   never clears the mark, not even when the car is later taken off the streets and placed somewhere
   else (the placed car then never rejoins traffic after a crash). **Engine:** keep the mark for the
   whole session, as MM2 does.
4. The car is collidable again and its slot is free. Its pose is not changed.

While knocked (state 2) the car's per-frame behaviour only copies its body's matrix into its own
(and a wreck without a body declares itself a mover that only looks for objects). It stays in its
lane list with its last distance and speed 0, so the cars behind stop behind it (a knocked car ahead
at the line is skipped, `traffic-driving.md`, step 5) and cars about to enter its intersection or
its road choose another road. Physics never removes a car from the streets: only emptying its road
or a reset does. A physical car's wheels are drawn with no spin at their wheel positions moved by
the deflections and compression: (o.x − *e*_lat × 0.2, o.y + *q*, o.z − *e*_long × 0.3), times the
body matrix; `WHL4`/`WHL5` at their pivots without spin.

Breakable parts (none on the shipped traffic types): an impact on a knocked car with impulse
magnitude |J| > 100 and ≥ 2500 knocks off the attached part whose pivot is nearest the contact
point; it becomes a loose prop with 8 random draws (direction, speed 10–12 m/s, spin 3–5 rad/s).
Parts come back when the car is placed again.

### What the car-to-car physics needs

- Bound: one box per type, corners CG ± Size/2 in the car's model frame (front at −Z), placed with
  the car's matrix (scripted) or the body matrix (knocked; same origin). E.g. `va_sedans_s`:
  x −0.919 … 0.920, y 0.258 … 1.381, z −1.973 … 2.314.
- Material: elasticity 0.5, friction 1.0 (combined with the other object's as `collision.md`
  says).
- A scripted car is a static object in the contact test and becomes a body (as above) before the
  response; it never pushes anything and is never pushed while scripted.
- Knocked cars are ordinary movers (as an opponent's car) for pair tests; rejoining and avoiding cars
  collide with the city and objects only.
- The "honk on impact" event (`Horns and voices`) takes |Jx| + |Jy| + |Jz| of each impact on a knocked
  car.

## Checked against

- Static analysis of the shipped game; `../formats/aivehicledata.md` lists the data values these
  rules use (`va_sedans_s`: mass 585.1 kg, box inertia (957.378, 1060.797, 226.358), W₀ =
  2866.97 N per wheel).
- `tools/ai/traffic.py data` prints every type's bumper and side distances used by "In path",
  the corners and the box.
