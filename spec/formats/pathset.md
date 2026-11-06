# Path sets (`.pathset`, magic `PTH1`)

A path set is a list of named **paths**, each a short polyline of points with a placement mode and a
spacing. The city editor used it for everything placed "along a line": benches, bollards, trees and
crates set by hand (`props.pathset`), the flat road markings (`decals.pathset`), race barricades
(the race path sets) and the moving gizmos. This page gives the file layout, how a path turns into
objects, and the two city uses (props and decals). Street props generated along every street are
`street-props.md`.

Conventions as in `psdl.md`; float32 arithmetic with one rounding per operation, left to right as
written (`../physics/vehicle.md`, "Precision").

## Files

| File | Read | Contents |
|---|---|---|
| `city/<c>/props.pathset` | city load | props: each path's name is a prop model |
| `city/<c>/decals.pathset` | city load | decals: each path's name is a texture |
| `race/<c>/<race>.pathset` | city load, with the session's race name (`aimap.md`, "Which race file": `roam`, `race`*N*, `circuit`*N*, …) | race props (barricades, crates, jump trailers); a missing file is no error (no `roam.pathset` ships, so Cruise has none) |
| `race/<c>/<c>_<gizmo>.pathset`, `<c>_<gizmo>_<race>.pathset` (`bridge`, `ferry`, `parkedcar`, `sailboat`, `train`) | when the mode sets up its gizmos | moving bridges, ferries, parked cars, boats, trains: `../game/level-objects.md` |
| `city/<c>/audio_pathsets/*.pathset` | never | ambient sound paths of the editor (MM2's ambient sound comes from its music data) |
| `city/<c>/circuit<N>.pathset`, `city/sf/sf.pathset`, `backup.pathset`, `decals01`, `decals_bad`, `decals_good`, `city/sf/bak/`, `city/phys/`, `city/race0.pathset`, `race/london/circuitX.pathset`, `london_parkedcar_test.pathset` | never | leftovers |

## Layout

Little-endian.

| Size | Contents |
|---|---|
| 4 | ASCII `PTH1`; anything else: the file is not read (MM2 prints an error) and gives nothing |
| u32 | path count *P* (MM2 keeps at most 192 paths; refuse a file with more) |
| u32 | unused (often *P* − 1) |
| — | *P* path records |

Path record:

| Size | Contents |
|---|---|
| 32 | name, NUL-terminated (prop model or texture name; the gizmo files may prefix `open:` etc.) |
| u32 | point count *n* |
| u32 | unused |
| *n* × 16 | points: u32 editor value (often a room number; unused), then f32 x, y, z |
| u8 | placement **mode**: 0, 1 or 2 (others place nothing) |
| u8 | spacing in quarter metres; the spacing is byte × 0.25 m, and 5 m if the byte is 0 |
| 2 | unused |

The file ends after the last path (no trailing data in the shipped files). Read every value as
stored, without checking it. Three shipped files were damaged by a text-mode copy (each byte 0x0A
became 0x0D 0x0A): `race/london/blitz10.pathset`, `blitz11.pathset` and
`london_bridge_blitz10.pathset`. MM2 reads them byte by byte like any other file, so after the first
damaged byte their values are garbage (blitz11: path 1 is intact, path 2 gets mode 17 and places
nothing, path 3's point count runs past the end of the file). When the file ends inside a path,
stop: drop that path and all later ones (what MM2 does there is not reproducible).

## Placing a path's objects

A path yields a sequence of placements (axes X, Y, Z, position P, and whether the object keeps its
full matrix). Points are p₀ … pₙ₋₁; A = (0, 1, 0). Cross products are written out as MM2 groups
them:

- X × A = (A.z·X.y − X.z·A.y, X.z·A.x − A.z·X.x, A.y·X.x − A.x·X.y);
- Z × X = (X.z·Z.y − Z.z·X.y, Z.z·X.x − X.z·Z.x, Z.x·X.y − Z.y·X.x).

**Mode 0, one object per point.** For each point pᵢ: X = (1, 0, 0), Y = (0, 1, 0), Z = (0, 0, 1),
P = pᵢ; turn only.

**Mode 1, pairs: position and facing point.** For *i* = 0, 2, 4, … while *i* < *n* − 1: P = pᵢ;
X = (pᵢ₊₁.x − pᵢ.x, 0, pᵢ₊₁.z − pᵢ.z) (the height difference is dropped);
L² = X.z·X.z + X.x·X.x, k = 1/√L² (0 if L² = 0), X = (k·X.x, k·X.y, k·X.z); Z = X × A; Y = Z × X;
turn only. (With an odd *n* the last point is unused.)

**Mode 2, evenly along every segment.** For each segment *i* = 0 … *n* − 2 (from pᵢ to pᵢ₊₁):

1. d = pᵢ − pᵢ₊₁, L = √((d.z·d.z + d.x·d.x) + d.y·d.y).
2. X = pᵢ₊₁ − pᵢ (per component), L² = (X.z·X.z + X.x·X.x) + X.y·X.y, k = 1/√L² (0 if L² = 0),
   X = (k·X.x, k·X.y, k·X.z) (it keeps the segment's slope); Z = X × A; Y = Z × X.
3. The step: q = L / spacing (float32), then step = L / ⌊q⌋ (the floor is an exact whole number;
   the division is rounded to float32). If ⌊q⌋ = 0 the loop below does not run.
4. P = pᵢ. While L ≥ spacing (false for a NaN):
   - if each of X, Y and Z has at least one non-zero component, place an object with the
     **full matrix** (X, Y, Z, P); otherwise skip it (MM2 warns "bad path data");
   - P = (X.x·step + P.x, step·X.y + P.y, X.z·step + P.z); L = L − step.

So a segment of length L gets ⌊L/spacing⌋ objects (one fewer if rounding leaves the last L just
under the spacing), starting at its first point and spread evenly; its end point is the next
segment's start, and the path's last point gets nothing. On a slope the matrix is tilted (X along
the slope, Z horizontal with length |(X.x, X.z)|, Y = Z × X), not orthonormal.

## Path-set props

`props.pathset` and the race path set make props: for each path in file order, each placement in
order (an empty path gives nothing):

- The model is the path's name; the object exists only if `tune/banger/<name>.dgBangerData`
  exists (as for every prop). SF's `props.pathset` names `r4i_rails_f` (a rail texture) on 31 paths;
  they make nothing.
- Room: the room containing P, by the global search `Find(P, 0)`
  (`../physics/city-collision.md`, "Finding the room of a point"); the object goes to the front of
  that room's list. A point outside every room gives room 0, which is never drawn (shipped: no
  path-set prop or decal lands there).
- It is a breakable prop like the street props (`street-props.md`, "Creating a prop"): position
  p = P + R·CG with the full placement matrix, material set 0, same drawing rules (`BillFlags`),
  glows, collision bounds, breaking and resets. Modes 0 and 1 keep only p and X's x and z (turn
  only, like compact `.inst` props); mode 2 objects keep their full matrix (X, Y, Z) at p (like
  full-matrix `.inst` props, `inst.md` bit 25).

Creation order: after all `.inst` and `_ai.inst` objects, `props.pathset` first, then the decals,
then the race path set (`street-props.md`, "Creation order").

## Decals

Each path of `decals.pathset` with at least 3 points becomes one flat **decal**: a textured strip
lying just above the road (zigzag lines, junction crosses and crossing stripes in London, tram rails
in SF). Paths with fewer points are ignored. The mode and spacing bytes are not used.

**Geometry.** Vertex *i* = (pᵢ.x, pᵢ.y + 0.01, pᵢ.z) (0.01 as float32). The vertices p₀, p₁, p₂, …
form one triangle strip in the `psdl.md` sense (p₀ p₁ p₂, p₂ p₁ p₃, …): the editor stores the two
edges of the strip alternately (p₀, p₂, p₄, … one edge; p₁, p₃, … the other). Shipped decals all face
up (counter-clockwise seen from above).

**Texture coordinates.** u = 0 for even *i*, 1 for odd *i* (across the strip). v is the same for
both vertices of a pair *j* = *i* div 2: with w = √((e.z·e.z + e.y·e.y) + e.x·e.x), e = p₀ − p₁ (the
strip's width at its start), and for pair *j*: dⱼ = p₂ⱼ − p₀,
vⱼ = ⌊√((dⱼ.z·dⱼ.z + dⱼ.x·dⱼ.x) + dⱼ.y·dⱼ.y) / w + 0.5⌋ (the division and the + 0.5 rounded to float32,
then the floor), i.e. the texture repeats once per strip width along the strip, snapped to whole
repeats. With an odd point count MM2 reads past its table for the last vertex's v (undefined); use
the previous pair's v (one shipped SF decal has 37 points).
Example: London path 1, `decal_x_inter_l`, 4 points 15.7 m wide, second pair 17.2 m along: v = 0
and 1.

**Texture.** `texture/<name>` by the usual rules (`tex.md`), including the night and rain variants
(`night.md`, section 1; the London decals have `_fa` wet versions).

**Room.** The room containing the midpoint of p₀ and p₂, c = ((p₂.x − p₀.x)·0.5 + p₀.x, …), by
`Find(c, 0)`. Decals are filed in the room's list as **static** objects (with the landmarks and
plain statics, `../physics/collision-objects.md`, "Room lists"); they are never moved, reset or
knocked over.

**Collision.** None: decals are not collidable, neither for car bodies nor for wheels, and the AI
ignores them.

**Drawing.** Decals are drawn after the city surfaces and the static objects (buildings,
landmarks) and before the props, in a pass over the visible rooms whose distance is **below** the
room limit (`inst.md`); only MM2's software renderer skips the pass:

- vertex colour white (MM2 uses the room's colour, which is white because the shipped light maps
  are rejected, `city-files.md`);
- no lighting; fog on as for the city;
- depth test on, depth writes off;
- alpha-blended (source alpha, inverse source alpha), with MM2's default alpha test (reference 0);
- **depth range** 0.001 … 0.999 instead of 0 … 1 for this pass (window depth = 0.001 + 0.998 ×
  normalised depth), which moves the decal's depth slightly towards the camera wherever the
  normalised depth is above 0.5, i.e. everywhere except very close to the camera; together with the
  1 cm lift this keeps the decal in front of the road it lies on;
- back faces culled as for the city surfaces.

## Checked against

`tools/formats/props.py pathsets` reads every `.pathset` under `work/data` with this layout: all end
exactly after their last path except the three damaged files named above.

| | London | SF |
|---|---:|---:|
| `props.pathset` paths (mode 0 / 1 / 2) | 87 (21 / 11 / 55) | 144 (13 / 32 / 99) |
| objects placed / created (full matrix) | 1,033 / 1,033 (383) | 894 / 778 (305) |
| in room 0 | 0 | 0 |
| `decals.pathset` paths / decals | 84 / 79 | 52 / 48 |

- London path-set props: `sp_tree1_s` 387, `sp_bollard_stone_l` 171, `sp_bollard_black_l` 153,
  `sp_lightpark_f` 72, `sp_dumpstr_l` 60, `sp_cone_l` 38, `sp_oaktree1_s` 37, … `op_eros_l` 1 (the
  Piccadilly Circus statue, which wheels can drive on: `CollisionType` 48). SF: `sp_tree1_s` 320,
  `sp_barricadewood_f` 171, `sp_lightstreet_rt_f` 57, `sp_cleat_f` 42, `sp_cone_f` 30, freeway exit
  signs, banners, …
- Samples: London path 0 `sp_tree1_s`, mode 2, spacing byte 64 (16 m), 2 points 121.9 m apart:
  7 trees, step 17.4 m, X = (0.053422, 0, −0.998572), first at (346.2885, 4.8504, −930.6403) in
  room 886. Path 7 (mode 0): 2 trees at its 2 points. Path 23 `sp_dumpstr_l` (mode 1, 120 points):
  60 dumpsters.
- Decal textures: London `decal_zigzag_l` 43, `decal_x_inter_l` 21, `decal_rxwalk03_l` 15; SF
  `r4i_rails_f` 44, `r4i_railsX_f` 4. Every decal and every path-set prop lies in a room
  (`work/check/decals_<c>.csv`, `props_<c>.csv`, plots `props_<c>.png` with decals as white lines and
  path-set props as squares).
