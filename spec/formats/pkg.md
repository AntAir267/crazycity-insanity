# Models: .pkg packages and .mtx pivot files

Every 3D object in MM2 (cars, traffic, props, buildings, the sky, pedestrians, HUD arrows) is a
`.pkg` file in the `geometry/` folder of the archives (see `ar.md`). A package holds one object:
its meshes at several levels of detail, the materials those meshes use, and for a few city
objects a list of attached props. Small `.mtx` files next to the package give the pivot point
of individual parts. How a car is put together from its parts is in `car-model.md`; how a
texture name becomes an image is in `tex.md`.

All numbers are little-endian. `u8/u16/u32` are unsigned integers, `f32` is an IEEE float.

## Coordinate system and units

- Right-handed, **+Y up**, units are **metres** (the VW Beetle body is 4.0 long, 1.85 wide).
- Vehicles face **−Z**. **+X is the vehicle's right-hand side**, −X its left. (Evidence: the
  steering wheel of every US car sits at −X, while the British cars — London cab, Aston Martin
  DB7, old Mini — have it at +X; headlights are at negative Z.)
- A vehicle's origin is on the centre line, roughly at road height: the bottoms of the wheels
  are within a few centimetres of y = 0 (the 4x4's are 0.11 below). See `car-model.md` for how
  to stand a car on the ground.
- Triangles are **counter-clockwise when seen from their front side** (the cross product of
  edge 1→2 and edge 1→3 points the same way as the vertex normals, for every triangle checked).
  MM2 draws with back-face culling, so the back side of a triangle is invisible.
- Matrices stored in files are 12 floats: the object's X axis, Y axis, Z axis (each 3 floats)
  and then its position. A point p in the object's space lands at
  `p.x·Xaxis + p.y·Yaxis + p.z·Zaxis + position`.
- Texture coordinates (u, v): u runs across a texture row, v = 0 is the **first row stored**
  in the texture file (see `tex.md`). Values outside 0..1 repeat unless the texture asks for
  clamping.

## Container

| Offset | Size | Contents |
|---|---|---|
| 0 | 4 | magic: ASCII `PKG3` (or `PKG2`, see below) |
| 4 | — | file entries, one after another, until the end of the package |

Each file entry:

| Size | Contents |
|---|---|
| 4 | ASCII `FILE` |
| 1 | name length *n* (u8), counting the terminating NUL |
| *n* | name, Latin-1, NUL-terminated |
| 4 | body size in bytes (**PKG3 only**) |
| size | body |

A reader can walk a PKG3 package by sizes alone. Name comparisons are case-insensitive
(`BODY_H` and `body_H` are the same name).

**PKG2** is an older layout with no body-size field: the reader must parse each body to find
where the next entry starts (geometry and material bodies are self-delimiting, see below).
Only two shipped packages use it: `sky_dawn.pkg` and `vpdb731.pkg` (an unused car). MM2 can only
use PKG2 where it reads entries strictly in order without skipping; cars and city objects
require PKG3 (the jump to `shaders`, rule 4 below, refuses a PKG2 package and MM2 stops with an
error). A new engine may simply accept both.

Body kinds, chosen by the entry name:

| Name | Body |
|---|---|
| `shaders` | material table (one per package, after all meshes) |
| `xrefs` | attached props (only some city objects) |
| `offset` | 3 floats, ignored by the game (see below) |
| anything else | a mesh (geometry block); the name says which part and level of detail |

Typical order: all meshes, then `shaders`, then `xrefs` if present, then `offset`.

## Mesh (geometry block)

A mesh is a list of **batches**. A batch is one vertex list plus one index list drawn with one
material.

Header:

| Size | Contents |
|---|---|
| 4 | batch count *B* (u32; only values below 128 are valid, see note) |
| 4 | total vertex count over all batches (informational) |
| 4 | total index count over all batches (informational) |
| 4 | batch count again (ignored) |
| 4 | vertex format flags *F* (below) |

Then *B* batches:

| Size | Contents |
|---|---|
| 4 | strip count *S* (always 1 in shipped data) |
| 4 | material index (only the low byte counts) into the selected material set |
| … | *S* strips |

Each strip:

| Size | Contents |
|---|---|
| 4 | primitive code (always 3; the game ignores it) |
| 4 | vertex count *V* (at most 65,535) |
| *V* × vertex size | vertices |
| 4 | index count *I* (at most 65,535; a multiple of 3) |
| *I* × 2 | indices, u16, each < *V*, counted from the start of this strip's vertices |

Every strip is drawn as an **indexed triangle list**: indices 0,1,2 form the first triangle,
3,4,5 the second, and so on, regardless of the primitive code. If a batch had more than one
strip, MM2 would keep only the last one; a reader should do the same.

Note: if bit 0x80 of the first word is set, MM2 reads a different, header-only form (low 7 bits =
batch count, then a u32 format word, then one material-index byte per batch, and no vertex
data). No shipped file uses it; a reader may reject it.

### Vertex format

*F* uses the Direct3D 7 flexible-vertex-format bit values. Fields appear in this order, each
only if its bit is set:

| Bit | Field | Bytes |
|---|---|---|
| position (low 4 bits = 0x2) | x, y, z (f32) | 12 |
| 0x10 | normal nx, ny, nz (f32), unit length | 12 |
| 0x20 | reserved word | 4 |
| 0x40 | vertex colour: bytes R, G, B, A (no shipped mesh has one) | 4 |
| 0x80 | second colour (specular), same layout | 4 |
| 0x100 | texture coordinate u, v (f32) | 8 |
| 0x200 | second texture coordinate u, v (f32) | 8 |

Other position codes in the low 4 bits (0x4 = x,y,z,w; 0x6/0x8/0xA/0xC/0xE = x,y,z plus 1–5
blend weights, 16/20/24/28/32 bytes) are size-legal but never used; reject them.

Shipped data uses exactly two formats:

| *F* | Layout (32 or 20 bytes) | Meshes |
|---|---|---|
| `0x112` | position, normal, uv | 4,606 |
| `0x102` | position, uv (no normal) | 68 |

### Lighting of a mesh

- A mesh **with** normals is lit with the material (below) and the scene's lights.
- A mesh **without** normals is drawn unlit: colour = texture × vertex colour, where the vertex
  colour is white unless the mesh has the 0x40 colour field.
- Vehicle shadow and light-glow parts are always drawn unlit even though they have normals
  (list in `car-model.md`).

### Bounding radius

MM2 gives every part a radius for visibility tests: for each level of detail, the largest
distance from the part's origin to any vertex; the part's radius is the largest of those over
its levels.

## Materials (`shaders`)

A package carries one or more **material sets**, each with the same number of materials. A
batch's material index picks a material from the set currently in use. Sets are how MM2 does
paint colours on cars, time-of-day/weather looks on the sky, and day/night looks on buildings;
the choice of set belongs to the object's owner (cars: `car-model.md`).

Header:

| Size | Contents |
|---|---|
| 4 | set count in the low 7 bits; bit 0x80 set = **compact** material records |
| 4 | materials per set *M* |

Then set 0's *M* materials, set 1's *M* materials, and so on.

One material record:

| Size | Full form | Compact form |
|---|---|---|
| 1 | texture name length *n* (0 = untextured) | same |
| *n* | texture name, NUL-terminated, no extension | same |
| 16 / 4 | diffuse colour r,g,b,a (4 × f32) | diffuse (4 bytes r,g,b,a, each /255) |
| 16 / — | ambient colour (4 × f32), **discarded** | absent |
| 16 / 4 | specular colour | specular (4 bytes /255) |
| 16 / 4 | emissive colour | emissive (4 bytes /255) |
| 4 | power (f32) | power (f32) |

Shipped data: 295 packages use the full form, 741 the compact form.

After reading, MM2 adjusts the colours:

1. **Full form only**: every component of diffuse, specular and emissive is snapped to steps of
   1/32: a value below 0.05 becomes 0, a value above 0.95 becomes 1, anything else becomes
   `floor(v × 32) / 32`. The compact form is used as read (byte / 255).
2. **Untextured materials**: diffuse r, g, b are multiplied by a global factor: 1.0 normally,
   0.5 for anything loaded while a city is set to night (time-of-day setting 3). Alpha is not
   changed.
3. Ambient is set equal to diffuse (all four components). The file's ambient is never used.

What the fields do when drawing:

- **diffuse, ambient (= diffuse), emissive** feed standard per-vertex lighting
  (colour = emissive + ambient × scene ambient + diffuse × Σ lights), then × texture.
- **diffuse alpha** below 1 makes the batch translucent (see alpha below).
- **specular** is never switched on by the game; it can be ignored.
- **power** is *not* a specular exponent in practice: it is the strength (0..1) of the
  environment-reflection pass MM2 draws over cars at the highest detail level (`car-model.md`).
  0 means no reflection.
- **texture**: resolved by name (`tex.md`). If it cannot be found the batch is drawn untextured
  with its material colour.

### Translucency

A batch is drawn alpha-blended (source alpha / inverse source alpha) when its material's diffuse
alpha is not 1, or its texture has an alpha channel (`tex.md`). MM2 has one switch for blending
and the alpha test together: whenever blending is on, texels whose alpha is not above the current
cut-off are discarded (0 by default, so only alpha 0; city objects use 100, some props 140 —
`inst.md`). Quirk: once one batch of a mesh needs blending, MM2 keeps blending switched on for the
rest of that mesh's batches. Opaque texels look the same either way, so a new engine need not copy
this.

For city objects MM2 decides once, when the model is loaded, whether it may blend at all, by looking
at material set 0 only: if no material of set 0 needs blending, the object is never blended or
alpha-tested, even when drawn with another set. (No shipped city object is affected.)

### Set count limit

MM2 can be told to load only the first *k* sets (the rest are parsed and dropped). For vehicles
it loads all of them. A set index chosen by an owner is always taken modulo the number of sets
loaded.

## `xrefs`

Props attached to a city object (street lights, doors, trees). Used only by the city instance
loader.

| Size | Contents |
|---|---|
| 4 | record count *N* |
| *N* × 80 | records |

Record:

| Offset | Size | Contents |
|---|---|---|
| 0 | 48 | placement matrix (12 f32, layout in "Coordinate system") relative to the owning object |
| 48 | 32 | prop name, NUL-padded (a prop package name such as `sp_light_red_f`) |

The prop's world placement is its record matrix followed by the owning object's world matrix.
MM2 skips (with an error message) a record whose combined matrix has an all-zero axis or a
pair of axes whose dot product is above 0.01, and re-normalises any axis whose squared length is
outside 0.97..1.03. A prop name that has no package is skipped with an error. The prop then keeps
only its position and its turn about the vertical axis, uses the owner's variant number for its
material set, and joins the room that contains its position (`inst.md`).

## `offset`

Three f32. It records where the object sat in the artist's scene when it was exported (for city
objects it equals the object's `(null).mtx` origin; for cars it happens to equal the origin of
the last part in the file). MM2 never reads it.

