# City object placements (`.inst`)

The PSDL (`psdl.md`) draws streets and simple box buildings. Everything more detailed (shaped
buildings, landmarks such as St Paul's, the Transamerica Pyramid or the Golden Gate Bridge, park furniture, signs) is a
model from `geometry/` placed by an instance list.

A city loads two lists, both in this format: `city/<city>.inst`, then `city/<city>_ai.inst` (stop
signs and other street furniture used by traffic). Other `*.inst` files in `city/` (`*.sdl_ai.inst`,
`sf1_ai.inst`, …) are leftovers and are not loaded.

## Layout

Little-endian. The file is a plain sequence of records until the end of the file (an empty file
is valid; `city.inst` is empty).

| Size | Contents |
|---|---|
| u32 | header (below) |
| u8 | name length *L* in bits 0–6; bit 7 set = compact placement |
| *L* | object name; the NUL is included in *L* |
| 48 or 20 | placement (below) |

Header:

| Bits | Meaning |
|---|---|
| 0–15 | room number in the city's PSDL that the object belongs to |
| 16–23 | variant: picks the object's material set (below) |
| 24 | landmark |
| 25 | breakable prop |
| 26 | with bit 24: a landmark that wheels do not drive on: it still collides with car bodies, but wheel probes ignore it (`../physics/collision-objects.md`, "Probing an object"); drawn like any landmark. Without bit 24 it has no effect. Never set in shipped data |
| 29 | spans several rooms |
| others | 0 |

## Placement

Full form (bit 7 of the length byte clear): 12 × f32 = X axis, Y axis, Z axis, position, in the
matrix convention of `pkg.md` (a model point (x, y, z) goes to x·X + y·Y + z·Z + position). The
axes are used as stored: they are orthogonal but often not unit length, so objects are scaled,
sometimes unevenly (London has many with Y × 1.333 or × 0.833).

Compact form (bit 7 set): f32 c, f32 s, then 3 × f32 position. The matrix is
X = (c, 0, s), Y = (0, 1, 0), Z = (−s, 0, c): a turn about the vertical axis, with a horizontal
scale of √(c² + s²) (not always 1).

## Object name and model

The name may contain a folder written with backslashes; only the part after the last backslash
counts. The model is `geometry/<name>.pkg`, looked up without regard to case (the files use mixed
case, e.g. `kL_picadeli_l.pkg`). The package format is `pkg.md`.

The object uses the package's meshes `H`, `M`, `L`, `VL` (high to very low detail). Missing ones
are filled from the level below: if `L` is missing it uses `VL`, then if `M` is missing it uses `L`,
then if `H` is missing it uses `M`. A missing lower level is not filled from above: an object
without `VL` is not drawn when the very-low level is chosen. The game also looks for optional part
meshes `shadow_*`, `mask_*`, `nonrandom_*`, `refl_*`, `opaque_*` (with the same level suffixes); no
shipped city object has them.

The object's radius is the largest radius of its level meshes. The package's `offset` entry is
ignored; its `xrefs` (attached props) are placed as `pkg.md` describes, combined with this
placement. Each becomes a breakable prop:

- It keeps only its position and its turn about the vertical axis (matrix rebuilt from the X
  axis's x and z as for landmarks below); tilt is dropped.
- Material set: the owner's variant byte (header bits 16–23, as stored), modulo the prop's own
  set count.
- Room: the room that contains its world position, searched in this order: the owner's room; the
  owner's neighbouring rooms (if several contain it, the highest room number); a search over the
  whole city. A room contains a point when the point's x/z lies inside its perimeter and, for a
  stacked room (flag 0x40), the height lies in the room's vertical range (`psdl.md`); an ordinary
  room does not contain a point that one of the stacked rooms overlapping it contains. If no room
  contains it, it goes to room 0 and is never drawn.

**Variant.** The material set used is variant mod (number of sets loaded for that package)
(`pkg.md`, material sets). While loading `.inst` objects (and the street props) the game keeps at
most 99 sets per package; that value never changes (all modes, times of day and weathers). No
shipped package has more than 48 sets, so in practice every set is kept.

## Kinds

| Bits set | Kind | Notes |
|---|---|---|
| none | static object | full placement kept; the only kind that is one-sided ("Drawing") |
| 29 | static object spanning rooms | drawn through the neighbours of its header room (below), not through the header room itself; never one-sided |
| 24 | landmark | never one-sided (below). Collision from the model's own bound (`bound/<name>_BOUND.*`, physics spec). Keeps only position and X axis x and z: the matrix is rebuilt as X = (m₀₀, 0, m₀₂), Y = (0, 1, 0), Z = (−m₀₂, 0, m₀₀), so tilt and vertical scale are dropped |
| 25 | breakable prop | can be knocked over; the compact form makes a prop that turns only about Y, the full form keeps its matrix. Breaking is specified with the props |

Bit 29 has no effect together with bit 24.

**Spanning objects (bit 29).** MM2 lists the object in each direct neighbour of its header room
whose shared perimeter edge is touched by the object's x/z bounding circle (centre = position,
radius = object radius), plus every neighbour with flag 0x80, up to 32 rooms. It is drawn at most
once per frame, as soon as any of those rooms is drawn, with the ordinary distance and level rules
but **without** the one-sided test. If none of those rooms is visible, it is not drawn (the header
room does not count). Shipped: 13 in London, 2 in SF, each listed in 4–7 rooms.

A landmark's room is marked as "has a landmark" (used by the free-roam start, `controls.md`).

## Drawing

- Distance *d* = depth of the object's position along the camera's viewing direction (not the
  straight-line distance) minus the object radius. Level: *d* > T3 → `VL`, *d* > T2 → `L`,
  *d* > T1 → `M`, else `H`. Objects outside the view or beyond the far plane are not drawn.
- Objects are drawn only in the rooms of the frame's room list (`cpvs.md`: the `.cpvs` row of the
  camera's room, or the room walk, and the view test). A room's distance is the same kind of value
  for its bounding sphere (`psdl.md`, "Levels of detail").
- **Static objects, landmarks and spanning objects have no room limit**: they are drawn whenever
  their room is visited and they pass the tests above (cut-off distance 9999).
- **Breakable props** (bit 25, xref props, street props) are drawn only if their room's distance is at
  most the room limit and their own depth (without the radius) is at most the room limit.
- Shadows and glow parts of objects are drawn only in rooms whose distance is below the room limit.
- **Where breakable props are drawn.** Every breakable prop (bit 25 in either placement form,
  xref props, street props, path-set props, traffic lights) keeps as its position its centre of
  gravity: placement position + R·CG, with `CG` from its `.dgBangerData` and R the full matrix it
  was created with (`collision-objects.md`, "Props"; for the turn-only kinds R is the full matrix
  before the turn is extracted). The prop is drawn with that stored matrix as it is: no offset is
  taken back off, and prop meshes are modelled around their CG, so this puts their base on the
  ground (e.g. `sp_stop_f`, mesh y ±1.61, CG y 1.614). The distance *d* and the room-limit test
  use this position too.
  - Glow sprites (`GlowOffset` in `.dgBangerData`, e.g. street lamps) are placed by the same stored
    matrix: world = stored matrix × GlowOffset (the offsets are in the same CG-centred space; the
    `sp_lightstreet_l` glow at (−1.93, 4.19, −0.03) sits at the lamp head).
  - Traffic lights are the exception for their light meshes only: the `REDGLOW…`, `YELLOWGLOW…`,
    `GREENGLOW…`, `WALK…` and `NOWALK…` meshes (`pkg.md`) are drawn with the stored matrix minus
    R·CG, i.e. at the original placement, while the housing is drawn at the CG position like any
    other prop.

| Object detail setting | Room limit | T3 | T2 | T1 |
|---|---:|---:|---:|---:|
| 0 (low) | 200 | 150 | 70 | 20 |
| 1 | 250 | 175 | 90 | 30 |
| 2 | 300 | 200 | 100 | 40 |
| 3 (highest) | 300 | 200 | 130 | 70 |

The game's built-in values are those of setting 2; MM2's setup picks 3 on any machine of 800 MHz or
more (0 below 500 MHz, 2 in between), so 3 matches the game on a modern computer. The same
thresholds choose car levels (`car-model.md`).

- **One-sided objects.** Only **plain static objects** (header bits 24, 25 and 29 all clear) are
  one-sided. Landmarks (bit 24, with or without bit 26), spanning objects (bit 29), breakable props
  (bit 25, xref props, street props) and cars never get this test. A plain static object is
  skipped when

  (C.x − P.x) · Z.x + (C.z − P.z) · Z.z < 0

  where C is the camera's position in the world (the eye point), P is the object's position (the
  placement's translation, i.e. the model pivot, not its bounding-sphere centre) and Z is the
  placement's Z axis as stored (its length does not matter; for compact placements Z = (−s, 0, c)).
  Heights are ignored; there is no tolerance, distance or angle condition, and exactly 0 counts as
  visible. The test is applied before the distance and level choice and is on whenever a city is
  loaded. Example: an object at the origin with Z = (0, 0, 1) is drawn from (0, 10, 50) and hidden
  from (0, 10, −50), i.e. hidden while the camera is north (−Z) of the line through its pivot.
  Many building models are fronts meant to be seen only from their +Z (street) side, and the
  blocks behind them have their own PSDL roofs and ground, so hiding them shows the PSDL beneath.
- **Missing levels.** A missing level is only ever filled from the level below it ("Object name and
  model"), so an object whose package has no `VL` mesh is not drawn at all when *d* > T3, one with
  neither `L` nor `VL` not when *d* > T2, and one with only `H` not when *d* > T1.
  This matters for landmarks: 122 of London's 176 landmark placements and 23 of SF's 88 have no
  `VL` (e.g. `wl_westabbey_l`, `cl_stpauls_l`, `kl_hydeparklake_l`, the Lambeth and Waterloo bridge
  river sections, `nl_coit_f`); the Tower Bridge and Golden Gate upper parts have only `H`. Several
  of them are the only ground of their block (`psdl.md`, "Ground supplied by landmarks"), so from
  far away, or from high above, those blocks show the clear colour. MM2 does the same; its camera
  just rarely gets that far from them.
- Materials and textures follow `pkg.md` / `tex.md`, drawn lit by the city lights (`city-files.md`).
  Static objects, landmarks and spanning objects are drawn after the rooms with depth test and write
  on, fog on, and the `pkg.md` translucency rule: batches from the first one that needs blending
  onward are alpha-blended **and** alpha-tested at > 100 (MM2 switches both together; earlier opaque
  batches get neither). Whether a model can blend at all is decided once, from its material set 0
  (`pkg.md`). Breakable props follow the same rule; prop types marked in the props data use the
  cut-off 140 and no lighting, and trees are drawn separately (`street-props.md`, "Drawing"). No depth sorting.

## Checked against

`tools/formats/inst.py` (dirty side) parses every `.inst` in `work/data` to its last byte, and every
name resolves to a package in `geometry/` when matched without case:

| File | Records | Static | Spans rooms | Landmark | Breakable | Compact | Names | Rooms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| london.inst | 1,997 | 1,808 | 13 | 176 | 0 | 544 | 221 | 287 |
| london_ai.inst | 67 | 0 | 0 | 0 | 67 | 67 | 1 (`sp_stop_f`) | 58 |
| sf.inst | 3,763 | 3,673 | 2 | 88 | 0 | 211 | 165 | 196 |
| sf_ai.inst | 40 | 0 | 0 | 0 | 40 | 40 | 1 | 35 |
| sfai.inst | 2,079 | 2,010 | 0 | 69 | 0 | 98 | 121 | 167 |
| variant.inst | 78 | 78 | 0 | 0 | 0 | 3 | 7 | 1 |
| city.inst | 0 | | | | | | | |

- All full matrices are orthogonal to 0.01; 1,458 of London's and 1,694 of SF's records have axes
  that are not unit length (scaled objects).
- Variants in use: London 0–5 and 12, SF 0–9.
- Positions plotted on the PSDL maps (`work/check/london_map.png`, `sf_map.png`) fall on the blocks
  and kerbs they belong to. Landmark positions lie inside their own room's outline (171 of 176 in
  London, 78 of 88 in SF; e.g. `cl_stpauls_l`, `el_transam_f`, `nl_coit_f`, all in block rooms whose
  PSDL leaves the space empty). Static objects are placed by their pivot, which is often on or past
  the room edge (626 of 1,808 London static objects lie outside their room's outline), so the room
  number cannot be recomputed from the position.
