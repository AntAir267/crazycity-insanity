# Collision bounds (`.bnd`, `.bbnd`, `.ter`)

A **bound** is the shape the physics uses for an object: a car body, a landmark building, a
prop. Bounds are separate from the drawn model (`pkg.md`) and much simpler (a car body is 8 to 18
corner points). They live in `bound/` and are named `<model>_BOUND` plus an extension:

| File | What it is |
|---|---|
| `bound/<name>_BOUND.bnd` | text bound (always present; 525 files) |
| `bound/<name>_BOUND.bbnd` | the same bound in binary (324 files; every one has a matching `.bnd` with identical content) |
| `bound/<name>_BOUND.ter` | a terrain acceleration grid for a big landmark bound (184 files); see "Terrain grids" |

Which objects use which file, and how a bound is placed in the world, is in
`spec/physics/collision.md` ("Bounds in use"). Lookups are case-insensitive like every other file
(`ar.md`). `bound/nathan.bfg` and `bound/unopt.bnd` are leftovers and are never loaded.

## Geometry bound

A geometry bound is a list of points, a list of physical materials and a list of flat polygons
(triangles and quads). Coordinates are in the same model space as the object's package
(`pkg.md`: right-handed, +Y up, metres; a car faces −Z). There is no separate pivot: the bound's
origin is the model's origin, so it is placed with exactly the same matrix as the model.

### Text form (`.bnd`)

Plain text read as whitespace-separated tokens (spaces, tabs and line ends are equivalent). The
tokens must appear in this order; a wrong keyword is an error.

```
version: 1.01
verts: <NV>
materials: <NM>
edges: <NE>
polys: <NP>
v <x> <y> <z>                 (NV lines)
mtl <name> { ... }            (NM blocks, see "Material blocks")
edge <a> <b>                  (NE lines)
tri <a> <b> <c> <m>           (NP lines in total, each tri or quad)
quad <a> <b> <c> <d> <m>
```

- Numbers are read as decimal text into double precision (C `atof`) and stored as single
  precision (rounded to nearest); integers as C `atoi`.
- The version is read as a number and must equal 1.01 in single precision; anything else rejects
  the file.
- `NV` = 0 or `NP` = 0 rejects the file.
- `a b c d` are 0-based point indices; `m` is a 0-based index into this file's material list (only
  its low 8 bits are kept).
- `tri a b c m` makes the triangle (a, b, c).
- `quad a b c d m` makes the quad (a, b, c, d), except when **d = 0**: then it is stored as
  (b, c, 0, a). The reason: a stored polygon whose fourth index is 0 counts as a triangle, so a quad
  that ends in point 0 is rotated to keep all four corners. (If both a and d are 0 the result is the
  triangle (b, c, 0); no shipped file does this.)
- Anything after the last polygon is ignored.

### Binary form (`.bbnd`)

Little-endian.

| Size | Contents |
|---|---|
| u8 | version (1 in every file; not checked) |
| u32 | point count NV |
| u32 | material count NM |
| u32 | polygon count NP |
| NV × 3 × f32 | points |
| NM × 104 | materials: name (32 bytes, NUL-padded), f32 elasticity, f32 friction, effect name (32 bytes), sound name (32 bytes) |
| NP × 10 | polygons: 4 × u16 point indices, then u16 material index (low 8 bits kept) |

A fourth index of 0 means a triangle (no rotation is applied when reading; the files already
follow the rule above). There is no edge list in the binary form.

When both forms exist, which one the game reads depends on the caller (`collision.md`); their
contents are identical in all shipped data, so a loader may read either.

### Material blocks

A material block in a `.bnd` is the same syntax as `city/materials.mtl` (`city-files.md`):

```
mtl <name> {
    elasticity: <f>
    friction: <f>
    effect: <name>
    sound: <n or none>
  [ drag: <f>  width: <f>  height: <f>  depth: <f>
  [ ptxindex: <i> <i>  ptxthreshold: <f> <f> ] ]
}
```

The block may end after `sound:` or after `depth:`. Shipped bounds only use the short form.

**Bound materials are shared by name.** The game keeps one global material table per city
(`collision.md`, "Materials"). Loading a bound material looks its name up in that table
(case-sensitive):
- if a material of that name exists, the bound uses the existing one and **the values written in
  the bound file are ignored**. This is the usual case: every shipped bound names `default`,
  `grass`, `cobblestone`, `water`, `deepwater` or `sand`, which all exist before any bound is
  loaded (`default` is the built-in default material, not the `_default` entry of
  `materials.mtl`);
- otherwise a new material is added to the table with the values from the block (the text form
  fills the fields the block leaves out from the built-in defaults; the binary form only has
  elasticity and friction) and later bounds naming it get that one. In shipped data this happens
  only for `mud` (one landmark bound).

