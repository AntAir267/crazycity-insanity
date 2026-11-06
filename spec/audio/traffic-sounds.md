# Traffic, pedestrian and police sounds

What the ambient traffic, the pedestrians and the police make the sound system play: traffic
engine loops, traffic horn patterns, traffic driver shouts, pedestrian screams, and the police
siren start/stop calls with the chase count the music reads. The terms **sample**, **instance**,
**2D sample**, **3D source**, **3D slot**, **drop-offs**, **priority**, **attenuation**, **pan**,
**doppler**, **echo state** and **sound random** (`rnd_s(a)`, `rnd_s(lo, hi)`) are defined in
`sound-system.md`; this page only says what each source sets. The cop car's own siren, engine and
explosion sounds are `car-sounds.md`; impact sounds of traffic cars are `tyre-impact-sounds.md`;
trains and cable cars are `world-sounds.md`; music is the music milestone.

Arithmetic: float32, one rounding per operation, in the order written (`../physics/vehicle.md`,
"Precision"). *F* is the frame time (as in `../ai/traffic.md`). Speeds in m/s.

## The short version

- Traffic sound has **no traffic-side pool**: every traffic car within 100 m of the listener asks
  for one of the 4 shared 3D slots every frame (priority 8) and competes with every other 3D
  source; only a car holding a slot makes any sound. So at most 4 traffic cars sound at once,
  usually fewer.
- A sounding traffic car loops one engine sample whose pitch follows its rail speed through a
  speed table (a different "deceleration" line while it slows down), plus its horn patterns and
  its driver's shouts.
- Horns: on starting to avoid a player about 50 % chance of a random pattern other than the last;
  on each impact of a knocked car of at least the file's force (5500) 25 % chance of the last
  pattern (the "stuck horn": 3 s). A driver shout may follow a honk; an impact shout follows a
  crash at most once per 30 s (whole city).
- Pedestrians scream (2D, within 40 m) when they dive away from a player's car, start to run
  from it, or stop backing away from it; never on being hit, and they have no other sound.
- Cops: the siren starts when a cop enters FOLLOW and stops when the perp escapes or the cop is
  reset; a counter of sirens chasing the **local player** feeds the music. There are no officer
  voices and no AI horns.

## Files

All data files are read from the game archives by path (case-insensitive,
`../formats/ar.md`) with extension `.csv`. Sample names get a folder prefix here; the final file
name (`aud\aud22\<prefix>\<name>.22k.wav` by default) is `sound-system.md`'s.

### Which files

| Use | File tried first | Fallback | When chosen |
|---|---|---|---|
| traffic engine | `aud/cardata/ambient/<type>_engine` | `aud/cardata/ambient/default_engine` | per traffic car, at game set-up |
| traffic horn | `aud/cardata/ambient/<type>_horn` | `aud/cardata/ambient/default_horn` | per traffic car |
| traffic impact sounds | — | `aud/cardata/opponent/default_impacts` (always; `tyre-impact-sounds.md`) | per traffic car |
| driver voices | `aud/creaturedata/<type>_ambcarvoice<sfx>` | `aud/creaturedata/default_ambcarvoice<sfx><k>` | per traffic car |
| driver voice count | `aud/creaturedata/numambcarvoicefiles<sfx>` | — | once per game |
| male pedestrian voices | — | `aud/creaturedata/default_mpedvoice<m>` | per pedestrian |
| female pedestrian voices | — | `aud/creaturedata/default_fpedvoice<f>` | per pedestrian |
| pedestrian counts | `aud/creaturedata/nummalepedvoicefiles`, `numfemalepedvoicefiles` | — | once per game |

- `<type>` is the traffic car's type name from the car mix (`../ai/traffic-population.md`), e.g.
  `va_sedans_s`.
- `<sfx>` is `_l` if the city name is exactly `london` (byte for byte, lower case), else `_s`
  (every other city, mods included). The same test picks the siren file (`car-sounds.md`).
- `<k>`, `<m>`, `<f>` are whole numbers drawn once per game from the count files ("Count files").
  The pedestrian names have an empty suffix: `default_mpedvoice1`, not `default_mpedvoice_s1`.
- There is no third fallback: if the default engine, horn or impact file is missing, the car has
  no such sound (MM2 reads past its table then; **Engine:** no sound). A missing voice file
  leaves the car or pedestrian without voices.

### Loading and caching

1. **Once per game, at game set-up, before the streets are set up:** the city suffix is chosen,
   then the three count files are read in this order: driver voices, female pedestrians, male
   pedestrians (each with its own sound random draw, normally within the same second, so they
   share one *r*).
