# Car sounds: engine, gear click, horn, siren and the big-vehicle extras

Every car (the player's, opponents', cops', other players' in a network game) owns one **car
audio**: a sound source with the car's engine samples, its horn, the reverse "gear click", and,
depending on the car, a police siren or the reverse beeper and air brake of a bus. This page says
which files each car reads, how every one of those sounds is driven, and the two flags the music
reads from the player's car (airborne, chase count). Surface, skid, wobble, suspension and impact
sounds belong to the same source but are on `tyre-impact-sounds.md`; traffic cars use their own
much simpler audio (`traffic-sounds.md`).

Words (sample, 2D sample, 3D source, slot, attenuation A, pan, doppler factor, echo state, echo
copy) are those of `sound-system.md`. All car samples are made **with pitch control**, one instance
each (the 3D sample cache gives each slot its own copy, `sound-system.md` "Samples of 3D sources").
Numbers are MM2's exact single-precision values; formulas are evaluated left to right in float32
as `../physics/vehicle.md` "Precision" says.

## The short version

- A car's audio kind comes from three lists in `aud/cardata/shared/vehtypes.csv`: semi/bus
  (`vpcentury`, `vpbus`, `vpddbus`), police (`vpcop`, `vpsemi`, `vpeagle`), and an "always nitro"
  switch (off). Police cars get a siren; semi/bus cars a reverse beeper and an air brake.
- The human player's car reads `aud/cardata/player/<car>.csv` and is a **2D** source (centred, full
  level, whatever the camera does). Every other car reads `aud/cardata/opponent/<car>.csv` and is a
  **3D** source (0–150 m, priority 9; police 7, 10 while the siren sounds). Missing file:
  `default.csv` of the same folder.
- The engine is 1–4 looping samples, each with a volume window and a pitch ramp over the car's
  displayed RPM. Nothing else (throttle, load, damage) changes the engine sound.
- The "clutch" wave clicks only when the gear goes into or out of reverse, and only for the player.
- The player's horn loops while held; on a car with a light bar the horn control toggles the siren
  instead. AI cars and police-listed cars never honk.
- The siren plays its samples in a scripted cycle. A cop chasing the human player adds 1 to the
  **chase count** the music listens to. A 3D siren is never quieter than 0.75 and keeps its slot
  at any distance.
- Several MM2 features are inert: the "min amp speed" volume boost, the air brake on the shipped
  buses, the nitro sound, the siren fade and (mostly) the explosion wind-down. They are described
  so that mods behave as in MM2.

## Which audio a car gets

### Owner kinds

Every car's audio is made when the car is set up, with one of three owner kinds:

| Owner kind | Cars | cardata folder | Source | Horn sample | Gear click sample | Airborne test |
|---|---|---|---|---|---|---|
| player | the human player's car | `player` | 2D | loaded | loaded | yes |
| AI | every computer-driven car made by the race data: opponents, cops, the other AI drivers of a race or game mode | `opponent` | 3D | not loaded | not loaded | no |
| network | other players' cars in a network game | `opponent` | 3D | loaded (not for police-listed cars) | not loaded | no |

The car's **name** is its base name as the game uses it everywhere (the player's chosen car from
the car list, `../formats/car-model.md`; the name in the race data for opponents and cops,
`../ai/police.md` "The police list in the race file"; the remote car's name for network cars).

### Audio kind (`vehtypes.csv`)

`aud/cardata/shared/vehtypes.csv` is read once per game, before any car is made. Reading: lines as
in "Reading the text files" below.

| Line | Contents |
|---|---|
| 1 | header, ignored |
| 2 | **semi/bus list**: car names separated by commas, ended by a field exactly `ENDOFDATA` |
| 3 | header, ignored |
| 4 | **police list**, same form |
| 5 | header, ignored |
| 6 | **always nitro**: on if the first field is `TRUE` (any case), else off |

Shipped: semi/bus `vpcentury`, `vpbus`, `vpddbus`; police `vpcop`, `vpsemi`, `vpeagle` (no car of
that name ships); always nitro `FALSE`. A list without `ENDOFDATA` makes MM2 crash (ours: the end
of the line ends the list). If the file is missing, both lists are empty and nitro is off.

A car's audio kind is decided in this order, comparing names **exactly** (case matters, unlike file
names):

1. name in the semi/bus list → **semi/bus** audio;
2. else name in the police list → **police** audio;
3. else always nitro on → **nitro** audio;
4. else **plain** audio.

Every kind contains everything plain audio has (engine, horn, gear click, tyre and impact sounds);
the others add their extras.

Note: the cops of the shipped race files are mostly `vpcop`, but some police lists also use
`vpauditt`, `vpdb7` and `vpmustang99`; those cops have plain audio, so they have no siren sound and
no explosion (their light bars follow `../ai/police.md` independently).

### Setting up a car's audio

