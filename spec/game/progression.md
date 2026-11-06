# Progression: drivers, records, unlocks, rewards and saving

What MM2 does with its player files (layouts in `../formats/profiles.md`): which races are open,
which cars and paint jobs are locked, what a finished race writes into the records and the hall of
fame, when a reward is earned, and when each file is saved. The race modes themselves (finishing,
places, timers, laps) are the race-modes spec (`../ROADMAP.md`, milestone 4); this page starts
at "the player has finished". The menus that show all this are `menus.md`.

Words used here: a **driver** is a profile (one entry of `players.dir`); the **current driver** is
the one selected in the menus; the race types are numbered as in `../formats/profiles.md`, "Game
mode numbers" (1 checkpoint, 3 circuit, 4 blitz, 6 crash course). A driver's **record file** for a
city is `players/<RaceDir>/<file>.rec`; it is **valid** when its header passes the reading rules
(version, check value, stamp equal to the driver's). *P*<sub>t</sub> is the passed-bits word of
type *t* in it, *N*<sub>t</sub> the race count of type *t* in its header, and passed(*t*) the
number of set bits among bits 0 … *N*<sub>t</sub> − 1 of *P*<sub>t</sub>.

## Which cities keep records (an MM2 limit)

MM2 names the two shipped cities in its code: it creates record files for `sf` and `london` only
(when a driver is created or selected), resolves car locks from those two only, and its race menus
use records only when the current city's `RaceDir` is `sf` or `london`. A mod city therefore never
gets a record file from MM2, and in a mod city:

- every race and every crash course is open, every race's settings can be changed;
- finishing a race records nothing and earns no reward (the record file does not exist);
- the hall of fame still works if its files exist (they are created when that city's race
  statistics are first shown).

Our engine follows this. (Keeping records for mod cities with the same layout would be an easy
extension, but it is not MM2's behaviour; do it only if asked.)

## Open races

Computed whenever the race menu is shown, from the current driver's valid record file of the
current city (`sf` or `london`):

**Checkpoint races** (12 per shipped city), a bit mask of open races:

1. open = races 0, 1, 2 (mask 0x007);
2. if races 0, 1, 2 are all passed: open = races 0 – 5 (0x03F);
3. if races 3, 4, 5 are all passed: add races 6 – 8 (0x1C0);
4. if races 6, 7, 8 are all passed: add races 9 – 11 (0xE00).

Each step looks only at its own three passed bits. Races numbered 12 and up never open this way.

**Crash courses** (13 slots):

1. open = courses 0 – 2, 4 – 6 and 8 – 10 (0x777);
2. if courses 0 – 2 are all passed: open = 0x77F (adds course 3);
3. if courses 4 – 6 are all passed: add course 7 (0x080);
4. if courses 8 – 10 are all passed: add course 11 (0x800);
5. if courses 0 – 11 are all passed: everything is open (course 12 and any further).

**Circuit and blitz races** are always all open.

Without a driver, without a valid record file, or in a mod city, everything is open.

## Locked cars and paint jobs

A car (or one of its paint jobs) is locked when a line of a rewards file names it and that line's
condition is not yet met. The locks are worked out afresh, never stored:

1. Mark every car and every paint job unlocked.
2. For each of the cities `sf` then `london` (fixed, see above): read `race/<c>/<c>_rewards.csv`
   (`../formats/profiles.md`) and the current driver's record file for that city. If the file or
   the record file cannot be read, skip the city (its rewards stay unlocked).
3. For each rewards line (type *t*, condition, car, variant), the condition is **met** when:
   - `half`: passed(*t*) ≥ ⌊*N*<sub>t</sub> / 2⌋ (5 of 10, 6 of 12);
   - `all`: passed(*t*) = *N*<sub>t</sub>;
   - a number *r*: bit *r* of *P*<sub>t</sub> is set.

   If it is not met: variant 0 locks the car; variant *v* > 0 locks paint job *v* of that car.

This runs whenever a driver is applied: at program start, when a driver is created, selected or
deleted, and each time the menus are entered. When a game starts, MM2 rebuilds its car list (all
unlocked) and applies step 3 for the **current city only**; that is the state the reward check
below sees. A debug switch can skip step 2 (all unlocked); it is not reachable from the menus.

Shipped result for a new driver: locked cars vp4x4, vpauditt, vpcoop2k, vpdb7, vpdune,
vppanozgt, vpsemi, vpvwcup; locked paint jobs vpauditt 4, vpbullet 4, vpcab 1, vpcoop 4,
vpcoop2k 4, vpddbus 3, vpdune 4, vppanozgt 4, vpsemi 2, vpvwcup 4, 5 and 6. The shipped `DriverX`
(two SF blitz races passed) has the same locks.

How the car menu treats a locked car or paint job is in `menus.md`.

## After a race: what is recorded

Nothing is recorded without a current driver, and nothing once a chat cheat code has been typed
in this session (`menus.md`, "Cheats and hidden switches"): the session counts as cheating until
the program ends. One exception: Checkpoint and Blitz skip the recording step when there is no
current driver, but Circuit runs it anyway. MM2 is without a current driver only after the
selected driver's `.sav` failed to load (the list is never empty: MM2 never deletes the last driver,
`../formats/profiles.md`). Which record file is then valid at game start, and the name a Circuit
result enters the hall of fame under (by the rules below, if that file could be read at game start),
come from the driver data the failed load left in memory: after a check-value failure the failed
file's own fields (so the kept last driver's record file can be valid and receive the Circuit
result), after a missing
or too-old file the previous driver's (at program start nothing: no valid record file and an empty
name). Our engine, which skips broken drivers when importing, never reaches this state.

