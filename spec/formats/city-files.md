# City files: inventory, sky, fog and lighting

A city is announced by a small text file `tune/<file>.cinfo` ("City list" below) and made of a
set of files found through the archives (`ar.md`), mostly in `city/`. This page says which cities
exist, lists every file type the shipped cities contain, says which ones the game reads and which a
city viewer needs, and specifies the small text files a viewer needs (sky, fog, lighting). Big
formats have their own pages: `psdl.md`, `inst.md`, `pkg.md`, `tex.md`, `cpvs.md`. The rest get
full specs later.

Names below use `<c>` for the city's level name (its `MapName`; `london`, `sf`). Lookups are
case-insensitive.

## City list (`tune/*.cinfo`)

MM2's list of cities comes from the `tune/` folder, not from `city/`:

1. First `tune/sf.cinfo`.
2. Then every file in `tune/` whose name ends in `.cinfo` (compared without regard to case, so
   `.CINFO` counts too; the part after the **last** dot must be exactly that, so `j01.cinfo.bak`
   does not count; a name that is only `.cinfo` does not count either), in folder-listing order:
   archive by archive in priority order, then in each archive's entry order (`ar.md`, "Several
   archives"). A file in two archives is listed twice; both times the winning copy is read.
3. Each file is read as below. A city is skipped if its file fails to read, or if a city already in
   the list has the same `RaceDir` (compared without regard to case). So the first one wins:
   `sf.cinfo` is read again by the folder walk and dropped, and a mod city whose `RaceDir` repeats
   an earlier one never appears. London is not built in: it comes from the folder walk like a mod
   city. With the shipped data the list is San Francisco, London.

A `.psdl` without a `.cinfo` is not a city; a `.cinfo` whose level files are missing is still
listed (it fails when loaded).

### Reading a `.cinfo`

Text. The game reads exactly nine lines, in this fixed order, each matched against a pattern
(keys are case-sensitive, nothing may stand between the key and `=`):

| Line | Pattern | Value |
|---|---|---|
| 1 | `LocalizedName=` | the rest of the line up to a carriage return: the name shown in the menus |
| 2 | `MapName=` | one word (up to the first white space): the level name `<c>` |
| 3 | `RaceDir=` | one word: the race data name |
| 4 | `BlitzCount=` | integer |
| 5 | `CircuitCount=` | integer |
| 6 | `CheckpointCount=` | integer |
| 7 | `BlitzNames=` | the rest of the line up to a carriage return: race names separated by `\|` |
| 8 | `CircuitNames=` | the same |
| 9 | `CheckpointNames=` | the same |

How a line is read (the same helper as the car `.info`, `car-model.md`): skip any tabs, spaces,
carriage returns and line feeds (so blank lines and indentation are ignored), then take the rest of
the line, at most 255 characters in all, up to and including its line feed. A longer line is cut
there and its remainder is read as the next line. The line must then match its pattern: if the key
does not match, a number does not parse, or a "rest of the line" value is empty (e.g. a bare
`BlitzNames=`, even with a count of 0), the city is rejected. (A `MapName`, `RaceDir` or count
line with nothing at all after the `=` is not rejected by MM2 but leaves the field unset; such a
city cannot load. Reject it.) In a file with bare line-feed line ends the line feed stays at the end
of the "rest of the line" values; the shipped files use carriage return + line feed.

MM2 keeps `LocalizedName`, `MapName` and `RaceDir` in 40-byte fields each, without a length check;
a value of 40 characters or more overwrites the next field, so treat 39 characters as the limit.

Lines after the ninth are never read: the shipped files' `MustPlace` and `UnlockGroup` lines have
no effect, and no other key exists.

**Race counts.** A count that is 0 stays 0 (that race type has no races in this city, and its names
line is ignored but must still be present and non-empty). A count that is not 0 is replaced by the
number of names in its line: 1 + the number of `|` characters (in a double-byte character set, a
lead byte and the byte after it count as one character). Shipped: 10 blitz, 10 circuit and 12
checkpoint races in both cities, matching their counts.

### Which name is used for what

- **`MapName`** is the level: every `city/<c>…` file in the table below (the `.psdl`, `.cpvs`,
  `.sky`, fog, lights, `.inst`, `city/<c>/` prop files and path sets, …) and the name tests MM2 makes
  on the city name (`../physics/city-collision.md`, "Room data computed at load"). The game also
  sets up the AI network only if `city/<MapName>.bai` exists.
- **`RaceDir`** names the race data that the race and AI set-up reads from `race/<RaceDir>/…`
  (the race's `.aimap`, the opponents' files) and the AI files: the network is opened as
  `city/<RaceDir>.bai` and `city/<RaceDir>_sup.bai`, the traffic settings as
  `city/<RaceDir>.aimap`. The exception is the race path set loaded with the level, which is
  `race/<MapName>/<race>.pathset` (table below). `RaceDir` also identifies the city in the list
  (above) and when a city is chosen by name (an unknown name picks the first city).
