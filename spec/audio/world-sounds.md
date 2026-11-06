# World sounds: city ambience, ferries, drawbridges, trains, cable cars, rain

The sounds of the city itself: gulls, buoys, tug horns, the cable in the San Francisco streets and
the London Underground announcements (the **city ambience**), the moving level objects that carry
a sound (ferries, drawbridge leaves, Underground trains, cable cars), and the rain and thunder of
a rainy game. Everything here is built on the sound layer of `sound-system.md` (samples, pitch
control, 3D sources and their 4 slots, attenuation *A*, pan, doppler, echo state, sound random
rnd_s, pause and results); this page says what each source sets and when.

Units: metres, seconds. Values read from text files are converted as C `atof` (to double, then
rounded to float32 when stored) and `atoi`; arithmetic is float32 with one rounding per operation
unless a step says otherwise. *dt* is the frame time in seconds. "Listener" is the listener of
`sound-system.md` (the active camera).

## The short version

| Source | Exists when | Position | Samples | Heard |
|---|---|---|---|---|
| City ambient objects | the **ambient** option (bit 0x800) is on at game set-up, city `london` or `sf` | moved to the nearest of its listed points | `aud/aud22/Amb3D/` names from the files | 3D source per file; loops, 2D and 3D one-shots |
| Ferries | single-player games with a ferry path set | the ferry | `ferry.csv`: engine loop, horn every 5–10 s | always (3D) |
| Drawbridge leaves | a bridge path set | the leaf | `drawbridge.csv`: motor loop, bell | while the leaf moves |
| Underground trains (path sets) | a train path set (London ships one) | the train's middle car | `subwaycar.csv`: `LondonTube` loop | while the train is not waiting, and only while the player is in a tunnel |
| Network subway trains | a city aimap with `[Subway]` (none in the retail data) | each car | `subwaycar.csv` | as above, while moving faster than 1 m/s |
| Cable cars (SF) | cable cars run | the cable car | 4 root samples: start, loop, stop, go-bell | by speed (state machine) |
| Rain | weather is rain | none (2D) | `Rainexterior` or `Raininterior` loop by view | always; quieter in tunnels |
| Thunder | rain at night | none (2D) | `Thunder` twice per 16 s cycle | always |

Sailboats carry no sound. The other ambient option effects (the music system's city ambience
segment) are the music spec's; see "The ambient option".

## City ambience

### Which list a game loads

At game set-up, when the player's car is set up (just after the 3D manager is made):

1. If the **ambient** option bit 0x800 (`sound-system.md`, "Options in the player file") is off,
   there is no city ambience for this game. The bit is read only here: changing the option during
   a game has no effect until the next game.
2. The level name (`MapName`, `../formats/city-files.md`, "Which name is used for what") is
   compared byte for byte, **case-sensitively**: exactly `london` gives the list
   `aud/ambient/londonambientcontainer.csv`, exactly `sf` gives `aud/ambient/sfambientcontainer.csv`.
   Any other name (every mod city, and `London` or `SF` spelled with capitals) has **no city
   ambience**, even if it ships `<city>ambientcontainer.csv`. There is no fallback.
3. If the list file does not exist, there is no city ambience (no message).

The default options of a new player have bit 0x800 on (and music off), so by default a game in
London or San Francisco has its city ambience.

### The list file

Lines end with CR LF. Line 1 is a header and is ignored (`file names` in both shipped lists).
Every following line, to the end of the file (no count, no end marker):

1. Cut the line at its first CR or LF. Then take the text up to the first comma (or the whole line
   if it has none) as a **file name**, exactly as written (no trimming of spaces).
   A line that is empty or only CR LF gives the name `\r\n` (MM2 does not cut it), which matches
   no file; our engine may skip empty lines.
2. If `aud/ambient/<name>.csv` exists, make one **ambient object** from it (next sections),
   appended to the city's list. Otherwise skip the line.

