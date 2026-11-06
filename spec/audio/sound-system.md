# Sound system: files, samples, mixing, 3D, echo

This page is the layer every MM2 sound goes through: how a sound file is found and decoded, what a
sample can do, how volumes, pitches and pans turn into what the player hears, the voice limits,
how 3D sources get positioned (and the very small number of them that can sound at once), the
tunnel echo, the player's sound options and the random numbers sounds use. The pages that use it:

| Page | What makes the sounds |
|---|---|
| `car-sounds.md` | the car audio of the player, opponents and police: engine, gears, horn, siren, semi and nitro extras |
| `tyre-impact-sounds.md` | rolling and skid sounds per surface, tyre wobble, suspension thumps, impacts of every kind |
| `traffic-sounds.md` | ambient traffic engines and horns, driver and pedestrian voices, police voices |
| `world-sounds.md` | city ambience, ferries, drawbridges, trains, cable cars, rain and thunder |
| `speech.md` | race announcers, the Crash Course instructor, game and menu one-shots |
| `events.md` | which game event plays which sound, with the page that produces each event |
| `music.md` | the DirectMusic soundtrack rules (another writer's; formats in `../formats/directmusic.md`) |

Music (DirectMusic) is a separate system; where it touches sound effects this page says so.

All numbers below are exact values from the game. "Volume", "pitch" and "pan" are always the
values described in "Mixer"; never apply a curve of your own on top.

## Words used on the sound pages

- **sample**: one sound file loaded under one name, with a set number of **instances** (copies
  that can play at the same time; 1 unless a page says otherwise). It has a volume, a pitch factor
  and a pan, and can be played once, played looped, stopped and asked whether it is playing.
- **2D sample**: plays without a position; its owner sets volume, pitch and pan directly.
- **3D source**: an object with a position that owns some samples and asks the 3D manager for a
  slot ("3D sources" below); only while it holds a slot are its samples heard.
- **sound random**: the random draw of "Sound random numbers" below, written rnd_s(a) or
  rnd_s(lo, hi).
- **echo state**: on while the player's car is in an enclosed room ("Echo").

## Finding a sound file

A sample is loaded by a name built by its owner, usually a folder prefix and the name read from a
data file (e.g. `engines\` + `RACECARIDLE`). The full path inside the archives (`../formats/ar.md`,
names matched without regard to case, `\` and `/` alike) is:

1. Take the sample's **sub-path** and **extension**. Every sample uses sub-path `aud22` and
   extension `.22k` unless its owner set others (speech uses `aud11` and `.11K`, plus the Crash
   Course folders; `speech.md`).
2. If the name already ends in a dot and three characters (the fourth character from the end is
   `.`), the path is `aud\<sub-path>\<name>`; otherwise `aud\<sub-path>\<name><extension>`.
3. Then, unless that path ends in `.wav` or `.adpcm`, `.wav` is appended.

So `engines\RACECARIDLE` is `aud/aud22/engines/RACECARIDLE.22k.wav`. There is **no fallback**: a
sample whose file is missing (or fails to decode) stays silent and every action on it does
nothing; MM2 never tries the other sample folder or another rate. Archive order and mods follow
`../formats/ar.md`, so a mod replaces a sound by shipping the same path.

Shipped folders under `aud/aud22/`: the root (game and menu one-shots, car-select clips),
`engines/`, `horns/`, `Sirens/`, `surfaces/`, `suspension/`, `impacts/`, `Creature3D/` (voices),
`Amb3D/` (ambience). Speech: `aud/aud11/<voice>/`. The 226 files directly in `aud/aud11/` of
`mm2audex.ar` (11 kHz copies of most `aud22` sounds) are never read: MM2 has no low-quality
switch. `tools/audio/sounds.py` lists every name the game
can build and the file each resolves to.

## WAV files

A sound file is a RIFF `WAVE` file. MM2 reads it as follows:

1. The first 12 bytes must be `RIFF`, a size, `WAVE`; otherwise the load fails.
2. Walk the chunks from offset 12 (each: 4-byte id, u32 size, data padded to an even length).
   Accept `fmt ` (its size must be at least 14), `data` and `fact`; **any other chunk met before
   both `fmt ` and `data` have been seen makes the load fail**. Stop walking as soon as both have
   been seen, so chunks after them (`LIST`, `smpl`, `cue `, …) are never looked at.
3. From `fmt ` read the 18-byte wave format (format tag, channels, rate, byte rate, block align,
   bits per sample, extra size). Format tag 1 (PCM) is played as it is, 8 or 16 bits, mono or
   stereo, at the file's own rate. (Tag 0x6969 is an old 4:1 compressed format of the engine,
   decoded to 16-bit PCM; no shipped file uses it and support is optional.)
4. The `data` chunk is the sound.

Shipped data (`sounds.py wav`): 2,503 files, all PCM 16-bit; mono 11,025 Hz 2,112, mono
22,050 Hz 375, mono 48,000 Hz 13, mono 44,100 Hz 1, stereo 11,025 Hz 1, stereo 22,050 Hz 1.
The rate in the file name (`.22k`, `.11k`) is only a name: four `.22k` files are 11,025 Hz and
13 files are 48 kHz; each plays at its own rate. 707 files have chunks after `data` (ignored);
none has a foreign chunk before it.

## Samples

- **Pitch control.** Each sample is made either **with** or **without** pitch control (the owning
  page says which). A sample without pitch control always plays at its file's rate: pitch changes
  are accepted and remembered but have no audible effect. All samples have volume and pan.
- **Instances.** A sample made with *n* instances can play up to *n* sounds at once; the owner
  picks which instance an action applies to (the pages say how). Each instance may hold a copy of
  the same file or a file of its own: the race modes, for example, keep their one-shots as one
  sample whose instances are different files (`speech.md`). Volume set while an instance is
  current applies to that instance; the priority applies to the whole sample.
- **Play once / play looped** take an optional volume and pitch, set first when given. Playing an
  instance that is already playing: in a one-instance sample it is stopped and starts again from
  the beginning; in a sample with several instances nothing happens (it carries on).
- **Stop** stops the instance and rewinds it to the beginning.
- **Is playing** is true from the play until the end of a once-played sound, or until stopped.
  MM2 asks the sound output at that moment (the status of the instance's voice in the operating
  system's mixer), so the end of a sound follows the output's own playback in real time, at the
  playback rate of each moment, not the game's frame times. **Engine:** our mixer may answer from
  its own play position instead (file frames at the playback rate, advanced by each frame's time),
  so that native and browser runs agree; the output then follows the mixer's commands.
- **Priority**: a number 0…65535 per sample (default 0), used only by "Voice limits". The race
  one-shots `Waypoint`, `Lastwaypoint`, `Messagenote` and `Carhorn1double` use 23; every other
  sample keeps 0 unless its page says otherwise.

## Mixer

### Volume

A sample's volume *v* is a number 0…1 that is **linear in decibels**:

    level (dB) = (v_played − 1) × 100,      v_played = clamp(g × v, 0, 1)

where *g* is the sound-effects gain from the options (below). So v = 1 is full level, 0.9 is
−10 dB, 0.5 is −50 dB and 0 is −100 dB (silent). (MM2 hands DirectSound the attenuation
trunc((v_played − 1) × 10000) in hundredths of a decibel.) Owners multiply volumes freely (for
example a 3D attenuation times a sample volume); the product is then mapped this way, so a factor
of 0.5 takes 50 dB off, not 6 dB.

The **sound-effects gain** *g* comes from the "SFX volume" option *s* (0…1):

    g = log(200 × s) / log(200)    (s > 0),      g = 0  (s = 0)

(s = 1 gives 1, s = 0.5 gives 0.8692, s = 0.1 gives 0.5654; below s = 0.005 g is negative and
everything is silent.) Changing *s* re-applies every sample's volume at once. It applies to every
sound on these pages, speech included; music has its own volume (`music.md`).

### Pitch

A pitch factor *f* plays the sample at `rate × clamp(f, 0, 2)` samples per second, where `rate` is
the file's own rate, and that playback rate is then limited to 100…100,000 Hz (whole hertz,
truncated). f = 1 is the file as recorded. Doppler, engine RPM and the like all feed this factor.

### Pan

A pan *p* is clamped to −1…1. Positive moves the sound right by attenuating the **left** channel
by p × 100 dB; negative attenuates the right channel by |p| × 100 dB; the other channel stays at
full level. Pan is only applied when the **stereo** option is on; in mono no pan is ever set and
every sound is centred.

### Balance

The "Balance" option *b* (−1…1) is a global left/right setting applied to the whole sound output
(MM2 sets the operating system's wave-out mixer line, so it scales linear amplitude, not decibels):
with *m* the output level, b > 0 gives left = (1 − b) × m, right = m; b < 0 gives left = m,
right = (1 + b) × m; b = 0 leaves both at m. MM2 sets its CD-audio line to the same balance; music
played through the wave line is balanced with everything else.

### Voice limits

- At most **32 samples** play at the same time (a sample counts once however many of its instances
  play). When a sample starts while 32 are playing, the playing sample with the **lowest priority
  among those whose priority is at most the new one's** is stopped (all its instances; the first
  such in the order the samples started, when several share the lowest value) and the new one
  starts; if every playing sample has a higher priority than the new one, the new one does not
  play.
- Within one sample, at most its instance count play at once (one per instance, see "Samples").

The "sound quality" option's channel count (8, 16 or 32) is stored but changes neither limit.

## Options in the player file

The audio block of `players/<name>.cfg` (layout in `../engine/controls.md`, "Options in
`players/<name>.cfg`"):

| Field | Meaning |
|---|---|
| f32 | SFX volume *s*, 0…1 (gives *g*, "Volume") |
| f32 | music volume, 0…1 (`music.md`) |
| f32 | balance *b*, −1…1 ("Balance") |
| u32 | option bits, below |
| u32 | channel count 8, 16 or 32 (the "sound quality" choice; no effect) |
| u32 *n*, *n* bytes | sound device name (NUL included in *n*); ours: ignore |

| Bit | Option | Effect |
|---|---|---|
| 0x001 | sound effects on | off: no sound effect or speech at all (MM2 does not even start its sound system when the game starts with it off; turning it off in the menu mutes everything with *g* = 0) |
| 0x004 | music on | the DirectMusic soundtrack (`music.md`); turning it on turns ambient off |
| 0x400 | commentary on | race speech exists (`speech.md`) |
| 0x800 | ambient on | instead of music: the city's ambient sound objects are created (`world-sounds.md`) and the music system plays its city ambience segment; turning it on turns music off |
| 0x040 | stereo | pans are applied ("Pan") |
| 0x100 | with 0x040: the third speaker choice ("3D"); no effect on anything here |
| 0x010, 0x020 | sound quality: none = 8 channels, 0x010 = 16, 0x010 + 0x020 = 32 (0x020 only when the sound card reports a capability); no effect |
| 0x080 | the echo state of the moment ("Echo"), written into the file by accident; ignore when reading |
| 0x002, 0x200 | not used by these rules |

Defaults for a new player (and before any file is read): bits 0xC73 (effects, stereo, quality 32,
commentary, **ambient**; music off), 32 channels, SFX 1.0, music 1.0, balance 0. The shipped
`player0.cfg` has 0xCF3 (the same plus the stray echo bit).

When the menu turns sound effects back on during a session, MM2 restores *g* to the raw slider
value *s* instead of the curve above until the next time the options are applied; an
implementation may simply re-apply the curve.

## 3D sources

### Listener and slots

- The **listener** is the active camera: its world matrix, read every frame. Positions are in
  world metres.
- A game has **4 slots** (made when the player's car is set up, emptied at the results screen and
  enabled again when the game resets). A source is heard only while it holds a slot. Every source
  of every kind (the player's own car, opponents, cops, traffic, pedestrians, ambient objects,
  trains …) competes for the same 4.
- Each source has a **priority** number (its page gives it) and is either **3D** (positioned) or
  **2D** (heard everywhere, positioned by its owner or centred). Its **slot priority** is that
  number, plus 1,000,000 if it is 2D, so a 2D source always wins against 3D ones.
- Each 3D source has **drop-offs** *min* and *max* (metres; stored as *min*² and *max*²). A new
  source has *min* = 0 and an invalid *max* (it can never be heard) until its owner sets them.

### Distance

For a source at position P and the listener at position L (both world):

    d² = |P − L|²                                (squared straight-line distance)
    D  = |Px − Lx| + |Py − Ly| + |Pz − Lz|       (Manhattan distance, for pan and doppler)
    ΔD = D_previous − D                          (0 the first time; then the change since the last measurement)

A 2D source counts as d² = 0.01 for slot decisions; a reset 3D source counts as 1,000,000 until
measured. d², D and ΔD are measured each time a step uses them: when a 3D source without a slot
asks for one ("Getting and losing a slot" step 2) and in each update of a slot holder. So a source
that gets its slot in step 1 of the frame ("Order within a frame") is measured twice in that
frame, and the second ΔD (the one its doppler uses) is the change since the first measurement: 0
when neither the source nor the listener moved in between.

### Getting and losing a slot

Every frame, each source's owner runs this step (the pages say when; usually in the owner's own
update):

1. If the 3D manager is disabled or the source already holds a slot, nothing.
2. A 2D source asks for a slot. A 3D source measures its distance and asks only if d² < max².
3. **Asking**: if a slot is free, the first free one (lowest number) is taken. If all 4 are taken,
   choose a candidate among the holders: start with the holder of slot 0; for slots 1, 2, 3 in
   order, move the candidate to that slot's holder if the holder is farther than the candidate
   (larger d²) and its slot priority is not higher than the candidate's. Then the asking source
   takes the candidate's slot if its slot priority is higher than the candidate's, or equal with
   the candidate strictly farther than the asker (larger d²). Otherwise the asker gets nothing
   this frame.
4. The source that loses a slot is told at once ("releasing" below), the new holder at once
   "assigned".

**Assigned**: the source picks up its samples for that slot (below), sets their pans for a 2D
stereo source, and from then on is updated by the manager. **Releasing**: the source turns its
echo copies off, stops what its page says (usually its loops; once-played sounds may be left to
finish) and lets go of the samples; it may ask again next frame.

Every frame (after the game-mode update, only when not paused), the 3D manager updates each slot
holder in slot order. A 3D holder first measures its distance; **if d² ≥ max² it releases its
slot** and does nothing more this frame (except a source marked "keeps its slot", e.g. a siren
after it starts, `car-sounds.md`: it keeps the slot, and its attenuation keeps the last value it
had inside *max*). Otherwise it computes the values below and applies them to its samples (the
pages say how).

### Samples of 3D sources

Samples used by 3D sources come from a shared cache: each distinct name (with its pitch-control
choice) is loaded **once per slot**, i.e. 4 copies, each with volume 0 at first (samples of the
cache have pitch control unless a page says otherwise). A source holding slot *k* uses copy *k* of
each of its samples. So two sources never share a playing sample.

### Attenuation, pan and doppler

For a 3D holder with d² < max²:

    a = 0                                    if d² ≤ min²
    a = (d² − min²) × (1 / (max² − min²))    otherwise
    attenuation A = 1 − a

The source multiplies its sample volumes by A (in the decibel-linear scale of "Volume"; e.g. a
car (min 0) at half its maximum distance has a = 0.25, A = 0.75: −25 dB more than up close, before the
sample's own volume).

    pan = 0                                  if d² < min²
    pan = 0.2 × x / D                        otherwise

where *x* is the source's position along the listener's right-hand axis: x = (P − L) · R with R the
first row (right axis) of the listener's matrix. So 3D pan never exceeds ±0.2 (the far ear at most
20 dB down). (MM2 also computes a "behind the listener" factor that nothing uses.) Pan is only
used in stereo.

    doppler factor = ((ΔD × k) × dt) + 1

with dt the frame time in seconds and *k* a constant per kind of source given by the caller (cars
0.0176315; the pages give the others). Because ΔD is per frame and is multiplied by dt again, the
effect is tiny (a car closing at 50 m/s at 30 frames per second gives about 1.001); keep the
formula as it is. The factor multiplies into the pitch of the samples the page says.

A 2D holder gets A = 1, pan = its owner's fixed pan, doppler 1 (its page may set its own values).

## Echo (tunnels)

**Echo state.** Every frame, the room containing the player car's body position
(`../formats/psdl.md`, point in room) is looked up; if the room has the enclosed flag (PSDL room
flag 0x02, tunnels and the Underground) the echo state is on, otherwise off. While on, the 3D
manager holds an **echo delay of 0.5 s**. The music system reads the state too (`music.md`).

**Echo copies.** When the state changes, each sound owner turns the echo of its samples on or off
(each page lists which samples and the values). An echo copy is a second voice of a sample's
instance with three settings, the delay *T*, the echo gain *G* and an initial echo pitch *F*:

1. **On**: the first time for that instance, the echo voice is made (sharing the sample's sound
   data) with the instance's current play position, level and playback rate (the file's rate for
   a sample without pitch control), and *T* is fixed; a later switch-on reuses that voice where it
   stopped (it is not moved, and the first *T* stays). An owner that gives an *F* then sets the
   echo pitch to `rate × F` (file rate; limited to 100…100,000 Hz); otherwise the voice keeps that
   rate until the first pitch repeat arrives. If the instance is **playing looped** at the
   switch-on, a looped play repeat is queued at once: a loop already running (an engine, a skid)
   gets its echo *T* seconds later, from the echo voice's position (the first time: where the
   instance was at the switch-on). A once-played sound already playing gets no echo. From now on:
2. every **play** (once or looped), **stop**, **volume** and **pitch** command given to the
   instance is repeated on the echo voice **T seconds later**: a play starts the echo from the
   position the instance had when it was told to play; a volume *v* reaches the echo as
   g × v × G (the effects gain *g* applied before, mapped as in "Volume" but not clamped); a pitch
   reaches it as the instance's final playback rate at the time of the command;
3. every **pan** command reaches the echo voice at once, as clamp(−0.25 × p, −1, 1): mirrored and
   quartered;
4. **Off**: pending repeats are dropped and the echo voice stops at once.

Repeats are kept per kind of command in order, at most N = trunc(T × 180) of each kind (90 for
0.5 s, 9 for 0.05 s; fixed with *T*). A command that arrives when its kind already holds N
overwrites the oldest and **all the other pending repeats of that kind are forgotten**: it is then
the only one, waiting its full *T*. (With one command of a kind per frame this happens only above
about 180 frames per second, and then that kind's repeats never come due.) Repeats advance only
when the sample's owner ticks the sample (each page says where; the owners tick their samples once
per frame in their slot update while the echo is on, nothing else does): each tick adds the frame
time to the age of every pending repeat, and when the oldest one's age reaches *T* it is carried
out and every other repeat of that kind that is also due is dropped (at most one per kind per
tick; with one command per frame that is every command). The echo voice is not a separate sample
for "Voice limits".

The car's horn, for example, uses T = 0.05 s, G = 0.96, F = 0.997, and its other samples use the
manager's 0.5 s (`car-sounds.md`).

## Pause, results and reset

- **Pause**: on the first two frames of a pause every playing sample and echo voice is stopped.
  Nothing is resumed by the sound system: looping sounds come back because their owners start them
  again when they find them not playing; once-played sounds that were cut are lost.
- The 3D manager does not update its holders while paused; sources keep their slots.
- **Results screen**: the 3D manager is disabled and every slot released (all 3D and 2D sources go
  silent); a game reset enables it again.

## Sound random numbers

Sounds never use the game-wide generator. Each draw makes a fresh generator and takes its first
number:

1. seed = the wall-clock time in whole seconds since 1970-01-01 UTC (the C `time()` value).
2. Seed the subtractive generator of Knuth (as in *Numerical Recipes* "ran3") with modulus
   1,000,000,000 and seed constant 161,803,398, exactly: mj = (161803398 − |seed|) computed as a
   32-bit signed integer and reduced with the C remainder `% 1000000000` (no absolute value:
   with today's clock mj is negative, and the generator runs on with it); ma[55] = mj; mk = 1;
   for i = 1…54: ii = (21 × i) mod 55, ma[ii] = mk, mk = mj − mk (+ 10⁹ if negative), mj = ma[ii];
   then four rounds of: for i = 1…55, ma[i] −= ma[1 + (i + 30) mod 55] (+ 10⁹ if negative);
   inext = 0, inextp = 31.
3. Draw: inext = inext + 1 (back to 1 after 55), inextp likewise; mj = ma[inext] − ma[inextp]
   (+ 10⁹ if negative); ma[inext] = mj; r = mj × 10⁻⁹ (0 ≤ r < 1).
4. One-argument form: rnd_s(a) = ((a × 0.01) × r) × 100. Two-argument form: with
   lo′ = lo × 0.01 rounded to single precision, rnd_s(lo, hi) = ((hi × 0.01 − lo′) × r + lo′) × 100.
   (0.01 is the single-precision 0.0099999998; the rest is in extended precision and the caller
   rounds when it stores.)

So every sound decision taken within the same second gets the same *r*: two cars honking in the
same second pick the same pattern. Our engine should do the same (seed from the real clock), and a
check run may pin the clock.

## Order within a frame

1. Game objects update (cars, traffic, pedestrians, …); each sound owner updates its own state and
   runs "Getting and losing a slot".
2. The player's update: listener ← active camera; echo state from the player car's room.
3. Game-mode update, then (not paused) the 3D manager: ambient objects (`world-sounds.md`), then
   each slot holder in slot order.
4. Speech update (`speech.md`).
5. The sound system's own update: pause handling (echo repeats are ticked by the owners in
   step 3).

## For the browser (engine notes)

- Gain of a played volume: `10^((v_played − 1) × 5)` (amplitude), times the balance gain of each
  channel.
- Pan: with p > 0, left gain × `10^(−5p)`, right × 1; p < 0, right × `10^(5p)`. Use a channel
  splitter/merger, not an equal-power panner.
- Pitch: `playbackRate = clamp(f, 0, 2)` with the 100…100,000 Hz limit; samples without pitch
  control keep rate 1.
- Play position copying for echo voices: start the echo buffer source at the main voice's offset.
- Seed sound random numbers from `Math.floor(Date.now() / 1000)`.

## Checked against

- The program: the start-up of the sound system and the options menu (bits, defaults, the
  exclusive music/ambient pair, the empty channel-count setter), the player-file reader and writer
  (field order), the effects-gain curve (log base 200, read from the x87 code), the wave-out
  balance code, the sample loader (path rule, `.wav` rule, chunk walk), the DirectSound volume,
  pitch and pan conversions (×10000, −1, 100…100,000 Hz, pitch clamp 0…2), the two voice-stealing
  loops, the 3D manager (4 slots at set-up, eviction order read from the machine code, sample cache
  one copy per slot), the 3D object maths (squared drop-offs, attenuation, pan 0.2, Manhattan
  distance, doppler with dt; the distance measured again by each asking step and each holder
  update), the echo copy queues (delay × 180 entries, a full queue wrapping to its first entry;
  gain 0.98 by default, pan × −0.25; the echo voice made once per instance from the main voice's
  position, level and rate; a looped play repeat queued at switch-on when the main voice loops),
  the "is playing" query (the output's own status of the voice), the echo-state test in the
  player update (room flag 0x02, delay 0.5), the pause stop and the sound random generator (ran3
  constants, time seed, 0.01 / 100 scaling).
- Data (`tools/audio/sounds.py`): every shipped WAV parsed with the rules above (2,503 files, the
  format counts given in "WAV files", no file rejected); every sample name the sound pages can build
  resolved against the archives: 3,359 references, 2,162 distinct files, 2,067 found, 95 missing
  (each explained on its page: never-played speech categories, `horns\FIRETRUCKSIREN`,
  `surfaces\skidflagstone`, `impacts\caroncobble`, `Amb3D\NOTHING`, `UIreplay`); 436 of the 2,503
  WAVs are reached by no name (226 of them the unused 11 kHz copies in `aud/aud11/`); `players/player0.cfg`
  audio block 1.0, 1.0, 0.0, 0xCF3, 32, `Voice Modem Wave #00 Line (emulated)`.