When a car is set up (and again whenever it is set up anew; the old car audio is deleted first):

1. Pick the audio kind (above).
2. Police: read the siren file ("Siren file" below) and set the **chase count** to 0 (see "Chase
   count": every police car set-up resets it). Semi/bus: read `semidata.csv`. Nitro: read the nitro
   file.
3. Read the cardata file: `aud/cardata/<folder>/<car>.csv`; if it is missing or unusable ("The
   cardata file"), `aud/cardata/<folder>/default.csv`. If neither works, **stop here**: the car
   makes no sound at all (not even tyres or impacts).
4. The tyre, suspension, wobble and impact files (`tyre-impact-sounds.md`).
5. Source settings:
   - position: the car's body position (the origin of its body matrix), followed every frame;
   - 2D for the player kind, 3D otherwise; the player's 2D source **never** switches to 3D (MM2
     has a switch for it that nothing calls), so camera views do not change the player's car
     sounds;
   - drop-offs min 0 m, max 150 m (both stored squared, `sound-system.md`);
   - priority 9, police 7 (10 while the siren sounds, below);
   - doppler constant k = 0.0176315 (single 0x3C906FF1);
   - pan of a 2D car: 0 (centred).
6. The "min amp speed" boost, inert in MM2: the source has a speed value s (set to 0 here and never
   updated), a limit 5.0 and a top factor 1.05; whenever the 3D update below finds d² < 625.0
   (25 m), it multiplies the horn and gear click volumes (and the impact sound's,
   `tyre-impact-sounds.md`) by `boost = 0.0099999905 × s + 1.0` if s < 5.0, else 1.05. With s
   always 0 the boost is exactly 1.0. **Ours: leave it out.**

## Reading the text files

All files on this page are text. Each read takes one line up to and including its line feed (at
most 999 characters; a longer line continues as the next read); the line is cut at its first CR or
LF, then split at commas where **runs of commas count as one and leading commas are skipped**, so
an empty field cannot be expressed: the fields after it move up. Numbers are read like C `atof`
(decimal, optional sign, point and exponent; anything else ends the number; no number = 0) or
`atoi` for integers. Asking for a field the line does not have makes MM2 read through a null
pointer and crash; ours: a missing number reads 0, a missing name means no sample, and the loader
logs a warning. No shipped file that MM2 opens has a short line, an empty field inside a line or a
blank line.

## The cardata file (`aud/cardata/<player|opponent>/<car>.csv`)

| Line | Fields |
|---|---|
| 1 | header, ignored |
| 2 | horn wave, horn volume, flags, number of engine samples, clutch wave, clutch volume (later fields ignored) |
| 3 | engine header: only its **4th field** matters (format choice) |
| 4 … end of file | one **engine sample** per line |

Line 2:

| Field | Type | Use |
|---|---|---|
| horn wave | name | sample `horns\<name>`, loaded for the player and network kinds only |
| horn volume | float | horn volume *h* |
| flags | int | read and **ignored** (shipped 0; `vpbus` 1, `vpcentury` 2, `vpcop` 4, `vpddbus` and `vpsemi` 8) |
| number of engine samples | int | read and **ignored**: the engine has one sample per row actually present |
| clutch wave | name | sample `<name>` (no folder: `REVERSE`, `TRUCKGEARSHIFT` are in `aud/aud22/`), loaded for the player kind only |
| clutch volume | float | gear click volume *c* |

So `opponent/default.csv` (count 2, one row) gives one engine sample, `player/vpcop.csv` (count 2,
four rows) four. The file is **unusable** (and `default.csv` is tried next) when it has fewer than
three lines; a usable car file that fell back this way has still loaded its line-2 samples, which
the default file then replaces.

**Format choice**: if the 4th field of line 3 is `Volume Divisor` (any case) every row is in the
divisor format, else in the window format. Shipped car files all use the window format (header
field 4 `fade in  start RPM`).

### Window format rows (11 fields)

| # | Field | Name here |
|---|---|---|
| 1 | engine wave | sample `engines\<name>` |
| 2 | min volume | *v₀* |
| 3 | max volume | *v₁* |
| 4 | fade in start RPM | *a* |
| 5 | fade in end RPM | *b* |
| 6 | fade out start RPM | *c* |
| 7 | fade out end RPM | *d* |
| 8 | min pitch | *p₀* |
| 9 | max pitch | *p₁* |
| 10 | pitch shift start RPM | *s* |
| 11 | pitch shift end RPM | *e* |

Fields after the 11th are ignored. Precomputed slopes (float32; inv(x) = 0 if x = 0, else 1 / x):

    kIn  = (v₁ − v₀) × inv(b − a)
    kOut = (v₁ − v₀) × inv(d′ − c)
    kP   = (p₁ − p₀) × inv(e′ − s)

where *d′* and *e′* are the numbers as parsed from the text (not yet rounded to float) and the
subtraction is rounded to float32; for the shipped whole-number RPMs this is the same as using
*d* and *e*.

### Divisor format rows (8 fields)

| # | Field | Name here |
|---|---|---|
| 1 | engine wave | sample `engines\<name>` |
| 2 | min volume | *v₀* |
| 3 | max volume | *v₁* |
| 4 | volume divisor | *D* |
| 5 | min pitch | *p₀* |
| 6 | max pitch | *p₁* |
| 7 | pitch divisor | kP = inv(field 7) |
| 8 | vol inverse RPM | *R* |

Only two never-opened shipped files (`player/Copy of default.csv`, `opponent/Copy of
vpmustang99.csv`) use it; it matters only for mods.

### Files never opened

`aud/cardata/engineparamsPlay.csv` and `engineparamsOpp.csv` (debug dumps with garbage names) are
not named by the program. Files whose name no car has (`Copy of …`, `vpbullet.wrk.csv`,
`vpcaddie59.csv`) are never reached; `opponent/policesiren.csv` is never opened (the siren always
comes from the `player` folder).

## Engine

### What drives it

The engine sound follows one number: the car's displayed engine RPM `rpm`
(`../physics/vehicle-powertrain.md`, "State and reset" and "Update", step 5–6: it includes the
slide during the clutch delay `GCL`). It is read every audio update, for every kind of car. Throttle,
engine load, gear, speed and damage have no other effect; idle is simply `rpm` near `IdleRPM`.

### Volume and pitch of one engine sample

Window format, for `r = rpm`:

    V(r) = v₀                    if r ≤ a
           v₀                    if not (r < d)          (at or above the fade-out end)
           (r − a)·kIn + v₀      if r < b
           v₁                    if r ≤ c
           (d − r)·kOut + v₀     otherwise

    P(r) = p₀                    if r ≤ s
           p₁                    if not (r < e)
           r·kP + p₀             otherwise

The tests are made in that order. Outside its window a sample keeps its **minimum** volume (it does
not go silent). In the pitch ramp the start RPM is **not** subtracted: the pitch jumps from p₀ to
s·kP + p₀ just above *s* and can exceed p₁ just below *e* (then jumps back to p₁); keep it exactly.
Pitch factors above 2 are clamped by the mixer (`sound-system.md` "Pitch").

Divisor format:

    V(r) = clamp(r / D if r < R else D / r,  v₀, v₁)     (below v₀ → v₀, above v₁ → v₁)
    P(r) = r·kP + p₀    while  s < r < e,   else p₀ / p₁ as above

MM2 never sets *s* and *e* in this format (they hold whatever memory was there). **Ours**: treat
them as unset, i.e. P(r) = r·kP + p₀ always.

### Applying the engine samples (every audio update)

For each engine sample in file order:

1. V = V(rpm). If V < 0.25: stop the sample if it is playing, and go to step 5.
2. P = P(rpm).
3. Player (2D): volume V, pitch P. 3D car: volume V × A, pitch P × doppler factor, and pan (stereo
   only) — A, pan and doppler from the car's 3D update below (A **without** the inert boost).
4. If the sample is not playing, play it looped (from the start).
5. If the sample's echo is on, tick it ("Each audio update", "Ticking").

The smallest minimum volume in the shipped files that MM2 opens is 0.432, so the 0.25 rule never
stops a shipped engine sample by itself; it is what silences the engine (next section).

### Silencing

The engine has a **silenced** switch: while on, every engine sample's v₀ and v₁ count as 0, so V = 0
and every sample is stopped on each update; off restores the file values. It is used only for the
player's car by two game modes (game modes' spec): **Checkpoint** switches it on when the player's
car is damaged out (wrecked, `../physics/damage.md` "Wrecked") and when the car hits water
(Checkpoint's water rule), **Blitz** when the player's car is damaged out; both switch it off at the
mode's reset. Cruise and the other modes never silence the engine (a wrecked car in Cruise idles
with throttle 0).