The moment the player's result is final, MM2 builds one result and adds it

- to the driver's record file (below), if that file was valid when the game started (so never in
  a mod city), and
- to the hall of fame, if the hall-of-fame file of the skill level being played could be read
  when the game started (this does not depend on the record file).

| Type | When | Only if (the race's own settings) | Time | Passed | Score | Hall of fame |
|---|---|---|---|---|---|---|
| Checkpoint | the player crosses the finish | cops, traffic density, time of day and weather equal the race's settings | the race timer | see below | place score | time and score |
| Circuit | the player finishes the last lap | the same four, plus laps and number of opponents | the **best lap** time | see below | place score | every lap's time (in lap order) to the time table; the score once |
| Blitz | the player reaches the last checkpoint in time | race number below 12; cops, traffic density, time of day, weather | the race timer | 1 | first-place score | time and score |
| Crash course | the course is passed (end of its last event) or failed | course number 12 or below | 1.0 | 1 passed, 0 failed | 1 | none |

"The race's settings" are the values in the race-settings file for this race and the skill level
(`mm*data.csv`, milestone 4) and "equal" is exact (the cop count is compared as a number with the
cops setting, so once the menu's 0 … 1 cop slider has been moved a race with a count of 2 or more
cannot be recorded until its settings are loaded again: `menus.md`, "The cop control"). A race played
with changed environment settings is therefore never recorded. A
lost race (out of time, wrecked, drowned, disqualified) records nothing, except a failed crash
course.

**Passed** (checkpoint and circuit): 1 if the place is 1, 2 or 3 at Amateur, or 1 at
Professional; in a checkpoint race also 1 if that race was already passed before; else 0. Place is
the player's finishing place, counting from 1. (A circuit already passed stays passed anyway:
step 3 below keeps the old passed flag and bit.)

**Score** = trunc(`ScoringBias` of the car, `tune/<car>.info`) × *F* × trunc(*D*), with *F* = 50
for place 1, 25 for place 2, 10 for place 3 and 0 otherwise, and *D* the race's `Difficulty`
setting. Blitz always uses *F* = 50. (Shipped biases run from 4 to 100; vpford 15 at difficulty
1 gives the shipped 750.)

### Adding a result to the record file

The record of (type, race) is updated like this, *old* being the stored record and *new* the
result:

1. If *new* is passed, set the race's bit in the passed bits.
2. If *old*'s time is 0.0, store *new* unchanged and stop.
3. Otherwise store: time and car of *new* if *new*'s time < *old*'s time, else *old*'s (a tie
   keeps *old*); score = the larger of the two; passed = 1 if either is passed, else 0.
4. The record is written with its check value at once; the header (with the passed bits) is
   rewritten when the record file is closed, immediately afterwards.

A crash course keeps the car of its first attempt (its time is always 1.0).

### Adding a result to the hall of fame

The file is `amateur.dat` or `pro.dat` by the skill level of the game. For the race's ten
entries (`../formats/profiles.md`):

1. Read them in order; from the first entry whose check value fails, that entry and all after it
   count as empty.
2. **Time table**: go through ranks 0 – 4 and stop at the first rank whose time is 0 (empty) or
   greater than the new time. If there is one, move the ranks from there down by one (rank 4
   falls off) and put the new entry there: driver name, car, the time, passed. If all five ranks
   are non-empty and not greater, the time does not enter.
3. **Score table**: stop at the first rank whose score, truncated to an integer, is less than the
   new score; if there is one, move down and insert the same way with the score as value. A score
   of 0 never enters.
4. If either table changed, write all ten entries back with fresh check values.

