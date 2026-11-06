# Speech and one-shots: announcers, the Crash Course instructor, game and menu sounds

Race commentary, the Crash Course instructor, and the plain one-shot sounds of the race modes and
the menus. Everything here is 2D (no slot, no position, no pan, no echo) and uses the mixer of
`sound-system.md` (the sound-effects gain applies to speech too). The race modes themselves are a
later milestone (`../ROADMAP.md`, milestone 4); this page names each trigger in game terms so the
race-mode pages can wire it.

## Who gets speech

At the start of a game (after the player's car is set up), if the **commentary** option (bit
0x400, `sound-system.md` "Options") is on:

| Game mode (MM2 number) | Speech |
|---|---|
| Cruise (0), Checkpoint (1), Circuit (3), Blitz (4) | race announcer ("Race speech") |
| Crash Course (6) | instructor ("Crash Course") |
| Cops and Robbers (2) | its file is loaded but no line is ever played |
| any other (multiplayer roam, …) | none |

Race and Crash Course speech exist only when the city name is exactly `sf` or `london` (compared
without regard to case); any other city, including mod cities, gets **no speech** in MM2.

## The speech player

Each speech set is a list of **entries** (one per data row, numbered from 0 in load order across
all of its files) and a small **queue**.

- An **entry** is a clip name prefix and a clip range: clip *k* is the file
  `<prefix><NN>` with NN = *k* as two digits (`01` … `09`, then `10` …), in the set's sub-path with
  extension `.11K` (`sound-system.md`, "Finding a sound file"). Playing an entry picks the clip
  (below).
- **Clip choice**: k = trunc(rnd_s(add + 1, end + 0.99)) with *end* and *add* the entry's two
  numbers; if k equals the clip this entry played last and *end* > 1, k = k + 1, and if that
  exceeds trunc(*end*), k = 1. (So with add = 0 the clips are 1 … end, never the same twice in a
  row.)
- Clips are **streamed**: one voice for the whole set, volume 1.0 (the set's volume is set to 1.0
  after loading), no pitch control, no pan. Starting a clip while another plays stops the other.
  MM2 opens the clip's file and starts it within the play request itself (the frame waits for the
  disk), so a line never waits for its file. A missing (or unreadable) clip file simply plays
  nothing: the clip choice still counts as "the clip this entry played last", the voice is not
  playing, and the next due queued line can start at the next update. **Engine:** where a clip's
  file must first be fetched (browser), the line may start when its file arrives and count as
  speaking meanwhile, so the queue waits for it; after at most 3 s treat the file as missing.
- **Play an entry with delay *T***:
  - T = 0: stop the current clip and start the new one at once;
  - T > 0: put (entry, T) into the first free queue slot (none free: the request is dropped).
- **Queue slots**: a set starts with one; Crash Course and unlock rows add more as their files
  load ("Crash Course", "Race speech"). Every frame (also while the game is paused), for each slot
  in order: its delay decreases by the frame time; when no clip is playing and the slot holds an
  entry whose delay is ≤ 0, that entry is played now, the slot is emptied and the rest of the
  queue waits for the next frame. So a queued line waits both for its delay and for the current
  line to end.
- **Stop**: empties every queue slot and stops the current clip.

The "sound random" draws (`sound-system.md`) are taken from the wall clock, so draws made within
the same second share one value; e.g. the pre-race split and the entry choice below use the same
*r*.

## Race speech

### Voice

1. Read `aud/spchdata/<city>.csv`: line 1 is a title, line 2 holds the number of announcers *n*
   (first field), line 3 a title, line 4 the prefix (`AS` for `sf`, `AL` for `london`).
2. voice = trunc(rnd_s(1, n + 0.99)), so 1 … n.
3. If the game is a **minimal install**, voice = 1. MM2 asks the Windows registry
   (`InstalledGroup` of its install key equal to 1); when the key is missing it also counts as a
   minimal install, so a copied game without its installer always uses voice 1. **Engine:** treat
   it as a minimal install when the archive with the other voices (`mm2audex.ar`, which holds the
   voice folders 2 and up) is not present, otherwise use the drawn voice.
4. If the city is `sf` and voice = 3, voice = 4 (no SF voice 3 ships). SF thus uses 1, 2, 4, 5
   (4 twice as likely); London 1 … 6.
5. Voice name V = prefix + voice number (`AS4`, `AL2`); data folder `aud/spchdata/<V>/` (lower case
   in the archive), clip sub-path `aud11\<V>`, and every entry prefix is V + the row's prefix
   (`AL1` + `PRE` → clips `AL1PRE01` …).

Shipped: `sf.csv` 5 announcers, prefix AS; `london.csv` 6, prefix AL.

### Files loaded per game

In this order (each missing file is skipped):