A name listed twice makes two independent objects. Shipped: London lists `londonriver`,
`tubevoices`; SF lists `birdies`, `buoyseals`, `horns_gulls`, `trolleycable` (in that order).

### The ambient file

The same format is used by the city's objects and by the files of the ferry, drawbridge and train
objects below. Lines end with CR LF; on every line MM2 first cuts at the first CR or LF and then
splits at commas **the way C `strtok` does: runs of commas count as one separator, so empty
fields are skipped** and the fields after them move left. Fields past the ones listed are ignored.

| Line | Contents |
|---|---|
| 1 | header, ignored (`Min distance,Max distance,3D priority,Audible area,,,,,`) |
| 2 | **min distance** (float), **max distance** (float), **priority** (integer), **audible area** (integer) |
| 3 | header, ignored (`sample name,sample volume,sample type,…`) |
| 4 … | one **sample row** per line, until the end of the file or a row whose first field is `VECTORPOINTS` |
| after `VECTORPOINTS` | one header line, ignored (`x,y,z,,,,,,`); then one **point** per line to the end of the file: x, y, z (floats, world metres) |

Line 2 sets the source's drop-offs (min, max) and its priority (`sound-system.md`, "3D sources").
The audible area is 0 (everywhere), 1 (only while the echo state is on, i.e. the player's car is in
an enclosed room: tunnels and the Underground) or 2 (only while it is off); any other value acts as
0.

Sample row (fields in this order):

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | name | text, case kept | the sample `Amb3D\<name>`, i.e. `aud/aud22/Amb3D/<name>.22k.wav` (`sound-system.md`, "Finding a sound file"). Exactly `VECTORPOINTS` (case-sensitive) ends the rows instead |
| 2 | volume | float | the row's volume *V* |
| 3 | type | integer | 0 loop, 1 2D one-shot, 2 3D one-shot, 3 3D parameters only (below) |
| 4 | low | float | one-shot interval, lower bound (s) |
| 5 | high | float | one-shot interval, upper bound (s) |
| 6 | active | integer | non-zero: the row runs from the start; 0: it is off until its owner switches it on |
| 7 | min speed | float | speed window (below) |
| 8 | max speed | float | speed window |
| 9 | doppler | integer | non-zero: the sample is made **with pitch control** and gets the source's doppler factor; 0: made **without** pitch control |

Each row also has a **timer**, 0 at load. Samples come from the 3D sample cache
(`sound-system.md`, "Samples of 3D sources"); the cache tells apart the same name with and without
pitch control, so files that share a name with the same doppler choice share the cached copies.

Loader rules and quirks (MM2 does no checking):

- A missing field (fewer fields on line 2 or in a sample row, counted after the comma collapsing)
  crashes MM2 (it converts a null field). Our engine: use 0 for a missing number.
- A blank line among the sample rows or the points crashes MM2 the same way. Our engine: skip it.
- A missing file (for the owned objects below) leaves an object with no samples whose max distance
  is invalid: it is never heard.
- A **city** object needs points: without a `VECTORPOINTS` line, or with no point rows after it,
  MM2 crashes the first time the object asks for a slot (it has no position). Our engine: an
  object of the city list without points is not created.
- Points in a file of an owned object (ferry, bridge, train) would move that object's sound to the
  nearest point exactly as for city objects (no shipped owned file has points).

### The ambient object, every frame

State: the rows (above), the source's position (none until a point is chosen), its slot, its own
**echo flag** (off at creation), and the source values *A*, pan and doppler factor *f* from the
last slot update (initially 0, 0 and 1). The owner passes a **speed** *s* every frame; **every
owner in MM2 passes 0** (city objects, ferries, bridges and trains alike), so the speed window only
ever compares 0 with the row's min and max speed.

