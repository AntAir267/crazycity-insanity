# Body contacts: finding where the car body touches the world

This page says how MM2 finds the contacts between the car's bound (`spec/formats/bnd.md`) and
the city polygons (`city-collision.md`) in one physics sample, and turns them into the impact
list that `collision.md` ("Collision response") resolves. Contacts with placed objects use the
same building blocks and are in `collision-object-contacts.md`.

The method in one paragraph: every corner of the car bound is followed along the straight path it
moved since the previous sample, and every edge of the bound is checked in its current position.
A corner whose path enters a polygon from its front, or an edge that pierces a polygon, gives an
**intersection**. The intersections are then grouped per corner and per edge to decide which
contacts become impacts, with which normal, depth and position.

Single precision throughout, operations in the order written (see `vehicle.md`, "Precision"). In
a race the game's penetration allowance ("skin") is 0, so it does not appear below.

## Terms

- **Body matrix** M: the car's world matrix as drawn (rows X, Y, Z, origin; `vehicle.md`
  "Centre of mass": origin = model origin). Bound points are transformed by it:
  p′ = p.x·X + p.y·Y + p.z·Z + origin.
- **Previous matrix** M₀: the car's body matrix as it was when the previous sample's collision ran
  (kept at the end of every sample, after collisions, before the push is applied; at reset it is
  the start matrix). For the city test, if the largest push the car received in the previous
  sample came from the city, M₀'s origin is moved by `lastPush` (`vehicle-body.md`: the push
  applied at the end of the previous sample). "Largest" is by squared length (x² + y²) + z²,
  compared strictly (the first of equal ones counts), over all pushes the car received in that
  sample, from the city and from objects.
- dt, 1/dt: the current physics step time and its inverse (`vehicle.md`).
- **Displacement of a body point** x between M₀ and M: r = x − origin(M);
  l = (r·X, r·Y, r·Z) using M's rows (each dot product evaluated as (r.z·a.z + r.y·a.y) + r.x·a.x);
  D = M − M₀ element by element (rows and origin); disp = l.x·D.X + l.y·D.Y + l.z·D.Z + D.origin,
  summed as ((D.Z·l.z + D.Y·l.y) + D.X·l.x) + D.origin per component. It is x minus where the same
  body point was under M₀. Its **velocity** is disp·(1/dt).
- Polygon plane distance: plane(x) = ((x.z − c0.z)·n.z + (x.y − c0.y)·n.y) + (x.x − c0.x)·n.x for a
  polygon with first corner c0 and normal n.
- **Line-inside test** of a polygon for a line through S with direction D (two-sided; it tests the
  infinite line, not a segment): for each polygon edge j (corner j → j+1, wrapping) with inward edge
  normal eⱼ: g = (c(j+1) − cⱼ) × D, f = (S − cⱼ)·g, h = eⱼ·g, with D = E − S for a line from S to
  E and both dot products summed (a.z·b.z + a.y·b.y) + a.x·b.x. The line fails if for some edge
  f ≠ 0 and sign(f) ≠ sign(h) (sign = sign bit). It passes otherwise.

## Step 1: which polygons

1. If the car's room is 0, there is no terrain collision this sample.
2. Query sphere: centre = origin of M, radius = the bound's radius (`bnd.md`; half the box
   diagonal, measured from the box centre — the sphere is still centred on the origin).
3. Rooms: the car's room, then Touched(room, centre, radius, 8) (`city-collision.md`).
4. Collect the rooms' polygons in that room order, each room's in its own order, at most 256 in
   total (`city-collision.md`, "Culling and buffers").

## Step 2: intersections

