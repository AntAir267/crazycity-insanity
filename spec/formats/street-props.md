# Street props (`propdefs.csv`, `proprules.csv`)

Street lamps, parking meters, bins, trees, phone boxes, benches, news boxes and the like line every
street of an MM2 city. They are not in any placement file: MM2 generates them while it loads the
city, walking along the sidewalks of the PSDL's **AI roads** (`psdl.md`, "AI road record") and
dropping a prop every so many metres, following two small tables per city. This page gives the
tables, the walk (exact arithmetic), what each prop becomes (a breakable prop, as the `.inst` props
of `inst.md`), and the order in which they are created. Props placed by hand along paths
(`props.pathset`) and the flat road decals are `pathset.md`.

Conventions as in `psdl.md` (metres, +Y up). All arithmetic is float32 with one rounding per
operation, evaluated left to right as written (`../physics/vehicle.md`, "Precision"); where a sum
is grouped differently the formula says so.

## Files

`city/<c>/propdefs.csv` (the prop types) and `city/<c>/proprules.csv` (which types go on each side
of a street). Both are read with the table rules below; if either cannot be opened, the city gets
no street props at all. Shipped: London 16 types and 32 rules (`n01left` … `n16right`), SF 33 types
and 40 rules (`n01left` … `n20right`). Other files in those folders (`props.csv`,
`PROPDEFS02.CSV.txt`, …) are not read.

### Reading the tables

MM2's table reader (also used for other CSV files) works on raw bytes:

1. The file is read as lines of at most 255 bytes (a longer line continues as the next line).
   Everything from a `#` to the end of its line is dropped.
2. The first line is the header. It is split into **cells** (below); at most 16 cells are taken,
   and each becomes a column name. The shipped headers have exactly 16 (the named columns followed
   by empty names from trailing commas).
3. Every further line is a data row, including empty lines (a row of absent cells). A row is split
   the same way; its cells fill columns 0, 1, 2, … and stop when the columns run out; columns
   after the last cell are **absent** (as opposed to empty).
4. Splitting: skip bytes below 0x20 or of 0x80 and above; the cell is the run of bytes from there
   up to the next byte that is a comma, below 0x20 or 0x80 or above (spaces are part of the cell).
   If that byte is not the end of the line it is consumed. The cell is kept if it is non-empty or
   if a byte was consumed after it, so `a,,b` gives `a`, an **empty** cell, `b`, while nothing is
   kept for the end of a line (and the `\r` of a CRLF line ends the last cell).
5. A column is found by name, a row by the value of one of its columns, both compared without
   regard to case; the first match wins. A missing column or row is a load error (MM2 stops).
6. Numbers: a float cell is read as a decimal number and rounded to float32; an integer cell as a
   decimal integer (leading sign and digits).

### `propdefs.csv`: prop types

| Column | Type | Meaning |
|---|---|---|
| `name` | text | the type's name, used by the rules |
| `start` | float | distance along the sidewalk, in metres, of the first prop |
| `distance` | float | spacing between props, metres |
| `maxUse` | int | the most props of this type one rule entry places on one road (the shipped 9999 never runs out) |
| `minLerp`, `maxLerp` | float | how far across the sidewalk, from the kerb (0) to the outer edge (1), a prop stands: a random fraction between the two |
| `file1` | text | model name (`geometry/<name>.pkg`, prop data `tune/banger/<name>.dgBangerData`) |
| next 3 columns | text | further models; one of the given models is picked at random per prop (below). They are found by position (the three columns right after `file1`, named `file2`–`file4` in the shipped files), not by name |

Shipped examples: London `streetlight,9,29,9999,0.1,0.1,sp_lightstreet_l` (a lamp every 29 m from
9 m on), `mailbox,2,1,1,0.2,0.2,sp_mailbox_l` (one mailbox, 2 m in, a fifth of the way across);
SF `newsboxs,7,10,1,0.1,0.1,sp_newsblue_f,sp_newsred_f,sp_newsyelw_f` (one of three colours).

### `proprules.csv`: rules

