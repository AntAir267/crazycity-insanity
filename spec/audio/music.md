# Music: what plays when

MM2's soundtrack is interactive DirectMusic music: segments composed from styles, played by a
software synthesiser from DLS instrument collections. This page gives the game's rules (which
table and row, which segment for which situation, how switches happen, volume). How a segment
turns into notes and sound is `../formats/directmusic.md`; the words **primary**, **secondary**,
**beat**, **measure**, **motif** and **automatic transition** are defined there.

Related pages: the sound options, the sound random draw *r* and the echo state are
`sound-system.md`; the chase count is `traffic-sounds.md` ("The chase count"); the city ambience
sound objects of the ambient option are `world-sounds.md`. *F* is the frame time
(`../physics/vehicle.md`, "Time step").

## The music option

The player file's option bits (`sound-system.md`, "Options in the player file") choose one of
three: **music** (bit 0x004), **ambient** (bit 0x800), or neither. The menu keeps them exclusive
(turning one on turns the other off). A new player has ambient on and music off. The music volume
(0…1, default 1.0) and the balance (−1…1, default 0) come from the same file. The command-line
switches `nomusic` and `noaudio` turn the whole music system off.

## The music player

There is at most one music player. It is made for a phase (the menus, or one game) and thrown
away, with everything it plays stopped, when the phase ends. It holds:

- a row of **slots**, each one segment file (`aud/dmusic/<name>.sgt`) or empty;
- one **motif** slot and one **band** (games with music only);
- *current* and *previous* slot numbers (both 0 when made);
- the game state below.

Loading a slot: the name gets `.sgt` appended unless it already ends in exactly `.sgt` or `.SGT`
(other spellings such as `.Sgt` get a second extension and fail); the file is looked up in
`aud/dmusic/` without regard to case (`../formats/ar.md`) and loaded with all instruments its band
tracks name (`../formats/directmusic.md`, "Playing bands"). A missing or broken file leaves the slot
empty; playing an empty slot does nothing.

Every player starts its synthesiser with 2 channel groups (PChannels 0–31), at most 41 voices,
reverb on, at the sound system's mixing rate (22,050 Hz) (`../formats/directmusic.md`,
"Synthesiser").

## Music tables

Tables are `aud/dmusic/csv_files/<name>.csv`, read line by line (a line ends at LF; CR and LF
are dropped; at most 999 characters). The first line is a header and is ignored: **columns are
taken by position**, never by their header text. The number of data rows *n* is the number of
lines after the first (a blank line counts as a row). Fields are separated by commas.

The row is chosen once, when the player is made:

    row = trunc(r × ((n − 0.01) × 0.01) × 100)

with *r* the sound random draw (`sound-system.md`, "Sound random numbers"; 0.01 and 100 are
single-precision constants, the rest extended precision, truncation toward zero), so each row is
equally likely. Then row `row` (0 = the first data row) is used.

| Table | Used by | Columns (by position) |
|---|---|---|
| `singlerace` | Checkpoint, Blitz, Circuit, Crash Course; multiplayer Checkpoint, Blitz, Circuit | 1 start, 2 return, 3 idle, 4 idle-cop, 5 chase, 6 pause, 7 results, 8 motif style, 9 motif name, 10 motif band |
| `singleroam` | Cruise; multiplayer Cruise and Cops and Robbers | 1 start, 2 return, 3 idle, 4 chase, 5 idle-cop (loaded, never used), 6 pause, 7 motif style, 8 motif name, 9 motif band |
| `ui` | menus | 1 segment (first data row only, no random choice) |
| `sfambience` / `londonambience` | ambient option, single-player games | 1 segment |

The ambience table is `sfambience` when the level name (`MapName`) is exactly `sf` (byte for byte),
otherwise `londonambience` (London and every other city). Multiplayer games have no ambience
table (the ambient option gives them no music at all).

Shipped rows (file names as written in the tables; lookups ignore case):