1. The mode file: `cruise`, `checkpoint`, `circuit` or `blitz`.
2. Weather: `weaclr_prerace`, `weacldy_prerace`, `weafog_prerace`, `wearain_prerace` for weather
   0 … 3 (clear, cloudy, fog, rain; `../formats/night.md`).
3. Time of day: `timemorn_prerace` (0), `timenoon_prerace` (1), `timeeve_prerace` (2, **only in
   clear weather**; evening in other weather loads no time file), `timenight_prerace` (3).
4. The player's car (its base name, e.g. `vpbug`): `<car>_prerace`, then `<car>_results`.
5. `finallap`.
6. `cruisedamage` in Cruise, `racedamage` in the other modes.
7. Later, at the results screen, `<car>_unlock` or `<car>_texture` of a car that was just unlocked
   ("Results and unlocks").

### File format

Text, comma-separated, lines ending CR LF. Line 1 is a title (skipped). Every further line is
either a **category line**, whose first field contains the word `header` (any case, somewhere
after the start), or a **row**: prefix, *end*, *add* (numbers; further fields ignored).

A category line starts with the category name (compared over the name's length, any case):

| Category | Remembered as | Rows used |
|---|---|---|
| PRERACE | a range: first and last entry of the rows that follow | all |
| UNLOCKRACE, UNLOCKVEHICLE | a range, as PRERACE; each such category line also adds a queue slot from the second one on | all |
| UNLOCKTEXTURE, FINALCHECKPOINT, FINALLAP, RACEPROGRESS, RESULTSPOOR, RESULTSMID, RESULTSWIN, VEHICLERESULTSPOOR, VEHICLERESULTSMID, VEHICLERESULTSWIN, VEHICLEPRERACE, TIMEOFDAY, WEATHER, DAMAGEPENALTY | one entry: the first row after it | first only (others still become entries) |
| anything else | ignored; the rows after it still become entries | — |

A category seen again later (in a later file) replaces what was remembered. Every row of every
file becomes an entry, in load order, whatever its category. A category never seen has no entry
("none"); a range never seen counts as the range −1 … −1 (see the note in "Pre-race").

Example (`al1/checkpoint.csv`): PRERACE has rows `PRE,11,0` (clips 01–11) and `CHECKGEN,2,0`;
FINALCHECKPOINT `RACECHECK,3,0`; RESULTSPOOR `RESULTPOOR,15,0`; RESULTSMID `RESULTMID,10,0`;
RESULTSWIN `RESULTWIN,15,0`; UNLOCKRACE `UNLOCKRACE,1,0`.

### Lines and their triggers

**Pre-race** — at every start or restart of a game (the game reset):

1. x = rnd_s(11.5).
2. x ≤ 7.5: entry = trunc(rnd_s(first, last + 0.99)) of the PRERACE range.
3. 7.5 < x ≤ 8.5: the TIMEOFDAY entry; 8.5 < x ≤ 9.5: the WEATHER entry (nothing if none).
4. x > 9.5: the VEHICLEPRERACE entry, or if none, a PRERACE entry as in step 2.
5. Stop, then play the entry with delay **1.5 s**.

So about 65 % a general line, 9 % time of day, 9 % weather, 17 % the car's own line. (With no
PRERACE range at all, as for SF voice 1 in Cruise, step 2 draws in −1 … −0.01, which truncates to
entry 0: the first entry loaded, here the weather line. Reproduce it.)

**Final checkpoint** — when the player has passed every gate but the finish, i.e. the finish
becomes the only target (in Circuit: when the last gate before the line is passed on the last lap);
the music switches at the same moment (`music.md`). Stop, play the FINALCHECKPOINT entry at once.

**Damage penalty** — only in **Blitz**, when the player's car reaches its maximum damage
(`../physics/damage.md`): stop, play the DAMAGEPENALTY entry at once (the `racedamage` collision
lines). The `cruisedamage` lines are loaded but never played.

**Results** — see below. **Final lap**, **race progress** and **unlock race** lines are loaded but
MM2 never plays them (FINALLAP's clips `…RACELAPS0109`/`…0110` do not ship either; SF voice 2 has
no `finallap` file).

### Results and unlocks

When a race ends with the results screen (Checkpoint, Circuit, Blitz; for the player of the
current profile), MM2 first checks whether the result unlocks a car or a paint job (milestone 10):

- **A car unlocked**: load `<car>_unlock` (that car's base name), then play the UNLOCKVEHICLE
  range: entry = trunc(rnd_s(first, last + 0.99)), queued with delay **0.1 s** (no stop).
- **A paint job unlocked**: load `<car>_texture`, stop, play the UNLOCKTEXTURE entry at once.
- **Otherwise** the results line for the player's place *p* (1 = first) among *n* (the race's
  opponent-count setting, a whole number; integer division below):
  - p = 1: **win**: if rnd_s(10) > 5 and a VEHICLERESULTSWIN entry exists, that; else RESULTSWIN;
  - 0 < p − 1 < n / 2: **mid**: rnd_s(10) > 5 → VEHICLERESULTSMID, else RESULTSMID;
  - otherwise **poor**: rnd_s(10) > **8** → VEHICLERESULTSPOOR, else RESULTSPOOR;
  - then stop and play it at once (nothing if neither entry exists).

  So the car's own line comes 50 % of the time for a win or a middle place, 20 % for a poor one.
