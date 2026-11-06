# AI driver: the planner (target point, bends, turns, detours)

Every DRIVE frame (`driver.md`, step 6.4) the AI driver plans a few candidate **paths** from its car
forward along the roads of its route window, bends them through the road's bends and the turns at
intersections, sends them around cars and props in the way, and picks one. The second point of the
chosen path is the driver's **target point**; the driver's speed control and steering use it
(`driver.md`). The planner draws no random numbers.

Parts: conventions and data (1–3), bends and junction turns (4–5), the planning pass and path tree
(6–11), placing points along the corridor (12–15), obstacles (16–18), notes (19).

## 1. Conventions

- Arithmetic as in `../physics/vehicle.md`, "Precision": float32, one rounding per operation, left to
  right as written, parentheses as written. `sin`, `cos`, `atan2`, `acos` in double precision. Where
  this page says *(unrounded)* the double value is used directly (in a comparison, or as a factor of
  the next product, whose result is rounded once); otherwise results are rounded to float when
  produced. A bearing θ = atan2(r, f) that is compared with a stored limit is compared as a double;
  when it becomes a limit it is stored as a float.
- `trunc(x)` rounds toward zero; `float(n)` converts an integer exactly. "xz" means only x and z are
  used. Vector expressions are per component, products first, left to right.
- `normalise(v)`: q = (v.x·v.x + v.y·v.y) + v.z·v.z; inv = 0 if q = 0, else 1/√q; result
  (inv·v.x, inv·v.y, inv·v.z). `flat_normalise(v)`: v.y := 0, then normalise. `unit(v)` is
  normalise with s = 1/√q computed first and the products written (v.x·s, v.y·s, v.z·s) (same
  values).
- `angle(a, b)` (unsigned angle, the path cost): Bq = (b.x·b.x + b.y·b.y) + b.z·b.z, Aq = (a.x·a.x +
  a.y·a.y) + a.z·a.z, inv = 1/float(√(Bq·Aq)); 0 if inv = 0; c = ((b.x·a.x + b.z·a.z) + b.y·a.y)·inv;
  0 if c > 0.99999988; 3.1415927 if c < −1.0; else acos(c). When the angle is added to a previous
  total, the double `acos` plus the float total is rounded once; a lone angle is rounded to float.
  A zero-length vector gives c = NaN and a NaN angle (it happens when a point lands on the
  previous one).
- `hdist(a, b)` = √((a.x − b.x)² + (a.z − b.z)²) (horizontal).
- In §13 and §14 every dot product of a section axis (X, Z) with a vector (the bearings' r and f,
  the offsets m, c and w) is **horizontal**: a·b = a.z·b.z + a.x·b.x; MM2 never uses the y
  components there (some are computed and dropped). `unit()` and `normalise()` stay 3D.
- `clamp(v, −h, h)`: −h if v < −h, else h if v > h, else v.
- **Tiny-lateral rule** (where a step names it with a value *q*): if r > −0.01 (−0.01 as a double,
  so r = −0.0099999998 counts) and r < 0.0099999998 (float 0.01), r := *q*.
- Comparisons exactly as written (`<` versus `≤`).

## 2. Data

### 2.1 Road data

For a road *R* with *N* sections (*j* = 0 … *N* − 1), from `../formats/bai.md` after loading:

| Name | Data |
|---|---|
| C[*j*], X[*j*], Y[*j*], Z[*j*] | centre points and section axes (X toward side A, Z against the road direction) |
| CD[*j*] | centre distances (CD[0] = 0); len(*a*, *b*) = CD[*b*] − CD[*a*] |
| hw | half width |
| Ea[*j*] | side A's kerb line (first *N* points of side A's edge block) |
| Eb[*j*], Bo[*j*] | side B's kerb line and outer sidewalk edge (side B's edge block) |
| L_B, S_B | side B's lane and sidewalk counts |
| T[0 … 9] | side B's band edges |
| divided, alley | road flags 0x01, 0x02 |
| traffic lists, prop lists | per side and section the traffic cars and props registered there (`traffic.md`, obstacle bookkeeping; `road-network.md`, "Obstacles"); per intersection its traffic cars and props |

`lanezone(R) = T[2·L_B − 1]` (0.0 when L_B = 0; MM2 reads a meaningless near-zero value there),
`walkzone(R) = T[2·L_B + 1]`, `curbw(R)`: with *j* = S_B + L_B − 1, T[2*j*] if *j* > 0, else
T[2*j* + 1]. (*j* = −1, a road with neither a lane nor a sidewalk on side B, makes MM2 read T[−1],
the same meaningless value as lanezone with L_B = 0: use 0.0. No shipped road has it: every road
has S_B + L_B ≥ 1.)

**Travel side** σ: 1 = travelling toward increasing section index (the driver's direction *d* = +1),
0 = toward decreasing index (*d* = −1). Per side at section *j*:

| | σ = 1 | σ = 0 |
|---|---|---|
| forward | −Z[*j*] | Z[*j*] |
| right rt | −X[*j*] | X[*j*] |
| right kerb | Eb[*j*] | Ea[*j*] |
| left kerb | Ea[*j*] | Eb[*j*] |
| far end (section reached last) | *N* − 1 | 0 |

For a horizontal vector *v* (a·b = a.x·b.x + a.z·b.z): σ = 1: right(*v*) = −(X[*j*]·*v*), ahead(*v*) =
−(Z[*j*]·*v*); σ = 0: right(*v*) = X[*j*]·*v*, ahead(*v*) = Z[*j*]·*v*.

**Progress index** of a point on *R* for side σ: the road's "section of a point" in direction σ
(`road-network.md`, "Section of a point"; written `progress(R, p, σ)` here, it may return *N*).
Raw section of progress *k*: *k* for σ = 1, *N* − 1 − *k* for σ = 0. Other road queries used:
`onroad(R, p, margin)` = "Is a point on the road" (1, 2 or 3), `index(R, p)` = "Section index of a
car on a road" (raw) (both `road-network.md`).

### 2.2 The driver's values

From `driver.md`: window roads *S*₀, *S*₁, *S*₂ (any may be none) with sides σ₀, σ₁, σ₂; route
index *w* and length *n*; **route-end slot** *f* (the first window slot not taken by the route's own
roads); destination *D* and its place kind κ (0 none, 1 road, 2 shortcut, 3 intersection); stop
distance *s*₂; pad *k*_p (the planning horizon; cops 75.0); the start lateral offset λ₀ (the
planner may update it, §14 step 4); the switches (avoid traffic, props, players, opponents; path
preference); the car's position *p*, body rows rowX (right) and rowZ (back); its extents F (front),
B (back), Lx (left), Rx (right) and tl = Lx + 1.0, tr = Rx + 1.0; whether its car kind is
`vppanozgt` (the **no-walk** car); and per junction turn *u* = 0, 1 the angle *a_u* and sign τ_u
(`driver.md`, "Junction angles"). "Drive on the left": the city aimap's setting (London: yes).

A window slot index of 3 arises wherever a rule moves on from slot 2 because "*S*₃ exists" (§13
steps 1 and 5, §14 steps 1 and 3), adds 1 to slot 2 (an arc point past the end of *S*₂: §11.1 step 8
with slot(src) = 2, §11.2 step 5.3 with *u* = 1), and in §16 (step 1, and the player test of step
3). A record with slot 3 is then handled by the same rules as any other. MM2 reads, as *S*₃, the
road that was *S*₀ when the junction geometry was last computed (the driver's last *S*₀; while
the planner runs this is always the current *S*₀, set in `driver.md` DRIVE step 6.3 just before),
and as σ₃ a byte it never writes (left-over memory). **Engine:** σ₃ = 0. Further:

- arc_test (§10) of a slot-3 record returns 0 (its slot loop runs *u* = slot … 2), so no arc ever
  starts from slot 3, and no record gets a slot above 3.
- *S*₄ (asked for as "*S*_s+1" with s = 3, and by §16 step 1) is always none: MM2 reads the
  driver's mode and previous mode there, both 0 (DRIVE) whenever the planner runs. So slot 4 only
  ever counts as a missing road.
- The junction values of slot 3 (§14, "Junction values of slot 2", same layout) would be τ₁ for
  *a*₃ and *G*₁ for *J*₃; the rules reach them only with σ₃ = 1, so with the engine's σ₃ never.

### 2.3 Obstacles

Things the obstacle scan (§16) can return: traffic cars, AI-driven cars (cops, opponents), player
cars, props. Each answers:

- `position(o)`: vehicles their position; props as §17.2 defines;
- `window_place(o)`: (slot *s* ∈ {0, 1, 2}, section index *v*) of the road it is on within the
  querying driver's window, or *s* = −1 (§18.3);
- `passing_points(o, from, dir, clr)`: a left and a right point to pass it (§18.1).

## 3. Planner state

### 3.1 Records

A **record** is one point of a path:

| Field | Meaning |
|---|---|
| pos | the point, always stored raised by 1.0 m in y |
| cost | cumulative turning angle from the car (the path cost) |
| dist | cumulative horizontal distance from the car; 9999.0 marks "the destination itself" |
| slot | window slot (0, 1, 2) of the road the point belongs to |
| prog | progress index on that road |
| mark | 0 plain, 1 arc point / no detour, 2 detour point, 3 blocked (no way round) |
| arc | 0 or 1 = the junction turn at the end of window slot 0 or 1; 2 + *t* = bend *t* of its road; for points placed by the corridor scans, the slot number |
| cls | class of the obstacle that caused a detour (stored, never read) |
| zone | 1, or for detour points the road class of the point (1 lanes, 2 sidewalk strip) |
| obj | the obstacle being passed (detour points) |

### 3.2 Working path and path table

- **Working path**: records Q₀ … Q₃₉. Q₀ is the car. Below, P(*k*), cost(*k*), … are the fields of
  Q_*k*.
- **Path table**: up to 25 rows of 40 records ("One array" below for longer ones), each row with
  its record count, a *blocked* flag (some record has mark 3) and a *zone-2* flag (some record has
  zone 2); the row count; the **chosen row** (−1 = none).
- All records persist between frames; the driver's reset zeroes every field of all of them. Fields
  this page does not write keep their previous contents, and this matters (§19).
- With chosen row −1 the target is record 1 of the working path (MM2 keeps the working path just in
  front of the table, so "row −1" reads it).
- **One array.** The working path and the table are one array of 40 + 25 × 40 records: working
  record 40 + *i* is row 0's record *i*, and row *r*'s record *i* is array record 40·(*r* + 1) + *i*.
  EXPAND never makes a record past Q₃₉ (§7 step 1), but a junction-turn arc (§11.2 step 5.6)
  advances *k* without a limit, so an arc that runs past Q₃₉ writes on into row 0's first records.
  That record then gets its obstacle check and CONTINUE as usual (CONTINUE's EXPAND(*k* + 1) stops
  at once). A commit copies records 0 … *k* one at a time in increasing order to array records
  40·(R + 1) + 0 … *k* (a row longer than 40 runs on into the next row's records; with R = 0 and
  *k* ≥ 40 a record copied late reads a slot the same commit has already overwritten) and sets the
  row's flags from the copied records. Reproduce this layout. A commit that would run past the
  end of the table (row 24 longer than 40) writes over MM2's other planner fields: undefined, never
  seen; **engine:** keep only the records that fit (the row's count is the number kept). (A turn
  arc's step count M grows with its radius, which is large for shallow turns, so how far past Q₃₉
  it can reach is bounded only by the geometry; the reference opponent run never gets past Q₉.)

### 3.3 Mutable values

| Name | Meaning |
|---|---|
| REF | reference point of the turn and bend geometry; changes **during** the expansion and is not restored when the expansion backs up |
| REF0 | saved reference point (set with REF by the driver when *S*₀ changes and when orders arrive) |
| FINAL | 0 while route intersections remain ahead, 1 once planning has reached the route's end. It is the driver's "route done" flag: the driver sets it to (*w* ≥ *n*) before planning (`driver.md`, DRIVE step 6.1); the expansion changes it and does not restore it |

## 4. Bends

Every road (main roads and shortcuts, `bai.md` load steps 2 and 4) gets a list of **bends** once at
load, before the drive-on-the-left mirroring. Each bend: first raw section *v*, signed angle *A*, sign
*s* (±1), corner *K*; and values recomputed by any driver that plans through the road (§4.2):
radius ρ, tangent length *dt*, centre *G*, start direction *Sd*, end direction *Ed* (unit, y = 0).
Readers always see the most recent recomputation (the values are shared by all drivers). Before a
bend is refreshed for the first time (by any driver) its ρ, *dt*, *G*, *Sd* and *Ed* are **0**
(MM2 leaves them uninitialised: the record is allocated without clearing). Such values can be read
by the arc test for window roads beyond the record's own slot (§10; with zero directions it never
matches) and by the driver's sharp-turn speed check for window roads the planner has not reached
(`driver.md`, "Speed control").