2. When the traffic pool is created (`../ai/traffic-population.md`), for each car in pool order:
   engine (type file, else default), horn (type file, else default), impact sounds, voices (type
   file, else default). When the pedestrian pool is created, for each pedestrian: its voice file.
3. Every file name is looked up in a per-game name cache first: a name already loaded reuses the
   loaded data; a name already found missing is not tried again (it goes straight to the
   fallback). The cache and all loaded data are dropped when the game ends.
4. Each loaded file is kept as **one copy per 3D slot** (4 copies): the copy belongs to the slot,
   not to a car. A car holding slot *j* uses copy *j* of each of its files, so the run-time state
   of a horn pattern or a voice reaction stays with the slot when cars come and go.

A file that opens is "found" even if it is empty or malformed (no fallback then).

### Reading lines and fields (all files on this page)

- A line is read up to its line feed (at most 999 characters). It is cut at the first CR or LF.
- Fields are split at commas, and **runs of commas count as one separator** (empty fields are
  skipped: `0.5,0.2,,` is two fields, `SFCOLLIDE3,0.98,1,` three). Numbers are read like C `atof`
  (leading number, else 0) and stored as float32 (nearest).
- "Skip" means the line is read and ignored whatever it holds. Names are compared without regard
  to case.
- MM2 crashes on a data line with fewer fields than the table below requires (and on a blank
  line in the row parts). **Engine:** ignore such a line.

### Engine file (`<type>_engine.csv`)

| Line | Content | Notes |
|---|---|---|
| 1 | skipped | header |
| 2 | sample name, volume | sample `engines\<name>` (a 3D-cache sample with pitch control); further fields ignored |
| 3 | skipped | header |
| 4 … end | min speed, max speed, min pitch, max pitch | one **row** per line, to the end of the file |

Every row gets a slope k = (max pitch − min pitch) × g with g = 1 / (max speed − min speed), or
g = 0 when max speed − min speed = 0. The **last row** is the deceleration row; the others are the
speed rows (in file order). A file with a single row has no speed rows.

Shipped (rows as min–max speed: min–max pitch; the last is the deceleration row):

| File | Sample, volume | Rows |
|---|---|---|
| `default_engine` | `enginesedan1`, 0.98 | 0–33: 0.317–1; 33–500: 1–1.25; dec 0–500: 0.317–13.977 |
| `va_bus_f`, `va_ddbus_l`, `va_diesels_s`, `va_garbagetruck`, `va_pickup_f` | `FREIGHTDRIVE`, 0.97 | 0–25: 0.5–1; 25–500: 1–1.1; dec 0–500: 0.5–10.5 |
| `va_sedan_s` | `ENGINEFERRARI1`, 0.97 | 0–15: 0.27–1; 15–35: 1–1.25; 35–500: 1.25–1.5; dec 0–500: 0.27–24.603 |
| `va_smallsuv_s` | `ENGINEFERRARI1`, 0.98 | as `va_sedan_s` |

### Horn file (`<type>_horn.csv`)

| Line | Content |
|---|---|
| 1 | skipped (header) |
| 2 | sample name, volume, pitch, minimum impact force (sample `horns\<name>`, with pitch control) |
| 3 … end | a line whose first field is `horn play duration` starts a new **pattern**; any other line is a **segment** of the latest pattern: play time, pause time (seconds) |

Patterns are numbered 0, 1, … in file order; *c* = the number of the last pattern (pattern count
− 1). A segment line before the first `horn play duration` line crashes MM2 (**Engine:** ignore
it); a file without patterns gives a horn that never sounds.

Shipped (minimum force 5500 in every file; patterns as play/pause pairs; A = 0.5/0.2, 1.5/0;
B = 0.25/0.1, 0.15/0.1, 0.25/0.07, 1/0; S = 0/0.15, 3/0, always the last pattern):

| File | Sample, volume, pitch | Patterns |
|---|---|---|
| `default_horn` | `CARHORNAMB1`, 0.97, 1 | A, B, S |
| `va_sedan_s` | `HORNLOOP1`, 0.97, 1.15 | A, B, S |
| `va_smallsuv_s` | `CARHORNAMB1`, 0.97, 1.1 | A, B, S |
| `va_compact_s` | `HORNLOOP1`, 0.97, 1 | 0.25/0.2, 1.5/0; B; S |
| `va_pickup_f` | `AMBTRUCKHORN`, 0.95, 1 | A; 0.25/0.1, 0.15/0.1, 1/0; S |
| `va_diesels_s` | `DIESELHORN`, 0.94, 1 | A; B; 0.25/0.1, 0.15/0.1, 1/0; S |
| `va_bus_f`, `va_ddbus_l` | `BUSHORN`, 0.97, 0.9 | 0.5/0.2, 1/0; B; 1.5/0; S |
| `va_garbagetruck` | `BUSHORN`, 0.97, 0.95 | as `va_bus_f` |

