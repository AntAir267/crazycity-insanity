# City collision: polygons from the PSDL, rooms and lookups

What a car touches in a city is (1) polygons the game makes from the PSDL elements of each room
(this file) and (2) the bounds of some placed objects (`collision-objects.md`).
There is no separate collision file for the streets: the PSDL is the collision geometry, read
through rules that differ from the drawing rules in `psdl.md` (some drawn things do not collide,
some collide in a simplified shape, and roads are not split at the centre line).

The game makes a room's polygons on demand, for one query at a time; an implementation may build
them once per room at load and keep them, as long as it keeps the order given here.

Read `psdl.md` first: room records, element words, texture slots and the point/height tables are
defined there. `V[i]` is point i, `F[i]` height i, *t* the current texture slot.

## Arithmetic

All positions are single-precision floats. The curb height is the single-precision value of 0.15
(bit pattern 0x3E19999A, 0.150000006). "P + h" (P raised by h) adds h to the Y of P in single
precision; the X and Z are copied unchanged. Several rules below test heights for exact equality,
so do this arithmetic in single precision as written (−0.15 + 0.15 is exactly 0).

## Materials of texture slots

Every texture slot has a **material code** (a small integer stored with each polygon):

1. Build the material list: entry 0 is the built-in default material; then every `mtl` block of
   `city/materials.mtl` in file order, skipping names already in the list (case-sensitive).
   Material fields are in `collision.md`, "Materials".
2. Read `city/materials.csv`: skip the first line; each further line is `texture,physics`. If
   `physics` is `none`, ignore the line. Otherwise look `physics` up in the list (exact,
   case-sensitive); if found at position *i*, texture `texture` gets code *i* + 1; names not in the
   list (the shipped file has `ash` and `mud`) give nothing. If a texture appears on several lines,
   the first line that gives it a code wins.
3. A slot's code is the code of its texture name (the name after removing an animated `-0nnn`
   suffix, `psdl.md`), compared exactly (case-sensitive); slots without a code, and slot 0, have
   code 0.

Code 0 and code 1 both mean the built-in default material; code *c* ≥ 1 means list entry *c* − 1.
With the shipped files: 2 = `deepwater`, 3 = `_default`, 4 = `grass`, 5 = `water`, 6 = `dirt`,
7 = `sand`, 8 = `cobblestone`, 9 = `wood`; every road, sidewalk and crosswalk texture is
`cobblestone` (8); parks are `grass` (4); `s_thames` and `s_ocean` are `deepwater` (2); `s_pond`
and `s_water` are `water` (5); everything else (building walls, roofs, most plazas) is 0.

## Collision polygons

A collision polygon has 3 or 4 corners (positions), a material code, a unit normal and one inward
edge normal per corner:

- **Normal.** If the first three corners have exactly equal Y, the normal is exactly (0, 1, 0).
  Otherwise n = normalise((c2 − c1) × (c0 − c1)) for corners c0, c1, c2. If that cross product has
  length 0 the polygon is **dropped** (it is not made at all; nothing below counts it).
  Exactly: a = c2 − c1, b = c0 − c1, n = (a.y·b.z − a.z·b.y, a.z·b.x − a.x·b.z, a.x·b.y − a.y·b.x),
  L = (n.y·n.y + n.x·n.x) + n.z·n.z (note the order), k = 1/√L (0 if L = 0), n = (n.x·k, n.y·k,
  n.z·k); the polygon is dropped if (n.z·n.z + n.y·n.y) + n.x·n.x < 0.9 afterwards, which in single
  precision happens exactly when L = 0. A quad's
  fourth corner does not take part (non-planar quads keep the normal of their first three corners).
- **Edge normals.** For each corner k: e = c(k+1) − c(k) (wrapping), edge normal = normalise(n × e)
  (a zero vector stays zero), with g = n × e = (n.y·e.z − n.z·e.y, n.z·e.x − n.x·e.z,
  n.x·e.y − n.y·e.x), L = (g.x·g.x + g.y·g.y) + g.z·g.z, k = 1/√L (0 if L = 0), g·k per component. They point into the polygon. A polygon with two equal corners but
  equal Y keeps its (0, 1, 0) normal and is not dropped (roofs have a few such zero-area
  triangles); it can only be hit exactly on its line.