A 3D police car's engine is also silent while its siren sounds ("Siren", each update).

## Gear click (the clutch wave)

Player kind only. The car audio remembers the previous gear index *g′* (−1 when made; gear indices
as in `../physics/vehicle-powertrain.md` "Gear slots": 0 reverse, 1 neutral, 2 first …). Each
audio update, with *g* the car's current gear:

1. If the clutch sample exists and the change is between reverse and not reverse (*g′* = 0 and
   *g* ≠ 0, or *g′* ≠ 0 and *g* = 0): play it once (volume *c*, pitch 1, centred; set once at
   set-up, below).
2. *g′* = *g*.

Ordinary up and down shifts make no sound (`TRUCKGEARSHIFT` too clicks only into and out of
reverse). Neutral counts as "not reverse". A car that starts in reverse clicks on its first update.

## Horn

- **Player**: while the horn control is held (`../engine/controls.md`, Enter = horn) the horn
  loops; on the press, if the horn sample exists and is not playing, it is rewound and played
  looped; on the release it is stopped. The car audio keeps a **horn held** flag (set on the press,
  cleared on the release) that traffic reads (`../ai/traffic-reactions.md`, "H").
- **Car with a light bar** (any `SRN` part, `../ai/police.md` "Lights"): the horn control does not
  honk; each **press** toggles the light bar and with it the siren sound (start when the light
  bar turns on, stop when it turns off; "Siren"). MM2 assumes such a car is police-listed: a light
  bar car without police audio crashes MM2 on the press (ours: toggle the lights, no sound). A
  police-listed car without a light bar honks silently (police audio has no horn playback).
