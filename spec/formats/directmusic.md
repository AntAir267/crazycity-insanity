# DirectMusic music files (`aud/dmusic/`)

MM2's music is Microsoft DirectMusic content: **segments** (`.sgt`) that run tracks in parallel,
**styles** (`.sty`) holding patterns of notes the style track composes from, **bands**
(`.bnd`, and inside segments and styles) that choose instruments, and **DLS collections**
(`.dls`, Downloadable Sounds level 1) holding the instruments' samples. This page gives the
layout of every chunk MM2's files contain and the playback rules a player needs to sound like MM2
with its own composer and synthesiser. When the game plays what is `../audio/music.md`.

DirectMusic is a large system; this page covers what MM2's 95 music files use and marks the rest
out of scope (end of page). The playback rules come from the DirectMusic runtime MM2 calls into
(version 7/8 of the Windows component), checked against MM2's data.

## Inventory

All music is in `mm2aud.ar` under `aud/dmusic/` (plus the tables in `aud/dmusic/csv_files/`,
`../audio/music.md`). No other archive holds DirectMusic files. Names are looked up without regard
to case.

| Kind | Files | Count |
|---|---|---|
| Segment `.sgt` | `BackOff{Start,ReStart,Idle,Cops,CopsIdle}`, `Bullet{Start,Return,Idle,IdleCops,Cops}`, `Duck{Start,Return,Idle,IdleCops,Cops}`, `Enemy{Start,Return,Idle,IdleCops,Cops}`, `Groover{Start,Return,Idle,Cops}`, `LeTigre{Start,Restart,Idle,Cops,CopsIdle}`, `London{Start,Return,Idle,Cops,CopsIdle,Trans1}`, `Pimphand{Start,Return,Idle,IdleCops,Cops}`, `SunRoof{Start,Return,Idle,Cops}`, `SunIdleCops`, `TacoLoco{Start,Restart,Idle,Cops,CopsIdle}`, `Pause`, `Results1`, `Results2`, `UI`, `SFAmbience`, `LondonAmbience`, `UndergrounAmbience`, `NightRoamStart`, `UseForSomething`, `Vanilla1`, `Vanilla2`, `bigair` | 62 |
| Style `.sty` | `Backoff`, `BigAir`, `BulletStyle`, `DuckStyle`, `Enemy`, `GrooverCops`, `GrooverStyle`, `LeTigre`, `London`, `LondonAmbience`, `Pause`, `Pimphand`, `SFAmbience`, `SunRoof`, `TacoLoco`, `UndergroundAmbience`, `VanillaNice` | 17 |
| DLS `.dls` | `BackOffDls`, `DLS Collection1`, `DLS Collection2`, `DLS Collection3`, `DLS SunRoof`, `DuckandCover`, `LeTigre`, `London`, `LondonAmbience`, `Pimphand`, `SFAmbience`, `TacoLoco`, `UndergroundAmb`, `VanillaNice` | 14 |
| Band `.bnd` | `BigAir`, `Pimphand` | 2 |

There are no chordmaps, templates, containers, scripts, audio paths, wave files or loose MIDI
files. Instruments a band names without a collection come from the system's General MIDI set
("General MIDI instruments" below).

## Common rules

- Files are RIFF: a chunk is a four-character id, a u32 size and that many bytes, padded to an
  even length. `RIFF` and `LIST` chunks start with a four-character form type and hold chunks.
  All numbers are little-endian.
- Every file is one `RIFF` form: `DMSG` segment, `DMST` style, `DMBD` band, `DLS ` collection.
- Unknown chunks are skipped. Chunk order inside a container does not matter except where said.
- **Item arrays**: several chunks are a u32 *item size* followed by items of that size. Read the
  size from the file and take the fields below from the start of each item; MM2's files were
  written by the DirectX 7 era tools and their items are the sizes given here. Bytes past the
  fields listed are padding and may hold garbage (the command items do).
- **Names**: `LIST UNFO` holds `UNAM`, a NUL-terminated UTF-16 name. Names do not affect playback
  except a motif's name (the music tables choose the motif by name, case-insensitively).
- **GUIDs** are 16 bytes in the usual mixed-endian layout (first three groups little-endian).
- `guid` (16 bytes) is an object's own GUID; `vers` is two u32 (version high, low) and is
  ignored. Segment GUIDs are not unique in MM2's data (13 groups of copies share one), so a
  segment must never be found by GUID; styles, bands and collections have unique GUIDs.

### References (`LIST DMRF`)

A reference to another file:

| Chunk | Contents |
|---|---|
| `refh` | class GUID (16), u32 valid-fields bits |
| `guid` | the target's GUID |
| `name` | the target's name (UTF-16) |
| `file` | the target's file name (UTF-16), e.g. `Enemy.sty`, `DLS Collection1.dls` |
| `vers` | version (ignored) |

MM2's references are all of class style `d2ac288a-b39b-11d1-8704-00600893b1bd` (62) or DLS
collection `480ff4b0-28b2-11d1-bef7-00c04fbf8fef` (1,803), with valid bits 0x97 (GUID, class,
name, file name, version).