**Object update** (run for every city object by the 3D manager's update each frame, after the
game-mode update and only when not paused, `sound-system.md`, "Order within a frame"; owned
objects run it in their owner's update, below):

1. Store *s*.
2. Audible area: area 1 and the echo state is off → stop here. Area 2 and the echo state is on →
   stop here.
3. If the 3D manager is disabled or the object already holds a slot → stop here.
4. If the file had points, move the source to the **nearest point**: for each point P in file
   order, D = |P.x − L.x| + (|P.z − L.z| + |P.y − L.y|) with L the listener position (computed in
   double precision, rounded to float32 per point); the first point with the smallest D wins (a
   later point must be strictly nearer).
5. Ask for a slot if d² < max² (`sound-system.md`, "Getting and losing a slot"; ambient objects
   are 3D sources).

So the sound sits at one point and stays there **while it holds its slot**, even if the listener
drives towards another point; it is moved to the then-nearest point only after the slot was given
back (out of range, taken by a higher-priority source, results screen) and before it asks again.
Each file is one source, heard from one place at a time.

**Slot update** (each frame while it holds a slot, by the 3D manager in slot order):

1. **Echo.** If the echo state is on and the object's echo flag is off: turn on the echo copies
   of all its row samples with T = the manager's echo delay (0.5 s) and G = 0.96, F left as made
   (`sound-system.md`, "Echo"); set the flag. If the state is off and the flag is on: turn them off;
   clear the flag.
2. **Area.** Area 1 with the flag off, or area 2 with the flag on: give the slot back ("Losing
   the slot") and stop.
3. If the flag is on, run the echo step of each row sample (the step that carries out its due
   echo repeats, `sound-system.md`, "Echo").
4. Measure the distance. If d² ≥ max², give the slot back and stop.
5. If the file has at least one row of type 0, 2 or 3 (active or not): compute *A*, the pan (in
   stereo) and the doppler factor *f* with **k = 1/56.7166633605957 = 0.017631503**
   (float32 0x3C906FF1; the same as cars) (`sound-system.md`, "Attenuation, pan and doppler").
   Otherwise these keep their old values (unused).
6. For each row in file order whose active flag is non-zero, by type:

| Type | Each slot update |
|---|---|
| 0 loop | apply 3D values; then if s < min speed or s > max speed: stop the sample; otherwise play it looped if it is not playing |
| 1 2D one-shot | run the timer |
| 2 3D one-shot | apply 3D values; then run the timer |
| 3 3D parameters only | apply 3D values (nothing ever plays it: no MM2 code fires such a row) |
| other | nothing (MM2 prints an error every frame) |

**Apply 3D values**: volume = *V* × *A*; if the row's doppler field is non-zero, pitch factor = *f*;
in stereo, pan = the source pan.

**Run the timer** (types 1 and 2): if min speed ≤ s ≤ max speed:
1. If timer ≤ 0: **fire** the row, then draw a new timer: if low ≠ high, timer = rnd_s(low, high),
   else timer = high.
2. timer = timer − *dt*.

Outside the speed window the timer is frozen. Timers start at 0, so every one-shot fires on the
first slot update after the game starts, then every low…high seconds of slot time. Timers keep
their value while the object has no slot (they are not run) and are never reset.

**Fire**:
- Type 2 (and 3): volume factor *A*, pan = the source pan, pitch *f*.
- Type 1: volume factor = rnd_s(0.75, 1.0), pan = rnd_s(−1, 1), pitch 1.
- Then: volume = *V* × factor; in stereo set the pan; if the row's doppler field is non-zero set the
  pitch; **play once if the sample is not playing** (a shot that comes while the previous one
  still plays is lost; the timer is re-drawn anyway).

The draws of one shot (volume, pan, then the new timer) are made in the same second, so they use
the same *r*: a type-1 shot's pan is 2r − 1 and its volume factor 0.75 + 0.25r (louder shots are
further right), and its next interval is low + (high − low)·r.

Note the asymmetry: a type-1 sound is not attenuated or positioned; it is heard at full (random)
volume, anywhere on the stereo field, whenever the object holds a slot, i.e. whenever the listener
is within max distance of the chosen point.

**Switching rows on and off** (only the owners below do this): switching on sets the active flag.
Switching off clears it and, for a type-0 row holding a sample that plays, stops it; one-shots
already playing finish.

**Losing the slot**: turn the echo copies off if the flag is on (clear it), stop every row sample
that is playing (all types) and let go of the samples. Loops start again on the first slot update
after the next slot is obtained.

**Reset** (owned objects only, when their owner resets): give the slot back and forget the doppler
history (`sound-system.md`); the rows' timers and active flags are kept.

### The shipped city objects

| City | File | Drop-offs (m) | Priority | Area | Points | Rows (type, volume, interval s, doppler) |
|---|---|---|---|---|---:|---|
| SF | `birdies` | 100–250 | 12 | 0 | 23 | `gulls1` 1, 0.91, 7–15; `gulls2` 1, 0.87, 5–15 |
| SF | `buoyseals` | 5–75 | 12 | 2 | 93 | `buoybell` 2, 0.95, 5–10; `sealions` 2, 0.92, 10–24 |
| SF | `horns_gulls` | 100–250 | 12 | 0 | 69 | `gulls1`, `gulls2` as birdies; `tughorn` 2, 0.95, 8–20, doppler; `tughorn2` 2, 0.92, 10–24, doppler |
| SF | `trolleycable` | 10–100 | 12 | 0 | 93 | `streetcable` 0 (loop), 0.82 |
| London | `londonriver` | 100–250 | 12 | 0 | 24 | as `horns_gulls` |
| London | `tubevoices` | 30–80 | 12 | 1 | 10 | `tubevoice01` 2, 0.98, 9–15; `tubevoice02` 2, 0.98, 5–20; `tubevoice03` 2, 0.98, 10–30 |

All rows are active, speed window 0…999,999. `trolleycable` follows SF's cable-car lines
(the hum of the cable under the street), `buoyseals` the bay, `tubevoices` sits at the
Underground stations (y ≈ −13; heard only in tunnels). The pan limit of 3D sources (±0.2) does not
apply to the gulls (type 1): they jump anywhere left to right.

Files in `aud/ambient/` that no list names and no code opens: `waves` (a shoreline of 99 points),
`maleped1`, `maleped2`, `femaleped1` (type-3 screams, 0–45 m, priority 8: an abandoned pedestrian
idea). They are never loaded. (`ferry`, `drawbridge` and `subwaycar` are opened by their owners,
below.) Also never read:
`aud/cardata/ambient/subwaycar.csv` (a copy of `subwaycar.csv` without the area column and
without the second row; no code builds that name). `city/<c>/audio_pathsets/` is never read
(`../formats/pathset.md`).

## Moving level objects

These own one ambient object each (two per two-leaf bridge), made from an ambient file at the
moment the object is made. Their movement is milestone 5 (`../ROADMAP.md`, "Moving level objects";
not specified yet); here is what their sound needs from it. Each frame, in the owner's update, the
owner sets the source position to its own position (the translation of its world matrix) and runs
the object update with s = 0; the 3D manager runs the slot update.

### Ferries

- One per ferry path of the ferry path set (`race/<c>/<c>_ferry.pathset` or its race variant,
  `../formats/pathset.md`, "Files"), **only in single-player games** (MM2 makes no ferries in
  network games). Position: the ferry. File `aud/ambient/ferry.csv`.
- Shipped `ferry.csv`: 0–225 m, priority 12, area 0, no points; `ferryengine` type 0, 0.98,
  doppler; `ferryhorn` type 2, 1.0, every 5–10 s, doppler; both active. So every ferry within
  225 m of the listener (and holding a slot) chugs continuously and sounds its horn every 5–10 s,
  whether it is moving or not.

### Drawbridges

- One ambient object per **leaf** (a bridge path with more than two points makes two leaves, each
  with its own object), positioned at the leaf. File `aud/ambient/drawbridge.csv`.
- Shipped: 0–150 m, priority 12, area 0; row 1 `bridgemove` type 0, 1.0, doppler; row 2
  `bridgebell` type 2, 0.95, interval 0–0, doppler; **both inactive** in the file.
- The leaf switches rows **1 and 2** (the first two rows, whatever their names) **on** when it
  starts to move: when a timed bridge starts opening after its closed wait, and when any bridge
  starts closing after its open wait. It switches them **off** when the leaf reaches its end
  position (fully open or fully closed). So while a leaf moves the motor loops and the bell, with
  its 0–0 interval, rings back to back (it fires on every slot update in which it is not playing,
  i.e. the frame after each ring ends); at the end the motor stops at once and the last ring
  finishes. A bridge opened by the proximity trigger opens in silence (the trigger does not switch
  the rows on) and sounds only while closing.
- Bridges that never move (path names starting `inac` or `open`, and every bridge in network
  games in London, which MM2 forces open) never sound. Shipped: `london_bridge.pathset` gives 7
  leaves of which 6 sound (`giz_bridge01_l` and two `giz_waterloo_l` paths, timed, two leaves
  each; `open:giz_bridge02_l` is silent); the race variants open or deactivate some of them
  (`crash5`: all inactive, `circuit0`: open); SF's only bridge is inactive.
- A file with fewer than two rows: MM2 switches rows that do not exist (undefined); our engine
  ignores missing rows.
- Resetting a bridge does not switch its rows off: a bridge reset while moving keeps its motor
  and bell going until its next movement ends.

### Underground trains

**Path-set trains.** London ships `race/london/london_train.pathset` (8 paths in the Underground:
65 of their 67 points lie in enclosed rooms); every game in London, single player or network, has these trains (3 cars of
`va_ug_l` each). One ambient object per train, file `aud/ambient/subwaycar.csv`, positioned at the
**middle car** (car 2 of 3). (These are not the network trains of `../ai/traffic-control.md`,
"Trains", which retail London does not have.)

Shipped `subwaycar.csv`: 0–150 m, priority 12, **area 1**, no points; row 1 `LondonTube` type 0,
0.98, doppler; row 2 `NOTHING` type 0 (no such file: silent); both active in the file.

Each frame the train's sound gets a "moving" value: 50 while the train is in any state other than
waiting at a stop, 0 while it waits. Then:

1. If the value is below **1.0** and the train's sound is in its "moving" setting (the setting it
   is made with): switch row 1 off and row 2 on; setting = "stopped".
