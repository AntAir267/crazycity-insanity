# Object contacts: where the car body touches a placed object

This page says how MM2 finds the contacts between the car's bound and the bound of one gathered
placed object (`collision-objects.md`) in one physics sample, and turns them into the impact list
that `collision.md` ("Collision response") resolves. It covers every object a car meets in a race:
geometry landmarks, terrain landmarks (landmarks with a `.ter` grid), spanning objects, box props,
geometry props and capsule props. All of them are **fixed** here: landmarks and spanning objects
never move, and a prop that has not been hit yet collides as an immovable object (its first hit
may break it: `collision.md`, "Breakable props").

Much of the work reuses `collision-contacts.md` (the car against the city): its terms
(displacement of a body point, line-inside test, records, record flags, numbers) and its impact
steps 3a–3f. This page refers to those by name and gives what differs.

Single precision throughout, operations in the order written (`vehicle.md`, "Precision"); where
MM2 groups a sum differently from the canonical order, the order is listed in "Evaluation orders"
at the end. In a race the skin is 0, so it does not appear below.

## Terms

- **Car matrix** M and **previous car matrix** M₀: the body matrix and the previous matrix of
  `collision-contacts.md` ("Terms"), with the shift rule of "Push source" below instead of the
  city's. Rows X, Y, Z, origin o.
- **Object matrix** O: the object's matrix as placed for collision (`collision-objects.md`,
  "Bounds and placement": a landmark's turn about Y, a spanning object's orthonormalised matrix, a
  prop's matrix with its position at the prop's centre of gravity). The object's previous matrix
  is O itself, bit for bit, so the displacement of any object point is exactly +0 (its terms are
  ±0 and the origin difference, +0, is added last).
- **Transpose inverse** inv(A) of a matrix A (rows X, Y, Z, origin o), which assumes the rows are
  orthonormal and never renormalises: its rows are A's columns, and its origin is
  (−(X·o), −(Y·o), −(Z·o)).
- **Matrix product** A·B (row convention): the matrix that applies A, then B. Its rows are A's
  rows turned by B and its origin is A's origin moved by B. A·inv(B) takes points from A's model
  coordinates to B's model coordinates.
- dt, 1/dt: the physics step time and its inverse (`vehicle.md`). huge: the largest float.
- **Sphere test** of a segment P0 → P1 against the sphere of radius r about the origin (with
  r2 = r·r): d = P1 − P0; s = d·P0. If s < 0: L = |d|²; if −s < L the segment misses when
  L·r2 < |P0|²·L − s·s and hits otherwise; if −s ≥ L, q = P1. If s ≥ 0, q = P0. With q: miss when
  r2 < |q|², hit otherwise.
- **One-sided line-inside test** of a polygon for the line through S with direction D: as the
  line-inside test of `collision-contacts.md` (per edge j: g = (c(j+1) − cⱼ) × D,
  f = (S − cⱼ)·g, h = eⱼ·g), but the line fails only if for some edge f < 0 and h > 0 (both
  strict). (This is the edge rule of the one-sided segment test of `collision-objects.md`.)
- **Polygon-edge record**: a record of the third kind, made in 2.3. The "records of another kind"
  that the rules of `collision-contacts.md` step 3 skip are these.

### Push source and the previous matrix

MM2 remembers, for the car, which side gave it the largest push of the sample (largest by
squared length (p.x² + p.y²) + p.z², compared strictly, so the first of equal pushes counts; a
sample without any non-zero push has no source). Sides are told apart by an **identity**: the city has one; every
fixed object tested without a rigid body of its own (landmarks, spanning objects, untouched
props) is tested through one shared stand-in and so shares **one** identity; a prop that has its
own rigid body has its own identity.

When the car is tested against an object, M₀'s origin is moved by `lastPush`
(`vehicle-body.md`) if the identity the object is tested under equals the source of the car's
largest push in the previous sample. The impacts of an untouched prop are handed over to the
prop's own body before the response (5.10), so a push received from a prop is booked under that
prop's identity, never under the shared one. Consequence: in a test against any fixed object, M₀
is shifted exactly when the car's largest push of the previous sample came from a landmark or a
spanning object — any of them, not necessarily the one being tested. (For the city test the
shift applies when that push came from the city, `collision-contacts.md`.)

## 1. Setting up a test

The car is side **A**, the object side **B**. Every impact this page produces ends with A = the
car, B = the object, and its normal pointing out of the object into the car (the direction that
pushes the car out; `collision.md`, "The impact record").

| Object bound | Contacts |
|---|---|
| geometry landmark, spanning object, box prop, geometry prop | polygonal: 1.1–1.3 and 2 |
| terrain landmark | terrain: 3 |
| capsule prop | capsule: 5 |

The bounds of box and geometry props are in 4.

### 1.1 Separating test (polygonal objects only)