- **`LocalizedName`** and the race names are display text.

In the shipped files `MapName` and `RaceDir` are the same (`london`, `sf`), so the split only
matters for mods.

## Files the game reads

In the order the game loads a city.

| File | What it is | Viewer needs it |
|---|---|---|
| `city/materials.mtl` | text; physics materials (`mtl name { elasticity, friction, effect, sound, drag, width, height, depth, ptxindex, ptxthreshold }`) | no |
| `city/materials.csv` | text; `texture,physics` rows mapping texture names to those materials (`none` = default) | no (deep-water rooms only) |
| `city/<c>.psdl` | streets, blocks, simple buildings, rooms (`psdl.md`) | **yes** |
| `city/<c>.cpvs` | binary, magic `PVS0`: per room, which other rooms can be seen from it (2 bits per room, run-length coded behind an offset table); decides which rooms are drawn each frame, with a fallback walk when it is missing (`cpvs.md`) | yes, in the "MM2" draw distance (`../engine/viewer.md`) |
| `city/<c>.sky` | one line: sky model and its motion (below) | **yes** |
| `city/<c>_fog.csv` | text; fog colour and range for each time of day and weather (below) | **yes** |
| `city/<c>.lt00` … `.lt15` | text; sun, two fill lights and ambient for each time of day and weather (below) | **yes** (wall shading) |
| `city/<c>.lmap` | binary, magic `LMP0`: one colour per room (`psdl.md`, "Colour"); ignored unless its count matches the PSDL room count (the shipped ones do not) | optional |
| `city/<c>.water` | text; first token the water level (f32, metres; London −3.8, SF −1.9), then room numbers that are deep water | optional |
| `city/<c>/propdefs.csv`, `proprules.csv` | text; street prop types (spacing, models) and the left/right rules picked by the prop byte of each AI road's first room; the props are generated along the sidewalks of the PSDL's AI roads (`street-props.md`) | **yes** (street lights, trees, bins along kerbs) |
| `city/<c>.inst` | placed models (`inst.md`) | **yes** |
| `city/<c>_ai.inst` | placed breakable street furniture (stop signs) (`inst.md`) | **yes** |
| `city/<c>/props.pathset` | binary path set: props placed by hand along paths (trees, bollards, benches; `pathset.md`) | **yes** |
| `city/<c>/decals.pathset` | binary path set: flat decals (road markings, tram rails), each filed in the room under its start (`pathset.md`) | **yes** |
| `race/<c>/…` | race-mode files (waypoints, opponents, race props); the session's race path set `race/<c>/<race>.pathset` (barricades and other race props, `pathset.md`) is loaded at the end of the city load | no |
| `city/<c>.bai` | binary, magic `CAI1`: AI road and intersection network (traffic, police, opponents, pedestrians); full layout in `bai.md`. Opened under the city's `RaceDir` ("City list") | no (Drive mode reads its intersections, "AI network" below) |
| `city/<c>_sup.bai` | binary, magic `CAI1`: supplementary AI network (shortcuts), loaded with the `.bai` (`bai.md`); also under `RaceDir` | no |
| `city/<c>.aimap` | text, `[Section]` blocks: speed limit, drive-on-left, ambient vehicle types and densities, pedestrian models, traffic-light models (`aimap.md`); opened under `RaceDir` | no |
| `city/<c>/circuit<N>.pathset` | older copies of the circuit race path sets (SF only); not read: the game reads `race/<c>/circuit<N>.pathset` (`pathset.md`) | no |
| `texture/…`, `geometry/…`, `bound/…`, `tune/…` | textures, models, collision, tuning files referenced from the above | yes (textures and models) |

## Sky (`city/<c>.sky`)

One text line: `<model> <a> <b> <c>`, e.g. `sky_dome_l 0 0.95 0.005`.

| Field | Meaning |
|---|---|
| model | package `geometry/<model>.pkg`; the game draws its `H` mesh |
| a | height offset, metres |
| b | how much the sky follows the camera up and down (0 = fixed height, 1 = moves with the camera) |
| c | spin speed about the vertical axis, radians per second |

Each frame: angle = (angle + c · frame time) mod 2π (starts at 0 when a race starts). The sky is
placed at (camera x, camera y · b + a, camera z), turned by *angle* about +Y, and drawn before
anything else with depth test and depth writes off, lighting off and **no fog** (fog is switched on
only for the rooms and objects drawn after it).