- **Police-listed cars never honk**, whoever drives them; AI cars have no horn sample at all.
- **Network cars**: the remote car's horn bit and siren bit (network spec) start and stop the horn
  and the siren the same way (siren uncounted).
- Levels: player (2D): volume *h*, pitch 1, centred, set once right after the car first takes its
  slot at set-up (MM2 does not set them again; if the 2D car ever had to take a slot later, its horn
  and click copies would keep whatever volume they last had, 0 for a fresh copy). Network (3D):
  every update volume A × *h*, pitch = doppler factor, pan.

## Siren

Police-listed cars only. The same siren is used by cops, the player's own police car, police cars
driven by opponents and network police cars.

### Siren file

`aud/cardata/player/londonpolicesiren.csv` if the city's name is exactly `london` (lower case),
otherwise `aud/cardata/player/sfpolicesiren.csv`; always the `player` folder, for every owner kind.

| Line | Contents |
|---|---|
| 1 | header, ignored |
| 2 | explosion wave (field 1): sample `sirens\<name>`; field 2 (a volume) is **not read**: the explosion volume is always 1.0 |
| 3 … | blocks, read line by line with a mode (none at first) |

From line 3 on, each line is classified by its first field:

- exactly `Sample name` → mode "sample";
- exactly `play time` → mode "step";
- anything else, in mode "sample": a **siren sample** — field 1 its wave (`sirens\<name>`), field 2
  its volume *vᵢ*; appended to the list (so several data lines after one header make several
  samples);
- anything else, in mode "step": a **step** of the last siren sample — field 1 play time *tⱼ*
  (float, seconds), field 2 next sample index *nⱼ* (int); appended to that sample's step list;
- anything else with no mode yet: ignored.

The other fields of a line are ignored. Next indices are not checked (ours: an index outside the
list stops the stepping); a siren sample without steps in a file with two or more samples makes MM2
crash when it becomes current (ours: it never switches away). A missing siren file leaves the list
empty and MM2 crashes on the first start (ours: no siren sound).

Shipped (`car_sounds.py siren`):

| File | Explosion | Siren samples (volume) and steps (play time → next) |
|---|---|---|
| `londonpolicesiren` | `explosion` | 0 `siren_london` (0.95): 15 → 1; 1 `siren_london2` (0.95): 15 → 0 |
| `sfpolicesiren` | `explosion` | 0 `police2sirenloop` (0.95): 4.45 → 1, 2.65 → 1, 5.25 → 1, 4.15 → 1; 1 `police2hilowloop` (0.95): 1.25 → 0, 2.31 → 0, 2 → 0, 3 → 2; 2 `police1fastsirenloop` (0.95): 4.45 → 3, 1.65 → 3, 2.35 → 3, 0.25 → 3; 3 `police1whailerloop` (0.95): 1 → 2, 0.65 → 2, 0.15 → 2, 1.3 → 0 |

### Siren state

| Field | When made | Meaning |
|---|---|---|
| mode | off | off, **on (counted)** or **on (uncounted)** |
| current | 0 | index of the current siren sample |
| step index | 0 per siren sample | which step of that sample is next |
| timer | 0 | seconds the current sample has played |
| level | — | working volume of the wind-down |
| wind-down | off | flag, with its time *t* |
| explosion flag | off | |
| keeps slot | off | the 3D source "keeps its slot" mark of `sound-system.md` |

Nothing but the stepping changes current, timer and the step indices: they survive stops, starts
and resets, so a siren resumes where it stopped.

### Start

Start(counted):

1. If mode ≠ off: nothing.
2. Priority 10, keeps slot on, mode = on (counted) if *counted* else on (uncounted), level = the
   current sample's volume. If counted: **chase count + 1**.
3. If the car holds a slot and the current sample is not playing: player (2D): volume = its
   volume, pitch 1; 3D: volume = A × its volume, pitch = doppler factor (the values of the car's
   last 3D update); play it looped.
4. 3D and holding a slot: stop all engine samples.

Who starts it: the player's horn toggle and the network siren bit start it uncounted; a cop starts
it when it enters FOLLOW (`../ai/police.md`, "Following or apprehending"; `traffic-sounds.md` covers
the police events), **counted if the cop's perp is the human player's car** (the perp's car audio
has the player kind), uncounted otherwise.

