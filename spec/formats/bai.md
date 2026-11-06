# AI road network (`.bai`, magic `CAI1`)

`city/<c>.bai` and `city/<c>_sup.bai` describe the streets as the AI sees them: **roads**
(stretches of street between two junctions) made of lanes and sidewalks, and **intersections**
where roads meet. Ambient traffic, police, opponents, cable cars and pedestrians all move on this
network. This page gives the bytes and the load procedure; what the AI derives from it (lane paths,
turns, position look-ups, routing) is `spec/ai/road-network.md`, and traffic lights and stop
signs are `spec/ai/traffic-control.md`. `city-files.md` ("AI network") gave just enough of this
layout to find the intersections; this page supersedes it.

## Conventions

- Little-endian. `u16`, `s16`, `u32`, `f32` as in `psdl.md`; `vec3` = 3 × f32 (x, y, z).
- Coordinates as in `psdl.md`: metres, +Y up, +X east, −Z north.
- A road has *n* ≥ 2 **sections**, numbered 0 … *n* − 1 along the road. The **road direction** is
  from section 0 to section *n* − 1.
- Every road has two **sides**, stored in the file as side **A** then side **B**. Traffic on side B
  travels in the road direction (section 0 → *n* − 1); traffic on side A travels against it
  (*n* − 1 → 0). In the file side A lies to the left of the road direction (+X of the sections,
  below) and side B to the right, i.e. the file is laid out for driving on the right; London is
  mirrored at load (step 8 of the load procedure).
- **Ends**: end 0 is the road's end at section *n* − 1, end 1 its end at section 0. Traffic on
  side B arrives at end 0's intersection; traffic on side A arrives at end 1's.
- Each side has *L* lanes and *W* sidewalks (*W* is 1 on every shipped main road, 0 on
  supplementary roads). They are stored as *L* + *W* **lines** of *n* points: lines 0 … *L* − 1 are
  the lane centre lines, lines *L* … *L* + *W* − 1 the sidewalk lines. Lane 0 is the lane nearest
  the road's centre line, lane *L* − 1 the one nearest the kerb (before London's mirroring).

## Files

| File | Read by the game | Contents |
|---|---|---|
| `city/<c>.bai` | yes | the main network: intersections, roads, room activation tables |
| `city/<c>_sup.bai` | yes, if present | **shortcuts**: extra one-lane roads (through parks, alleys, across squares) used for routing; no ambient traffic |
| `city/<c>_bak.bai` | no | an older copy of the main file, same layout (same counts, slightly different data) |
| `city/sfai.bai` | no | the network of `sfai.psdl` (an unused SF variant, `psdl.md`); same layout |
| `*_sup.bai.tmp`, `*_sup.bai.bak` | no | an older format, magic `BAI1`; not readable with this layout |

The game finds the supplementary file by appending `_sup` to the city name. A file whose first four
bytes are not `CAI1` is still read (the game only prints a warning); a loader should refuse it.

## Main file