Cars replace all of their bound's materials with their own (`collision.md`, "The car's bound").

### After loading

The game derives these from the loaded bound; they are part of the behaviour (later sections
use them):

1. **Edge list.** Start with the file's `edge` lines in order. Then go through the polygons in
   order and, for each polygon, through its edges in order — the edge from the last corner to the
   first corner, then corner 0→1, 1→2, … — and append every edge (as the pair (from, to)) whose
   two points are not yet joined by an edge in the list in either direction. Shipped files have
   no `edge` lines, so the list is simply every distinct polygon edge.
2. **Edge numbers per polygon.** For each polygon and each corner k, the number (position in the
   edge list) of the edge from corner k to corner k+1 (wrapping).
3. **Polygon normal.** For corners v0, v1, v2: n = normalise((v2 − v1) × (v0 − v1)). It points to
   the side from which v0, v1, v2 run counter-clockwise (outwards on closed bounds). In single
   precision: a = v2 − v1, b = v0 − v1, n = (a.y·b.z − a.z·b.y, a.z·b.x − a.x·b.z,
   a.x·b.y − a.y·b.x), L = (n.y·n.y + n.x·n.x) + n.z·n.z (this order), k = 1/√L (0 if L = 0),
   n = n·k per component. (Same as city polygons, `city-collision.md`, without the flat shortcut.)
4. **Edge normals per polygon.** For each corner k, e = v(k+1) − v(k) and the inward edge normal
   normalise(n × e), summed (x² + y²) + z², then each component times 1/√L (0 if L = 0).
5. **Edge normal and edge cosine** (one each per edge of the edge list). For edge (a, b):
   scan the polygons in order; a polygon is *forward* if a and b are consecutive corners in its
   winding order (a then b, including last → first) and *reverse* if b then a. Remember the
   normal nF of a forward polygon and nR of a reverse polygon (a later match of the same kind
   replaces an earlier one), and stop as soon as one of each has been seen.
   - Neither found: edge normal (1, 0, 0), cosine −1.
   - Only one found: the other is its negation.
   - s = nR + nF; if |s|² < 0.000001: s = nR × (V[b] − V[a]); if that also has |s|² < 0.000001:
     s = (0, 1, 0). Edge normal = s·(1/√L) per component, L = (s.x² + s.y²) + s.z² (the same sum
     is the |s|² of both tests; 0.000001 is a single-precision constant).
   - With ev = V[b] − V[a]: if nR · (edge normal × ev) ≤ 0 (convex edge) the cosine is
     nR · edge normal; otherwise (concave) it is 2. Both dot products are summed
     (nR.z·u.z + nR.y·u.y) + nR.x·u.x (u = the other vector); crosses are the standard
     right-handed ones.
6. **Box:** min and max of the points per axis. **Centre** = (max − min)·0.5 + min per
   component (in this form, not (min + max)/2). **Radius** = distance from the centre to max (half
   the box diagonal): with d = centre − max, radius = √((d.z² + d.y²) + d.x²).
7. **Penetration allowance** = k·s, with s the smallest of the three box sizes (k·1 if
   s < 0.001), where k is the game's global "skin" value. k is 0 whenever a race is running (the
   game sets it to 0 when the race's physics is created, before the car is loaded), so in practice
   the allowance is 0 and plays no part; it is listed only because the original computes it.

The box, centre and radius are in model space.

## Terrain grids (`.ter`)

The format and use of terrain grids are in `spec/physics/collision-objects.md` ("Terrain bounds"),
next to the landmarks that use them.

## Checked against

`tools/formats/bnd.py` (dirty side) parses all 525 `.bnd` and 324 `.bbnd` files without error;
every `.bbnd` matches its `.bnd` point for point (to 1e-4), polygon for polygon and material name
for material name. Totals: 13,217 polygons in `.bnd` (4,958 triangles), all `edges: 0`;
materials named: `default` (every file), `grass` 55, `cobblestone` 43, `water` 6, `deepwater` 4,
`sand` 1, `mud` 1. Car bounds have 8–18 points; e.g. `vpbug_BOUND`: 16 points, 14 quads,
28 derived edges, box (−0.922, 0.245, −1.982) … (0.943, 1.565, 2.009), radius 2.299. Polygon normals of the car bounds point outwards.

The 184 `.ter` files (format in `spec/physics/collision-objects.md`) were written by the same
derivation: their edge lists equal the edge list rule above in order and direction for every
file, all 19,727 edge normals agree with item 5, and the cosines agree except 17 flat edges where
the convexity test is within rounding of 0 (1 versus 2).