- Facing: the normal points to the side from which the corners run counter-clockwise.

### Building blocks

The element rules below are written with these constructions. "Section" means one cross-section
of a strip element; *i* and *i*+1 are consecutive sections.

- **Strip quad** S(p, hp; q, hq; p′, hp′; q′, hq′), where p, q are point indices of section *i* and
  p′, q′ of section *i*+1, each with a height offset (0 or the curb height):
  1. If q = q′ (same point index): it is the triangle (p+hp, q+hq, p′+hp′).
  2. Else if p = p′: the triangle (p+hp, q+hq, q′+hq′).
  3. Else the quad (p+hp, q+hq, q′+hq′, p′+hp′) — **except** when p′ is point index 0 and hp′ is 0:
     then the game takes it for the triangle (p+hp, q+hq, q′+hq′) (its "fourth index 0 means
     triangle" rule; keep this).
- **Wall** W(p, q, yp, yq) for point indices p, q: the quad (V[p], V[q], (V[q].x, yq, V[q].z),
  (V[p].x, yp, V[p].z)). It stands on the ground line p→q and faces to the right of p→q seen from
  above, i.e. along (−(q.z − p.z), 0, q.x − p.x).
- **Wall from positions** W(A, B, yA, yB): the same with positions A, B.
- **Flat triangle** at height y: the triangle of the three points with their Y replaced by y.
- **Triangle** T(A, B, C): the triangle of the three positions as given.

### Walking a room

Walk the room's elements in order (`psdl.md`, "Element words"), keeping *t* (initially 0) and a
flag *hidden* (set by a texture element whose value is 0, cleared by any other texture element).
While *hidden* is set, elements of types 0, 1, 2, 4, 5, 6 and 8 make nothing; types 7, 9 and 12
still do. A divided road (type 8) skipped while *hidden* is set is stepped over by only 5*n* + 2 of
its 2 + 6*n* data words: the walk goes on at that word, reading the road's last *n* words as the next
element header(s) (an original slip, the same in the drawing walk, `psdl.md`, "Drawing state"). No
shipped room has a divided road after a texture element of value 0. Let *ms* = code of slot *t* + 1 ("sidewalk material"), *mr* = code of slot *t*.

**Triangle mode.** If the room's PSDL flag byte has bit 0x20, road surfaces, sidewalks, medians
and ground strips are made as two triangles per section instead of one strip quad, and **these
triangles always use *ms*** (the material of slot *t* + 1, even for the road surface — an original
quirk; road and sidewalk textures share a material in shipped data). Walls, curbs, crosswalks and
fans are the same in both modes. 27 rooms in London and 77 in SF have bit 0x20 (bridges, freeway
ramps).

The polygons of each element, in the order made:

**Type 0, road** — sections (a, b, c, d) as in `psdl.md`.
1. For each section pair *i*, *i*+1, if aᵢ ≠ bᵢ (point indices, tested per section):
   left sidewalk S(aᵢ,0; bᵢ,curb; aᵢ₊₁,0; bᵢ₊₁,curb), material *ms* (triangle mode:
   T(aᵢ, bᵢ+curb, aᵢ₊₁) and T(bᵢ+curb, bᵢ₊₁+curb, aᵢ₊₁)); then the left curb face
   W(bᵢ, bᵢ₊₁, bᵢ.y + curb, bᵢ₊₁.y + curb), material *ms*.
2. For each section pair: road surface S(bᵢ,0; cᵢ,0; bᵢ₊₁,0; cᵢ₊₁,0), material *mr* (triangle
   mode: T(bᵢ, cᵢ, bᵢ₊₁), T(cᵢ, cᵢ₊₁, bᵢ₊₁), material *ms*). One polygon across the whole road:
   unlike the drawing, there is no split at the centre line.
3. For each section pair, if cᵢ ≠ dᵢ: right sidewalk S(cᵢ,curb; dᵢ,0; cᵢ₊₁,curb; dᵢ₊₁,0), material
   *ms* (triangle mode: T(cᵢ+curb, dᵢ, cᵢ₊₁+curb), T(dᵢ, dᵢ₊₁, cᵢ₊₁+curb)); then the right curb face
   W(cᵢ₊₁, cᵢ, cᵢ₊₁.y + curb, cᵢ.y + curb), material *ms*.

So the sidewalk top is a slope from the outer edge (at its own height) to the curb top (curb
point raised by 0.15), and the curb faces are vertical 0.15 m walls facing the road.

**Type 1, sidewalk** — pairs (p, q).
- Curb end cap (*n* = 2 and the first pair is (0, 0) or (1, 1)): one triangle, material *ms*:
  with A = V of the second pair's first index and B = V of its second index, (A+curb, B, A) for
  (0, 0) or (A, B, A+curb) for (1, 1).
- Otherwise for each pair *i*, *i*+1: top S(pᵢ,curb; qᵢ,0; pᵢ₊₁,curb; qᵢ₊₁,0), material *ms*,
  then curb face W(pᵢ₊₁, pᵢ, pᵢ₊₁.y + curb, pᵢ.y + curb), material *ms*. (No triangle mode.)

**Type 2, ground strip** — pairs (p, q): for each pair *i*, *i*+1: S(pᵢ,0; qᵢ,0; pᵢ₊₁,0; qᵢ₊₁,0),
material *mr* (triangle mode: T(pᵢ, qᵢ, pᵢ₊₁), T(qᵢ, qᵢ₊₁, pᵢ₊₁), material *ms*).

**Type 3, low wall** and **type 11, building wall**: no collision.

**Type 4, crosswalk** — points p0, p1, p2, p3: one flat polygon at y = V[p0].y with corners
(p1, p0, p2, p3) in that order (each with its Y replaced by y), material = code of slot *t* + 2.
(If p3 is point index 0 and its own Y already equals y, it is a triangle of the first three, by
the fourth-index rule.)

**Type 5, intersection fan** — points p0 … p(n+1). If the code of slot *t* is 2 (deep water),
nothing. Otherwise flat triangles at y = V[p0].y: (p0, p(k−1), p(k)) for k = 2 … n+1, material
*mr*. The whole fan is treated as flat at its first point's height.

**Type 6, ground fan** — if the code of slot *t* is 2, nothing; otherwise the triangles
(V[p0], V[p(k−1)], V[p(k)]) for k = 2 … n+1 with their own heights, material *mr*. (Deep-water
ground is not solid: a car falls through it into the water, see `collision.md`, "World limits".)

**Type 7, wall light** (the building's collision wall) — words k, h, A, B:
W(A, B, F[h], F[h]): from the ground line A→B (each point at its own Y) up to the absolute height
F[h], material *mr*. These are the walls of every PSDL building (the building walls of type 11
only draw).

**Type 8, divided road** — w0, w1 and sections (a, b, c, d, e, f). kind = w0 & 0x3F,
mt = w0 >> 8, h = w1 / 256.
1. Left sidewalk and curb from (a, b) exactly as type 0 step 1.
2. For each section pair *i*, *i*+1:
   - left road S(bᵢ,0; cᵢ,0; bᵢ₊₁,0; cᵢ₊₁,0), material *mr* (triangle mode: two triangles as for
     type 0, material *ms*);
   - median: if kind = 1: S(cᵢ,0; dᵢ,0; cᵢ₊₁,0; dᵢ₊₁,0), material = code of slot mt + 1 (triangle
     mode: T(cᵢ, dᵢ, cᵢ₊₁), T(dᵢ, dᵢ₊₁, cᵢ₊₁), material *ms*). For any other kind: the walls
     W(cᵢ₊₁, cᵢ, h + cᵢ₊₁.y, h + cᵢ.y) and W(dᵢ, dᵢ₊₁, h + dᵢ.y, h + dᵢ₊₁.y), material *ms*, then the
     top S(cᵢ,h; dᵢ,h; cᵢ₊₁,h; dᵢ₊₁,h), material = code of slot mt + 2 (triangle mode: the top is
     T(cᵢ, dᵢ, cᵢ₊₁), T(dᵢ, dᵢ₊₁, cᵢ₊₁) at road level, material *ms* — the walls still stand).
     The collision median is a box of height w1/256 for kinds 2 and 3 alike (the drawn kind-3
     median is 1 m with sloped sides; shipped kind-3 medians have w1 = 256, so both are 1 m). No
     end caps.
   - right road S(dᵢ,0; eᵢ,0; dᵢ₊₁,0; eᵢ₊₁,0), material *mr* (triangle mode as above).
3. Right sidewalk and curb from (e, f) exactly as type 0 step 3 with (e, f) in place of (c, d).

**Type 9, tunnel** — collision walls are vertical and their height is the **clearance**
c = max(3, word 2 / 256) (not the drawn wall height). Material *mr*.
- *Junction tunnel* (10 words): walls = 32-bit mask (words 4–5, low word first). For each
  perimeter index i with bit i set: W(P(i), P(i−1), P(i).y + c, P(i−1).y + c), P = the room's
  perimeter points (wrapping). These face into the room. Nothing else (no ceiling, no outside).
- *Road tunnel* (3 words): find the road element as in `psdl.md` (next element, skipping one
  texture element; types 0, 8 or 2; section count = low 8 bits of its count). Per section *i*:
  Lᵢ = first point, L′ᵢ = second, Rᵢ = last, R′ᵢ = second-to-last (for type 8 the sections start
  after w0, w1). With k = (word 1 / 256)·0.333 (0.333 is the single-precision constant
  0.333000004; k rounded to single), the outward points Oᵢ (from A = Lᵢ, B = L′ᵢ) and Qᵢ (from
  A = Rᵢ, B = R′ᵢ) are, in single precision, f = k / √((A.x − B.x)² + (A.z − B.z)²) (the two
  squares added, then the root, then the division) and (A.x − (B.x − A.x)·f, A.y,
  A.z − (B.z − A.z)·f): a step of k away from B, only X and Z change. There is no guard: equal
  X and Z of A and B would divide by zero and give non-finite points (the shipped data never
  has this). Flags as in `psdl.md`.
  - *Flat walls* (flag 0x2000 clear):
    1. For each section pair *j*, *j*+1: if 0x0001, W(Lⱼ, Lⱼ₊₁, Lⱼ.y + c, Lⱼ₊₁.y + c); if 0x0002,
       W(Rⱼ₊₁, Rⱼ, Rⱼ₊₁.y + c, Rⱼ.y + c).
    2. If 0x0001 and 0x0004: if 0x0010, W(O₀, L₀, …); then for each pair W(Oⱼ₊₁, Oⱼ, …); then if
       0x0020, W(L(n−1), O(n−1), …). (Each "…" is: both tops at their own point's Y + c.)
    3. If 0x0002 and 0x0004: if 0x0040, W(R₀, Q₀, …); then for each pair W(Qⱼ, Qⱼ₊₁, …); then if
       0x0080, W(Q(n−1), R(n−1), …).
  - *Curved walls* (0x2000): q = c / 4.
    - If 0x0001, for each pair *j*: X = Lⱼ if *j* = 0 and flag 0x0200, else Oⱼ;
      Y = Lⱼ₊₁ if *j* = n−2 and flag 0x0400, else Oⱼ₊₁. First the quad
      (Lⱼ, Lⱼ₊₁, Y + q, X + q) (a slope from the road edge out and up to the wall line), then the
      wall W(X, Y, X.y + c, Y.y + c) standing on X, Y at their own (road edge) height.
    - If 0x0002, for each pair: X = Rⱼ if *j* = 0 and flag 0x0800, else Qⱼ; Y = Rⱼ₊₁ if *j* = n−2
      and flag 0x1000, else Qⱼ₊₁; the quad (Rⱼ₊₁, Rⱼ, X + q, Y + q), then W(Y, X, Y.y + c, X.y + c).
  - No ceilings, arches or undersides collide.

**Type 10, texture**: sets *t* and *hidden* (see above). **Type 12, roof** — words h, p0 … pn:
flat triangles at y = F[h]: (p0, p(k−1), p(k)) for k = 2 … n, material *mr*. Roofs are solid.

Unlike the drawing, types 7, 9 and 12 also make polygons after a texture element of value 0.

## Rooms for collision

### Room data computed at load

For every room, besides `psdl.md`'s data:

- **Vertical range** [ylo, yhi], exactly as `psdl.md` "Vertical range" (ymin − 1 … 1000, or for
  flag 0x02 rooms −1000 … ymax + clearance).
- **Room state bits** (the game's own per-room bits, not the PSDL byte):
  - *plain* — set when the PSDL byte has 0x10 and not 0x40; or has 0x08, not 0x40, and the first
    element that is neither a texture nor a tunnel is not a divided road, and the last tunnel
    element passed on the way (if any) has neither flag 0x0001 nor 0x0002. Only used by an
    optimisation that is switched off in the game (see `collision.md`); listed for completeness.
  - *enclosed* — the PSDL byte has 0x02 (used by cameras and rain).
  - *landmark* — set when an `.inst` landmark record (header bit 24, `inst.md`) is filed in the
    room; rain tests it (`night.md`). It is not the PSDL byte's 0x20.
  - *deep water* — `psdl.md` "Deep water" (first element a texture whose slot code is 2, unless
    the room has both 0x02 and 0x04; or listed in `city/<city>.water`).
  - *extra rooms* — only when the city's name contains `sf`: rooms 411, 412, 423 and 625 get this
    bit (see "Ground queries" in `collision.md`).
- **Overlap lists.** For every stacked room S (PSDL bit 0x40), in increasing room order, and every
  perimeter edge A→B of S: take the seven flat points A, B and P with
  P.x = (B.x − A.x)·f + A.x, P.z = (B.z − A.z)·f + A.z for f = 0.33, 0.66, 0.5, 0.16, 0.86
  (single precision; the constants are the single-precision values). Every room X without bit
  0x40 that contains any of these points (flat point-in-room test below) gets S added to its
  overlap list (once); the rounding matters, because many of these points lie on, or within
  rounding distance of, an edge of X (London: 197 rooms get a list, 8 of them only when the
  points and the point-in-room test are computed in single precision as written). In London only, after
  that, room 382 is added to the lists of rooms 597, 599, 600 and 1297 (if the city's name
  contains `london`).

### Point in room

A point (x, y, z) is **in room r** when:
1. (x, z) is inside r's perimeter by the crossing rule: walk the perimeter edges (previous point →
   current point, starting with last → first); skip an edge whose two perimeter entries name the
   same point index; an edge crosses when (prev.z ≤ z < cur.z) or (cur.z ≤ z < prev.z), at
   x′ = (prev.x − cur.x)·(z − cur.z)/(prev.z − cur.z) + cur.x (single precision, left to
   right: product, quotient, sum); count it only if x′ > x. Inside when the count is odd.
2. and, if r has PSDL bit 0x40: ylo ≤ y ≤ yhi of r;
3. and, if r has no bit 0x40 but has an overlap list: the point is **not** in any room S of the
   list in the sense "ylo(S) ≤ y ≤ yhi(S) and (x, z) inside S's perimeter".

### Finding the room of a point

Find(point, hint):
1. If hint > 0 and the point is in the hint room, the answer is the hint.
2. If hint > 0: among the rooms named as neighbours in the hint room's perimeter that contain
   the point, the answer is the one with the highest room number (if any).
3. Otherwise the global search: the game buckets rooms in a grid over the city box and returns, of
   the rooms in the point's bucket that contain the point, the **last** one in the bucket's list
   (which is in increasing room order), i.e. the highest-numbered room containing the point; 0 if
   none (the point is outside the city). Exactly: the grid has 64 × 64 buckets over the box
   (x0, z0)–(x1, z1) of all rooms' perimeter points, sx = 64 / (x1 − x0), sz = 64 / (z1 − z0)
   (single precision). Room r is listed in the buckets ix = trunc((rx0 − x0)·sx) …
   ceil((rx1 − x0)·sx) − 1, iz = trunc((rz0 − z0)·sz) … ceil((rz1 − z0)·sz) − 1, with
   (rx0, rz0)–(rx1, rz1) the box of r's perimeter points (each product rounded to single, then
   truncated toward zero or rounded up). The point's bucket is ix = trunc((x − x0)·sx),
   iz = trunc((z − z0)·sz); outside 0 … 63, or an empty bucket, gives 0. (So a point just inside
   a room's far edge whose product rounds up onto the integer where that room's buckets end is
   not found in that room; this is the only difference from "the highest-numbered room".)

Each moving object and each probe end keeps its last room as the hint for the next query.

### Rooms touched by a sphere

Touched(room, centre, radius, at most 8) gives the neighbours a body near the room edge can
touch. Walk the room's perimeter entries *i* in order; let N be the neighbour across the edge from
point Pᵢ to Pᵢ₊₁ (wrapping):
- skip N = 0 and rooms already in the result;
- if N has PSDL bit 0x80, add it;
- else if Pᵢ and Pᵢ₊₁ are the same point index: add N if the flat distance from the centre to Pᵢ
  is less than the radius (compared squared: (c.x − Pᵢ.x)² + (c.z − Pᵢ.z)² < radius·radius);
- else with flat vectors a = Pᵢ − centre, b = Pᵢ₊₁ − centre, L² = |Pᵢ₊₁ − Pᵢ|²,
  T = 2(L² + radius²): add N if |a|² < T and |b|² < T and K·radius² − (aₓb_z − a_zbₓ)² > 0
  with K = |b − a|² (the sphere's flat circle crosses the edge's line, near the edge). In single
  precision: radius² = radius·radius; L² from the points, (Δz² + Δx²) with Δ = Pᵢ₊₁ − Pᵢ;
  T = (L² + radius²) doubled; |a|², |b|² sums of the two squares; K from the differences of the
  already rounded a and b (bₓ − aₓ and b_z − a_z, not Pᵢ₊₁ − Pᵢ), K = (Δz² + Δx²) of those;
  then (K·radius²) − cross·cross with cross = aₓb_z − a_zbₓ;
- stop when 8 rooms have been added.

## Culling and buffers (how many polygons a query sees)

A query (body collision or probe, `collision.md`) asks a room for its polygons near a sphere
(centre c, radius r). The game skips polygons that are clearly out of reach. **Implement these
tests exactly**: they are conservative for normal cars, but a long vehicle's bound can stick out
of its query sphere by a few millimetres, and the 256-polygon budget below counts only polygons
that pass. Skipped polygons do not change the order of the others. All distances here are flat
(X and Z only).

**Section range of a strip.** Roads (type 0), divided roads (type 8) and ground strips (type 2)
first narrow the sections they work on. Each of their passes has a cross-section line per section
i, from point column k0 to point column k1 of that section:

| Element, pass | k0 → k1 |
|---|---|
| road: left sidewalk / road surface / right sidewalk | a → b / b → c / c → d |
| divided road: left sidewalk / roads and median / right sidewalk | a → b / b → e / e → f |
| ground strip | p → q |

The signed distance of c from line i is d(i) = ((P.z − c.z)·D.x − (P.x − c.x)·D.z)·w, with P the
k0 point, D = (k1 point) − P and w = 1/√(D.x² + D.z²) (w = 0 for a zero-length line). Then, with
n sections:
1. lo = 0, hi = n − 1. If n < 3, use all sections (lo … hi − 1).
2. m = n. Repeat: k = lo + (m div 2). If d(k) > −r: lo = k; if also d(k) < r, go to 3; else
   m = (m + 1) div 2. Otherwise (d(k) ≤ −r): hi = k, m = (m div 2) + 1. If m < 3, stop and use
   lo … hi − 1.
3. While lo ≥ 1 and d(lo) < r: lo = lo − 1.
4. j = k; while j < hi and d(j) > −r: j = j + 1. hi = j.
5. The pass makes its polygons only between sections lo … hi − 1 (each i with i+1).

Sidewalk elements (type 1), tunnels, fans, walls and roofs have no section range.

**Polygon test.** Every polygon about to be made gets a reference corner R and a flat extent L²:
- triangles (including fan triangles, curb caps and triangle-mode pairs), with corners c0, c1, c2
  in polygon order: e1 = |c0 − c1|², e2 = |c1 − c2|², e3 = |c2 − c0|²; if e1 ≤ e2 then
  L² = max(e2, e3) and R = c2, else L² = max(e1, e3) and R = c0;
- quads and walls: R = the first corner, L² = |first corner − third corner|² (for a wall that is
  its ground line, for a strip quad the diagonal p → q′).
The polygon is made only if |c − R|² < 2·(L² + r²) (strictly less); otherwise it is skipped (it
does not count, consumes no budget and makes no vertices). In single precision every |…|² here
is the sum of its two flat squares, r² = r·r, and the right side is (L² + r²) doubled.

**Fans and roofs.** An intersection fan (type 5) or a roof (type 12) at height y is skipped as a
whole unless (c.y + r − y)·(c.y − r − y) < 0 (the sphere straddles its plane).

**Budget.** The body collision collects at most 256 polygons over all its rooms per query; once
256 have passed, every later polygon of the query is lost (the game reports it; a car never gets
there in the shipped cities). The probe has no such limit (it works through a room in batches).

## Checked against

`tools/physics/citycoll.py` (dirty side) builds these polygons for every room:

| City | Rooms | Polygons | Facade walls | Roof triangles | Curb faces (road / sidewalk / divided) | Tunnel walls |
|---|---:|---:|---:|---:|---:|---:|
| London | 1,341 | 41,469 | 9,618 | 9,619 | 1,084 / 5,002 / 68 | 239 |
| SF | 1,171 | 49,515 | 12,537 | 11,699 | 1,447 / 4,917 / 230 | 754 (incl. outsides and caps) |

Polygons made per element type, London / SF: road (type 0, incl. its curb faces) 2,863 / 4,356;
sidewalk (type 1, incl. curbs and caps) 10,135 / 9,931; ground strip 212 / 68; crosswalk
697 / 648; intersection fan 3,879 / 3,217; ground fan 3,877 / 5,115; wall light 9,618 / 12,537;
divided road (incl. curbs and median walls) 330 / 1,190; tunnel 239 / 754; roof 9,619 / 11,699.
Dropped for a zero cross product: London 61 (type 1: 15, type 6: 31, type 7: 2, type 9: 13), SF 326
(type 0: 189, type 1: 25, type 2: 2, type 6: 31, type 7: 2, type 8: 2, type 9: 75). Triangle-mode
rooms make 197 (London) and 2,125 (SF) polygons. These counts depend on doing the curb and wall
heights in single precision as "Arithmetic" says (with double-precision heights, 143 SF triangles
and one London sidewalk polygon change between the exact-(0, 1, 0) normal and the dropped case);
the engine build and this checker agree exactly.

- Top-down height maps `work/check/coll_london.png`, `coll_sf.png` (surfaces coloured by kind,
  curbs yellow, walls taller than 0.5 m red): roads, sidewalks, parks, roofs and building walls
  line up with the drawn city; deep water (Thames, the bay) has no polygons.
- Ground queries across a London street (room 819, z = −60, from x = 52 to 80): grass at y = 0
  (code 4), sidewalk at y = 0 (code 8), a 0.15 m curb drop to the road at y = −0.15 (code 8), and
  back up at the far curb; normals exactly (0, 1, 0). Across an SF freeway (room 574): road at
  53.403, the median top 1 m higher at 54.403 (code 0), curb tops at 53.553. On an SF hill
  (room 451, z = 340): the road is one quad per section rising 21.5 m over 85 m (14.2°), normal
  (0, 0.969, 0.245), sidewalks 0.15 m above it. An Underground tunnel (London room 367): floor at
  −13.09 (a ground strip), walls 7 m high facing each other.
- Median data: London kind 2 with w1 = 38 (0.15 m) ×14, kind 1 ×15; SF kind 3 with w1 = 256
  (1 m) ×66, kind 2 ×27, kind 1 ×6. Tunnel clearances (word 2 / 256): 7, 3, 2, 1 m; the last two
  collide at 3 m.
- Texture names match `materials.csv` exactly in both cities (no case-only matches), and the
  slot codes used are 0, 2, 4, 5, 8.