**Clear colour.** Each frame the screen and depth buffer are cleared to the current fog colour
(below; the very first frame clears to black), so gaps between city surfaces show the fog colour.

Material set (`pkg.md`): (time of day × 4 + weather) mod the package's set count, with time of day
0 morning, 1 noon, 2 evening, 3 night and weather 0 clear, 1 cloudy, 2 foggy, 3 rain. The same index
numbers the fog rows and the `.ltNN` files. Everything else that changes with time of day and
weather (night and wet textures, the ground overlay, glows, headlights) is in `night.md`.

**Sky on or off** is a graphics option, saved with the player's settings (built-in default on;
MM2's hardware check turns it on with a 3D card that has more than 3,072,000 bytes of usable video
memory, or, with the software renderer, on a processor faster than 400 MHz, and off otherwise).
Loading the sky first sets it to "on unless the software renderer is used", but the option is
applied right after the city has loaded, so the option alone decides. No renderer setting stops the
sky when the option is on.

## Fog (`city/<c>_fog.csv`)

Text. A header line, then 16 rows `red,green,blue,start,end,description`, in index order
(clear-morning, cloudy-morning, foggy-morning, rainy-morning, clear-noon, …, rainy-night). Colour
components are 0–255 (alpha 255), start and end are whole-number distances in metres for linear
fog (read as integers); the description is ignored. Stops early at the first unreadable line. Example: London clear noon =
155, 172, 195, from 900 m to 1000 m; foggy noon = 100, 110, 120, from 15 m to 75 m.

The row used is the same index as the sky's material set. The fog actually applied is linear
per-vertex fog from min(start, far − 30) to min(end, far), where *far* is the camera's far plane
(`../engine/cameras.md`, "Projection": the player's view distance, 600 m with the shipped profile,
so London's clear-noon fog runs 570 → 600 m).

## Lighting (`city/<c>.lt00` … `.lt15`)

One file per index (above). Text: a `type: a` line, then a block named after the condition:

```
clear-morning {
  KeyHeading 2.2
  KeyPitch -0.2
  KeyColor 0.9 0.9 0.8
  Fill1Heading 3.14   Fill1Pitch -0.025   Fill1Color 0.5 0.3 0.2
  Fill2Heading -1.2   Fill2Pitch -0.5     Fill2Color 0.6 0.6 0.8
  Ambient -14803406
}
```

- Three directional lights: key (the sun or moon) and two fills. Heading h and pitch p (radians)
  give the direction the light travels: (cos h · cos p, sin p, sin h · cos p), not re-normalised
  (it is already unit length). Negative pitch points down. Colours are 0–1 per channel; the game
  sets each light's diffuse colour to the file colour times a scale that is 1, 1, 1 (only when a
  room colour file is accepted, `psdl.md`, are objects lit with the colours scaled by their room's
  colour).
- `Ambient` is a 32-bit ARGB colour written as a signed decimal (−14803406 = 0xFF1E1E32; red is
  bits 16–23). The game keeps one ambient per number of lights the lighting detail setting allows:
  3 lights → the file's value; 2 → each channel a + ((255 − a)·84 >> 8); 1 → a + ((255 − a)·168 >> 8);
  0 → white. (MM2 computes the 2- and 1-light values just before reading the file, so they come from
  the previous contents of that slot: the built-in default or the previously loaded city. With 3
  lights this does not matter.)
- Before any city is loaded every condition holds the built-in defaults listed in `psdl.md`
  ("When MM2 builds the table").

These lights shade the PSDL building walls (`psdl.md`, wall light table; note when MM2 builds that
table) and light the models every frame with the current conditions.

## AI network (`city/<c>.bai`): just enough to find intersections

The full layout and load procedure are in `bai.md` (which supersedes this summary); what the AI
derives from it is `../ai/road-network.md`. The free-roam start (`controls.md`)
needs only the intersections, which come after the road records, so a reader must step over the
roads. Little-endian; "×" sizes are byte counts.