| Column | Meaning |
|---|---|
| `rulename` | `nNNleft` or `nNNright`, NN = a room's prop rule byte as two digits (`psdl.md`) |
| `prop1` and every column after it | the entries of the rule: names of prop types, used in column order. An empty cell is skipped; the first absent cell ends the list |

E.g. London `n11left,streetlight02,trashcan,mailbox,,bench` has four entries (the empty fourth cell
is skipped); `n15left,streetlightb` has one.

## Which roads get props

The props hang off the PSDL's AI roads (`psdl.md`, "AI road record"), not the `.bai` network. The
AI roads are processed one after another in file order. For road *r*, with room list
*R*₀ … *R*ₘ₋₁ (absolute values of the stored room numbers):

1. Build the road's **slots** and **sides** (next section).
2. Set MM2's global random generator (`../engine/controls.md`: state × 214013 + 2531011, result
   bits 16–30) to state **1**. This happens for every road, with or without props.
3. Let *b* be the prop rule byte of the road's **first** room *R*₀ (the bytes of its other rooms
   are ignored). If *b* = 0 the road gets no props. Otherwise the **left rule** is the row
   `rulename` = `n<b>left` and the **right rule** `n<b>right` (*b* written with at least two
   digits, `n04left`, `n17right`).
4. Place the props of the left rule, then of the right rule ("Placing the props").

Shipped: London 540 AI roads, 404 with a non-zero byte on their first room; SF 379 and 331.

## The two sidewalks of a road

### Slots

For each room *R*ⱼ in list order, walk the room's elements ("Walking a room's elements" below) and,
for every element of type 0 (road), 2 (ground strip) or 8 (divided road), append a **slot**: the
element's point words (for type 8, those after its two header words), its **stride** (4, 2 or 6
point words per section) and its **section count** *n* (the element's count; it is a road element's
number of 4-point sections etc.). Stop appending once the road has *m* slots. If, after all rooms,
the road has fewer than *m* slots, the road has no sidewalks: nothing is placed and no random number
is drawn for it. MM2 has room for 100 slots; a road with more rooms overflows its table (refuse
such a file; the shipped maximum is 40, SF road 325). (Shipped: every room on an AI road has
exactly one such element, so slot *j* belongs to room *R*ⱼ.)

**Sections.** The road has *T* = *n*₀ + (*n*₁ − 1) + … + (*n*ₘ₋₁ − 1) sections, numbered
0 … *T* − 1: slot 0 holds sections 0 … *n*₀ − 1 (its local sections 0 … *n*₀ − 1); each further
slot holds the next *n*ⱼ − 1 numbers with its local sections 1 … *n*ⱼ − 1 — its local section 0 is
skipped because it repeats the end of the previous slot (shipped: the same points, or points within
4 cm). The **room of section** *k* is *R*ⱼ of the slot that holds it.

**Section points.** A side (left or right of the direction from section 0 to section *T* − 1) has
two lines, the **kerb** and the **outer edge**. The point of section *k* on one of the four lines is
a point of the point table, picked from the slot's point words at position *base* + *o*, where
*base* = stride × (local section) and *o* comes from this table (*e* = 2 for stride 6, else 0):

| Line | Road flag 0x40 set | Road flag 0x40 clear |
|---|---|---|
| left outer edge | *base* | *base* |
| left kerb | *base* + 1 | *base* |
| right kerb | *base* + 2 + *e* | *base* + 1 |
| right outer edge | *base* + 3 + *e* | *base* + 1 |

"Road flag" means the AI road record's u32 flags (`psdl.md`). With 0x40 set, a road element gives
its points a, b, c, d (`psdl.md`, type 0) and a divided road a, b, e, f (type 8), i.e. the two
sidewalks. Positions count on through the room's following element words if they go past the
element (never in shipped data). With 0x40 clear both lines of a side are the same point, so
nothing can be placed on it (below).