### 4.1 Building the list

Heading change at raw section *i*: *d* = C[*i* + 1] − C[*i*] (xz);
θ_*i* = atan2((−X[*i*].x)·d.x + (−X[*i*].z)·d.z, (−Z[*i*].x)·d.x + (−Z[*i*].z)·d.z), rounded.
For *i* = 1 while *i* < *N* − 1 (*i* := *i* + 1 after each pass):

1. If −0.7 ≤ θ_*i* ≤ 0.7: if len(*i*, *i* + 1) < 10.0 and *i* < *N* − 2: θ = θ_{*i*+1} + θ_*i*,
   *i* := *i* + 1; if θ < −0.7 or θ > 0.7, add a **two-section bend** with first section *a* = *i* − 1
   and angle θ.
2. Otherwise (|θ_*i*| > 0.7): if len(*i*, *i* + 1) ≥ 10.0 or *i* ≥ *N* − 2: add a **simple bend**
   *v* = *i*, *A* = θ_*i*, *s* = +1 if *A* ≥ 0 else −1, *K* = X[*i*] + Eb[*i*] (*s* = +1) or
   Ea[*i*] − X[*i*] (*s* = −1), all three components. Otherwise θ = θ_{*i*+1} + θ_*i*, *i* := *i* + 1; if
   |θ| > 0.7 add a two-section bend (*a* = *i* − 1); otherwise MM2 counts a bend without creating it
   (never happens in the shipped cities; skip).

Two-section bend: *v* = *a*, *A* = θ, *s* by sign as above; with d1 = −Z[*a*], d2 = Z[*a* + 2] and
*s* = +1: P1 = X[*a*]·1.5 + Eb[*a*], P2 = X[*a* + 2]·1.5 + Eb[*a* + 2]; *s* = −1: P1 = Ea[*a*] −
X[*a*]·1.5, P2 = Ea[*a* + 2] − X[*a* + 2]·1.5 (x and z);
t = ((((P2.z·d1.x) − (P1.z·d1.x)) − (d1.z·P2.x)) + (d1.z·P1.x)) / ((d1.z·d2.x) − (d1.x·d2.z));
*K* = (d2.x·t + P2.x, (*s* = +1 ? Eb : Ea)[*a* + 1].y, d2.z·t + P2.z).

**Bends in travel order** for side σ: σ = 1: bend *t* is list entry *t*; σ = 0: entry (count − 1 −
*t*). The bend's travel start section is *v* (σ = 1) or *N* − *v* − 1 (σ = 0); all other values are
the entry's own.

### 4.2 Refreshing the bends of road *R* for REF and side σ

For each bend in list order (*v*, *A*, *s*, *K*):

1. β = (3.14 − |*A*|)·0.5; a = REF.x − *K*.x; c = REF.z − *K*.z.
2. σ = 1: lat = (a·X[*v*].x) + (c·X[*v*].z). σ = 0: *m* = 1, and *m* = 2 if *v* + 2 < *N* and θ_*v*
   ≠ *A* (θ_*v* as in 4.1, rounded); lat = ((−X[*v*+*m*].x)·a) + ((−X[*v*+*m*].z)·c). Then lat := lat·*s*.
3. wmax = (lanezone + lanezone) − 1.5; h = 3.0 if lat < 3.0, else wmax if lat > wmax, else lat.
4. ρ = h / (1.0 − sin β); *dt* = cos β·ρ (sin, cos unrounded).
5. q = (ρ − h)·*s*; *G* = ((*dt*·Z[*v*]) + *K*) − (q·X[*v*]).
6. *Sd* = flat_normalise(((X[*v*]·(ρ·*s*)) + *G*) − *G*).
7. θ = *A*·*s*; sr = sin θ·ρ (rounded); cr = (cos θ·ρ)·*s*;
   Qe = ((*G*.x − Z[*v*].x·sr) + cr·X[*v*].x, (*G*.y − Z[*v*].y·sr) + X[*v*].y·cr, (*G*.z − Z[*v*].z·sr)
   + cr·X[*v*].z); *Ed* = flat_normalise(Qe − *G*).

(Roads travelled with σ = 0 almost always get h = 3.0 because of the sign in step 2.) These are
the "sharp turns" of the driver's speed control (`driver.md`): its *r* is ρ, its *t* is *dt*, its *C*
is *K*.

## 5. Junction turns

For *u* = 0, 1, the turn from *S_u* into *S_u*₊₁ (angle *a_u* and sign τ_u from `driver.md`). Values
kept per *u*: corner *J_u*, offset *h_u*, radius rⱼ_u, tangent length tⱼ_u, centre *G_u*, start
direction *Sdir_u*, end direction *Edir_u*. Computed when the driver's *S*₀ changes and in every
planning pass (§6 step 5), and then only for a *u* with τ_u ≠ 0 whose two roads *S_u* and *S_u*₊₁
both exist: for such a *u* 5.2 and then 5.3 run, for any other *u* nothing is done (5.2 included)
and all its values keep what they held.

### 5.1 Queue depth and clearances

`queue(list, P0, d)`: m = 0; for each obstacle o of the list: q = position(o);
val = ((q.x − P0.x)·d.x + d.z·(q.z − P0.z)) + 1.25; if val > m, m = val. Result m.

For slot *u*: road *S_u* (side σ, far end *e*₀) and *S_u*₊₁ (side σ′, first section *n*₁ in travel
order). "Own queue" = *S_u*'s traffic list of side σ at entry *N*(*S_u*) − 1; "oncoming queue" =
*S_u*₊₁'s traffic list of the **other** side (1 − σ′) at entry *N*(*S_u*₊₁) − 1. Right turn = τ_u > 0,
left turn = τ_u ≤ 0. Kerbs and rt from §2.1 for the respective road and side.

| | right-hand traffic, right turn | right-hand traffic, left turn | drive on left, right turn | drive on left, left turn |
|---|---|---|---|---|
| e_in (entry offset, *S_u*) | queue(own, rightkerb(*e*₀), −rt(*e*₀)) + Rx | Lx | Rx | queue(own, leftkerb(*e*₀), rt(*e*₀)) + Lx |
| m_in (other side, *S_u*) | 0 | queue(own, rightkerb(*e*₀), −rt(*e*₀)) | queue(own, leftkerb(*e*₀), rt(*e*₀)) | 0 |
| e_out (exit offset, *S_u*₊₁) | Rx | queue(onc, leftkerb(*n*₁), rt(*n*₁)) + Lx | queue(onc, rightkerb(*n*₁), −rt(*n*₁)) + Rx | Lx |
| m_out (other side, *S_u*₊₁) | queue(onc, leftkerb(*n*₁), rt(*n*₁)) | 0 | 0 | queue(onc, rightkerb(*n*₁), −rt(*n*₁)) |