### Stop

1. If chase count > 0 and mode = on (counted): chase count − 1.
2. Mode off, keeps slot off, priority 7, explosion flag off.
3. If the car holds a slot: stop the current sample. (The explosion is not stopped.)

Called by the player's horn toggle, the network siren bit, a cop when "the perp escapes" (always
after the explosion, if any) and a cop's reset (`../ai/police.md` "Parking (reset)" step 2).

### Explosion

Only a cop's "the perp escapes **with explosion**" (the cop is wrecked, `../ai/police.md` "The perp
escapes") runs it, and Stop always follows at once:

1. If the car holds a slot and the explosion is not playing: volume A × 1.0, play it once (pitch
   left as it is, i.e. 1).
2. Explosion flag on; level = the current sample's volume.
3. If chase count > 0 and mode = on (counted): chase count − 1.
4. If the car holds a slot: wind-down on with *t* = 0.01, and run one wind-down step (below).
   Otherwise mode = off.

Consequences of MM2's order, to be kept:

- When the cop holds a slot, step 4 leaves mode = on (counted), so the Stop that follows counts
  down **again**: a wreck can take two off the chase count (never below 0).
- A wrecked cop repeats "the perp escapes with explosion" every frame until it is reset
  (`../ai/police.md`, "One frame of police", step 6), so while it holds a slot the explosion
  **plays again each time it ends** (`explosion` is 3.17 s long).
- The wind-down flag is not cleared by Stop or by a reset, so the next time that cop's siren
  starts (after a world reset) it **winds down instead of sounding**: about 1 s at half pitch, then
  0.45 until 1.75 s, then a few halvings and off. Its end turns the wind-down and the mode off
  without counting down, so each wind-down that runs to its end leaves the chase count one too high
  (until the next police set-up); keeps slot stays on. The same update then **starts the current
  sample again** (step 2 of the update below runs whatever the mode): it loops on at the last volume
  and pitch the wind-down gave it (A × the last level: A × 0.11875 for a sample of volume 0.95, about
  −88 dB at A = 1; pitch doppler × 0.45) and nothing touches it until the cop's next Stop or Start, a
  pause or the loss of its slot. The next Start (e.g. the cop back in FOLLOW from APPREHEND) finds it
  playing and leaves it, counts up again (if counted), and from the following update the siren
  sounds normally (the wind-down flag is off by then). A Stop during the wind-down leaves the flag
  on: the next Start winds down again from the *t* reached.
- A "lost him" escape (without explosion) sets a fade flag that the Stop immediately clears: the
  siren just stops. (The fade itself, volume × 0.95 per update down to 0.2, is therefore never
  heard.)

### Each update while the siren sounds (mode ≠ off)

**Stepping** (only if the file has a number of siren samples other than 1):

1. With *S* = the current sample and (*tⱼ*, *nⱼ*) = its step at its step index: if timer ≥ *tⱼ*:
   advance *S*'s step index (back to 0 after its last step), stop *S* if playing, current = *nⱼ*,
   play the new current sample looped (with the volume and pitch its copy has), timer = 0.
2. timer = dt + timer (dt = the frame time), whether or not it switched.

So a switch happens on the first update whose timer (the sum of the frame times of the updates
since the last switch, counting the switching update's own dt) is at least *tⱼ*. With a one-sample
file the sample just loops. The London siren alternates its two samples every 15 s; the SF siren
cycles 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3 and back to 0 (at 30 frames per second the
first switches are at 4.50, 5.77, 8.43, 10.77 s; `car_sounds.py siren` prints the whole sequence).

**Then, 3D (cops, AI and network police cars)**, with A, pan and doppler from the car's 3D update:

1. Unless wind-down is on: volume = max(v × A, 0.75) with *v* the current sample's volume, pitch =
   doppler factor. If wind-down is on: one wind-down step instead.
2. Play the current sample looped if it is not playing (also when the wind-down step of step 1
   has just stopped it and turned the mode off: it starts again, see "Explosion"); pan (stereo).
3. If the explosion is playing: its volume = max(A × 1.0, 0.85), its pan = pan.
4. The engine samples are **not** updated (they stay stopped).

Because keeps slot is on, a sounding siren never gives its slot back for distance; beyond 150 m its
A keeps the last value it had inside (`sound-system.md`), so a chasing cop is heard at no less than
0.75 at any distance once it has a slot (and wins slots with priority 10).

**Player (2D)**: one wind-down step if wind-down is on (never, in practice); play the current sample
looped if it is not playing (whatever the mode, as in 3D step 2). Each siren sample plays at its own
volume and pitch 1 (set when the car takes its slot). The player's engine keeps running under the
siren.

**Wind-down step** (on the current sample):

- *t* ≤ 1.0: volume A × *v*, pitch doppler × 0.5, then *t* = dt + *t*;
- 1.0 < *t* ≤ 1.75: the same with 0.45;
- *t* > 1.75: if level > 0.2: level = level × 0.5, volume A × level; else stop the sample, wind-down
  off, mode off (chase count and keeps slot untouched).

### Slot rule of a police car

Each 3D update starts with the car's 3D update below, except the release test: the police car gives
its slot back when (it is past its maximum distance — impossible while keeps slot is on — or the
explosion flag is on) **and** mode ≠ on (counted) **and** the explosion is not playing. (With Stop
always clearing the explosion flag right after the explosion, this is the plain car rule in
practice, plus "stay while the explosion plays".)

### Chase count

One number for the whole game, read every frame by the music (music spec: chase music on 0 → 1,
return on 1 → 0, exact values only):

- set to 0 every time a police-listed car's audio is set up (at game set-up, and also when a
  network police car is set up during a game);