## Part names and levels of detail

A mesh name is `<PART>_<LOD>`, or just `<LOD>` for single-part objects (most city props and
buildings), where `<LOD>` is:

| Suffix | Meaning |
|---|---|
| `H` | high detail, used close up |
| `M` | medium |
| `L` | low |
| `VL` | very low, used far away |

Rules MM2 applies when it loads a part (names are matched case-insensitively):

1. It asks for `<PART>_H`, `<PART>_M`, `<PART>_L`, `<PART>_VL` in that order. Any may be missing.
2. **Missing higher levels are filled from the next lower one**: no L → use VL; no M → use L;
   no H → use M (applied in that order, so a part with only `_L` is used for L, M and H). Missing
   lower levels stay missing: the part is simply not drawn at those distances.
3. **Order matters, and one stray entry hides the rest.** MM2 reads a package front to back and
   keeps one *current* entry (at first, the first entry). The owner (car, trailer, prop…) asks for
   its parts in a fixed list order (lists in `car-model.md`; city objects in `inst.md`), each part
   as its four names in the order of rule 1. A request is met only if the current entry has
   exactly that name (case-insensitively); MM2 then reads that entry and the next one becomes
   current. Otherwise the request fails (that level of that part is missing) and the reader
   **does not move**. So an entry that is not the one being asked for when its turn comes stays
   current for good: an unknown name, a level suffix other than the four, a part or level stored
   after its turn in the list, or levels stored out of H, M, L, VL order. Every request after it
   fails, so **all later parts of the list are missing**, even those whose entries follow in the
   right order; reading goes on only at rule 4. Put simply: walk the request list once, taking
   the current entry whenever it is the one asked for; the first entry the walk never takes ends
   the loading of meshes.
   - Example: a car package with entries `BODY_H`, `BODY_M`, `WHL0_H`, `FOO_H`, `WHL1_H`, …
     loads `BODY` (H, M) and `WHL0` (H) only: `FOO_H` is never asked for, so `WHL1` and every
     later entry stay unread.
   - An entry stored too early (ahead of entries of parts listed before it) is taken at its
     turn, but the parts listed before it find it current and are missing, and their own entries,
     met after their turn, then block everything that follows.
   - Required parts that end up with no level at all make MM2 stop with an error: a car's `BODY`
     and `WHL0`, a traffic car's `BODY` and `WHL0`–`WHL3`, a trailer's `TRAILER`, `TWHL0` and
     `TWHL1`, the object's own levels (`H` … `VL`) of a breakable prop or traffic light, and
     each `BREAKnn` part such an object's prop data asks for (`BREAK01` up to its `NumParts`).
   - For a city object (a building, prop, traffic light) a blocking entry loses its later levels
     and parts (for example the `BREAKnn` parts of a breakable prop, which is then an error, or a
     traffic light's glow meshes), but its materials and `xrefs` are still read (rule 4).

   Every shipped package follows its owner's order; the only entries never taken are
   `vpcentury_trailer`'s `TWHL4`/`TWHL5`, which come last before `shaders`, so they block nothing.
   A new engine that looks parts up by name loads everything MM2 loads, plus parts MM2 loses
   behind a blocking entry; for fidelity with odd mods it should apply this rule (and drop the
   parts after the first entry not taken).
4. After the meshes MM2 jumps forward to `shaders`, from the current entry and by the entries'
   sizes (so a blocking entry and everything up to `shaders` are passed over). A package without
   `shaders` is an error (MM2 stops). `xrefs` is read only if it is the entry right after
   `shaders`; `offset` is never read.

Part names found in the shipped packages:

| Kind of package | Parts (LODs present) |
|---|---|
| Player cars `vp*.pkg` | BODY (H M L VL), SHADOW (H M), HLIGHT, TLIGHT, RLIGHT, BLIGHT, SLIGHT0/1, SIREN0/1 (L), WHL0–WHL3 (H M L), WHL4/5, BREAK0–3, BREAK01, BREAK12, BREAK23, BREAK03 (H, some M), FNDR0/1 (H M), SRN0–3 (H), HEADLIGHT0/1 (H), TRAILER_HITCH (H) |
| Traffic `va_*.pkg` | BODY (H M L VL), SHADOW (H M), HLIGHT, TLIGHT, SLIGHT0/1 (L), WHL0–3 (H M L), WHL4/5 (bus), HEADLIGHT0/1 (H) |
| Trailers `vp*_trailer.pkg` | TRAILER (H M L VL), SHADOW (H M), TLIGHT (L), TWHL0–5 (H M L), TRAILER_HITCH (H) |
| Cockpits `vp*_dash.pkg` | DASH, ROOF, WHEEL, SPEED_NEEDLE, TACH_NEEDLE, DAMAGE_NEEDLE, GEAR_INDICATOR (all H) |
| City objects and props | H, M, L, VL; breakable props add BREAK01, BREAK02 … (two digits) with their own LODs; traffic lights add REDGLOWDAY/NIGHT, YELLOWGLOWDAY/NIGHT, GREENGLOWDAY/NIGHT, WALK_DAY/NIGHT, NOWALK_DAY/NIGHT (H) |
| Pedestrians | `pedmodel_*.pkg`: H only (animation is elsewhere); `pedanim_*.pkg` hold only `shaders` and `offset` |

## .mtx pivot files

A part that has to be placed or moved on its own has a pivot file
`geometry/<package>_<PART>.mtx` (for example `vpbug_WHL0.mtx`, `vpsemi_trailer_TWHL2.mtx`).
For an object without part names the file is literally `<package>_(null).mtx` (MM2 builds the
name from a missing part string). Case-insensitive like everything else.

Always 48 bytes, 12 f32. For almost every part it is not a matrix but a box and an origin (table
below). The one exception is the live suspension parts of a car (`SHOCK`, `ARM`, `SHAFT`,
`AXLE`, none shipped), whose 48 bytes are read as a placement matrix in the layout of "Coordinate
system" (`car-model.md`, "Live suspension parts").

| Floats | Meaning |
|---|---|
| 0–2 | bounding box minimum x, y, z |
| 3–5 | bounding box maximum x, y, z |
| 6–8 | box centre |
| 9–11 | **origin: where the part's own (0,0,0) goes in the parent's space** |

The part's mesh is stored relative to its own pivot, so the part is drawn at the parent's
transform plus a translation by the origin (no rotation in the file). Exporters wrote two
styles and a reader must not care which:

- box given in the part's own space, centre = (0,0,0), origin = placement (wheels, headlights,
  sirens, hitches, some breakables) — 335 files;