**Centre variant.** If the road flags have 0x400000 set and 0x4 clear, the **kerb** point (either
side) is instead E + (C − E)·0.95 per component, where E is the kerb point of the table and C the
midpoint (B − A)·0.5 + A of the points at positions *base*′ and *base*′ + 1 + *e*, *base*′ =
*base* + 1 for strides 4 and 6, *base* for stride 2.

Shipped: every road whose first room has a road or divided-road element has flag 0x40 and strides 4
or 6. The centre variant never applies: London's 47 roads with 0x400000 are 40 roads of ground
strips (no road element in the first room, so the placement below never runs) and 7 road-element
roads whose first room has rule byte 0. Most slots have 2 sections (London 505 of 634, SF 381 of
547).

### Walking a room's elements

To find the road elements of a room (for the slots and for the placement below), MM2 steps through
the room's element words with its own size rule, which matches `psdl.md` ("Element words") for the
element types found in rooms on AI roads but not for all types. Header word *h*: count *c* = *h* & 7;
if *c* = 0 the next word's **low byte** is the count and that word is skipped. Then the element's
data words are skipped:

| Type (bits 3–6) | Words skipped |
|---|---|
| 0 road | 4*c* |
| 1 sidewalk, 2 ground strip | 2*c* |
| 4 crosswalk, 7 wall light, 9 tunnel, 11 building wall | *c* |
| 5 intersection fan, 6 ground fan | *c* + 2 |
| 8 divided road | 2 + 6*c* (its point words start after the first 2) |
| 10 texture | none (a texture element with *s* ≠ 0 has a data word, which this walk then reads as the next header) |
| 12 roof | *c* + 1 (one less than the drawing) |
| 3 low wall | MM2 stops with a load error |

The walk ends after the element with bit 7. In the shipped cities every room on an AI road parses
exactly as in `psdl.md` (no textures above slot 255, roofs or low walls in them); a city whose AI-road
rooms contain those elements would be read differently by MM2, and one with a low wall there does not
load.

### Side polylines

Each of the four lines is turned into a polyline of *N* = 2*T* vertices V(0) … V(*N* − 1), using the
section points S(*k*) of that line:

- V(0) = S(0) and V(*N* − 1) = S(*T* − 1).
- For 0 < *v* < *N* − 1, with *k* = *v* div 2: A = S(*k*); B = S(*k* + 1) if *v* is odd, S(*k* − 1)
  if *v* is even. d = B − A (per component),
  len = √((d.y·d.y + d.z·d.z) + d.x·d.x), L² = (d.z·d.z + d.y·d.y) + d.x·d.x,
  s = 1/√L² (0 if L² = 0), u = (s·d.x, s·d.y, s·d.z), *m* = len·0.333 (0.333 as float32); the limit
  λ = 15 if *v* = 1 or *v* = *N* − 2, else 0.1; if λ < *m* then *m* = λ.
  V(*v*) = (A.x + u.x·*m*, A.y + u.y·*m*, A.z + u.z·*m*).

So the polyline runs through every section point but cuts each inner corner 0.1 m to either side
(the first and last pieces are just split, at a third of their length or 15 m). The **room of
vertex** *v* is the room of section *k* + 1 when *v* is odd and 0 < *v* < *N* − 1, otherwise the
room of section *k* (*k* = *v* div 2).

### Point at a distance along a polyline

Point(*s*) for a distance *s* ≥ 0: for *i* = 0 … *N* − 2: p = V(*i*), q = V(*i* + 1), d = p − q,
len = √((d.z·d.z + d.y·d.y) + d.x·d.x). If *s* > len is false (this includes *s* = len and a NaN):
t = *s*/len, the point is ((q.x − p.x)·t + p.x, (q.y − p.y)·t + p.y, (q.z − p.z)·t + p.z) and its
room is the room of vertex *i*; stop. Otherwise *s* = *s* − len and go on. If no segment is left
there is no point (the distance is beyond the end of the road).

## Placing the props

For road *r* with a rule pair, take the entries of the left rule in column order, then the entries
of the right rule. For each entry (a prop type *D*):