- Blitz run out of time: the poor results line is played at that moment (as p = 10, n = 10).

## Crash Course

Instructor sets: SF uses folder `aud/spchdata/ccs/` and clip sub-path `aud11\CCS`; London
`aud/spchdata/ccl/` and `aud11\CCL`. Entry prefixes are the row prefixes as they are (no voice
prefix): `CCS01INTRO` → clips `CCS01INTRO01` ….

1. Load `cc<s|l><R>.csv` with R the course number (0-based race index). Same file layout as race
   speech; categories (compared over their length): PRERACE (one entry: the first row), UNLOCK
   (one entry; adds a queue slot from the second on), CHECKPOINT (one entry), RESULTSPOOR,
   RESULTSWIN. A PRERACE row also adds trunc(end) − (trunc(add) + 1) + 1 queue slots.
2. For course 4 only, also load `cc_cpoint_waveinfo.csv` (a CHECKPOINT entry, `CCLLOCATION,22,0`
   in London) and `cc_cpoint_indexinfo.csv`: line 1 a title, then one integer per gate: the clip
   number to say at that gate, −1 for none. Only London ships them (gates 0–7: −1, 15, 3, 6, 5, 10,
   12, 13).

Lines:

- **Pre-race** (game start or restart): stop; play the PRERACE entry with delay 1.5 s; then the
  gate-0 line with delay 1.51 s.
- **Gate passed** (gate *g*): if the gate list exists and has an entry for *g* that is not −1,
  queue clip number *that* of the CHECKPOINT entry (no random choice) with delay 0.01 s.
- **Result**: passed → stop, play RESULTSWIN at once; failed → RESULTSPOOR.

UNLOCK lines and the shipped `<car>_texture.csv` files in these folders are never loaded.

## Cops and Robbers

`aud/spchdata/bullshit.csv` is loaded when the city is exactly `sf`, `aud/spchdata/CNRLONDON.csv`
otherwise (so `CNRSF.csv` is never read); their named categories (BLUETEAMHASGOLD, ROBGETLOOT …)
are never asked for: **no Cops-and-Robbers line is ever played**. Nothing to implement.

## Race-mode one-shots

Each mode loads one 2D sample bank **without pitch control**, priority 23, one file per instance:

| Instance | File | Volume | Modes |
|---|---|---|---|
| 0 | `Startracelow` | 0.9 | Checkpoint, Circuit, Blitz, Crash Course |
| 1 | `Startracehigh` | 0.9 | same |
| 2 | `Endofracetag` | 0.925 | same |
| 3 | `Youlose` | 0.925 | same |
| 4 | `Damgelose` | 0.925 | Checkpoint, Blitz, Crash Course |
| 4 or 5 | `Messagenote` | 0.9 | Circuit (4), the others (5) |
| 6 | `Timerwarning` | 0.9 | Blitz, Crash Course |

Cruise loads only `Messagenote` (priority 23, volume left at full); multiplayer modes `Startracehigh`
(0.98 in Cops and Robbers, 0.9 in multiplayer Cruise). The gates use two more samples, `Waypoint`
and `Lastwaypoint` (volume 0.95, Crash Course 0.91; priority 23; no pitch control).

Triggers (game terms; each "play" plays once):

- **Countdown** (Checkpoint, Circuit): when the pre-race camera hands over, play `Startracelow`
  and start a 2.5 s count; at 1.25 s left play `Startracelow` again; at 0 play `Startracehigh`
  (GO: the race clock starts). Blitz: the same with a 5.0 s count (low at 0 s, low again at
  3.75 s, high at 5.0 s). Crash Course (`../game/crash-course.md`): a course's first event shows
  the course name for 1.25 s, then counts 5.0 s like Blitz, but 2.5 s (low, low again at 1.25 s,
  high at 2.5 s) in the checkpoint events (kinds 7 and 9); the clean-driving kind (1) counts 5.0 s
  at once and the accelerate kind (6) always counts after the name; any other later event of a
  course has no count, only `Startracehigh` at its go.
- **Finish**: first place → `Endofracetag`; any other place → `Youlose`. Blitz: finishing in time
  → `Endofracetag`; time running out → `Youlose`.
