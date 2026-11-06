# Tyre and impact sounds: surfaces, skids, wobble, suspension, impacts

What a car's wheels and body sound like: the **rolling** loop of the surface under it, the
**skid** loops when its tyres slide, the **tyre wobble** thump of a badly damaged car, the
**suspension** thump of a hard bump or landing, and the **impact** one-shots of every collision
(walls, props, other cars, traffic). The generic sound layer (samples, mixer, 3D slots,
attenuation, pan, echo, pause) is `sound-system.md`; words in **bold** there (sample, 2D, 3D
source, slot, attenuation A, echo state) are used here with the same meaning. Which car gets car
audio at all, its 3D priority and drop-offs, and the engine, horn and siren of the same car are
`car-sounds.md`; traffic engines, horns and voices are `traffic-sounds.md`.

Units: metres, seconds, m/s. Arithmetic is float32, one rounding per operation, left to right as
written (`../physics/vehicle.md`, "Precision"), except where a loader below says a value enters at
double precision. "F" is the frame time in seconds (the same F as the game's frame clock).

## The short version

- Every car with car audio (the player's car, opponents, police, network cars) uses the **same**
  surface files from `aud/cardata/player/`: `default_surfacedry.csv`, or `default_surfacewet.csv`
  when it rains. A surface file is a list of blocks; the ground material's `sound` number picks the
  block; a block has one rolling loop and a few skid loops.
- The block follows the **front-left** wheel's material, with a hysteresis: it only changes when
  neither front wheel is on the current block's material. While the echo state is on (the player is
  in a tunnel) every car uses the file's **tunnel block** instead.
- The rolling loop plays only with at least 2 wheels on the ground, above 2.0 m/s, and while none
  of the block's skid loops is playing; its volume and pitch rise linearly with the car's speed up
  to the block's "max speed".
- The skid loops are chosen by the **largest `slide`** of the four wheels; every skid sample whose
  slide window contains it loops, at one volume for all.
- Tyre wobble: once the car's damage is past 5 % of the way from `MedDamage` to `MaxDamage`, one
  thump per back-left-wheel circumference travelled.
- Suspension thump: when the mean compression rate of the four wheels reaches 2 m/s (shipped)
  with at least 2 wheels down.
- Impacts: a car plays an impact sound for a **new or stronger** impact (`../physics/damage.md`,
  "Per impact report" step 5) with loudness F_imp = (|J.z| + |J.y|) + |J.x|; a traffic car with a
  slot plays one for **every** impact applied to it. The sample comes from the impact table
  `default_impacts.csv` (player or opponent folder), entry = the **other side's impact sound id**:
  0 (`WALL`) for walls, the city, landmarks, other cars and traffic cars; a knocked-loose prop's
  `ColliderId`. There is no scraping loop, no splash sound and no sound of props hitting each
  other.

## Which files are read

At car sound set-up (`car-sounds.md`) each car reads, by name (paths inside the archives,
`../formats/ar.md`; the extension `.csv` is added by the reader):

| File | Read by | Notes |
|---|---|---|
| `aud/cardata/player/default_surfacewet` | every car, when the session's weather is 3 (rain, `../formats/night.md`) | |
| `aud/cardata/player/default_surfacedry` | every car otherwise (also when there is no session state, e.g. outside a game) | |
| `aud/cardata/player/suspensionaudio` | every car | |
| `aud/cardata/player/tirewobble` | every car | |
| `aud/cardata/player/default_impacts` | the player's car | |
| `aud/cardata/opponent/default_impacts` | every other car (opponents, police, network cars) | but see "Which impact table a car uses" |
| `aud/cardata/opponent/default_impacts` | ambient traffic (one shared copy) | `traffic-sounds.md` set-up |

The file names never contain the car's name: a car-specific surface or impact file is never
looked for. Shipped files the game never opens: `opponent/default_surfacedry.csv`,
`opponent/default_surfacewet.csv` (its blocks have an extra tenth column "for tunnels"),
`player|opponent/default_surfaceice.csv` (a different, 12-column layout that the surface reader
would misread), and the `Copy of …` files.

## Reading rules shared by these files

- Lines are read one at a time, at most 999 bytes per read; a read ends after a line feed (kept).
  A read that gets no byte at all is **end of file**. (A byte 0xFF also ends a read, like end of
  file; no shipped file has one.)
- "Cut the line" = end the line at its first CR or LF (after skipping any CR/LF at its very
  start). Lines that are only cut are ignored (headers).
- Columns are separated by commas; **empty fields are skipped** (C `strtok` behaviour: `a,,b` is
  two columns `a`, `b`), and a field keeps any trailing CR/LF of an uncut line.
- Numbers: a decimal number as `strtod` reads it (longest valid prefix after white space), stored
  as float32; integers as `atoi`. A **missing** column (fewer fields than expected) makes MM2
  crash; read it as 0.
- `NOSOUND` (exact, case-sensitive) in a sample column means "no sample".

## Surface files

### Format

| Line | Contents |
|---|---|
| 1 | header, ignored (not checked) |
| 2 | cut; first column = **tunnel block index** (integer); other columns ignored |
| then, repeated until end of file: | **one block** (below) |

A block:

| Line | Contents |
|---|---|
| b1 | header ("surface wave,max speed,…"), cut, ignored. End of file here ends the file normally. |
| b2 | cut; 9 columns: rolling wave name (or `NOSOUND`), max speed, min surface volume, max surface volume, min surface pitch, max surface pitch, min skid volume, max skid volume, number of skid rows *n* (integer). Further columns are ignored (the opponent wet file's "for tunnels" column). |
| b3 | header ("skid wave,min slippage,max slippage"), cut, ignored |
| b4 … b(3+n) | one skid row each, **not cut**: skid wave name (or `NOSOUND`), min slide, max slide; further columns ignored |

Block *k* (0-based, in file order) is the block for material sound number *k*. There is no end
marker: `ENDOFDATA` or any other line after the last block is read as the header of one more block.

Per block the reader keeps, as float32: `maxSpeed`, `minVol`, `maxVol`, `minPitch`, `maxPitch`,
`minSkid`, `maxSkid`, and computes three values **from the text values just read**:

    k          = 1 / maxSpeed                     (0 if maxSpeed = 0)
    volSlope   = round(maxVol_read − minVol) · k
    pitchSlope = round(maxPitch_read − minPitch) · k
    skidRange  = round(maxSkid_read − minSkid)

where `x_read` is the number exactly as parsed (double precision, before it is rounded to
float32) and round(…) is one rounding of the exact difference to float32; `minVol`, `minPitch`,
`minSkid` and `maxSpeed` are the stored float32 values; the products are float32. (This matters:
for the shipped files the all-float32 difference differs in the last bit for several blocks, e.g.
the dry file's block 1 skid range is 0.349999994, not 0.350000024.)

Loader quirks (keep them):
1. The first two lines are not checked for end of file; a file with fewer than two lines gives an
   undefined tunnel index (use 0).
2. End of file inside a block (after its b1 line) stops reading; that block **stays in the list**
   (counted) with whatever it had read so far (nothing if it ended at b2: no rolling sample, all
   values 0, no skids).
3. *n* ≤ 0 reads no skid rows.
4. A skid row named `NOSOUND`: MM2 leaves that skid's sample undefined (no shipped file has one).
   Treat it as a skid row without a sample: silent, and never "playing".
5. A rolling wave or skid wave name is used as it is: the sample is `surfaces\<name>`
   (`sound-system.md`, "Finding a sound file"). A missing file is a silent sample.
6. If the file cannot be opened the car has no blocks and makes no surface sounds (MM2 itself
   would crash for the player's car).

### Shipped data

`default_surfacedry.csv`: tunnel block 0, 5 blocks. `default_surfacewet.csv`: tunnel block 5,
6 blocks.

| Block (material `sound`) | Dry: rolling, max speed, vol, pitch / skid vol / skids (slide window) | Wet |
|---|---|---|
| 0 (default, asphalt, every material with `sound` 0) | `NOSOUND`, 125, 0–0, 0–0 / 0.5–0.88 / `tireskid1_ps1` 0.55–0.65, `tireskid2_ps1` 0.65–0.75, `tireskid3_ps1` 0.75–1 | `NOSOUND` / 0.5–0.9 / `wetskid` 0.6–0.7, `wetskid2` 0.7–1 |
| 1 (`water`) | `NOSOUND` / 0.5–0.85 / `wetskid` 0.75–0.85, `wetskid2` 0.85–1 | same |
| 2 (`grass`) | `surfacegrass`, 25, 0.35–0.75, 0.85–1.25 / 0.5–0.72 / `surfacegrassskid` 0.25–1 | same rolling / 0.5–0.87 / `grassskidwet` 0.25–1 |
| 3 (no shipped material) | `caroncobble`, 125, 0.75–0.83, 0.85–1.25 / 0.75–0.95 / `skidcobble` 0.25–1 | skid vol 0.75–0.9 |
| 4 (no shipped material) | `NOSOUND`, 125, 0.3–0.9, 0.85–1.25 / 0.75–0.95 / `skidflagstone` 0.25–1 (**no such file**: the shipped WAV is `skidflagstone1`, so this skid is silent) | skid vol 0.75–0.9 |
| 5 (tunnel block, wet only) | — | `NOSOUND` / 0.55–0.88 / `tireskid1_ps1` 0.55–1 |

The shipped materials use only `sound` 0, 1, 2 (`../physics/collision.md`, "Materials"; every
bound material has 0 or `none`), so blocks 3 and 4 are reachable only by mods. When raining, a
tunnel sounds like dry asphalt (block 5).

## Surface sounds, every frame

### Inputs

Per car, from the latest physics sample: the four wheels front-left FL, front-right FR,
back-left BL, back-right BR (`../physics/vehicle-wheels.md`), each with `grounded` (as left by its
last tyre update), `slide` (0…1) and its ground **sound number** = the `sound` field of the
material its last ground query found (`../physics/collision.md`, "Materials": `none` = 0; a wheel
that has never had a material counts as 0; a negative value, which MM2 would use as an index
outside its block list, counts as 0 too); the car's `speed`
(`../physics/vehicle.md` step 2); the echo state (`sound-system.md`, "Echo").

Per car the surface state is: **current block** `cur` (0 at set-up), **previous block** `prev`
(−1 at set-up), a **skidding** flag (false), and the airborne flag (below).

The player's car is a **2D source**: the volumes below are used as they are, with the car's fixed
2D pan (`car-sounds.md`). Every other car is a **3D source**: in its slot update every volume
below is multiplied by its attenuation A (`sound-system.md`, "Attenuation, pan and doppler"; the
plain A, without the car's near boost) and its pan is set to the 3D pan (stereo only); doppler
does not apply to these samples. A car without a slot runs none of this.

### Step 1: rolling loop

1. Count the wheels with `grounded` true: *g*.
2. If *g* < 2: stop block `cur`'s rolling loop. For the player's car only: if *g* = 0 run the
   airborne test (below), else clear the airborne flag. Done.
3. (Player's car) clear the airborne flag.
4. If not `speed` > 2.0: stop block `cur`'s rolling loop; done.
5. If any skid sample of block `cur` is playing: stop block `cur`'s rolling loop; done.
6. **Choose the block**: if the echo state is on, `cur` = the tunnel block index (no range check
   in MM2; treat an index ≥ the block count as 0), and nothing is stopped. Otherwise, if neither
   FL's nor FR's sound number equals `cur`: stop block `cur`'s rolling loop, then `cur` = FL's
   sound number, and if `cur` ≥ the number of blocks, `cur` = 0. (MM2's code for the player's car
   stops block `prev` instead when `prev` differs from FL's number; since step 2 sets `prev` =
   `cur` every frame, that is the same block. A sound number ≥ the block count under both front
   wheels therefore stops block 0's loops and starts them again from the beginning every frame.)
7. If block `cur` has no rolling sample (`NOSOUND`): done (nothing plays).
8. Volume and pitch:

       if speed < maxSpeed:  pitch = speed·pitchSlope + minPitch ;  vol = speed·volSlope + minVol
       else:                 pitch = maxPitch ;                     vol = maxVol

   Set the rolling sample's pitch to `pitch`, its volume to `vol` (3D: `vol·A`, and pan), and play
   it looped if it is not playing. `prev` = `cur`.

### Step 2: skid loops

Only if the car has at least one block.

1. s = 0; for FL, FR, BL, BR in that order: if the wheel's `slide` > s, s = that `slide`.
2. **Choose the block** as in step 1.6, except that on a change it is block `cur`'s **skid loops**
   that are stopped (all of them), before `cur` changes. (No grounded or speed test here: a
   wheel off the ground has `slide` 0.)
3. If s > 0:

       vol = s·skidRange + minSkid

   For each skid row of block `cur`, in file order: if s < min or s > max, stop its sample if it is
   playing; otherwise set its volume to `vol` (3D: `vol·A`, and pan) and play it looped if it is
   not playing. Set skidding; `prev` = `cur`; done.
4. Otherwise, if skidding: stop every skid loop of block `cur`, clear skidding. `prev` = `cur`.

So several skid loops can run together where windows overlap (the shipped windows share their
end points: at a slide of exactly 0.65 on dry asphalt both `tireskid1_ps1` and `tireskid2_ps1`
play). The skid volume does not subtract the window minimum. Skid samples never change pitch.
There is no crossfade anywhere: a change of block stops the old loop and starts the new one from
its beginning; a stopped rolling or skid loop restarts from its beginning when played again.

### Echo copies

When the car's echo copies are switched (`car-sounds.md`: at the start of the car's update, when
the echo state differs from the car's), the rolling sample and every skid sample of **every**
block get echo with delay T = the manager's echo delay (0.5 s), gain G = 0.96, and no initial echo
pitch of their own (`sound-system.md`, "Echo"). Suspension, wobble and impact samples have no echo.

**Ticking.** While the car's echo is on, at the start of each of its slot updates (after the echo
switch, before the suspension thump and step 1; `car-sounds.md` ticks the horn and gear samples
at the same point) the car ticks the rolling sample and every skid sample of block `cur` only.
Echo repeats pending on the samples of other blocks do not advance until that block is `cur`
again (or the echo is switched off, which drops them). Suspension, wobble and impact samples are
never ticked (they have no echo).

### What stops these loops otherwise

- Losing the slot (3D cars; `sound-system.md`, "Getting and losing a slot"): MM2 switches the
  echo of every block's samples off (if it was on) and stops only block `cur`'s rolling loop and
  block `cur`'s skid loops; the suspension, wobble and impact one-shots are let go without being
  stopped.
- Pause stops everything (`sound-system.md`); the loops come back by step 1.8 / 2.3.

### Airborne flag (player's car only)

The flag that `car-sounds.md` reports as "airborne" (used by the music's big-air motif) is kept
here, only for the player's car (every other car's test is switched off at set-up):

- cleared in step 1.2/1.3 whenever at least one wheel is grounded;
- when no wheel is grounded (step 1.2), unless already set: probe straight down from the car's
  body position P (`../physics/collision.md`, "Probe", mask "ground", nothing ignored, a fresh
  probe state, start room = the car's room) along 10 consecutive segments of 3.0 m:
  from (P.y − 3.0·(i + 1)) to (P.y − 3.0·(i + 2)) for i = 0…9 (x, z of P), stopping at the first
  hit; any hit sets the flag. So the flag is set when the ground is between 3 m and 33 m below the
  car, and stays set until a wheel touches down.

## Suspension thump

### File `suspensionaudio.csv`

| Line | Contents |
|---|---|
| 1 | header, ignored. End of file: no thump (the sample stays unset). |
| 2 | cut; 6 columns: sample name, min velocity, max velocity (read, unused), min volume, max volume, volume divisor |

Sample `suspension\<name>` (no `NOSOUND` test), pitch control (never used). Stored: `minVel`,
`minVol`, `maxVol` (float32) and `inv` = 1/divisor (0 if the divisor is 0; computed from the
value as read, rounded once). Shipped: `Suspension3`, 2, 3, 0.85, 0.9, 3 (so every thump is at
0.85–0.9).

### Rule (every frame, before step 1, only while the car holds its sample)

1. mean = (((FL.rate + FR.rate) + BL.rate) + BR.rate) · 0.25, with `rate` each wheel's
   compression rate (`../physics/vehicle-wheels.md`, "Suspension update"; positive while the
   spring compresses).
2. If mean < |minVel| (or is NaN): nothing.
3. If fewer than 2 wheels are `grounded`: nothing.
4. If the suspension sample is playing: nothing.
5. v = mean · inv; if v < minVol, v = minVol; else if v > maxVol, v = maxVol.
6. Set its volume to v (3D: v·A, and pan) and play it once.

Only compression counts (a fast rebound gives a negative mean). This is also the landing sound
after a jump; there is no separate landing, bottoming-out or airborne sample.

## Tyre wobble

### File `tirewobble.csv`

| Line | Contents |
|---|---|
| 1 | header, ignored. End of file: no wobble. |
| 2 | cut; 6 columns: sample name, min volume, max volume, min pitch, max pitch, pitch divisor |

The sample is the name **as it is, without a folder** (shipped `tirewobble1` →
`aud/aud22/tirewobble1.22k.wav`), pitch control. Stored float32: `minWV`, `maxWV`, `minWP`,
`maxWP`, `inv` = 1/divisor (0 if 0, from the value as read). Shipped: `tirewobble1`, 0.97, 1,
0.75, 1.5, 15.

State per car: distance `acc` (0 at set-up), and `circ` = BL's wheel radius · 6.28318
(the float32 constant 6.28318, 0x40C90FD0; not 2π), set each time the car gets its slot.

### Rule (every frame, after step 2, only while the car holds its sample)

1. With the car's damage values (`../physics/damage.md`, "State" and "Tune file"):
   frac = (damage − MedDamage) / (MaxDamage − MedDamage).
2. If frac < 0 (or NaN): done. If frac > 1: frac = 1. Else if frac ≤ 0.05: done.
3. d = speed·F + acc. If d < circ: acc = d; done.
4. vol = frac clamped to [minWV, maxWV] (if frac < minWV → minWV, else if > maxWV → maxWV);
   pitch = (speed · inv) clamped to [minWP, maxWP] the same way.
5. If the wobble sample is playing: done, **without** storing d (the distance waits until the
   last thump has finished).
6. Set its volume to vol (3D: vol·A, and pan), its pitch to `pitch` (the player's car only; a 3D
   car's wobble keeps the pitch the sample already had), play it once; acc = 0.

It uses the car's `speed`, not the wheels' spin, and the back-left wheel's size whatever wheel
is damaged (damage is not per wheel). Shipped: about one thump per 2 m, volume 0.97–1, pitch
0.75 below 11.25 m/s rising to 1.5 at 22.5 m/s.

## Impact sounds

### File `default_impacts.csv`

A list of **entries**; entry *i* is the sound of impact sound id *i*. Per entry, line by line:

| Line | Contents |
|---|---|
| e1 | separator (`***`), cut, not checked |
| e2 | header ("Banger name,Num samples,ID"), ignored |
| e3 | cut; columns: entry name (only compared with `ENDOFDATA`), number of sample rows *n* (integer). The third column (`ID`) is **never read**: the id is the entry's position. |
| e4 | (only if *n* > 0) header ("sample name,min volume,…"), cut, ignored |
| e5 … e(4+n) | cut; columns: sample name, min volume, max volume, min force, max force, frequency |

The file ends with an entry whose e3 name is exactly `ENDOFDATA` (case-sensitive; its other
columns are not read). Quirks:
1. **If end of file comes anywhere before the `ENDOFDATA` entry, every entry is thrown away**: the
   table is empty and this car (or the traffic) plays no impact sound at all.
2. An entry with *n* ≤ 0 reads no lines after e3 (not even e4), so the next entry starts one line
   early.

Per sample row: sample `impacts\<name>`, float32 `minV`, `maxV`, `minF`, `maxF`, `freq`, and

    slope = (maxV − minV) · (1 / (maxF − minF))      (0 if maxF − minF = 0; all float32)

**Frequency**: if `freq` = 1.0 the sample is made **without** pitch control; otherwise **with**
pitch control, and its pitch is set to `freq` each time the car or traffic car gets its slot
(shipped: 1.49492 for `METALBARREL`, `BBQ`; 1.5 for `COKEMACHINE`, `NEWSSTAND`; 0.5 for
`SWIMWITHFISHES`). The same WAV used with and without pitch control is two separate samples.

Shipped (both read files: 24 entries, ids 0–23, all with force window 0–999999 except `WALL`):

| Id | Name | Samples (min–max volume) | Id | Name | Samples |
|---:|---|---|---:|---|---|
| 0 | `WALL` | see below | 12 | `BBQ` | `trashimpact` 0.94–1, freq 1.49492 |
| 1 | `LIGHT` | `trafficlightimpact` 0.95–0.98 | 13 | `CARDBOARDBOX` | `carimpactmed1` 0.94–1 |
| 2 | `SIGN` | `signimpact2` 0.8–0.855 | 14 | `AWNING` | `carimpactsoft3` 0.95–1 |
| 3 | `MAILBOX` | `mailimpact1` 0.94–0.97 | 15 | `ORANGECONE` | `orangecone` 0.95–1 |
| 4 | `PARKINGMETER` | `parkingmeter6` 0.94–0.97 | 16 | `HOTDOGCART` | `parkbenchimpact`, `carimpacthard3` 0.85–0.9 |
| 5 | `TRASHCAN` | `trashimpact` 0.92–0.95 | 17 | `TCANCONCRETE` | `carimpacthard3` 0.95–1 |
| 6 | `BENCH` | `parkbenchimpact` 0.85–0.9 | 18 | `TCANPLASTIC` | `caroncobble` 0.95–1 (**no such file** in `impacts\`: silent) |
| 7 | `BUSH` | `bushimpact` 1–1 | 19 | `PHONESTAND` | `phoneimpact` 0.94–1 |
| 8 | `TREE` | `bushimpact` 0.9–1, `carimpactmed1` 0.95–1 | 20 | `COKEMACHINE` | `trafficlightimpact` 0.94–0.98, freq 1.5 |
| 9 | `BILLBOARD` | `billboardimpact` 0.92–0.94 | 21 | `NEWSSTAND` | `mailimpact1` 0.92–0.97, freq 1.5 |
| 10 | `SAWHORSE` | `roadworksawhorse` 0.94–0.96 | 22 | `SWIMWITHFISHES` | `fountainsplash` 0.98–1, freq 0.5 |
| 11 | `METALBARREL` | `trashimpact` 0.94–1, freq 1.49492 | 23 | `GLASS` | `glassbreak` 0.97–0.99 |

`WALL` differs between the two files:

| File | `Carimpactsoft3` 0.91–0.93 | `Carimpactmed1` 0.92–0.95 | `Carimpacthuge1` 0.94–1 |
|---|---|---|---|
| `player/default_impacts.csv` | force 1000–8000 | 8000–20000 | 20000–999999 |
| `opponent/default_impacts.csv` | force 2–500 | 500–1500 | 1500–999999 |

### Which impact table a car uses

The impact table is cached by its **name** `default_impacts` (not by folder) in one cache shared
by the cars and the traffic, which is emptied when a game ends:

1. The traffic loads `opponent/default_impacts` once per game, when the first traffic car is set
   up, and records the name in the cache with the traffic's table number, which is always 0.
2. A car being set up first looks the name up in the cache. If it is not there, the car loads its
   own copy of its file (player or opponent folder) and appends it to the **car** table list
   (without recording it in the cache). If it **is** there (some traffic car was set up earlier in
   this game) the car takes the recorded number, 0, as its car-table number: it uses the car table
   list's entry 0, i.e. the copy loaded by the first car set up in the game, which is the
   **player's** (`player/default_impacts.csv`). If the file cannot be opened (or the cache holds
   a failed load) the car also uses car-table entry 0 (none: no impact sounds).
3. The game sets up the player's car first, then the opponents, then the traffic, then the
   police. So **the police cars play the player's impact table** whenever the game has traffic;
   opponents play the opponent table; network cars set up during the game (after the traffic)
   also get the player's. Keep this.

Each loaded table has one copy per 3D slot, so cars in different slots never share a playing
impact sample (`sound-system.md`, "Samples of 3D sources").

### The other side's impact sound id

Every collision side has an impact sound id (a property of its collision body, not of the
material):

| The other side | Id |
|---|---|
| the city (and its walls, curbs, …) | 0 |
| a landmark, a spanning object, any fixed object tested without a body | 0 |
| a prop, a piece of a broken prop or a car part thrown off: anything with a loose body (`../physics/loose-bodies.md`, 2) | its prop data's `ColliderId` (`../physics/collision-objects.md`; 0 when the key is missing) |
| the player's car, an opponent, a police car | 0 |
| a physical traffic car (`../physics/car-collision.md`, 4) | 0 |
| a network car | 1000 + its player number |

`AudioId` in the prop data is read but no sound uses it (0 in every shipped file). Id rules when
playing: an id **above 1000** is treated as 0; id **−1** plays nothing; any other id that is
negative or not below the table's entry count uses entry 0. So everything a car hits sounds like
`WALL` except loose props. Shipped `ColliderId` use: 0 (856 prop files), 1 (44), 2 (26), 3 (16),
4, 5 (2), 6 (5), 8 (9), 10, 11 (2), 13 (2), 14 (10), 15 (2), 16, 19 (2), 21 (9), 23 (7 doors and
glass panes); entries 7, 9, 12, 17, 18, 20, 22 are used by no shipped prop.

### Playing an impact

Given a table, the force F_imp and the id:

1. If id > 1000: id = 0. If id = −1: nothing. Pick the entry (rules above); remember the id as
   the source's **last impact id**. Empty table: nothing.
2. For each sample of the entry, in file order, with minF ≤ F_imp ≤ maxF (both ends included):
   - if the sample has no copy for the source's slot: skip;
   - if it is playing: skip (it is not restarted);
   - v = slope·F_imp + minV (the minimum force is **not** subtracted);
   - set its volume to v (2D) or v·A′ (3D; below), its pan to the source's pan (3D, stereo), and
     play it once; remember v for the sample.

Every matching sample plays: at a force on a shared boundary (8000 in the player's `WALL`) two
samples start together. Volumes above 1 are clamped by the mixer.

**2D or 3D.** The player's car plays its impacts as a 2D source: volume v, its fixed pan. A 3D car
uses A′ = the car's 3D volume factor from its latest slot update (`car-sounds.md`: its attenuation
A times its near boost), a traffic car its plain attenuation A (`traffic-sounds.md`), and their
current pan. In every slot update of such a source, each sample of its **last impact id's** entry
that is still playing gets volume (remembered v)·A′ (or ·A) and the new pan again.

### When a car plays one

In the car's impact report (`../physics/damage.md`, "Per impact report"), only when the car's
damage is **enabled** (so the player's car is silent in no-damage mode), and only for an impact
that reaches the **count** step 5 (a new impact taking a free slot, or one more than 1.25 times
stronger than the slot's peak), with its strength s > 0.001 — before and regardless of the
`ImpactThreshold` and 10 mph tests of that step:

    F_imp = (|J.z| + |J.y|) + |J.x|

with J the impulse applied to this car for this impact (step 3 there, already weighted by 1/N),
id = the other side's impact sound id at that moment, table = the car's ("Which impact table a car
uses"). Both cars of a car-to-car impact report it, so each plays its own (`WALL`) sound; a
car without its slot (3D) is silent.

### When a traffic car plays one

Every impact applied to a physical traffic car (`../physics/car-collision.md`, 3.4 "Applying",
"a traffic body's report") plays, while the traffic car holds a 3D slot: F_imp =
(|J.z| + |J.y|) + |J.x| with J the impulse applied to the traffic car, id = the other side's
(0 for any car), the traffic table. The same report then gives the traffic horn and driver voice
reactions (`traffic-sounds.md`; `../ai/traffic-reactions.md`, "Horns and voices").

### Scraping, splashes and what makes no sound

- **No scrape loop.** A lasting contact (sliding along a wall) re-sounds only through the rules
  above: for a car, a new impact sound each time the damage slot of that side has expired (0.2 s
  after the last new-or-stronger impact, `../physics/damage.md` step 4) or an impact comes 25 %
  stronger, and never while the same sample is still playing; for a traffic car, whenever the
  previous one-shot has ended.
- **Water**: MM2 has no splash sound (the water effect is buoyancy only, `../engine/effects.md`,
  "Effects MM2 does not have"); the `SWIMWITHFISHES` entry (id 22) is heard only if a mod gives a
  prop `ColliderId` 22. Driving on the `water` material uses surface block 1 (wet skids only).
- Props, pieces and car parts make no impact sounds of their own (a prop hitting the city or
  another prop is silent); pedestrians have no collision body (their sounds are
  `traffic-sounds.md`). Bottoming out, landing and being airborne have no samples of their own
  (`aud22/airborne.22k.wav` ships but nothing names it).

## Samples

All come from the 3D sample cache (one copy per slot; a car or traffic car uses its slot's copy,
`sound-system.md`, "Samples of 3D sources"), have one instance and sub-path `aud22`; their owner
sets volume, pitch and pan directly.

| Sample | Name built | Pitch control | Played | Echo |
|---|---|---|---|---|
| rolling | `surfaces\<rolling wave>` | yes | looped | yes |
| skid | `surfaces\<skid wave>` | no | looped | yes |
| suspension | `suspension\<name>` | yes (never set) | once | no |
| tyre wobble | `<name>` (no folder) | yes | once | no |
| impact | `impacts\<name>` | only if `freq` ≠ 1.0 | once | no |

## Order within a frame

For each car holding a slot, in its slot update (`sound-system.md`, "Order within a frame"):
1. echo switch if the echo state changed, then (echo on) the echo tick of "Echo copies";
2. a 3D car takes its new attenuation, pan and A′ (which also refreshes its playing impact
   samples); a 3D car past its maximum distance releases its slot here and does nothing more;
3. gear and engine sounds (`car-sounds.md`);
4. suspension thump, step 1 (rolling), step 2 (skids), tyre wobble.

Impacts play during the physics samples, when the collision response reports them.

## Events

| Sound | Event | Produced by |
|---|---|---|
| rolling, skids, wobble, suspension | the car's audio update each frame | `sound-system.md`, "Order within a frame"; inputs from `../physics/vehicle-wheels.md` ("Wheel state": `grounded`, `slide`, `rate`), `../physics/collision.md` ("Materials" `sound`), `../physics/vehicle.md` step 2 (`speed`), `../physics/damage.md` (damage, `MedDamage`, `MaxDamage`) |
| surface file choice | weather 3 | `../formats/night.md` |
| tunnel block, echo | echo state | `sound-system.md`, "Echo" |
| car impact | count step of an impact report | `../physics/damage.md`, "Per impact report" step 5 (and the enabled flag, "State") |
| traffic impact | a traffic body's impact report | `../physics/car-collision.md`, 3.4 |
| airborne flag → big-air motif | all wheels off the ground | this page; music spec |

Not yet exposed by the game pages (the builder needs them): the impact report must hand the sound
system the impulse **vector** J (not only s) and the **impact sound id** of the other side
(table above: the collision bodies need an id field: 0 by default, `ColliderId` for a loose body,
1000 + n for a network car); the wheel's material `sound` number must be kept with its last ground
hit.

## Things that look like mistakes (keep them)

- All cars, opponents included, use the player folder's surface files; the opponent folder's
  surface files (with their "for tunnels" column) are never read, and nothing ever reads the
  ice file.
- The impact table cache by name makes police cars use the player's impact table (above).
- The `ID` column of the impact file is ignored; a file without `ENDOFDATA` gives no impact
  sounds at all.
- Impact and skid volumes do not subtract the window minimum (force or slide).
- The surface block follows the front-left wheel only (with the two-front-wheel hysteresis), and
  the skid loudness the largest slide of all four.
- While the echo state is on, every car (also one far from the tunnel) uses the tunnel block, and
  switching to or from it stops nothing: a rolling or skid loop that was playing on the old block
  keeps looping, unchanged, until that block is used again (or the car loses its slot). For example
  entering a tunnel in the rain while skidding on asphalt leaves `wetskid` looping.
- A 3D car's tyre wobble never sets its pitch.
- A material sound number at or above the block count under both front wheels restarts block 0's
  rolling and skid loops every frame (step 1.6).
- Samples missing from the shipped data: `surfaces\skidflagstone` (block 4) and
  `impacts\caroncobble` (`TCANPLASTIC`).

## Checked against

`tools/audio/tyre_impact.py` reads every shipped file of these formats with the rules above
(outputs in `work/check/audio/tyre_impact_*.txt`):

- `player/default_surfacedry.csv`: tunnel 0, 5 complete blocks, 8 skid rows; e.g. block 2 (grass)
  volSlope 0.0160000008, pitchSlope 0.0159999989, skidRange 0.219999999; at 10 m/s the grass loop
  plays at volume 0.50999999, pitch 1.00999999; at 25 m/s and above 0.75 / 1.25. Block 0 skid
  volume at slide 0.65: 0.746999979 with `tireskid1_ps1` and `tireskid2_ps1`.
- `player/default_surfacewet.csv`: tunnel 5, 6 complete blocks. The opponent dry/wet files would
  give 5 / 6 blocks (the wet one with an ignored extra column); the ice files would misread (their
  layout has 12 columns and an `ENDOFDATA` line read as a ninth block header).
- `suspensionaudio.csv`: `suspension\Suspension3`, |minVel| 2, volumes 0.850000024–0.899999976,
  inv 0.333333343: all rates 2.0 → volume 0.850000024; mean 1.975 → no thump.
- `tirewobble.csv`: `tirewobble1`, 0.970000029–1, pitch 0.75–1.5, inv 0.0666666701; vpbug
  (Med 150000, Max 321300): no wobble below damage 158565 (frac 0.05); a 0.335 m wheel thumps
  every 2.10486531 m.
- Both read impact files: 24 entries, 28 sample rows, `ENDOFDATA` found, ID column equal to the
  position; e.g. player `WALL` at F_imp = 8000 starts `Carimpactsoft3` at 0.932857156 and
  `Carimpactmed1` at 0.939999998; opponent `WALL` at 1000 gives `Carimpactmed1` 0.949999988.
- 995 prop data files: `ColliderId` values as listed above; materials: `grass` 2, `water` 1, all
  others and every bound 0.
- 78 sample references (31 distinct names); the two missing files are the ones named above.
- Static reading of the game binary: car audio set-up and its file names, the surface, suspension,
  wobble and impact readers (separators, number conversion and the slope arithmetic), the 2D and
  3D per-frame updates of all four car sounds, the airborne probe, slot assignment and release,
  the echo switch, the impact table cache, the impact play and refresh paths, the car and traffic
  impact callbacks, every writer of the collision bodies' id, and the set-up order of the game.
