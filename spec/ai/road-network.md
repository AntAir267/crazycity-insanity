# Road network: lane paths, turns, look-ups

What the AI computes from the loaded network (`spec/formats/bai.md`): the paths cars follow along
lanes and through intersections, how the next road is chosen at the end of a lane, how a world
position is placed on the network, and the graph the police route over. Traffic lights and stop
signs are `traffic-control.md`. How fast cars move along these paths, spawning and reactions belong
to the traffic spec; police and opponent driving to theirs.

All arithmetic is float32 with one rounding per operation, evaluated left to right as written
(`spec/physics/vehicle.md`, "Precision"). Formulas that MM2 groups differently say so.

## Terms

- **Road** *r* with *n* sections, centre points C[*i*], section axes X[*i*] (toward side A), Y[*i*]
  (up), Z[*i*] (against the road direction), tangents T[*i*], centre distances CD[*i*] (all from
  the file, after the load procedure of `bai.md`).
- **Side** *s*: +1 for side B (travels section 0 → *n* − 1), −1 for side A (travels *n* − 1 → 0).
  A side is **open** when bit 0 of its side flags is clear (ambient traffic may use it).
- **Lane** *l* of side *s*: its points P[0 … *n* − 1] and line distances D[0 … *n* − 1] are the
  file's line *l* of that side; after loading they are in **travel order** for both sides (point 0
  where the traffic enters the road). The section of travel point *i* is sec(*i*) = *i* on side B,
  *n* − 1 − *i* on side A. X(*i*) below means X[sec(*i*)].
- Directions of a side (unit vectors from the file, not renormalised):
  - travel tangent at point *i*: Tan(*i*) = T[*i*] on side B, −T[*n* − 1 − *i*] on side A;
  - **start direction** S = −Z[0] on side B, Z[*n* − 1] on side A (direction of travel where the side
    enters the road);
  - **end direction** E = −Z[*n* − 1] on side B, Z[0] on side A (direction of travel where it leaves).
- The side's **entry intersection** is the one at end 1 (side B) or end 0 (side A); its **exit
  intersection** is the other one: side B leaves at end 0's intersection, side A at end 1's.
- For a road *q* meeting intersection *I*, its **outgoing side** at *I* is +1 if *q*'s end 1 names
  *I* (traffic leaving *I* along *q* travels the road direction), else −1.
- `seglen(k)` = D[*k*] − D[*k* − 1] of the lane in question (3D length of its segment *k*).

## Lane paths

A car following the network carries two numbers of its own (the traffic spec sets them):

- **o**, its lateral offset: a fixed random shift of its path along the section X axes (MM2:
  o = 0.5 · sin(6.2831 · r) with r a random number in [0, 1), drawn once when the game creates its
  pool of ambient cars, not at each spawn);
- **f**, its front reach: how far its front bumper is ahead of its reference point. The car stops
  following a lane *f* before the lane's end, so its bumper ends at the lane's end.