### Voice file (`*_ambcarvoice*.csv`, `*_pedvoice*.csv`)

A sequence of blocks. The first line's first field must be `min speed` or `min impact force`,
otherwise the file loads with no blocks (no voices). Each block:

| Block | Lines |
|---|---|
| speed band (header `min speed`) | values line: min speed, max speed, *A* (min time in range), *B* (max time out of range); one skipped line; then sample lines: name, volume |
| impact (header `min impact force`) | values line: minimum impact force; one skipped line; then sample lines: name, volume, delay (s) |

Sample lines run until a line whose first field is `min speed` or `min impact force` (the next
block's header) or the end of the file. Samples are `Creature3D\<name>`, loaded as 2D samples
(no pitch control). Any number of speed bands; a second impact block replaces the first. A
band's sample count is its *n*.

Shipped default driver files (5 `_s`, 3 `_l`): band 1 = 0–20 m/s, *A* 5, *B* 0, three
`SFDRIVER…`/`LDRIVER…` lines; band 2 = 50–999999, *A* 0.25, *B* 5, three lines; impact 5500, three
`SFCOLLIDE…`/`LCOLLIDE…` lines with delay 1; all volumes 0.98. `default_ambcarvoice_s5` uses the
`V…` lines `VWHATYOUDOING`, `VLEARNTODRIVELOSER`. Bus files (`va_bus_f_ambcarvoice_l`/`_s`,
`va_ddbus_l_ambcarvoice_l`): band 1 0–20, *A* 20, *B* 0; band 2 35–999999, *A* 0.25, *B* 5;
impact 5500 with the single sample `vbusscream`, delay 0.5. Pedestrian files: one band 0–500,
*A* 0.01, *B* 0, three `MALESCREAM…`/`FEMALESCREAM…` lines, volume 0.98, no impact block.

### Count files (`numambcarvoicefiles<sfx>`, `numfemalepedvoicefiles`, `nummalepedvoicefiles`)

Line 1 skipped; the first field of line 2 is *n* (read as a number). The file number is
*k* = trunc(`rnd_s`(1.0, *n* + 0.25)): 1 … *n*, each of 1 … *n* − 1 with chance 1/(*n* − 0.75) and
*n* with 0.25/(*n* − 0.75) (and 0, which names no file, only for *r* below 10⁻⁷ or less,
because 1 × 0.01 rounds below 0.01). A missing count file keeps the previous game's number (0 at
program start, which names no file). Shipped: `_s` 5, `_l` 3, female 3, **male 1** (so only
`default_mpedvoice1` is ever used; `default_mpedvoice2`/`3` are never loaded).

## Traffic cars

### The 3D source

Each traffic car has one 3D source, set up when the pool is created:

| Property | Value |
|---|---|
| position | the car's matrix position (model origin; for a knocked car its body pose), followed every frame |
| drop-offs | min 0, max 100 m |
| priority | 8 (constant) |
| doppler constant | 0.035263006 (float32 of 1 / 28.358332; twice the player-car value) |
| samples | engine loop, horn sample, impact samples, voice samples (from the car's files, the copy of its slot) |

When a car is placed on the streets (`../ai/traffic-population.md`, each placement) its source is
reset: it gives back any slot it holds, its distance history restarts (`sound-system.md`) and its
**speed** and **previous speed** are set to 0.

### Which traffic cars sound

There is no list or handler choosing traffic cars. In each traffic car update, step 2 ("sound
position", `../ai/traffic.md`, "One traffic car update"), in every state:

1. The source's position = the car's matrix position.
2. **speed** = the car's rail speed *v* (`../ai/traffic-driving.md`; 0 while knocked, wrecked or
   out after a crash, `../ai/traffic-reactions.md`).
3. If the source holds no slot, it asks the 3D manager for one (`sound-system.md`, "Getting and
   losing a slot": only while inside its 100 m drop-off; a free slot first, else it may evict a
   lower-priority source or an equal-priority one farther from the listener).

Every source also gives its slot back in its slot update once it is past 100 m. With 4 slots
shared by all 3D sources (other cars, `car-sounds.md`; ambient objects, `world-sounds.md`;
pedestrians, below), normally the nearest few traffic cars sound. A cop with its siren on
(priority 10) always beats traffic; a cop with its siren off (7) loses to it.

A car taken off the streets (back to the free list) or out of every lane list is no longer
updated: it keeps a slot it holds, still sounding at its last speed, until it is past 100 m or is
placed again.

### Slot update (each frame, for a car holding a slot)

Run by the 3D manager's per-frame update of the slot holders (`sound-system.md`, "Getting and
losing a slot" and "Order within a frame" step 3):

1. **Echo:** when the echo state turned on since the last update, the engine, horn and voice
   samples switch their echo copies on with *T* = the manager's echo delay (0.5 s), *G* = 0.96
   and *F* left at its default; when it turned off, off (`sound-system.md`, "Echo (tunnels)").
   While the echo is on, this step also **ticks** the echo of those samples (engine, horn, every
   band and impact voice sample of the car), once per slot update; nothing else ticks them. The
   impact samples are `tyre-impact-sounds.md`'s.
2. If the source is past its max drop-off: give the slot back (below) and stop here.
3. *a* = the attenuation *A*, *p* = pan (0 in mono), *d* = the doppler factor with the constant
   above (`sound-system.md`, "Attenuation, pan and doppler"); *D*² = the squared distance to the
   listener (d²).
4. **Deceleration test:** if (previous speed − speed) > 4.0: *s* = previous speed × 0.95 (stored
   as the new previous speed), *decay* = yes; otherwise *s* = speed, *decay* = no.
5. **Horn:** if a pattern is running: horn volume = *a* × file volume, pitch = *d* × file pitch,
   pan *p*.
6. **Engine:** volume = *a* × file volume, pitch = *d* × *P* where *P* is the engine pitch from
   the **previous** slot update of this copy (0 if never computed), pan *p*; start the loop if it
   is not playing.
7. **Engine pitch** *P* for the next update ("Engine pitch" below, with *s*, the previous speed
   and *decay*).
8. **Voices:** store *a*, *p*, *D*² for the car's voice reactions; every voice sample of the car
   that is playing gets volume = its file volume × *a* and pan *p* (so it follows the car).
9. **Impact sounds:** hand *a* and *p* to the car's impact sounds (`tyre-impact-sounds.md`).
10. If *decay* = no: previous speed = speed.
11. **Horn pattern** update ("Horn patterns").

The previous speed changes only in this update, so a car that held no slot for a while keeps the
value from its last slot update (0 after placement).

### Losing and getting a slot

On getting slot *j*, the car's sounds bind to copy *j* of its files (nothing else changes: the
copy keeps its last pitch, current horn pattern and segment). On losing its slot (past 100 m,
evicted, or reset):

- echo copies off; the engine loop and the horn sample stop;
- the current horn pattern is marked idle, **keeping its segment number and timer** (so the next
  time that pattern starts on that copy, it resumes at that segment, from time 0);
- every pending voice reaction of the copy is cancelled (all reactions ready again).

### Engine pitch

Inputs *s*, previous speed *s*ₚ (after step 4), *decay*:

- If *s* < *s*ₚ or *decay*: the deceleration row L: *P* = (*s* × k_L) + min pitch_L (its min speed
  is **not** subtracted).
- Otherwise: the first speed row *i* (file order, last row excluded) with min speed_i ≤ *s* ≤ max
  speed_i: *P* = ((*s* − min speed_i) × k_i) + min pitch_i. No such row: *P* is unchanged.

The engine volume does not depend on speed. A standing car (*s* = 0, not decelerating) uses the
first row whose range contains 0, i.e. the idle pitch is row 0's min pitch (0.317 for the
default file, 0.5 freight, 0.27 sedan). Examples (default file): 15 m/s 0.6275 while not slowing,
0.7268 while slowing; 33 m/s 1.0 / 1.2186. The deceleration rows reach beyond 2 at high speed
(`va_smallsuv_s` slowing at 40 m/s: 2.2166); the mixer limits the pitch factor
(`sound-system.md`).

A drop of more than 4 m/s between two slot updates (a knocked car's speed going to 0, a car
placed again) is not followed at once: the deceleration row is evaluated at a speed that shrinks
by 5 % per frame until it is within 4 m/s of the real speed, so the pitch glides down.

**Knocked, wrecked and out cars** keep their engine loop at the idle pitch (after the glide) for
as long as they hold a slot, wherever their body goes; nothing stops a wreck's engine. Their horn
and voices react to impacts ("Honk on impact", "Impact reaction").

### Horn patterns