Equal times or scores go below the ones already there. A circuit result adds each lap separately
(first lap with the race's score, later laps with score 0, which only reach the time table).

### Rewards

After the record has been written (checkpoint, circuit and blitz every time; a crash course only
when it was passed), MM2 looks for a reward:

1. Read the city's rewards file and the driver's record file again (now including this result).
   The city here is named by its `MapName` (equal to `RaceDir` for the shipped cities).
2. Go through the lines of the race type just played, in file order. For a line whose condition
   holds **now** — `half`: passed(*t*) = ⌊*N*<sub>t</sub> / 2⌋ exactly; `all`: passed(*t*) =
   *N*<sub>t</sub>; a number *r*: bit *r* set — check its car or paint job: if it is still locked,
   unlock it, and that line is the reward (stop). If it is already unlocked, go on.
3. With a reward, the results screen shows the line's message and the announcer says the "new
   car" or "new paint job" line (sound spec, milestone 7). Without one, the announcer comments on
   the place instead.

The unlocked state is not stored: it follows from the records the next time the locks are worked
out ("Locked cars and paint jobs").

### Data that does not unlock anything

`UnlockScore` and `UnlockFlags` in `tune/<car>.info` are read (and must be present,
`../formats/car-model.md`) but nothing ever uses them; the total score unlocks nothing. The
`.cinfo` lines `MustPlace` and `UnlockGroup` are never read (`../formats/city-files.md`): the
top-three / first-place rule and the groups of three above are fixed in the program.

## Totals shown in the menus

From the current driver's record files of `sf` and `london` together:

- per race type (checkpoint, circuit, blitz): the sum of the records' scores and the number of
  records with passed = 1 (the driver record screen);
- the **total score**: the sum of all checkpoint, blitz and circuit scores of both cities (crash
  courses excluded). It is shown only when the driver's skill level is Professional; at Amateur no
  total is shown;
- per crash course: not tried (time 0), passed, or tried but not passed (for the crash-course
  menu).

## Saving

| When | What MM2 writes |
|---|---|
| program start without a readable `players.dir` | it creates the **default driver**: name = text 65, network name = text 66 (`../formats/mmlang.md`), stamp = the game clock (0.0), skill = Amateur, city = the current city; writes its `.cfg` (the options in effect, new-driver view bytes), its `.sav` and `players.dir`, then (as for "driver selected") the `sf` and `london` record files |
| program start, every time | for `london` then `sf` (if the city exists): makes `players/<c>/`; creates `amateur.dat` and `pro.dat` empty if missing or unreadable |
| program start with drivers | selects the last-used driver (next row) |
| driver selected | reads its `.sav` (if that fails the driver is deleted, next-but-one row, except the last driver; which driver, if any, is then current: `../formats/profiles.md`) and `.cfg` (if missing: written from the options in effect); writes `players.dir` with it as last driver; for `sf` and `london`: if the record file is not valid, writes a fresh empty one |
| new driver | refuses as `../formats/profiles.md` says; adds it, writes `players.dir` (it is the last driver), its `.sav` (empty car, paint 0, skill = the skill level currently selected, network name = text 77, stamp = the game clock), its `.cfg` (the options in effect, "physics realism" 0.75, new-driver view bytes) and fresh `sf` and `london` record files; then it is the current driver |
| driver deleted | refuses for the last driver; removes it from `players.dir` (written), deletes its `.sav`, `.cfg` and its record file in every city of the city list. If it was the current driver, the first driver in the list becomes current |
| options screen closed with "done" | `.cfg` (with all current options) |
| leaving the menus to play | `.sav` (car, paint, city, mode, race, network name, last address), `.cfg` |
| game start | reads the `.cfg` and applies it; opens the record file once to see whether it is valid (records on or off) and the hall-of-fame file of the skill level |
| a result is recorded (above) | the record and the record file header, the hall-of-fame race entries, then the `.sav` again |
| "next race" from the results | `.sav` (the new mode and race), `.cfg` (with the current view bytes) |
| game end | if the `.cfg` was read at game start: `.cfg` with the current options and view bytes |
| race statistics shown for a city | creates that city's `amateur.dat` / `pro.dat` if missing; rebuilds one empty if its race counts differ from the city's |
| driver records shown | empties a record file whose race counts differ from the city's |

An empty car in the `.sav` (a driver that never played) is replaced by `vpbug` with paint 0 when
the driver is applied; the default and new drivers start that way.

## Checked against

- `tools/game/profiles.py` applies the open-race and lock rules to the shipped `DriverX`: SF and
  London checkpoint races open 0x007, crash courses 0x777; locked cars and paint jobs as listed
  above; total score 1,500 (two SF blitz records of 750, shown only at Professional).
- The shipped records agree with the recording rules: SF blitz 0 and 1 in vpford (bias 15,
  difficulty 1): passed, score 15 × 50 × 1 = 750, and the same two results at rank 0 of the SF
  Amateur hall of fame (time and score tables); London crash course 0 passed and 1 failed, both
  time 1.0 and score 1 in vpcab, no hall-of-fame entry.