| Size | Contents |
|---|---|
| 4 | ASCII `CAI1` |
| u16 | intersection count *NI* |
| u16 | road count *NR* |
| — | *NR* road records (below), road *i* is road index *i* |
| — | *NI* intersection records (below) |
| u32 | room count *NC* (the PSDL's room count, including the unused room 0) |
| — | *NC* lists, the **ambient activation table**: for room *r*, u16 count *k*, then *k* × u16 road indices |
| — | *NC* lists, the **pedestrian activation table**, same layout |

The file ends after the second table.

## Supplementary file

| Size | Contents |
|---|---|
| 4 | ASCII `CAI1` |
| u16 | road count *NS* |
| — | *NS* road records, same layout as the main file's |

Supplementary roads get the road indices *NR* … *NR* + *NS* − 1 in file order. The stored road
index of each record is ignored (the shipped files store 9000), and the 5th word of each side
block (side flags) is replaced by 3 at load.

## Road record

Let *n* = section count, *LA*/*WA* and *LB*/*WB* the lane and sidewalk counts of sides A and B.

| Size | Field | Meaning |
|---|---|---|
| u16 | index | road index (main file: 0, 1, 2 … in order) |
| u16 | *n* | section count |
| u16 | flags | road flags (below) |
| u16 | *c* | room count |
| *c* × s16 | rooms | PSDL rooms the road passes through (may repeat; no negative values in shipped data) |
| f32 | half width | distance from the centre line to the outer edge of the sidewalks, each side |
| f32 | speed | ignored: the game replaces it at load (step 9); shipped files all store 15 |
| — | side A block | below |
| — | side B block | below |
| *n* × f32 | centre distance | distance along the centre line from section 0 to each section (3D, cumulative; entry 0 is 0) |
| *n* × vec3 | centre | centre point of each section, on the road surface |
| *n* × vec3 | X | section axis pointing to side A (left of the road direction), unit length; normally Y × (−Z) |
| *n* × vec3 | Y | section up axis (road surface normal), unit length |
| *n* × vec3 | Z | section axis pointing **against** the road direction: in the shipped files Z[*i*] = −(direction from centre[*i* − 1] to centre[*i*]) for *i* ≥ 1, and Z[0] is normally Z[1] |
| *n* × vec3 | T | section tangent, a smoothed road direction at the section (used as spline tangents), unit length |
| 38 | end 0 | the end at section *n* − 1 (below) |
| 38 | end 1 | the end at section 0 |

The sizes match `city-files.md`'s summary: each side block is 10 + 4·(*L*+*W*)·*n* + 4·(*L*+*W*) +
40 + 12·(*L*+*W*)·*n* (+ 12·*n* per extra line) + 24·*n* bytes.

### Side block

| Size | Field | Meaning |
|---|---|---|
| u16 | *L* | lane count |
| u16 | cable | non-zero: the side has a cable-car line |
| u16 | subway | non-zero: the side has a subway-train line |
| u16 | *W* | sidewalk count |
| u16 | side flags | bit 0: no ambient vehicles on this side; bit 1: no pedestrians on this side; other bits unused |
| (*L*+*W*)·*n* × f32 | line distances | for each line (line-major), the distance along that line from its point 0 to each point (3D, cumulative, entry 0 is 0) |
| (*L*+*W*) × f32 | line offsets | the lateral offset of each line from the centre line; **not read by the game** (side A's values are wrong in places) |
| 10 × f32 | band edges | lateral band edges: entry 2*j* + 1 is the outer edge of lane *j*'s band (entry 2*L* − 1 is the outer edge of the last lane, entry 2*L* + 1 the outer edge of the sidewalk band); entries past 2*L* + 1 and entry 0 are padding or editor values. Only side B's are read (`road-network.md`, "Is a point on the road") |
| (*L*+*W*)·*n* × vec3 | lines | for each line (line-major) its *n* points |
| *n* × vec3 | cable line | only if cable ≠ 0 |
| *n* × vec3 | subway line | only if subway ≠ 0 |
| 2*n* × vec3 | edges | the kerb line (*n* points), then the outer sidewalk edge line (*n* points) |

**Point order.** Side B's lines, side A's sidewalk lines and both sides' edge lines are stored in
section order (point *i* lies at section *i*; on bends the outer lanes' points can sit a few metres along the road from the section's cross line). **Side A's lane lines and cable line are stored in
travel order**: point *i* lies at section *n* − 1 − *i*. So for both sides, lane point 0 is where
the side's traffic enters the road and point *n* − 1 where it leaves, and the line distances of a
lane grow in the direction of travel.

### End block (38 bytes)

| Size | Field | Meaning |
|---|---|---|
| u32 | intersection | index of the intersection at this end |
| u16 | light slot | runtime: which light of the intersection's light set controls this approach (`traffic-control.md`); the file holds junk (0xCDCD or 0xFFFF) |
| u16 | control | how traffic arriving here is controlled: 0 stop sign, 1 traffic light, 2 yield (not supported by the game), 3 none |
| u16 | turn rule | runtime: 3 = arriving traffic always goes straight across (set for some traffic-light junctions, `traffic-control.md`); the file holds 0 |
| u32 | slot | position of this road in the intersection's sorted road list (load step 4); recomputed for intersections that get shortcuts |
| vec3 | sign position | where the traffic light or stop sign of this approach stands (on the ground); (0, 0, 0) when there is none |
| vec3 | facing point | a point the traffic light faces: the light's +X axis points from the sign position toward it |

A road whose two ends name the same intersection (a loop) occurs 3 times in London, once in SF.

### Road flags

| Bit | Name | Used for |
|---|---|---|
| 0x01 | divided | opponents and police do not cross the median (their spec) |
| 0x02 | alley | excluded from the free-roam start (`controls.md`); opponent driving |
| 0x04 | freeway | speed limit + 12.5 (load step 9); freeway turn rules (`road-network.md`); excluded from the free-roam start |
| 0x08 | level | ambient cars on it are kept upright at the height of the road's first centre point instead of following the ground (traffic spec) |

## Intersection record

| Size | Field | Meaning |
|---|---|---|
| u16 | index | 0, 1, 2 … in file order |
| u16 | room | the PSDL intersection room (flag 0x10) |
| vec3 | centre | centre point, on the road surface |
| u16 | *m* | number of roads meeting here |
| *m* × u32 | roads | road indices, sorted by angle (load step 4) |

## Activation tables

The two tables at the end of the main file are indexed by PSDL room number. The ambient table
lists, for a room, the roads that carry ambient traffic while a player is in that room; the
pedestrian table does the same for pedestrians. When a player moves from room *a* to room *b*, roads
in *b*'s list but not *a*'s are populated and roads in *a*'s list but not *b*'s are emptied (the
traffic spec owns the details). Room 0 has empty lists. Only main-file roads appear.

## Load procedure

The game builds the network in this order (each step uses the results of the steps before):

1. **Read** the main file's header, create *NI* intersections and *NR* + *NS* roads (*NS* from the
   supplementary header, 0 without the file).
2. **Read the main roads** in order. For each, register it in the room lists (`road-network.md`,
   "Room lists": its interior centre points, as type *road*).
3. **Read the intersections** in order. For each, register it in its room's list (type
   *intersection*, unless already there) and register its room's heavy props as obstacles
   (`road-network.md`, "Obstacles"). An intersection where at least one of its roads has control 1
   at this intersection gets a traffic-light set (`traffic-control.md`); its light count is the
   number of such roads.
