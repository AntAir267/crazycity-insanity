# Pedestrian walking

Part of the pedestrian spec (`pedestrians.md` has the terms, the pool and the frame order). This
page covers a pedestrian's state, its per-frame update, the path it follows along a sidewalk, how
it steers, steps around obstacles, chooses where to go at a road end, and crosses streets at
traffic lights. Reactions to the player's car are `pedestrian-reactions.md`.

## State

| Field | Meaning |
|---|---|
| road *r*, side *s*, direction *d* | where it walks: *d* = +1 toward higher section indices, −1 toward lower |
| previous road *r*′, side *s*′, direction *d*′ | where it came from (equal to the current ones after placing) |
| segment *k* | 1 … *n* − 1: between sidewalk points *k* − 1 and *k*; 0 or *n*: on the **crossing piece** between *r*′ and *r* |
| distance *x* | along *r*'s sidewalk line, from point 0 |
| offset *o* | lateral offset of its path, −1.5 … 1.5 (positive: toward side B of the road, i.e. along −X) |
| heading *h*, matrix | rows X = (−cos *h*, 0, sin *h*), Y = (0, 1, 0), Z = (−sin *h*, 0, −cos *h*), position *p* |
| mode | 0 walking, 1 wary, 2 dodging (`pedestrian-reactions.md`) |
| step | while walking: 0 walk, 1 to the kerb, 2 wait, 3 cross |
| seen mode, seen step | the mode and step whose start has run (−1 after placing) |
| wall flag *w* | a wall was found behind the sidewalk (`pedestrian-reactions.md`) |
| choice *c* | the last road-end choice: 0 corner, 1 cross the corner road, 2 cross this road |
| target *t* | the point it steers to |
| nearest player, *q* | the player whose car is nearest, and that squared distance |
| dive start, reverse flag | used while dodging |
| speed | forward speed of its current sequence |
| animation state | `../formats/ped-anim.md`, "Playback" |

**Matrix**: built from *h* at the end of every update and when placed (the formula above). Its
forward is −Z = (sin *h*, 0, cos *h*), its left −X.

## One update

For a pedestrian with a road (always, after placing):

1. Count the update; latFactor = 1.0.
2. **Nearest player**: *q* = 1.0e9, nearest = none; for each player in list order:
   e = dx × dx + dz × dz with dx = *p*.x − P_c.x, dz = *p*.z − P_c.z; if e < *q*: *q* = e,
   nearest = that player (ties keep the earlier player).
3. **Reaction choice** (`pedestrian-reactions.md`, "Choosing the mode") — may change the mode.
4. **Behaviour** by mode: 0 → by step: 0 "Walking", 1 "To the kerb", 2 "Waiting", 3
   "Crossing"; 1 → "Wary"; 2 → "Dodging" (may set latFactor).
5. Matrix from *h*.
6. **Move**: speed = current sequence's forward speed *vf*; f = *vf* × *F*; l = (latFactor × *vs*)
   × *F* (*vs* the sideways speed); for each component c: *p*.c = *p*.c − ((f × Z.c) + (l × X.c)).
   So it moves *f* forward and *l* to its left.
7. **Height**: if road *r* has flag 0x08 ("level") and *q* > 2500: *p*.y = side B's kerb line
   point 0's y. Otherwise probe from (x, y + 2, z) to (x, y − 2, z) (mask "ground", nothing ignored,
   the pedestrian's own probe state, room hint = its current room); on a hit with
   |hit.y − *p*.y| < 0.5: *p*.y = hit.y; otherwise *p*.y is kept.
8. **Room**: room = Find(*p*, current room) (`../physics/city-collision.md`); move it to that room if
   different.
9. Target *t*.y = *p*.y.
10. **Animation**: advance the shared clock and this pedestrian's animation (`ped-anim.md`,
    "Playback").

## Sidewalk geometry

For the current road and side (*n* sections, sidewalk line W, Dw):

