# Player files: drivers, options, records, hall of fame, rewards

MM2 keeps everything about its players in loose files in a `players` folder next to the game
(not in the archives). This page gives their exact byte layouts, the check value that guards
them, how MM2 reads and repairs them, and how our engine imports a player's MM2 profile. What the
game does with them (which races and cars open, what a race writes, when files are saved) is
`../game/progression.md`. The options inside the `.cfg` that drive the car are explained in
`../engine/controls.md`, "Options in `players/<name>.cfg`"; this page lists them with their
place in the file.

All numbers are little-endian. `u8/u32/i32` are unsigned/signed integers, `f32` an IEEE single.
"Text *n*" is a fixed field of *n* bytes holding zero-terminated 8-bit text in the code page of
the player's Windows (Windows-1252 for the Western versions). MM2 often writes such a field
straight from a shorter string in memory, so **the bytes after the terminating zero are
leftovers** (the shipped files have them: `DriverX` followed by junk). Read up to the first zero;
when writing, fill the rest with zeros (MM2 accepts that). The leftovers still count for the
check values below, so a check value is always computed over the bytes exactly as stored.

## Files

| File | What |
|---|---|
| `players/players.dir` | the list of drivers and the last one used |
| `players/<file>.sav` | one driver: name, skill level, last car, paint, mode, race, city |
| `players/<file>.cfg` | that driver's options: audio, graphics, controls, key bindings, view settings |
| `players/<city>/<file>.rec` | that driver's results in one city: passed races, best times, scores |
| `players/<city>/amateur.dat`, `pro.dat` | the city's hall of fame (top five per race), one per skill level |
| `race/<city>/<city>_rewards.csv` (in the archives) | which car or paint job each kind of win unlocks |

`<file>` is the driver's file name from `players.dir` (`player0`, `player1`, …), not the
driver's name. `<city>` is the city's `RaceDir` (`city-files.md`, "Reading a `.cinfo`"; `sf`,
`london`). The shipped game includes one driver (`DriverX`, file `player0`) with a few results,
and empty halls of fame for both cities.

## Check value

The `.sav`, `.rec` and hall-of-fame files protect their contents with a 32-bit check value. It
is a CRC-32 in the usual reflected form but with an unusual polynomial:

- table: for each byte value *i* (0–255): *c* = *i*; 8 times: *c* = (*c* >> 1) XOR (0xED7282A0 if
  the low bit of *c* was 1, else 0); entry *i* = *c*. (0xED7282A0 is the polynomial in reflected
  bit order; in normal order it is 0x05414EB7.)
- start with *s* = 0xFFFFFFFF; for each byte *b* of the covered fields, in the order the field
  lists below give: *s* = (*s* >> 8) XOR entry[(*s* XOR *b*) AND 0xFF];
- the check value is NOT *s* (all 32 bits inverted).

Every check starts afresh. Test values: the nine bytes `123456789` give 0xC57B26A2; 92 zero bytes
(an empty city record) give 0x695A3C42; 128 zero bytes (an empty hall-of-fame entry) give
0x3DE0A897.

## `players/players.dir`

| Offset | Size | Contents |
|---|---|---|
| 0 | u32 | version, 6 |
| 4 | u32 | *n*, the number of drivers (1 to 18) |
| 8 + 80·*i* | text 40 | name of driver *i* (*i* = 0 … *n* − 1, in the order they were created) |
| 48 + 80·*i* | text 40 | file name of driver *i* |
| 8 + 80·*n* | text 80 | name of the driver used last |

Size 88 + 80·*n* (168 for the shipped file). No check value.

Reading: if the file is missing, its version is not 6, or *n* is 0, MM2 treats it as absent and
starts with no drivers (it then creates the default driver, `../game/progression.md`, "Saving").
Names are compared byte for byte, case included. If the last-used name is not one of the drivers,
the driver at the end of the list is used.

**Rules for the list** (the new-driver screen applies them):

- at most 18 drivers; an empty name is refused; a name already in the list is refused;
- a new driver is added at the end. Its file name is `player` + a number *k* found like this:
  *k* = 0; repeat *n* times: go through the existing drivers in order and, each time one's file
  name equals `player`*k*, add 1 to *k*. (With `player0` and `player1` taken, in either order,
  this gives `player2`.)
- the last remaining driver cannot be deleted.