2. If the value is 1.0 or more and the setting is "stopped": switch row 2 off and row 1 on;
   setting = "moving".
3. Run the object update (s = 0).

The first frame always switches to "stopped" (a train starts waiting). So `LondonTube` loops from
the moment a train pulls out until it has stopped at the next station, heard only while the
player's car is in a tunnel room (area 1) and within 150 m of the middle car.

**Network subway trains** (only if a city aimap has `[Subway]`, `../formats/aimap.md`; none in the
retail data): every car of such a train makes its own `subwaycar` object at its own position, and
the switch above uses the car's speed (m/s) as of its previous update instead of 50/0.

### Sailboats

No sound.

## Cable cars

Each cable car (`../ai/traffic-control.md`, "Trains (notes for later)", cable cars; the cable car
itself is milestone 5) is a **3D source** of its own kind:

- Position: the cable car; drop-offs **0–100 m**; priority **8**; doppler k = 0.017631503 (as
  above). It asks for a slot every frame in its own update, before it moves.
- Samples (3D cache, **with pitch control**, names in the `aud22` root): `CABLECARSTART`,
  `CABLECAR` (loop), `CABLECARSTOP`, `CABLECARGOBELL`; `STREETCABLE` is loaded too but never
  played (the street-cable hum comes from the `trolleycable` ambient object).