1. Read *D*'s row: start *s*₀, spacing δ, maxUse *M* (an integer counter for this entry and this
   road), minLerp *a*, maxLerp *b*, and its model cells.
2. For **each** element of type 0 or 8 in the road's first room *R*₀, in element order (shipped:
   always exactly one, or none — then the entry places nothing), walk the **left** side and then
   the **right** side of the whole road as follows. The element only triggers the walks; its own
   points are not used, and a second such element would repeat them. Both sides are walked for
   every entry, but props are only created on the side the rule belongs to:
   1. *s* = *s*₀.
   2. K = Point(*s*) on the side's kerb polyline; stop this side if there is none.
   3. O = Point(*s*) on the side's outer-edge polyline, with its room ρ; stop this side if there
      is none. (Kerb and outer edge are each measured along their own polyline.)
   4. Draw a random fraction r₁ (the 15-bit random number × 2⁻¹⁵);
      t = (*b* − *a*)·r₁ + *a*; pos = ((O.x − K.x)·t + K.x, (O.y − K.y)·t + K.y,
      (O.z − K.z)·t + K.z). The placement point is **P** = (pos.x, pos.y + 0.15, pos.z) (0.15 as
      float32: the kerb points lie at road level, 0.15 m below the sidewalk).
   5. The placement axes: X = O − K per component, L² = (X.z·X.z + X.y·X.y) + X.x·X.x,
      k = 1/√L² (0 if L² = 0), X = (k·X.x, k·X.y, k·X.z) — pointing from the kerb across the
      sidewalk, tilted by any height difference between the two lines; Y = (0, 1, 0);
      Z = X × Y computed as (Y.z·X.y − Y.y·X.z, X.z·Y.x − Y.z·X.x, Y.y·X.x − X.y·Y.x), i.e.
      (−X.z, 0, X.x).
   6. If all three components of X are 0, or all three of Z (a NaN component counts as 0), nothing
      is placed. Otherwise, if this is the rule's side and *M* ≠ 0: *M* = *M* − 1; choose the model
      (next paragraph); if the chosen cell names a model, create the prop ("Creating a prop") with
      the matrix (X, Y, Z, P) in room ρ.
   7. Draw a random fraction r₂; *s* = ((δ − δ)·r₂ + δ) + *s* (MM2 keeps the spacing as a range
      whose two ends are both `distance`, so the step is exactly δ, but the number is drawn). Go
      to 2.

**Choosing the model.** Of the four model cells (`file1` and the three after it), count = 4 if the
fourth is present (not absent), else 3 if the third is present, else 2 if the second is, else 1.
Draw a 15-bit random integer *x*; the model is cell *x* mod count. If that cell is absent or empty,
no prop is created (the counter *M* and the random number are used up anyway).

Random numbers per road therefore come in a fixed pattern: two per walk step on each side for every
entry (r₁, r₂), plus one for every model choice. Because the generator restarts at 1 for
every road, a road's props depend only on that road.