- + 1 at a counted Start, − 1 (never below 0) at Stop or Explosion of a counted siren.

The player's own siren never counts; nor does a cop chasing a network player or an opponent.

## Semi and bus extras

Semi/bus-listed cars (shipped `vpcentury`, `vpbus`, `vpddbus`; `vpsemi` is police-listed and gets a
siren instead). `aud/cardata/shared/semidata.csv`:

| Line | Fields |
|---|---|
| 1 | header, ignored |
| 2 | reverse sample, air blow sample, reverse volume *r*, air blow volume *q* |

Samples are named without a folder (shipped `freightreverse`, `freightairblow` in `aud/aud22/`;
0.87, 0.9). Missing file: no samples, volumes 0.87 and 0.9.

Each update (after the plain car update; in 3D only if the car kept its slot):

1. Levels — 3D: beeper volume A × *r*, air blow volume A × *q*, both pitch = doppler, pan. 2D (the
   player): both samples got volume ***r*** (not *q*: MM2 uses the reverse volume twice) and pitch 1
   at set-up.
2. **Reverse beeper**: gear index 0 → if the beeper is not playing, rewind it and play it looped;
   any other gear → stop it if playing. No speed condition.
3. **Air brake**: *braking* means both front wheels' `BrakeCoef` (`../physics/vehicle-tune.md`) are
   above 0.5 — a fixed tuning value, **not** the brake pedal. With *v* = the car's speed
   (`../physics/vehicle.md` step 2) and one latch shared by **all** semi/bus cars (off at start):
   - *v* > 0.04: if not braking, latch off;
   - *v* ≤ 0.04: if braking and the latch is off: play the air blow once, latch on; if not braking,
     latch off.

The shipped semi/bus cars have front `BrakeCoef` exactly 0.5, so they **never** play the air brake.
A mod car with front `BrakeCoef` > 0.5 plays it once, the first time it is nearly stopped, and then
never again in that game unless a semi/bus car with `BrakeCoef` ≤ 0.5 resets the shared latch.

## Nitro (inert)

With always nitro on, every car that is neither semi/bus nor police gets nitro audio: plain audio
plus one sample read from `aud/cardata/opponent/default_nitro.csv` (always this name and folder;
line 2: sample name without folder, volume, default 0.98; not shipped). Nothing in MM2 ever plays it
(it only gets its echo switched and, while playing, its 3D levels updated). **Ours**: treat nitro
audio as plain audio.

## Airborne flag (player's car, for the music)

The music's big-air motif fires on the rising edge of this flag (music spec). Only the player's car
sets it; for every other car it stays off. Each audio update of the player's car (inside its tyre
sound step, `tyre-impact-sounds.md`):

1. *n* = number of the car's wheels that are `grounded` (`../physics/vehicle-wheels.md`), as left by
   the last physics step.
2. *n* ≥ 1: airborne = off.
3. *n* = 0: with (x, y, z) the car's body position, for k = 1 … 10: probe from S = (x, y_k, z) to
   E = (x, y_{k+1}, z), where y₁ = y − 3.0 and y_{k+1} = y_k − 3.0 (float32, repeated subtraction),
   using a fresh probe state, ignoring the car itself, with the ground mask (`../physics/collision.md`,
   "Ground queries"). At the first hit: airborne = on, stop. No hit: unchanged.

So it turns on when all four wheels are off the ground with ground between 3 m and 33 m below the
car, and off as soon as one wheel touches.

## Each audio update

