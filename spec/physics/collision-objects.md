# Placed objects: which collide, their bounds, gathering and probing

Objects placed by `.inst` (`inst.md`), attached package props (`xrefs`, `pkg.md`), street props
placed along roads (`city/<c>/propdefs.csv`, `proprules.csv`) and props of path sets
(`city/<c>/props.pathset`) are drawn models. Only some of them collide, each with a **bound**
(`spec/formats/bnd.md`). This page says which, with what shape, where, and how the car finds them
for body collisions and wheel probes, and how contacts between the car body and an object are
found ("Contacts").

## Which objects collide

| Object | Collides with the car body | Wheels drive on it | Bound |
|---|---|---|---|
| plain static `.inst` object (no kind bits) | **no** | no | none (even if `bound/<name>_BOUND.bnd` exists) |
| landmark (bit 24) | yes | yes, unless its header also has bit 26 (`inst.md`; no shipped record has it) | terrain bound if `bound/<name>_BOUND.ter` exists, else geometry bound from the `.bnd` |
| static spanning rooms (bit 29, not landmark) | yes | yes | geometry bound from the `.bnd`, scaled (below) |
| breakable prop (bit 25), xref prop, road-side prop, path-set prop | yes | only if its `CollisionType` has bit 0x20 | from its `tune/banger/<name>.dgBangerData` (below) |
| traffic-light pole (`../ai/traffic-control.md`) | yes, as a prop | only if its `CollisionType` has bit 0x20 (shipped: no) | from its `.dgBangerData` (shipped: capsules) |
| decals path set | no | no | — |
| moved prop (a broken prop's copy or piece, a thrown car part; `loose-bodies.md`) | yes, while it lies still; as a moving body while it moves | no | from its `.dgBangerData` |

So most buildings placed by `.inst` are only scenery: their sides are not solid. The solid city is
the PSDL (whose building walls and roofs do collide, `city-collision.md`), the landmarks and the
props.

Objects whose `.dgBangerData` file is missing are not created at all (neither drawn nor solid).

Shipped cities: London has 176 landmarks (58 with a terrain bound, 118 with a geometry bound),
13 spanning objects (`sw_subway_b4_10_l`), 65 xref props; SF has 88 landmarks (85 terrain,
3 geometry), 2 spanning objects (`nw_bwaytunnel_f`), 368 xref props. The `_ai.inst` lists hold
only `sp_stop_f` (a capsule-shaped prop).

### The geometry cache

Each model name (compared exactly, case-sensitive) loads its geometry once; whether that entry
has a bound is decided by the first object loaded under the name. Landmarks get their bound when
their entry has none yet. In practice every name is either always a landmark or never.

## Bounds and placement

All collision code treats object matrices as **orthonormal**: world → object coordinates by dot
products with the three axes (transpose), object → world by the matrix. Radii are never scaled.

### Landmarks

- Placement: position and the turn about Y only (`inst.md`, landmarks: X = (m₀₀, 0, m₀₂),
  Y = (0, 1, 0), Z = (−m₀₂, 0, m₀₀)). Every shipped landmark has |(m₀₀, m₀₂)| = 1.
- Bound: if `bound/<name>_BOUND.ter` exists, a **terrain bound**: its geometry is read from
  `.bbnd` if present, else `.bnd`, then the `.ter` (format below). Otherwise a geometry bound from
  the **text** `.bnd` (a `.bbnd` alone is not used). A file that fails to load gives no bound.
- The object's radius (for gathering and probes) becomes D if D > the model radius (else it stays
  the model radius), where D is measured from the origin to only **two** corners of the bound's
  box, its min corner and its max corner: |v|² = (v.x² + v.y²) + v.z² for each, D = √|min|² if
  |min|² > |max|², else D = √|max|². (Not the farthest of the eight corners: for 77 of the 251
  landmark names of the shipped cities that would give a different radius.)
- A landmark belongs to exactly one room: its `.inst` room.

### Static objects spanning rooms (bit 29)