**Resolving** (MM2's own loader): first look for an already loaded object with that GUID; then
for one loaded under that file name (compared without regard to case); otherwise load
`aud/dmusic/<file>` from the archives (`../formats/ar.md`). Loaded styles and collections stay
loaded for the life of the music player (`../audio/music.md`) and are shared by everything that
refers to them. Every reference in MM2's data resolves by file name and the file's GUID matches.

## Segments (`RIFF DMSG`)

| Chunk | Contents |
|---|---|
| `segh` | header, 24 bytes (below) |
| `guid`, `vers`, `LIST UNFO` | as above |
| `LIST trkl` | the tracks: one `RIFF DMTK` each |

Header: u32 repeats; i32 length (ticks); i32 play start (ticks); i32 loop start; i32 loop end;
u32 default resolution (0 in all files: see "Starting a primary segment").

Time is counted in **ticks**, 768 per quarter note.

Each `RIFF DMTK` track has `trkh`: class GUID (16), u32 position (0 in MM2's files), u32 group bits
(1, or 0xFFFFFFFF for all groups), the id of the track's data chunk (4 bytes, or zero) and the form
type of its data `LIST`/`RIFF` (or zero); then the data.

| Track | Class GUID | Data | Files |
|---|---|---|---|
| style | `d2ac288d-…` | `LIST sttr` | 62 |
| band | `d2ac2894-…` | `RIFF DMBT` | 62 |
| tempo | `d2ac2885-…` | `tetr` | 62 |
| command | `d2ac288c-…` | `cmnd` | 61 (all but `bigair`) |
| chord | `d2ac288b-…` | `LIST cord` | `bigair` only |
| sequence | `d2ac2886-…` | `seqt` | `bigair` only |
| time signature | `d2ac2888-…` | `tims` | `bigair` only |

(`…` = `b39b-11d1-8704-00600893b1bd`.) Every segment the game plays has exactly the first four.

### Style track (`LIST sttr`)

One or more `LIST strf`, each `stmp` (i32 time) and a `LIST DMRF` style reference. MM2's
segments have one style each, at time 0 (at 12288 in `DuckCops`, `DuckReturn`: the first style also
applies before its own time).

### Band track (`RIFF DMBT`)

`LIST lbdl` of `LIST lbnd`, each: `bdih` (i32 band time) and a `RIFF DMBD` band (below). 83 bands
in 62 segments; times 0 plus, in some segments, later times (e.g. `EnemyStart` 0, 3072, 52224).
No `bdth` chunk occurs.

### Tempo track (`tetr`)

Item size 16: i32 time, 4 bytes padding, f64 tempo (quarter notes per minute). One item at time 0
in every segment except `LondonTrans1` (150, 146, 140, 135 at 0, 768, 1536, 2304). Tempos used:
86.5 … 180.

### Command track (`cmnd`)

Item size 12: i32 time, u16 measure, u8 beat, u8 command, u8 groove level, u8 groove range,
2 bytes padding. In MM2's files every command is 0 (**groove**: set the groove level), beat 0,
groove range 0, time = measure × the style's measure length, and the padding bytes are garbage
(0xDF, 0x6F in all 256 items); they must be ignored (a reader that takes byte 10 as a "repeat
mode" gets garbage). Groove levels 1…99; every segment's first command is at measure 0.

### Chord, sequence and time-signature tracks (`bigair.sgt` only)

`bigair.sgt` is never played by the game; these three are listed for completeness.

- `LIST cord`: `crdh` u32 (key root in the top byte, 24-bit key scale below: 0, 0xAB5AB5);
  `crdb`: u32 chord size (40), then per chord: 32-byte UTF-16 name, i32 time, u16 measure, u8 beat,
  u8 flags, u32 subchord count, u32 subchord size (20), subchords (u32 chord pattern, u32 scale
  pattern, u32 inversion points, u32 levels, u8 chord root, u8 scale root, 2 padding).
- `seqt`: chunks inside: `evtl` (item size 20: i32 time, i32 duration, u32 PChannel, i16 offset,
  u8 status, u8 data 1, u8 data 2, padding) and `curl` (curves; empty here).
- `tims`: item size 8: i32 time, u8 beats per measure, u8 beat (4 = quarter), u16 grids per beat.

## Styles (`RIFF DMST`)

| Chunk | Contents |
|---|---|
| `styh` | 12 bytes: time signature (u8 beats per measure, u8 beat note value, u16 grids per beat), f64 tempo |
| `guid`, `vers`, `LIST UNFO` | |
| `LIST part` | one per part (below); 1,786 in all |
| `LIST pttn` | one per pattern (below); 183 in all |
| `RIFF DMBD` | the style's bands (31 in all); the first is the style's **default band**, played by automatic transitions; MM2 uses no other |

Style time signatures: 4/4 (most), 2/4 (`London`), 1/4 (`Pimphand`, `VanillaNice`), all with 2
grids per beat. A measure is beats × (4 / beat value) × 768 ticks (3072, 1536, 768).

### Parts (`LIST part`)

`prth` (154 bytes): part time signature (4 bytes as above), 32 × u32 **variation choices**
(one per variation), part GUID (16), u16 length in measures, u8 play mode, u8 inversion upper
bound, u8 inversion lower bound, 1 padding. Then optionally `note` and `crve`.

- Part time signatures: beats 1, 2 or 4, beat value 4, grids per beat 2, 4, 8 or 9 (a part may
  have a finer grid than its style).
- Play modes: 0x0E (1,658 parts) or 0x00 (128). Inversion bounds 0…127 except one part (44…56).
- Variation choices: bits 30–31 are clear in every word (mostly 0x3FFFFFFF).

`note` (item size 22): i32 grid start, u32 variation bits (bit v = the note belongs to variation
v), i32 duration (ticks, ≥ 1 in all files), i16 time offset (ticks; −31744 … +), u16 **music
value**, u8 velocity, u8 time range, u8 duration range, u8 velocity range, u8 inversion id, u8 play
mode. 155,817 notes. Note play modes: 0x0E or 0x10 ("use the part's"); inversion ids 0; 137 notes
have ranges (time, duration, velocity) = (2, 0, 8), (4, 5, 6) or (7, 10, 10).

`crve` (item size 28): i32 grid start, u32 variation bits, i32 duration, i32 reset duration, i16
time offset, i16 start value, i16 end value, i16 reset value, u8 event type, u8 shape, u8
controller number, u8 flags. 3,720 curves: event types 3 (pitch bend), 4 (controller: 7, 1, 64, 10,
100, 101, 6, 38), 5 (channel pressure); shapes 1 and 4; flags 0 or 1 (reset).

### Patterns (`LIST pttn`)

| Chunk | Contents |
|---|---|
| `ptnh` | 10 bytes: time signature (4), u8 groove bottom, u8 groove top, u16 embellishment, u16 length in measures |
| `rhtm` | u32 per measure: the chord rhythm (bit b = a chord may change on beat b); 1 then zeros in every pattern |
| `mtfs` | motif settings, motifs only (below) |
| `LIST UNFO` | the pattern's name |
| `LIST pref` | one per part reference: `prfc` and a `LIST UNFO` |

`prfc` (22 bytes): part GUID (16), u16 **PChannel** (the part plays on this PChannel), u8
variation lock id, u8 subchord level, u8 priority, u8 variation mode. Values in MM2: lock id 0…4,
subchord level 0, priority 100, variation mode 1 (random, 1,897 refs) or 0 (in order, 62). A
part GUID may be used by several patterns (and several PChannels); all part GUIDs referred to
exist in the same style.

Embellishments: 0 normal (158 patterns), 8 ending (14), 0x10 motif (11). Pattern lengths 1…504
measures. Normal patterns almost always have groove bottom = top, so each groove level selects
its pattern(s) directly; motifs and endings use (1, 100) or one level. A pattern's parts are
usually as long as the pattern (1,913 refs); 42 are a whole fraction of it and 4 neither.

Motif settings `mtfs` (20 bytes): u32 repeats, i32 play start, i32 loop start, i32 loop end, u32
default resolution. MM2's 11 motifs: repeats 0, start 0, loop 0 … (pattern length in ticks),
resolution 0x1000 or 0x2000 (see "Motifs").

## Bands (`RIFF DMBD`)

`guid`, `LIST UNFO`, `LIST lbil` of `LIST lbin`; each `lbin` is `bins` and optionally a `LIST DMRF`
collection reference.

`bins` (40 bytes): u32 patch, u32 assign patch, 4 × u32 note-range bits, u32 PChannel, u32 flags,
u8 pan (0 = left, 64 = centre, 127 = right), u8 volume, i16 transpose, u32 channel priority.

Patch: program in bits 0–6, bank select LSB in bits 8–14, MSB in bits 16–22, bit 31 = drum kit.
Flag bits: 0x1 program change, 0x2 bank select, 0x8 assign patch, 0x10 note ranges, 0x20 pan,
0x40 volume, 0x80 transpose, 0x100 General MIDI, 0x200 GS, 0x400 XG, 0x800 channel priority,
0x1000 default GM set, 0x2000 pitch bend range.

In MM2's files (2,073 instruments): flags 0xE3 (1,818: 1,795 with a collection reference; 23
without one, in the ambience and `Groover*` bands, naming patches no collection holds: they
download nothing but still send their bank, program, CC 7 and CC 10, so their channel plays
whatever downloaded instrument has that patch — none in MM2, and no part MM2 plays has notes
there), 0x83,
0x82, 0x1E1, 0x361 (General MIDI instruments without a reference: drum kit 0x80000000 and a few
programs) and 0x300 (99 empty entries with no program, volume or pan bit). Assign patches hold
garbage where the assign bit is clear; note ranges and transposes are all 0; PChannels 0…31;
channel priorities 0x80000000 … 0x8000000F.

## DLS collections (`RIFF DLS `)

Downloadable Sounds level 1 (MMA DLS 1.1) as written by DirectMusic Producer:

| Chunk | Contents |
|---|---|
| `colh` | u32 instrument count |
| `vers`, `LIST INFO` | ignored |
| `dlid` | the collection GUID (what references match) |
| `ptbl` | u32 header size (8), u32 count, then count × u32 offsets of each `LIST wave` from the start of the `wvpl` list's data (after its form type) |
| `LIST lins` | instruments, `LIST ins ` each |
| `LIST wvpl` | waves, `LIST wave` each, in table order |

Instrument (`LIST ins `): `insh` (u32 region count, u32 bank, u32 program), `LIST lrgn` of
`LIST rgn `, optionally `LIST lart` (instrument articulation), `LIST INFO` (`INAM` name). Bank:
LSB in bits 0–6, MSB in bits 8–14, bit 31 drums. MM2's 204 instruments are in banks LSB 1 (128)
and 2 (76), MSB 0, no drum instruments; every instrument has an articulation.

Region (`LIST rgn `): `rgnh` (u16 key low, u16 key high, u16 velocity low, u16 velocity high —
both 0 in all files, ignored —, u16 options (1 = a note does not stop an earlier note of the same
key), u16 key group (0)); `wsmp` (below; every region has one); `wlnk` (u16 options, u16 phase
group, u32 channel (1), u32 wave index into the pool table); optionally `LIST lart` (64 regions:
an articulation that replaces the instrument's for this region).

`wsmp`: u32 size (20), u16 unity note, i16 fine tune (cents; 0 in all files), i32 attenuation
(units of 1/655,360 dB; 0 … −4,390,912 ≈ −6.7 dB), u32 options (1), u32 loop count (0 or 1), then
loops (u32 size 16, u32 type 0 = forward, u32 loop start, u32 loop length, in sample frames).
Unity notes 3 … 85. A region's `wsmp` replaces the wave's.

Articulation (`LIST lart` of `art1`): u32 size (8), u32 count, then connections (u16 source, u16
control, u16 destination, u16 transform (0), i32 scale). Connections present:

| Source, control → destination | Meaning | In MM2 |
|---|---|---|
| none → 0x004 | pan (0.1 % × 65536; ±500 = hard left/right) | ±500 |
| none → 0x104 | LFO frequency (absolute pitch, cents × 65536) | 0.11 … 10.0 Hz |
| none → 0x105 | LFO start delay (timecents × 65536) | 0.049 … 2.73 s |
| none → 0x206 / 0x207 / 0x209 | volume envelope attack / decay / release time (timecents × 65536) | 0.01 … 2.83 s / 1.5 … 40 s / 0.002 … 21.7 s |
| none → 0x20A | volume envelope sustain (0.1 % × 65536) | 0 … 99 % |
| none → 0x30A / 0x30B / 0x30D | pitch envelope attack / decay / release | 0.15 … 10 s / 1.39 … 40 s / 0.021 s |
| none → 0x30E | pitch envelope sustain | 0 … 76.6 % |
| LFO → 0x001 | tremolo depth (gain, 1/655,360 dB) | −2.8 … 2.9 dB |
| LFO → 0x003 | vibrato depth (cents × 65536) | −31 … 77 cents |
| LFO, mod wheel (0x81) → 0x001 / 0x003 | extra tremolo / vibrato at full mod wheel | −0.2 … 0.9 dB / −600 cents |
| key velocity → 0x206 | attack time scaling at velocity 127 (timecents × 65536) | −1169 tc (one instrument) |
| key number → 0x30B | pitch decay scaling at key 127 | 62 tc (one instrument) |
| pitch envelope → 0x003 | pitch envelope depth (cents × 65536) | −46 … 15 cents |

All transforms are 0 (none); sources 0 none, 1 LFO, 2 velocity, 3 key number, 5 pitch envelope;
control 0x81 is CC 1.

Wave (`LIST wave`): `fmt ` (PCM: tag 1, 1 channel, 16 bits, rates 8,000 … 44,100 Hz),
`data` (the samples), `wsmp`, `smpl`, `wavu` (2 bytes, ignored), `LIST INFO`, and in 12 waves
`cue ` and `LIST adtl` (editor markers, ignored; some of their sub-chunks have odd sizes — read
them leniently). 368 waves, 135 … 330,514 frames; loops end at or before the last frame.

## Playback: time

- **Ticks**: 768 per quarter note. A beat is 3072 / (beat note value) ticks; a measure is beats per
  measure × that; a grid is a beat divided by grids per beat (integer divisions).
- **Time signature**: MM2's segments have no time-signature track (except `bigair`); the style track
  supplies the style's time signature. Measure, beat and grid boundaries of a segment are counted
  from **segment time 0** (the style's reference time, 0 in MM2), not from its start point: e.g.
  `BackOffCops` starts at 23808 (measure 7, beat 4) and its first measure boundary comes 768 ticks
  after it starts.
- **Tempo** comes only from tempo tracks (the style's own tempo is never used for playback). A
  tempo event holds until the next; a segment with no tempo inherits the tempo already in force;
  with nothing ever set, 120. The tempo track is read in segment time (it follows loops).
- **Clocks.** The **render point** is the first sample the synthesiser has not yet rendered (MM2's
  *latency clock*). The **present** is the sample being heard: MM2's synthesiser tops its output
  up every 20 ms to 80 ms past the sound card's write position, so the render point runs 60–80 ms
  (plus the card's own lead; 240 ms with an emulated driver) ahead of the present. ***now*** (the
  *queue time*) is the render point or, if later, the time of the latest MIDI event already handed
  to the synthesiser, which receives each event once its time is less than 75 ms after the render
  point (checked every 10 ms). **Engine:** the render point is the next sample the engine renders;
  the present is 80 ms before it.
- **Composing ahead.** Tracks are played (notes composed; patterns, variations and random draws
  chosen) up to the present + 1000 ms (MM2 never changes this prepare time); a segment that starts
  inside that window is composed from its start at once. Each MIDI event takes effect at its own
  time, or at the render point if that time has already passed when it is composed. So a note
  pulled early by its time offset sounds on time when composed early enough, even before its own
  pattern or segment begins; composed too late it sounds at the render point; if its note-off has
  passed too it makes no sound (both arrive together and its voice is released at its first
  sample, from level 0), as with the nine `LeTigreCops` notes 31,168–31,744 ticks early. A stop
  or a new primary removes the events composed for its time on ("Starting a primary segment",
  "Stopping").

## Playback: a segment

A segment plays from its **play start** to its **loop end**, then the loop region (loop start …
loop end) as many more times as its **repeat count**, then from the loop end to its **length**,
and stops. When it starts:

1. If repeats ≠ 0 and loop start = loop end = 0, the loop is the whole segment (0 … length).
2. If loop start < loop end, repeats = min(repeats, floor((1,073,741,823 − length) / (loop end −
   loop start))) (so 2³⁰ means "a few thousand loops", for ever in practice); if loop start = loop
   end ≠ 0, repeats = 0.
3. A loop end of 0 means the length.
4. If loop end ≤ play start there is no loop at all.

Total time = (length − play start) + repeats × (loop end − loop start). At the start and at every
loop jump the tracks are told so: the style track repeats its pattern choices (with their random
draws) from segment time 0 up to the new position without sounding them ("catching up", below) and the band track (on the
start only) first sends every band whose time lies before the play start, in order.

### Starting a primary segment

One segment at a time is the **primary** (MM2's music); others play beside it as **secondary**
segments (the motif, the band). A primary is started for the time *now* ("Playback: time"). If a
primary segment is playing at that time, *now* is
rounded **up** to the next boundary of that segment (measure, beat or grid as asked; measure wins
over beat, beat over grid; a time already on a boundary stays) in its own time signature and
counted from its segment time 0. If no primary is playing, it starts at *now* with no rounding.

A primary that is still playing when a new one starts is stopped one tick before the new start
with its notes cut, any primary waiting in the queue is dropped, and every note still sounding at
the new start — the motif's included — is cut there; secondary segments then go on from that time.
This happens before the new segment is composed at all (the same at a queued segment's start), so
the new segment's own notes, those sounding early by a negative time offset included ("Composing
ahead"), are never cut by it.

### Stopping

"Stop at once" ends the segment at *now*: notes already sounding get their note-off then, notes
not yet started never play, notes that started earlier end then; curves with the reset flag send
their reset value. No all-notes-off is sent and secondaries go on. A segment stopped this way
still counts as "playing at *now*" for an immediately following start, so MM2's cut (stop, then
start at the next beat) starts the new segment on the next beat of the **old** segment's grid.

## Composing from a style (style track)

The style track turns the style into notes by choosing a **pattern** at a time, and inside it a
**variation** for each part. With MM2's data the chord is always the default chord (below) and
the commands are groove levels only.

### When a pattern is chosen

The track keeps the current pattern, its start time and its **end time**. It chooses:

- when the segment starts (at the start point) and at every loop jump: first it repeats the
  choices silently from segment time 0 (choose at 0, again at that pattern's end, and so on) until
  the chosen pattern's end lies after the new position; that pattern then plays from the middle;
- whenever playback reaches the current end time; the new pattern starts exactly there, from its
  own first measure.

Nothing else makes a new choice (no check per measure). The end time is start + pattern length
(measures × measure length), but no later than the time of the **next command** after the
choice time (or the segment end / loop end when there is no later command): every command,
even one repeating the same groove level, cuts the pattern and forces a new choice there.

### Inputs of a choice made at time t

- *A* = whole measures from t to the segment end (to the loop end while loops remain), at least 1,
  at most the longest pattern of the style.
- *C* = whole measures from t to the next command (or the same end) — floor; 0 when less than a
  measure.
- For each measure i < *A*: the command in force at t + i measures: its groove level (clamped to
  1…100) and its kind — the kind (groove, fill, intro, break, end) only if a command lies exactly
  at that measure's start, otherwise "groove". Without any command the level is 62.

A pattern **matches over n measures** when it is not a motif and, for each measure i < min(n, its
length) that is measure 0 or whose command is not "groove" or whose level differs from measure 0's:
- **kind**: a groove command needs embellishment 0; end needs bit 8; fill bit 1; intro bit 4;
  break bit 2 (an embellishment with a non-zero high byte must equal the command number instead);
- **groove**: with h = range ÷ 2 (0 in MM2): lo = level − h (1 if level < h), hi = min(level + h,
  100); the pattern matches if its groove bottom ≤ hi and its groove top ≥ lo.

### The choice

Three tests, tried in order; within each test the patterns are taken in groups of equal length,
longest first (and, at equal length, the longest measure first); the first group with any pattern
passing the test supplies the candidates:

1. matches over *A*, length ≤ *A*, length ≤ *C* (when *C* > 0), and its chord-rhythm words have at
   least one bit (always true in MM2: "1 then zeros"); keep only those with the fewest rhythm bits;
2. matches over *A*, length ≤ *A*, length ≤ *C* (when *C* > 0);
3. matches over 1 measure (any length; the longest wins and is cut at the next command).

If no test finds any, **every pattern of the style except motifs** is a candidate (normal, end,
any groove). The pattern is candidate number rand() mod *n* (in file order), one draw even when
*n* = 1 (random numbers below).

Examples from the data: `DuckStart` groove 1 for 4 measures → `DuckIntro` (4) out of
`duckandcover` (130), `DuckIntro`, `DuckBPart` (33); `GrooverStart` groove 5 for 4 measures →
one of `Early`, `Early_1.5`, `Early_2` at random; `EnemyStart` groove 2 for 8 measures → one of
`EnemyPause_3`, `EnemyRace1.1`.

### Parts and variations

A pattern's part references play at the same time, each on its PChannel. A part shorter than the
pattern repeats inside it every part length (the part's own time signature when its beat value is
not 0); a part longer than the pattern is cut at the pattern's end.

A **variation** is chosen for every part reference, in reference order, each time a pattern is
chosen:

1. Allowed variations: v is allowed when its choice word c (0xFFFFFFFF counts as 0x3FFFFFFF) has
   (c & m) = m, with m = 0x02200001 if c has bit 29, else 0x00002101 (the masks of the default
   chord; other chords give other masks). MM2's words are only 0x3FFFFFFF (allowed) and
   0x20000000 (never: not even enabled). If none is allowed: all enabled ones (c & 0x5FFFFFFF ≠ 0);
   if none is enabled: variations 0…15.
2. Lock ids: a reference with lock id L ≠ 0 takes the variation already chosen for L during this
   pass, if any.
3. Otherwise: variation mode 1 (random): the (rand() mod count)-th allowed variation in ascending
   order. Mode 0 (in order): the next allowed variation after the current one (cyclic); the
   current one is kept only while the **same** pattern is chosen again, a different pattern resets
   it so the lowest allowed variation comes first.
4. The choice is remembered for L.

A note or curve plays only if its variation bits contain the part's current variation. (1,557 of
MM2's parts have a single allowed variation and are therefore fixed.)

### Notes

For each note of the part's current variation whose **grid time** g = part repeat start + grid
position (grid → ticks with the part's time signature: (grid ÷ grids per beat) × beat ticks +
(grid mod grids per beat) × grid ticks) lies in the window being played (inside the pattern's
start … end), in note order:

1. time = g + time offset; if the time range byte x ≠ 0: time += R(x);
2. duration = the note's duration; if its duration range ≠ 0: duration += R(range);
3. velocity v = the note's velocity; if its velocity range r ≠ 0: v = (v + (rand() mod r) − (r ÷ 2))
   taken modulo 256; then 0 → 1 and anything above 127 → 127;
4. key = the note's music value converted with the note's play mode (0x10 means: use the part's
   play mode) and the part's inversion bounds ("Music values");
5. the note-on goes out at time with key + the PChannel's transpose (0 in MM2); a key outside
   0…127 or a duration below 1 drops the note; the note-off goes out at time + duration − 1
   (duration > 1) or time + 1, at least 1 ms after the note-on.

R(x) = (rand() mod w) − (w ÷ 2) with w = x for x < 191, (x − 152) × 5 up to 212, (x − 182) × 10
up to 232, (x − 222) × 50 above. Draw order per note: time, duration, velocity. (MM2's ranges are
2…10.)

### Curves

A curve of the current variation whose grid time is in the window starts at grid time + offset
(no random) and sends values on its PChannel:

- **Instant** shape (1): the end value once, at the start.
- Other shapes: elapsed e = 0, then every *step* ticks, and the end value exactly at start +
  duration. Value at e: i = trunc(e × 193 / duration); if i ≥ 192 the end value, else start +
  trunc((end − start) × T[i] / 19200) with T[i] = 100 i (linear, 0), trunc(19200 log₁₀(1 + 9i/192))
  (log, 3), 19200 − log[192 − i] (exp, 2), trunc(9600 (1 − cos(π i / 192))) (sine, 4).
  *step* = trunc(duration × K / |end − start|) (|…| = 0 counts as 1), K = 100 for pitch bend, 3
  otherwise, at least 16 ticks.
- Event types: 3 pitch bend (value clamped 0…16383, 8192 centre), 4 controller (CC 1, 10, 91, 93
  clamped 0…127; CC 7, 11 the low byte; any other number — 64, 100, 101, 6, 38 in MM2 — sent as
  is), 5 channel pressure.
- Reset flag: if the curve is cut by a stop or transition before start + duration + reset
  duration, its reset value is sent just after the cut; otherwise the controller simply keeps the
  end value.
- A pattern entered in the middle (after catching up) first re-sends, at the entry time, the last
  curve of each kind that started before it (an ended one as an instant one).

### Music values

A note's 16-bit music value names a pitch relative to the chord. Fields: octave = bits 12–15,
chord position c = bits 8–11, scale offset s = bits 4–7, accidental a = bits 0–3.

**Fixed (play mode 0)**: the value is the MIDI key, lowered by 12 while above 127.

**Chord and scale (play mode 0x0E, every other MM2 note)**, with the chord's root *R*, chord
pattern (bit k = a tone k semitones above the root; 0 counts as 1) and scale pattern:

1. Octave adjustment *o* = 0. While the value ≥ 0xE000: add 0x1000 (16-bit) and *o* −= 12 (octave
   nibbles 14 and 15 mean −2 and −1).
2. If s ≥ 9: s −= 9 and *o* = −12 (replacing step 1's adjustment); then s = s & 7.
3. a: 0…8 as is, 9…15 mean −7…−1.
4. The scale as 32 bits: its low 12 bits rotated so bit 0 is the scale root (12-bit pattern 0 →
   C major), repeated (24 bits, then bits 24–31 = bits 12–19). A scale with 4 or fewer tones is
   replaced by the closest of the twelve 7-tone church modes (never with MM2's chord).
5. If c < number of chord tones: t = the semitone offset of the c-th chord tone (c = 0 first).
   If s = 0: r = *R* + t + a. Else: from pitch class (t + *R*) mod 12 walk up the scale, counting
   a scale tone at the start too, until the (s + 1)-th scale tone; d = semitones walked;
   r = *R* + t + d + a.
   Else (c ≥ tones): d = semitones from *R* mod 12 up to the (s + 2c + 1)-th scale tone (counting
   the start); r = *R* + d + a.
   (Past the 32 bits of the scale each further tone counts as one semitone.)
6. key = r + *o* + 12 × octave − 12.
7. A key outside 0…127 is not folded: the note is sent with key **0**.
8. **Inversion**: if the key is outside the part's bounds (inversion lower … upper), move it by
   whole octaves into them (up while below lower, down while above upper). (MM2's chords have
   inversion points 0xFFFFFF, which makes this a plain fold; MM2's inversion ids are 0.)
9. Lower by 12 while above 127.

With MM2's chord (*R* = 12, tones C E G B, C major) this is key = 12 × octave + {0, 4, 7, 11}[c]
+ a when s = 0; e.g. 0x3000 → 36, 0x3110 → 41 (E plus one scale step = F), 0x210F → 27,
0x4300 → 59, 0x510F → 63, 0x3400 → 50 (c ≥ 4: eight scale steps up from C), 0x30F0 → 35.

### The chord

A chord has a name, a key root, a key scale and one or more subchords (chord pattern, scale
pattern, inversion points, levels, chord root, scale root). With no chord track (every segment MM2
plays) the chord is the default: name "M7", key root 12, key scale 0xAB5AB5, one subchord with
chord pattern 0x891 (C E G B), scale 0xAB5AB5, inversion points 0xFFFFFF, all levels, chord root
12, scale root 0. Notes use the first subchord whose levels contain the part reference's subchord
level (0 in MM2), else subchord 0.

## Playing bands

A band plays at its time on the PChannels it names. Its instruments are taken in **reverse file
order**; each sends, at the band time:

| Flag bit set | Sends |
|---|---|
| 0x1 program | bank select (from the patch's MSB and LSB if bit 0x2 is set, else bank 0 0) then program change (patch & 0x7F); the drum bit is never sent |
| 0x80 transpose | sets the PChannel's transpose (added to every later note) |
| 0x40 volume | CC 7 = volume |
| 0x20 pan | CC 10 = pan |
| 0x800 channel priority | the synthesiser channel priority (never set in MM2) |

Note ranges (which would limit the regions downloaded) and assign patches are not enabled in
MM2's bands. So MM2's 0xE3 entries send bank, program, CC 7 and CC 10; 0x83 bank and program; 0x1E1 and 0x361 program (bank
0), CC 7, CC 10; 0x82 and 0x300 nothing.

**Downloading** (when a segment is loaded, every band of its band tracks; when a band file is
loaded): each instrument whose collection reference resolves is taken from that collection by
patch; one without a reference but with any of the GM, GS, XG bits (0x700) is taken from the
**General MIDI set** (below); if that fails and a GM/GS/XG bit is set, the patch & 0x7F is tried
(a drum patch becomes 0). The program bit is not looked at: MM2's 0x300 entries download GM piano
(patch 0) and the GM drum kit (0x80000000) although they send nothing. Downloaded instruments stay
in the synthesiser while the segment is loaded and are found by patch from any PChannel.

## Motifs

A motif is a pattern with embellishment 0x10, played as its own secondary segment: one track
playing that pattern (and its own band at −64 ticks, sent as it starts, if the pattern contains
one — MM2's do not), length = pattern measures × measure length, its repeats, start point, loop points
and resolution from the pattern's motif settings. The motif plays its pattern over and over for
the segment's length and repeats, choosing new variations at each lap; it uses no groove level and
no pattern choice. It follows the primary's tempo and chord (the default chord in MM2).

A secondary segment starts at the next boundary (beat when beat is asked, even with grid also
asked) of the primary playing at that time, counted from the render point (not *now*);
with no primary it starts at once. It runs to its own end; stopping or cutting the primary does not
stop it (a new primary cuts its sounding notes and it goes on, see "Starting a primary segment").

## Automatic transitions

A transition goes from what is playing to a new segment (or to silence) with a **command** and a
**boundary**. MM2 uses *groove at the next measure* (command groove, boundary measure) and *ending
at the next beat* (command end, boundary beat).

1. **When**: *now* rounded up to the boundary (as for a primary start). Let t₁ be one tick before
   that time (the time itself when *now* was already on the boundary). The **from** segment is the
   primary playing at t₁, and *e* = t₁ − the time at which its start point played: the elapsed
   play time, with the play start and loop jumps **not** added back, so it is the true segment
   position only for a segment that started at 0 and has not looped (after looping *e* can even
   exceed the length).
2. If nothing is playing, or the from segment has no style track: no transition. A new segment is
   started as a primary with the same boundary (no rounding when nothing plays, so it starts at
   *now*); without a new segment every segment, secondaries included, is stopped at that time.
3. Otherwise read from the from segment at segment time *e*: its style, its tempo (none: the new
   segment's tempo at 0, else 120) and its command (the last command at or before *e*; none: groove
   62) — its groove level *G*.
4. **Length**: groove command: 1 measure. End command: the longest end pattern (embellishment
   exactly 8) of the style, whatever its groove; 1 measure if it has none. (MM2: `BulletStyle` 2,
   `GrooverStyle` 2, `London` 2, `Pimphand` 4, `DuckStyle`, `Enemy`, `SunRoof` 1; the styles with
   no end pattern 1.)
5. **The transition segment**: that length (in the style's measures), repeats 0, start 0, and the
   tracks: the style (from time 0), one command at measure 0 (the transition's command at level
   *G*, range 0), one tempo (step 3's), one chord (the from segment's chord at *e*, the default in
   MM2), and a band track with the style's **first** band at −64 ticks (before the start, so it is sent as
   the transition starts, ahead of its first notes; it can change instruments and volumes for the
   transition).
6. It plays as the primary at the rounded time (the old segment stops one tick before, notes cut);
   the new segment is queued behind it and starts exactly when the transition ends, from its own
   play start with its own repeats. With no new segment the music ends with the transition.

What sounds in the transition follows "Composing from a style": for a groove transition a 1-measure
pattern covering *G* if there is one, else the first measure of the longest groove pattern
covering *G*, else a random pattern; for an end transition the longest end pattern covering *G*
(then groove patterns at *G* for any measures left), else one random pattern of the style (any
kind), again followed by groove choices for any measures left. With the groove levels MM2's
segments use, only `BulletStyle` and `GrooverStyle` have end patterns that cover them (groove
1…100); `Enemy` (100), `DuckStyle` (90), `SunRoof` (33), `Pimphand` (60) and `London` (100)
never match, and the other styles have none, so their endings are the transition's length (1–4
measures) of a randomly chosen pattern.

## Synthesiser

MM2's music synthesiser is a General MIDI style DLS level 1 software synthesiser with 2 channel
groups, at the sound system's mixing rate *fs* = 22,050 Hz, stereo, 41 voices, reverb on. The
rules below are given at *fs*; times derive from seconds and the reverb's delays from *fs*, so an
engine rendering at another rate uses the same formulas at its rate (rendering at 22,050 Hz and
resampling gives the closest sound, aliasing included).

**Channels.** PChannel p (0…31) is MIDI channel p mod 16 of group p ÷ 16 + 1. MIDI channel 10
(PChannels 9 and 25) of each group is a **drum channel**: its notes look for the patch with the drum
bit (0x80000000) added, and if not found the standard drum kit 0x80000000. Channel state at the
start: program 0, bank 0, CC 7 = 100, CC 10 = 64, CC 11 = 127, pitch bend 8192 with range 200
cents, CC 1 = 0, sustain off. Bank select (CC 0 MSB, CC 32 LSB) takes effect at the next program
change: patch = program | LSB << 8 | MSB << 16. State persists for the life of the music player.

**Instrument lookup.** A note-on looks for a downloaded instrument with the channel's patch that
has a region covering the key (velocity ranges 0 … 0 count as 0…127); collection instruments are
found before General MIDI ones of the same patch, and among collection instruments the latest
downloaded first (a collection's instrument that is already downloaded is not downloaded again and
keeps its place). Not found: drum channels try the standard kit; melodic channels stay silent.
Every region of the instrument covering the key starts a voice. A region whose option bit 1 is
clear first quick-stops voices of the same channel, key and region (3 of MM2's 381 regions);
key groups are 0 in MM2.

**Voice pitch.** In cents: (key − unity note) × 100 + fine tune + the channel's fine and coarse
tuning (RPN 1, 2; set at note-on), then per block the LFO, the pitch envelope and the pitch bend
((bend − 8192) × range) >> 13 are added. The playback step = 2^(cents/1200) × wave rate / *fs*
(the synthesiser computes it with tables in 1/4096 steps; the total is clamped to ±4800 cents).
Samples are read with **linear interpolation**; when the position reaches loop start + loop length
it moves back by the loop length (one extra sample equal to the loop start's follows the loop end,
0 after a one-shot's last sample). A one-shot voice ends when it runs out of data; a loop shorter
than 6 frames counts as a one-shot.

**Gain**, in hundredths of a decibel (0 = unity): base = V[velocity] + (wsmp attenuation × 10 >>
16); per block add the volume envelope (level × 96 / 10 − 9600), the LFO's volume, V[CC 11], V[CC
7] and +600. V[0] = −9600, V[v] = trunc(4000 log₁₀(v/127)) (V[64] = −1190, V[100] = −415). Pan
position = CC 10 + the articulation pan (trunc((scale >> 12) / 125), −64…64; ±64 for MM2's ±500),
clamped 0…127; left = base + P[127 − pan], right = base + P[pan] with P[0] = −2500, P[i] = trunc(1000 log₁₀(i/127)) (P[64] = −297). A channel's gain
in amplitude = 4095 × 10^(k/200) / 4096 with k = trunc(gain/10) clamped to −1000…0 (nothing ever
exceeds unity). Velocity always uses V; a velocity-to-attenuation connection is ignored.

**Articulation.** The region's art1 if it has one, else the instrument's (never merged); transforms
are ignored. Missing connections: times 0, sustains 100 %, LFO 5 Hz with no delay, depths 0, pan 0.
Times: samples = trunc(*fs* × 2^(scale / (1200 × 65536))) (scale 0x80000000 = 0). Sustain levels:
scale >> 16 in 0.1 % of the 96 dB range. LFO frequency: 440 × 2^((scale/65536 − 6900)/1200) Hz,
limited to 0.084…9.99 Hz; depths: pitch scale >> 16 cents, volume (scale × 10) >> 16.

**Envelopes.** Levels 0…1000 (1000 = full, 0 = −96 dB); u = samples since note-on.
- Volume envelope: attack and release first raised to at least *fs*/1000 samples; then attack ×
  2^(trunc(velocity × velocity-to-attack / 127)/1200) (so a scaled attack can end below that
  minimum) — and both envelopes' decay × 2^(trunc(key × key-to-decay / 127)/1200); the decay is
  then multiplied by (1000 − sustain)/1000.
- Attack: x = u × 1000 / attack (0…1000); volume level = A[x ÷ 5] with A[0] = 0, A[i] =
  trunc(1000 + (10000/96) log₁₀((i/200)²)) (rising linearly in amplitude); pitch level = x.
- Decay: 1000 − (1000 − sustain) × (time into decay) / decay; then the sustain level.
- Release (at note-off, level L₀ then): length R × L₀ / 1000; level L₀ × (1 − elapsed / that); the
  voice ends after the first block of the release at whose end the volume envelope alone (level ×
  96 / 10 − 9600, without velocity, controllers or LFO) is below −8000.
- Quick stop (stealing, exclusive retrigger, all sound off): like a release no longer than *fs*/70.
- Pitch envelope in cents = level × depth / 1000.
- LFO: a 256-step sine L[i] = trunc(100 sin(2π i/256)), 0 during the delay, then starting at phase
  0, not interpolated; pitch = (CC1 × mod-wheel pitch depth / 127 + LFO pitch depth) × L / 100;
  volume likewise.

**Blocks.** Voices are rendered in blocks that end at the earliest of 20 ms render chunks, 50 ms,
the note-off, and the next control point (each envelope segment end, ¼ and ½ of decay and release
segments longer than 100 samples, every ⅛ LFO period while it has depth); gains and the step are
computed at each block end and ramped from the previous values across the block. (An engine may
evaluate per sample; the ramps only smooth.) Controllers take effect from the next block end.

**Controllers.** CC 7, 11 and 10 apply to sounding voices at once; CC 1 only through mod-wheel
connections; CC 64 any non-zero value holds note-offs; RPN (CC 101/100 then CC 6): RPN 0 sets the
bend range to data × 100 cents; CC 120 and mono/poly quick-stop the channel; CC 123 releases (or
holds, with the pedal) its notes; CC 121 resets CC 11, bend, CC 1, pressure, sustain (and CC 7 =
100, CC 10 = 64 only when its value is not 0). Channel pressure has no effect.

**Voices.** 41 voices plus a reserve of 10. Channel priority: MIDI channel 10 highest
(0x80000010), then channels 1, 2, … 16 (0x8000000F … 0x80000001). A note-on with no free voice
(and none left in the reserve) takes one of equal or lower priority: released and quieter voices
first, else the oldest held one, cut at once; no candidate → the note is not played. Each render
keeps the reserve full by quick-stopping the lowest-priority released/quietest or oldest voices.
A note-off releases every voice of the oldest held note-on of that channel and key.

**Mix and reverb.** Voices are summed at half scale with 16-bit saturation (every add clamps to
−32768…32767); the reverb runs on that stereo mix in place (its outputs clamped the same way); then
every sample is doubled with saturation. Nothing wraps and nothing scales or limits the mix: with
MM2's data loud passages reach full scale and clip (about 1–3 % of the samples of `SunRoofStart`,
0.8 % of `EnemyStart`), and that is how MM2 sounds. The reverb is a fixed small-room
reverb on the whole mix (no per-channel send), input gain 0 dB, reverb mix −10 dB, reverb time
1000 ms, high-frequency ratio 0.001, built from these rules (integer arithmetic, Q15 coefficients
q(x) = trunc(clamp(x × 32768 + 0.5, −32768, 32767)), arithmetic shifts):
- delays: d0 = round(0.045 *fs*), d2 = round(d0 × 2^0.25), d1 = round(d2 × 2^0.25), d3 = round(d1 ×
  2^0.25); allpass delays c0 = round(0.230933 (d0 + d1)/2), c1 = round(0.230933 (d2 + d3)/2); line
  delays n0 = d0 − c0, n1 = d1 − c0, n2 = d2 − c1, n3 = d3 − c1 (at 22,050 Hz: 715, 1126, 851,
  1339; c0 = 277, c1 = 329);
- k = −3000 / (time_ms × *fs*); per-line gain G_i = 10^(k n_i); allpass gains g0 = 10^(k c0), g1 =
  10^(k c1); high-frequency taps: f = 1/hf − 1 (hf clamped 0.001…0.999), h_i = 10^(k (c + n_i) f),
  e_i = (1 − h_i)/2, b0_i = (1 − e_i) G_i, b1_i = e_i G_i;
- norm = √Σ_i (1 + t + t² + t³ + t⁴)², t = G_i × g (its pair's allpass gain); input gain I = 1
  (0 dB), mix M = 10^(−10/10); dry = √(1 − M) × I, wet = √M × I × 4 / norm, lattice a = 0.618034
  (at 22,050 Hz: dry 31086, wet 7893, a 20252, g 30044 / 29559, taps 13096 / 11514 / 12550 / 10771
  for both b0 and b1);
- per frame with input L, R: iL = L × wet >> 15, iR = R × wet >> 15; x_i = line i delayed n_i; each
  pair (x0, x1) with c0, g0 and (x2, x3) with c1, g1: p0, p1 = the pair buffer delayed c; y0 = (g p0
  + a x1) >> 15, y1 = (g p1 − a x0) >> 15; write (a y1 >> 15) + x0 and x1 − (a y0 >> 15) into the
  pair buffer; out L = sat(L × dry >> 15 + y0 + y2), R = sat(R × dry >> 15 + y1 + y3); feed line 0
  ← iL + ((y0 b0_0 + y0′ b1_0) >> 15), line 1 ← iR + (y1 …), line 2 ← (y2 …) − iR, line 3 ← iL +
  (y3 …), where y′ is the previous frame's value of the same output.

The music output then gets the music volume and balance of `../audio/music.md`.

### General MIDI instruments

Instruments a band takes from the General MIDI set come from the operating system's DirectMusic GM
collection (Roland's `gm.dls`), which MM2 does not ship. With MM2's data this concerns only: every
note on PChannels 9 and 25 (the drum channels: MM2's collections have no drum instruments, so they
play the GM standard kit — about 110 part notes in `Duck*` and `Enemy*` patterns), and melodic
channels with no program of their own (PChannel 16 in `DuckStart`/`Return`/`Cops`, 22 in
`PimphandIdleCops`, 194 part notes), which keep whatever program they last had — program 0 at
first, which finds GM piano because MM2's empty band entries download it.
**Our choice (Ant, 2026-10-15):** use the player's own `gm.dls` if they add it (loaded like their
other MM2 files; never shipped or downloaded by us), otherwise a bundled General MIDI drum kit and
piano trimmed from FluidR3 GM (Frank Wen, MIT licence; its copyright notice ships with the game;
Ant chose it 2026-10-25). Only these instruments come from that bank; everything else in MM2's music
comes from its own collections.

## Random numbers

Every random choice above (pattern, random variation, note time / duration / velocity ranges) takes
rand(): x ← x × 214013 + 2531011 (mod 2³²), value (x >> 16) & 0x7FFF. MM2's runtime keeps this
generator per thread and its starting state is not known (its loader seeds with the clock), so no
two plays of MM2 need agree. **Engine:** one generator per music player, started from the clock
(a check run starts it at 1); draws in playback order.

## Which runtime

These rules are those of the DirectMusic runtime of late 2000 (DirectX 8.0) driving MM2's DirectX 7
style content (MM2's style files are the older layout, so the style engine runs in its DirectX 7
mode: no variation seeds, no repeat modes, no destination grooves). MM2 was released for
DirectX 7.0a; later Windows versions ship later runtimes. Where those could differ, this page
follows the analysed runtime, notably the elapsed-time read of automatic transitions and the
random pattern choice.

## Out of scope

Not used by any file MM2 plays, so not specified: chord tracks other than the default chord
(`bigair.sgt` has one but is never played), chordmaps and signposts, composing segments from
templates, chord progressions and key modulation; fill, intro and break embellishments; groove
ranges and the performance's master groove offset; play modes with the key root (1, 5, 9) and
pedal point; inversion ids; mute, marker, lyric, script, sysex, segment-trigger and wave tracks;
sequence tracks (only in `bigair.sgt`); time-signature tracks; DirectX 8 additions (variation seeds
and repeat modes, destination grooves, audiopaths, DLS level 2 instruments and filters, band pitch
bend range); scripts and containers; synthesiser features MM2's data never reaches (key groups,
velocity layers, channel pressure connections, XG and GS modes).


## Checked against

- `tools/audio/dmusic.py` (survey) parses all 95 files with no RIFF errors and prints the chunk
  inventory above and every field range quoted on this page: 1,865 references (`LIST DMRF`: 62
  style, 1,803 collection), all found by file name with matching GUIDs; every band patch with a
  collection reference exists in that collection; variation choice words only 0x3FFFFFFF (3,497) and 0x20000000 (53,655); 1,557 parts
  with exactly one allowed variation under the default chord, 229 with several, none with none;
  command padding bytes 0xDF 0x6F in all 256 commands.
- `tools/audio/dmplay.py convcheck`: MM2's 155,817 notes use 85 distinct (music value, play mode,
  inversion bounds) combinations; all convert inside 0…127 (no note is sent as key 0), and the
  conversion of "Music values" agrees on all 85 with an independent emulation of the runtime's
  machine code.
- `tools/audio/dmplay.py notes` / `render` compose and synthesise segments with these rules
  (reverb and General MIDI instruments left out): `EnemyStart` chooses `Enenmy_intro` at measure 0
  (cut at measure 1 by the next command), then one of `EnemyPause_3` / `EnemyRace1.1` (groove 2,
  8 measures), `EnemyRace1.3` (56), `EnemyRace1.2` twice (54), and at the loop end re-chooses from
  measure 0 to the loop start; `DuckStart` plays `DuckIntro`, `DuckPart1`–`3`, `DuckPart5`–`7` as its
  commands step through groove levels 1, 2, 3, 4, 6, 7, 8; `SunRoofStart` plays the pattern `SunRoofStart`, then `SunRoofTheme`.
  `work/check/music_EnemyStart.wav` (25 s) and `music_SunRoofStart.wav` (20 s) are the renders.