1. dir = O.origin − M.origin, per component (not normalised).
2. lA = dir in car coordinates (X·dir, Y·dir, Z·dir with M's rows). maxV = the largest p·lA over
   the car's bound points p (model coordinates), starting from −huge and replacing only on
   strictly greater. maxA = (M.origin·dir) + maxV.
3. nd = −dir; lB = nd in object coordinates (O's rows). maxW = the largest q·lB over the
   object's bound points q. minB = −((O.origin·nd) + maxW).
4. If maxA < minB (strict), the bodies are apart along dir: no contact, no impacts.
5. Thresholds for the culling below: thrA = minB − (M.origin·dir); thrB = (O.origin·dir) + (−maxA).

### 1.2 The two passes and their relative matrices

Contacts are searched twice, each time with one bound giving its **points and edges** (the
moving bound T) and the other its **polygons** (the polygon bound Q), all in Q's model
coordinates:

| Pass | T | Q | current relative matrix | previous relative matrix | lT, threshold |
|---|---|---|---|---|---|
| car pass | car | object | cur = M·inv(O) | last = M₀·inv(O) | lA, thrA |
| object pass | object | car | cur = O·inv(M) | last = O·inv(M₀) | lB, thrB |

(MM2 has a mode without the previous positions; it is never used for a car against a fixed
object, so every pass below sweeps the points.)

### 1.3 Candidates

For one pass, with r = Q's bound radius (`bnd.md`: half its box diagonal) and the sphere of the
sphere test centred on **Q's model origin** (not its box centre):

1. **Cull**: point k of T is culled when pₖ·lT < thrT (pₖ in T's model coordinates). A culled car
   point lies behind the object's extreme point along dir; a culled object point lies beyond the
   car's extreme point.
2. Cₖ = cur applied to every point pₖ (its current position in Q's coordinates).
3. **Point candidates**, for k from the last point to the first, skipping culled points:
   Sₖ = last applied to pₖ (its previous position in Q's coordinates); the point is a candidate
   if the segment Sₖ → Cₖ passes the sphere test. Each candidate keeps bestT = 2.0 and "no
   polygon". The candidate list is therefore in **descending** point order.
4. **Edge candidates**, for each edge e = (a, b) of T's edge list from the last to the first: if
   at least one of a, b is not culled and the segment C_a → C_b passes the sphere test, the edge
   is a candidate (tMin = 2.0, tMax = −1.0, no polygons), and each culled end of it is marked as
   needing a plane distance. Such an end stays culled: it does not make later edges eligible and
   gets no point candidate. The candidate list is in descending edge order.

## 2. Polygonal objects

### 2.1 One pass

For each polygon i of Q **from the last to the first** (first corner c0, normal n, both in Q's
model coordinates; plane() as in `collision-contacts.md`):

1. dₖ = plane(Cₖ) for every point that is not culled or needs a plane distance.
2. **Point candidates.** For candidate point k with dₖ < 0 (strict): d₀ = plane(Sₖ). If d₀ > 0:
   t = d₀ / (d₀ − dₖ); if t < bestT (strict) and the line through Sₖ towards Cₖ passes the
   **one-sided** line-inside test of polygon i: bestT = t, polygon = i, depth = −dₖ. (If d₀ ≤ 0
   nothing happens.)
3. **Edge candidates**, with no precondition on the polygon: exactly the edge rules of
   `collision-contacts.md` step 2.3 (different sign bits of d_a and d_b; t = d_a / (d_a − d_b);
   t > tMax or t < tMin, both strict; the two-sided line-inside test for the line through C_a
   towards C_b; the tMin and tMax updates with dMin, dMax). If d_a and d_b are zeros of different
   sign, t is NaN; every comparison of a NaN t in this step counts as true for < and ≤ and false
   for > and ≥, so it passes the range test and can only become tMin (and a NaN tMin is replaced by
   the next crossing that passes), as in the city test.

Since polygons are taken from the last, on equal t the **highest-numbered** polygon wins (the city
test: the lowest). The order of candidates inside one polygon does not change the result.

Then write the pass's records, first the points in candidate order (descending point index) for
every point that found a polygon, then the edges in candidate order (descending edge index), each
with its tMin record and, if polygonMax differs from polygonMin, its tMax record. The fields are
those of the corner and edge records of `collision-contacts.md` step 2, with:

- start, end, point and normal in **Q's model coordinates** (start = Sₖ, end = Cₖ for a corner;
  start = C_a, end = C_b for an edge; point = (end − start)·T + start per component);
- polygon = Q's polygon, its material index, and Q as the polygon's owner; T as the owner of the
  number, "from" and "to";
- the world edge normal of an edge record = T's edge normal (`bnd.md` item 5; for a box prop,
  4.1) turned by **T's own world matrix** (M in the car pass, O in the object pass).

The **car list** holds the car pass's records, the **object list** the object pass's. MM2 gives
each list room for 100 records and does not check it; a car never comes near it, so an
implementation only needs lists large enough for (points + 2·edges) plus the polygon-edge
records of 2.3.

### 2.2 The order of the work

1. Car pass, then object pass (2.1). If both lists are empty, there is no contact.
2. Polygon-edge records (2.3).
3. Every record to world coordinates (2.4).
4. Impacts (2.5).

### 2.3 Polygon-edge records

These find an edge of a polygon of one body that is buried in the other body without crossing
any face of it. Two rounds:

| Round | Owner X of the polygons | Records looked at (they hit X's polygons) | Inside/outside evidence | New records go to |
|---|---|---|---|---|
| 1 | the car | the object list | the car list | the car list |
| 2 | the object | the car list (its original records only, without those added in round 1) | the object list | the object list |

For each looked-at record, in list order, whose polygon is not the polygon of an earlier
looked-at record (each of X's polygons once), with corners v[0] … v[n−1] of that polygon
(n = 3 or 4):

1. For each corner k, the corner is **inside** when the evidence list (its records as written by
   2.1, before any normalisation) names v[k] as an inside end of some record and never as an
   outside end. The inside end of a record is its "to" if front = yes, its "from" if front = no;
   the outside end is the other one. (A corner record has front = yes and "to" = its point.)
2. Choose one edge (i, j) with f = the inside flags:
   - f[0] and f[1]: (0, 1);
   - f[0], not f[1]: (n−1, 0) if f[n−1], else none;
   - not f[0], f[1]: (1, 2) if f[2] and (n = 3 or not f[3]), else none;
   - neither: (2, 3) if n = 4 and f[2] and f[3], else none.
3. If an edge was chosen and the target list holds fewer than 100 records (counting those added
   so far), append a **polygon-edge record**: number = X's edge number of the polygon's edge slot i
   (`bnd.md` item 2; for a box, 4.1), from = v[i], to = v[j], start and end = X's points v[i] and
   v[j] in X's model coordinates, edge normal = X's edge normal of that number (model
   coordinates), owner = X. Its T is 0, point 0, normal (1, 0, 0), it has no polygon and its
   material is "none"; its depth and front are not used anywhere.

Polygon-edge records are never made against a terrain landmark (section 3).

### 2.4 Into world coordinates

The car list's records of 2.1 are in the object's coordinates and go to world by O; the object
list's records of 2.1 go to world by M: start, end and point as points, the polygon normal turned
by the rows. The world edge normals are already world. A polygon-edge record's start and end go
to world by its owner's matrix (car: M, object: O) and its edge normal is turned by the same rows.

### 2.5 Impacts

This is `collision-contacts.md` step 3 run on two lists, with these steps and differences.

1. fast = 1.5·dt.
2. **Normalise** (3a) the car list, then the object list, each on its own ("the previous record"
   is the previous record of the same list). A polygon-edge record is only marked *grouped*: it is
   not turned round and is not itself checked by the 1.03 rule. It still counts as "the previous
   record" for the record after it (whatever that record's kind): if that record has the same
   number and its "from" equals the polygon-edge record's "to", the 1.03 rule is applied to the
   pair with the polygon-edge record's T = 0 (so both get *pair*). Polygon-edge records are
   appended at the end of their list, so the record after one is always another polygon-edge
   record, which skips the check; in practice this never fires. Every record also gets the partner state
   used in step 7: partner = none, and its metric m starts at huge.
3. **Corner groups** (3b): first every group of the car list, in its record order, then every
   group of the object list. Everything 3b does over "the whole list" (membership, blocking,
   suppression, the second best) stays inside the group's own list, and polygon-edge records take
   no part in any of it: they never start a group (they are already *grouped*), are never members
   of one, never block one and are never suppressed by it. Differences:
   - In the car list a record that is *pair* still starts a group (only *grouped* is checked); in
     the object list, as in the city, it does not.
   - The velocity: let v be the velocity of the displacement of the car (M₀ → M) at the end point
     of the first record of the list (displacement·(1/dt) per component). Car-list groups use
     vel = (+0) − v per component (the object's own velocity minus the car's: −v as in the city,
     except that a zero component comes out +0); object-list groups use vel = v − (+0) = v.
   - The impact: car list, kind "point of the car against a face of the object", normal = +N (the
     object polygon's world normal); object list, kind "point of the object against a face of the
     car", normal = −N (N = the car polygon's world normal). Depth and position as in 3b.
   - Each grouped record keeps its metric m (3b, step 2) for step 7.
4. **Pair merge** (3c) on the car list, then on the object list; besides sharing *suppressed*,
   both records of a merged pair get the smaller of their two metrics.
5. **Which edge pass.** e1 = the number of edge records in the car list (corner and polygon-edge
   records not counted); e2 = the number of records in the object list that are not corner
   records (**polygon-edge records counted**).
   - e1 > 0 and e2 > 0: the two-list edge pass (step 7) and no single-list pass;
   - e1 > 0, e2 = 0: the single-list pass (step 6) over the car list, owner = the object;
   - e1 = 0, e2 > 0: the single-list pass over the object list, owner = the car;
   - both 0: no edge pass.
6. **Single-list pass**: 3d over that list, with these changes.
   - vel = (displacement of the record's own body at P's point − displacement of the owner at
     P's point)·(1/dt), the subtraction first. With a fixed object (displacement exactly +0) this
     is exactly the car's velocity v at the point for the car list, and ((+0) − d)·(1/dt) for the
     object list, d the car's displacement at the point: −v, except that a zero component comes
     out +0.
   - The edge-edge test runs in the owner's model coordinates: vel and dir are turned and P's
     point is moved into them by inv(owner's matrix); the polygon is the owner's polygon of the
     record, with its normal and corners in the owner's model coordinates. For steps 1–4 of the
     test everything is in those coordinates; then the two corners of the chosen polygon edge go
     to world by the owner's matrix, and steps 5–6 run in world coordinates with P's world start,
     end and world edge normal and the edge cosine of P's own bound (a box prop's: 4.1).
   - The test also returns the owner's **edge number** of the chosen polygon edge (the polygon's
     edge slot that starts at corner J−1).
   - The impact (edge-edge): car list, normal = +(test normal); object list, normal = −(test
     normal). A = the car, B = the object.
   - **Second record of a pair**: its impact is written only if its test is ok and its edge
     number differs from the first test's (a first test that was not ok has no edge number, so then
     the second is written). The first record of the pair, not the second, is marked *done* when
     either test is ok, as in the city.
   - A pairing hazard: "paired" compares numbers only, so an edge record directly followed by a
     polygon-edge record with the same number would count as a pair, and MM2 would then run the
     edge-edge test on the polygon-edge record, which has no polygon (it reads through a missing
     polygon; not observed in play). Here an edge record is never paired with a polygon-edge
     record in the single-list pass.
7. **Two-list edge pass.** Every record has partner state: partner (index into the other list,
   none at first), metric m (from steps 3–4, else huge), and, once it has a partner: partner
   normal, partner depth, partner position, partner offset s and an *angle ok* flag.
   Go through the object list (index j; record Q) and, for each Q, through the car list (index i;
   record P). In both lists a record counts as **paired** when it is not the last and the next
   record of its list has the same number; a paired record advances its loop by 2, others by 1;
   corner records are skipped (advance 1). For each Q: dQ = Q.end − Q.start, LQ = |dQ|². For each
   P, go on only if Q is not a polygon-edge record and Q and P are not both *suppressed* (P may be
   a polygon-edge record):
   1. dP = P.end − P.start. Closest points of the two edge lines: step 5 of the edge-edge test of
      `collision-contacts.md` with P in the place of the polygon edge (A = P.start, B = P.end,
      pe = dP) and Q in the place of the car edge (S = Q.start, E = Q.end, ce = dQ). Skip this P if
      not ok. This gives nrm (pointing from Q's line to P's line), depth, ptP on P's line and ptQ
      on Q's line.
   2. *angle ok* = yes if depth = 0; otherwise (nrm·enQ ≤ −((cosQ − 0.258819043636) − 0.01)) and
      (((cosP − 0.258819043636) − 0.01) ≤ nrm·enP), with en = the record's world edge normal and
      cos = the edge cosine of the record's edge in its own bound (a polygon-edge record: its
      owner's). (Q's edge must face against nrm, P's edge along it; nrm points from Q's line to
      P's line.)
   3. d2 = depth²; c = dQ × dP; nearly parallel = (d2·LQ > |c|²) or (|dP|²·d2 > |c|²).
   4. Time to contact ttc: 0 if depth = 0; else rv = (displacement of the car at ptP −
      displacement of the object at ptQ)·(1/dt), rn = nrm·rv, ttc = huge if rn ≤ 0, else
      depth/rn.
   5. pos = (ptP + ptQ)·0.5.
   6. If ttc < fast (strict) — a **fast** candidate — clear *face* on P, on the record after P if
      P is paired, on Q, and on the record after Q if Q is paired. Otherwise clear *face* on each of
      those whose depth > depth·0.5 (the candidate's depth).
   7. Offer the candidate (step 8) to P with partner j and other point ptQ; if P is paired, to the
      record after P likewise; to Q with partner i and other point ptP; if Q is paired, to the
      record after Q likewise.
8. **Offer** of a candidate to a record R (with partner index, other point ptO, and the
   candidate's fast, nearly parallel, nrm, depth, d2, angle ok, ttc, pos):
   - ignored if R is *suppressed* or a polygon-edge record;
   - s = (ptO − R.point)·R.normal (R.normal = the normal of the polygon R's edge crossed);
   - a fast candidate is accepted if R has no partner, or s > R's partner offset, or R's m > fast,
     or R's *angle ok* is clear;
   - a slow candidate is rejected if nearly parallel, or R.depth·1.2 < depth, or R's m ≤ fast;
     otherwise, if R has no partner it is accepted when s² + d2 < R.depth², and if R has one when
     s² + d2·4 < (pdepth²·4 + ps²) with pdepth, ps = R's partner depth and offset (all strict;
     R.depth is R's own crossing depth);
   - accepting sets R's m = ttc, partner normal = nrm, *angle ok*, partner depth = depth, partner
     position = pos, partner offset = s, partner = the given index.
9. **Emission.** First the car list, i = 0 upwards: a record P with a partner k, *angle ok* set,
   and the object list's record k not *suppressed*, unless the previous car-list record has the
   same number and the same partner k: mark P *done* and write an impact (edge-edge, A = the car,
   B = the object, position = P's partner position, normal = −(P's partner normal), depth = P's
   partner depth). Then the object list, j = 0 upwards: a record Q with a partner k, *angle ok*
   set, the car list's record k not *suppressed*, and not (the previous object-list record has the
   same number and the same partner k): mark Q *done*; let jj = j − 1 if the previous object-list
   record has the same number as Q, else j; write the impact (as above from Q's partner data) only
   if car-list record k's partner is not jj and, when record k + 1 exists and has the same number
   as record k, record k + 1's partner is not jj.
10. **Face impacts** (3e): the car list, then the object list, for records marked *face* and
    neither *suppressed* nor *done*, with the same skip rule. Car list: normal = +(record normal);
    object list: normal = −(record normal); both with position = end + (record normal)·(depth·0.5)
    and A = the car, B = the object.
11. **Materials.** The car side names the car's bound material, the object side the object
    bound's **first** material (a prop's bound has only one), for every impact, whatever polygon
    was hit (`collision.md`, "Material of a contact").
12. Impacts are written in the order of the steps (car-list groups, object-list groups, edge
    pass, car-list faces, object-list faces) and resolved in that order with weight
    1/(number written). At most 200; never reached by a car.

## 3. Terrain landmarks

A landmark with a terrain bound is tested only with the car's points and edges against the
landmark's polygons, found through the grid; there is no separating test, no culling and no
sphere test. (The pass with the landmark's own features would run only for a `.ter` file whose
"hot edges" flag is set; none is.) The records form one list, the car list; the object list stays
empty.

### 3.1 Segments

cur = M·inv(O), last = M₀·inv(O). Cₖ = cur applied to every car point. Then, in this order,
each segment is tested against the grid (3.2, 3.3) and its records appended:

1. **Corner segments**, k = 0, 1, … (ascending): start = last applied to pₖ, end = Cₖ. Then the
   start is rebuilt by MM2's skin rule, which with skin 0 reads: if start ≠ end,
   start = (start − end) + end per component (this can move a component by one unit in the last
   place when it changes sign or the two differ much in size); if start = end it stays.
2. **Edge segments**, e = 0, 1, … (ascending) over the car's edge list (a, b): start = C_a,
   end = C_b.

The list has 100 places. A corner segment is tested only while at least 1 place is free, an edge
segment only while at least 2 are free (once a single place is left, every later edge is
skipped).

### 3.2 Corner segment: the grid probe

Find the cells of the segment (`collision-objects.md`, "Cells of a segment"; walk details in 3.4)
and test the polygons of those cells, cell by cell in walk order and in each cell's list order,
each polygon at most once per segment, with the one-sided segment test of `collision-objects.md`
and the limit = the best T so far (2.0 at first). A hit replaces the best (so on equal T the later
polygon wins). If there is a best hit, write one **corner record**: start and end of the segment,
T, point = (end − start)·T + start, normal = the polygon's normal, polygon and its material index,
depth = −d_E (the test's penetration), front = yes, number = k, from = none, to = k.

### 3.3 Edge segment: the grid edge test

Same cells and polygons, each once, with this **two-sided** segment test: d_S = (S − c0)·n,
d_E = (E − c0)·n; no hit unless their sign bits differ; t = d_S / (d_S − d_E); no hit if
tMin < t < tMax (strictly inside the current range; t equal to either end is tested); no hit if
the line through S towards E fails the (two-sided) line-inside test. A hit has T = t, point =
(E − S)·t + S, normal n, and: if d_S ≤ 0, depth = −d_S and front = no; else depth = −d_E and
front = yes.

Keep tMin = 2.0, tMax = −1.0, hits = 0 and two slots. On every hit: hits += 1; if t ≤ tMin and
(front or t < tMin), slot 0 = this hit and tMin = t; then, if t ≥ tMax and (not front or
t > tMax), slot 1 = this hit and tMax = t. Write min(hits, 2) **edge records** from slot 0, then
slot 1 (one hit fills both slots but writes one record): start = C_a, end = C_b, the slot's T,
point, normal, polygon, material index, depth and front, number = e, from = a, to = b, and world
edge normal = the car's edge normal turned by M's rows. Records start with all flags clear.
A NaN t (d_S and d_E zeros of different sign) is compared as in 2.1 step 3 (true for < and ≤,
false for > and ≥): it is tested, and a hit with it can only fill slot 0.

### 3.4 Walking the grid

Additions to "Cells of a segment" of `collision-objects.md`, for the walk between two different
cells. x0, z0 and x1, z1 are the segment ends' X and Z; min, max the bound's box; fx, fz the cell
factors; W, D the cell counts along X and Z; N = W·D. trunc() truncates towards zero to a 64-bit
integer and keeps its low 32 bits as a signed integer: a value of magnitude 2³¹ or more but below
2⁶³ wraps, and a non-finite value or one of magnitude 2⁶³ or more gives 0 (the same as any value
in (−1, 1)); clamping then works on that result.

1. **Clip.** dx = x1 − x0, dz = z1 − z0, t0 = 0, t1 = 1.
   - X: if dx = 0, nothing if x0 > max.x or x0 < min.x. Otherwise inv = 1/dx,
     ta = (max.x − x0)·inv, tb = (min.x − x0)·inv. If x0 < x1: nothing if ta < 0; if ta ≤ 1,
     t1 = ta; nothing if tb > 1; if tb ≥ 0, t0 = tb. Else: nothing if ta > 1; if ta ≥ 0, t0 = ta;
     nothing if tb < 0; if tb ≤ 1, t1 = tb. Nothing if t0 > t1.
   - Z: if dz = 0, nothing if z0 > max.z or z0 < min.z. Otherwise inv = 1/dz,
     ta = (max.z − z0)·inv, tb = (min.z − z0)·inv. If z0 < z1: nothing if ta < 0; if ta ≤ 1 and
     ta < t1, t1 = ta; nothing if tb > 1; if tb ≥ 0 and tb > t0, t0 = tb. Else: nothing if
     ta > 1; if ta ≥ 0 and ta > t0, t0 = ta; nothing if tb < 0; if tb ≤ 1 and tb < t1, t1 = tb.
     Nothing if t0 > t1.
   - If t0 > 0, t0 = t0 + 0.0001; if t1 < 1, t1 = t1 − 0.0001 (no new check of t0 ≤ t1).
2. **Grid coordinates.** gx0 = ((dx·t0 + x0) − min.x)·fx, gx1 = ((dx·t1 + x0) − min.x)·fx,
   gz0 = ((dz·t0 + z0) − min.z)·fz, gz1 = ((dz·t1 + z0) − min.z)·fz; gdx = gx1 − gx0,
   gdz = gz1 − gz0. ix = trunc(gx0) clamped to [0, W−1], iz = trunc(gz0) clamped to [0, D−1].
   "x rises" means x1 > x0, "z rises" z1 > z0. "Add c" appends cell c if its reference count is
   not 0.
3. **One column**, when trunc(gx0) = trunc(gx1) (unclamped): c = W·iz + ix; stop if c ≥ N. Repeat:
   stop if c < 0; add c; if z rises, iz += 1, c += W, and continue only if iz < gz1; else
   iz −= 1, c −= W, and continue only if gz1 − 1 < iz; stop if c ≥ N.
4. Else **one row**, when trunc(gz0) = trunc(gz1): the same with ix, c ± 1, x rises, and the tests
   ix < gx1 / gx1 − 1 < ix.
5. Else **diagonal**: c = W·iz + ix; stop if c ≥ N; nx = ix + 1 if x rises else ix; nz = iz + 1 if
   z rises else iz. Repeat: stop if c < 0; add c; tx = (nx − gx0)/gdx, tz = (nz − gz0)/gdz; stop
   if tx > 1 and tz > 1; if tx < tz (strict) step in X (ix and nx each +1 if x rises, else −1),
   otherwise step in Z (iz and nz likewise; a tie steps in Z); c = W·iz + ix; stop if c ≥ N.
   ix and iz are not kept inside the grid: only 0 ≤ c < N is checked, so ix = −1 names the last
   cell of the previous row and ix = W the first cell of the next row. Keep this.

The integer-to-float conversions in the comparisons are exact.

### 3.5 Impacts

The records go to world by O (2.4), and the impact steps of 2.5 run with the car list only:
there are no polygon-edge records; e2 = 0, so if the car list has edge records the edge pass is
the single-list pass over the car list with the landmark as owner (polygons, corners and edge
numbers from the landmark's bound and `.ter`); then the car list's face impacts.

**Material** (the one difference from 2.5 step 11): the landmark side names, for every impact of
the test, the bound's material with the material index of the polygon of the **first record of
the list** — the lowest-numbered car point that hit, or if none, the first edge record. The car
side names the car's material.

## 4. Bounds of box and geometry props

### 4.1 Box (`CollisionPrim` 1, or a geometry prop without a `.bnd`)

Centred on the prop's position (its centre of gravity), h = Size·0.5.

- Points (exactly ±0.5 times each Size component): p0 (+,+,+), p1 (−,+,+), p2 (−,−,+),
  p3 (+,−,+), p4 (+,+,−), p5 (−,+,−), p6 (−,−,−), p7 (+,−,−) (signs of x, y, z).
- Edges: e0 (0,1), e1 (1,2), e2 (2,3), e3 (3,0), e4 (4,5), e5 (5,6), e6 (6,7), e7 (7,4),
  e8 (0,4), e9 (1,5), e10 (2,6), e11 (3,7).
- Quads (corners in order; edge number of each slot, slot k = corner k → corner k+1):

  | Quad | Corners | Edge numbers | Faces |
  |---|---|---|---|
  | 0 | 4, 0, 3, 7 | 8, 3, 11, 7 | +X |
  | 1 | 2, 1, 5, 6 | 1, 9, 5, 10 | −X |
  | 2 | 1, 0, 4, 5 | 0, 8, 4, 9 | +Y |
  | 3 | 7, 3, 2, 6 | 11, 2, 10, 6 | −Y |
  | 4 | 3, 0, 1, 2 | 3, 0, 1, 2 | +Z |
  | 5 | 5, 4, 7, 6 | 4, 7, 6, 5 | −Z |

  Material index 0 for all. Polygon normals (order in "Evaluation orders") and inward edge
  normals are computed as for any bound polygon (`bnd.md` items 3–4); the polygon normals point
  outwards.
- Edge normals: a fixed table, **independent of Size**, with s = 0.70710677: e0 (0, s, s),
  e1 (−s, 0, s), e2 (0, −s, s), e3 (s, 0, s), e4 (0, s, −s), e5 (−s, 0, −s), e6 (0, −s, −s),
  e7 (s, 0, −s), e8 (s, s, 0), e9 (−s, s, 0), e10 (−s, −s, 0), e11 (s, −s, 0).
- Edge cosine: **0.785398185 for every edge** (the angle π/4, not cos 45°). So an edge-edge test
  on a box edge needs en·nrm < −((0.785398185 − 0.258819043636) − 0.01) = −0.516579.
- Box min = −h, max = h; radius = |h| (half the diagonal).
- Material: the prop's one material (`collision-objects.md`, "Props").

### 4.2 Geometry prop (`CollisionPrim` 0)

The text `.bnd` is loaded and all its derived data are computed as for any bound (`bnd.md`,
"After loading": edge list, edge numbers, polygon normals, inward edge normals, edge normals and
cosines) from the points **as in the file**. Then every point is moved by −CG (p + (−CG) per
component) and only the box, centre and radius are computed again from the moved points. The
normals and cosines keep the values computed before the move. Polygon material indices are the
file's; the bound's material is the prop's one material.

## 5. Capsule props (`CollisionPrim` 2)

### 5.1 The capsule

Radius r = YRadius, cylinder length L = Size.y, h = L·0.5. Its axis is the prop matrix H's Y row
and its centre is H's origin (the prop's centre of gravity); in the capsule's own coordinates the
cylinder covers y in [−h, h] and the two end spheres are centred at (0, −h, 0) (**part 0**, bottom)
and (0, h, 0) (**part 1**, top); the cylinder side is **part 2**. A prop placed by its turn about Y
has a vertical axis; one with a full matrix uses that matrix's Y row. (The gathering test of
`collision-objects.md` centres its cylinder on the placement point, not on this centre.)

There is no separating test and no culling. The capsule's previous matrix is H itself.

### 5.2 Setup (car coordinates)

1. d = M.origin − H.origin.
2. Capsule centre in car coordinates: c = −(d in car coordinates), i.e. c.x = −(X·d) etc.
3. Axis a = H's Y row in car coordinates; s0 = −a, s1 = +a.
4. End-sphere centres: E0 = c + s0·h (bottom), E1 = c + s1·h (top), per component h·s.k + c.k.
5. **Car motion relative to the capsule** (world): with w = H.origin − M₀.origin,
   l = w in H's coordinates, q = l turned back to world by H's rows: disp = d + q. (This is the
   motion of the car's origin relative to the capsule since the previous sample; the car's
   rotation is ignored. It is not simplified, because H's rows times their transpose is not
   exactly the identity in floats.)
6. v = disp in car coordinates (M's rows); V2 = |v|².
7. count = 0; the contact flags F0 = F1 = no (one per capsule half); "full" when count = 200.

### 5.3 Contact records

While this test runs, a contact record has: depth, normal and position (world), part (0, 1 or 2),
feature (a car point, edge or polygon number) and kind: **edge** (0), **face** (1) or **vertex**
(2). Its normal points out of the car into the capsule until the hand-over (5.10).

**Writing** a contact found in car coordinates (point Q, normal n): position = M applied to Q,
normal = n turned by M's rows (not renormalised).

**Committing** a new contact (written at the end, index count, not yet counted): unless
count = 0, run the duplicate search; **if it finds a record, the new contact is dropped** (it is
not written, no merge rule runs, nothing changes; pass 4 is the exception: there the found record
is deleted and the new contact goes on). Otherwise write it; run a merge rule unless count = 0; if
the merge rejects it, drop it; else set its kind and feature and count += 1. The passes say which
steps they use.

**Duplicate search** for a new contact with part P and feature F: the first committed record i
that matches by the lookup type, or none:

| Lookup type | Used for | Record i matches if |
|---|---|---|
| edge | edge contacts | kind edge, part = P, feature = F |
| vertex | vertex contacts | (kind face, part = P, feature = F) or (kind vertex, part = F, feature = P) |
| face | face contacts | (kind face, part = F, feature = P) or (kind vertex, part = P, feature = F) |

The vertex and face rows compare against the wrong kinds (see "Things that look like mistakes");
keep them.

**Merge rules.** For the new record N: nN = its normal, dN its depth, cN = N.position +
nN·(dN·0.5) (per component nN.k·(dN·0.5) + pos.k), pN its part. For a record I: cI = I.position +
I.normal·(I.depth·0.5). "a is not in front of b along n by tol" means
(a − b)·n < tol (strict; a NaN counts as not in front). Deleting a record moves the later ones
down (N stays last) and does not advance the index.

- **First merge rule**: (1) for i = 0 while i < count: if I's part = pN, and cN is not in front of
  cI along I.normal by I.depth·0.01, and cI is not in front of cN along nN by dN·0.01: if
  I.depth ≥ dN reject N, else delete I; otherwise i += 1. (2) For i = 0 while i < count: if I's part
  = pN and cI is not in front of cN along nN by dN·0.01, delete I; else i += 1. Accept N.
- **Second merge rule** (for contacts on the cylinder side), with hc = H.origin, ax = H's Y row
  (world), sN = (cN − hc)·ax. For each I its normal is flattened onto the plane across the axis:
  t = I.normal·ax; if t is neither 0 nor NaN, u = I.normal − ax·t, and the flattened normal is
  u·(1/|u|) (0 if |u|² is 0 or NaN); else the normal itself. side = ((I.position − hc)·ax)·sN
  (I is on the same half of the capsule as N when side > 0). cI uses the flattened normal.
  (1) For i = 0 while i < count: if side > 0 and cN is not in front of cI along the flattened
  normal by I.depth·0.01, then: if cI is in front of cN along nN by dN·0.01 or more, reject N;
  else if I.depth ≥ dN, reject N; else delete I. Otherwise i += 1. (2) For i = 0 while
  i < count: if side > 0 and cI is not in front of cN along nN by dN·0.01, delete I; else
  i += 1. Accept N. Unlike the first rule, the part is not compared, and step (1) rejects N
  where the first rule would go on to the next record.

### 5.4 Pass 1: car polygons against the end spheres

For each car polygon p in index order (corners V[0] … V[n−1] in the car's model coordinates,
normal N), and for j = 0 then 1 (end centre Pⱼ = Eⱼ, direction sⱼ), stopping when full:

1. Sphere–polygon test (5.9.1) of the sphere at Pⱼ with radius r → result, point Q, normal,
   depth, corner or edge slot k.
2. By result:
   - **vertex**: kept only if (Q − Pⱼ)·sⱼ ≥ 0 (the contact is on the cap's outer half); feature =
     the car point at corner k; lookup type vertex.
   - **edge**: the same test; feature = the polygon's edge number of slot k; lookup type edge.
   - **face**: kept only if N·sⱼ ≤ 0.06 (a NaN passes); feature = p; lookup type face.
   - **none**: the swept rescue, only if N·v > 0 and N·sⱼ < 0.1 (both strict; a NaN fails the
     first and passes the second): one-sided segment test (`collision-objects.md`) of the
     segment Pⱼ + v → Pⱼ against polygon p with limit 2.0 (the cap centre crossed the face
     during the sample). If it hits: result face,
     normal = N, Q0 = Pⱼ − N·r, depth = (V[0] − Q0)·N, Q = Q0 + N·(depth·0.5), feature = p, lookup
     type face. Otherwise nothing.
3. Commit (5.3) with part j, the duplicate search with (j, feature), the first merge rule, kind
   from the result (vertex → vertex, edge → edge, face → face). If committed: Fⱼ = yes.

### 5.5 Pass 2: car edges against the axis

For each car edge e = (a, b) in edge-list order, stopping when full (A, B = the car points):

1. Segment–edge test (5.9.2) of the axis E0 → E1 with radius r against A → B → result, normal
   nrm, depth, axis point X.
2. Only the results **interior** (feature = e, lookup type and kind edge) and **edge end**
   (feature = a or b as the test says, lookup type and kind vertex) go on.
3. k = r − depth·0.5; Q = X − nrm·k.
4. **Flip**: if nrm·v < 0 (a NaN passes) and V2 > r·r (strict): depth = (r + r) − depth,
   Q = Q + nrm·r, nrm = −nrm. (The car moved more than r and came from the other side of the axis.)
5. Commit with part 2, the duplicate search with (2, feature), the **second** merge rule. If
   committed: if |X − E0|² ≤ h·h, F0 = yes, else F1 = yes.

### 5.6 Pass 3: the axis pierces a car polygon

Only if not full.

1. Test the segment E0 → E1 against all car polygons **from the last to the first** with the
   two-sided test and the two slots of 3.3 (as for a grid edge test, but over every polygon); keep
   the polygons of min(hits, 2) slots, slot 0 first.
2. dAx = E1 − E0. For each kept polygon, stopping when full: best = huge, found = no,
   inside = no. For each edge slot k of the polygon: e = its edge number, Va = the car point of
   e's first end in the edge list, De = the point of e's second end − Va.
   1. (s, t) = segment parameters (5.9.3) of (Va, De) and (E0, dAx).
   2. Pe = Va + De·s, Pa = E0 + dAx·t (per component De.k·s + Va.k, dAx.k·t + E0.k);
      g = Pa − Pe; G2 = |g|².
   3. **Near** if G2 ≤ V2 or G2 is NaN (MM2 treats NaN as near): m = −(g·v); take it if
      inside = no or m > best (strict); taking it sets inside = yes. **Far** otherwise: F = F1 if
      t > 0.5, else F0 (a NaN t gives F0); take it only if F = no, inside = no and G2 < best
      (strict), with m = G2.
   4. Taking: best = m, chosen edge = e, nrm = −g, Q = Pe, found = yes.
3. If found: skip it if count > 0 and the duplicate search (edge, part 2, chosen edge) finds a
   record. Else L = √|nrm|², nrm = nrm / L (per component division; L = 0 is not guarded),
   depth = L + r, Q = Q − nrm·(depth·0.5); write it, kind edge, part 2, feature = the chosen edge;
   count += 1 (no merge rule).
4. If this pass made any record, pass 4 is skipped.

(Among the polygon's edges within the sample motion |v| of the axis, the one furthest towards
where the capsule came from wins; otherwise the nearest edge on a capsule half without a contact
yet. The normal pushes the capsule past that edge.)

### 5.7 Pass 4: swept car points

Only if V2 > r·r (strict), not full, and pass 3 made nothing. There is no full check inside.
For each car point k in index order:

1. W = M applied to pₖ (world); S = W − disp.
2. cur = W in capsule coordinates (W − H.origin, dotted with H's rows); prev = the same for S.
3. Capsule probe (5.8) of prev → cur with limit 2.0. If it hits (point pt, normal nh,
   penetration pen, part):
   1. if count > 0 and the duplicate search (vertex, part, k) finds record i: delete record i;
   2. write directly: depth = pen, normal = −(nh turned by H's rows), position = H applied to pt,
      part;
   3. if count > 0: second merge rule if part = 2, else first; if rejected, drop it;
   4. kind vertex, feature k; count += 1.

### 5.8 The capsule probe

A segment S → E in capsule coordinates.

1. If S is inside the capsule, no hit. Inside: with hr = h + r, outside if hr < S.y, or
   −hr > S.y, or ρ² = S.x·S.x + S.z·S.z > r·r, or (S.y > h and r·r < (S.y − h)² + ρ²), or
   (−h > S.y and r·r < (h + S.y)² + ρ²); otherwise inside (the surface counts as inside; a NaN
   S.y, or a NaN right side in either r·r < … test, counts as outside).
2. A = (S.x, h + S.y, S.z) (the start measured from the bottom cap centre); d = E − S.
3. Ray test (below) → number of crossings n, t1, part1. No hit if n = 0, or if t1 > 2.0 (a NaN
   passes; there is no t1 ≥ 0 check).
4. P = S + d·t1 (per component d.k·t1 first, then added); normal = the part normal of P for part1;
   pen = (P − E)·normal. Hit: point P, that normal, T = t1, pen, part1.

**Part normal** of P: part 2: (P.x, 0, P.z); part 1: (P.x, P.y − h, P.z); otherwise
(P.x, h + P.y, P.z); normalised by 1/√ of its squared length (0 if that is 0 or NaN).

**Cylinder test** (the infinite cylinder about Y, radius r, r2 = r·r), from A with direction d:
c = (A.x·A.x + A.z·A.z) − r2; startIn = not (c ≥ 0); endIn = not ((d.z + A.z)² + (d.x + A.x)² ≥
r2). Both in: 0 crossings. b = d.x·A.x + d.z·A.z; a = d.z·d.z + d.x·d.x; D = ((b·b)/a − c)/a; 0
crossings if not D ≥ 0. s = √D, bp = b/a, and U(t) = ((d.y·t + A.y)/L) + 0.5. If startIn:
t1 = s − bp, u1 = U(t1), 1 crossing. Else t1 = (−bp) − s (kept even when rejected); 0 crossings if
not t1 ≥ 0 or t1 > 1; t2 = s − bp; u1 = U(t1); if endIn, 1 crossing; else u2 = U(t2), 2 crossings.
"In the band" means 0 ≤ u ≤ 1. Because A is measured from the bottom cap centre while U is meant
for a start measured from the capsule centre, U(t) = y/L + 1 for a crossing at capsule height y:
the band covers y in [−L, 0] instead of [−h, h] (MM2's bug; keep it).

**Cap test** (a sphere of radius r at the origin of the given start point P, one half only;
"lower" for the bottom cap, "upper" for the top), writing results into two given places tA, tB:
b = (P.y·d.y + d.z·P.z) + P.x·d.x; c0 = (P.x·P.x + P.y·P.y) + P.z·P.z;
a = (d.x·d.x + d.y·d.y) + d.z·d.z; D = ((b·b)/a − (c0 − r2))/a; if not D ≥ 0: 0, nothing written.
A crossing at t is on the right half when ((t·d.y) + P.y)·g > 0 (g = +1 upper, −1 lower; the
equator belongs to neither). startIn = not (c0 > r2); endIn = not (|P + d|² > r2) (summed z, x,
y).
- startIn: if endIn, 0. Else tA = √D − b/a; result 1 if tA is on the right half, else 0.
- Otherwise: tn = (−b/a) − √D, written to tA at once; 0 if not tn ≥ 0 or tn > 1; tf = √D − b/a.
  If tn is on the right half: tB = tf, and the result is 1 if tf > 1 or tf is not on the right
  half, else 2. Else tA = tf; 0 if tf > 1; else 1 if tf is on the right half, else 0.

**Ray test.** Bottom(x, y) = cap test, lower, from A; Top(x, y) = cap test, upper, from
A′ = (A.x, A.y − L, A.z) (the start measured from the top cap centre); "scratch" is a throw-away
place. "Below" means not u ≥ 0, "above" u > 1. Sorting swaps (t1, t2), (u1, u2) and (part1, part2)
when t1 > t2. Run the cylinder test (crossings n, t1, t2, u1, u2), then:

- n = 0, or n = 1 with u1 outside the band: k = Bottom(t1, t2). k = 2: parts 0, 0, result 2.
  k = 1: part1 = 0; if Top(t2, scratch) > 0: part2 = 1, sort, result 2; else result 1. k = 0:
  k = Top(t1, t2); if k > 0 both parts are 1; result k.
- n = 1 with u1 in the band: part1 = 2; if Bottom(t2, scratch) > 0: part2 = 0, sort, result 2;
  else if Top(t2, scratch) > 0: part2 = 1, sort, result 2; else result 1.
- n = 2, the first case that applies:
  1. both u in the band: parts 2, 2, result 2 (no sort);
  2. both below: k = Bottom(t1, t2); if k > 0 both parts 0; result k;
  3. both above: k = Top(t1, t2); if k > 0 both parts 1; result k;
  4. one below and the other above: if Bottom(t1, scratch) = 0: call Top(t1, scratch) and ignore
     its result; part1 = 1; result 1. Else part1 = 0; if Top(t2, scratch) > 0: part2 = 1, sort,
     result 2; else result 1;
  5. otherwise (exactly one in the band), the cap results are ignored: if u1 is in the band:
     part1 = 2, then if u2 is below, Bottom(t2, scratch) and part2 = 0, else Top(t2, scratch) and
     part2 = 1; sort; result 2. If u1 is not: if below, Bottom(t1, scratch) and part1 = 0, else
     Top(t1, scratch) and part1 = 1; part2 = 2; sort; result 2.

Ignored cap tests still write their places as described, so the reported t1 can be a value left
by a cap test that found nothing; reproduce the writes exactly. The probe reports t1 and part1
after all this.

### 5.9 Geometric helpers

#### 5.9.1 Sphere–polygon test

Sphere centre P, radius r; polygon corners V[0] … V[n−1], normal N. Results: vertex, edge, face
or none.

1. d0 = (P − V[0])·N; none if |d0| > r (a NaN goes on).
2. For each edge i = 0 … n−1 (to corner j = i+1, wrapping): a = P − V[i], e = V[j] − V[i],
   m = N × e, test = m·a.
   - test ≥ 0: edge i is "inside" (P is on the polygon's side of it).
   - Otherwise (also for a NaN test) edge i is "outside": if a·e < 0 or NaN, mark it "before"
     and go on; b = P − V[j]; if b·e > 0, mark it "after" and go on. Otherwise this edge
     decides: la2 = |a|², lb2 = |b|², le2 = |e|², Δ = la2 − lb2, Σ = lb2 + la2,
     h2 = (((Σ + Σ) − le2) − (Δ·Δ)/le2)·0.25 (the squared distance to the edge line). If
     h2 ≤ r·r or NaN: depth = r − √h2; t2 = (la2 − h2)/le2; t = 0 if t2 ≤ 0 or NaN, else √t2;
     Q = V[i] + e·t (per component e.k·t + V[i].k); nrm = P − Q, scaled by 1/√|nrm|² (0 if that
     is 0 or NaN); Q = Q − nrm·(depth·0.5); result edge, slot i. Else none.
3. If every edge was inside, or every edge was outside (each "before" or "after"): face case:
   d = (P − V[0])·N (summed x, z, y); if |d| ≤ r or NaN: normal N, depth = r − d (d signed),
   Q = P − N·((d + r)·0.5), result face. Else none.
4. Otherwise the vertex case: the corner k = i+1 (wrapping) for the first edge i marked "after"
   whose next edge is marked "before"; if there is none, k = i+1 of the last edge marked "after";
   if no edge is marked "after", k = 0. nrm = P − V[k]; if |nrm|² > r·r, none. Else L = √|nrm|²,
   nrm = nrm / L (division), depth = r − L, Q = V[k] − nrm·((r − L)·0.5); result vertex, corner k.

The normal points from the polygon towards P (out of the car into the capsule) and Q is the
middle of the overlap.

#### 5.9.2 Segment–edge test

Axis E0 → E1 with radius r, car edge A → B. Results: interior, edge end (A or B), capsule end, none.

1. d1 = B − A, d2 = E1 − E0.
2. Line distance: c = d2 × d1, C2 = |c|², thr = (|d2|²·|d1|²)·0.0012217 (the float
   0.001221699989). If C2 < thr or NaN (nearly parallel): w = E0 − A, u = w × d2, n = u × d2,
   scaled by 1/√|n|² (0 if that is 0 or NaN); dist = −(n·w) (not made positive). Otherwise
   n = c / √C2 (division), w = A − E0, dist = w·n; if dist < 0 or NaN, dist = −dist and n = −n.
   n points from the axis line towards the edge line.
3. None if dist > r.
4. (s, t) = segment parameters (5.9.3) of (A, d1) and (E0, d2).
5. If the parameters came out **interior**: nrm = −n, depth = r − dist, X = E0 + d2·t (per
   component d2.k·t + E0.k); result interior.
6. Else if t ≤ 0, t ≥ 1 or t is NaN: result capsule end (not used).
7. Else: the end is A if s < 0.5 or NaN, else B; X = E0 + d2·t; nrm = end − X; none if
   |nrm|² > r·r; L = √|nrm|², depth = r − L, nrm = nrm / (−L) (division); result edge end.

#### 5.9.3 Segment parameters

For the segments P0 + s·D0 and Q0 + t·D1 (s on the first, t on the second):

1. Lines: w = P0 − Q0; i2 = 1/|D1|²; q = (w·D1)·i2; wp = w − D1·q; c = (D1·D0)·i2; up = D0 − D1·c;
   s = −((wp·up)/|up|²). Likewise w′ = Q0 − P0; i1 = 1/|D0|²; q′ = (w′·D0)·i1; wp′ = w′ − D0·q′;
   c′ = (D1·D0)·i1; up′ = D1 − D0·c′; t = −((wp′·up′)/|up′|²). If 0 < s < 1 and 0 < t < 1 (all
   strict; NaN fails), the result is (s, t), **interior**.
2. Otherwise P1 = P0 + D0, Q1 = Q0 + D1, and with
   - proj(X0, D, Y): w = X0 − Y, k = −(w·D); 0 if k ≤ 0 or NaN; rem = |D|² − k; 1 if rem ≤ 0 or
     NaN; else k/(rem + k);
   - dist(X0, D, Y): w = X0 − Y, u = w × D, n = u × D scaled by 1/√|n|² (0 if that is 0 or NaN);
     −(n·w) (the distance from Y to the infinite line);

   a = proj(Q0, D1, P0), b = proj(Q0, D1, P1), sQ0 = proj(P0, D0, Q0), sQ1 = proj(P0, D0, Q1),
   dQ0 = dist(P0, D0, Q0), dQ1 = dist(P0, D0, Q1), dP0 = dist(Q0, D1, P0), dP1 = dist(Q0, D1, P1).
   "mid" means neither exactly 0 nor exactly 1 (a NaN a or b counts as 0); each ≤ in the table
   also holds when a side is NaN. The result (s, t):

   | a | b | (s, t) |
   |---|---|---|
   | 0 | 0 | (sQ0, 0) |
   | 0 | 1 | dQ1 ≤ dQ0 ? (sQ1, 1) : (sQ0, 0) |
   | 0 | mid | dP1 ≤ dQ0 ? (1, b) : (sQ0, 0) |
   | 1 | 0 | dQ1 ≤ dQ0 ? (sQ1, 1) : (sQ0, 0) |
   | 1 | 1 | (sQ1, 1) |
   | 1 | mid | dP1 ≤ dQ1 ? (1, b) : (sQ1, 1) |
   | mid | 0 | dP0 ≤ dQ0 ? (0, a) : (sQ0, 0) |
   | mid | 1 | dP0 ≤ dQ1 ? (0, a) : (sQ1, 1) |
   | mid | mid | dP1 ≤ dP0 ? (1, b) : (0, a) |

   (A rule of thumb, not an exact closest-points solution.)

### 5.10 End of the test and hand-over

1. If count > 1, the **motion filter** with disp (world): D2 = |disp|², thr = D2·0.01, and for each
   record dotᵢ = normalᵢ·disp. Find the first record with dotᵢ² > thr (strict); if there is none,
   keep all. Otherwise delete every record before it, then for i = 1 while i < count: keep
   record i (i += 1) if dotᵢ² > thr, else delete it (a NaN dotᵢ² is deleted). (When some normal
   is not nearly across the motion, the records whose normal is nearly across it are dropped.
   disp = 0 drops nothing.)
2. **Hand-over.** The untouched prop gets its own rigid body, and every record becomes an impact
   with A = the car, B = the prop's body and the normal **negated** (now out of the prop into the
   car). Kind, part and feature are left as they were (they do not affect the response).
3. The impacts are resolved in order with weight 1/count by the breakable-prop rule of
   `collision.md`. Materials: the car's bound material and the prop's one material.

At most 200 records; passes 1–3 stop when full, pass 4 does not check (a car cannot reach it).

## Not covered here (later milestones)

- Props that have been hit and move (their tests against the city, other objects and cars),
  including a prop that already has a rigid body when the car is tested against it (its impacts
  are swapped completely instead of handed over).
- Car against car (both sides move; AI cars use box bounds): `car-collision.md`.
- Sphere bounds and capsule against capsule (no prop type uses them against a car).
- A `.ter` file with the hot-edges flag set, and MM2's mode without previous positions.

## Things that look like mistakes (keep them)

- All fixed objects share one identity for the previous-matrix shift: a push from any landmark or
  spanning object shifts M₀ for the tests against every fixed object in the next sample, while
  pushes from props never do.
- The culling sphere is centred on the bound's model origin, not its box centre, with the
  half-diagonal radius; points of bounds that reach far from their origin (most car bounds by a
  little, trucks by metres, some landmarks by much more) can be culled although close.
- Polygon ties go to the highest-numbered polygon (the city: the lowest); point sweeps use the
  one-sided inside test (the city: two-sided).
- Group velocities are taken at the end point of the first record of the list, not at the group's
  point (as in the city).
- Car-list records marked *pair* still start corner groups; object-list ones do not.
- e2 counts polygon-edge records. If the object list holds only corner and polygon-edge records
  while the car list has edge records, the two-list pass runs, finds nothing (polygon-edge
  records never act as Q), and the car's edges get no edge-edge impact at all.
- In the two-list pass polygon-edge records act as P but never accept a partner; a P that chose Q
  with *angle ok* clear stops both P's and Q's impact. The two-list angle test uses ≤, the
  single-list one <.
- Box props: edge normals ignore Size and every edge cosine is π/4.
- Terrain landmarks: the start of a corner segment is rebuilt as (start − end) + end; one material
  (from the first record) for all impacts; grid column indices wrap into the neighbouring row; the
  edge test does not reject t equal to the current tMin or tMax.
- Capsule: the duplicate search compares vertex and face contacts against the wrong kinds, so real
  duplicates of those are not found and unrelated records can match (in pass 4 such a match is
  deleted); car point, edge and polygon numbers are small, so this happens.
- Capsule probe: the cylinder band is shifted down by h, so side crossings in the upper half of a
  short prop are usually missed and phantom side hits appear below the bottom cap centre; some cap
  results are ignored and can leave a stale t1.
- Capsule: the car's rotation is ignored in disp; the second merge rule rejects where the first
  goes on; pass 3 does not guard L = 0.
- At the hand-over the normal is negated but kind, part and feature are not swapped.

## Evaluation orders

Products commute, so only the order of additions matters. For a matrix with rows X, Y, Z and
origin o, "to world, zxy" for an output component c means ((Z.c·p.z + X.c·p.x) + Y.c·p.y) (+ o.c
for a point); "into, yzx" for component c means (A.y·w.y + A.z·w.z) + A.x·w.x with A the row of
c. Three orders are for the components x/y/z.

- Separating test: directions into car or object coordinates: into, yzx/yzx/yzx; p·l: zyx;
  origin·dir: yzx.
- Cull test p·l: zyx. Transpose inverse: the origin component for each row A is
  −((A.x·o.x + A.y·o.y) + A.z·o.z).
- Matrix product A·B, element (row, column) = sum over k of A[row][k]·B[k][column], terms in the
  order of k given (origin row: then + B's origin). This is MM2's product that writes into a new
  matrix (used here for the relative matrices, and for the xref composition of
  `collision-objects.md`); it is **not** the frame product of `vehicle-body.md` ("Products"),
  which MM2 computes in place with different orders (e.g. row Y, column x adds k = 0 last there
  and k = 2 last here). Both tables are taken from the machine code; use each where its page says.
  Only the last term matters for the result (the first two are added first, in either order):

  | Row of A | column x | column y | column z |
  |---|---|---|---|
  | X | 0 1 2 | 1 2 0 | 1 2 0 |
  | Y | 1 0 2 | 2 1 0 | 2 1 0 |
  | Z | 0 2 1 | 2 1 0 | 2 1 0 |
  | origin | 1 0 2 | 2 1 0 | 2 1 0 |

- Sphere test: s = (d.z·P0.z + d.y·P0.y) + d.x·P0.x; L = (d.z² + d.x²) + d.y²; |P0|², |q|²: zyx.
- Polygonal pass: Cₖ to world zxy/xzy/xzy (as the city's current points); Sₖ to world
  yzx/xyz/xyz; plane() as the city; world edge normals to world zyx/zyx/zyx. Line-inside tests:
  f = (g.z·w.z + g.y·w.y) + g.x·w.x with w = S − cⱼ, h = (g.z·e.z + g.y·e.y) + g.x·e.x.
- Records to world: start, end, point xzy/zyx/zyx; polygon normal zxy/zyx/yxz; polygon-edge
  record edge normal xzy/zyx/yzx.
- Single-list pass in owner coordinates (inv = the owner's transpose inverse, whose rows are the
  owner's columns, so "into" uses the owner's rows): vel into yzx/zyx/zyx; point into
  yzx/zyx/zyx, then + inv's origin component; dir into yxz/xzy/zyx. Corner J−1 to world
  zyx/zyx/xzy, corner J to world zyx/yzx/xyz.
- Two-list pass: dot products (nrm·en, nrm·rv, s) zyx; LQ, |dP|², |c|²: zyx; c = dQ × dP
  canonical.
- Terrain: Cₖ to world zyx/yzx/yxz; corner start to world yzx/yxz/yxz; world edge normals zyx.
- Segment tests (terrain probes and edge tests, capsule passes 1 and 3): one-sided test
  d_S = (w.z·n.z + w.x·n.x) + w.y·n.y (w = S − c0), d_E likewise with E − c0 (zxy); two-sided test
  d_S = (w.y·n.y + w.x·n.x) + w.z·n.z, d_E likewise (yxz); their inside tests as above.
- Box polygon normal: a = c2 − c1, b = c0 − c1; n = (a.y·b.z − b.y·a.z, a.z·b.x − b.z·a.x,
  a.x·b.y − a.y·b.x); L = (n.y² + n.x²) + n.z²; n = n·(1/√L) (0 if L = 0). Box radius:
  √((h.x² + h.y²) + h.z²).
- Capsule setup: c into xzy/zyx/zyx, each negated; a into zyx/zyx/zyx; l into yzx/yzx/yzx;
  q to world zyx/zxy/zxy; v into zyx/zyx/zyx; V2 = (v.z² + v.y²) + v.x².
- Capsule writing: position to world zxy/zyx/zyx + o; normal to world xzy/xzy/xzy. Pass 4: W to
  world zyx/zyx/zyx + o; cur, prev into xzy/zyx/zyx; probe normal to world xzy/zyx/zyx, then
  negated; probe point to world xzy/zyx/zyx + o.
- Capsule dot products: (Q − Pⱼ)·sⱼ and N·sⱼ: xzy; N·v: zyx; (V[0] − Q0)·N: zyx; nrm·v (flip): xzy;
  |X − E0|², G2: zyx; g·v: zyx; |nrm|² in pass 3: xyz. Merge rules: "in front" (a − b)·n: zyx;
  sN and side: yxz; t = I.normal·ax: yzx; |u|²: zyx. Motion filter: D2: xyz; dotᵢ: xzy. Probe:
  pen: zyx; part normal length: zxy.
- Sphere–polygon test: d0 = (P − V[0])·N: yzx; test = m·a: zyx; a·e, b·e: yzx; la2, lb2: zyx;
  le2: yzx; |nrm|² for the edge and vertex cases: xyz; m = N × e as (e.z·N.y − e.y·N.z,
  e.x·N.z − e.z·N.x, e.y·N.x − e.x·N.y).
- Segment–edge test: C2: zyx; |d2|²: xzy; |d1|²: xyz; |n|² (parallel): zyx; dist (parallel):
  −((n.y·w.y + n.x·w.x) + n.z·w.z); dist (general): xyz; |nrm|² (edge end): zyx.
- Segment parameters, step 1, written out: |D1|² = (D1.x² + D1.y²) + D1.z²;
  w·D1 = (w.y·D1.y + w.z·D1.z) + w.x·D1.x; c and c′ both use D1·D0 = (D1.z·D0.z + D1.y·D0.y) +
  D0.x·D1.x; wp·up = (wp.z·up.z + wp.y·up.y) + up.x·wp.x; |up|² = (up.z² + up.x²) + up.y²;
  |D0|² = (D0.x² + D0.y²) + D0.z²; w′·D0 = (w′.x·D0.x + w′.y·D0.y) + w′.z·D0.z;
  wp′·up′ = (wp′.z·up′.z + wp′.y·up′.y) + up′.x·wp′.x; |up′|² = (up′.z² + up′.x²) + up′.y².
  Step 2: proj: k = −((w.z·D.z + w.y·D.y) + w.x·D.x), |D|² = (D.x² + D.y²) + D.z²; dist:
  |n|² = (n.z² + n.y²) + n.x², n·w = (n.y·w.y + n.z·w.z) + n.x·w.x.