- box given in the parent's space, centre = origin = placement (most breakables, city
  `(null)` files) — 383 files.

Many cockpit parts have origin (0,0,0) (their meshes are already in place; the cockpit view
positions them itself). Uses of
the box: wheel radius = (max.y − min.y) / 2 and width = max.x − min.x (vehicle physics); simple
box collision bounds use size = max − min around the centre.

## Checked against

`tools/formats/pkg.py` over all of `mm2core.ar`'s `geometry/` (1,040 `.pkg`, 819 `.mtx`):

- 1,036 packages parse completely (1,034 PKG3, 2 PKG2); every PKG3 body is consumed exactly to
  its stated size. The 4 that fail are broken in the shipped archive: two 0-byte files
  (`wf_hse_baywin03_stone_wht_5s_12_l.pkg`, `ww_bldg1_stone_tan_t6_20_l.pkg`), a truncated test
  file `thing.pkg`, and `vpvw_dune.pkg` (truncated inside `WHL1_L`; no car uses it).
- 4,674 meshes, 12,269 batches; every batch has 1 strip with primitive code 3; every index count
  is a multiple of 3 and every index is in range; total vertex/index counts in the headers
  always match. Largest strip: 1,413 vertices, 2,880 indices; most batches in one mesh: 31;
  most materials per set: 41; every material index is below the set size.