The car itself, every frame after its physics (the car's post-physics update), refreshes its
position and asks for a slot when it has none (2D: always; 3D: inside 150 m; `sound-system.md`
"Getting and losing a slot"; a cop's own update asks once more). Then, when the 3D manager updates
its holders (not paused), the car audio runs:

1. **Echo**: if the echo state differs from the car's own echo flag, switch the echo copies of all
   its samples on or off (table below) and copy the flag; then tick the echo copies ("Ticking"
   below).
2. 3D car: **3D update**: if past the maximum distance (and not keeping its slot): release the slot
   and stop. Otherwise compute A, pan and doppler (`sound-system.md`, k = 0.0176315) and set the
   horn and gear click levels (above; × the inert boost). Then gear click, engine samples (3D),
   tyre sounds and the impact sound's per-update step (`tyre-impact-sounds.md`), then the kind's
   extras (semi/bus: levels, beeper and air brake). Police audio instead: slot rule of "Siren",
   gear click, tyre sounds, then the engine samples if the siren is off or the siren update if it
   sounds; police audio never runs the impact sound's per-update step.
3. Player (2D): gear click, engine samples (2D), tyre sounds (with the airborne flag), the impact
   sound's per-update step, then the extras (semi/bus: beeper and air brake). Police audio: gear
   click, tyre sounds, engine samples, then the siren update if it sounds (no impact step).

Releasing the slot stops all the car's samples and switches their echo off; the next assignment
picks up the slot's copies again (siren samples get their own volumes back; engine levels come with
the next update).

Echo copies (`sound-system.md` "Echo"; T = delay, G = echo gain, F = initial echo pitch; "manager"
= the manager's echo delay, 0.5 s):

| Samples | T | G | F |
|---|---|---|---|
| engine samples | manager | 0.96 | not set |
| horn | 0.05 | 0.96 | 0.997 |
| gear click | manager | 0.96 | not set |
| siren samples | manager | 0.96 | not set |
| explosion | no echo | | |
| semi beeper, air blow | manager | 0.96 | not set |
| nitro sample | manager | 0.96 | not set |
| tyre and impact samples | `tyre-impact-sounds.md` | | |

**Ticking** (the only place the car's echo repeats advance, `sound-system.md` "Echo"; once per
audio update, so not while paused or without a slot):

- Horn, gear click, siren samples, semi beeper and air blow, nitro sample (and the tyre samples'
  tick of `tyre-impact-sounds.md`): ticked in step 1, before this update's commands, whenever the
  car's echo flag is on. Plain audio switches on and ticks in the same update; police, semi/bus and
  nitro audio tick only from the update after the switch-on (and once more in the update that
  switches it off, which has no effect since the repeats were dropped).
- Each engine sample: ticked inside the engine step ("Applying the engine samples"), right after
  that sample's commands of this update (also when step 1 there stopped it), while its own echo is
  on. So when the engine step does not run — a 3D police car with its siren sounding — its engine
  samples' echo repeats do not advance.
- The explosion has no echo and is never ticked.

### Pause, reset, results

- **Pause**: no car audio updates; the sound system stops every playing sample in the first two
  paused frames. After the pause the loops come back on the next update (engine samples, a
  sounding siren's current sample, the beeper); a horn held through the pause stays silent until
  pressed again; a cut gear click or explosion is lost. The siren timer does not advance while
  paused.
- **Car reset** (respawn, world reset): the source's 3D values are reset; a 3D car gives its slot
  back and asks again on the next frame, the player's 2D car keeps it. Not reset: the previous gear
  (a car reset while in reverse clicks once), the airborne flag, the horn held flag and all siren
  state — so the player's own siren keeps sounding through a reset (as does its light bar). Cops
  also stop their siren at their reset.
- **Results screen**: every source is silent (`sound-system.md`).

## The car as a 3D source (summary for `sound-system.md`)

| Item | Value |
|---|---|
| position | the car's body position |
| 2D / 3D | player kind 2D (never switched); AI and network 3D |
| drop-offs | 0 m … 150 m |
| priority | 9; police 7, 10 while the siren sounds |
| doppler k | 0.0176315 (cars and police alike) |
| keeps its slot | police car while its siren sounds (set at Start, cleared at Stop) |
| A used for | engine, siren, explosion: A; horn, gear click, impact: A × boost (boost = 1.0) |
| doppler used for | engine pitch × doppler; horn, siren, beeper, air blow, nitro pitch = doppler; not the gear click or explosion |

## Events

| Sound | Triggered by | Producing page / section |
|---|---|---|
| engine samples | `rpm` every update | `../physics/vehicle-powertrain.md` "Update" (step 6 of `../physics/vehicle.md`) |
| engine silenced / restored | player damaged out, water (Checkpoint); mode reset | `../physics/damage.md` "Wrecked"; game modes' spec (**not written yet**) |
| gear click | gear index into / out of 0 | `../physics/vehicle-powertrain.md` "Gear slots"; `../engine/controls.md` "Gearbox keys", "Automatic reverse" |
| horn | horn control press / release (player); horn bit (network) | `../engine/controls.md` "Default keyboard bindings"; network spec (**not written yet**) |
| player siren toggle | horn press on a light-bar car | `../ai/police.md` "Lights" |
| cop siren start | cop enters FOLLOW | `../ai/police.md` "Following or apprehending" (`traffic-sounds.md`) |
| cop siren stop | the perp escapes; cop reset | `../ai/police.md` "The perp escapes", "Parking (reset)" |
| explosion | the perp escapes with explosion (cop wrecked), every frame while wrecked | `../ai/police.md` "One frame of police" steps 3.7 and 6; `../physics/damage.md` "Wrecked" |
| reverse beeper | gear index 0 | `../physics/vehicle-powertrain.md` "Gear slots" |
| air brake | speed ≤ 0.04 with front `BrakeCoef` > 0.5 | `../physics/vehicle.md` step 2; `../physics/vehicle-tune.md` |
| airborne flag | no wheel grounded + downward probe | `../physics/vehicle-wheels.md` (`grounded`); `../physics/collision.md` "Ground queries" |
| chase count | counted siren start / stop / explosion | this page; music spec reads it |

## For our engine

- Crash cases of MM2 (missing fields, lists without `ENDOFDATA`, an empty siren file, a light-bar car
  without police audio, siren steps pointing outside the list) get the safe readings given above
  and a log line; none occurs with shipped data.
- Keep the inert features out of the sound but keep their effects on state where they exist: the
  chase count rules (double count-down, leftover wind-down) change the music and must be kept.
- Reading the divisor format's unset pitch bounds as "no bounds" is our choice; MM2's result
  depends on leftover memory.

## Checked against

- The program: the vehicle-types reader (line positions, `ENDOFDATA`, exact-case list match, `TRUE`
  test, the order semi → police → nitro → plain), the owner kinds at every car set-up (player 2,
  network 0, all AI drivers 1) and which folder, 2D/3D, horn and clutch each gets; the cardata loader
  (line 2 fields, the two ignored integers, the engine header test `Volume Divisor`, rows to end of
  file, fallback to `default` when the engine part fails) and both row parsers with their slope
  arithmetic read from the x87 code; the volume window, pitch ramp, divisor volume, 0.25 stop test
  and both apply routines (2D and 3D); the common set-up constants (drop-offs 0/150, priority 9,
  625.0, 1.05, min amp speed 5.0, the speed field only ever written 0); the gear click test; the horn
  handler (press edge, light-bar toggle, no null check), container horn calls (police skipped);
  the siren reader (`Sample name` / `play time` modes, explosion volume never read), start, stop,
  explosion, wind-down (1.0 / 1.75 / 0.5 / 0.45 / 0.2; the siren update's play-if-not-playing
  after it, with no mode test, in the 3D and 2D paths), stepping (timer reset then + dt, int next
  index), the 3D floor 0.75 and explosion floor 0.85, the dead fade, the chase counter (reset in the
  police audio set-up, perp-kind test in the cop's start); the semi reader, beeper, air brake (0.04,
  shared latch, `BrakeCoef` field), the 2D volume slip; the nitro reader (folder from a flag still
  set at that moment) and its unreachable play calls; the airborne probe (3.0 m steps, 10 tries,
  ground mask, player only, cleared by any grounded wheel); echo parameters (0.05 / 0.96 / 0.997,
  manager delay, 0.96); engine silencing callers (Checkpoint and Blitz); the static doppler constant
  0.0176315 of cars and police.
- Data (`tools/audio/car_sounds.py`): `vehtypes.csv` lists as above; 51 car-format files in the two
  cardata folders (49 window format, 2 never-opened divisor files), 123 engine rows; flags values
  0/1/2/4/8; the count field differs from the rows in 30 files (e.g. `opponent/default.csv` 2 vs 1,
  `player/vpcop.csv` 2 vs 4); smallest minimum volume in an opened file 0.432 (`player/vpdb7.csv`
  `AUSTINMID`); every car of the 20-car list has its own player and opponent file (kinds: semi/bus
  `vpbus`, `vpcentury`, `vpddbus`; police `vpcop`, `vpsemi`; the rest plain); example `vpbug` player
  file at IdleRPM 750: `VWIDLE` V 0.8172 P 0.9732, `VWDRIVE` 0.5880 / 0.7239, `VWMID` 0.5714 /
  1.0893, `VWHIGH` 0.5500 / 0.6500 (`car_sounds.py engine vpbug`); no shipped sample stops at
  IdleRPM, OptRPM or MaxRPM of any car. Sirens: London 2 samples / 2 steps, SF 4 samples / 16 steps,
  all next indices valid, explosion `explosion` (3.167 s); `semidata.csv` `freightreverse`,
  `freightairblow`, 0.87, 0.9; front `BrakeCoef` 0.5 for `vpcentury`, `vpbus`, `vpddbus`;
  `default_nitro.csv` absent. 180 sample references (95 names) resolved against the archives; the
  only missing one is `horns\FIRETRUCKSIREN` (`vpsemi`'s horn; the file is in `Sirens/`; never
  played since police-listed cars never honk).