4. **Read the supplementary roads** in order. Each gets road index *NR* + *i* and side flags 3 on
   both sides. Then it joins its end 1 intersection and its end 0 intersection, in that order: for
   each of the two, the road is appended to the intersection's road list and the list is
   **re-sorted**: the intersection's centre becomes the bounding-sphere centre of its PSDL room
   (`psdl.md`, "Room bounding sphere", computed exactly as given there), every road gets the key
   atan2(dx, dz) with dx = *p*.x − *c*.x and dz = *p*.z − *c*.z (float subtractions; the angle kept
   as a float), where *c* is that centre and *p* the road's centre point at the end that touches the
   intersection (centre[*n* − 1] if end 0 names the intersection, else centre[0]), and the roads
   are put in ascending key order (MM2 uses an exchange sort that swaps only on a strictly smaller
   key; the shipped data has no equal keys); then each road's slot at this intersection (end 0 or
   end 1, whichever names it) is set to its new position. Then the road is registered in the room
   lists (type *shortcut*).
   The main file's lists are already in this order around the stored centres, and their slots
   already match. At the intersections that get shortcuts the computed room centres differ from the
   stored ones by up to 0.06 m (London) and 0.2 m (SF); that is enough to move a road lying almost due north of the centre (key near ±π)
   from one end of the list to the other: at 15 SF intersections (18, 21, 29, 44, 47, 49, 99, 141,
   142, 143, 145, 151, 177, 178, 181) the final list is a cyclic rotation of the one a stored-centre
   sort would give. Turn choices only use relative slots and do not change, but the light numbering
   (`traffic-control.md`) does, so the centre must be computed exactly.
5. **Set up the traffic-light sets** (`traffic-control.md`, "Setting up a light set"), in
   intersection order.
6. **Register street props as obstacles** on the main roads (`road-network.md`, "Obstacles").
7. **Read the activation tables.**

MM2 also derives, per road, a list of sharp bends of the centre line during steps 2 and 4; only the
opponent and police driving uses it (their spec). Then, using the `.aimap` files (`aimap.md`;
MM2 reads them before the network):

8. **Mirror for driving on the left**, if the city's aimap says so: every road (main and
   supplementary) with *LA* ≠ 0 (two-way roads; one-way roads are left alone) swaps its lane lines
   between the sides:
   - new side B line *LA* − 1 − *j* = old side A lane *j* with its point order reversed
     (*j* = 0 … *LA* − 1), and new side B line *LA* = old side B line *LA* (the sidewalk, when
     *LA* = *LB*);
   - new side A line *LB* − 1 − *j* = old side B lane *j* reversed (*j* = 0 … *LB* − 1), and new side
     A line *LB* = old side A line *LB*;
   - lane and sidewalk counts, side flags, band edges, edges, cable and subway lines, the centre line
     and the section axes are **not** changed;
   - the line distances of every line of both sides are recomputed: entry 0 = 0, entry *i* =
     entry *i* − 1 + √((dz·dz + dy·dy) + dx·dx), with (dx, dy, dz) = point *i* − point *i* − 1.

   The result: side B traffic still travels in the road direction but on the physical left (+X)
   side, side A traffic against it on the right, lane 0 is now the kerb lane and lane *L* − 1 the
   one next to the centre line. Nothing else of the road changes: in particular MM2 does **not**
   swap the lane counts, line offsets or band edges of the two sides.

   MM2's copies are only well defined when *LA* = *LB* and each side has exactly one sidewalk
   line: it builds side B's new lines in a buffer of *LA* + *WB* lines and copies that many back
   (side A likewise with *LB* + *WA*), so with *LA* ≠ *LB*, or *W* = 0, it reads and writes past
   the ends of the line arrays, and with *W* ≥ 2 the further sidewalk lines are left undefined.
   *Our choice* for those cases: each side keeps its own *L* + *W* lines; each copy above is made
   only when both its source line and its destination line exist, and a line that is not written
   keeps its old points (its distances are still recomputed). Shipped data never needs this:
   every London main road with *LA* ≠ 0 has *LA* = *LB* and *WA* = *WB* = 1, and every shortcut
   has *LA* = 0 (so it is left alone).