## `players/<file>.sav`

Version 3, 345 bytes:

| Offset | Size | Contents |
|---|---|---|
| 0 | u32 | version: 3. Version 2 files are still read (they end before the last field, which then counts as empty); versions below 2 are refused |
| 4 | u32 | check value (checked only when the version is 3) |
| 8 | f32 | **stamp**: the game clock in seconds when the driver was created (0.0 for a driver made the moment the game first starts). The driver's `.rec` files carry the same number and are refused if it differs |
| 12 | u8 | skill level: 0 Amateur, 1 Professional |
| 13 | text 80 | car: `BaseName` of the car last chosen; empty for a driver that never left the menus |
| 93 | i32 | paint: colour number of that car (`car-model.md`, "Paint (material set) choice") |
| 97 | i32 | last game mode (numbers below) |
| 101 | i32 | last race: race number within that mode, from 0 |
| 105 | text 40 | driver name (as in `players.dir`) |
| 145 | text 40 | network name: the name shown to others in multiplayer |
| 185 | text 40 | file name (as in `players.dir`) |
| 225 | text 80 | city: `RaceDir` of the city last chosen |
| 305 | text 40 | the network address last typed in the multiplayer "join by address" box (version 3 only) |

Covered by the check value, in this order: stamp, skill, car, paint, mode, race, city, name,
network name, file name, address.

**Game mode numbers** (used here, in the `.rec`, in the rewards file and by the menus):

| Number | Mode | Word in the rewards file |
|---|---|---|
| 0 | Cruise | `roam` |
| 1 | Checkpoint race | `race` |
| 2 | Cops and Robbers (multiplayer) | `multicop` |
| 3 | Circuit | `circuit` |
| 4 | Blitz | `blitz` |
| 5 | multiplayer Cruise | `croam` |
| 6 | Crash Course | `crash` |

A `.sav` that cannot be read (missing, version below 2, or version 3 with a wrong check value)
makes MM2 throw the driver away when it is selected: the driver is removed from `players.dir` and
its `.cfg` and `.rec` files are deleted. (Its `.sav` itself survives, because MM2 still holds it
open at that moment.) So hand-edited `.sav` files lose the driver. The exception is the **last**
driver of the list, which MM2 never deletes: it stays in `players.dir` with its files, and MM2 goes
on without a current driver (`../game/progression.md`) — at every start, as long as the file stays
broken. `players.dir` is therefore never left empty. After a deletion, if the failed file still
carried the driver's name (a check-value failure, not a missing or too-old file), MM2 loads the
first driver of the list instead, which is then current if its `.sav` reads; otherwise no driver
is current until one is chosen.

## `players/<file>.cfg`

| Offset | Size | Contents |
|---|---|---|
| 0 | u32 | 1234 |
| 4 | u32 | version, 31. With any other version only these 8 bytes are used: the driver keeps the options in effect and the file is rewritten with them later |
| 8 | f32 | sound-effects volume, 0 … 1 (1.0) |
| 12 | f32 | music volume, 0 … 1 (1.0) |
| 16 | f32 | left/right balance, −1 … 1 (0.0) |
| 20 | u32 | audio option bits (below) |
| 24 | u32 | number of sound channels: 8, 16 or 32 (kept as a 16-bit number) |
| 28 | u32 | *n*: length of the next field |
| 32 | *n* | name of the sound device, **including** its terminating zero (so *n* = length + 1; 37 in the shipped file). MM2 does not limit *n* when reading; treat more than 200 as damaged |
| 32 + *n* | 56 | graphics options: 14 four-byte fields (below) |
| 88 + *n* | 13 × 4 | control options (below) |
| 140 + *n* | 170 × 12 | key and button bindings (`../engine/controls.md`) |
| 2180 + *n* | 12 | view settings, one byte each (`../engine/controls.md`) |

Size 2192 + *n* (2229 shipped). No check value. A missing `.cfg` is not an error: MM2 writes one
from the options in effect.

**Audio option bits** (offset 20). Program default 0xC73; the shipped file has 0xCF3. The
audio options screen (`../game/menus.md`) edits them:

| Bit | Meaning |
|---|---|
| 0x001 | sound effects on |
| 0x004 | music on |
| 0x010, 0x020 | sound quality: neither = low (8 channels), 0x010 = medium (16), both = high (32; 0x020 only if the sound hardware allows it). The channel count at offset 24 follows the same choice |
| 0x040 | stereo (clear = mono) |
| 0x100 | surround (with 0x040; offered only when the hardware has at least 8 positional voices) |
| 0x400 | race commentary (announcer speech) on |
| 0x800 | city ambience instead of music while driving |
| 0x080 | not an option: the game sets and clears it every frame while the player is in an enclosed room (it ends up saved in whatever state the last frame left it) |
| 0x002, 0x200 | set by default; passed to the sound system's set-up, no option changes them |

**Graphics options** (offset 32 + *n*), field *k* at 32 + *n* + 4·*k*. Display adapter,
renderer and screen resolution are not in this file (the graphics set-up keeps them in its own
configuration). Meanings and the screens that set them: `../game/menus.md`, "Options".

| *k* | Type | Contents | Program default | Shipped |
|---|---|---|---|---|
| 0 | i32 | texture quality, 0 (lowest) – 3 | 1 (then auto-detected) | 2 |
| 1 | i32 | object detail 0 – 3 | 2 | 0 |
| 2 | i32 | unused (never written; keep as read) | — | 0 |
| 3 | i32 | car reflections on (0/1) | 1 | 1 |
| 4 | i32 | cloud shadows 0 none, 1 low, 2 high | 1 | 1 |
| 5 | i32 | textured sky on (0/1) | 1 | 1 |
| 6 | f32 | view distance in metres, 100 – 1000: in a game it replaces the cameras' far plane (`../engine/cameras.md`, "Projection") | 400 | 600.0 |
| 7 | f32 | lighting quality, a whole number 0 – 3 | 3.0 | 2.0 |
| 8 | f32 | object detail / 3 + 0.25 (MM2 recomputes it from field 1 when a game starts; the stored value is ignored) | — | 0.25 |
| 9 | i32 | pedestrians switched **off** in the graphics options (1 = off) | 0 | 0 |
| 10, 11 | i32 | leftovers of the previous game (never changed, no effect) | 0 | 0, 0 |
| 12 | i32 | unused (never written) | — | 0 |
| 13 | i32 | "portals" switch (hidden in the options; no effect found) | 1 | 1 |

**Control options** (offset 88 + *n*; meanings and defaults: `../engine/controls.md`, "Options
in `players/<name>.cfg`"):

| Offset | Type | Contents | Shipped |
|---|---|---|---|
| 88 + *n* | u32 | control device: 0 mouse, 1 keyboard, 2 joystick, 3 gamepad, 4 wheel | 3 |
| 92 + *n* | u32 | gearbox: 1 automatic, 0 manual | 1 |
| 96 + *n* | f32 | "physics realism" (unused; a new driver gets 0.75) | 0.75 |
| 100 + *n* | u32 | automatic reverse: 1 on | 1 |
| 104 + *n* | u32 | joystick hat ("point of view") switch, offered only for a joystick with a hat | 0 |
| 108 + *n* | u32 | force feedback on | 0 |
| 112 + *n* | f32 | force-feedback collision strength, 0 … 2 | 1.0 |
| 116 + *n* | f32 | force-feedback road strength, 0 … 2 | 1.0 |
| 120 + *n* | f32 | overwritten by the game every frame; ignore | 1.0 |
| 124 + *n* | f32 | controller dead zone, 0 … 0.33 | 0.1 |
| 128 + *n* | f32 | unused (shipped: leftover bytes) | — |
| 132 + *n* | f32 | keyboard steering target *K* | 1.0 |
| 136 + *n* | f32 | steering sensitivity factor *U* = 1 / "Steering Sensitivity" | 1.0 |

A new driver's control options are the ones in effect when it is created (program defaults on a
first start: keyboard, automatic, automatic reverse on, force feedback on, dead zone 0.1, *K* 1,
*U* 1).
What the force-feedback options and the dead zone do: `../engine/controls.md`, "Force feedback"
and "The game controller".

Then the binding table, 170 entries of 12 bytes (u32 kind, u32 input device, u32 component;
entry *d* × 34 + *a* for device *d* 0 – 4 and action *a* 0 – 33), and the twelve **view bytes**
(offset 2180 + *n*), both specified in `../engine/controls.md`:

| Byte | Meaning | New driver | Shipped |
|---|---|---|---|
| 0 | external camera: 0 near chase, 1 bumper, 2 far chase | 0 | 0 |
| 1 | HUD map mode 0 – 3 (`../game/hudmap.md`) | 0 | 0 |
| 2 | wide view on | 0 | 0 |
| 3 | dash view on | 0 | 0 |
| 4 | rear-view mirror on at game start | 0 | 0 |
| 5 | gauges overlay on | 0 | 1 |
| 6 | orbit view on (never used) | kept | 0 |
| 7 | opponent markers on | 1 | 1 |
| 8, 9 | unused | 0, kept | 0, 0 |
| 10 | map heading-up | kept (program default 1) | 1 |
| 11 | map zoomed in (replaced by the city's `ZoomIn` at each game start) | kept (program default 0) | 0 |

## `players/<city>/<file>.rec`

One per driver and city. Results of the four single-player race types:

| Offset | Size | Contents |
|---|---|---|
| 0 | f32 | version, 2.0 |
| 4 | u32 | check value of bytes 8–43 |
| 8 | f32 | stamp: must equal the driver's `.sav` stamp |
| 12 | 4 × i32 | number of races of each type, in the order checkpoint, circuit, blitz, crash course |
| 28 | 4 × u32 | **passed bits** in the same order: bit *r* set = race *r* of that type passed |
| 44 | 96 each | one record per race: all checkpoint races in order, then circuit, blitz, crash course |

Size 44 + 96 × (sum of the four counts); 4,364 for 12 + 10 + 10 + 13. The record of race *r* of
type *t* starts at 44 + 96 × (*r* + the counts of the types before *t*).

The three race counts are the city's (`.cinfo` `CheckpointCount`, `CircuitCount`,
`BlitzCount`); the crash-course count is always **13**, whatever the city.

A record:

| Offset | Size | Contents |
|---|---|---|
| 0 | u32 | check value of bytes 4–95 |
| 4 | f32 | best time in seconds (Circuit: the best lap); 0 = no result yet (crash courses store 1.0 once tried) |
| 8 | text 80 | `BaseName` of the car that set the best time |
| 88 | i32 | passed (0 or 1) |
| 92 | i32 | best score |

Reading rules:

- The header counts only if the version is 2.0, its check value matches **and** its stamp equals
  the driver's. Otherwise MM2 treats the file as missing. (When a driver is selected MM2 then
  writes a fresh empty one for `sf` and `london`, losing the old results.)
- If the three race counts in the header differ from the city's current counts (a mod changed the
  race list), MM2 empties the file (new header, all records empty) the next time it shows the
  driver's records.
- A record whose check value fails is replaced, the moment it is read, by an empty one (all zero,
  with its correct check value), and its passed bit is cleared; both are written back.
- A fresh file: version 2.0, the driver's stamp, the four counts, all passed bits 0, all records
  zero with check value 0x695A3C42.
- The passed bits decide everything (unlocks, rewards); the record's own passed field is kept
  equal to its bit.

The shipped `players/sf/player0.rec`: blitz 0 and 1 passed (26.15 s and 49.55 s in vpford,
score 750 each). `players/london/player0.rec`: crash course 0 passed, 1 tried (vpcab).

## `players/<city>/amateur.dat` and `pro.dat`

The hall of fame: for every checkpoint, circuit and blitz race of the city, the five best times
and the five best scores of all drivers, kept separately for the two skill levels (`amateur.dat`
for results at Amateur, `pro.dat` at Professional). Crash courses have none.

| Offset | Size | Contents |
|---|---|---|
| 0 | u32 | version, 1 |
| 4 | 3 × u32 | number of races: checkpoint, circuit, blitz |
| 16 | 132 each | 10 entries per race: checkpoint races in order, then circuit, then blitz |

Size 16 + 1,320 × races (42,256 for 12 + 10 + 10). Within one race's ten entries, entry 2*k* is
rank *k* (0 = best) of the **time** table and entry 2*k* + 1 is rank *k* of the **score** table.
Entry *e* of race *r* of type *t* starts at 16 + 132 × (10 × (*r* + the counts of the types
before *t*) + *e*).

An entry:

| Offset | Size | Contents |
|---|---|---|
| 0 | u32 | check value of bytes 4–131 |
| 4 | text 40 | driver name |
| 44 | text 80 | car `BaseName` |
| 124 | f32 | the time in seconds (time table) or the score stored as a float (score table); 0 = empty |
| 128 | u32 | passed (1 if that run passed the race) |

