# City geometry (`.psdl`, "PSD0")

`city/<city>.psdl` holds a city's streets, sidewalks, parks, water and the boxy buildings that line
the streets. It does not hold triangles. It holds a shared table of points plus, for every
**room** (a patch of the city: one block, one stretch of road, one intersection), a list of
compact **elements** ("a road strip through these points", "a building wall from here to there,
this high"). The game turns the elements into triangles, texture coordinates and colours. This
document gives the bytes and the exact geometry each element produces.

Detailed props and landmark buildings are separate models placed by `.inst` (`inst.md`); the other
files that make up a city are listed in `city-files.md`.

Shipped files: `london.psdl`, `sf.psdl` (the two cities), `sfai.psdl`, `variant.psdl` (older or
variant builds of SF that are not among the game's cities), `city.psdl` (a 13-room test patch).

## Conventions

- Little-endian. `u8/u16/u32` unsigned, `s16` signed, `f32` IEEE float.
- Coordinates, units and facing follow `pkg.md`: right-handed, +Y up, metres; a triangle is
  front-facing when its corners run counter-clockwise seen from the front, i.e. its normal
  (b − a) × (c − a) points to the front. In both cities +X is east and −Z is north (seen from
  above with +X to the right, −Z points up the page and the maps match the real cities).
- **Strip**: points p0, p1, p2, … make triangles (p0,p1,p2), (p2,p1,p3), (p2,p3,p4), (p4,p3,p5), …
  (every second triangle has its first two corners swapped so all face the same way).
  **Fan**: p0, p1, p2, … make (p0,p1,p2), (p0,p2,p3), (p0,p3,p4), …
- `P + h` means point P raised by h metres (only Y changes). `V[i]` is entry i of the point table,
  `F[i]` entry i of the height table.
- A texture coordinate is written (u, v). v = 0 is the first stored row of the texture (`tex.md`).
- "Distance" is the 3D distance unless it says "flat", which ignores Y.

## File layout

| Size | Contents |
|---|---|
| 4 | ASCII `PSD0` |
| u32 | 2 (any other value: refuse the file) |
| u32 | point count *NV* |
| NV × 12 | points: f32 x, y, z |
| u32 | height count *NF* |
| NF × 4 | heights: f32 (used by building elements; shared between rooms) |
| u32 | texture slot count *NT* (slot 0 is implicit and empty) |
| — | NT − 1 texture names, for slots 1 … NT − 1 (below) |
| u32 | room count *NR* (room 0 is implicit and empty) |
| u32 | index of the first room that is not a block (= number of block rooms + 1) |
| — | NR − 1 room records, for rooms 1 … NR − 1 (below) |
| NR × u8 | room flags, indexed by room number (entry 0 unused) |
| NR × u8 | prop rule per room (entry 0 unused; see "Prop rules") |
| 3 × f32 | city bounds minimum (x, y, z) |
| 3 × f32 | city bounds maximum |
| 3 × f32 | a reference point (not the box centre; not needed for drawing) |
| f32 | bounding radius |
| u32 | AI road count, then that many AI road records (below) |

The file ends right after the last AI road record.

### Texture names

Each name is a u8 length *L* followed by *L* bytes. *L* = 0 means an empty slot. Otherwise the
bytes are the name and its NUL (the NUL is counted in *L*). Names are looked up in `texture/` as in
`tex.md` (case-insensitive).

If a name ends in `-0` followed by three more characters (e.g. `s_thames-0009`), drop the last five
characters (`s_thames`): it is an animated texture, loaded through the frame rules in `tex.md`
(`s_thames-0001`, `-0002`, …).

Each slot also gets a physical material for driving: look the (shortened) name up in
`city/materials.csv` (header line, then `texture,physics` rows; `none` = no special material); the
material names are defined in `city/materials.mtl`. Physics materials are specified with the
physics; a renderer only needs one fact: a room whose first element selects a texture whose
material is `deepwater` counts as deep water (see "Room flags").

**Texture groups.** Elements never name a texture directly. A texture element selects a slot *t*,
and each kind of surface uses *t* plus a fixed offset (road surface *t*, its sidewalk *t* + 1,
crosswalk *t* + 2, …). The file is laid out so those neighbours exist, for example slots 1, 2, 3 =
`r1_stone256_l`, `swalk_stone_l`, `r1_stone128_l` (road, sidewalk, low-detail road).

### Room record

| Size | Contents |
|---|---|
| u32 | perimeter point count *NP* (at most 255; the game keeps it in a byte) |
| u32 | element word count *NA* |
| NP × (u16, u16) | perimeter: point index, neighbouring room |
| NA × u16 | element words |

The perimeter is the room's outline, a closed polygon of points from the point table, running
counter-clockwise seen from above. The room number in entry *i* is the room on the other side of
the edge from perimeter point *i* to point *i* + 1 (wrapping to 0); 0 means nothing is there. A room
contains the points whose flat position is inside its perimeter (rooms of flag 0x40 additionally
need the height to be inside the room's vertical range; see below).

### AI road record

The road network that traffic follows and props are placed along. Its full meaning belongs to the
AI spec; the layout is:

| Size | Contents |
|---|---|
| u32 | flags (0x100 = has a cable-car lane, 0x200 = subway; 0x40, 0x4 and 0x400000 decide where street props stand, `street-props.md`) |
| u8, u8 | lane counts *a* (one direction), *b* (other direction) |
| (a + b) × f32 | one lane position per lane, a fraction across the road (London 0.069–0.931, SF 0.071–0.929, e.g. 0.125, 0.375, 0.625, 0.875 for four lanes) |
| 2 × u8 | one byte per road end (start, end); values 0x00, 0x01, 0x80, 0x81 |
| 8 × u16 | point indices, four per road end (start end first); the first two of each four lie on the perimeter of the room at that end |
| u8 | room count *n* |
| n × s16 | rooms along the road, in order; the absolute value is the room number |

Only the flags, the room count and the rooms are used by the game. The lane positions, the two end
bytes and the point indices are read but never used: the only code that reads them (placing points
across lanes, and stop lights from the end bytes and points) is never called. A reader may keep
them as raw bytes.

## Room flags

| Bit | Meaning |
|---|---|
| 0x04 | block: buildings, parks, water between streets. Block rooms are numbered first (1 … header value − 1) |
| 0x08 | road: a stretch of street (or tunnel, bridge, freeway) |
| 0x10 | intersection |
| 0x02 | enclosed (tunnels): the room's space ends under its ceiling (see below); cameras and rain also test it (`../engine/cameras.md`, `night.md`), and the room walk of `cpvs.md` does not follow overlap links into it |
| 0x40 | stacked: the room overlaps another room seen from above (bridges, freeways, tunnels under streets); point-in-room also checks height |
| 0x80 | always counted as a neighbour when gathering nearby rooms (seen on bridges and freeways) |
| 0x20 | collision triangle mode (bridges, freeway ramps; 27 rooms in London, 77 in SF): road, sidewalk, median and ground-strip collision is made of triangles (`../physics/city-collision.md`, "Triangle mode"); no effect on drawing |
| 0x01 | never set |

Exactly one of 0x04, 0x08, 0x10 is set on every room.

**Vertical range** (used for the 0x40 point test and collision, not for drawing). Let *ymin* and
*ymax* be the lowest and highest perimeter points.
- Without 0x02: the room spans from *ymin* − 1 up to 1000.
- With 0x02: it spans from −1000 up to *ymax* + clearance, where clearance is 7 m, unless the
  room's first element (after one optional texture element) is a tunnel and flag 0x04 is clear, in
  which case it is the tunnel's word 2 (or, for a 10-word junction tunnel, its height word) ÷ 256.

**Deep water.** A room is marked as deep water ("water of death": cars that fall in are lost) when
its first element is a texture element whose slot's physical material is `deepwater` (the game
numbers materials from 1 with its built-in default first, so it tests for number 2, the first
material in `materials.mtl`), or when it is listed in `city/<city>.water` (`city-files.md`). The
texture rule is not applied to rooms that have both 0x02 and 0x04.

**Prop rules.** The per-room byte, if not 0, picks the rule pair `nNNleft` / `nNNright`
(NN = the byte, two digits) in `city/<city>/proprules.csv`. Only the byte of an AI road's **first**
room is used: it decides the street props along that whole road (`street-props.md`); the bytes of
other rooms have no effect.

## Element words

A room's element list is a sequence of elements. Each starts with a header word:

| Bits | Meaning |
|---|---|
| 0–2 | small count *s* |
| 3–6 | element type (0–12, table below) |
| 7 | last element of the room |
| 8–15 | always 0 |

If *s* = 0, the next word is the count *n*; otherwise *n* = *s*. Then come the element's data words.
Read until the element with bit 7; any words left after it are ignored (shipped files have a few
spare texture elements there).

| Type | Name | Data words |
|---:|---|---|
| 0 | road | 4*n* |
| 1 | sidewalk | 2*n* |
| 2 | ground strip | 2*n* |
| 3 | low wall | 4 |
| 4 | crosswalk | 4 |
| 5 | intersection fan | *n* + 2 |
| 6 | ground fan | *n* + 2 |
| 7 | wall light | 4 |
| 8 | divided road | 2 + 6*n* |
| 9 | tunnel | *n* (always 3 or 10) |
| 10 | texture | see below |
| 11 | building wall | 6 |
| 12 | roof | *n* + 2 |

**Type 10, texture**, sets the current texture slot to (*s* << 8) | *w*, where *w* is the word after
the header. When *s* = 0 that word was already taken as the count, so the element has no further
words; when *s* ≠ 0 it is one data word. (The game also skips a word when *s* = 0 but *w* has any of
bits 8–10 set; writers must keep *w* < 256 when *s* = 0. Shipped files always do.)

### Drawing state

Walk each room's elements in order, keeping:
- the current texture slot *t*, initially 0;
- whether *t* came from a texture element with value 0: then types 0, 1, 2, 4, 5, 6 and 8 draw
  nothing until the next texture element (types 3, 9, 11, 12 still draw, untextured). A divided
  road (type 8) skipped this way is stepped over by only 5*n* + 2 of its 2 + 6*n* data words, and the
  walk goes on from there, reading its last *n* words as the next element(s) (an original slip; the
  collision walk does the same, `../physics/city-collision.md`). No shipped room has a divided road
  after a texture element of value 0;
- the current wall light index *k*, initially 0 (set by type 7);
- the room colour *C* (see "Colour and render state").

### Ground supplied by landmarks

Some places have no PSDL surface on purpose, because a landmark model (`inst.md`) draws it:
- Block rooms with an empty element list (0 words): 41 in London, 53 in SF. Every one of them holds
  a landmark whose model is the block's ground: Westminster Abbey (room 57), Parliament (62), St
  Paul's (212), Hyde Park's lake (254), the Thames under Lambeth and Waterloo bridges (345, 351); in
  SF the Golden Gate ramps (1, 187), Coit Tower (73), the Hyatt plaza (141). No block relies on a
  plain static object for its ground.
- Texture slot 0 hides surfaces that a landmark replaces. London has 120 such elements: 117 in the
  Underground (station tunnels and the ramps down to them, rooms of flags 0x4A, 0x6A, 0x52, 0xD2
  or 0x08, mostly around −13 m), drawn by the `*_subway*` landmarks, and the road decks of Tower
  Bridge, Waterloo Bridge and Lambeth Bridge (rooms 857, 939, 970), drawn by the bridge landmarks
  of neighbouring rooms. SF has 1.

Where that landmark is not drawn, the room shows the clear colour. That happens in MM2 too when the
landmark has no mesh for the chosen level (many have no `VL`, see `inst.md`, "Missing levels"),
which only shows from far away or from above.

### Texture coordinates along a strip ("strip u")

Roads and some strips give every cross-section a u value from a chain of points q0 … q(m−1) (one
per section) and a second chain r0 … r(m−1) across the strip:

1. Segment lengths sᵢ = |q(i+1) − qᵢ| for i = 0 … m−2; total *S* = Σ sᵢ; largest *smax* = max(1, sᵢ…).
2. Width sum *W* = Σ |qᵢ − rᵢ| for i = 0 … m−2 (the last section is left out), average width *W*/m
   (dividing by m, not m − 1).
3. If m < 2, *W* = 0 or *S* < 0.1: all u are 0. Otherwise repeats *R* = max(1, floor(*S* ÷ (*W*/m) + 0.5)),
   scale *k* = *R* / *S*; if *k* · *smax* > 128, *k* = 127 / *smax*.
4. u0 = 0. Keep a running value *a* = 0; for each segment, if *a* > 0 subtract sᵢ from it, otherwise
   add sᵢ; then u(i+1) = *a* · *k*.

Step 4 makes u zig-zag (0 → forward → back towards 0 → …) instead of growing, so it stays small;
the texture is mirrored on every second segment. Keep this behaviour: it decides how road markings
line up.

**Wall u** (tunnels) is the same with a given repeat length *L* in place of the average width:
*R* = max(1, floor(*S* / *L* + 0.5)); all zero if *L* = 0 or *S* < 0.1; same cap and zig-zag.

**Flat mapping** with tile size *T* and origin point *O*: u = x/T − floor(O.x/T), v = z/T − floor(O.z/T).

## Element geometry (full detail)

These are the triangles at the highest detail level. Lower levels are in "Levels of detail".
Curb height is **0.15 m** throughout. "Dim" means the vertex colour at half brightness (see
"Colour and render state").

### Type 0: road

*n* sections of 4 point indices (a, b, c, d) across the road: a = left outer edge of the left
sidewalk, b = left curb, c = right curb, d = right outer edge ("left" is the left side when walking
from section 0 towards section *n* − 1). In the file, a and d are at sidewalk height, b and c at
road height.

- **Left sidewalk**, only if a ≠ b in section 0. Texture *t* + 1. u = strip u along a with
  widths to b. Strip over the sections: (aᵢ, u, 1), (bᵢ + 0.15, u, 0).
  **Left curb face**, dim, texture *t* + 1: strip (bᵢ + 0.15, u, 0), (bᵢ, u, 0).
- **Right sidewalk**, only if c ≠ d in section 0. Texture *t* + 1. u = strip u along c with widths
  to d. Strip: (cᵢ + 0.15, u, 0), (dᵢ, u, 1). **Right curb face**, dim: strip (cᵢ, u, 0), (cᵢ + 0.15, u, 0).
- **Road surface**, texture *t*. u = strip u along b with widths to c. mᵢ = midpoint of bᵢ and cᵢ.
  Two strips: (bᵢ, u, 1), (mᵢ, u, 0) and (mᵢ, u, 0), (cᵢ, u, 1). (v = 0 on the centre line, 1 at
  both curbs: the road texture is one half of the road, mirrored.)

### Type 1: sidewalk

*n* pairs (p, q): p on the road side (road height), q on the outer side (sidewalk height).
Texture *t* + 1, flat mapping with tile 4 m and origin p of the first pair.

- **Curb face**, dim: strip (pᵢ, uv), (pᵢ + 0.15, uv) (uv from the point's own x, z).
- **Surface**: strip (pᵢ + 0.15, uv), (qᵢ, uv).

**Curb end cap.** If *n* = 2 and the first pair is (0, 0) or (1, 1), the element is instead one
dim triangle closing the end of a curb, with A, B = the second pair and the same flat mapping
(origin A): first pair (0, 0): (A + 0.15, B, A); first pair (1, 1): (A, B, A + 0.15).

### Type 2: ground strip

*n* pairs (p, q). Texture *t*. u = strip u along p with widths to q. Strip (pᵢ, u, 0), (qᵢ, u, 1).
Used for alleys, paths, river banks.

### Type 3: low wall

Words: top height index *h*, texture scale index *g*, points A, B. A vertical wall from the ground
edge A→B up to the absolute height *top* = F[h]. Texture *t*; colour lit by the current wall light
(below). Scale σ = F[g].

u at B: *ue* = floor(flat distance(A, B) · σ + 0.5). vA = (A.y − top)·σ, vB = (B.y − top)·σ
(negative: v runs down from 0 at the top). Fan, with A′, B′ = A, B moved to height *top*:

| vA | vB | Fan |
|---|---|---|
| ≠ 0 | ≠ 0 | (A, 0, vA), (B, ue, vB), (B′, ue, 0), (A′, 0, 0) |
| = 0 | ≠ 0 | (A, 0, 0), (B, ue, vB), (B′, ue, 0) |
| ≠ 0 | = 0 | (A, 0, vA), (B, ue, 0), (A′, 0, 0) |
| = 0 | = 0 | nothing |

The wall faces to the right of A→B seen from above: its outward direction is
*n* = (−(B.z − A.z), 0, B.x − A.x). The game draws the wall only when the camera (`cpvs.md`, "The
camera point") is on that side or on the line: (C.x − A.x)(A.z − B.z) + (C.z − A.z)(B.x − A.x) ≥ 0,
in x and z only; then back-face culling applies as for every PSDL surface.

**Ends above the top.** All corners of the fan lie in the vertical plane through A and B, and each
of its triangles faces along *n* or against it: the triangle with corner B′ (the first one,
(A, B, B′), or the 3-point fan when vA = 0) faces *n* only if B is below the top, and the triangle
with corner A′ ((A, B′, A′), or the 3-point fan when vB = 0) faces *n* only if A is below the top.
A triangle that faces against *n* is **never visible**: from the outward side it is seen from
behind and culled, and from the inward side the camera test skips the whole wall. So a renderer
that only uses back-face culling must drop those triangles itself: with both ends above the top
(250 walls in London, 916 in SF) the whole wall; with one end above and one below (48 in London,
695 in SF) the triangle at the end that is above. What remains then needs no camera test (normal
back-face culling gives MM2's result).

### Type 4: crosswalk

Points p0, p1, p2, p3: p1→p0 is the near edge, p3→p2 the far edge. Texture *t* + 2. Width
*w* = |p0 − p1|, or |p2 − p3| if p0 = p1; length *l* = |p0 − p2|; r = l / w. Strip:
(p1, 0, 0), (p0, 1, 0), (p3, 0, r), (p2, 1, r).

### Types 5 and 6: intersection fan, ground fan

*n* + 2 point indices, one fan. Texture *t*, flat mapping with tile 8 m and origin the first point.
Type 5 is the road surface of intersections, type 6 everything else flat (grass, plazas, water).
The game only draws type 5 (and crosswalks) when the camera is not below the first point; a
renderer can ignore that test.

### Type 7: wall light

Words: light index *k* (0–63), wall height index, points A, B. Draws nothing. It sets the light
index used by the following low walls and building walls, and it is also the building's collision
wall (from A→B up to F[height index]; physics spec). *k* is the direction the walls face:
*k* = floor((atan2(nz, nx) + π/2) ÷ (π/32)) mod 64, where (nx, nz) is the outward direction of A→B
as for type 3.

### Type 8: divided road

Words: *w0*, *w1*, then *n* sections of 6 point indices (a, b, c, d, e, f): a left outer edge, b left
curb, c and d the left and right edges of the median, e right curb, f right outer edge.

- Sidewalks and curbs as type 0, with b as the left curb and (e, f) in place of (c, d) on the right:
  left only if a ≠ b, right only if e ≠ f (section 0).
- **Road surface**, texture *t*, u = strip u along b with widths to c. Strips (bᵢ, u, 1), (cᵢ, u, 0)
  and (dᵢ, u, 0), (eᵢ, u, 1) (both halves use the same u).
- **Median.** kind = *w0* & 0x3F; start cap = bit 6 of *w0*; end cap = bit 7; median texture slot
  *mt* = *w0* >> 8 (an absolute slot, not relative to *t*); *h* = *w1* ÷ 256. u = strip u along c
  with widths to d.

**Kind 1, painted median**, texture *mt* + 1: strip (cᵢ, u, 0), (dᵢ, u, *h*).

**Kind 2, raised median** of height *h*. Inner top points: Iᵢ = cᵢ + h·unit(dᵢ − cᵢ) + (0, h, 0);
Jᵢ = dᵢ + (−oₓ, o_y, −o_z) where o = h·unit(dᵢ − cᵢ) + (0, h, 0) (the same offset with its
horizontal part mirrored); Kᵢ = dᵢ + h·unit(cᵢ − dᵢ) + (0, h, 0). (unit is the 3D unit vector.)
1. Texture *mt*, dim: strip (cᵢ, u, 0), (cᵢ + h, u, h); strip (dᵢ + h, u, 0), (dᵢ, u, 1).
2. Texture *mt* + 1: strip (cᵢ + h, u, 0), (Iᵢ, u, 1); strip (Kᵢ, u, 1), (dᵢ + h, u, 0).
3. Texture *mt* + 2: strip (Iᵢ, u, 0), (Jᵢ, u, 1).
4. Start cap, texture *mt* + 3: strip (c₀, 0, 0), (d₀, 1, 0), (c₀ + h, 1, 1), (d₀ + h, 0, 1).
5. End cap, with the last section: strip (d, 0, 0), (c, 1, 0), (d + h, 1, 1), (c + h, 0, 1). The game
   does not select a texture for it: it uses *mt* + 3 if the start cap was drawn, else *mt* + 2.

**Kind 3, sloped median**, 1 m high (fixed; *w1* is not used), sides leaning in 0.4 m. Offsets
Lᵢ = 0.4·unit(dᵢ − cᵢ) + (0, 1, 0), Rᵢ = 0.4·unit(cᵢ − dᵢ) + (0, 1, 0); mirror(o) = (−oₓ, o_y, −o_z).
1. Texture *mt* + 1: strip (cᵢ, u, 0), (cᵢ + Lᵢ, u, 1); strip (dᵢ + Rᵢ, u, 1), (dᵢ, u, 0).
2. Texture *mt* + 2: strip (cᵢ + Lᵢ, u, 0), (dᵢ + mirror(Lᵢ), u, 1).
3. Start cap, texture *mt* + 3: strip (c₀, 0, 0), (d₀, 1, 0), (c₀ + L₀, 1, 1), (d₀ + mirror(L₀), 0, 1).
4. End cap (texture as for kind 2), last section: strip (d, 0, 0), (c, 1, 0), (d + R, 1, 1),
   (c + mirror(R), 0, 1).

Any other kind: no median (the game reports an error).

### Type 9: tunnel

Tunnel walls, ceilings and the sides of raised roadways (bridges, freeways). Words 0 and 1 are
*flags* and *h* × 256 (wall height). Word 2 is the clearance used for the room's vertical range
only. A 3-word tunnel follows a road; a 10-word tunnel follows the room perimeter (intersections).
*k* = 0.333·*h* is the wall thickness.

**Road tunnel (3 words).** The road is the next element after the tunnel, skipping one texture
element if there is one. It must be type 0 (4 points per section), 8 (6 points; skip its two
header words) or 2 (2 points). Its section count is the low 8 bits of its count. For section *i*:
Lᵢ = its first point, L′ᵢ = its second, Rᵢ = its last, R′ᵢ = the one before last. The wall line
moved outward by *k*: Oᵢ = Lᵢ + k·flatunit(Lᵢ − L′ᵢ) (only x, z change), Qᵢ = Rᵢ + k·flatunit(Rᵢ − R′ᵢ).
u = wall u along L with repeat length *h* (the right side reuses these u values).

| Flag | Meaning |
|---|---|
| 0x0001 | left wall, texture *t* |
| 0x0002 | right wall, texture *t* + 1 |
| 0x0004 | thick walls with an outside (raised roadways), textures *t* + 3, *t* + 4, *t* + 5 |
| 0x0008 | flat ceiling, texture *t* + 2 (ignored if 0x0100) |
| 0x0010, 0x0020 | left side: start / end closed (curved wall, outside caps, arch ends) |
| 0x0040, 0x0080 | right side: start / end closed |
| 0x0100 | arched ceiling, texture *t* + 2 |
| 0x0200, 0x0400 | left outside: corner at start / end |
| 0x0800, 0x1000 | right outside: corner at start / end |
| 0x2000 | curved walls |
| 0x4000 | flat walls are two-sided |

*Flat walls* (no 0x2000): left strip (Lᵢ + h, u, 1), (Lᵢ, u, 0); right strip (Rᵢ, u, 0), (Rᵢ + h, u, 1).
With 0x4000 each side also gets the same strip with the two points of every pair swapped.

*Curved walls* (0x2000): one 8-point strip per segment j → j+1, bulging out to Oᵢ (left) or Qᵢ
(right) between a quarter and three quarters of the height.
Left, segment j: (L(j+1) + h, u(j+1), 1), (Lⱼ + h, uⱼ, 1), (X75′, u(j+1), 0.75), (X75, uⱼ, 0.75),
(X25′, u(j+1), 0.25), (X25, uⱼ, 0.25), (L(j+1), u(j+1), 0), (Lⱼ, uⱼ, 0), where X75′ = O(j+1) + 0.75h and
X25′ = O(j+1) + 0.25h, except on the last segment with flag 0x20 where they are L(j+1) + 0.75h and
L(j+1) + 0.25h; X75 = Oⱼ + 0.75h and X25 = Oⱼ + 0.25h, except on the first segment with flag 0x10
where X75 = Lⱼ + **h** (sic: the game uses the full height there) and X25 = Lⱼ + 0.25h.
Right, segment j: (Rⱼ + h, uⱼ, 1), (R(j+1) + h, u(j+1), 1), (Y75, uⱼ, 0.75), (Y75′, u(j+1), 0.75),
(Y25, uⱼ, 0.25), (Y25′, u(j+1), 0.25), (Rⱼ, uⱼ, 0), (R(j+1), u(j+1), 0), with Y = Qⱼ + 0.75h / 0.25h, or
Rⱼ + 0.75h / 0.25h on the first segment with 0x40; Y′ = Q(j+1) + …, or R(j+1) + … on the last
segment with 0x80.

*Thick walls* (0x0004; needs at least 2 sections). First the outward points of the end sections are
redone: O₀ and O(n−1) as above; then with 0x0200, O₀ += c₀; with 0x0400, O(n−1) −= c(n−1), where
cᵢ = 1.414·k·(−dz, 0, dx)/flatlength(d) for d = Lᵢ − L′ᵢ. On the right only Q₀ is redone
(the game never revisits the last right section): Q₀ as above, and with 0x0800 Q₀ −= the same
correction computed from R₀ − R′₀.
- With 0x0001, texture *t* + 3: top strip (Oᵢ + h, u, 0), (Lᵢ + h, u, 1); outside strip: if 0x0010
  first (L₀, −1, 1), (L₀ + h, −1, 0); then (Oᵢ − 1, u, 1), (Oᵢ + h, u, 0) for every section; if
  0x0020 last (L(n−1), u(n−1) + 1, 1), (L(n−1) + h, u(n−1) + 1, 0).
- With 0x0002, texture *t* + 4: top strip (Rᵢ + h, u, 0), (Qᵢ + h, u, 1); outside strip: if 0x0040
  (R₀ + h, −1, 0), (R₀, −1, 1); then (Qᵢ + h, u, 0), (Qᵢ − 1, u, 1); if 0x0080
  (R(n−1) + h, u(n−1) + 1, 0), (R(n−1), u(n−1) + 1, 1).
- With both, the underside, texture *t* + 5: u2 = strip u along L with widths to R. Recompute
  (plain, without corner correction) O₀ if 0x0200, O(n−1) if 0x0400, Q₀ if 0x0800, Q(n−1) if 0x1000.
  Strip (Qᵢ − 1, u2, 0), (Oᵢ − 1, u2, 1).

*Flat ceiling* (0x0008 without 0x0100), texture *t* + 2: u3 = strip u along L with widths to R;
strip (Rᵢ + h, u3, 0), (Lᵢ + h, u3, 1).

*Arched ceiling* (0x0100), texture *t* + 2, u3 as above. Per segment j a 10-point strip across the
road. Dᵢ = Lᵢ − Rᵢ with Y set to 0. Rise values at the quarter points 1.5 m and at the middle 2.0 m;
on the first segment with 0x0010 the rises of section j are 0, on the last segment with 0x0020 the
rises of section j+1 are 0. Points, alternating section j+1 then j:
(R + h, u, 0), then for f = 0.25, 0.5, 0.75: (R + f·D + (0, h + rise(f), 0), u, f), then (L + h, u, 1).

**Junction tunnel (10 words).** Word 3 is a perimeter index *s*; words 4–5, 6–7, 8–9 are three
32-bit masks (low word first): *walls*, *bendA*, *bendB*. Bit *i* of a mask stands for the perimeter
edge from point *i* − 1 (wrapping) to point *i*. Pᵢ = the room's perimeter points, *N* their count,
*ymax* the highest of them. Nothing is drawn if *h* = 0.
- For each edge *i* in *walls*, texture *t*: u = max(1, |Pᵢ − P(i−1)| / h); strip
  (Pᵢ + h, 0, 1), (Pᵢ, 0, 0), (P(i−1) + h, u, 1), (P(i−1), u, 0). If flag 0x4000 is set and 0x0004 is
  not, also the reverse: (Pᵢ, 0, 0), (Pᵢ + h, 0, 1), (P(i−1), u, 0), (P(i−1) + h, u, 1).
- Flag 0x0008, ceiling, texture *t* + 2: a fan of *N* points, starting at perimeter point *s* and
  going backwards (s, s − 1, …, wrapping), each at height *ymax* + *h*, flat mapping tile 8 m with
  origin P(s).
- Flag 0x0004, raised junction:
  - Underside, texture *t* + 5: a fan over c = N−1, N−2, …, 0. For each c, with p = c + 1 and
    q = c − 1 (wrapping): if edge p is in *walls*, offset = out(P(p) − P(c), k); else if edge c is,
    offset = out(P(c) − P(q), k); else offset = out(P(p) − P(c), 0.25). Point = P(c) + offset at height
    *ymax* − 1, uv = (x/8, z/8) of that point (no floor offset). out(d, m) = (m/flatlength(d))·(−dz, 0, dx).
  - For each edge *i* in *walls*, texture *t* + 4: o = out(Pᵢ − P(i−1), k); oA = o, oB = o; if bit *i*
    of *bendA*, oA = (oₓ − 1.414·o_z, 0, o_z − 1.414·oₓ); if bit *i* of *bendB*, oB likewise (same
    formula, signs as written — checked against the binary; it is not a 45° turn of o).
    With A, B = Pᵢ, P(i−1) moved to height *ymax*, u = max(1, flatlength(Pᵢ − P(i−1)) / h):
    outside strip (A + oA − 1, 0, 0), (A + oA + h, 0, 1), (B + oB − 1, u, 0), (B + oB + h, u, 1);
    top strip (A + oA + h, 0, 0), (A + h, 0, 1), (B + oB + h, u, 0), (B + h, u, 1).

### Type 11: building wall

Words: bottom height index, top height index, u repeats, v repeats, points A, B. A flat vertical
rectangle above the ground line A→B, from absolute height F[bottom] to F[top] (the points' own Y is
not used). Texture *t*, colour lit by the current wall light. Fan:
(A at bottom, 0, 0), (B at bottom, U, 0), (B at top, U, Vr), (A at top, 0, Vr), with U and Vr the
repeat words as **unsigned** numbers.

It faces outward as type 3 does. **Inverted walls**: 1,555 London walls and 2 SF walls have their
bottom height above their top. Their v repeat word is 0 (264 of them) or a small negative number
stored unsigned (65515–65535, i.e. −21 … −1; 926 of them are 65533–65535). The game draws them like
any wall: the facing test (camera side of A→B, in x and z only) passes from outside, but the fan
then winds the other way and back-face culling removes it; from inside the facing test skips it.
So they are **never visible from either side**, and a reader should skip every type-11 wall whose
bottom is above its top. (No right-way-up wall has a negative v; no wall has bottom = top.)

The CSV files next to the cities (`facades.csv`, `floors.csv`, `walls.csv`) are inputs of the tool
that generated the PSDL; the game never reads them. Everything about a wall (texture, repeats,
heights) is already in its elements.

### Type 12: roof

Words: height index, then *n* + 1 point indices. A fan of the points moved to the absolute height
F[height index]. Texture *t*, flat mapping with tile 8 m, origin the first point. (The game draws
roofs only when the camera is not below them.)

## Colour and render state

Every vertex has a colour; textures are multiplied by it (the PSDL is drawn unlit).

- **Room colour C.** From `city/<city>.lmap`: magic `LMP0`, u32 count, then count × u32 ARGB, one
  per room. It is used only if count equals the PSDL room count; otherwise every room is white
  (0xFFFFFFFF). The shipped files do not match (London 1,341 vs 1,342 rooms, SF 1,125 vs 1,172), so
  the retail game draws all rooms white.
- **Dim** (curb faces, median sides): ((C >> 1) & 0x7F7F7F) | 0xFF000000.
- **Lit walls** (types 3 and 11): C multiplied per channel by light table entry *k*:
  out = (C · L) >> 8 for each of R, G, B, with alpha 0xFF; if either colour is 0xFFFFFFFF the other
  one is used unchanged.
- **Wall light table**: 64 colours, one per facing direction *k*. Entry *k* is for walls whose
  outward direction is (cos a, 0, sin a) with a = k·π/32 − π/2 (the same direction the type-7
  index was rounded down from). Built from the lights as follows:
  1. Start each of R, G, B at the ambient colour's channel (0–255, from the ARGB value: R = bits
     16–23, G = 8–15, B = 0–7). Ambient is added once, not per light.
  2. For each directional light (all light slots set so far, at most 8; in the city these are the
     key, fill 1 and fill 2 below, in that order), with direction of travel (Lx, Ly, Lz) and colour
     (r, g, b) on 0–1: d = cos a · (−Lx) + sin a · (−Lz). If d > 0, add d·255·r, d·255·g, d·255·b.
     The direction is used as stored (not re-normalised), so a light's horizontal strength is
     cos(pitch): a high sun lights walls weakly. Light Y is ignored.
  3. Cap each channel at 255, drop the fraction (truncate), and pack 0xFFRRGGBB.

  Each light's direction comes from its heading h and pitch p (`city-files.md`):
  (cos h · cos p, sin p, sin h · cos p); its colour is the file's colour (multiplied by a scale that
  is always 1, 1, 1 for this purpose). The table is not doubled or brightened in any other way: a
  wall's vertex colour is C × L as above, and the texture is multiplied by that colour (plain
  modulate). Example, `sf.lt04` (clear noon): k = 0 → (96, 96, 129), k = 6 → (100, 100, 110) (lit
  only by fill 2), k = 22 → white, k = 38 → (119, 115, 136), k = 54 → (157, 157, 214); darkest
  entry (73, 73, 93), ambient alone (40, 40, 50).

- **When MM2 builds the table (a quirk that decides what you see).** The game builds it twice
  while starting a race: when it creates the city (before loading anything), from the lights it has
  just set up there, and once more while loading the city, right after reading the new `.ltNN`
  files but **before** setting up any light from them. Both times it reads back the lights and
  ambient that are currently set, and it never rebuilds the table during the race. The lights at
  that moment are the ones made at city creation from the **previous race's** time-of-day/weather
  index and the light files of the **previously loaded** city. So:
  - on the first race after the game starts, walls are lit by built-in defaults: key heading 0,
    pitch −π/6 (−0.5236), colour (1, 1, 1); fill 1 heading 2π/3 (2.0944), pitch −π/8 (−0.3927),
    colour (0.75, 0.75, 0.75); fill 2 heading −2π/3, pitch −π/8, colour (0.5, 0.5, 0.5); ambient
    0xFF101010 (every one of the 16 conditions starts with these values). This gives a mid-grey
    table from (118, 118, 118) to (236, 236, 236); e.g. k = 6 → (192, 192, 192), k = 22 →
    (121, 121, 121);
  - on later races, walls are lit as the previous race's city was lit under the previous race's
    conditions, whatever the current conditions are (only a repeat of the same city and conditions
    gives the "right" table).
  If the lighting detail setting keeps fewer than three lights, the unused light slots still hold
  whatever was last put in them and still count. A new engine has to choose between this history-
  dependent behaviour and the table for the current conditions (what the files clearly intend);
  the formula above is the same either way.

Surface drawing state for PSDL (textures as in `tex.md`, including animated textures, clamping and
the cloud-shadow flags):
- alpha: MM2 switches alpha blending and the alpha test on and off together. Batches whose texture
  has an alpha channel (`tex.md`) are drawn **blended** (source alpha / inverse source alpha) with
  the alpha test keeping texels whose alpha is greater than **100** (0–255), and they get no cloud
  shadows. All other batches are drawn with neither. MM2 collects up to 32 different textures at a
  time and, for each such group, draws the opaque textures first and then the alpha ones; there is
  no depth sorting;
- no lighting, vertex colour × texture;
- depth test and write on; back faces culled;
- distance fog on, linear, colours and ranges from `city/<city>_fog.csv` (`city-files.md`);
- after each opaque batch with the cloud-shadow flag, a second pass with the ground overlay map
  (cloud shadows by day, light pools at night) — `night.md`, which also covers the night and rain
  texture swaps.

## Levels of detail

The game picks a level per room from its distance *d* = (depth of the room's bounding-sphere
centre along the camera's viewing direction) − (sphere radius): over 300 m → 0, over 100 m → 1,
over 50 m → 2, otherwise 3. The room the camera is in always counts as *d* = −radius. The sphere
(computed once at load, float32 with one rounding per operation as everywhere in MM2) is defined
under "Room bounding sphere" below. Level 3 is everything above. Lower levels change only roads
and sidewalks:

- **Level 2**: as level 3, but sidewalks are not raised (the +0.15 is dropped) and there are no curb
  faces. Sidewalk elements (type 1) become a single strip (pᵢ, uv), (qᵢ, uv); curb end caps are
  not drawn.
  Crosswalks, medians and everything else unchanged.
- **Level 1**: roads (type 0) and divided roads (type 8) become one strip from the left outer edge
  to the right outer edge, both lowered by 0.15, with texture *t* + 2 ("low-detail road", sidewalks
  painted in): u = strip u along a with widths to d (to f for type 8), (aᵢ − 0.15, u, 0),
  (dᵢ − 0.15, u, 1). No medians. Sidewalk elements as level 2.
- **Level 0**: as level 1 without the lowering, and with fewer sections when *n* > 2: keep the
  first section and then every second one so that the last is always kept: odd *n* keeps sections
  0, 2, 4, …, n−1; even *n* keeps 0, 1, 3, 5, …, n−1 (u values are those of the kept sections).
  When a game option (not identified) is set, low walls and building walls at level 0 are drawn
  untextured in a colour stored with each texture, multiplied as for lit walls.

A new engine may use level 3 everywhere.

### Room bounding sphere

Also used by the AI as a room's centre (`bai.md`, load step 4; the driver's recovery target).
With the room's perimeter point indices *q*₀ … *q*_{N−1} ("Room record" above; V = the point
table):

1. A = Sx = Sy = Sz = 0, count *k* = 0, *i* = 0, prev = *q*_{N−1}.
2. While *i* < N:
   - if *q*ᵢ = prev (a repeated point): if *i* = N − 1, stop; else *i* = *i* + 1;
   - *k* = *k* + 1; cur = *q*ᵢ; *i* = *i* + 1; P = V[cur], Q = V[prev];
   - w = (P.z·Q.x) − (P.x·Q.z);
   - A = A + w; Sx = Sx + (P.x + Q.x)·w; Sy = Sy + Q.y; Sz = Sz + (P.z + Q.z)·w;
   - prev = cur.
3. If A = 0: centre = V[*q*₀]. Otherwise *f* = 1 / (A·3) and centre = (*f*·Sx, Sy / *k*, *f*·Sz)
   (*k* converted to float exactly).
4. radius² = the largest (dz·dz + dy·dy) + dx·dx over **all** N perimeter points (repeats
   included), d = V[*q*] − centre, starting from 0 and replaced only by a larger value;
   radius = √radius².

The y of the centre is the mean height of the points that start an edge (each point once when there
are no repeats). The float32 products of large coordinates lose precision: for the intersection
rooms, centres computed this way differ from the `.bai` file's stored (editor) centres by a median
of 2–5 mm but up to 0.36 m in London (intersection 176) and 0.69 m in SF (intersection 92); SF's
degenerate intersection room 1078 (intersection 187) has A = 0 and gets its first perimeter point.

## Things that look like mistakes in the original

Kept above because they change pixels; listed here so nobody "fixes" them by accident:
- strip u averages the widths of all sections but the last and divides by the section count;
- the start of a curved left tunnel wall uses the full height for its three-quarter point;
- a raised-median end cap reuses whatever median texture was selected last;
- thick tunnel walls never redo the last right-hand outward point, so flag 0x1000 only affects the
  underside;
- building wall repeats are unsigned;
- inverted building walls (bottom above top) are generated but can never be seen, and so are
  the triangles of a low wall at an end above its top;
- a divided road after a texture element of value 0 is stepped over by 5*n* + 2 words instead of
  2 + 6*n*;
- the texture element's skip rule looks at bits 8–10 of its word.

## Checked against

`tools/formats/psdl.py` (dirty side) parses all five shipped files with no unknown element types,
no element running past its room, and no point index out of range:

| File | Points | Heights | Texture slots | Rooms (blocks) | AI roads | Triangles generated |
|---|---:|---:|---:|---:|---:|---:|
| london.psdl | 24,986 | 1,669 | 470 | 1,342 (366) | 540 | 97,628 |
| sf.psdl | 28,861 | 6,777 | 458 | 1,172 (401) | 379 | 93,117 |
| sfai.psdl | 35,739 | 6,528 | 358 | 1,329 (400) | 474 | 63,970 |
| variant.psdl | 25,457 | 44 | 193 | 1,124 (400) | 380 | 25,618 |
| city.psdl | 530 | 0 | 108 | 14 (3) | 6 | 449 |

The triangle counts include the inverted building walls (2 triangles each). With them skipped, as a
reader should: London 94,518 (1,555 walls), SF 93,113 (2 walls).

Element counts, London: road 434, sidewalk 979, ground strip 242, low wall 5,205, crosswalk 697,
intersection fan 1,793, ground fan 1,592, wall light 9,620, divided road 29, tunnel 127, texture
32,977, building wall 19,454, roof 2,953. SF: 457, 795, 29, 6,824, 648, 1,539, 1,849, 12,539, 99,
159, 27,912, 11,533, 4,820. Every file ends exactly after its AI roads. Spare words after a room's
last element: London 81 rooms, SF 17 (always texture elements). Median kinds seen: 1, 2, 3; tunnels
always 3 or 10 words; the element after a road tunnel is always type 0, 2 or 8.

Other checks:
- Wall light table for Ant's SF report (camera (−1716.9, 62.1, 987.4), room 787 area): the teal
  wall (`sw_stc_green_nonbay_f`, room 298, A (−1737, 998) → B (−1753, 986)) and the yellow and tan
  walls further along the same side (`sw_stc_yellow_nonbay_f`, `sw_stc_tan_nonbay_f`) all have
  k = 6, so MM2 always gives them the same colour: (100, 100, 110) with `sf.lt04`, white with
  `sf.lt00`, (192, 192, 192) with the built-in defaults.
- Wall light index vs wall direction: the index equals floor((atan2(nz, nx) + π/2)/(π/32)) for
  9,608 of 9,620 London and 11,310 of 12,539 SF wall lights; the rest are exactly on a boundary
  (walls along the axes) and are one lower.
- Facing: 421 of 434 London roads (437 of 457 SF) have their first triangle facing up with the
  winding above; perimeters run counter-clockwise from above in 1,337 of 1,341 London rooms.
- Low walls (type 3) by their ends against the top: London 4,732 both below, 250 both above, 48 one
  above and one below, 175 one end at the top and the other below; SF 4,825, 916, 695, 388.
- AI road records: all 1,381 London and 1,277 SF lane values read as floats between 0.069 and 0.931;
  every one of the 8 × u16 point indices of all 540 London and 379 SF roads is a valid point.
- Room flags: London 0x02 209 rooms, 0x20 27, 0x40 221, 0x80 95; SF 14, 77, 129, 149. No divided
  road follows a texture element of value 0.
- Rendered top-down maps (`work/check/london_map.png`, `sf_map.png`) show London with the Thames,
  Hyde Park in the west and the Tower in the east, and SF with the Golden Gate Bridge to the north
  and Market Street running north-east to the bay: not mirrored.
- A textured street view (`work/check/london_view.png`) shows doors at the bottom of building walls
  with v = 0 at the first stored texture row, road markings running along the road, raised
  sidewalks with curbs; a London Underground tunnel shows flat lit walls with an arched ceiling.