- State: a sound **state** (0 silent, 1 stopping, 2 starting, 3 running) and the previous frame's
  state, both 0 at first; and the **old speed**, set to the cable car's speed when it is made.
- Speed: the cable car's current speed along its line (m/s).

Slot update (each frame while it holds a slot):

1. Measure the distance; if d² ≥ max² give the slot back (the old speed is still updated in
   step 5).
2. Compute *A*, the pan (stereo only, else 0) and the doppler factor *f*. Volume *v* = 0.98 × *A*.
3. New state, from the speed now (*n*) and the old speed (*o*), first rule that applies:
   1. *o* < 0.001 and *n* < 0.001 → 0;
   2. *o* < 0.1 and *n* ≥ 0.1 → 2;
   3. *o* > 0.5 and *n* ≤ 0.5 → 1;
   4. the state is 2 and `CABLECARSTART` is not playing → 3;
   5. otherwise unchanged.
4. Play by state:
   - 0: stop `CABLECAR` if it plays.
   - 1: stop `CABLECAR` if it plays; `CABLECARSTOP` volume *v*, pan, pitch *f*; play it once if it
     is not playing.
   - 2: `CABLECARSTART` volume *v*, pan, pitch *f*, play once if not playing; `CABLECARGOBELL`
     volume *v* (pan and pitch untouched); if the previous state was not 2 and the bell is not
     playing, play it once.
   - 3: `CABLECAR` volume *v*, pan, pitch *f*; play it looped if it is not playing.
   Then previous state = state.