Example (London road 12: one room, 396, rule byte 4, a straight 2-section road 32.7 m long):
`n04left` = streetlight, trashcan, streetree. The street light (start 9, spacing 29, lerp 0.1)
stands at P = (−217.161041, 0.0149000138, 328.375488) with X = (−0.174760833, 0.0499336943,
−0.983343959) (r₁ was the generator's first number, the model choice its second); the next step,
*s* = 38, runs off the road. The trash can (start 4, maxUse 1) is placed at 4 m (numbers 6 and 7)
and then the walk goes on drawing numbers at 14 and 24 m without placing.

## Creating a prop

A street prop is a breakable prop exactly like a compact-placement `.inst` prop (`inst.md`, bit 25;
`../physics/collision-objects.md`, "Props"):

- It exists only if `tune/banger/<model>.dgBangerData` exists; otherwise nothing is created (all
  shipped street-prop models have one).
- Its position is its centre of gravity, computed with the full placement matrix (X, Y, Z, P):
  p.c = P.c + ((CG.z·Z.c + CG.y·Y.c) + CG.x·X.c).
- It keeps only p and the x and z of X: its matrix is (X.x, 0, X.z), (0, 1, 0), (−X.z, 0, X.x), p,
  **not re-normalised**. Where the outer edge is higher or lower than the kerb, |(X.x, X.z)| is a
  little below 1 (London ≥ 0.998, SF ≥ 0.953, on steep cross-slopes); MM2 draws and collides with
  this matrix as it is.
- It is filed in room ρ (the room of the outer-edge point, never searched) at the front of the
  room's object list (`../physics/collision-objects.md`, "Room lists").
- It uses material set 0 of its model (street props have no variant), drawn with the textures and
  night/rain rules of every city model (`night.md`).

### Collision and breaking

Every street prop collides with car bodies, with the bound its `.dgBangerData` describes
(`CollisionPrim`; `../physics/collision-objects.md`). Wheels drive on it only if its
`CollisionType` has bit 0x20 (none of the shipped street-prop types). It is knocked over like any
breakable prop when an impact exceeds its `ImpulseLimit2` (`../physics/collision.md`, the breakable
rule). Shipped street-prop types: lamps, meters, bus stops, telephone poles and call boxes are
capsules (`CollisionPrim` 2); `sp_tree1_s` uses its geometry bound (`CollisionPrim` 0 with a
`bound/sp_tree1_s_BOUND.bnd`); bins, mailboxes, phone booths and stands, news boxes, benches, no-parking
signs and `sp_lightthames_l` are boxes of their `Size` (`CollisionPrim` 1, or 0 without a bound
file, which falls back to the box).

## Drawing

As every breakable prop (`inst.md`, "Drawing"): drawn only in rooms within the room limit and at
most the room limit away, at the level chosen by the distance thresholds, at its CG position with
its stored matrix, with the city lights. The prop data's `BillFlags` change this:

| Bit | Effect |
|---|---|
| 0x80 | drawn without lighting and with the alpha-test reference 140 (instead of 100) |
| 0x200 | a **tree**: not drawn with the other props but collected and drawn right after them, always with its `H` mesh whatever its distance, without lighting, with alpha test "greater than 120". MM2 also sets this bit at load for every prop type whose name contains `_tree` (case-sensitive), e.g. `sp_tree1_s`, `sp_tree6_s`, `sp_treepalm4_f`, but not `sp_oaktree1_s` |
| 0x10 | no shadow (no visible effect: props draw no shadows) |
| 0x100 | a **glass pane**: MM2 makes it a separate kind of object that is drawn like a prop until it is hit, then shatters into a burst of glass shards (window-glass texture `s_win_trans_01`) that fall to the ground below it, instead of being knocked over; at most two panes are shattering at a time. No shipped prop type sets it (a hidden command-line option, `andyglasshack`, sets it on `sp_streetlight03_m`, which does not exist); not specified further here |
| 0x40 | set on the lamps; no effect found |

0x200 is tested before 0x80 (`sp_tree1_s` has both: it is drawn as a tree).

**Night glows.** Prop types with `NumGlows` (`night.md`, "Street lamps and other lit props") get
their glow billboards at night: of the street props, the lamps `sp_lightstreet_l`,
`sp_lightstreet_f`, `sp_light_tall_l`, `sp_lightpark_f`, `sp_lightthames_l`, `sp_chinalight_f` and
the banner lamps `sp_lightbanrb_f`, `sp_lightbanrg_f`, `sp_lightbanr_rainbo_f`.

## When they exist

- **Created** once per session, while the city loads, before any other object (order below).
- **Reset** (every race or session start, `../physics/collision-objects.md`, "Room lists"): each
  street prop, in creation order, goes back to its own room at the front of the list, standing
  where it was created — a knocked-over one too (its broken pieces disappear).
- **AI.** Street props are ordinary breakable props to the AI: when the road network loads, the
  breakable props in the rooms of each road and intersection are registered as obstacles by the
  rules of `../ai/road-network.md`, "Obstacles" (the walk over a road's rooms meets every breakable
  prop filed there by then: street, `.inst`, xref and path-set props, and the traffic-light poles,
  which are created just before). Traffic and the police and opponent planner
  (`../ai/driver-planner.md`, obstacle scan) see props only through these lists.

## Creation order

MM2 creates the objects of a city in this order (the order matters for the room lists and for
object ids):

1. **Street props** (this page): road order; within a road, left-rule entries then right-rule
   entries in column order, each entry's props in walk order (left side, then right side).
2. The `<c>.inst` objects, each record followed by its package's xref props, then `<c>_ai.inst`
   (`inst.md`).
3. The `props.pathset` objects, path by path (`pathset.md`).
4. The decals (`pathset.md`; static, not collidable).
5. The race-mode path set's objects (`pathset.md`; none in Cruise).
6. The level objects (`../game/level-objects.md`, "Creation order"; the parked cars are props).
7. When the road network loads: the traffic-light poles (`../ai/traffic-control.md`).

After a reset, a room's list therefore holds, front to back: its poles (newest first), the
race-mode path-set props, the `props.pathset` props, the `_ai.inst` props, the `.inst` props and
xref props, the street props (each group newest first), then the static objects (decals, then
landmarks, spanning objects and plain statics, newest first).

## Checked against

`tools/formats/props.py` (dirty side) implements this page and `pathset.md` in float32:

| | London | SF |
|---|---:|---:|
| AI roads / with a rule byte on the first room | 540 / 404 | 379 / 331 |
| of those, with a road or divided-road element in the first room | 374 | 331 |
| street props created | 4,835 | 5,349 |
| rooms holding street props | 379 | 409 |

- London by type: `meter` 1,057, `streetree` 591, `streetlight` 505, `streetlightb` 458,
  `trashcan` 372, `streetlight04` 326, `streetlight04b` 296, `phone` 249, `mailbox` 243,
  `streetlight02` 215, `streetlight02b` 199, `newsboxs` 189, `bench` 135 (models
  `sp_parkmtr_f`, `sp_lightstreet_l` 963, `sp_light_tall_l` 622, `sp_tree1_s`, `sp_lightpark_f`
  414, …). SF: `meter` 1,295, `streetlight` 1,159, `streetlightb` 1,069, `trashcan` 319,
  `streetree` 295, `busstop` 222, `newsboxs` 116 (54 yellow, 32 blue, 30 red), … `callbox` 8.
- Every road's slots: one per room; no position read beyond its element; no rejected matrix; every
  model has a `.dgBangerData` and a package.
- Heights: for 400 random street props per city the placement point P lies 1–3 cm above the PSDL
  ground straight below it (London), −4 … +6 cm on SF's slopes (vertical probe of the city
  geometry), i.e. the props stand on the sidewalk.