Each horn copy has a list of patterns and a **current pattern** (0 at load). A pattern has a
segment number (0 at load), a timer *t* and a state: **idle** (at load), **playing** or
**pausing**. Only the current pattern is updated, once per slot update (step 11):

- **playing** segment *g*: *t* = *F* + *t*; if *t* ≥ play time_g: stop the sample, *t* = 0,
  **pausing**; otherwise start the sample looping if it is not playing.
- **pausing** after segment *g*: *t* = *F* + *t*; if *t* ≥ pause time_g: if *g* + 1 < segment
  count: *g* = *g* + 1, **playing**, start the loop; otherwise **idle**, *g* = 0, stop the sample;
  in both cases *t* = 0. While *t* < pause time_g: stop the sample if it is playing.
- **idle**: stop the sample if it is playing.

**Start** a pattern: only if it is idle: start the sample looping at once, *t* = 0, **playing**
(the segment number is kept: 0 unless a slot loss cut the pattern). **Stop** a pattern: *g* = 0,
**idle**, stop the sample. A segment with play time 0 sounds only from its start to the next
update.

Shipped timing: the stuck-horn pattern 0/0.15, 3/0 is 0.15 s of silence, then 3 s of horn. At
*F* = float32(1/60) (counting the slot update of the start frame) the default patterns take
30 + 12, 91 + 1 frames (pattern 0), 15 + 6, 9 + 6, 15 + 5, 61 + 1 (pattern 1) and 1 + 9, 181 + 1
(stuck horn) as sounding + pausing frames per segment: a pause of 0 still lasts one frame, and
float sums make 1.5 s and 3 s one frame longer.

### Honk on avoiding

Event: a traffic car **starts** avoiding a player (`../ai/traffic-reactions.md`, "Avoiding a
player", "Start", step 1). Only if the car holds a slot (its horn is bound) and its current pattern
is idle:

1. *h* = trunc(`rnd_s`(2*c* − 0.01)) (2*c* as a float, minus float32 0.01).
2. If *h* ≥ *c*: no honk (and no voice). Otherwise start pattern *h*, current pattern = *h*, and
   the **avoidance reaction** of the driver follows (below).

So the chance is *c* / (2*c* − 0.01): 50.13 % with 3 patterns, 50.08 % with 4; the last pattern is
never chosen; a one-pattern horn never honks here. (This confirms `../ai/traffic-reactions.md`,
"Horns and voices".)

### Honk on impact (stuck horn)

Event: every impact on a knocked traffic car's body (`../ai/traffic-reactions.md`, "What the
car-to-car physics needs", last bullet) with *L* = (|J.z| + |J.y|) + |J.x| of the impulse, if the
car holds a slot. Order per impact: the impact sound (`tyre-impact-sounds.md`), then this, then the
**impact reaction** of the driver.

1. Nothing unless *L* ≥ the horn file's minimum impact force (5500 shipped) and
   `rnd_s`(10.0) ≥ 7.5 (25 %).
2. If the current pattern is not idle: if it is the last pattern *c*, nothing more (already
   honking); otherwise stop it.
3. Start pattern *c*; current pattern = *c*.

The stuck horn can cut an avoidance honk; an avoidance honk never cuts anything.

## Driver voices

### Reaction state

Each voice copy has its speed bands and its impact block. A **band** keeps: time in range
*t*ᵢₙ, time out of range *t*ₒᵤₜ (both 0 at load), a chosen variant, a wait timer, and a
**ready/armed** flag (ready at load). The impact block keeps: chosen sample, timer, ready/armed
(ready). Three values are shared by every car and pedestrian of the game: the last chosen band
variant (0 at program start), the last chosen impact sample (0 at program start) and the **impact
gate time** (0 at program start; never reset by a new game).

### Per-frame update (start of the AI update)

At the very start of each AI update (`../ai/traffic.md`, "Frame order", before step 1.1), if there
is at least one player, with *s* = the **first player's** car speed (`../physics/vehicle.md`,
step 2): impact gate time = *F* + gate time; then for every loaded driver voice file and every
slot copy (also copies no car holds):

- **Bands:** if min ≤ *s* ≤ max: *t*ᵢₙ = *F* + *t*ᵢₙ, *t*ₒᵤₜ = 0; else *t*ₒᵤₜ = *F* + *t*ₒᵤₜ,
  *t*ᵢₙ = 0. If armed: wait = *F* + wait; if wait ≥ 5.0: ready again (the shout is dropped);
  otherwise, if the stored *D*² ≤ 2500.0 (within 50 m): **play** the chosen variant.