| Size | Contents |
|---|---|
| 4 | ASCII `CAI1` |
| u16 | intersection count *NI* |
| u16 | road count *NR* (equals the PSDL's AI road count) |
| — | *NR* road records |
| — | *NI* intersection records |
| — | more data (not needed here) |

Road record, with *n* = its section count:

| Size | Contents |
|---|---|
| u16 | road index |
| u16 | *n* |
| u16 | flags (bits 0x02 and 0x04 matter for the start rule; meaning belongs to the AI spec) |
| u16 | *c*, then *c* × u16 |
| 8 | two f32 |
| 5 × u16 | *L1*, *a*, *b*, *L2*, unused |
| (*L1*+*L2*)·*n* × 4, then (*L1*+*L2*) × 4, then 40, then (*L1*+*L2*)·*n* × 12 | lane data, one side |
| *n* × 12 if *a* ≠ 0; *n* × 12 if *b* ≠ 0; then *n* × 24 | |
| 5 × u16 | *R1*, *c2*, *d2*, *R2*, unused |
| (*R1*+*R2*)·*n* × 4, then (*R1*+*R2*) × 4, then 40, then (*R1*+*R2*)·*n* × 12 | lane data, other side |
| *n* × 12 if *c2* ≠ 0; *n* × 12 if *d2* ≠ 0; then *n* × 24 | |
| *n* × 4, then 5 × *n* × 12 | |
| 2 × 38 | the two ends: u32 intersection index, 3 × u16, u32, 12, 12 |

Intersection record:

| Size | Contents |
|---|---|
| u16 | intersection index (0, 1, 2, … in file order) |
| u16 | PSDL room number (an intersection room) |
| 3 × f32 | position (centre of the intersection, on the road surface) |
| u16 | road count *m* |
| *m* × u32 | indices of the roads meeting here |

`city/<c>_sup.bai` (same magic) adds roads that are not needed for this.

Checked: London 328 intersections / 540 roads, SF 214 / 379; walking the roads by this table lands
exactly on intersection 0, indices run 0 … *NI* − 1, every road index is below *NR*, every room is a
PSDL intersection room (flag 0x10).

## Leftovers the game never reads

Shipped with the data but not referenced by the game; a loader should ignore them:
`city/<c>.ext` (4 numbers, extents), `city/<c>.extra` (room/model lists), `city/<c>.reset`,
`city/<c>.rid`, `city/<c>.geo`, `city/<c>.pvs` and `.pvs.342` (uncompressed visibility),
`.pvshist`, `sf082100.*`, the other `*.cpvs` (`<c>_0.cpvs` … `<c>_255.cpvs`, `_bad`, `_00`),
`*_bak.bai`, `*.bai.tmp`, `*.bai.bak`, `london_bridge.tmp`, `city/<c>.sdl_ai.inst`, `sf1_ai.inst`,
`sf2_ai.inst`, `sfai.*`, `variant.*`, `city.psdl`/`city.inst`, `*_chop.csv`, `sf_lights.csv`,
`*_fog_orig.csv`, `*.ldef` (ambient-lighting source images), `race0.pathset` in `city/`, `groups.txt`,
`getcsv.bat`, `*.lnk`, `vssver.scc`, `city/phys/` (an older copy of the per-city CSVs), `city/sf/bak/`,
`city/sf/backup.pathset`, `city/sf/sf.grp`, `PROPDEFS02.CSV.txt`, `props.csv.txt`,
`decals_bad.pathset`, `decals_good.pathset`, `decals01.pathset`, and the editor inputs `facades.csv`,
`floors.csv`, `walls.csv`, `lighting_ignore.csv` (in `city/`, `city/<c>/` and `city/phys/`).
`city/props.csv` and `city/<c>/props.csv` (lists of race prop models) are not referenced by name
either; they may be reached another way and are left for the race spec. `city/<c>/audio_pathsets/`
(ambient sound paths) is never read (`pathset.md`).

## Checked against

- Every file listed exists for both cities in `mm2core.ar` except `circuit<N>.pathset` (SF only)
  and `city/<c>/audio_pathsets/` contents (London: 1 file, SF: 6).
- `.sky`: `london.sky` = `sky_dome_l 0 0.95 0.005`, `sf.sky` = `sky_dome 0 0.95 0.005`; both models
  exist in `geometry/`.
- `_fog.csv`: both have the header plus 16 rows.
- `.lt00`–`.lt15`: 16 files per city, each with the ten keys above.
- `.lmap`: `LMP0`, London 1,341 entries (PSDL 1,342 rooms), SF 1,125 (PSDL 1,172): both rejected.
- `.cpvs`: `PVS0`, count 1,343 (London) and 1,173 (SF) = room count + 1 (decoded in `cpvs.md`).
- `.cinfo`: `tune/london.cinfo` and `tune/sf.cinfo` have the nine lines in order (carriage return +
  line feed ends) followed by `MustPlace=3` and `UnlockGroup=3`; their counts (10, 10, 12) equal
  the numbers of names. `tune/j01.cinfo.bak` and `tune/phys.cinfo.bak` are not listed.
- `.bai`, `_sup.bai`: magic `CAI1`.
- `.water`: London −3.8 then rooms 345, 351, 356; SF −1.9 then 228, 399, 401.
- The game's strings contain no reference to `ext`, `extra`, `reset`, `rid`, `geo`, `pvshist`,
  `chop`, `ldef`, `grp`, `sdl_ai`, `phys`, `facades`, `floors`, `walls`, `audio_pathsets`.