Transform every bound point k by M → Cₖ (current position) and by M₀ → Pₖ (previous position).
For each point, keep bestT = 2.0 and "no polygon". For each bound edge (from the bound's edge list)
keep tMin = 2.0, tMax = −1.0, and "no polygon" for both ends of the range.

For each polygon i in order (normal n):
1. dₖ = plane(Cₖ) for every point.
2. **Corner paths.** For each point k with dₖ < 0 (strictly behind the plane now):
   d₀ = plane(Pₖ). If d₀ > 0 (it was in front): t = d₀ / (d₀ − dₖ); if t < bestT (strictly — the
   first polygon wins a tie) and the line through Pₖ towards Cₖ passes the polygon's line-inside
   test: bestT = t, polygon = i, depth = −dₖ.
3. **Edges**, only if plane(origin of M) ≤ radius: for each bound edge e = (a, b), from the last
   edge to the first: if d_a and d_b have different sign bits: t = d_a / (d_a − d_b); if
   (t > tMax or t < tMin) and the line through C_a towards C_b passes the line-inside test:
   - if t ≤ tMin and (d_a > 0 or t < tMin): tMin = t, polygonMin = i, dMin = (d_a ≤ 0 ? d_a : −d_b);
   - then if t ≥ tMax and (d_a < 0 or t > tMax): tMax = t, polygonMax = i,
     dMax = (d_a ≤ 0 ? d_a : −d_b).
   (So each edge remembers its first and last crossing along a → b; d > 0 means a is in front.)
   If d_a and d_b are zeros of different sign (+0 and −0), t = 0/0 is NaN. Every comparison in
   this step involving a NaN (t, or a NaN tMin) counts as true for < and ≤ and false for > and ≥:
   a NaN t passes the range test (as t < tMin), takes the tMin update (with dMin as usual) and
   never the tMax update; once tMin is NaN, the next crossing of that edge that passes the
   line-inside test replaces it. tMax never becomes NaN. (Same rule as for objects,
   `collision-object-contacts.md` 2.1 and 3.3.)

Then write the intersection list, first the points in point order, then the edges in edge order:
- **Corner record** for every point k that found a polygon: start S = Pₖ, end E = Cₖ, T = bestT,
  point = S + (E − S)·T (per component (E − S)·T + S), normal = the polygon's normal, polygon,
  material code of the polygon, depth = −dₖ (> 0), front = yes, number = k, "from" = none (−1),
  "to" = k, kind = corner.
- **Edge records**: for each edge with a polygonMin, one record with T = tMin, d = dMin; and if it
  has a polygonMax different from polygonMin, a second record with T = tMax, d = dMax. Fields:
  start = C_a, end = C_b, point = start + (end − start)·T, front = (d > 0), depth = d if front,
  else −d (so d = +0 gives depth −0), normal,
  polygon and material code of that polygon, number = e, from = a, to = b, kind = edge, and the edge's
  **world edge normal** = the bound's edge normal (`bnd.md` item 5) turned by M's rows (no origin).

A record's **number** is its point index (corner records) or its edge index (edge records); the
rules below compare numbers without looking at the kind, exactly as the original does.

If the list is empty, the car does not touch the city this sample.

## Step 3: impacts from the intersections

Record flags used below: *grouped*, *face* (emit a face impact at the end), *pair* (the edge was
crossed both ways consistently), *fast*, *suppressed*, *done* (an edge-edge impact was made from it).
Each record also has a metric m (initially "huge").

### 3a. Normalise edge records

For each record in list order:
- clear all flags; m = huge;
- for an edge record with front = no: swap its ends (from ↔ to), swap start and end, T = 1 − T,
  front = yes. Now every edge record runs from its front end (start, "from") to its back end
  (end, "to") and T counts from the front end; the point is unchanged.
- if the previous record in the list (whatever its kind) has the same number and its "to" equals
  this record's "from" (the same car edge, crossed in the opposite direction): if (1.03 − previous T) <
  this T, give both *suppressed* and *done*; otherwise give both *pair*.

### 3b. Corner groups

The **group** of a bound point v = all records whose "to" is v: v's corner record (if any) and
every edge record whose back end is v.

Go through the records in list order; for each record not yet *grouped* and not *pair*, process
the group of its "to" point v:

1. vel = −(velocity of the displacement of the **end point of the first record in the whole
   list**) (an original quirk: not of v). fast = 1.5·dt.
2. best = huge, bestDepth = huge, second = huge, secondDepth = huge, found = no, blocked = no,
   keep = yes. Then for every record R of the whole list, in order:
   a. if R's "from" is v and R is neither *done* nor of another kind: blocked = yes;
   b. if R's "to" is v (R is in the group): if R is *pair*, blocked = yes. Mark R *grouped*.
      Its metric: a corner record has m = ((1 − T)·dt)·0.9 and becomes *fast*; an edge record has
      m = depth/rn when rn = (vel.x·n.x + vel.z·n.z) + vel.y·n.y > 0, else m = huge (and m = 0 if
      depth < 0), and is *fast* when m < fast.
      R becomes the **best** if it is a corner record, or (it is *fast* and m < best), or (best >
      fast and depth < bestDepth and T > 0.001 and not blocked so far). Then the old best becomes
      the second; best = m, bestDepth = depth, found = yes, and the draft impact is: normal = R's
      normal, depth = R's depth, polygon = R's polygon, point v; position:
      - corner record: the middle of its point and its end, P = point + (end − point)·0.5 written
        as (1 − w)·end + w·start with w = (1 − T)·0.5; keep = no;
      - edge record, *fast* and m > 0.00001: end + vel·(m·0.5); keep = (R is *done*);
      - other edge record: end + n·(depth·0.5); keep = not (m > 0.00001 and R is not *done*).
      Otherwise R becomes the **second** if found and R's polygon differs from the draft's and
      ((R is *fast* and m < second) or (second > fast and depth < secondDepth and T > 0.001 and not
      blocked)).
      R's metric is stored.
3. If nothing was found, the group gives no impact now.
4. If blocked: if keep = no, mark the best record *face*. No impact now.
5. Otherwise, with N = the draft normal and c = 0.258819043636 (sin 15°), for every record R of the
   list (not of another kind):
   - if R's "to" is v: if R's polygon is the draft polygon, mark R *suppressed*; else if R is not
     the best and (R is a corner record or R's number is not the best record's edge number, which a
     corner best does not have): d = end − start,
     q = (d.z·N.z + d.y·N.y) + d.x·N.x; if q < 0 and q² > |d|²·c, mark R *suppressed*; else if q > 0
     and q² > |d|²·c, a second impact is needed (|d|² evaluated as (d.x² + d.z²) + d.y²);
   - else if R's "from" is v and the best is a corner record: the same with d = start − end.
   If a second impact is needed and there is a second-best record, mark it *face*.
6. Write the impact: kind "face of the city against point of the car", A = the city, B = the car,
   normal = −N (pointing into the city), depth and position from the draft. (`collision.md`
   impact record.)

### 3c. Pair merge

i = 0; while i < (count − 1): if records i and i+1 have the same number, share *suppressed* between
them (if either has it, both have it) and i += 2; else i += 1.

### 3d. Edge against edge

Only if there are edge records. i = 0; while i < count: let P = record i;
paired = (P is an edge record, i < count − 1 and record i+1 has the same number).
If P is an edge record and not *suppressed*:
1. dir = d·k per component, with d = P.end − P.start, L = (d.z² + d.y²) + d.x², k = 1/√L (0 if
   L = 0); vel = velocity of the displacement of P's point.
2. Edge-edge test of P (below) → ok, normal, position, depth, flag.
   If ok: mark P *done* and write an impact: kind edge-edge, A = the city, B = the car,
   normal = −(test normal), depth, position.
3. If flag: clear *face* on P (and on record i+1 if paired); else clear *face* on P if
   P.depth > depth/2, and on record i+1 (if paired) if its depth > depth/2. "depth" is one
   variable for the whole step 3d of this test: it starts at 0 when the test's step 3d begins, every
   edge-edge test that gets as far as computing a depth (step 5 of the test) overwrites it, and a
   test that gives up earlier leaves the value from before (MM2 leaves the start value undefined;
   0 is this spec's choice). flag is always set by the test (step 3 of the test).
4. If paired, with Q = record i+1: steps 1–3 again with Q's point for the velocity, Q as the record
   tested, and P's dir. Step 3 then works on P and record i+1 (= Q) again, with this second test's
   flag and depth. If this test is ok, mark P (not Q) *done*, and write its impact only if the
   first test of the pair was not ok.
Advance i by 2 if paired, else by 1.

**Edge-edge test** of an edge record R (city polygon of R with normal N and corners in order):
1. vp = vel + dir·s with s = −((dir.y·vel.y + dir.x·vel.x) + dir.z·vel.z) (the motion across the
   car edge); with c = dir × N,
   cc = (c.x² + c.z²) + c.y², dn = |(dir.z·N.z + dir.y·N.y) + dir.x·N.x|: if dn = 0, vp = 0;
   else if cc ≥ 0.000001:
   k = −(((c.x·vp.x + vp.z·c.z) + vp.y·c.y) / cc), w = vp + c·k, vp = vp + w·(1/dn − 1).
2. Over the polygon's edges j (from corner j−1 to corner j, starting with last → first):
   e = normalise(corner(j−1) − corner(j)) (L = (e.x² + e.y²) + e.z², times 1/√L, 0 if L = 0),
   m = e × N (pointing into the polygon),
   dist = ((p.x − cⱼ.x)·m.x + m.z·(p.z − cⱼ.z)) + m.y·(p.y − cⱼ.y) with p = R's point,
   pen = max(dist, 0), rate = (m.x·vp.x + m.z·vp.z) + m.y·vp.y;
   if rate > 0 and pen < rate·best: best = pen/rate, bestJ = j (best starts huge);
   if dist < minDist: minDist = dist, minJ = j (minDist starts huge).
3. If 1.5·dt > best: flag = yes, J = bestJ; else flag = no, J = minJ (if there is none, not ok).
4. The polygon edge is A = corner(J−1) (corner n−1 when J = 0), B = corner J; pe = B − A;
   ce = R.end − R.start; S = R.start, E = R.end.
5. Closest points of the two lines:
   - n0 = pe × ce; L = (n0.z² + n0.y²) + n0.x²; if L ≤ 0, not ok. nrm = n0·(1/√L) per
     component; w = A − S; depth = (nrm.y·w.y + nrm.x·w.x) + nrm.z·w.z; if depth < 0, negate both
     depth and nrm.
   - q = nrm × ce; s0 = q·(A − S), s1 = q·(B − S), each dot product as (q.z·w.z + q.y·w.y) + q.x·w.x
     (as are t0 and t1 below, t0 negated after summing). Not ok if A and B are on the same side:
     s0 > 0 and s1 > 0.01·s0 and s0 > 0.01·s1, or s0 ≤ 0 and s1 < 0.01·s0 and s0 < 0.01·s1.
     Point on the polygon edge: (A + B)·0.5 if s1 = s0, else (s1·A − s0·B)·(1/(s1 − s0)).
   - r = nrm × pe; t0 = −((A − S)·r), t1 = (E − A)·r; the same same-side rule on (t0, t1) → not ok.
     Point on the car edge: (S + E)·0.5 if t1 = t0, else (t1·S − t0·E)·(1/(t1 − t0)).
6. ok if (en.z·nrm.z + en.y·nrm.y) + en.x·nrm.x < −((cos − 0.258819043636) − 0.01), where
   en = R's world edge normal and cos = the car bound's edge cosine of R's edge (`bnd.md`; concave
   edges have 2 and never pass). Both constants are single-precision values (0.258819043636 is
   the float nearest to it, the same as in 3b) and every step is rounded to single precision, so
   plain float arithmetic gives the original's result exactly. Then the position is the middle of
   the two closest points.

### 3e. Face impacts

For each record marked *face* and neither *suppressed* nor *done*: skip it if it is not *fast*
and T ≤ 0.75 and depth² ≥ 0.04·|end − start|² (with d = end − start, |d|² summed
(d.z² + d.y²) + d.x²); otherwise write an impact: kind "face of the city
against point of the car", A = the city, B = the car, normal = −(record normal), depth,
position = end + normal·(depth·0.5) (with the record's normal, before negation).

### 3f. Materials

Every impact written in this step names, as the city's material, the material code of the
**first intersection in the list** (an original quirk: one material for all impacts of the
sample, whichever polygon each one touched). The car's material is its bound material
(`collision.md`, "The car's bound").

The impacts are then resolved one by one with weight 1/(number of impacts written)
(`collision.md`, "Collision response"). At most 200 impacts are written; edge-edge impacts stop
when 200 have been written (others are not checked; the limit is never reached by a car).

## Notes for implementers

- Polygons are one-sided here: a corner only counts when its path enters through the front. Edges
  are tested in their current position only, so a car resting still on a polygon gets contacts
  only from edges piercing polygons; corners need motion.
- The 256-polygon and 100-intersection buffers are never exceeded by a car in the shipped cities.
- A degenerate car edge or polygon edge (zero length) normalises to the zero vector; keep that.
- Summation orders MM2 uses here (for bit-exact results; `vehicle.md` "Precision"): current
  bound points x = ((Z.x·p.z + X.x·p.x) + Y.x·p.y) + o.x, y = ((X.y·p.x + Z.y·p.z) + Y.y·p.y) + o.y,
  z = ((X.z·p.x + Z.z·p.z) + Y.z·p.y) + o.z; previous bound points
  x = ((Y.x·p.y + Z.x·p.z) + X.x·p.x) + o.x and the same pattern for y and z; world edge normal
  x′ = (Y.x·e.y + Z.x·e.z) + X.x·e.x and the same pattern for y′, z′ (X, Y, Z, o = rows and
  origin of the matrix, p = model-space point, e = model-space edge normal).