- Bound: geometry bound from `bound/<name>_BOUND.bnd`. If any axis a of the placement matrix has
  |length² − 1| ≥ 0.01 (length² = (a.x² + a.y²) + a.z²), the bound is loaded again with every
  point multiplied per axis by the axis lengths (|X|, |Y|, |Z|), each √((a.x² + a.y²) + a.z²)
  (a separate copy for this object) and the object's matrix is replaced by
  an orthonormal one: X′ = normalise(Y × Z), then Y′ = normalise(Z × X′), then Z′ = normalise(Z)
  (Y, Z the original axes). Each normalise is a·(1/√L) per component (0 if L = 0), with
  L = (a.z² + a.y²) + a.x² for X′ and (a.x² + a.y²) + a.z² for Y′ and Z′. All 15 shipped ones are scaled.
- Rooms: the object is entered into every room returned by Touched(its `.inst` room, its position,
  its model radius, at most 32) (`city-collision.md`) — **but not into its own `.inst` room** (an
  original quirk; keep it). A car gathering objects sees each such object once per sample even if
  it is in several of the car's rooms.

### Props (breakable, xref, road-side, path-set)

- Placement: a full `.inst` matrix keeps the full matrix; compact `.inst` records, xref props and
  road-side props keep position and the turn about Y (like landmarks); path-set props follow
  their path set's flag. Xref matrices: the package's xref matrix P (rows P_X, P_Y, P_Z and
  translation P_T) is combined with the object's placement (axes X, Y, Z, position o) as
  C_i.c = Σ_k P_i.k·A_k.c with A_x = X, A_y = Y, A_z = Z and t_k = P_i.k·A_k.c summed as
  (t_x + t_y) + t_z for C_X.x and C_Y.x, (t_x + t_z) + t_y for C_Z.x, (t_y + t_z) + t_x for the
  other six axis elements; the position C_T.x = ((t_x + t_y) + t_z) + o.x,
  C_T.y = ((t_y + t_z) + t_x) + o.y, C_T.z = ((t_y + t_z) + t_x) + o.z. The combined matrix is
  rejected if an axis has all three components 0, or if a dot product of two axes is
  **greater** than 0.01 (signed: a negative dot never rejects), with
  C_X·C_Y = (X.x·Y.x + X.z·Y.z) + X.y·Y.y, C_Y·C_Z = (Y.z·Z.z + Y.y·Z.y) + Y.x·Z.x,
  C_Z·C_X = (X.x·Z.x + X.z·Z.z) + X.y·Z.y (here X, Y, Z = the axes of C). Then each axis is
  normalised if its squared length L is outside [0.97, 1.03] (L < 0.97 or L > 1.03), as
  a·(1/√L) per component, with L = (X.z² + X.x²) + X.y² for X and (a.z² + a.y²) + a.x² for Y and
  Z. The xref prop's room is Find(position, the `.inst` room) (`city-collision.md`).
- The prop's position is its centre of gravity: p.c = placement position.c + (R·CG).c with
  (R·CG).c = (CG.z·Z.c + CG.y·Y.c) + CG.x·X.c (`CG` from its `.dgBangerData`), where X, Y, Z are
  the axes of the full matrix the prop is placed with: for an xref prop the combined (and
  normalised) matrix C, **before** only its turn about Y is kept. So the CG of an xref prop whose
  combined matrix is tilted is offset by the tilted rotation, while the prop afterwards stores
  only position p and X's x and z (170 of the 433 shipped xref props are tilted by more than
  0.01 in Y — lights and awnings, some lying on their side; their CG offsets differ from the
  turn-only rotation's by up to 1.45 m).
- Bound, by `CollisionPrim` (one bound per prop type, shared by all its copies), in coordinates
  relative to the prop's position (the CG):
  - 0 (also when the key is missing): geometry bound `bound/<name>_BOUND.bnd` (text) with every
    point moved by −CG; if the file is missing, as 1;
  - 1: a box of size `Size` centred at 0;
  - 2: a capsule ("hotdog") along the prop's Y axis, radius `YRadius`, length `Size.y`;
  - 3: a sphere of radius `YRadius`.
  The prop's bound has one material: elasticity `Elasticity`, friction `Friction` from the
  `.dgBangerData`, the other fields as the built-in default.