### 5.2 Corner *J_u* and offset *h_u*

1. Corner lines: right turn: line 1 through P1 = rightkerb(*e*₀) + e_in·(−rt(*e*₀)), line 2 through
   P2 = rightkerb(*n*₁) + e_out·(−rt(*n*₁)). Left turn: P1 = leftkerb(*e*₀) + e_in·rt(*e*₀),
   P2 = leftkerb(*n*₁) + e_out·rt(*n*₁). Directions d1 = Z[*e*₀] of *S_u*, d2 = Z[*n*₁] of *S_u*₊₁.
2. t = ((((P2.z·d1.x) − (P1.z·d1.x)) − (P2.x·d1.z)) + (P1.x·d1.z)) / ((d2.x·d1.z) − (d2.z·d1.x));
   *J_u* = (d2.x·t + P2.x, y of the kerb point of P2 before the offset, d2.z·t + P2.z).
3. dx = REF.x − *J_u*.x, dz = REF.z − *J_u*.z; f = 1.0 if (tⱼ_u + 12.0)·(tⱼ_u + 12.0) < dx·dx + dz·dz
   (tⱼ_u = the **previous** value), else 0.0.
4. g = ((dz·L.z + dx·L.x)·τ_u) + f with L = X[*N* − 1] for σ = 1 and L = −X[0] for σ = 0 (= −rt(*e*₀)).
5. lo = Lx + Rx. For *S_u*: lim = (((curbw + curbw) − Rx) − e_in) − m_in; hi = lo if lim < lo, else
   min(lim, 9999.0); g := lo if g < lo, else hi if g > hi. Then the same with *S_u*₊₁, e_out, m_out.
   *h_u* = g.

### 5.3 Radius, centre, start and end

Right after 5.2, under the same condition (τ_u ≠ 0 and both roads exist):

1. h = *h_u* (5.2, with the current REF and the old tⱼ_u).
2. β = (3.14 − |*a_u*|)·0.5; rⱼ_u = h / (1.0 − sin β) (sin unrounded); tⱼ_u = cos β·rⱼ_u (cos
   unrounded).
3. Road-end axes of *S_u*: σ = 1: X′ = X[*N* − 1], Z′ = Z[*N* − 1]; σ = 0: X′ = −X[0], Z′ = −Z[0].
4. *G_u* = ((tⱼ_u·Z′) + *J_u*) − (((rⱼ_u − h)·τ_u)·X′) (y included).
5. *Sdir_u* = flat_normalise(((X′·(rⱼ_u·τ_u)) + *G_u*) − *G_u*).
6. θ = *a_u*·τ_u; sr = sin θ·rⱼ_u (rounded); cr = (cos θ·rⱼ_u)·τ_u; Qe = ((*G_u*.x − Z′.x·sr) + cr·X′.x,
   (*G_u*.y − Z′.y·sr) + X′.y·cr, (*G_u*.z − Z′.z·sr) + X′.z·cr); *Edir_u* = flat_normalise(Qe − *G_u*).

The driver's speed control uses *J_u*, rⱼ_u and tⱼ_u (`driver.md`, junctions).

## 6. One planning pass

1. Record 0: P(0) = *p* with y + 1.0; cost 0; dist 0; arc 0. Its slot and progress:
   *k* = progress(*S*₀, *p*, σ₀). If *k* < *N*(*S*₀): slot 0, prog *k*. Else if *S*₁ is none: slot 0,
   prog *N*(*S*₀) − 1. Else slot 1, *k* = progress(*S*₁, *p*, σ₁); if *k* < *N*(*S*₁): prog *k*; else if
   *S*₂ is none: prog *N*(*S*₁) − 1 (slot 1); else slot 2, prog progress(*S*₂, *p*, σ₂) (may equal
   *N*(*S*₂); the corridor steps then read sections outside the road, §19 note 7). (obj, cls and
   zone of record 0 are not written.)