- Material blocks: set counts 1–48 (the sky has 16, pedestrians 48, `vpbug` 4 × 18).
- 33 packages carry `xrefs` (e.g. `bl_pwrplant_l`: 4 × `sp_light_red_f`).
- `.mtx`: all 819 are 48 bytes; centre = midpoint of min/max in 817.
- `vpbug_WHL0.mtx` = box (−0.128, −0.336, −0.336)…(0.128, 0.336, 0.336), centre 0, origin
  (−0.754, 0.307, −1.184): wheel radius 0.336, width 0.255, front-left.
- Entry order (rule 3), simulated with the owners' request lists over every city-object package
  of `mm2core.ar` (fixed objects, breakable props with their `BREAKnn` parts, traffic lights):
  every entry before `shaders` is taken; the vehicle packages were checked the same way
  (`car-model.md`). Only the broken `thing.pkg` fails.
- Winding: all 918 triangles of `vpbug.pkg` (and all of `vpcop`, `va_bus_f`, `cl10`) are
  counter-clockwise against their normals.
- Assembled renders of `vpbug`, `vpcop`, `vp4x4`, `vpcab`, `vppanoz`, `vpauditt`, `vpsemi`
  (+ trailer), `vpcentury` (+ trailer), `va_bus_f`, `va_limo_f` in `work/check/` look right:
  wheels on their hubs, breakable parts in place, readable logos.