| `singlerace` row | start | return | idle | idle-cop | chase | results |
|---|---|---|---|---|---|---|
| 0 | EnemyStart | EnemyReturn | EnemyIdle | EnemyIdleCops | EnemyCops | Results1 |
| 1 | BulletStart | BulletReturn | BulletIdle | BulletIdleCops | BulletCops | Results2 |
| 2 | DuckStart | DuckReturn | DuckIdle | DuckIdleCops | DuckCops | Results1 |
| 3 | LondonStart | LondonReturn | LondonIdle | LondonCopsIdle | LondonCops | results2 |
| 4 | PimpHandStart | PimpHandReturn | PimpHandIdle | PimpHandIdleCops | PimpHandCops | results1 |

| `singleroam` row | start | return | idle | chase | (idle-cop) |
|---|---|---|---|---|---|
| 0 | SunroofStart | SunRoofReturn | SunRoofIdle | SunRoofCops | SunRoofIdle |
| 1 | TacoLocoStart | TacoLocoRestart | TacoLocoIdle | TacoLocoCops | TacoLocoCopsIdle |
| 2 | LeTigreStart | LeTigreRestart | LeTigreIdle | LeTigreCops | LeTigreCopsIdle |
| 3 | BackOffStart | BackOffRestart | BackOffIdle | BackOffCops | BackOffCopsIdle |

Every row of both tables has pause `Pause`, motif style `GrooverStyle`, motif `BigAir` and band
`BigAir`. `ui` names `UI`; `sfambience` names `SFAMbience`; `londonambience` names
`LondonAmbience`. The race table does not depend on the city: London music can play in San
Francisco and the other way round.

## Setting up a game's music

When a game mode initialises (before its countdown), if music is on:

1. Make the player with 7 slots (race table) or 6 (roam table) and set its volume (below).
2. Read the table and pick the row. Load the slots:

| Role | Race slot ← column | Roam slot ← column |
|---|---|---|
| start | 0 ← 1 | 0 ← 1 |
| chase | 1 ← 5 | 2 ← 4 |
| return | 2 ← 2 | 1 ← 2 |
| pause | 3 ← 6 | 5 ← 6 |
| idle | 4 ← 3 | 3 ← 3 |
| idle-cop | 5 ← 4 | 4 ← 5 (never played) |
| results | 6 ← 7 | none |

   The slots are loaded, and their instruments downloaded, one after another: race table in the
   order of the rows above (slot 0 to 6), roam table in column order (start, return, idle, chase,
   idle-cop, pause); then steps 3 and 4. The order matters only where two collections hold the same
   patch (`../formats/directmusic.md`, "Instrument lookup"); no shipped row has such a patch.
3. Load the motif: the named motif of the style file `<motif style>.sty` (`.sty` appended unless
   the name ends in exactly `.sty`/`.STY`; `../formats/directmusic.md`, "Motifs").
4. Load the band `<motif band>.bnd` (same extension rule), download its instruments, and play it
   as a secondary segment (nothing plays yet, so it is sent at once; `../formats/directmusic.md`,
   "Playing bands", "Motifs"). The shipped `BigAir.bnd` sets PChannels 30 and 31, the two
   channels of the `BigAir` motif; no other band touches them, so they keep it all game.
5. The player's state: current = previous = 0; started = no; clock = 0; idle timer = 10000;
   previous chase count = 0; big-air latch off; **idle blocked** = no. Race-table games also mark
   "block idle at start" (Cruise does not).

With the ambient option (and music off) a single-player game instead makes a player with one
slot from its ambience table; there is no motif, band or game state beyond "started" and
"stopped for a tunnel" (off).

## Kinds of switch