Its path is a chain of **pieces**. Each piece is a cubic Hermite curve in the ground plane from
P0 to P1 with end tangents T0 and T1 (already scaled), and a length *L* used to turn a distance
into the curve parameter (u = *d*·(1/*L*) for a moving car, "Evaluating a piece"). Pieces carry no height;
cars get their height from the ground (traffic spec).

### Pieces along a lane (a moving car)

For road *r*, side *s*, lane *l* with *n* ≥ 3 points, pieces *k* = 1 … *n* − 1 join point *k* − 1
to point *k*. This is what a driving ambient car builds as it moves from piece to piece:

| Piece | L | P0 | P1 | T0 | T1 |
|---|---|---|---|---|---|
| first (*k* = 1) | seglen(1) | P[0] + o·X(0) | P[1] + o·X(1) | L·S | L·Tan(1) |
| middle (2 ≤ *k* ≤ *n* − 2) | seglen(*k*) | P[*k*−1] + o·X(*k*−1) | P[*k*] + o·X(*k*) | L·Tan(*k*−1) | L·Tan(*k*) |
| last (*k* = *n* − 1) | seglen(*n*−1) − f | P[*n*−2] + o·X(*n*−2) | Q(f) + o·X(*n*−2) | L·Tan(*n*−2) | L·E |

`v + o·X` means each component plus o times the axis component; `L·V` scales a vector.

**Q(t)**, the point *t* before the lane's end, is measured on the last segment with the length of
**lane 0's** last segment of the same side (a quirk; exact for lane 0):
L₀ = D₀[*n* − 1] − D₀[*n* − 2] (lane 0's distances), *w* = (L₀ − *t*) / L₀,
Q(*t*) = (P[*n*−1] − P[*n*−2])·*w* + P[*n*−2] per component. (Checked in the machine code: the
moving car's last piece and its turn piece both use this point, and the last piece offsets it with
the X axis of point *n* − 2.)

If the last piece's *L* ≤ 0 (*f* is longer than the last segment) the last piece is skipped and the
turn piece starts from Q(seglen(*n* − 1)) instead of Q(f).

**Pieces set up other ways.** The table is what a car builds as it **advances** from one piece to
the next (by far the common case). MM2 builds a piece differently when it is set up out of
sequence — when a car is **started** on its rail, when it **rejoins** its new lane after a lane
change (traffic spec), and when it **arrives** on a road of only 2 sections:

- a first piece (*k* = 1) set up by **rejoining** is built like a middle piece: T0 = L·Tan(0)
  (starting and arriving use L·S as in the table);
- a last piece set up by starting, rejoining or arriving ends at the car's **own** lane's stop
  point with the X axis of the exit section: *L* = seglen(*n*−1) − f,
  P1 = (P[*n*−1] − P[*n*−2])·*w* + P[*n*−2] + o·X(*n*−1) with *w* = 1 − (f / seglen(*n*−1)) of its
  own lane, T0 = L·Tan(*n*−2), T1 = L·E (on a 2-section road this single piece has T0 = L·S when
  set up by arriving, L·Tan(0) when set up by starting or rejoining).

So the same lane's last piece ends at Q(f) + o·X(*n*−2) when reached by advancing but at the own-lane
point + o·X(*n*−1) when set up by starting or rejoining. (Machine-code check: the advance builds its
last piece from the lane-0 point and the X axis of point *n* − 2; the own-lane point appears only in
the start, rejoin and 2-section arrival paths.) Roads with *n* = 2 do not occur in the shipped
cities.

### The turn piece through an intersection

From lane *l* of road *r* (side *s*) to lane *l*′ of the next road *r*′ (its outgoing side *s*′),
with *t* = f (or the skipped-last-piece value above):

- P0 = Q(*t*) + o·X(*n* − 1) (the X axis at the exit section of *r*);
- P1 = P′[0] + o·X′(0) (the first point of the next lane, offset with *r*′'s axis);
- *L* = |P0.x − P1.x| + |P0.z − P1.z| (flat "city-block" length);
- T0 = *L*·E (end direction of *r* for side *s*), T1 = *L*·S′ (start direction of *r*′ for side *s*′).

After the turn piece the car continues with the first piece of the next lane. MM2 does not check
here that the chosen next lane exists (the selection rules below always give an existing lane on
the shipped cities); only the re-attach variant below clamps it to the side's last lane.

### Evaluating a piece (a moving car)

When a moving car starts a piece, MM2 stores four coefficients per axis (x and z; y is 0):

```
a = ((P1·(−2) + T0) + T1) + P0·2
b = ((T1·(−1) + T0·(−2)) + P1·3) + P0·(−3)
c = T0
d = P0
```

with each product by a constant a float multiplication (rounded) and each sum rounded. It also
stores R = 1 / *L* (one division, rounded). A car *e* metres into the piece is at u = *e*·R (a
multiplication, no clamping) and

```
position   = ((u·a + b)·u + c)·u + d
derivative = ((u·a)·3 + (b + b))·u + c        (the car's heading)
```

This is the standard cubic Hermite curve: position P0 at u = 0, P1 at u = 1, derivatives T0 and T1
there.

The placement and re-attach rules below evaluate their pieces directly instead: u = (distance into
the piece) / *L* (a division; the placement rule clamps u below at 0, not above; the re-attach rule
does not clamp), and
b = ((P1·3 + T0·(−2)) + T1·(−1)) + P0·(−3) (the same terms summed in a different order; a and c as
above), position as above with P0 for d.

### Distance along a lane

Distances along a lane are measured with its line distances D. The piece containing distance *s*
(0 ≤ *s* ≤ D[*n* − 1], clamped) is the first *k* in 1 … *n* − 1 with *s* ≤ D[*k*] + 0.00001 (*n* − 1
if none); the distance into it is *s* − D[*k* − 1].

### Point at a route distance (placement variant)

When MM2 needs the point *s* metres along a car's planned route without moving the car (placing a
new ambient car, testing whether the player is ahead of a car: traffic spec), it uses this rule,
which differs slightly from the pieces above:

1. If *s* < D[*n* − 1] − f: find piece *k* for *s* as above; use the first/middle/last piece of the
   table (the last piece with *L* = seglen(*n* − 1) − f) and u = (*s* − D[*k* − 1]) / *L*.
2. Otherwise, with *s*′ = *s* − (D[*n* − 1] − f) and the turn piece built with Q(f): if *s*′ is less
   than the turn's *L*, evaluate the turn piece at *s*′ / *L* — but with **T1 = L·S′ computed with
   the current side *s* instead of *s*′** (MM2 bug; it only matters when *s* ≠ *s*′).
3. Otherwise continue on the next lane with *s*″ = *s*′ − *L*: its piece *k*′ and
   u = (*s*″ − D′[*k*′ − 1]) / seglen′(*k*′), using the first piece (T0 = seglen′(1)·S′ with the
   correct side) or a middle piece of the table; the next lane's last segment is also treated as a
   middle piece (untrimmed, T1 = seglen′·Tan′(*n*′ − 1)).

A second variant is used once when a knocked-about ambient car rejoins the network (traffic
spec); it also returns the derivative (both with the placement coefficients, no clamping of u),
and differs from this one in three ways (all MM2 bugs except the first): step 2 uses the correct
side *s*′ for T1; its **first piece** keeps the table's first piece (*L* = seglen(1), T0 = L·S,
T1 = L·Tan(1), u = *s* / *L*) but with P0 = P[1] + 2o·X(1) (o·X(1) added twice) and P1 = P[1]
(no offset); and in step 3, when *s*″ falls in the next lane's last segment, it returns
the point P′[*n*′−3] + o·X′(*n*′−3) with derivative seglen′(*n*′−1)·Tan′(*n*′−3) (a piece evaluated
at u = 0). It also clamps the next lane to the next side's last lane.

## Choosing the next road

When a car on lane *l* of road *r*, side *s* needs its next road (when it is placed on a road,
and again when it enters a road — traffic spec), MM2 picks (next road, its outgoing side, next lane)
by the rules below. *I* = the exit intersection, *m* = its road count, *i* = the slot of *r* in
*I*'s road list (end 0's slot for side B, end 1's for side A).

**Order around the intersection.** *I*'s list is sorted by atan2(dx, dz) ascending (`bai.md`).
Seen from above with north up, that is counter-clockwise. For a car arriving along road *r*,
slot *i* + 1 is therefore the **first road to its right**, *i* + 2 the next, and so on;
*i* − 1 (wrapping) is the first to its left. "Scanning right" visits slots *i* + 1, *i* + 2, …,
*i* + *m* − 1 (mod *m*), "scanning left" *i* − 1, *i* − 2, … A candidate *q* is **open** when its
outgoing side at *I* is open.

Random choices: r = (RNG 15-bit value) / 32768, index = trunc(r × count) (the RNG is the game's
global one, `controls.md`; the AI map reseeds it to 1 at every reset). A rule draws exactly one
number when it reaches its random pick and none otherwise (rules 5 and 6 draw even with a single
candidate; rule 4 only when it collected at least one road).

Rules, first match wins (*L* = lane count of side *s* of *r*):

1. **Straight-across rule**: if the approach's turn rule is 3 (`traffic-control.md`): next road =
   slot (*i* + 2) mod *m*, its outgoing side, lane = min(*l*, its lane count − 1). (No open check.)
2. **First right**: if the approach's turn rule is 1 (never set in MM2): scan right for the first
   open road; lane = its last lane. If none, nothing changes.
3. **Freeway** (*r* has flag 0x04):
   - if *l* = *L* − 1: draw r; if trunc(2r) = 1 apply "freeway onward" (next item, including its
     fallback), else rule 2; then **always** apply the lane rule — rule 5 if *l* = 0, else rule 6 —
     which overwrites that result (so the first draw only consumes random numbers);
   - otherwise "freeway onward": scan right for the first open road that also has flag 0x04;
     next lane = *l* (same index). If there is none: rule 5 if *l* = 0, rule 6 if *l* = *L* − 1,
     else rule 7.
4. **Single lane** (*L* = 1): scan right and collect every open road (with its outgoing side).
   If one of them has flag 0x04, take the first such road at once (lane = its last lane).
   Otherwise, if any were collected, pick one at random (index = trunc(r × count)) and set its lane
   by the **turn class**: lane 0 if the turn is left or straight, the last lane if it is a right
   turn. Turn class: d = the next road's second centre point minus its first in its outgoing
   direction (C[1] − C[0] for outgoing side +1, C[*n*−2] − C[*n*−1] for −1); with *r*'s exit frame
   (side B: right = −X[*n*−1], ahead = −Z[*n*−1]; side A: right = X[0], ahead = Z[0]),
   angle = atan2(d·right, d·ahead) (3D dot products); right turn if angle > 0.5, left if < −0.5,
   straight otherwise. If none were collected the car gets no next road.
5. **Lane 0** (*L* ≥ 2, *l* = 0): scan **left**. The first open road found is candidate 1. Continue
   scanning left from there to the next open road; it is candidate 2 if it does not lie to the
   right, i.e. if `lat > −2` where, with *l*″ = *l* (or that road's lane count − 1 if its count is
   **less than** *l*; a count equal to *l* is not corrected), v = its lane *l*″ point 0 minus *r*'s lane
   *l* point *n* − 1 (x and z), and lat = v.x·X[*n*−1].x + v.z·X[*n*−1].z on side B,
   (−X[0].x)·v.x + (−X[0].z)·v.z on side A. Pick one candidate at random (trunc(r × 1 or 2)); next
   lane = 0. If there is no candidate, the car gets no next road (MM2 prints "No eligible roads").
6. **Last lane** (*l* = *L* − 1, *L* ≥ 2): the same scanning **right**, candidate 2 accepted if
   `lat < 2`; next lane = the chosen road's last lane.
7. **Middle lane** (0 < *l* < *L* − 1): let *k* = the number of open outgoing sides at *I* other than
   *r* (counting a road ending at *I* only if its side A has lanes).
   - If *k* < 3: scan right and collect the open roads whose outgoing side has more than 2 lanes.
     One such road: take it, next lane = *l*. None: apply rule 2 (first right). Several: take the
     one whose last lane's **last** point is closest sideways to *r*'s lane end:
     minimise |(p.x − e.x)·A.x + A.z·(p.z − e.z)| with e = *r*'s lane *l* point *n* − 1, p = that
     road's last lane point *n* − 1, and A = X[*n*−1] (side B) or −X[0] (side A); next lane = *l*.
   - Otherwise scan right and take the **second** open road; next lane = min(*l*, its lane
     count − 1).

So on two-way streets cars in the inner lane (lane 0 before mirroring: next to the centre line)
turn left or go straight, cars in the outer lane turn right or go straight, and middle lanes go
straight; in London, after mirroring, lane 0 is the kerb lane and the same rules give left turns
from the kerb lane. Shortcuts (supplementary roads) are never open, so ambient cars never use them.

## Room lists (world position → network)

Every PSDL room *ρ* has a list of **components**, (index, kind) pairs with kind 1 = road,
2 = shortcut, 3 = intersection, in the order they were added during loading (`bai.md`):

1. each main road, in road order: for each interior section *i* = 1 … *n* − 2, the room
   Find(C[*i*], 0) (`city-collision.md`, "Finding the room of a point") gets (road, 1) unless it
   already has it;
2. each intersection, in order: its file room gets (intersection, 3) unless already there;
3. each shortcut, in order: (shortcut, 2) for the rooms of its interior centre points as in 1;
   then, for each segment *k* = 0 … *n* − 2 of **side B's** kerb line E (all of them), and
   afterwards for each segment *k* = 0 … *n* − 2 of **side A's** kerb line, with
   *c* = trunc(CD[*k*+1] − CD[*k*]) and *j* = 0 … *c* − 1: the room Find(*x*, 0) of the point
   *x* = (E[*k*+1] − E[*k*])·(float(*j*) / float(*c*)) + E[*k*] per component (E = the first *n* points
   of that side's edge lines, `bai.md`) gets (shortcut, 2), unless that room is the room of either
   end intersection or already has this shortcut.

Look-ups (Find(*p*, hint) is the room search of `city-collision.md`; "first" means first in list
order):

- **Component of a room**: the first entry of kind 3 or 1; if none, (room number, kind 0).
- **Component at a position** *p* (hint *h*): room ρ = Find(*p*, *h*). The first entry of kind 1 or 3;
  else the first shortcut whose "on road" test (below) with margin 0 gives 1 or 2; else (ρ, 0).
- **Component at a position, preferring a road** *q* (used when a car re-attaches; traffic
  spec): ρ = Find(*p*, *h*); if *q* ≥ 0 and ρ's list has (*q*, 1), that; else if it has (*q*, 2), that.
  Then the first intersection; then the first road or shortcut whose on-road test gives 1 or 2;
  else (ρ, 0). A second form accepts only kind 1 for *q* and only roads (kind 1) in the last step.
- **All components at a position** (police routing): ρ = Find(*p*, *h*). If *q* ≥ 0 and ρ has
  (*q*, 1 or 2): that one. Empty list: (ρ, 0). Exactly one entry that is not a shortcut: it.
  Any intersection: the first one. Otherwise every road/shortcut entry (in list order, at most 5)
  for which some section *i* = 1 … *n* − 1 has (d·Z[*i*] > −0.1 and |d·X[*i*]| < 2 × half width),
  with d = *p* − C[*i*] in x and z; none: (ρ, 0).

### Is a point on the road (classification 1/2/3)

For road *r*, point *p*, margin μ: e₁ = bandB[2·*LB* − 1] − μ, e₂ = bandB[2·*LB* + 1] − μ (side B's
band edges, used for both sides). For *i* = 0 … *n* − 1:

- e = Bo[*i*] − C[*i*] (Bo = side B's outer edge line, the second *n* points of its edge lines);
  L = (e.z·e.z + e.y·e.y) + e.x·e.x; s = 1 / √L (0 if L = 0); v = (e.x·s, e.y·s, e.z·s);
- a = v.y·Y[0].z − v.z·Y[0].y, b = v.x·Y[0].y − v.y·Y[0].x (note Y[0] for every section);
- d = *p* − C[*i*]; if (d.z·b) + (d.x·a) > 0 (the point lies behind section *i*), stop at *i*.

With λ = (d.z·X[*i*].z + d.x·X[*i*].x)·(−1) (positive toward side B): **1** (on the roadway) if
−e₁ < λ < e₁, **3** (off the road) if λ ≥ e₂ or λ ≤ −e₂, else **2** (on a sidewalk); λ is also
returned. If no section stops the loop, the same with the last *i* (= *n* − 1) and its d. On a road with
*LB* = 0 MM2 reads an unrelated value for e₁ (a memory address as a float, effectively 0), so the
answer is never 1.

### Section of a point on a road

`section(p, s)`: best = 9999, result = *n*, prev = 0. For *i* = 0 … *n* − 1: dx = *p*.x − C[*i*].x,
dz = *p*.z − C[*i*].z, a = dz·Z[*i*].z + dx·Z[*i*].x. If a > −0.1 and ((CD[*i*] − CD[0]) − prev + 2 > a
or *i* = 0): λ = |dz·X[*i*].z + dx·X[*i*].x|; if λ < best then best = λ, result = *i*, and if
λ < half width + 4 stop. After each *i* > 0 that did not stop, prev = CD[*i* − 1] − CD[0] (so the
window covers the two segments before section *i*, plus 2 m). After the loop, if best > half width
+ 4: with d from C[*n* − 1], if d·Z[*n*−1] < 0 (flat, z term first) and the flat lateral |d·X[*n*−1]|
< best, result = *n*. Return result for *s* = +1, *n* − result for *s* = −1.

### Other road queries

- **Centre point at distance** *s* along the road (0 ≤ *s* ≤ CD[*n*−1] − CD[0], clamped): the first
  *k* ≥ 1 with *s* ≤ CD[*k*] − CD[0]; point = C[*k*−1] + (C[*k*] − C[*k*−1])·*w*, *w* =
  (*s* − (CD[*k*−1] − CD[0])) / (CD[*k*] − CD[*k*−1]), linear (no curve).
- **Distance of a point along the centre line**: for *k* = 1 … *n* − 1 with d = *p* − C[*k*] (3D),
  a = (d.z·Z[*k*].z + d.y·Z[*k*].y) + d.x·Z[*k*].x; at the first *k* with a ≥ 0 the answer is
  (CD[*k*] − CD[0]) − a. Past the last section MM2 reads one entry beyond the distance array
  (undefined); use CD[*n*−1] − CD[0].
- **Section index of a car on a road** (physics cars): for *k* = 1 … *n* − 1 with d = *p* − C[*k*]
  (3D): if d·Z[*k*] > 0 then the answer is *k* if |d·X[*k*]| < half width, else *n* − 1; *n* − 1 if no
  *k* qualifies.
- **Facing**: a car with forward axis −Zc is going in the road direction when Zc.x·Z[*j*].x +
  Zc.z·Z[*j*].z ≥ 0, where *j* = the first section ≥ 1 that the car's position is more than 4 m behind
  (flat d·Z[*j*] > 4), or `section(p, +1)` if none.

## Obstacles

At load (`bai.md`) static props are registered where the traffic AI looks for things in its lane
(traffic spec):

- **On roads**: for each main road, each segment *k* = 1 … *n* − 1 and each room of the road's room
  list, every breakable prop (`.inst` bit 25, xref and street props) in that room whose prop data
  does not have bit 0x20 in `CollisionType` (`tune/banger/*.dgBangerData`; 48 in shipped data) and
  whose flat position satisfies 0 < d·Z[*k*] < CD[*k*] − CD[*k*−1] (d = prop position − C[*k*]) is
  registered at section *k* on side −1 if (−X[*k*].z)·d.z + (−X[*k*].x)·d.x < 0 (the prop is on side
  A's half), else side +1. A room listed twice in the road's room list registers its props twice.
- **In intersections**: every breakable prop in the intersection's room whose `ImpulseLimit2` is
  greater than 7.5e7 (the unbreakable ones) is registered with the intersection.

"Breakable prop" here means any prop object, breakable or not: the moving level objects that
collide through prop data (drawbridge leaves, Underground train cars, ferries) are taken too
(`../game/level-objects.md`, "AI obstacles").

## Police routing graph

The police route finder (police spec) works on the intersections as nodes. Every road in an
intersection's list — main roads and shortcuts — is an edge to the intersection at its other end,
in both directions (one-way streets, lane counts and side flags are ignored), with cost = the
road's full centre length CD[*n*−1] − CD[0]. Start and goal positions are placed on the network
with "All components at a position" above; how partial roads are costed is the police spec's.

## What the other AI specs use (interface)

| Query | Defined in |
|---|---|
| Lane pieces from (road, side, lane, o, f); turn piece to (next road, side, lane); evaluate position and derivative at u | "Lane paths" |
| Piece and offset for a distance along a lane; lane length D[*n*−1] | "Distance along a lane" |
| Point at a route distance (placement and player-ahead tests) | "Point at a route distance" |
| Next (road, side, lane) at the end of a lane | "Choosing the next road" |
| Entry / exit intersection of a side; outgoing side of a road at an intersection; roads of an intersection in angle order | "Terms", `bai.md` |
| Lane count, open/closed side, pedestrian flag, speed limit of a road | `bai.md` |
| Component (road / shortcut / intersection) at a position or in a room | "Room lists" |
| On-road classification, section of a point, distance along the centre line, facing | "Is a point on the road", "Section of a point", "Other road queries" |
| Roads to populate when a player changes room | `bai.md`, "Activation tables" |
| May a car enter the intersection now; light state of an approach; stop-sign queue | `traffic-control.md` |
| Graph nodes, edges and costs for police routes | "Police routing graph" |

## Checked against

`tools/ai/bai.py` (dirty side) implements the pieces, the evaluation and the next-road rules:

- Turn paths exported for London intersections 83 and 59 and SF intersections 2 and 126
  (`work/check/paths_<c>_<i>.csv`, one row per sample: road, side, lane, next road, side, lane,
  rule, x, z; drawn in `paths_<c>_<i>.png`). They join each lane end (f = 2.2 m, o = 0) smoothly to
  the start of the chosen outgoing lane. E.g. SF intersection 2 (roads in list order 115, 2, 127,
  1): road 115 side −1 lane 0 → road 127 side −1 lane 0 (rule 5); lane 1 → road 2 side +1 lane 0
  or road 127 side −1 lane 1 (rule 6); road 127 side +1 lane 0 → road 115 side +1 lane 0 or road 2
  side +1 lane 0 (rule 5), lane 1 → road 115 side +1 lane 1 (rule 6); one-lane road 1 side +1 →
  road 115 side +1 lane 1, road 127 side −1 lane 0 or road 2 side +1 lane 0 (rule 4). London's paths
  keep to the left after mirroring.
- Every open lane end in both cities gets at least one next road: London 826 lane ends with one
  choice, 281 with two, 34 with three (single-lane roads); SF 598, 524, 89, and one with four.
- Intersection 2 (SF) list order by angle: roads ascending atan2 around the room centre; road
  slots match after the shortcut merge (0 mismatches on non-loop roads).
- `tools/ai/bai.py survey` prints the counts; `render` and `paths` write the pictures.