- **Impact block:** if armed: timer = *F* + timer; if timer ≥ the chosen sample's delay: **play**
  it, and timer = 0.

The pedestrian voice files get the same update right after (and add *F* to the gate time
**again**: the gate time grows by 2*F* per AI frame).

**Play** (band variant or impact sample): if that sample of the copy is already playing,
nothing (it stays armed: a band retries until its 5 s run out, an impact block after another
delay). Otherwise volume = *a* × its file volume, pan = *p* (the values of the last slot update),
play it once as a 2D sample (no pitch, no doppler); the band or block is ready again. Playing an
impact sample also sets the impact gate time to 0 and the last chosen impact sample.

### Avoidance reaction

Right after an avoidance honk (only if the horn started a pattern) and if the car holds a slot:
for each band of the car's file that **triggers**, i.e. *t*ᵢₙ ≥ *A* or *t*ₒᵤₜ ≤ *B*:

1. Only if the band is ready: *v* = trunc(`rnd_s`(2*n*)) (*n* = the band's sample count).
2. If *v* < *n*: if *v* equals the shared last variant, *v* = *v* + 1, and 0 if that is *n*;
   chosen variant = *v*, shared last variant = *v*, wait = 0, **armed**. Otherwise (about 50 %)
   nothing.

Because a band's in-range frame sets *t*ₒᵤₜ = 0, any band with *B* ≥ 0 triggers while the first
player's speed is inside it, and for *B* seconds after it leaves; *A* matters only with *B* < 0.
With the shipped files: shouts at a player driving 0–20 m/s, or at 50 m/s or more (and within 5 s
after dropping below 50); nothing between 20 and 50 m/s (buses: 0–20 and from 35). Several bands
can arm at once. The shout plays at the next AI update within 50 m (else within 5 s, else never).

### Impact reaction

Right after the stuck-horn test of each impact (above), if the car holds a slot and its file has
an impact block:

1. Only if the block is ready, the impact gate time ≥ 60.0 and *L* ≥ the block's minimum impact
   force (5500): timer = 0; *i* = trunc(`rnd_s`(*n* − 0.01)) (*n* = its sample count, as a float);
   if *i* equals the shared last impact sample, *i* + 1, and 0 if that is *n*; chosen = *i*,
   shared last = *i*, **armed**.
2. The sample plays after its delay (1 s shipped, 0.5 s for buses) through the per-frame update.

The gate is checked when arming, so in practice at most one crash shout per 30 s of play in the
whole city (none in the first 30 s after the program starts); two cars armed in the same window
both shout. There is no distance limit for impact shouts besides the slot.

**Engine:** copies other than the first of an impact block start **ready** (MM2 leaves that flag
unset in its copies).

## Pedestrian voices

### File and source

A pedestrian is **female** if its model name (`[GoodWeatherPedName / BadWeatherPedName]`,
milestone 6) contains `FEMALE`, `WOMAN`, `GIRL` or `HOOKER`, letters compared without case;
otherwise male. MM2's substring test restarts its match at the next character after a mismatch
without re-testing that character, so a name like `ffemale` or `wwoman` does not match.
Shipped: `pedmodel_man`, `pedmodel_manw` male; `pedmodel_woman`, `pedmodel_womanw` female.
File: `default_fpedvoice<f>` or `default_mpedvoice<m>` ("Files").

Each pedestrian has a 3D source: position = the pedestrian's position, drop-offs 0 and 40 m,
priority 8, doppler constant as traffic (unused: voices have no pitch). Pedestrian voice copies
work like the driver ones (bands, the per-frame update above, the shared last variant), with
these differences:

- A pedestrian holds a slot only to scream: at the start of each pedestrian's update, if it holds
  a slot and none of its voice samples is playing, it gives the slot back. Its slot update: echo
  on/off and ticks for its voice samples as traffic step 1; past 40 m it gives the slot back;
  otherwise *a*, *p*, *D*² are stored as for traffic (nothing else).
- **Scream event:** if the pedestrian has a voice file: it asks for a slot (only within 40 m; it
  may evict like any source); if it holds one, every triggering band runs the avoidance-reaction
  steps (50 % to arm a variant). The shipped band (0–500 m/s, *B* 0) always triggers.
- **Play** additionally needs the pedestrian to hold a slot (it asks for one again if not; no
  slot: not played, stays armed). So a scream plays at the next AI update, volume *a* × 0.98, pan
  *p*.

### Events

The pedestrian AI is not specified yet (`../ROADMAP.md`, milestone 6); MM2 raises the scream
event when a pedestrian, reacting to a newly threatening player car:

1. starts a **dive** to one side (left or right, chosen from the player car's sideways slip and
   its side of the car; MM2 has this branch twice, for two walking situations, both scream);
2. finds its escape way blocked at least 1.25 m away and starts to **run** (closer than 1.25 m it
   starts **backing up** instead, silently);
3. ends **backing up** and walks again (when its walking target changes).

No pedestrian sound plays on being hit, and pedestrians have no footsteps or other sounds. The
files `aud/ambient/maleped1`, `maleped2`, `femaleped1` are not pedestrian sounds (no city
ambience list names them; `world-sounds.md`).

## Police

### Siren calls

Events (all `../ai/police.md`); the siren's sound itself is `car-sounds.md`:

| Event | Call |
|---|---|
| a cop's state becomes FOLLOW ("Following or apprehending", on entry; from IDLE or APPREHEND) | **siren start** with *player* = (the perp's car is the local player's car) |
| the perp escapes without explosion (too far; deep water, also for an idle cop) | **siren stop** (MM2 first sets a fade-out flag, but the stop clears it at once: the siren stops immediately) |
| the perp escapes with explosion (wrecked; "One frame of police" step 6) | **explosion**, then **siren stop** |
| the cop is reset (world reset, fell below y = −200; "Parking (reset)" step 2) | **siren stop** |

- **Siren start** (only if the cop's siren is off): siren on, counting = *player*; if counting, the
  chase count + 1; the cop's source priority becomes 10 and it keeps its slot even past its max
  drop-off (silent there). Re-entering FOLLOW from APPREHEND while the siren is on does nothing.
- **Siren stop:** if counting and the chase count > 0: chase count − 1; siren off, priority 7, the
  siren sample stops.
- **Explosion:** plays the cop car's explosion sample once if the cop holds a slot and it is not
  already playing (`car-sounds.md`); if counting and the count > 0: chase count − 1; if the cop
  holds a slot the siren stays marked as counting (wind-down, `car-sounds.md`), else the siren is
  marked off.

Because a slot-holding wrecked cop is still marked counting when the siren stop follows, its wreck
takes **2** off the chase count (never below 0); without a slot, 1. And since step 6 of the cop
frame repeats for a wrecked cop that is already OUT, the explosion call runs **every frame**: the
explosion sample starts again whenever it has ended while the wreck holds a slot (priority 7, so
nearby traffic soon takes the slot), and the siren stop runs every frame (no further count
change).

### The chase count (music)

One counter per game, set to 0 whenever a police car's sound set is created (game set-up), read
by the music every frame (`../ROADMAP.md`, milestone 8: chase music on 0 → 1, back on 1 → 0). It
counts cops whose siren started while chasing the **local player's** car: cops chasing an
opponent (race modes) or a network player never count. With two counted cops, a wreck can take
the count from 2 straight to 0, so the music sees no 1 → 0 change; the remaining cop's later stop
cannot go below 0.

### No voices, no AI horns

There are no officer voices (the "lost him" flag of `../ai/police.md` is the siren fade flag
above), no arrest sound (`arrest.22k.wav` is never named), and neither cops nor race opponents
ever honk: only the player's horn control and network players' horns play car horns
(`car-sounds.md`). Traffic does not react to sirens.

## Random draws

All draws on this page are sound random (`sound-system.md`): never the traffic or police
generators. In the order a frame can make them:

| Where | Draw | Used |
|---|---|---|
| game set-up | `rnd_s`(1.0, *n* + 0.25) ×3 (driver, female, male counts) | file numbers |
| honk on avoiding | `rnd_s`(2*c* − 0.01) | pattern or no honk |
| avoidance reaction, per triggering ready band | `rnd_s`(2*n*) | variant or none |
| scream event, per triggering ready band | `rnd_s`(2*n*) | variant or none |
| honk on impact, per impact with *L* ≥ minimum | `rnd_s`(10.0) | 25 % |
| impact reaction, per impact passing the gate | `rnd_s`(*n* − 0.01) | sample |

Since all draws within one wall-clock second are equal, draws of one event are tied together: a
car that honks (*r* < ≈ 0.5) nearly always also arms a shout (the band draw succeeds for
*r* < 0.5), its variant follows *r*; a stuck horn (*r* ≥ 0.75) comes with the last impact shout
before the "not the same as last" bump; the three count draws normally pick matching numbers.

## Events this page needs

| Sound | Event | Producer |
|---|---|---|
| traffic engine | every traffic car update, any state: position, rail speed | `../ai/traffic.md` "One traffic car update" step 2; `../ai/traffic-driving.md`; `../ai/traffic-reactions.md` (speed 0 when knocked) |
| traffic source reset | a car placed on the streets | `../ai/traffic-population.md` (placing) |
| honk on avoiding, avoidance shout | state 4 start | `../ai/traffic-reactions.md` "Avoiding a player", "Start" step 1 |
| stuck horn, impact sound, impact shout | each impact on a knocked car, impulse J | `../ai/traffic-reactions.md` "What the car-to-car physics needs" (car-to-car physics spec) |
| voice band timers | first player's car speed, each AI update | `../physics/vehicle.md` step 2 |
| pedestrian screams | dive start, run start, back-up end; model name | **not exposed yet** (pedestrian AI, milestone 6) |
| siren start / stop / explosion | FOLLOW on entry; perp escapes (with/without explosion); cop reset | `../ai/police.md` "Following or apprehending", "The perp escapes", "One frame of police" steps 3.6, 3.7, 4.3, 6, "Parking (reset)" |
| chase count | the perp is the local player | `../ai/police.md` (perp), player list |
| echo | echo state | `sound-system.md` |

Not exposed yet: the pedestrian events above; "a traffic car leaves the free list / every lane
list" is implicit in `../ai/traffic-population.md` (the car is just no longer updated).

## Interfaces with sound-system.md

- 3D sources with position, drop-offs (min, max), priority, doppler constant; slot request
  (only within the max drop-off), eviction, slot release past the max drop-off; "keep the slot
  past the max drop-off" switch (cop siren); per-frame attenuation, pan, doppler factor and
  squared listener distance; slot-bound sample copies (4); 2D samples without pitch control.
- The order in the frame: AI update (voice timers at its start, honk and scream events) → physics
  (impact events) → the sound update (slot updates above).
- Echo copies (*T* = manager delay, *G* = 0.96, default *F*) for the engine, horn and voice
  samples of traffic; pedestrian voices get the same (*T*, *G*); `rnd_s`.

## Checked against

Static analysis of `midtown2.exe` (retail English, Nov 3 2000) and the shipped `mm2aud` data with
`tools/audio/traffic_sounds.py` (output `work/check/audio/traffic_sounds.txt`):

- Constants: drop-offs 100.0 / 40.0, priorities 8 (traffic, pedestrians), 10 / 7 (cop siren on /
  off), deceleration test 4.0, decay 0.949999988, doppler 1 / 28.358332 = 0.035263006, honk
  2*c* − 0.01 (float32 0.01), stuck horn 10.0 / 7.5, band wait 5.0, voice distance 2500.0, impact
  gate 60.0 (advanced twice per AI frame), counts *n* + 0.25, echo factor 0.96.
- `traffic_sounds.py files`: 8 engine files, 9 horn files, 17 voice files and 4 count files parse
  with the values in the tables above (no malformed lines); their 3 engine samples, 5 horn samples
  and 71 voice samples (55 driver, 16 pedestrian) all exist under `aud/aud22/`
  (case-insensitively; `tools/audio/sounds.py` resolves the `sample_refs()`: 0 missing).
- `traffic_sounds.py types`: 209 `.aimap` files, 5 with a car mix (`city/sf`, `city/london`,
  London `roam`, `roam.aimap_p`, `roambak`): 19 traffic types. Own engine file `va_bus_f`,
  `va_ddbus_l`, `va_diesels_s`, `va_smallsuv_s`; own horn those and `va_compact_s`; own voices
  `va_bus_f` (SF, `_s`) and `va_ddbus_l` (London); every other type uses the defaults. Never
  loaded by a shipped mix: `va_sedan_s_engine`/`_horn` (the sedan type is `va_sedans_s`),
  `va_pickup_f_*`, `va_garbagetruck_*`, `va_bus_f_ambcarvoice_l` (`va_bus_f` only drives in SF).
  The four pedestrian models: `pedmodel_man`/`manw` male, `pedmodel_woman`/`womanw` female.
- A knocked car from 15 m/s glides for 26 frames (default file 0.7063 → 0.4250) before its idle
  pitch 0.317.
- `traffic_sounds.py pitch`: the engine pitch tables per file (0–50 m/s, steady and slowing),
  e.g. default 0 m/s 0.317, 15 m/s 0.6275 / 0.7268, 33 m/s 1.0 / 1.2186; the honk chance per horn
  file (50.13 % / 50.08 %); the count-file distributions (`_s` 1–4: 23.5 % each, 5: 5.9 %;
  `_l` and female 1–2: 44.4 %, 3: 11.1 %; male always 1).