- **Damaged out** (the player's car at maximum damage, `../physics/damage.md`): instance 4, which
  is `Damgelose` in Checkpoint and Blitz but `Messagenote` in Circuit (MM2 plays the message chime
  there; reproduce it); in Checkpoint and Blitz the player's engine sound is also silenced
  (`car-sounds.md`). Crash Course by event kind: `Damgelose` in the jump (0) and checkpoint (7, 9)
  events, `Messagenote` (instance 5) in clean driving (1), `Youlose` in no contact (5), no sound in
  the others (2, 3, 4, 6, 8). Landing in water in a Crash Course plays `Damgelose`.
- **Timer warning** (Blitz; in the Crash Course only the clean-driving (1) and no-contact (5)
  events) while the race clock shows less than 10 s and has not run out: with more than 3 s
  left, play `Timerwarning` once per second (a one-second accumulator, starting full so the first
  beep comes at once; a beep is skipped if the last one still plays); with 3 s or less, play it
  **looped** until the race ends; stop it when the race ends or the time runs out.
- **Gate passed** (the gate system of the race modes): Checkpoint races play `Waypoint` for every
  gate except the one that leaves only the finish; Blitz plays `Waypoint` for every gate; Circuit
  plays `Lastwaypoint` when the start/finish gate is passed (a lap completed) and `Waypoint` for
  the others; Crash Course plays `Waypoint` for every gate. (The Crash Course's pass and fail
  sounds per event kind: `../game/crash-course.md`.)
- **Messagenote**: the HUD message chime (race-mode pages: when a HUD message with sound is
  shown).
- `Carhorn1double` (volume 0.85, priority 23) is loaded by the HUD only in network games; where
  it plays was not traced (multiplayer milestone).

## Menu one-shots

Our menus have their own design but play these sounds for the same actions (all 2D, no pitch
control; each restarts from the beginning when played again):

| Action | File | Volume |
|---|---|---|
| focus moves to another item | `Moveselector` | 0.75 |
| a value or item is chosen (buttons, dropdowns, rollers) | `Selectionmade` | 0.75 |
| a slider moves | `Switch` | 0.85 |
| entering a main-menu section: driver, vehicles, options, races, multiplayer, records, replay | `UIdriver` 0.93, `UIvehicles` 0.9, `UIoptions` 0.9, `UIraces` 0.9, `UImulti` 0.87, `UIrecords` 0.84, `UIreplay` 0.84 | as given |
| choosing a race setting: weather sunny, cloudy, rain, snow; time morning, noon, sunset, night; start | `Uisunny`, `Uicloudy`, `Uirain`, `Uisnow`, `Uimorning`, `Uinoon`, `Uisunset`, `Uinight`, `Uigo` | 0.9 |
| a car is shown in the vehicle menu | `<car>_select` (the car's base name from `tune/<car>.info`, e.g. `vpbug_select`) | 0.91 |

A voice clip is not restarted while it still plays. A mod car gets its clip if it ships
`aud/aud22/<car>_select.22k.wav`. (`UIreplay` has no shipped file; the old `AudiSelect`-style
files are never named.)

## Checked against

- The program: speech set-up (mode list, `sf`/`london` test, voice draw with +0.99, minimal
  install from the registry with a missing key counting as minimal, SF 3 → 4), the category
  parser and every play function (thresholds 7.5 / 8.5 / 9.5 of 11.5, 5 and 8 of 10, delays 1.5 and
  0.1, results rule with integer n / 2), their callers (game reset, gates, Blitz damage, results and
  unlock flow; no caller for final lap, race progress, unlock race and Cops and Robbers), the clip
  choice (no-repeat step), the stream play (file opened and started inside the request, nothing
  played on a failed open), the queue, the Crash Course loader and gate list, the mode one-shot banks
  and their volumes, the countdown timers (2.5 / 1.25 and 5.0 / 1.25; Crash Course kinds 7 and 9:
  2.5 / 1.25), the timer-warning rule
  (10 s, 3 s, 1 s), the gate sounds per gate kind, the menu sound selector and the vehicle-select
  name `%s_select`.
- Data (`tools/audio/speech.py`, `sounds.py`): SF voices 1, 2, 4, 5 and London 1–6; per voice and
  mode the category entries (e.g. Checkpoint: PRERACE entries 0–1, FINALCHECKPOINT 2, RESULTSPOOR 3,
  RESULTSMID 4, RESULTSWIN 5 for every voice); 2,579 race-speech clip references (1,590 distinct
  files, 1,503 found; the missing ones are in never-played categories — `…DAMAGE01/02`,
  `…RACELAPS0109/0110`, SF voice 1 collision lines — or a few unlock/vehicle lines and AL5's fog
  lines); Crash Course 256 references, 3 missing (`CCS06FAIL02`, `CCS07FAIL03`, `CCL08FAIL05`);
  the London gate list (7 clips, all present); every one-shot and all 20 `<car>_select` files found
  except `UIreplay`.