5. Old speed = the speed now.

So: pulling away (crossing 0.1 m/s upwards) rings the bell once and plays the start clip; when the
start clip ends the running loop takes over. Slowing through 0.5 m/s stops the loop and plays the
stop clip, which **repeats each time it ends** for as long as the car stays between the two
thresholds without reaching 0.001 (or without crossing 0.1 upwards again). The old speed only
advances while the cable car holds a slot (and in the frame it gives one back), so a cable car
coming back into range compares against the speed it had when it was last heard.

Losing the slot stops `CABLECAR` and `CABLECARSTART`; the bell and stop clip finish. The cable car
does not use the echo.

## Rain and thunder

Made at game set-up when the session's weather is rain (3; `../formats/night.md`, section 6).
Other weathers have no rain or thunder sound at all. These are plain 2D samples (no slot), one
instance each:

| Sample | Pitch control | Set up with |
|---|---|---|
| `Rainexterior` (`aud/aud22/rainexterior.22k.wav`) | no | volume 0.82 |
| `Raininterior` | no | volume 0.85 |
| thunder 1: `Thunder` (only if the time of day is night, 3) | yes | volume 1.0, pan −0.2 |
| thunder 2: `Thunder` again (a second sample) | yes | volume 1.0, pan +0.2, pitch 0.8 |

State: the **current** rain loop (exterior at first), an **inside** flag (MM2 leaves it
undefined; start with false), a **muted** flag (false), the thunder timer *t* = 0 and a thunder
phase (false).

**View changes.** Whenever the player's view is set (`../engine/cameras.md`, "Switching views";
MM2 does this only in its view-selection step, which is locked during the pre-race and post-race
cameras), inside = true for the **bumper** and **dash** views, false for the near and far chase
views, the orbit view and any other view. If inside changed: start the new loop (interior if
inside, else exterior) looped, then stop the other one if it plays; it becomes the current loop.