- **Sidewalk length** S(*a*, *b*) = Dw[*b*] − Dw[*a*] (*a*, *b* clamped below at 0).
- **Segment of a distance** *x*: clamp *x* to [0, Dw[*n* − 1]]; the first *k* in 1 … *n* − 1 with
  *x* ≤ Dw[*k*] + 0.00001, else *n* − 1.
- **Sidewalk heading** H(*x*, λ, *d*) (used at placement and in two reactions): take side B's
  arrays if *d* = +1, side A's otherwise — **whatever the pedestrian's side** —; clamp *x* to
  [0, D_λ[*n* − 1]] with D_λ the line distances of that side's line λ; find the first *k* in
  1 … *n* − 1 with *x* ≤ D_λ[*k*] (no tolerance); none: H = 0. With Q that side's last line
  (line *L* + *W* − 1, its sidewalk): *d* = +1: H = atan2(Q[*k*].x − Q[*k* − 1].x,
  Q[*k*].z − Q[*k* − 1].z); *d* = −1: H = atan2(Q[*k* − 1].x − Q[*k*].x, Q[*k* − 1].z − Q[*k*].z).
  It is the direction of travel along that sidewalk for direction *d*. (Placement passes λ = the
  pedestrian's side's lane count, so with equal lane counts it reads the sidewalk distances; the
  reactions pass λ = 0, side A's lane 0 is stored in travel order, so the segment can be off.)
  λ can be past that side's lines (λ ≥ *L* + *W*): a pedestrian placed with *d* = −1 on side B
  of a one-way road, whose side A has only its sidewalk (91 such sides in SF, 173 in London).
  MM2 then reads past the end of that side's distance list into unrelated memory, so the segment
  it finds is arbitrary (H is some segment's direction of that side's sidewalk, or 0). *Our
  choice*: a λ past the side's lines reads its last line (the sidewalk).

## The curve

The pedestrian's path through its segment is a cubic Hermite piece in x and z, built by
**curve**(*k*, offset) (also called "build the segment's curve"):

1. Clamp the offset to [−1.5, 1.5] and store it as *o*; let ō = −*o*.
2. Cases (ℓ = S(*k* − 1, *k*); X, T, Z the road's section vectors, not side-dependent; a sum
   `ō·X + W` is per component (ō × X.c) + W.c):
   - *k* = *n* − 1 (also when *n* = 2): P0 = ō·X[*k*−1] + W[*k*−1], P1 = ō·X[*n*−1] + W[*n*−1],
     T0 = ℓ·T[*k*−1], T1 = ℓ·(−Z[*n*−1]);
   - else *k* = 1: P0 = ō·X[0] + W[0], P1 = ō·X[1] + W[1], T0 = ℓ·(−Z[0]), T1 = ℓ·T[1];
   - else 1 < *k* < *n* − 1: P0 = ō·X[*k*−1] + W[*k*−1], P1 = ō·X[*k*] + W[*k*], T0 = ℓ·T[*k*−1],
     T1 = ℓ·T[*k*];
   - else (*k* = 0 or *n*: the **crossing piece**): with W′ the previous road's sidewalk line on the
     previous side and *n*′ its sections, A = W′[*n*′ − 1] if *d*′ = +1 else W′[0]; if *d* = +1:
     P0 = A, P1 = W[0]; else P0 = W[*n* − 1], P1 = A. T0 = T1 = P1 − P0 (all three components);
     ℓ = √((Δz·Δz + Δy·Δy) + Δx·Δx) with Δ = P1 − P0, but ℓ = 1 if the previous road is the current
     road.
3. Inverse length R = 1 / ℓ. Between two different roads ℓ can be 0 (their sidewalk ends
   coincide): MM2 gets R = +∞, and any target point taken on that piece is NaN, so the pedestrian
   is lost ("Stepping around"); keep this (plain IEEE arithmetic gives the same). With *d* = +1
   locate moves it to segment 1 before a target is taken, so only a *d* = −1 entry that locate
   leaves at *k* = *n* is lost. No road-end choice in the shipped cities makes such a piece
   (1 SF and 67 London pairs of sidewalk ends of different roads coincide, none of them a corner
   or crossing pair).
4. Coefficients a, b, c, d for x and for z exactly as the moving car's in `road-network.md`,
   "Evaluating a piece".

The curve at u gives x = ((u·a + b)·u + c)·u + d and z likewise; y is never taken from it.
The curve always runs in the road's direction (from point *k* − 1 to *k*), whatever *d*; on the
crossing piece it runs from the previous road's end to the current road's start for *d* = +1 and
the other way round for *d* = −1. *o* > 0 shifts the path along −X, i.e. toward side B.

**Target point** for a distance *e* (*e* = *x* + *d* × 6 when walking, computed as
(float(*d*) × 6.0) + *x*): u = *e* × R if *k* = 0; (*e* − Dw[*n* − 1]) × R if *k* = *n*;
(*e* − Dw[*k* − 1]) × R otherwise. *t*.x, *t*.z = the curve at u (u is not clamped, so beyond the
segment the cubic is extrapolated); *t*.y = *p*.y.

## Re-projecting onto the sidewalk

**Locate** (run at the start of each walking update, and after road changes) works out *k* and *x*
from the position, moving *k* across one section boundary at most, and returns *x*. All dot
products here are flat (x and z). Let W be the current sidewalk line.

**Direction +1:**

1. P = W[*k*]; dx = *p*.x − P.x, dz = *p*.z − P.z.
2. If *k* = 0: A = W′[*n*′ − 1] if *d*′ = +1 else W′[0] (previous road and side); v = A − P (3D);
   m = (v.y·v.y + v.z·v.z) + v.x·v.x; i = 0 if m = 0 else 1 / √m; v = v × i per component;
   a = dz·v.z + dx·v.x. Else a = dz·Z[*k*].z + dx·Z[*k*].x.
3. If a ≤ 0 (it has reached point *k*): *k* = *k* + 1.
   - If *k* < *n*: offset = ((W[*k*−1] − *p*) · X[*k*−1]) as ((e.z·X.z + e.y·X.y) + e.x·X.x) with
     e = W[*k* − 1] − *p* (3D); curve(*k*, offset) (which clamps it); return Dw[*k* − 1] − a.
   - Else: *k* = *n* − 1; offset from W[*n* − 1] and X[*n* − 1] the same way; curve(*k*,
     offset); return Dw[*n* − 1] − a.
4. Else (a > 0): if *k* ≠ 0 return Dw[*k*] − a; else return (A − W[0])·v − a, i.e.
   ((A.z − W[0].z)·v.z + (A.x − W[0].x)·v.x) − a with A and v of step 2.

**Direction −1:**

1. If *k* = *n*: P = W[*n* − 1]; A = W′[0] if *d*′ = +1 else W′[*n*′ − 1]; v = A − P;
   m = (v.z·v.z + v.x·v.x) + v.y·v.y; i as above; v.x, v.z scaled; a = (*p* − P)·v as
   dz·v.z + dx·v.x. If a < 0: *k* = *k* − 1 and curve(*k*, *o*) **with the current offset**.
   Return Dw[*k* − 1] + a (with the new *k*: on the frame it leaves the crossing piece this is one
   segment short; MM2 quirk, kept).
2. Else: P = W[*k* − 1]; d = *p* − P; a = d.z·Z[*k*].z + d.x·Z[*k*].x.
   - If a < 0 (still inside segment *k*): return Dw[*k* − 1] − a.
   - Else *k* = *k* − 1; if *k* ≤ 0: *k* = 0 and return −a. Otherwise offset from W[*k*] and X[*k*]
     as in direction +1 step 3; curve(*k*, offset); return Dw[*k*] − a.

   *k* = 0 with *d* = −1 happens only after "return −a" with a exactly 0 (x = 0 does not trigger
   the road end). The next locate then reads W[−1] with Z[0]: the side's lines are stored one after
   another, so W[−1] is the last point (section *n* − 1) of line *L* − 1, the side's outermost lane;
   a is then normally large and positive (that point lies at the far end of the road), the return
   is −a and the road end follows in the same update.
   Dw[−1] counts as 0 (lengths clamp at 0). A side without lanes has nothing before W[0] (MM2 reads
   outside its array); *our choice* there: use P = W[0] and Z[1], as for *k* = 1.

So walking along a segment keeps the path offset, and passing into the next segment takes the
pedestrian's current sideways position (clamped to ±1.5) as the new offset.

Note the end point A of the previous road: the curve and direction +1 use the end the pedestrian
left (W′[*n*′ − 1] after walking *d*′ = +1), but direction −1's locate uses the **other** end of
the previous road (W′[0] after *d*′ = +1): its v points from the current road's end toward the far
end of the previous road. MM2 quirk, kept.

## Turning toward a point

**Turn toward** *t* (walking, to the kerb, waiting, crossing): with Δx = *t*.x − *p*.x,
Δz = *t*.z − *p*.z and the matrix rows X, Z: α = −atan2(Δx·X.x + Δz·X.z, −(Δx·Z.x + Δz·Z.z))
(the angle of *t* to its left). If α > 0.15: *h* = 0.15 + *h*; else if α < −0.15: *h* = *h* − 0.15;
else *h* = α + *h*. (0.15 is float32 0.15000001: at most 8.6° per frame.)

## Walking (mode 0, step 0)

1. **Start** (when mode or step differs from the seen ones): queue a sequence after the current
   one: current `ANTIC` → `ANTIC_WALK`; `WALK_ANTIC` → `ANTIC_WALK`; `RUN` → `RUN_WALK`; `BACKUP`
   → play the avoidance voice and queue `BACKUP_WALK`. Then *w* = 0 and seen mode and step = the
   current ones.
2. *x* = locate().
3. pf = 9999, of = 9999, obstacle = none. If *q* < 36: **player ahead** test (below) may set pf.
   **Obstacle ahead** test (below) may set of and the obstacle.
4. If of < 9999 and of < pf: **step around** the obstacle prop (below); curve(*k*, *o*); stop.
5. If pf < of and pf < 9999: **step around** the player's car (below); curve(*k*, *o*); stop.
6. Otherwise: if current is `STAND`, queue `STAND_WALK`. Target point for *x* + *d* × 6; turn
   toward it; then the **road end** test with *x* (below).

(MM2 also has a branch for stepping around another pedestrian, taken only when both pf and of
exceed 9999, which cannot happen.)

**Player ahead** (nearest player's car body inside a box in front): dx = P_c.x − *p*.x,
dz = P_c.z − *p*.z; fwd = (−Z.z)·dz + (−Z.x)·dx; lat = dz·X.z + dx·X.x (pedestrian rows);
r = R_p. Inside if −r < fwd < 6.0 and −r < lat < r (strict); then pf = fwd.

**Obstacle ahead** looks at the props registered on the road network (`road-network.md`,
"Obstacles"): a road's props per segment and side, and an intersection's unbreakable props.
Lists, in order: (1) if *k* is 0 or *n*: the props of the intersection ahead (the road's end 0
intersection if *d* = +1, end 1's otherwise — on a crossing piece that is the far end of the road
the pedestrian is entering, not the intersection it is crossing: MM2 quirk, kept); else the props
of road *r*, segment *k*, side *s*;
(2) *k*′ = *k* + *d*: if *k*′ is 0 or *n*: the intersection ahead; if 1 ≤ *k*′ < *n*: road *r*,
segment *k*′, side *s*; otherwise nothing. The first prop in list order that **blocks** gives
of = its distance and the obstacle; the scan stops there. A prop blocks the way from *p* to *t*:
u = (*t* − *p*) normalised (3D; zero if zero length); L = √((*p*.z − *t*.z)² + (*p*.x − *t*.x)²);
c = the prop's base point (its position minus R·CG, `../physics/collision-objects.md`);
lat = (c.x − *p*.x)·(−u.z) + u.x·(c.z − *p*.z); fwd = (c.x − *p*.x)·u.x + (c.z − *p*.z)·u.z;
R = 0.25 + min(`YRadius`, 2.0) + 1.0; ang = atan2(lat, fwd). It blocks (distance fwd) if
−R < lat < R, 0 < fwd < L, and −0.7 < ang < 0.7. MM2 keeps a knocked prop on its list and tests
it where it now is; *our choice*, while knocked props vanish in this engine
(`../engine/damage.md`): skip them (as the police planner does).

### Stepping around

**Step around** a point Q with clearance *r*: for the prop, Q = its position (the prop's matrix
position) and *r* = its `YRadius` + 1; for the player's car, Q = P_c and *r* = R_p + 1. Then (if
current is `STAND`, queue `STAND_WALK`, after the steps below):

1. *j* = *k*, but *n* − 1 if *k* = *n*. Across-axis A = −X[*j*] if *d* = +1, else X[*j*] (A points
   to the pedestrian's right).
2. pl = (*p*.x − W[*j*].x)·A.x + (*p*.z − W[*j*].z)·A.z; ql = (Q.x − W[*j*].x)·A.x +
   (Q.z − W[*j*].z)·A.z (sideways positions of the pedestrian and the obstacle across the
   sidewalk).
3. If ql + *r* ≤ 1.5 or −1.5 ≤ ql − *r* (there is room on at least one side within ±1.5): pass on
   the pedestrian's own side of the obstacle if there is room there, else on the other side: if
   pl ≤ ql: *r* = −*r* when −1.5 ≤ ql − *r* (room on the low side), else keep *r*; if pl > ql: keep
   *r* when ql + *r* ≤ 1.5, else *r* = −*r*.
4. Otherwise (the obstacle fills the sidewalk):
   - if 0 < *k* < *n*: turn round: *d* = −*d*, *h* = H(*x*, 0, *d*) (the new *d*); *r* and A stay;
   - else (on a crossing piece): go back to the previous road: swap road and previous road, side
     and previous side; *d* = −*d*′ and the previous direction becomes the old *d*; move it to the
     front of the (new) current road's list; *h* = *h* + 3.14; *k* = 0 if *d* = +1 else *n* (of the
     new road); A recomputed as in step 1 for the new road, *k* and *d*; *x* = locate();
     curve(*k*, *o*). **MM2 bug**: the clearance used next is the old previous side's integer
     value read as a float: +1 gives 1.4e−45 (effectively 0), −1 gives NaN. With NaN the target
     and then the heading become NaN; the pedestrian's position becomes NaN on its next move and it
     never recovers (it is invisible, but stays in its road's list and keeps being updated).
     *Our choice*: with +1 use 0; with −1 the pedestrian is **lost**. A pedestrian becomes lost
     whenever its heading becomes NaN (here, or on a zero-length crossing piece, "The curve"): it is
     no longer drawn, moved or updated (it takes no random numbers, as MM2's NaN pedestrian takes
     none), except that each frame, at its place in the update order, it still advances the shared
     animation clock (`../formats/ped-anim.md`, "Playback"; MM2 keeps updating it, and that
     shifts the other pedestrians' steps). It leaves its road's list only when the road is emptied
     or at the next reset; placing it again makes it normal.
5. Target *t* = Q + *r*·A (all three components: (*r* × A.c) + Q.c).
6. Turn (variant): with Δ = *t* − *p* (3D), β = atan2((Δz·X.z + Δy·X.y) + Δx·X.x,
   −((Δz·Z.z + Δy·Z.y) + Δx·Z.x)); if β > 0.15: *h* = *h* − 0.15; else if β < −0.15:
   *h* = 0.15 + *h*; else *h* = β + *h*. (β is minus the angle of *t* to the left, so the large
   turns go toward the target but a small one goes the **wrong way**: within ±8.6° the heading
   jumps to the mirror direction. MM2 quirk, kept.)

## Road end

**Road end test** with distance *x* (after walking's turn):

- *d* = +1: if *k* ≥ 1 and *x* > S(0, *n* − 1) — past the end of the sidewalk;
- *d* = −1: if *k* < *n* and *x* < 0.

If so: previous side = *s*, previous direction = *d*; road R and the new *d*, *s* =
**choose**() (below); take the pedestrian off road *r*'s list; previous road = *r*; current road =
R; put it at the front of R's list; *k* = 0 if *d* = +1, else R's *n*. In every case (also when
not past the end): *x* = locate(); curve(*k*, *o*).

### Choosing where to go

*I* = the intersection ahead (end 0's if *d* = +1, else end 1's), *d*, *s* the values before the
choice.

1. Choice *c*: if *I* has no light set, or its light set's kind is 0 (no walk phase,
   `traffic-control.md`), *c* = 0; otherwise *c* = rnd15() mod 3 (0, 1 or 2; one raw draw).
2. If any vehicle registered in *I* (`traffic-reactions.md`, "Being seen by other AI") is in an
   accident (a traffic car in any state other than driving; other vehicles never count), *c* = 0
   (after the draw).
3. The **corner road** (for *c* = 0 and 1): *i* = the current road's slot at *I* (`bai.md`, end
   record); step σ = +1 if *d* = *s*, else −1 (*d* = *s* means the street is on the pedestrian's
   left, so σ = +1 is the next road to its right); *i* = (*i* + σ) mod *m* (*m* roads at *I*); while
   the road in slot *i* is a shortcut (index ≥ the main road count) and fewer than *m* slots have
   been tried, *i* = (*i* + σ) mod *m*; corner road K = the road in slot *i* (a shortcut if all were
   shortcuts).
4. New direction and side:
   - *c* = 0, turn the corner: if *d* = *s*: (*d*, *s*) = (−1, −1) if *I* is K's end 0, else
     (+1, +1); if *d* ≠ *s*: (−1, +1) if *I* is K's end 0, else (+1, −1). Road R = K. (The pedestrian
     keeps to the same block corner.)
   - *c* = 1, cross the corner road: if *d* = *s*: (−1, +1) if *I* is K's end 0, else (+1, −1); if
     *d* ≠ *s*: (−1, −1) if *I* is K's end 0, else (+1, +1). R = K; step = 1.
   - *c* = 2, cross this road: *d* = −*d*, *s* = −*s*; R = the current road; step = 1.
5. If side *s* of R is not a pedestrian side, or R is not on the active pedestrian road list: turn
   round instead: R = the current road, *d* = −(old *d*), *s* = old *s*, step = 0,
   *h* = *h* + 3.14.

After a turn-round the pedestrian is on the "crossing piece" from its own road's end to itself,
which has zero length; the next locate moves it straight on to segment 1 (or *n* − 1).

Pedestrians therefore never leave the populated area (roads on the active list) and never walk on
shortcut roads or on sides closed to them.

## Crossing a street (steps 1–3)

Kerb points: for road Q, side σ (its kerb line E): at the end with section *n* − 1,
K = E[*n* − 1] − 2.5·Z[*n* − 1] (per component E.c − (Z.c × 2.5)); at the end with section 0,
K = 2.5·Z[0] + E[0] ((Z.c × 2.5) + E.c). Both lie 2.5 m beyond the road's end, on the line of the
kerb, i.e. where a crosswalk starts. *I*′ = the intersection ahead of the **previous** road for the
previous direction.

**Near kerb** (step 1's target) and **far kerb** (step 2's target):

| Case | Near kerb | Far kerb |
|---|---|---|
| *c* = 2, *d*′ = +1 | previous road, section *n*′ − 1 end, side *s*′ | the same end, side −*s*′ |
| *c* = 2, *d*′ = −1 | previous road, section 0 end, side *s*′ | the same end, side −*s*′ |
| *c* = 1, *d*′ = *s*′, *I*′ is the current road's end 0 | current road, section *n* − 1 end, side A | same end, side B |
| *c* = 1, *d*′ = *s*′, otherwise | current road, section 0 end, side B | same end, side A |
| *c* = 1, *d*′ ≠ *s*′, *I*′ is the current road's end 0 | current road, section *n* − 1 end, side B | same end, side A |
| *c* = 1, *d*′ ≠ *s*′, otherwise | current road, section 0 end, side A | same end, side B |

**Turning back** (from steps 1–3): *d* = −*d*′, *h* = *h* + 3.14, *s* = *s*′, step = 0; take it off
the current road's list, current road = previous road (the previous road stays as it is), put it
at the front of that road's list; *k* = 0 if *d* = +1, else *n*; queue `WALK`.

**Player standing in the way** (steps 1–3): the nearest player's speed < 1.0 and the "player ahead"
test of Walking succeeds.

**Step 1, to the kerb.** Start (mode or step changed): *t* = near kerb; queue `WALK`; seen = current.
Every frame: turn toward *t*; if the player is standing in the way: turn back and stop. If
(*t*.z − *p*.z)² + (*t*.x − *p*.x)² < 1.0: step = 2.

**Step 2, waiting.** Start: *t* = far kerb; queue `STAND2` if rnd() ≥ 0.5, else `STAND` (one
draw); seen = current. Every frame: turn toward *t*; then turn back and stop if (a) a vehicle
registered in *I*′, or in the current road's traffic list number *m* of side B or list number *m*
of side A, with *m* = 1 if *d* = +1 and *n* − 1 if *d* = −1 (the numbering of
`traffic-reactions.md`, "Being seen by other AI"), is in an accident, or (b) the player is
standing in the way. (Side A's lists are numbered against the road — list *m* holds the cars near
section *n* − *m* —, so on side B the test looks at the crossing end of the road and on side A at
the far end: MM2 quirk, kept.) Otherwise: if the flat
squared distance to *t* < 1.0, step = 1; then, if *I*′ has a light set, in its walk phase, whose
light 0 shows state 4 (`WALK`, `traffic-control.md`): step = 3.

**Step 3, crossing.** Start: queue `WALK`; seen = current. Every frame: turn toward *t* (still the
far kerb); if *I*′ has a light set whose light 0 shows state 5 (`NOWALK` at the end of the walk
phase), queue `RUN`; if the player is standing in the way: turn back and stop. If the flat squared
distance to *t* < 1.5: step = 0 (walking resumes, with its start: a running pedestrian queues
`RUN_WALK`).

While waiting, the current sequence (normally `WALK`) plays to its end before `STAND` starts, so a
pedestrian may step a little past the kerb point. During steps 1–3 the pedestrian moves only by its
sequences' speeds and its heading; locate and the road end test do not run until it walks again.
Since a light set's walk phase lasts 6 s (3 s `WALK`, 3 s `NOWALK`) and pedestrians only start
crossing during `WALK`, a slow walker may still be in the street when the cars get green; nothing
checks for it.

## Checked against

- The light-set data the crossing uses: SF 116 and London 128 sets of kind 1 (`pedestrians.md`);
  at intersections without lights or with kind-0 sets pedestrians only turn corners.
- Sidewalk lines exist on both sides of every main road (`tools/ai/peds.py`, `pedestrians.md`).
- `tools/ai/peds.py` (edges survey): placement headings that read past a side's lines on 91 SF and
  173 London sides (all side B of one-way roads, *d* = −1); 1 SF and 67 London pairs of
  coinciding sidewalk ends of different roads, but no corner or crossing choice from any
  pedestrian side gives a zero-length crossing piece.
- The road-end choice, the curve and the locate rules were read from the machine code (sum
  orders checked in the disassembly); the dirty-side tool does not simulate walking.