- **Cut to slot s.** Nothing happens if s is the current slot, s is not a slot, or the current
  slot is empty (MM2 stops the current slot's segment first and gives up when it has none).
  Otherwise: stop the current slot's segment at once (`../formats/directmusic.md`, "Stopping");
  previous ← current; current ← s; then **start** slot s.
- **Start slot s** (the current slot): unless its segment is already playing, set its repeat
  count to 1,073,741,824 (2³⁰, i.e. for ever) and play it as the primary segment from the next
  beat (`../formats/directmusic.md`, "Starting a primary segment"). After a cut that is the next
  beat of the music just stopped, so a cut lands on the old music's beat; with nothing playing it
  starts at once. It always starts from its own play start point. **Already playing** means: the
  segment has a start that the **present** (`../formats/directmusic.md`, "Playback: time") has
  reached, and has not ended or been stopped before the present. A segment still waiting for its
  start (queued behind a transition, or due at a beat not yet reached) is not playing; one stopped
  at *now* still is until the present reaches *now*. So a cut back to a slot less than about 0.1 s
  after a cut away from it (a chase count of 1 for one frame, a pause menu closed at once) starts
  nothing: that slot's segment ends at its stop time and the music is silent until the next
  switch.
- **Transition to slot s with (command, boundary)**: nothing if s is current. Otherwise the
  automatic transition from whatever is playing to slot s's segment (`../formats/directmusic.md`,
  "Automatic transitions"); previous ← current; current ← s. Two kinds are used:
  - *groove at the next measure*: at the next measure of the old music, one measure composed from
    the old segment's style at its groove level, then slot s's segment from its play start;
  - *ending at the next beat*: at the next beat, the old style's ending (1–4 measures; for
    styles whose endings do not fit the groove level, as many measures of a randomly chosen
    pattern), then slot s's segment.
  With nothing playing there is no transition: slot s's segment simply starts at once. The
  segment keeps the repeat count it has (the file's value, or 2³⁰ if it was started before).
  If slot s is empty, it is a transition to nothing (as "Stop with an ending", with this
  transition's command and boundary: a groove transition plays its one measure, then silence),
  and previous ← current; current ← s as usual. (MM2 then stores an invalid pointer for that slot
  and uses it at the next transition to the same slot, a crash; an engine treats that transition
  like the first.)
- **Stop at once**: stop the current slot's segment immediately.
- **Stop with an ending**: the automatic transition to nothing, *ending at the next beat*: the
  ending plays, then silence; with nothing playing every segment, the motif included, stops at
  *now* (`../formats/directmusic.md`, "Automatic transitions"). The current slot does not change.

## Each frame of a game

The game mode runs this once per frame, after its own update and before the cars, the city and
the physics steps of the frame (so it sees the car values of the previous frame). Nothing runs
while the game is paused.

1. clock ← clock + *F*; if the clock is negative, set it to 100.
2. If music has not started yet: run "Starting the music" and stop for this frame.
3. Ambient option: run "Ambient instead of music" and stop.
4. Otherwise run, in this order, "Idle", "Police chase" and "Big air" with the player car's speed
   (`../physics/vehicle.md` step 2, m/s), the chase count and the car's airborne flag (below).

### Starting the music

When clock ≥ 1.25 s (first frame where it holds):

- Music: if the game has "block idle at start", set idle blocked. Make the start slot current and
  **start** it (nothing plays yet, so it starts at once: 1.25 s of unpaused game time after the
  player was made).
- Ambient: start slot 0 unless the echo state is on (car in an enclosed room).
- Then idle timer ← 10000, started = yes.

### Idle

Skip this rule entirely if the current slot is the results slot, or the game has no idle slot or
no return slot. With speed *v*:

- If *v* ≤ 5.0 m/s and idle is not blocked:
  1. If idle timer ≥ 5.0 and the current slot is none of idle, idle-cop, pause: if the current slot
     is not the chase slot, **transition** to idle (*groove at the next measure*); if it is the
     chase slot and the game has an idle-cop slot, transition to idle-cop (same kind).
  2. idle timer ← idle timer + *F* (single precision).
- Otherwise (faster, or idle blocked): if idle timer is exactly 0, nothing. Else: if the current
  slot is idle, transition to return; if it is idle-cop, transition to chase (both *groove at the
  next measure*); then idle timer ← 0.

Consequences worth knowing: the timer starts at 10000, so a car standing still when the Cruise
music starts goes to idle music at the first measure; in race modes idle is blocked until "GO",
and the blocked branch clears the timer on the first frame, so idle needs 5 s of slow driving
after the start. In Crash Course nothing ever clears the block: Crash Course never plays idle
music. Cruise has no idle-cop slot, so a slow car during a Cruise chase keeps the chase music.

### Police chase

With the chase count *c* (`traffic-sounds.md`) and the previous frame's count *p*:

- *c* = 1 and *p* = 0: **cut** to the chase slot;
- *c* = 0 and *p* = 1: **cut** to the return slot;
- any other change (0 → 2, 2 → 0, 3 → 1, …): nothing.

Then *p* ← *c*. Cuts happen whatever is playing, idle and pause included.

### Big air

The car's **airborne flag** (below). On a frame where it is on and the latch is off, play the
motif as a secondary segment (repeat count 1, so it plays twice: `BigAir` is one measure, heard
twice; it starts at the next beat of the playing music, `../formats/directmusic.md`, "Motifs")
and set the latch. On a frame where it is off, clear the latch. One jump plays the motif once,
however long it lasts. The motif sounds over whatever primary music plays and is not stopped by
switches (a transition, the start of the segment queued behind it, or a cut while a transition
segment is still playing cuts its notes sounding at that moment; a plain cut does not).

**Airborne flag** (kept by the player car's sound update each frame; the player's car only —
opponents and traffic skip the test):

1. Count the car's wheels whose "grounded" flag from the last physics step is set
   (`../physics/vehicle-wheels.md`).
2. Two or more: flag off. Exactly one: flag off.
3. None: probe straight down below the body position *p* (`../physics/vehicle-body.md`) in ten
   steps of 3.0 m: segment k (k = 1…10) runs from y_a to y_b at the car's x and z, where
   y_a and y_b start at p.y and p.y − 3.0 and each step first subtracts 3.0 from both (single
   precision), so segment 1 is p.y − 3 … p.y − 6 and segment 10 is p.y − 30 … p.y − 33. Each is a
   ground probe (`../physics/collision.md`, "Ground queries"; mask "ground", ignoring the car,
   fresh probe state for the frame that carries its rooms from one segment to the next). At the
   first segment that hits, set the flag on. If none hits, leave the flag as it was.

So the flag turns on only when all four wheels are off the ground and there is ground between
3 m and 33 m below the car; it stays on through higher flight and turns off on touching down.

## Race events

All of these are skipped with the ambient option.

| Event | Music |
|---|---|
| Race countdown reaches "GO" (Checkpoint, Blitz, Circuit, multiplayer Checkpoint/Blitz/Circuit) | idle blocked ← no |
| Circuit (also multiplayer Circuit): the start/finish gate is crossed and the final lap begins | **transition** to the chase slot, *ending at the next beat* |
| Circuit (also multiplayer): the last gate before the finish is crossed on the final lap | the same (does nothing if the chase slot is already current) |
| Checkpoint (also multiplayer Checkpoint): a checkpoint is taken and only the finish remains | the same (MM2 skips it when the game-mode number is that of Circuit, which never happens here) |
| The player finishes (Checkpoint, Blitz, Circuit) | **stop at once** |
| The player's car is wrecked (Checkpoint, Blitz) | **stop with an ending** |
| Blitz time runs out; Circuit wreck (the car is repaired and the race goes on) | nothing |
| The results screen opens (every mode with results, Crash Course included) | **transition** to the results slot, *ending at the next beat*; the finish stopped the music some seconds earlier, so normally nothing plays and the results segment just starts at once |

Blitz and Crash Course have no final-checkpoint music. A failed race (wreck, time out) ends in
the pause menu, so its music is the pause music.

## Pause

When a pause or chat menu opens during a game: **cut** to the pause slot (ambient: stop at once).
When it is closed to resume: **cut** to the previous slot (ambient: start slot 0 again). The
segment so resumed starts again from its own start point, not where it was cut. While the game is
paused nothing above runs (no idle timer, no chase rule).

## Ambient instead of music

The ambience segment plays only outside enclosed rooms. Each frame (not paused, after starting):

- echo state on and not "stopped for a tunnel": stop at once; stopped for a tunnel ← yes;
- echo state off and stopped for a tunnel: start slot 0 again (from its start point); stopped for
  a tunnel ← no.

The results screen stops the ambience at once. The ambience never changes with speed, chases or
jumps. (`UndergrounAmbience.sgt` and its style and DLS exist but no table or rule names them.)

## Menus

When the menus start (and no player exists), with music on: make a player with one slot, set its
volume, load the `ui` table's segment into slot 0 and start it (no 1.25 s wait; no frame rules).
In the audio options, turning music on starts it the same way (making the player if needed);
turning it off, or choosing ambient, stops it at once. The in-game audio options have only the
sound-effects volume, music volume and balance sliders, so the music option never changes during
a game.

## CD player

The in-game CD player (`../engine/controls.md`, key 3) silences the music while a CD plays: with
music on, starting the CD deactivates the synthesiser (everything goes silent, timing goes on) and
stopping the CD activates it again. A browser build without a CD player has nothing to do.

## Volume and balance

The music output has one level for everything it plays, from the music volume option *m* (0…1;
clamped), with the same curve as the sound-effects gain of `sound-system.md` ("Volume"):

    g_m = log(200 × m) / log(200)   (m > 0),      silent (m = 0)
    level (dB) = (g_m − 1) × 100

MM2 hands its output trunc((g_m − 1) × 10000) hundredths of a decibel (200 × m rounded to single
precision first; the logarithms, × 10000 and − 10000 in extended precision; truncation toward
zero). So m = 1 is 0 dB, 0.5 is −13.08 dB, 0.1 is −43.45 dB. For 0 < m < 0.005 the value is below
−100 dB and MM2's output refuses it, keeping the level it had; an engine may treat that as silence.
The level is set when the player is made and whenever the options are applied or the music slider
moves (the in-game slider also sets the CD-audio volume).

The balance option *b* reaches the music twice: like all wave output it is balanced by the global
balance of `sound-system.md` ("Balance"); and whenever the balance slider is moved (in the menu
or in the game's options, not when a player file is applied or a music player is made) the music
output is also panned by *b* with the decibel pan law of `sound-system.md` ("Pan"), handed over as
trunc(b × 10000) hundredths. That pan stays for the life of the music player.

## Unused

Data no rule or table reaches (the tables name 50 of the 62 segments): segments
`GrooverStart`, `GrooverReturn`, `GrooverIdle`, `GrooverCops`, `LondonTrans1`, `NightRoamStart`,
`SunIdleCops`, `UndergrounAmbience`, `UseForSomething`, `Vanilla1`, `Vanilla2`, `bigair`; styles
`GrooverCops`, `BigAir`, `UndergroundAmbience`; `Pimphand.bnd`; `UndergroundAmb.dls`; the styles'
bands other than their first, and every motif except `GrooverStyle`'s `BigAir`. MM2's program also holds code that is never called: a speed-to-groove-level table, a
music echo, master tempo and volume scaling and a streaming-music player. None of it is part of
this spec.

## Checked against

`tools/audio/dmusic.py tables` reads the five tables by position: `singlerace` 5 rows × 10
columns, `singleroam` 4 × 9, the three single-segment tables 1 row each; every named segment,
style and band exists in `aud/dmusic/` (looked up without regard to case: `PimpHandStart` →
`PimphandStart.sgt`, `BackOffRestart` → `BackOffReStart.sgt`, `results2` → `Results2.sgt`). The
motif `BigAir` exists in `GrooverStyle.sty` (1 bar, PChannels 30 and 31, one note each) and
`BigAir.bnd` sets exactly those two PChannels (patch bank 1 program 24 of `DLS Collection1.dls`,
pans 0 and 127). Start segments with play start points and loops as the tool lists them, e.g.
`TacoLocoRestart` starts at tick 239616 of 313344 and loops 30720…313344 (99 repeats).