- Shipped data: 992 prop types (`.dgBangerData`): `CollisionPrim` 1 (864), 0 (73), missing (18;
  2 of them have a `.bnd`, the other 16 use the box), 2 (37: street lights, traffic lights, poles,
  trees, parking meters, bollards, stop signs, freeway supports…). 13 of the prim-0 types have no
  `.bnd` and use the box.
- **Reading `.dgBangerData`** (text, `type: a`; the first two tokens after `type: a` are skipped,
  normally `dgBangerData {`). The values are read **in a fixed order**, most of them by position
  (the key token is skipped without being checked): `AudioId` (int), `Size` (3 floats), `CG`
  (3 floats), then, only if the next token is `NumGlows`, its int (else the glow count is 1),
  then that many `GlowOffset` vectors, then by position: Mass, Elasticity, Friction,
  ImpulseLimit2 (floats), SpinAxis, Flash, NumParts (ints), the `BirthRule { … }` block (particle
  data, not collision; its own fixed order: `../formats/particles.md`, "Prop data blocks"),
  TexNumber, BillFlags (ints), YRadius (float); then the optional keys `ColliderId`,
  `CollisionPrim`, `CollisionType` (ints), each read only if it is the next token.
  Defaults for what is not read: Size (0.2, 0.5, 0.2), CG 0, Elasticity 0.5, Friction 0.9, Mass 50,
  ImpulseLimit2 0, YRadius 0, CollisionPrim 0, CollisionType 0x10. A key-by-name reader gives
  the same values for every shipped file.
  (`ColliderId` is the prop's impact sound id, `../audio/tyre-impact-sounds.md`; `AudioId` is
  not used by any sound.)
- Kinds bits: a prop collides with the car body; its `CollisionType` bit 0x20 also makes wheels
  hit it (9 types: `giz_waterloo_l`, `giz_carferry01_l`/`_f`, `giz_pcar02_l`, `giz_bridge01_l`,
  `giz_bridge02_l`, `op_eros_l`, `sp_carferry01_f`, `sp_jumptrailer_f`).

## Terrain bounds (`.ter`)

A terrain bound is a geometry bound (from the `.bbnd`/`.bnd` of the same name) plus a flat grid
that lists, per cell, the polygons above it, and precomputed edge data. Little-endian:

| Type | Field |
|---|---|
| f32 | version, must be 1.1 |
| i32 | polygon count, must equal the geometry's (else the bound fails to load) |
| i32 | edge count E (replaces the geometry's derived edge list) |
| u8 | "hot edges" flag (0 in every shipped file) |
| 3 × f32 | size (box size) |
| 3 × i32 | grid cells W (along X), H (along Y, always 1), D (along Z) |
| i32 | cell count (= W·H·D) |
| i32 | total number of polygon references R |
| 3 × f32 | cell factors (W/size.x, 1, D/size.z) |
| 3 × f32, 3 × f32 | box min, box max |
| cells × u16 | index of each cell's first reference |
| cells × u16 | reference count of each cell |
| R × u16 | polygon indices |
| E × 2 × u16 | the edge list (point pairs) |
| polygons × 4 × u32 | per polygon, the edge number of each of its edges (low 16 bits) |
| E × 3 × f32 | edge normals |
| E × f32 | edge cosines |

After reading, the box min/max are recomputed from the points (they equal the file's), and the
box, centre, radius are derived as for any bound (`bnd.md`). The edge data in the file equal what
`bnd.md` "After loading" computes, except on a few back-to-back faces; **use the file's values**.
Likewise use the file's cell lists (cells are about 10 m; a cell lists every polygon whose flat
outline overlaps it, with a few exceptions from the exporter).

**Cell of a point** (x, z): ix = trunc((x − min.x)·factor.x), iz = trunc((z − min.z)·factor.z)
(the product rounded to float, then truncated towards zero to a 64-bit integer of which the low
32 bits are used as a signed integer; a non-finite value or one beyond ±2⁶³ gives 0), each
clamped to [0, W−1] / [0, D−1]; cell = iz·W + ix. Y is ignored.

**Cells of a segment** S→E (in the bound's coordinates):
- if S.x = E.x and S.z = E.z: if the point is inside the box in X and Z (inclusive), its one cell;
  else none;
- otherwise none if both ends are below the box min, or both above the box max, on any axis
  (X, Y or Z); if both ends are in the same cell, that cell; otherwise clip the flat segment to the
  box and walk the grid cells it crosses from S's end towards E's, keeping the non-empty cells in
  walk order:
  1. Clip, D = E − S, t0 = 0, t1 = 1, first in X, then in Z (axis a): if D.a = 0, none unless
     min.a ≤ S.a ≤ max.a; else inv = 1/D.a, t_hi = (max.a − S.a)·inv, t_lo = (min.a − S.a)·inv;
     if S.a < E.a: none if t_hi < 0 or t_lo > 1; t1 = t_hi if t_hi ≤ 1 and t_hi < t1;
     t0 = t_lo if t_lo ≥ 0 and t_lo > t0; else: none if t_hi > 1 or t_lo < 0; t0 = t_hi if
     t_hi ≥ 0 and t_hi > t0; t1 = t_lo if t_lo ≤ 1 and t_lo < t1. After each axis, none if
     t0 > t1. Then t0 += 0.0001 if t0 > 0 and t1 −= 0.0001 if t1 < 1.
  2. Grid coordinates of the clipped ends: gxᵢ = ((D.x·tᵢ + S.x) − min.x)·factor.x,
     gzᵢ = ((D.z·tᵢ + S.z) − min.z)·factor.z (i = 0, 1); the walk starts at ix = trunc(gx0),
     iz = trunc(gz0), each clamped as in "Cell of a point".
  3. Walk: before each visit, stop if the index iz·W + ix is ≥ the cell count or < 0 (ix and iz
     themselves are not clamped while walking, so a step past the side of the grid lands in the
     neighbouring row); a visited cell is kept if its list is not empty.
     - If trunc(gx0) = trunc(gx1) (unclamped): visit, then step iz by +1 while iz < gz1 if
       E.z > S.z, else by −1 while iz > gz1 − 1 (each test after the step; iz as an exact number).
     - Else if trunc(gz0) = trunc(gz1): the same along X with ix, gx1 and E.x > S.x.
     - Else: bx = ix + 1 if E.x > S.x else ix, bz likewise; repeat: visit;
       tx = (bx − gx0)/(gx1 − gx0), tz = (bz − gz0)/(gz1 − gz0) (each difference rounded first);
       stop if tx > 1 and tz > 1; if tx < tz step ix and bx by ±1 (towards E), else step iz and bz
       (a tie steps Z).
Each polygon is tested at most once per query.

## Room lists

Every room keeps one list of the objects in it; gathering (below) and the object part of a probe
(`collision.md`, "The query", step 4) walk it from the front. MM2 keeps it in two parts:

- **Static objects** (landmarks, the room entries of spanning objects, plain statics) form the
  back part. When one is filed into a room it goes to the front of that back part, so the back
  part runs **newest first** (the last one loaded comes first).
- **Every other object** (props of every kind, traffic-light poles, traffic cars without a body,
  pedestrians, the cars' own entries) goes to the very **front** of the list whenever it is filed
  into a room — when it is created, whenever it moves into another room, and, for an untouched
  prop or light pole, again at every race reset (all of them in creation order, each moved out of
  its room and back in). So the front part also runs newest first, and after a reset the untouched
  props and poles stand in reverse creation order: the light poles (created after the `.inst`
  objects, `../ai/traffic-control.md`) before the `.inst` props, and the `_ai.inst` props before
  the `.inst` props. Moving objects (traffic cars, loose bodies and moved props,
  `loose-bodies.md`) that enter the room later go in front of all of these. A prop that breaks
  leaves its room's list until the next reset (`loose-bodies.md`, 3).

So at the start of a check run, a room's list is: its light poles in reverse creation order, then
its props in reverse load order, then its static objects in reverse load order. MM2 also creates the
street props (before every `.inst` object) and the path-set props (after the `_ai.inst` objects);
where they fall in this order is `../formats/street-props.md`, "Creation order".

## Gathering: which objects a moving car tests

Each physics sample, before collisions (`collision.md`, "The physics step"), the car collects at
most 32 objects:

1. The car's **reference point** is its centre of mass plus its body Y axis (1 m up along the car's
   own up direction); its **radius** is the radius of the car's `BODY` part (`pkg.md`, "Bounding
   radius"): the largest over its `BODY_H`, `BODY_M`, `BODY_L`, `BODY_VL` meshes of
   √(max over the mesh's vertices of (x² + y²) + z²), the vertices in model coordinates. Other parts
   (wheels, glass, shadow…) do not count. **Object radius** likewise: the radius of the object's
   own model part (its plain `H`/`M`/`L`/`VL` meshes, the part `inst.md` draws), measured from the
   model origin; for a landmark enlarged as described under "Landmarks". A prop's radius is
   measured from its model origin but used at the prop's position (its CG).
   The car's room is Find(reference point, previous room), updated after every car update.
2. Rooms: the car's room, then Touched(room, reference point, radius, 8).
3. In each room in that order, each object in the room's list order ("Room lists" above),
   except the car itself: take it if it can collide with the car body (landmark, spanning object,
   untouched prop, moved prop lying still: `loose-bodies.md`, 5.4; not an object that has a body
   at the moment) and the **trivial test** passes, until 32 are taken.
4. Trivial test: the object's position and radius against the car's reference point and radius:
   collide if |c₁ − c₂|² ≤ (r₁ + r₂)², with d = c₁ − c₂ and |d|² = (d.z² + d.y²) + d.x² (a NaN
   does not collide). For an untouched prop with a non-zero `YRadius`, the prop
   uses radius `YRadius` and its centre's X and Z are the prop position minus R·CG, its Y equal to
   the car's (a vertical cylinder test): c.x = p.x − ((CG.z·Z.x + CG.y·Y.x) + CG.x·X.x),
   c.z = p.z − ((CG.z·Z.z + CG.y·Y.z) + CG.x·X.z), where X, Y, Z are the rows of the prop's
   **stored** matrix (the turn-only matrix for props that keep only the turn about Y). For a
   tilted xref prop this centre is therefore not its placement position, and even for others it
   is computed, not taken from the placement (p − R·CG is not bit-equal to it).

Each gathered object is then collided with the car (contacts below), in gathering order, after
the car's city collision.

## Probing an object (wheels)

For the probe of `collision.md` "Ground queries", step 4, each object of the probed rooms is tested
when its kinds match the mask (wheels: landmarks without header bit 26, spanning objects and
props with `CollisionType` bit 0x20) and it is not the car:

1. Rooms are skipped quickly if none of their objects matches the mask.
2. Reject test, with o = the object's position (its matrix origin) and ρ = its radius:
   the segment's inverse length λ is 1/|S.y − E.y| if S.x = E.x and S.z = E.z, otherwise
   d = S − E, L2 = (d.z² + d.y²) + d.x², λ = 1/√L2 (0 if L2 = 0). M.c = (S.c + E.c)·0.5 (sum first);
   m = M − o; q = (0.5/λ) + ρ. Reject the object if ((m.z·m.z + m.y·m.y) + m.x·m.x) > q·q
   (a NaN comparison also rejects).
   (IEEE division without traps: S = E gives λ = ∞ and q = ρ; λ = 0 gives q = ∞, never rejected.)
3. Into object coordinates, with the object matrix rows X, Y, Z and origin o: w = S − o (per
   component), S′.x = (w.z·X.z + w.y·X.y) + w.x·X.x, S′.y = (w.z·Y.z + w.y·Y.y) + w.x·Y.x,
   S′.z = (w.z·Z.z + w.y·Z.y) + w.x·Z.x; E′ the same with w = E − o. (The rows are taken as
   orthonormal; nothing is renormalised.)
4. Test the bound in object coordinates with the current best T of the whole probe as the limit
   (a nearer hit replaces the result; on equal T the later test wins):
   - geometry bound: every polygon, from the **last to the first**, with the one-sided test below;
   - terrain bound: the polygons of the cells of S′ → E′ (cell order, then list order, each once)
     with the one-sided test.
5. On a hit, the result's T is the T found in object coordinates; the hit point p and normal n
   (object coordinates) go back to world:
   x = ((p.x·X.x + p.z·Z.x) + p.y·Y.x) + o.x, y = ((p.x·X.y + p.z·Z.y) + p.y·Y.y) + o.y,
   z = ((p.z·Z.z + p.y·Y.z) + p.x·X.z) + o.z, and the normal by the same formulas without o (not
   renormalised). The object becomes the result's object, and the material is the bound's material
   with the polygon's material index. (The world point is this transformed point, not
   S + T·(E − S) in world coordinates.)

**One-sided segment test** (objects only; the city probe is two-sided), for S′, E′ and a polygon
with corners c0 … and normal n, inward edge normals eⱼ:
1. w = S′ − c0 (per component); d_S = (w.z·n.z + w.x·n.x) + w.y·n.y; no hit if d_S < 0 (or NaN).
2. e = E′ − c0; d_E = (e.z·n.z + e.x·n.x) + e.y·n.y; no hit if d_E ≥ 0 (not strictly below; a
   NaN d_E passes).
3. T = d_S/(d_S − d_E); no hit if T > limit (equal, and NaN, pass).
4. D = E′ − S′. For each polygon edge j (corner j → j+1, wrapping): a = c(j+1) − cⱼ,
   g = a × D = (a.y·D.z − a.z·D.y, a.z·D.x − a.x·D.z, a.x·D.y − a.y·D.x), w = S′ − cⱼ,
   s = (g.z·w.z + g.y·w.y) + g.x·w.x, h = (g.z·eⱼ.z + g.y·eⱼ.y) + g.x·eⱼ.x; no hit if s ≠ 0 and
   h > 0 and s < 0.
5. Hit: point p.c = (E′.c − S′.c)·T + S′.c, normal n, penetration −d_E, front = yes.

Objects only block from the front of their polygons for wheels.

## Contacts

For each gathered object, after the car's city test, the car's bound is tested against the
object's bound; `collision-object-contacts.md` gives the contact generation for each kind of
bound (polygonal objects, terrain landmarks, box and geometry props, capsule props). The result
is an impact list with A = the car and B = the object, each impact resolved with weight 1/N for
that object's N impacts (`collision.md`, "Collision response"; an untouched breakable prop gets a
body for the test and uses the impulse-limit rule there; a moved prop lying still gets a body and
is resolved with both bodies, `loose-bodies.md`, 6.3).

## Checked against

`tools/physics/objects.py` (dirty side) classifies every record of the shipped `.inst` files by
these rules: London 1,808 plain statics (no collision), 118 geometry landmarks, 58 terrain
landmarks, 13 spanning objects, and 67 `sp_stop_f` capsule props in `london_ai.inst`; SF 3,673
plain statics, 3 geometry landmarks, 85 terrain landmarks, 2 spanning objects, 40 capsule props.
`tools/formats/bnd.py ter` parses all 184 `.ter` files to the last byte (version 1.1, polygon
counts equal, H = 1, cell count = W·D, reference total = sum of counts, box = point box, edge data
as `bnd.md`).