**Every frame** (in the player's update, not while paused):

1. If the 3D manager is disabled (results screen): stop the current loop if it plays; stop here.
2. If the echo state is on and not muted: exterior volume 0.65, interior volume 0 (silent),
   both thunder volumes 0.85, both thunder pans 0; muted = true.
   If the echo state is off and muted: exterior 0.76, interior 0.83, thunder volumes 1.0 and
   1.0, thunder pans −20 and +20 (clamped to full left and full right, `sound-system.md`, "Pan");
   muted = false.
3. If the current loop is not playing, play it looped (this also restarts it after a pause).
4. If the time of day is not night: *t* = 0; stop here.
5. If the phase is false and *t* ≥ 15.0: play thunder 1 once if it is not playing; phase = true;
   *t* = 0.
6. If the phase is true and *t* > 1.0: play thunder 2 once if it is not playing; phase = false;
   *t* = 0.
7. *t* = *t* + *dt*.

So at night a thunder clap (slightly left) comes 15 s into the game and a lower second roll
(pitch 0.8, slightly right) a second later, then again every ~16 s (plus a frame or two). The
timer only runs while the update runs (not paused, not at the results). In tunnels the rain is
quieter outside and silent from the inside views and the thunder is centred and a little quieter;
after the first tunnel the levels never return to the set-up values (0.76 / 0.83 instead of
0.82 / 0.85, and the thunder pans jump to the extremes). MM2 also keeps a flag that rises 13 s into
each cycle that nothing reads (perhaps an unconnected lightning flash).

## The ambient option

The option bit 0x800 ("ambient", exclusive with music in the menu, `sound-system.md`):

- decides at game set-up whether the city ambient objects exist (above);
- makes the music system play its city ambience segment instead of the soundtrack, which also
  follows the echo state (music spec; it does not interact with the sound effects here);
- has no effect on ferries, drawbridges, trains, cable cars, rain or thunder, which exist whatever
  the option.

With music chosen (bit 0x800 off), a game has no gulls, buoys, tug horns, street cable or station
announcements.

## Not used by MM2

- A crowd "walla" loop (`SFwalla` in `sf`, `amb5` elsewhere, fading in below a car speed of 35
  when not in a tunnel and the ambient option is off) exists in the program but is never created.
- Type-3 rows, the speed window (every owner passes 0), and firing a row on demand.
- `Amb3D` samples named by no file: `TubeSquel`, `foghorn`, `carousel`, `Chinatown1`, `Chinatown2`,
  `Firecracker`, `theL`, the `Vscream*` and higher-numbered `MaleScream`/`FemaleScream` files, and
  `Amb3D` copies of the cable-car samples (the cable car uses the root ones).

## Events

| Sound | Trigger | Produced by |
|---|---|---|
| city objects created | game set-up, ambient option on, city `london`/`sf` | this page; option bits `sound-system.md` |
| city objects positioned, slots | listener position, echo state | `../engine/cameras.md` (listener); `sound-system.md`, "Echo" (room flag 0x02, `../formats/psdl.md`, "Room flags") |
| ferry engine and horn | ferry exists, its position each frame | milestone 5 (ferries, not specified) |
| bridge motor and bell | a leaf starts opening (timed) or closing; reaches its end | milestone 5 (drawbridges, not specified) |
| `LondonTube` | path-set train leaves / stops at a stop; position of its middle car | milestone 5 (path-set trains, not specified) |
| network subway sound | car speed | `../ai/traffic-control.md`, "Trains" (later) |
| cable car clips | cable car speed and position | `../ai/traffic-control.md`, "Trains"; milestone 5 (cable cars) |
| rain loop choice | view selection | `../engine/cameras.md`, "Switching views" |
| rain and thunder exist | weather rain, time of day night | `../formats/night.md` |
| everything silent | results screen (3D manager disabled) | `sound-system.md`, "Pause, results and reset" |

## For our engine

- Keep the one-source-per-file rule and the "move only without a slot" nearest-point rule: with
  only 4 slots they decide what is heard.
- Mods: a city that should have ambience needs MM2's exact names (`london`, `sf`); we may add
  `<MapName>ambientcontainer.csv` as an extension, but MM2 itself never reads it.
- Clamp nothing beyond `sound-system.md`; skip rows and points that would crash MM2 (missing
  fields, blank lines, objects without points) rather than guess.

## Checked against

**The program** (static reading): the player set-up (option bit test, the two exact name
comparisons, the 4-slot manager made just before), the list reader and the object-file reader
(CR LF cut, comma `strtok` splitting, field order and conversions, the exact `VECTORPOINTS`
comparison, the skipped header lines, the points reader), the object update and slot update
(area gates, nearest-point choice and its distance sum order, echo values, the per-type table, the
timer and the fire rules with the 0.75…1 and −1…1 draws), the owners (ferry and bridge leaf
set the position and pass speed 0 every frame; leaves switch the first two rows at the start and
end of each movement; the train's 50/0 value and the 1.0 threshold; the network subway car's speed), the cable
car (drop-offs 0–100, priority 8, thresholds 0.001 / 0.1 / 0.5, volume factor 0.98, the sample
of each state), rain and thunder (creation tests, volumes 0.82 / 0.85 / 1.0, pans ±0.2, pitch 0.8,
tunnel values 0.65 / 0 / 0.85, return values 0.76 / 0.83 / 1.0 / ±20, timings 15.0 / 1.0 / 13.0,
the view test), and the doppler constant 1/56.7166633605957 for ambient objects and cable cars.

**The data** (`tools/audio/world_sounds.py`, output `work/check/audio/world_sounds.txt`, maps
`ambient_london.png`, `ambient_sf.png` with enclosed rooms in red and max-distance circles):

- 15 files in `aud/ambient/`: 2 lists (London 2 names, SF 4, all resolve), 13 object files. All
  parse with these rules without a problem (4 fields on line 2, 9 per sample row, no blank lines);
  point counts birdies 23, buoyseals 93, horns_gulls 69, trolleycable 93, londonriver 24,
  tubevoices 10 (every listed file has points), unread waves 99. Never opened: waves, maleped1,
  maleped2, femaleped1. `aud/cardata/ambient/subwaycar.csv` would crash MM2's reader (line 2 has
  only 3 fields after the comma collapsing) but nothing reads it.