Reading rules: version must be 1; an entry whose check value fails reads as empty (when a result
is being added, every later entry of that race counts as empty too, `../game/progression.md`).
If the three counts differ from the city's, the file is rebuilt empty. A fresh file has the
counts and all entries zero (check value 0x3DE0A897).

## `race/<city>/<city>_rewards.csv`

Text, read from the archives. The first line is a header and is skipped. Each further line is
read up to 127 bytes (a longer line is cut there and its remainder read as the next line) and
split at commas; runs of commas count as one, so empty fields cannot be expressed:

| Field | Contents |
|---|---|
| 1 | race type word: `race`, `circuit`, `blitz`, `crash` (also `roam`, `multicop`, `croam`, which nothing uses), any case. A line whose first field starts with `#` is a comment; a line whose word is unknown is ignored |
| 2 | condition: `half`, `all` (any case), or a race number (decimal, from 0) |
| 3 | car `BaseName` (exact, case-sensitive). A line naming a car that is not in the car list is ignored |
| 4 | variant: 0 = the reward is **the car itself**; *v* > 0 = the car's paint job number *v* |
| 5 | message shown on the results screen when the reward is earned (optional; the rest of the line up to the next comma, so it cannot contain commas; without a trailing comma the line end stays in it) |

At most 32 rows are taken: reading stops right after the 32nd accepted row (the header, comments and
ignored lines do not count). Numbers are read like C `atoi` (leading digits; anything else is 0).
The meaning of the conditions is in `../game/progression.md`, "Locked cars and paint jobs". Shipped
(both cities): blitz/circuit/race `half` unlock a car and `all` a paint job of that car; crash
courses 3, 7 and 11 unlock paint jobs and 12 a car.

## Importing an MM2 profile

To take over a player's MM2 progress, our engine reads the player's own `players` folder:

1. Read `players.dir` (rules above). If it is unreadable there is nothing to import.
2. For each driver, read its `.sav`. Skip (and report) a driver whose `.sav` fails, as MM2 would
   have dropped it (MM2 keeps the last one listed but never loads it). If every driver fails there
   is nothing to import; never end up with an empty driver list. An empty car means `vpbug` with
   paint 0; an unknown car (mod removed) is handled like that too.
3. Read its `.cfg` (version 31) for options and view settings; a missing or older file means
   default options.
4. For every city whose `RaceDir` folder exists, read `<city>/<file>.rec`. Use it only if its
   header is valid and its stamp equals the `.sav` stamp; repair bad records as above (empty
   them). If the race counts no longer match the city's, MM2 would empty it; import it as empty.
5. Read `amateur.dat` and `pro.dat` of each city with the rules above.

All of MM2's decisions (open races, locked cars, totals) follow from these files alone, as
`../game/progression.md` describes; nothing else needs importing. Writing the files back in
these layouts keeps the profile usable by MM2 itself.

## Checked against

- `tools/game/profiles.py` decodes every shipped player file with these layouts and checks every
  check value with the polynomial above; all pass (`work/check/profiles.txt`).
- `players.dir`: 168 bytes, version 6, one driver `DriverX` / `player0`, last driver `DriverX`.
- `player0.sav`: version 3, check value 0xEE044F88 (computed equal), stamp 0.0, Amateur, car
  `vppanoz`, paint 2, mode 4 (Blitz), race 3, city `sf`.
- `player0.cfg`: 2,229 bytes, *n* = 37; volumes 1.0/1.0, balance 0, bits 0xCF3, 32 channels;
  control options as `../engine/controls.md` lists; view bytes `00 00 00 00 00 01 00 01 00 00 01 00`.
- Both `player0.rec`: version 2.0, counts 12/10/10/13, size 4,364, header and all 45 record check
  values valid, passed bits equal to the records' passed fields (SF blitz bits 0x3, London crash
  bit 0x1).
- All four halls of fame: 42,256 bytes, counts 12/10/10 (= the `.cinfo` counts), all 1,280 entry
  check values valid; only SF Amateur has entries (blitz 0 and 1, rank 0 time and score, DriverX in
  vpford: 26.145 s and 49.548 s, score 750), matching the `.rec`.
- Both rewards files: 10 rows each, every car known, every paint number below the car's number of
  `Colors`.