2. Row count = 0; clear all 25 blocked and zone-2 flags.
3. If *w* < *n*: r = arc_test(0) (§10; it may change record 0's slot). If r = 0: mark(0) = 0 and
   REF0 = *p* (not raised). Otherwise, or if *w* ≥ *n*: mark(0) = 1 (REF0 unchanged).
4. REF = REF0.
5. Recompute the junction turns (§5).
6. EXPAND(1) (§7).
7. Choose the row (§9). The target is record 1 of the chosen row. The driver's *k*₀ is that row's
   record 0 arc field.

## 7. EXPAND(*k*): make record *k* from record *k* − 1

1. If *k* > 39, stop (nothing is committed for this branch).
2. *s* = slot(*k* − 1), road *S_s*.
Steps 3 and 4 are the two branches of **one** test of FINAL, made once at the start of this
EXPAND: if the route corridor step of step 3 sets FINAL = 1, step 4 does not run for this record
(the new value applies from the next record on).

3. **FINAL = 0** (route intersections remain):
   1. Refresh the bends of *S_s* for REF and σ_s (§4.2).
   2. r = arc_test(*k* − 1). If r ≠ 0: arc(*k* − 1) = r − 1; place_arc(*k*, src = *k* − 1) (§11).
   3. If r = 0: REF = P(*k* − 1); **route corridor step** for *k* (§14); r = arc_test(*k*); if r ≠ 0:
      arc(*k*) = r − 1; place_arc(*k*, src = *k*).
4. **FINAL = 1**:
   1. If *S_s* is none: **destination step** for *k* (§12); go to 5.
   2. Refresh the bends of *S_s* for REF and σ_s; r = arc_test(*k* − 1).
   3. If r ≠ 0: arc(*k* − 1) = r − 1; t = place_arc(*k*, src = *k* − 1). If t ≠ 0: arc(*k*) += 1;
      REF = P(*k*); if *S*_slot(*k*) exists and onroad(*S*_slot(*k*), P(*k*), 2.0) > 1: **final-road
      corridor step** for *k* (§13).
   4. If r = 0: REF = P(*k* − 1); final-road corridor step for *k*; r = arc_test(*k*); if r ≠ 0:
      arc(*k*) = r − 1; place_arc(*k*, src = *k*).

   (place_arc may advance *k*, §11.2; from here on *k* is the advanced value.)
5. **Obstacle check** of the stretch P(*k* − 1) → P(*k*): zone(*k*) = 1. Run the **obstacle scan**
   (§16) from P(*k* − 1) to P(*k*) with start (slot(*k* − 1), prog(*k* − 1)), end (slot(*k*),
   prog(*k*) + 3), margin (B + F) + (B + F). It returns an obstacle O or none, and a class c (0 player
   car, 1 traffic or AI car, 2 opponent, 5 prop).
6. None → CONTINUE(*k*) (§8); done.
7. If c = 2 and (*w* ≤ 2 or mark(*k* − 1) = 1) → CONTINUE(*k*); done.
8. Q = position(O); dz = P(*k* − 1).z − Q.z, dx = P(*k* − 1).x − Q.x; if *k*_p·*k*_p ≤ dx·dx + dz·dz →
   CONTINUE(*k*); done.
9. Otherwise collect the alternatives (§18.2), with walking allowed unless the car is the no-walk
   car, and for each alternative *i* in order: use_alternative(*k*, *i*, c) (§18.4).

## 8. CONTINUE(*k*): extend or commit

1. If dist(*k*) < *k*_p, *S*_slot(*k*) exists, row count < 10, and either slot(*k*) = *f*, or
   (prog(*k*) ≥ *N*(*S*_slot(*k*)) − 1 and slot(*k*) + 1 ≤ 2 and *S*_slot(*k*)+1 is none): FINAL = 1 and
   EXPAND(*k* + 1); done.
2. Else if dist(*k*) < *k*_p, *S*₂ exists, not (slot(*k*) = 2 and prog(*k*) = *N*(*S*₂)), and row
   count < 10: EXPAND(*k* + 1); done.
3. Else **commit**: copy records 0 … *k* into row R = row count; its zone-2 flag if any copied record
   has zone 2, its blocked flag if any has mark 3; record count = *k* + 1; row count += 1.

Expansion is depth first: an EXPAND returns only after all paths it started are committed or
dropped; the alternatives loop of §7 step 9 then continues with the next alternative, overwriting
records *k*, *k* + 1, … of the working path.

## 9. Choosing the path

best = 99999.0, chosen row −1; a pass takes a row only if its last record's cost is < best
(strict: the first of equal rows wins), updating best.

1. If the path-preference switch is on: a pass over rows whose zone-2 flag is set.
2. If no row chosen: a pass over rows whose blocked flag is clear (best reset to 99999.0).
3. If still none: a pass over all rows (best reset to 99999.0).

## 10. arc_test(*q*): is record *q* inside a bend or a junction turn?

Returns 0 (no), *u* + 1 (inside the junction turn at the end of window slot *u*) or *t* + 3 (inside
bend *t* of its road, travel order; then it also sets slot(*q*)).

1. *R* = *S*_slot(*q*), σ = σ_slot(*q*). The first bend at or after the record: for *v* = prog(*q*),
   prog(*q*) + 1, … while *v* < *N*(*R*): *t* = the travel-order index of the bend whose travel start
   section is *v*; stop at the first found. None: *t* = −1. (prog(*q*) ≥ *N*(*R*) is undefined in
   MM2; use −1.)
2. For *u* = slot(*q*), …, 2:
   1. If *S_u* exists and *t* > −1: for each bend *t′* = *t*, …, count − 1 of *S_u* (travel order,
      side σ_u) with ρ, *G*, *Sd*, *Ed*, *s*: lim = (ρ + 15.0)·(ρ + 15.0); dx = P(*q*).x − *G*.x,
      dz = P(*q*).z − *G*.z; if dx·dx + dz·dz < lim:
      - σ_u = 1: T1 = ((*s*·*Sd*.x)·dz) + ((*s*·(−*Sd*.z))·dx), T2 = ((*s*·*Ed*.z)·dx) + ((*s*·(−*Ed*.x))·dz);
      - σ_u = 0: T1 = ((*s*·*Ed*.z)·dx) + ((*s*·(−*Ed*.x))·dz), T2 = ((*s*·(−*Sd*.z))·dx) + ((*s*·*Sd*.x)·dz);
      - if T1 ≥ −0.01 and T2 ≥ 0.01: slot(*q*) = *u*; return *t′* + 3.

      After this loop (whether or not it ran) *t* := 0. (If *S_u* is none or *t* = −1 the loop is
      skipped and *t* is **not** reset: a record with no bend ahead on its own road never tests the
      bends of later roads.) All five values of bend *t′*, the sign *s* included, are those of the
      travel-order bend *t′* for side σ_u (list entry *t′* for σ_u = 1, count − 1 − *t′* for σ_u = 0).
   2. Junction turn of slot *u*: only if *u* = 0, or *u* = 1 and (arc(*q*) = 1 or −0.2 < *a*₀ < 0.2);
      and τ_u ≠ 0; and with dx = P(*q*).x − *G_u*.x, dz = P(*q*).z − *G_u*.z: dx·dx + dz·dz <
      (rⱼ_u + 15.0)·(rⱼ_u + 15.0); then if ((−*Sdir_u*.z)·τ_u)·dx + (*Sdir_u*.x·τ_u)·dz ≥ −0.01 and
      (τ_u·*Edir_u*.z)·dx + ((−*Edir_u*.x)·τ_u)·dz ≥ 0.01: return *u* + 1.
3. Return 0.

## 11. place_arc(*k*, src): points on a bend or turn arc

Uses arc(src) (set just before by the caller) and P(*k* − 1). *k* is passed by reference (§11.2 may
advance it). Returns 0 or 1.

**FINISH(*k*, with angle?)**: mark(*k*) = 1; P(*k*).y := P(*k*).y + 1.0;
dist(*k*) = √(dz·dz + dx·dx) + dist(*k* − 1) with dz = P(*k* − 1).z − P(*k*).z, dx = P(*k* − 1).x −
P(*k*).x; with angle: cost(1) = angle(−rowZ, P(1) − *p*) for *k* = 1, else cost(*k*) =
angle(P(*k* − 1) − P(*k* − 2), P(*k*) − P(*k* − 1)) + cost(*k* − 1); without angle cost(*k*) keeps its
previous value.

### 11.1 Bend arc (arc(src) ≥ 2)

*t* = arc(src) − 2, road *R* = *S*_slot(src), side σ; the travel-order bend *t* has travel start
section *v_t*, *s*, ρ, *A*, *G* (§4.2 values).

1. arc(*k*) = slot(src).
2. n = trunc(((*A*·ρ)·*s*)·0.1); M = n + (n mod 2) (odd n rounds up); if M < 2, M = 2.
   Aa = *A*·*s*; step = Aa / float(M).
3. Axes Xv = X[*i*], Zv = Z[*i*] with *i* = *v_t* (σ = 1) or *N* − *v_t* − 1 (σ = 0) (the bend's own
   first raw section).
4. arcpt(θ): sr = sin θ·ρ (rounded), cr = (cos θ·ρ)·*s*; arcpt = ((*G*.x − Zv.x·sr) + cr·Xv.x,
   (*G*.y − 0) + 0, (*G*.z − Zv.z·sr) + Xv.z·cr) (sin θ, cos θ unrounded).
5. Arc start in travel order: σ = 1: Es = ((Xv.x·(ρ·*s*)) + *G*.x, 0 + *G*.y, (Xv.z·(ρ·*s*)) + *G*.z);
   σ = 0: Es = arcpt(float(M)·step).
6. u = normalise(Es − *G*) (x and z); w = P(*k* − 1) − *G*; φ = atan2((w.x·(−u.z)) + (u.x·w.z),
   (w.x·u.x) + (w.z·u.z)) rounded; if (*s* < 0 and σ = 1) or (*s* > 0 and σ = 0): φ := φ·(−1.0).
7. If φ < −0.1 (the previous point is before the arc): P(*k*) = Es; prog(*k*) = progress(*R*, Es, σ);
   slot(*k*) = slot(src); FINISH(*k*, with angle); return 0.
8. half = step·0.5. For *i* = 1 … M: if float(*i*)·step − half > φ: *j* = *i* (σ = 1) or M − *i* (σ = 0);
   P(*k*) = arcpt(float(*j*)·step); slot(*k*) = slot(src); prog(*k*) = progress(*R*, P(*k*), σ); if
   prog(*k*) ≥ *N*(*R*): slot(*k*) += 1, prog(*k*) = 0; FINISH(*k*, without angle); return 1 if
   *i* > 1 − trunc(float(M)·(−0.5)) (that is *i* > 1 + M/2), else 0.
9. No *i* qualified (the previous point is within half a step of the arc's end): Qe = arcpt(Aa);
   prog(*k* − 1) = index(*R*, Qe) (raw, no side conversion); record *k* is **not** written; return 1.

### 11.2 Junction turn arc (arc(src) = *u* ∈ {0, 1})

Road *R* = *S_u*, σ = σ_u; X′, Z′ as in §5.3 step 3.

1. q = rⱼ_u·τ_u; arc start A0 = (X′.x·q + *G_u*.x, X′.y·q + *G_u*.y, q·X′.z + *G_u*.z); arc(*k*) = *u*.
2. w = P(*k* − 1) − *G_u*; φ = atan2((*Sdir_u*.x·w.z) + ((−*Sdir_u*.z)·w.x), (w.z·*Sdir_u*.z) +
   (w.x·*Sdir_u*.x)) rounded; if τ_u < 0: φ := −φ.
3. If φ < −0.1: P(*k*) = A0; FINISH(*k*, with angle); slot(*k*) = *u*; prog(*k*) = *N*(*S_u*) − 1;
   mark(*k*) = 0; return 0.
4. n = trunc(((rⱼ_u·τ_u)·*a_u*)·0.1); M as in §11.1; step = (τ_u·*a_u*)/float(M); half = step·0.5.
5. For *i* = 1 … M: θ = float(*i*)·step (rounded). If θ − half > φ:
   1. sr = sin θ·rⱼ_u, cr = (cos θ·rⱼ_u)·τ_u; P(*k*) = ((*G_u*.x − Z′.x·sr) + cr·X′.x, (*G_u*.y −
      Z′.y·sr) + X′.y·cr, (*G_u*.z − sr·Z′.z) + X′.z·cr).
   2. FINISH(*k*, without angle).
   3. If (τ_u·*a_u*)·0.5 ≤ θ: slot(*k*) = *u* + 1; prog(*k*) = progress(*S_u*₊₁, P(*k*), σ_u₊₁); if
      prog(*k*) ≥ *N*(*S_u*₊₁): slot(*k*) += 1, prog(*k*) = 0; if *u* + 1 ≥ *f*: FINAL = 1. Else:
      slot(*k*) = *u*, prog(*k*) = *N*(*S_u*) − 1.
   4. arc(*k*) = *u*.
   5. If 2·*i* > M and onroad(*S_u*₊₁, P(*k*), 0.0) < 3: arc(*k*) = *u* + 1; REF = P(*k*); return 1.
   6. If *i* < M: *k* := *k* + 1 (the following arc points go into the next records).
6. Return 1. (If no *i* qualified, record *k* is not written.)

A turn arc writes every remaining arc step ahead of the previous point into consecutive records,
until a point in its second half lies on the next road or the arc ends; only the last of them gets
the obstacle check of §7 step 5.

## 12. Destination step (aiming straight at the destination)

Used when the window has no road for record *k* − 1 (§7 step 4.1), when the destination is at an
intersection or nowhere on the network, when the final-road scan reaches the destination, and when
the route scan runs out of roads before an intersection destination. Writes record *k* from
prev = record *k* − 1:

1. If *s*₂ = 0: P(*k*) = *D*. Otherwise d = hdist(prev.pos, *D*); if d < *s*₂: P(*k*) = prev.pos; else
   g = (d − *s*₂)/d and P(*k*) = (*D* − prev.pos)·g + prev.pos.
2. P(*k*).y := P(*k*).y + 1.0.
3. mark, prog, slot and arc are copied from prev; dist(*k*) = 9999.0 (the destination marker).
4. Cost as in "Recording a point" step 4.

**Recording a point** (end of §13 and §14), with the target already in P(*k*) and an output (slot
*s*, progress *q*):

1. mark(*k*) = 0; prog(*k*) = *q*; slot(*k*) = *s*; arc(*k*) = *s*.
2. P(*k*).y := P(*k*).y + 1.0.
3. dist(*k*) = hdist(P(*k* − 1), P(*k*)) + dist(*k* − 1).
4. *k* = 1: cost(1) = angle(−rowZ, P(1) − *p*); *k* > 1: cost(*k*) = angle(P(*k* − 1) − P(*k* − 2),
   P(*k*) − P(*k* − 1)) + cost(*k* − 1).

## 13. Final-road corridor step

Used on the final leg (FINAL = 1) while the destination is on a road. From P, look along the road
between its two kerbs pulled in by the car's width; keep the narrowest visible window; when the far
side of the road crosses the window, aim at the corner (apex) that blocks the view. In this section
prev = record *k* − 1, *N* = vertex count of the road named, and bearings use right()/ahead() of
§2.1.

**Step 1 – start.** s := prev.slot, q := prev.prog, *N* := *N*(*S_s*). If *k* > 1 and mark(*k* − 2) ≠
1: q := q + 1, and if then q = *N*: if *S_s*₊₁ exists, s := s + 1, q := 0, *N* := *N*(*S_s*); otherwise
q := q − 1. Frame index φ := *N* − q, or *N* − 1 if that equals *N* (used only when σ_s = 0).

**Step 2 – destination kind.** κ = 0 or 3: do §12 instead and stop. κ not in 0–3: change nothing and
stop. κ = 1 or 2: continue.

**Step 3.** P := prev.pos.

**Step 4 – initial window** at section *j* of *S_s* (σ = σ_s), giving the left limit λL and the right
limit λR. Both points are measured from prev.pos, even after P has been shifted below.

σ = 1 (*j* = q; the bearings' frame is section *j* of *S_s* itself: right() and ahead() of §2.1
with X[*j*] and Z[*j*], the same section as the two points):
- Left point L = C[*j*] − tl·X[*j*] if the road is divided, else Ea[*j*] − Lx·X[*j*] (no +1 here).
  v = L − prev.pos, r = right(v), f = ahead(v); tiny-lateral rule with −1.0; λL := atan2(r, f).
- Right point R = Rx·X[*j*] + Eb[*j*] (no +1); v = R − prev.pos; tiny-lateral rule with +1.0;
  λR := atan2(r, f).

σ = 0 (*j* = *N* − 1 − q; frame section φ; the divided flag is not used):
- u = unit(C[*j*] − Eb[*j*]).
- L = tl·u + Eb[*j*]; v = L − prev.pos; r = X[φ]·v, f = Z[φ]·v. If *k* = 1 and r > 0: P := prev.pos +
  r·X[φ] and r := 0. If *k* = 1 and f < 1: f := 1. λL := atan2(r, f).
- R = Ea[*j*] − tr·u; v = R − prev.pos; r = X[φ]·v, f = Z[φ]·v. If *k* = 1 and r < 0: P := prev.pos −
  r·X[φ] and r := 0 (this moves P further right — MM2's sign — and replaces a shift made by the
  left point). If *k* = 1 and f < 1: f := 1. λR := atan2(r, f).

Both apexes start at (s, q): (sL, qL) := (s, q), (sR, qR) := (s, q). The shifted P is used only
inside this step; nothing else sees it (the arc placement of §11 reads record *k* − 1, never P).

**Step 5 – first scanned section.** q := q + 1; if q = *N*: if *S_s*₊₁ exists, s := s + 1, q := 0;
otherwise q := q − 1. The scanned road does not change after this. If q ≥ its section count, skip
step 6.

**Step 6 – scan** q, q + 1, … up to the last section of *S_s*. Bearings use the frame of the previous
point: road *S*_prev.slot with side σ′ and frame section *j′* = prev.prog if σ′ = 1, else *N′* −
prev.prog − 1. (MM2 then replaces a *j′* equal to *N′* by *N′* − 1, which needs prev.prog = −1: no
record has a negative progress, so it never applies.)

σ = σ_s = 1 (*j* = q):
1. L = (divided ? C[*j*] : Ea[*j*]) − tl·X[*j*]; v = L − P; r, f; tiny-lateral rule with −1.0 (no lower
   bound on f); θ = atan2(r, f). If θ > λR: **stop at the right apex** (A1). If θ > λL − 0.001:
   λL := θ, (sL, qL) := (s, q).
2. R = tr·X[*j*] + Eb[*j*]; v = R − P; tiny-lateral rule with +1.0; θ. If θ < λL: **stop at the left
   apex** (A2). If θ < λR + 0.001: λR := θ, (sR, qR) := (s, q).

σ = 0 (*j* = *N* − 1 − q):
1. u = unit(C[*j*] − Eb[*j*]); L = tl·u + Eb[*j*]; v = L − P; r = right(v), f = ahead(v) (the previous
   point's frame); tiny-lateral rule with −1.0; f := 1 if f < 1; θ. If θ > λR: stop at the right apex
   (A3). If θ > λL − 0.001: update λL and (sL, qL).
2. R = Ea[*j*] − tr·X[φ] (φ from step 1 applied to *S_s*'s axes); tiny-lateral rule with +1.0;
   f := 1 if f < 1; θ. If θ < λL: stop at the left apex (A4). If θ < λR + 0.001: update λR, (sR, qR).

(λL − 0.001 and λR + 0.001 are float sums.)

Stop rules (the target goes to P(*k*); the output slot is the apex's). Everything a rule reads —
the points, the axes, *N* and the divided flag — belongs to the apex's road: *S*_sR for A1 and A3,
*S*_sL for A2 and A4 (not the scanned road, when the apex lies on an earlier one):
- **A1** (σ = 1 scan, right apex): if *k* > 1 and (sR, qR) = (prev.slot, prev.prog), qR := qR + 1
  unless that exceeds *N*(*S*_sR) − 1. Target = tr·X[qR] + (divided ? C[qR] : Eb[qR]) on *S*_sR,
  indexed by qR directly whatever its side.
- **A2** (σ = 1 scan, left apex): the same adjustment of qL. Target = (divided ? C[qL] : Ea[qL]) −
  tl·X[qL] on *S*_sL.
- **A3** (σ = 0 scan, right apex; no adjustment): σ_sR = 1: u = unit(C[qR] − Eb[qR]),
  target = tr·u + Eb[qR]; σ_sR = 0: *j* = *N* − qR − 1, u = unit(C[*j*] − Ea[*j*]), target = tr·u + Ea[*j*].
- **A4** (σ = 0 scan, left apex): σ_sL = 1: u = unit(C[qL] − Ea[qL]), target = tl·u + (divided ?
  C[qL] : Ea[qL]); σ_sL = 0: *j* = *N* − qL − 1, u = unit(C[*j*] − Eb[*j*]), target = tl·u + Eb[*j*].

After a stop the output progress is **min(qL, qR)** (adjusted values); go to step 8.

**Step 7 – no stop** (the window never closed): the target keeps prev.pos's sideways offset, placed
at an apex; *N* = section count of the apex's road:

1. σ_sR = 0 and qR < *N* − 1: h = (hw − Rx) − 1.0; c = clamp(−(X[*N*−1]·(C[*N*−1] − prev.pos)), −h, h);
   w = h − (c/h)·h; *j* = *N* − qR − 1; target = (Ea[*j*] − tr·X[*j*]) − w·X[*j*]; output (sR, qR).
2. Else σ_sL = 0 and qL < *N* − 1: h = (hw − Rx) − 1.0 (the right extent, as MM2 does);
   c = clamp(−(X[*N*−1]·(C[*N*−1] − prev.pos)), −h, h); h2 = (hw − Lx) − 1.0; g = (c/h + 1.0)·h2;
   *j* = *N* − qL − 1; target = (tr·X[*j*] + Eb[*j*]) + g·X[*j*]; output (sL, qL).
3. Else σ_sR = 1 and qR < *N* − 1: h = (hw − Rx) − 1.0; c = clamp(X[0]·(C[0] − prev.pos), −h, h);
   w = h − (c/h)·h; target = (tr·X[qR] + Eb[qR]) + w·X[qR]; output (sR, qR).
4. Else σ_sL = 1 and qL < *N* − 1: h = (hw − Lx) − 1.0; c = clamp(X[0]·(C[0] − prev.pos), −h, h); if
   divided: target = C[qL] − tl·X[qL]; else w = h − (c/h)·h and target = (Ea[qL] − tl·X[qL]) −
   w·X[qL]; output (sL, qL).
5. Else: P(*k*) is not written (keeps whatever it held); output (s, min(qL, qR)). If step 8
   then records the point, its y is still raised by 1.0 on top of the old value.

("(c/h)·h" is the quotient times h.)

**Step 8 – destination check.** qD = progress(*S*_out, *D*, σ_out). If the output progress ≥ qD, do
§12 instead (it overwrites the target). Otherwise record the point (§12, "Recording a point").

## 14. Route corridor step

Used while route intersections remain (FINAL = 0). Same window idea as §13, but the scan can run on
into the next route roads, sharp junction corners use the junction corners *J*, and on a divided road
the car stays on the half it is on. prev = record *k* − 1.

**Step 1 – start.** As §13 step 1, except that the advance happens when q + 1 ≥ *N* (q := 0 on the
next road, or q unchanged when there is none). φ := *N* − q (*N* − 1 if equal to *N*). P := prev.pos.

**Step 2 – initial window** at section *j* of *S_s* (σ = σ_s), u = unit(C[*j*] − Eb[*j*]):

σ = 0 (*j* = *N* − 1 − q; frame section φ):
- m = (prev.pos − C[*j*])·X[*j*] (prev's offset to the right of the centre).
- L = tl·u + (divided and m ≥ 0 ? C[*j*] : Eb[*j*]); v = L − P; r = X[φ]·v; f = Z[φ]·v; if *k* = 1 and
  f < 1, f := 1; λL := atan2(r, f).
- R = (divided and m < 0 ? C[*j*] : Ea[*j*]) − tr·u; the same; λR.

σ = 1 (*j* = q; frame section q):
- m = −(X[*j*]·(prev.pos − C[*j*])).
- L = (divided and m ≥ 0 ? C[*j*] : Ea[*j*]) − tl·u; r = right(v), f = ahead(v); the *k* = 1 clamp of f;
  λL.
- R = tr·u + (divided and m < 0 ? C[*j*] : Eb[*j*]); λR.

("m ≥ 0" is false for NaN.) No tiny-lateral rule in this step. Apexes start at (s, q).

**Junction values of slot 2.** Junction angles, signs and corners exist only for *u* = 0, 1. Where
this section reads *a_s* or *J_s* with s = 2 (step 3 below, and the end-of-road tests of rules B1
and B2), MM2 reads the next values stored after them: *a*₂ is junction 0's turn sign τ₀ as a
number (1.0, −1.0 or 0.0) and *J*₂ is junction 0's arc centre *G*₀. Reproduce this. "*S*₃ exists"
(step 3 with s = 2) means the slot-3 road of §2.2 (the driver's last *S*₀) is not none.

**Step 3 – first scanned section.** q := q + 1. If q = *N*:
- if *S_s*₊₁ exists: if *a_s* < −0.7 or *a_s* > 0.7: q := q − 1, P(*k*) := *J_s* and record the
  point with (s, q) — done. Otherwise, if s + 1 = *f*: q := q − 1 (stay); else s := s + 1, q := 0;
- otherwise q := q − 1.

**Step 4 – default target** on *S_s* (*N* its section count, h = (hw − Rx) − 1.0):
- σ_s = 0: c := λ₀ if *k* = 1 and q < 1; otherwise c := clamp(−(X[φ]·(C[*N*−1−q] − P)), −h, h), and
  for *k* = 1 also λ₀ := c. (φ is from step 1 even if step 3 moved to the next road.) g = c/h;
  E = Ea[0] − tr·X[0]; P(*k*) := (E − C[0])·g + C[0].
- σ_s = 1: c := λ₀ if *k* = 1 and q < 1; otherwise c := clamp(X[q]·(C[q] − P), −h, h) (and λ₀ := c for
  *k* = 1). g = c/h; E = tr·X[q] + Eb[*N*−1]; P(*k*) := (E − C[*N*−1])·g + C[*N*−1].

This is the end of the road (in travel order) at the start point's sideways offset c.

**Step 5 – scan** while q < *N*(*S_s*) (s may advance). Bearings use the previous point's frame:
road *S*_prev.slot, *j′* = prev.prog if σ′ = 1, else *N′* − prev.prog (or *N′* − 1 if that equals
*N′*) — one section different from §13.

For the current section (σ = σ_s, *j* = q if σ = 1 else *N* − 1 − q, u = unit(C[*j*] − Eb[*j*])):
1. Left point: σ = 0: L = tl·u + (divided and m ≥ 0 ? C[*j*] : Eb[*j*]); σ = 1: L = (divided and m ≥ 0
   ? C[*j*] : Ea[*j*]) − tl·u. v = L − P; r, f; tiny-lateral rule with −1.0; f := 1 if f < 1; θ. If θ >
   λR: **stop at the right apex** (B1). If θ > λL − 0.001: update λL, (sL, qL).
2. Right point: σ = 0: R = (divided and m < 0 ? C[*j*] : Ea[*j*]) − tr·u; σ = 1: R = tr·u + (divided and
   m < 0 ? C[*j*] : Eb[*j*]). r, f; tiny-lateral rule with **0.0**; f := 1 if f < 1; θ. If θ < λL:
   **stop at the left apex** (B2). If θ < λR + 0.001: update λR, (sR, qR).
3. End of road (q = last section of *S_s*):
   1. If s < 2: if *a_s* < −0.7 and qL = qR = q and sL = sR = s: P(*k*) := *J_s*, record with (s, q),
      done. Else if *a_s* > 0.7 and the same four equalities: likewise.
   2. If hdist(P, P(*k*)) + prev.dist ≥ *k*_p (P(*k*) = the current default target; the
      horizontal distance, not 3D; the sum is rounded once and a NaN sum fails the test): go to
      step 6.
   3. If s ≥ 2: go to step 6.
   4. If s + 1 ≥ *f*: if κ = 3, do §12 and stop; otherwise FINAL := 1 and go to step 6.
   5. If *S_s*₊₁ is none: go to step 6.
   6. Move on: s := s + 1, q := −1 (the increment below makes it 0). With the new road (*N* sections,
      h′ = (hw − Rx) − 1.0) the default target is carried over at the same sideways offset (both
      dots horizontal; σ_s = 0 forms (C[*N*−1] − P(*k*))·(−X[*N*−1]), the same value as the
      negated dot written below):
      σ_s = 0: c = clamp(−(X[*N*−1]·(C[*N*−1] − P(*k*))), −h′, h′), g = c/h′, P(*k*) := (E − C[0])·g +
      C[0] with E = Ea[0] − tr·X[0]; σ_s = 1: c = clamp(X[0]·(C[0] − P(*k*)), −h′, h′), g = c/h′,
      P(*k*) := (E − C[*N*−1])·g + C[*N*−1] with E = tr·X[*N*−1] + Eb[*N*−1].
4. q := q + 1.

Stop rules (output = the apex's slot and progress; no adjustment, no minimum). As in §13, the
points, axes, *N*, the divided flag and the junction values belong to the apex's road (*S*_sR for
B1, *S*_sL for B2); only m comes from the initial window:
- **B1** (right apex (sR, qR)): σ_sR = 1: u = unit(C[qR] − Eb[qR]), target = tr·u + (divided and m < 0
  ? C[qR] : Eb[qR]); σ_sR = 0: *j* = *N* − qR − 1, u = unit(C[*j*] − Ea[*j*]), target = tr·u + (divided
  and m < 0 ? C[*j*] : Ea[*j*]). Only when the scan road had σ = 1 and σ_sR = 1: if qR is the last
  section of *S*_sR and *a*_sR > 0.7, the target becomes *J*_sR.
- **B2** (left apex (sL, qL)): σ_sL = 1: u = unit(C[qL] − Ea[qL]), target = tl·u + (divided and m ≥ 0 ?
  C[qL] : Ea[qL]); σ_sL = 0: *j* = *N* − qL − 1, u = unit(C[*j*] − Eb[*j*]), target = tl·u + (divided
  and m ≥ 0 ? C[*j*] : Eb[*j*]). Only when the scan road had σ = 1 and σ_sL = 1: if qL is the last
  section and *a*_sL < −0.7, the target becomes *J*_sL.

Then record the point (§12).

**Step 6 – window still open.** Try in order (s = the current scan slot; *N* = section count of the
apex's road; "the apex qualifies": its slot < s, or its progress < *N* − 1). Each written fallback
also clears FINAL. prev = prev.pos; *S′* = *S*_prev.slot with *N′* sections and side σ′ (kerb lines
Ea′, Eb′, axes X′), pv = prev.prog. Everything unprimed — points, axes, *N* and the divided flag —
is the apex's road's (*S*_sR in (R) and (R1), *S*_sL in (L) and (L1)).

1. **(R)** σ_sR = 0 and the right apex qualifies: divided and m < 0: *j* = *N* − qR − 1, target = C[*j*] −
   tr·unit(C[*j*] − Eb[*j*]). Else, with pv1 = max(pv, 1): σ′ = 1: w = (prev − Eb′[pv1])·X′[pv1],
   target = w·X′[pv1] + Ea[*N* − qR − 1]; σ′ = 0: *j′* = *N′* − pv1, w = −((prev − Ea′[*j′* − 1])·X′[*j′*]),
   target = Ea[*N* − qR − 1] − w·X′[*j′*]. Output (sR, qR).
2. **(L)** else σ_sL = 0 and the left apex qualifies: divided and m > 0: *j* = *N* − qL − 1, target =
   tr·unit(C[*j*] − Eb[*j*]) + C[*j*] (right inset, as MM2). Else as (R) but with Ea[*N* − qL − 1] (MM2
   uses the same kerb here). Output (sL, qL).
3. **(R1)** else σ_sR = 1 and the right apex qualifies: divided and m ≥ 0: target = tr·unit(C[qR] −
   Eb[qR]) + C[qR]. Else: σ′ = 1: w = (prev − Eb′[pv])·X′[pv]; σ′ = 0: *j″* = *N′* − pv − 1,
   w = −((prev − Ea′[*j″*])·X′[*j″*]); if w < Rx, w := Rx; target = w·X[qR] + Eb[qR]. Output (sR, qR).
4. **(L1)** else σ_sL = 1 and the left apex qualifies: divided and m < 0: target = C[qL] − tl·X[qL].
   Else: σ′ = 1: w = −((prev − Ea′[pv])·X′[pv]); σ′ = 0: *j″* = *N′* − pv − 1, w = (prev −
   Eb′[*j″*])·X′[*j″*]; if w < Lx, w := Lx; target = Ea[qL] − w·X[qL]. Output (sL, qL).
5. **(none)** keep the default target; FINAL unchanged; output (s, q).

Then record the point (§12). (The non-divided fallbacks keep the previous point's distance from the
right kerb, or from the left kerb for (L1), measured on the previous point's road. The divided tests
of (R1)/(L1) are the opposite of (R)/(L): MM2's, reproduce.)

## 15. Notes on the corridor steps

- The scan loops clamp the forward component to at least 1 m (except §13 with σ = 1; the initial
  windows only for *k* = 1), so points just beside or behind P bend the window strongly instead of
  flipping it.
- The final-road scan outputs min(qL, qR) even when it aims at one apex; the route scan uses the
  apex's own progress.
- The asymmetries (no +1 inset in §13 step 4 σ = 1, the sign of the second *k* = 1 shift, the kerb
  used in §14 (L), the divided tests of (R1)/(L1), the frame-section difference between §13 and §14,
  the φ index reused on another road) are MM2's and must be kept.

## 16. Obstacle scan: the nearest obstacle blocking a leg

Input: leg start A and end B, start (slot sA, progress qA), end (slot sB, progress qB), margin M;
width Wd = Lx + Rx. (From §7: A = P(*k* − 1), B = P(*k*), qB = prog(*k*) + 3, M = (B + F) + (B + F).)
Obstacles are tested where they are now (current position and orientation); their speeds are not
used and nothing is predicted. Output: the obstacle with the smallest **block distance** *bd* (§17)
and its class: 0 player, 1 traffic car, 2 opponent, 5 prop. A candidate replaces the best only when *bd* > −1 and *bd* < best
(both strict; best starts at 99999.0), so among equal distances the first tested wins.

1. If *S*_sB exists and qB > *N*(*S*_sB): sB := sB + 1, qB := 1.
2. For s = sA … sB: skip a slot with no road (the start progress then carries over to the next road).
   On a road (*N* sections, σ = σ_s), for q from the start progress (qA on the first road visited, 0
   afterwards) up to *N* − 1, test these lists in this order:
   1. avoid traffic: q = 0: the traffic cars of the intersection the driver enters the road from
      (end 1's intersection if σ = 1, end 0's if σ = 0); q > 0: side B's traffic list at entry q
      (σ = 1) or *N* − q (σ = 0). Class 1.
   2. q = 0 and avoid props: the props of that same intersection whose `ImpulseLimit2` > 250000.0.
      Class 5.
   3. q > 0 and avoid props: side B's props of section (σ = 1 ? q : *N* − q) with `ImpulseLimit2` >
      250000.0. Class 5.
   4. q > 0 and avoid traffic: side A's traffic list at entry (σ = 1 ? *N* − q : q). Class 1.
   5. q > 0 and avoid props: side A's props of section (σ = 1 ? q : *N* − q), **all** props (no
      `ImpulseLimit2` test). Class 5.

   After these: if (q = qB and s = sB) or anything was found, stop the road scan.

   **Which list entry holds a traffic car** (`traffic-reactions.md`, "Being seen by other AI"): a car
   on side B is filed at entry section(p, +1), a car on side A at entry section(p, −1) = *N* − raw
   section, both limited to 1 … *N* − 1; a car whose room is an intersection room is in that
   intersection's list (read here at q = 0). So the entries read above, *N* − q for side A when
   σ = 1 and q for side B, are the cars level with the driver's progress q. Props are filed by
   `road-network.md` ("Obstacles"). All these lists are singly linked: walk from the first entry
   along the links until an empty link (new entries go to the front). (MM2's loop for traffic cars
   would also stop on coming back to the first entry; with lists filed this way that never
   happens.)
3. avoid players: each player car in player order (not the driver's own car); it counts only if the
   road the player is currently tracked on (below) is *S*₀, *S*₁ or *S*₂; a player that has never
   been tracked on a road never counts. Class 0.

   **Player tracking** (the AI map's bookkeeping per player; `traffic.md`, "Frame order", step 1.1,
   after the player's room step): every frame, the component at the player's body position with
   the room found last frame as the hint (`road-network.md`, "Room lists", "Component at a
   position"); the room found is kept for the next frame. If the component is a **road** (kind 1)
   the tracked road becomes that road (and the tracked section `section(p, +1)`); for an
   intersection, a shortcut or nothing the tracked road **stays as it was**, so a player crossing
   an intersection or driving a shortcut still counts on the last road it was on. At every world
   reset (in the AI reset, before the player's car is put back): the component at the car's stored
   start position (its body start point) with hint 0; a road gives that road (section from the
   car's current body position); an intersection gives the road chosen by `route-search.md`'s
   "Intersection *I*, keep heading" rule with this player's own gear and body Z, section 1;
   nothing leaves the tracked road as it was (none when the player is created).
4. avoid opponents and *w* > 2: each race opponent counts if its own current road (its *S*₀) is one of
   this driver's *S*₀ … *S*₂ and its racer number differs from this driver's number (a cop's number
   is its index in the police list, so cop *i* never sees opponent *i*; MM2's comparison, keep it).
   Class 2.

(§7 ignores an opponent result when *w* ≤ 2 or mark(*k* − 1) = 1, and any result farther than *k*_p.)

## 17. Block distance of one obstacle

Leg A → B, margin M, width Wd; len = hdist(A, B). Result: a forward distance along the leg, or −1.0.

### 17.1 Cars (traffic cars, players, AI cars)

The obstacle's body box: rows X, Z, position *o*; extents front Fo, back Bo, left Lo, right Ro (players:
Fo = Bo = Size.z·0.5 and Lo = Ro = Size.x·0.5 of the car simulation's `Size`; AI cars: their own
extents; traffic cars: the traffic spec's values).

1. d = B − A with d.y := 0; u = unit(d).
2. Corners in this order: c0 = (*o* − Fo·Z) − Lo·X; c1 = Ro·X + (*o* − Fo·Z); c2 = (Bo·Z + *o*) − Lo·X;
   c3 = Ro·X + (Bo·Z + *o*).
3. half = Wd·0.5 + 1.0.
4. For each corner in order: v = c − A; lat = u.x·v.z + v.x·(−u.z); fwd = v.z·u.z + v.x·u.x;
   a = atan2(lat, fwd) rounded. The corner blocks if −half < lat < half, fwd > 0, fwd < len + M and
   −0.69999999 < a < 0.69999999. Return fwd of the **first** blocking corner.
5. No corner blocks: −1.0.

### 17.2 Props

1. d = B − A (all three components); q = (d.z² + d.y²) + d.x²; u = d·(1/√q) (0 if q = 0). (Unlike cars,
   y is kept.)
2. Prop position *o* = ((M.pos − CG.x·M.X) − CG.y·M.Y) − CG.z·M.Z (the prop's current matrix M and its
   `.dgBangerData` CG).
3. radius = `YRadius`, but 2.0 if `YRadius` > 2.0; half = (radius + Wd·0.5) + 1.0.
4. lat = u.x·(*o*.z − A.z) + (*o*.x − A.x)·(−u.z); fwd = (*o*.z − A.z)·u.z + (*o*.x − A.x)·u.x;
   a = atan2(lat, fwd) as float. The same four conditions as 17.1 → fwd, else −1.0.

## 18. Detours around an obstacle

### 18.1 Passing points

`passing_points(o, from, dir, clr)` returns A (left) and B (right).

**Cars**: the corners c0 … c3 of §17.1. For each corner in order: e = normalise(c − from) (3D);
nv = ((−e.z)·clr, e.y·clr, e.x·clr); candidates nv + c, then c − nv (eight in the order c0+, c0−,
c1+, c1−, …). h = normalise(dir); for each candidate: w = cand − from; a = atan2(h.x·w.z +
w.x·(−h.z), w.z·h.z + w.x·h.x) (unrounded). A = the candidate with the smallest a (strictly smaller
than the best so far, compared with the best stored as float, starting at 99999.0), B = the largest
(strictly larger, starting at −99999.0). Ties keep the first.

**Props**: *o* = position(prop); uq = *o* − from; inv = 1/√((uq.z² + uq.y²) + uq.x²) (0 if the sum is
0); ex = inv·uq.x, nz = −(inv·uq.z); r = min(prop radius, 2.0) + clr (the prop type's bounding radius);
B = (nz·r + *o*.x, *o*.y + 0, *o*.z + r·ex), A = (*o*.x − r·nz, *o*.y − 0, *o*.z − r·ex).

### 18.2 Collecting alternatives for record *k* around obstacle O

Up to 10 alternatives (point, obstacle, zone, mark), in order:

1. If mark(*k* − 1) = 1 (the stretch starts on an arc): one alternative (P(*k*), O, zone 1, mark 1).
   Done.
2. from = P(0) and dir = −rowZ if *k* = 1; else from = P(*k* − 1), dir = P(*k* − 1) − P(*k* − 2).
   (A, B) = passing_points(O, from, dir, Rx + 2.0).
3. **Ahead test** of a point Y: ux = Y.x − P(*k* − 1).x, uz = Y.z − P(*k* − 1).z; a = atan2(yv, xv)
   (unrounded); ahead if −1.57 < a < 1.57, where:
   - *k* = 1: yv = uz·rowX.z + ux·rowX.x, xv = (−rowZ.z)·uz + (−rowZ.x)·ux;
   - else, road *S_s* with s = slot(*k* − 1), progress pr = prog(*k* − 1), **standard** frame:
     σ_s = 1: *i* = pr, yv = (−X[*i*].z)·uz + (−X[*i*].x)·ux, xv = (−Z[*i*].z)·uz + (−Z[*i*].x)·ux;
     σ_s = 0: *i* = *N* − pr − 1, yv = uz·X[*i*].z + ux·X[*i*].x, xv = uz·Z[*i*].z + ux·Z[*i*].x;
   - **for B in this step only** MM2 swaps the section choice: σ_s = 1 uses *i* = *N* − pr − 1 with
     the negated axes, σ_s = 0 uses *i* = pr with the plain axes. Reproduce this.
4. If A is ahead: follow(A, O, side −1, depth 0) (§18.3). Then if B is ahead (swapped frame):
   follow(B, O, side +1, depth 0).
5. If nothing was produced: one alternative (P(*k*), O, zone 1, mark 3 "blocked").

### 18.3 follow(Pt, O, side, depth)

1. depth := depth + 1; if depth = 10, stop.
2. (wo, ov) = window_place(O); if wo = −1 or *S*_wo is none, stop.
3. c = onroad(*S*_wo, Pt, Rx); if c ≠ 1: stop, unless walking is allowed and c = 2.
4. Obstacle scan (§16) from P(*k* − 1) to Pt with start (slot(*k* − 1), prog(*k* − 1)), end (wo,
   ov + 2), margin (B + F) + (B + F) → O2, c2.
5. If none, or c2 = 2: append (Pt, O, zone c, mark 2); stop.
6. (A2, B2) = passing_points(O2, from, dir, Rx + 2.0) with from/dir as in §18.2 step 2; pO =
   position(O), pO2 = position(O2).
7. If (pO.x − pO2.x)² + (pO.z − pO2.z)² > 225.0 and O2 is nearer the car than O ((pO2.x − *p*.x)² +
   (pO2.z − *p*.z)² < (pO.x − *p*.x)² + (pO.z − *p*.z)²):
   - side = −1: if B2 is ahead (standard frame), (P(*k* − 1).x − pO2.x)² + (P(*k* − 1).z − pO2.z)² <
     *k*_p·*k*_p, and c2 = 1: append (B2, O2, zone c, mark 2);
   - side ≠ −1: the same test with A2; append (A2, O2, zone c, mark 2).
8. Then: side = −1 → if A2 is ahead: follow(A2, O2, −1, depth); side ≠ −1 → if B2 is ahead:
   follow(B2, O2, +1, depth). (Same *k*, P(*k* − 1), slot and progress throughout.)

"Ahead" in this section always uses the **standard road frame** of §18.2 step 3 (road *S_s* at
prog(*k* − 1)), also when *k* = 1 (never the car's axes, never the swapped variant).

### 18.4 use_alternative(*k*, *i*, c)

For alternative (Pt, Oi, zone z, mark m):

1. (wo, ov) = window_place(Oi); if *S*_wo is none: wo := wo − 1 and ov := *N*(*S*_wo) − 1 (new wo).
   Clamp ov to 0 … *N*(*S*_wo) − 1. (MM2 does not handle wo = −1 here; skip such an alternative.)
2. mark(*k*) = m; cls(*k*) = c; obj(*k*) = Oi; zone(*k*) = z; P(*k*) = Pt; P(*k*).y += 1.0; dist and cost
   as FINISH (with angle); prog(*k*) = ov; slot(*k*) = wo.
3. CONTINUE(*k*).

In the two single-alternative cases of §18.2 (marks 1 and 3) Pt is P(*k*), already raised, so the
point ends up 2 m above the plan height; reproduce.

### 18.5 window_place

Given the driver's window (*S*₀ … *S*₂, σ₀ … σ₂), the slot holding the obstacle's road (or −1) and a
section index:

- **AI-driven cars** (cops, opponents): the car's own current road (its *S*₀) and its own section on
  it (`index`, converted with the obstacle car's **own** side: σ = 0 → *N* − index − 1); slot = the
  first window slot holding that road, else −1.
- **Traffic cars**: from the traffic car's **route place** (`traffic-reactions.md`, "Being seen by
  other AI": its rail road, its piece index sx in its own driving order — 1 … *N* − 1 on the lane,
  *N* in its turn piece — and its travel direction, +1 = toward increasing section index). For the first window slot wo holding its road:
  if sx ≠ *N*: v = *N* − sx when (σ_wo = 0 and direction +1) or (σ_wo = 1 and direction ≠ +1), else
  v = sx; result (wo, v). If sx = *N*: σ_wo = 0 → (wo, 0); else *S*_wo+1 none → (wo, *N* − 1); else
  (wo + 1, 0). If its road is not in the window: for wo = 0, 1, 2 with *S*_wo present, if the
  intersection the traffic car drives toward (its road's far-end intersection for its direction) is
  *S*_wo's far-end intersection for σ_wo → (wo + 1, 0) (first such wo). Otherwise (0, 0). Never −1.
- **Props** registered on a road: for wo = 0, 1, 2, the first *S*_wo that is that road → (wo,
  progress(*S*_wo, position, σ_wo)); none → −1. Props registered at an intersection: for wo = 0, 1,
  if *S*_wo's far-end intersection (for σ_wo) is it → (wo + 1, 1); else if *S*₀'s near-end
  intersection is it → (0, 1); else −1.
- Other classes (cable cars, trains) are not met by chasing cops; their rules belong to a later
  spec.

## 19. Reproduction notes

These follow from MM2's behaviour and change results; reproduce them unless stated:

1. Records persist; arc points (placed without angle) keep a stale cost, and every later record of
   that path adds to it. Initial contents are zero from the driver's reset.
2. A junction-turn arc may write several records; only the last gets an obstacle check. Records not
   written (§11.1 step 9, §11.2 when no step qualifies, §13 step 7.5) keep old contents and are still
   checked and committed.
3. REF, FINAL, λ₀ and the shared bend values change during the expansion and are not restored on
   backtracking; evaluate strictly in the order given.
4. Choices where MM2 is undefined: bend search starting beyond the road (*t* = −1); alternatives
   beyond 10 and rows beyond 25 (never seen; stop adding); window_place −1 in §18.4 (skip); lanezone
   of roads with L_B = 0 and curbw with S_B + L_B = 0 (0.0); the side of slot 3 (0, §2.2); a commit
   past the end of the table (§3.2; keep what fits).
5. If no row was chosen the target is working record 1 and the driver sees a path of 0 points.
6. The previous junction tangent length tⱼ_u is read before it is ever computed (§5.2 step 3); MM2
   does not initialise it. Use 0.0.
7. **Sections outside the road.** A record whose progress is *N* or more makes §13, §14 and
   §18.2 index a few sections before 0 or past *N* − 1 of a road: record 0 at slot 2 with progress
   *N*(*S*₂) (§6 step 1), the records §13 and §14 then make from it (§14 outputs progress *N* + 1
   there: step 3 takes q past *N*, so nothing is scanned and step 6 keeps the default target), and
   §13's frame *j′* = −1 for a previous point with progress *N′* and σ′ = 0. MM2 reads the memory
   just outside the road's arrays. Each per-section array (C, X, Z, the centre distances, each
   side's edge block) is a separate block of the game's heap, so index −1 lands on that block's
   allocator header (a heap address with flag bits, and the block size) and the previous block's
   last word, and index *N* on alignment padding (left-over memory, when the array's byte size is
   not a multiple of 8) and the next block's header; where the blocks lie depends on the whole
   allocation history. Only the kerb lines are partly defined: their block continues with the
   outer sidewalk edge, so Ea[*N*] and Eb[*N*] are the sides' outer edge points 0. Not
   reproducible. **Engine:** every per-section read clamps its index to 0 … *N* − 1 (kerb lines
   included); the progress values themselves stay as written. In §13 the out-of-range values are
   never used (they reach only λL, λR, the shifted P of step 4 and the frame *j′* of a scan that
   does not run, because q ends past *N*; step 7 ends in 7.5). In §14 they set the default target
   and λ₀ (step 4) and, for the record after, the scan frame *j′*. Shipped data: the reference
   driver on all 256 created amateur Checkpoint and Circuit opponent rows of both cities, 120 s
   each (1.84 million planning passes), never meets the case.

## Checked against

Machine code of `midtown2.exe` (retail English, Nov 3 2000); the evaluation order of every sum of
three or more terms and every comparison direction (strict or not, NaN side) read from the x87
instructions.

- Constants: 99999.0 (row choice, best bearings), 1.0 (raise), 2.0 (detour clearance and on-road
  margin), 225.0, 1.57 (ahead limit), 10 (follow depth), ±0.1, ±0.5 (arc steps), −1.0, 15.0, ±0.01,
  0.2, 0.7 (turn and bend threshold), 3.14, 12.0, 9999.0 (destination marker, junction limit and
  section search), 1.25, 1.5, 3.0, 10.0, 4.0, 0.001 (window tolerance), ±0.69999999 (obstacle cone),
  250000.0 (`ImpulseLimit2` threshold), 2.0 (prop radius cap), 0.99999988 and 3.1415927 (`angle`),
  −0.01 as a double and 0.0099999998 as a float (tiny-lateral band).
- Path cost is the last record's cumulative turning angle; the target is the chosen row's record 1;
  row −1 coincides with the working path.
- Cop orders as read by the planner: horizon *k*_p 75.0; avoid traffic, props and opponents on,
  players off; path preference off.
- Shipped road data with the §4.1 rules: SF 379 main roads → 9 bends, London 540 → 55, all
  two-section bends; the "counted but not created" case never occurs. SF road frames are left-handed
  at all 2,132 sections (X × Y = −Z), Z points toward lower section numbers, side A's kerb lies on
  the +X side on 377 of 379 roads; 10 of SF's 379 roads are divided.
- Side B band edges, SF road 0 (4 lanes + sidewalk): lanezone 9.999, walkzone 15.003, half width
  15.0026; one-lane roads: lanezone 3.499, walkzone 7.249; 9 SF roads have L_B = 0.
- The no-walk car kind is `vppanozgt`.
- Junction turns: the whole per-turn computation (5.2 and 5.3) sits behind the τ_u ≠ 0 and
  both-roads test. §13: the σ = 1 initial window's bearings use section q of the scanned road; the
  stop rules and §14's B1, B2 and step 6 read the divided flag and *N* of the apex's road. Slot 3:
  the side byte after the three window sides is never written by MM2; the word after the slot-3
  road is the mode pair (0 in DRIVE). The 25 rows follow the working path directly in memory,
  EXPAND's only record limit is *k* > 39, and the turn arc's record advance has none.
- Shipped roads: every road has at least one lane or sidewalk on side B (SF: 9 main roads (0, 1),
  49 shortcuts (1, 0); London: 70 shortcuts (1, 0); all others more).
- No random draws in any planner routine (all callees checked).