- Rooms: all 10 `tubevoices` points lie in enclosed rooms (area 1 fits), none of the other city
  objects' 302 points does; `london_train.pathset` 65 of 67 points in enclosed rooms.
- Samples: 30 references (22 ambient rows, 5 cable car, 3 rain/thunder), 24 distinct names; every
  file exists except `Amb3D\NOTHING` (the silent second train row). 31 `Amb3D` files are named by
  no ambient file.
- Bridges: `london_bridge.pathset` 7 leaves, 6 that sound; `_race0` and `_multi` the same (but
  network games force London's bridges open); `_blitz10` 4 of 5 (damaged file, `../formats/pathset.md`);
  `_crash3` 4 of 7; `_crash5`, `_circuit0` and `sf_bridge` none. Ferries: London 6 per path set,
  SF 1.
- Test runs: thunder at 15.00, 16.00, 31.00, 32.00, 47.00, 48.00 s (30 frames per second); a
  cable car accelerating at 1.5 m/s² from rest starts (bell + start clip) at 0.1 m/s, loops when
  the 1.74 s start clip ends, and braking at 3 m/s² enters "stopping" at 0.5 m/s; one-shot timers
  with the clock pinned show the shared draw (e.g. `gulls1` first shot volume 0.713 = 0.91 ×
  0.7835, pan −0.732 = 2 × 0.134 − 1, next shot 8.1 s later = 7 + 8 × 0.134).
- `near` examples: a listener at (700, 0, −150) in London picks `londonriver` point 6
  (695.65, −4.0, −208.45), 58.8 m away (inside min 100: *A* = 1); at (−800, 40, 160) in SF
  `trolleycable` point 91, 22.5 m away, *A* = 0.9591, the other SF objects out of range.