9. **Speed limits.** For every road *r* (main and supplementary): if the race aimap has an
   `[Exceptions]` row for road index *r*, the limit is that row's third value; otherwise it is the
   city aimap's `[Speed Limit]`, plus 12.5 if the road has flag 0x04. Units are those of the speed
   limit value (m/s; the traffic spec defines how cars use it).

## Relation to the PSDL

Main road *i* is the same street as the PSDL's AI road *i* (`psdl.md`): the PSDL file has the same
count, and their room lists agree for 532 of London's 540 roads and 375 of SF's 379 (the BAI list
adds a few rooms, e.g. tunnels underneath). The PSDL's own lane counts are not used by the AI.
Every intersection room is a PSDL intersection room (flag 0x10).

## Checked against

`tools/ai/bai.py` (dirty side) parses every `.bai` in `work/data/mm2core/city/` to its last byte
and runs the load procedure:

| File | Intersections | Roads | Rooms in tables |
|---|---:|---:|---:|
| london.bai | 328 | 540 | 1,342 |
| london_sup.bai | — | 70 | — |
| sf.bai | 214 | 379 | 1,172 |
| sf_sup.bai | — | 49 | — |
| london_bak.bai | 328 | 540 | 1,342 |
| sf_bak.bai | 214 | 379 | 1,172 |
| sfai.bai | 215 | 474 | 1,329 |

The two `BAI1` files are not readable with this layout. Room counts equal the PSDL room counts
(1,342 and 1,172 including room 0).

- Lanes (A, B) on main roads, London: (0,1) 129, (0,2) 64, (0,3) 36, (0,4) 23, (1,1) 142,
  (2,2) 118, (3,3) 28. SF: (0,1) 16, (0,2) 23, (0,4) 69, (1,0) 9, (1,1) 110, (2,2) 122, (3,3) 30.
  Every side with no lanes has side-flag bit 0 set. Supplementary roads: A (0 lanes, 0 sidewalks),
  B (1 lane, 0 sidewalks), flags 0, half width 2.5.
- Road flags, London: 0x08 400, 0x0A 69, 0 61, 0x02 10; SF: 0 286, 0x08 63, 0x02 17, 0x01 5,
  0x05 4, 0x0A 3, 0x09 1. Speed limits after load: London 15 on all 610 roads; SF 15, and 27.5 on
  the 4 freeway roads.
- Controls at road ends (main roads): London stop 45, light 519, none 516; SF stop 36, light 533,
  none 189; no yields. All supplementary ends are 3.
- Cable-car lines: SF 21 roads (both sides); subway lines: London 28 roads (side B only).
- Line distances and centre distances equal the cumulative 3D point distances (to 0.05 m) on every
  line. Z[*i*] = −direction(centre[*i* − 1] → centre[*i*]) (unit vectors equal to 0.001) on all
  2,644 London and 2,066 SF sections with *i* ≥ 1 (main and supplementary roads); Z[0] equals Z[1]
  as floats on 556 of 610 London and 387 of 428 SF roads (562 and 390 to 1e-5 per component);
  the 3D length of X − Y × (−Z) is at most 0.02 on every main-road section and exceeds it only on
  14 London and 40 SF shortcut sections. The engine uses the stored axes as they are.
- Point order: side A lanes reversed on all 462 London / 453 SF A-side lanes, A cable lines reversed
  (21), A sidewalks and all edge lines in section order.
- Every intersection's road list is in ascending atan2 order around its stored centre, and the
  slot of every end of a non-loop road equals the road's position in that intersection's list
  (London 1,074 ends, SF 756; the loop roads' two ends share one list entry, so one of their two
  slots matches).
- Sample, London road 88: *n* 4, flags 0x08, half width 11, lanes (2, 2), side B line offsets
  (2.0, 6.0, 9.5), band edges (−11, 4, 4, 7.75, 7.75, 11), kerb line at 8.0 m and outer edge at
  11.0 m from the centre.
- Sample, London intersection 10: room 1023, centre (−230.0, −0.1, 333.5).
- Maps: `work/check/bai_london.png`, `bai_sf.png` (lanes, shortcuts, subway and cable lines,
  intersections, lights, stop signs) and zoomed views `bai_<c>_zoom0/1.png`.