- Prop data of the street-prop models (`BillFlags`, `CollisionType`, `CollisionPrim`): lamps 64,
  16, 2 (`sp_lightthames_l` prim 0); `sp_tree1_s` 640 (0x280), 16, 0; `sp_parkmtr_f`,
  `sp_busstop_f`, `sp_callbox_f`, `sp_telephonepole_f` 0, 16, 2; bins, mailboxes, booths, news
  boxes, benches 0, 16, 0 or 1. Lamp CGs: `sp_lightstreet_l` (0, 5.2997, 0), `sp_lightstreet_f`
  (0, 3.8621, 0).
- Placement CSVs `work/check/props_london.csv`, `props_sf.csv` (one row per created object: road,
  side, rule entry, model, room, P, X, CG position); top-down plots `work/check/props_london.png`,
  `props_sf.png` and zooms `props_london_crop.png`, `props_sf_crop.png` around the free-roam starts
  (props along both kerbs of every street, on the sidewalk a tenth of the way across); street-level
  lists `work/check/props_london_near.txt` (−230, 333.5) and `props_sf_near.txt` (−995.5, 1291.7).
  Nearest to the London start: `sp_tree1_s` of road 80 at (−237.477, 3.515, 334.889) (CG point),
  7.6 m away; to the SF start, `sp_can_gen_f` of road 115 at (−993.061, 43.145, 1277.163).
