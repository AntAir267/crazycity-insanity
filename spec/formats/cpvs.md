# Room visibility (`.cpvs`, "PVS0")

Each frame MM2 draws only a list of **rooms** (`psdl.md`): their PSDL surfaces and everything filed
in them (objects, props, cars; `inst.md`, "Drawing"). This page says how the list is made. With a
`city/<city>.cpvs` file the list comes from a precomputed table, one row per room, that says which
rooms can be seen from it; without one MM2 walks the room graph outward from the camera's room.
Both ways a room is only taken if its bounding sphere is in view.

Shipped: `london.cpvs`, `sf.cpvs`. The other `*.cpvs` files in `city/` are never read
(`city-files.md`, "Leftovers").

## File layout

Little-endian.

| Size | Contents |
|---|---|
| 4 | ASCII `PVS0`; with anything else the file is ignored (as if there were none) |
| u32 | *C*, the table size. Shipped files have *C* = PSDL room count + 1 (London 1,343, SF 1,173) |
| (*C* − 1) × u32 | offsets O₀ … O_{C−2}, counted from the start of the row data |
| rest of the file | row data; it starts at file offset 4·*C* + 4 |

The row of room *r* (1 ≤ *r* ≤ *C* − 2) is the bytes from O_{r−1} up to, but not including, O_r of
the row data. So O₀ = 0, and the last offset is the length of the row data (London 113,958 bytes,
SF 122,837). Room 0 has no row. MM2 does not compare *C* with the PSDL; a file whose table has no
row for a room the camera can be in is broken (MM2 then reads outside its table): treat it as
missing.

The game reads the city's own file, `city/<city>.cpvs`. The command-line option `pvs` with a name
makes it read `city/<name>.cpvs` instead.

### Decoding a row

A row is run-length coded. Start with an empty output and read codes until the row's bytes are
used up:

| Code byte *b* | Meaning | Bytes used |
|---|---|---|
| *b* < 0x80 | the next byte, repeated *b* times (*b* = 0 adds nothing) | 2 |
| *b* ≥ 0x80 | the next *b* − 127 bytes, copied as they are | *b* − 126 |

A code is always used whole, even if it runs past the end of the row (shipped rows end exactly at a
code boundary). Then pad the output with zero bytes up to *B* = (*NR* + 3) div 4 bytes, where *NR* is
the PSDL room count (including the empty room 0). Only the first *B* bytes are looked at. (MM2
decodes into a 512-byte buffer without checking, so it can only handle rows of up to 512 decoded
bytes, i.e. cities of up to 2,048 rooms.)

The value of room *j* in a decoded row is

v(j) = (byte[*j* div 4] >> (2 · (*j* mod 4))) & 3,

so byte 0 holds rooms 0–3, room 0 in its lowest two bits. **Any non-zero value means "can be seen"**:
the shipped files use only 0 and 3, and 1 and 2 count as visible too. A writer must leave room 0
and the room numbers ≥ *NR* in the last byte at 0 (MM2 would try to visit a room that does not
exist). In the shipped files: room 0 is never marked, nothing lies beyond *NR*, and a row marks at
most 209 rooms (London) or 219 (SF), 63 and 82 on average.

## The camera point and the camera room

1. **Camera point** *P* = eye + near · (R.z, U.z, F.z), where *eye* is the camera position, *near* the
   near-plane distance (`../engine/cameras.md`, "Projection"), and R, U, F are the view's right, up and
   forward (viewing) directions in world space; (R.z, U.z, F.z) are their z components. With the
   camera pose of `../engine/cameras.md` (axes X right, Y up, Z backward) that is
   *P* = eye + near · (X.z, Y.z, −Z.z). For a camera without pitch or roll this is eye + near · F,
   the centre of the near plane; when pitched or rolled it is another point at distance *near* from
   the eye (MM2 takes a column of the view rotation where a row was meant; keep it). The same point is "the camera" in the PSDL drawing tests (`psdl.md`:
   the camera-side test of walls, and "camera not below" for intersection fans, crosswalks and
   roofs).
2. **Camera room** = Find(*P*, hint) of `../physics/city-collision.md`, "Finding the room of a point",
   with the camera room found the last time the city was drawn as the hint. If the answer is 0
   (*P* is outside every room), the previous camera room is kept. If there is no previous one either
   (there is none when the city has just been loaded), no room is drawn that frame; only the sky is.

## Making the list

Each room in the list carries its distance *d* = (depth of its sphere's centre along the viewing
direction) − radius, which picks its level of detail (`psdl.md`, "Levels of detail"); the camera
room counts as depth 0, i.e. *d* = −radius. The list holds **at most 512 rooms**: once it is full,
further rooms are dropped. It is drawn in the order it was made (no sorting).

**View test** of a room: its bounding sphere (`psdl.md`, "Levels of detail": centre and radius) with
the centre in view space (x, y, z), z being the depth along the viewing direction. The room is out
of view when z + radius < near, when z − radius > far, or when the centre lies more than one
radius outside any of the four side planes of the view (left, right, top, bottom). Otherwise it
passes. Objects use the same test for "outside the view" (`inst.md`).

### With a `.cpvs` file

1. Decode the camera room's row (MM2 keeps the decoded row until the camera room changes).
2. Go through the room numbers *j* = 0, 1, 2, … in increasing order up to 4·*B* − 1. Take room *j*
   when v(*j*) ≠ 0 and either *j* is the camera room (no view test) or its sphere passes the view
   test.

The camera room itself is taken only if its own value in its row is non-zero. In the shipped files
London room 363 and SF rooms 813 and 814 are not marked as visible from themselves, so while the
camera is in one of them that room is not drawn.

### Without a `.cpvs` file

MM2 walks the rooms breadth-first from the camera room:

1. Start a queue holding the camera room, and mark it.
2. Take the next room *r* from the queue. If *r* is the camera room, take it into the list;
   otherwise take it if its sphere passes the view test.
3. Whether or not *r* was taken, append to the queue, marking each as it is appended:
   - every unmarked room named in *r*'s perimeter (`psdl.md`, "Room record"; entries in perimeter
     order, 0 = nothing);
   - then every unmarked room on *r*'s overlap list (`../physics/city-collision.md`, "Overlap
     lists"; the list is kept newest first, i.e. in reverse order of being added) that does **not**
     have PSDL flag 0x02.
4. Repeat from 2 until the queue is empty.

So every room connected to the camera room is examined, not only the visible ones; in the shipped
cities that is every room. MM2's queue is a ring of 512 entries; in the shipped cities at most 202
(London) and 162 (SF) rooms are ever waiting, so it never overflows.

### Which way is used

MM2 uses the table once a `.cpvs` with the right magic has been loaded, and the walk otherwise. It
never switches the table off again: a later city in the same session that has no `.cpvs` (or one
with a wrong magic) is drawn with the previous city's rows, which do not fit it. Do not copy that:
use the walk for every city without its own valid file.

## Checked against

- `london.cpvs` and `sf.cpvs` decode as above: every row ends exactly at its next offset, no row
  decodes to more than *B* bytes (336 for London, 293 for SF), only values 0 and 3 occur, room 0 and
  the rooms ≥ *NR* are never marked.
- In both cities a walk over perimeter neighbours alone reaches every room from any start room.
