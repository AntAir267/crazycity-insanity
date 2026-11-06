# Race modes: catalogue, settings and the session

MM2's single-player modes are Cruise (free roam), the Checkpoint race, Circuit, Blitz and the Crash
Course. This page says which races a city offers, the race settings file and what each column does,
which values a race session runs with, how a race is set up and restarted, the order of one frame,
the clocks, the countdown, losing, water and falling through, the pre- and post-race cameras, the
police in races and random numbers. The pages next to it:

| Page | Contents |
|---|---|
| `waypoints.md` | the waypoint files, gates, the crossing test, the five gate layouts, the arrow target, the opponents' gate counts |
| `modes.md` | the rules of Checkpoint, Blitz, Circuit and Cruise, state by state |
| `crash-course.md` | the Crash Course: its event files and the nine event kinds |
| `results.md` | finishing order, standings, the results table, score, what is recorded, "next race" |
| `progression.md` | records, the hall of fame, open races and unlocks (what a finished race writes and opens) |
| `../ai/opponents.md` | the computer racers (cars, driving, their finish test) |
| `hud.md` | how the HUD shows the messages, clocks, places and the arrow named here |

Times are seconds, distances metres. *F* is the frame time (`../engine/controls.md`). Messages are
named by their text number in `mmlang.dll` (`../formats/mmlang.md`); "for *d* s" is how long the
HUD keeps it.

## Modes

| Number | Mode | Race name in files (`../formats/aimap.md`) | Races per city |
|---|---|---|---|
| 0 | Cruise | `roam` | one |
| 1 | Checkpoint race | `race`*N* | `CheckpointCount` of the `.cinfo` |
| 3 | Circuit | `circuit`*N* | `CircuitCount` |
| 4 | Blitz | `blitz`*N* | `BlitzCount` |
| 6 | Crash Course | `crash`*N* | always 13 (*N* = 0 … 12) |

(2 and 5 are multiplayer modes.) *N* counts from 0. The Checkpoint, Circuit and Blitz races of a
city and their display names come from its `tune/<file>.cinfo` (`../formats/city-files.md`, "City
list"): race *N* has the *N*-th name of its names line. A count of 0 means the city has no race of
that kind (the menus leave the mode out). The 13 crash courses have fixed names from the text file
(`../formats/mmlang.md`, the crash-course lesson lists), not from the `.cinfo`.

## Files of a race

All under `race/<RaceDir>/` except where noted (`RaceDir`, `MapName`: city list).

| File | Used for |
|---|---|
| `mmracedata.csv`, `mmcircuitdata.csv`, `mmblitzdata.csv`, `mmcrashdata.csv` | race settings, row *N* for race *N* (below) |
| `<race>waypoints.csv` | the waypoint list (`waypoints.md`); not for the Crash Course |
| `crash<N>data.csv`, `crash<N>data_p.csv` | Crash Course events at Amateur / Professional (`crash-course.md`) |
| `<event file>.csv` | a Crash Course event's waypoint list |
| `<race>.aimap`, `<race>.aimap_p` | police, opponents, traffic exceptions at Amateur / Professional (`aimap.md`) |
| `<race>-a-<k>.opp`, `<race>-p-<k>.opp` | opponent routes (`../formats/opp.md`) |
| `race/<MapName>/<race>.pathset` | race props (`../formats/pathset.md`) |
| `tune/<car>.info` | `ScoringBias` for the score (`results.md`) |

**Path sets of a race** (single player and network games alike; only the multiplayer Circuit's
sturdier barricades, `multiplayer.md` 1.4, are network-only). The race name — `race`*N*,
`circuit`*N*, `blitz`*N*, `crash`*N* (Cruise `roam`) — picks two kinds of path set in
`race/<city>/`: the **race props** `<race>.pathset`, read at the end of every city load
(`../formats/pathset.md`; a missing file gives none), and for each moving-object kind
`<city>_<word>_<race>.pathset` if it exists, else `<city>_<word>.pathset` (`level-objects.md`,
"Which file"). Shipped race props: London `race6`, `race7`, `circuit0` … `circuit9`, `blitz10`,
`blitz11`, `crash0`, `crash2`, `crash3`, `crash6`, `crash7`, `crash9`, `crash11`, `crash12`; SF
`circuit0` … `circuit9`, `crash0`. So, e.g., a single-player Circuit 0 in London loads
`circuit0.pathset` (its barricades) and `london_bridge_circuit0.pathset`; Crash Course 3 loads
`crash3.pathset`, `london_bridge_crash3` and `london_parkedcar_crash3`.

Files for race numbers beyond a `.cinfo` count are shipped (e.g. `blitz10` … `blitz13` in SF) but
never used. **Professional** differs from Amateur only in the files and columns it reads: the
`_p` aimap (and through it the `-p-` route files), the professional half of the settings row, the
`_p` crash event file, the stricter pass rule (`results.md`) and its own hall of fame
(`progression.md`).

## The race settings file (`mm<mode>data.csv`)

Text: a header line (skipped, whatever it holds), then one row per race in race order:

```
Description, CarType, TimeofDay, Weather, Opponents, Cops, Ambient, Peds, NumLaps, TimeLimit, Difficulty, (the same ten again)
none,0,0,0,7,0,0.1,0.0,3,50,1,0,0,1,6,0,0.2,0.0,4,40,1
```

Reading: lines of at most 255 bytes; fields separated by commas as C `strtok` does (runs of
commas count as one). Field 1 is the description (ignored); then ten **Amateur** fields and ten **Professional** fields in the order of the
table, integers read as C `atoi`, decimals as `atof`. A row with fewer than 21 fields (including a
blank line) makes MM2 crash; **engine:** missing fields are 0 and blank lines are skipped. MM2 has
room for **15 rows**; more overwrite its other columns. **Engine:** keep only the first 15 rows.

| Column | Kind | Effect |
|---|---|---|
| CarType | integer | none (never read) |
| TimeofDay | integer | the session's time of day: 0 morning, 1 noon, 2 evening, 3 night (`../formats/night.md`) |
| Weather | integer | 0 clear, 1 cloudy, 2 foggy, 3 rain |
| Opponents | integer | opponent number: Checkpoint as is; Circuit the default and the maximum of the player's choice; Blitz only through "next race" (0 in every shipped row; Blitz aimaps list no opponents anyway); Crash Course not used (below) |
| Cops | integer | police density, so 0 = no cops and 1 or more = every cop of the aimap (`../ai/police.md`, "How many cops"); not used by the Crash Course |
| Ambient | decimal | traffic density *D* (`../ai/traffic-population.md`); the Crash Course uses its event file instead |
| Peds | decimal | pedestrian density (pedestrian spec) |
| NumLaps | integer | Circuit: the number of laps. Other modes: not used (Checkpoint always 1) |
| TimeLimit | decimal | Blitz: the time limit. Not used by Checkpoint or Circuit (they have none); the Crash Course uses its event file |
| Difficulty | decimal | only in the score (`results.md`); 1 in every shipped row |

## The session's settings

When a race is chosen, its row (Amateur or Professional half by the skill level) sets the
session's values. Values marked "kept" are not touched and stay as the last chosen mode or race
left them.

| Value | Checkpoint | Circuit | Blitz | Crash Course | Cruise (menu) |
|---|---|---|---|---|---|
| opponent number | Opponents | player's choice 1 … Opponents (default Opponents) | kept ("next race": Opponents) | **8** | kept |
| cop density | Cops | Cops | Cops | 1.0 | 1.0 default, slider |
| traffic density *D* | Ambient | Ambient | Ambient | the event file's `AmbDensity` | 0.5 default, slider |
| pedestrian density | Peds | Peds | Peds | `mmcrashdata` Peds | 0.25 default, slider |
| time of day, weather | row | row | row | `mmcrashdata` row | 1 (noon), 0 (clear) default, menu |
| laps | 1 | NumLaps | kept | kept | kept |
| time limit | TimeLimit (unused) | TimeLimit (unused) | TimeLimit | each event's own | kept |
| difficulty | Difficulty | Difficulty | Difficulty | kept | kept |

In MM2's menus a race's settings can be changed by the player (time of day, weather, densities,
cops, opponents, laps) only once the race has been passed (`progression.md`); a race run with
changed settings records nothing. The cop density stays the row's `Cops` count until the menu's
0 … 1 cop slider is moved, which replaces it with the slider's value (`menus.md`, "The cop
control"). A race number without a row in the file leaves every value as it
was. "Next race" from the results sets the next race's values the same way, except that Checkpoint
and Blitz then leave the pedestrian density as it was and Blitz takes `NumLaps` into the (unused)
laps value (`results.md`).

Opponents actually created: `../ai/opponents.md` ("Which opponents exist"), from the opponent
number and the aimap's rows.

## Setting up a race

MM2 builds a race session in this order (the parts owned by other specs only named):

1. Load the city, the player's car with its cameras and HUD (`../engine/cameras.md`), the
   moving level objects.
2. **The race's own objects**: the waypoint list (`waypoints.md`; the Crash Course reads its event
   file and the first event's list, `crash-course.md`); for Checkpoint and Circuit the opponents'
   finish line (`waypoints.md`, last section). The player's start is put down (step 1 of "Placing
   the cars").
3. The AI: road network, traffic, police from the race aimap, the opponents
   (`../ai/opponents.md`).
4. "Placing the cars", steps 2 and 3.
5. HUD for the mode: the countdown clock limit (Blitz, Crash Course), the arrow on or off.
6. Time of day and weather.
7. A full reset of the race ("Restart") starts it.

### Placing the cars

1. The player's car is put at waypoint 0 (x, y, z) with heading θ = *a* × −0.017453292 (*a* =
   waypoint 0's heading in degrees, single precision) by MM2's placement rule
   (`../physics/vehicle.md`, "Placing the car in a city"). (Cruise uses its random intersection
   instead, `../engine/controls.md`, "Free-roam start".)
2. **Ground snap** (Checkpoint, Circuit, Blitz, Crash Course), once: with P = the car's body
   position after step 1, probe from (P.x, P.y + 2.0, P.z) down to (P.x, P.y − 10.0, P.z)
   (`../physics/collision.md`, "The query": a fresh state with room hints 0 and an empty polygon
   cache, the wheels' ground mask, ignoring nothing). On a hit at H the start point becomes
   (H.x, H.y + 0.9, H.z) (0.9 = 0.89999998) and the car is put down again there with the same θ.
   On no hit the start stays. (So the car stands 0.9 m above the surface found under its centre of
   mass, plus the placement rule's centre-of-gravity offset.)
3. Then each opponent is snapped the same way from its model origin (`../ai/opponents.md`, "Start
   position").

Every restart puts the cars back at these start points.

## Restart (a full reset of the race)

Run when the race starts, when the player restarts, and when the player's car falls through the
city (below). For Checkpoint, Circuit, Blitz and Crash Course, in this order:

1. **Hold every racer**: the player's car and every opponent are made **not drivable** with brake
   and neutral (every frame, before the physics, brake = 1 and the gearbox in neutral, as for the
   Cruise wreck, `../physics/damage.md`), and their damage counting is switched off (impacts add no
   damage). (Crash Course: first goes back to its first event, `crash-course.md`.)
2. The race's own state: state timer 0; per opponent: gate count 1, passed set {0}, not finished;
   finished count 0; the results table emptied; the "race finished" flag cleared. Circuit also
   clears its best lap (`results.md`).
3. The world reset (common to all modes): props back (`../physics/collision.md`), the player's
   water timer 0, the race speech's pre-race line (`../audio/speech.md`), the AI reset (which
   restarts the game random generator, "Random numbers"), then every object of the session is
   reset: the player (car back at its start point, damage cleared, controls enabled, camera to its
   view, water effect off), the waypoint list (`waypoints.md`, "Reset"), the HUD.
4. The mode's state 0 (`modes.md`); the pre-race camera starts ("Cameras", not in Cruise); the HUD
   clocks are stopped.

Cruise's restart is only step 3 plus "make the player drivable".

**Go** (the end of the countdown, `modes.md`): the player's car is drivable again (a gearbox in
neutral or reverse goes to first gear) with damage counting on unless the player has the
no-damage option; with an automatic gearbox the automatic-reverse state is cleared
(`../engine/controls.md`); every opponent is drivable with damage counting on. (The Crash Course
releases only some opponents, `crash-course.md`.)

## One frame

1. Input and, if the game is not paused, the **mode's update** (`modes.md`): the state machine,
   then (Checkpoint and Circuit) the standings and the opponents' gate counts and finish tests
   (`results.md`).
2. **Water and falling through** (below), for the player's car.
3. The objects of the session, in this order: the player (controls → car inputs, cameras), the
   opponent icons, the **waypoint list** (the gate test with the player's current pose,
   `waypoints.md`), the HUD map, the **HUD** (its clocks advance), the menus.
4. The level, the AI (traffic, police, opponents) and the physics steps (the cars move).

So a gate is tested against the pose the physics left at the end of the previous frame, the mode
sees a finished list one frame after the gate was crossed, and the clocks count the frame in which
they are started.

## Clocks

The HUD has three clocks (display: `hud.md`); in single player they run on game time: each frame,
while running and the game is not paused, *t* ← *t* + *F* (single precision), after the gate test
of that frame.

| Clock | Counts | Started | Used for |
|---|---|---|---|
| race clock | up from 0 | at "go" (reset to 0 then) | the player's time; Circuit lap times |
| opponents' clock | up from 0 | with the race clock | the opponents' finishing times (keeps running after the player finishes) |
| countdown | down from the limit: each frame, if *t* ≤ 0 then *t* = 0 and it stops, else *t* ← *t* − *F* (so it can read below 0 for one frame) | at "go" (Blitz, Crash Course) | time limits |

The countdown's limit is the time limit (Blitz: the session's; Crash Course: the event's; MM2 has
a hidden override that is always 0). **Running out** means the countdown reads ≤ 0 at a mode's
check. **"Stop the clocks"** (and "reset the clocks", "start the clocks") in these pages means the
race clock and the opponents' clock together; the countdown is stopped, reset or started only where
a page names it.

## Losing, finishing and the menus after the race

All race modes end in one of two states:

- **Lost** (wrecked, drowned, out of time in the modes where that ends the race, failed course):
  the end-of-race menu (MM2's pause menu: restart, options, quit) opens when the state timer,
  counted down by *F* each frame, first lies in (−1, 0] (MM2 checks −1 < *t* ≤ 0; with the
  frame-time cap of 0.1 s this is always met).
- **Finished**: the results (`results.md`) open the same way once the timer has run out and no
  other menu is open.

While in either state the in-race pause menu is locked out. "Restart" runs a full reset.

## Water and falling through

The player's water timer and the 5 s rule are in `../physics/collision.md` ("World limits"):
when the car has been in deep water for more than 5 s the mode's water rule runs (once per water
episode). Falling through the city (y < −50) is a full restart in every single-player mode. Both
checks run every frame **also while the game is paused** (the pause menu): MM2 makes them outside
its pause test and its frame time keeps running during the pause, so a car that was already in
deep water reaches its 5 s and gets the mode's water rule with the pause menu open. (The car itself
does not move while paused.)

| Mode | Water rule |
|---|---|
| Cruise | full restart (`../physics/collision.md`) |
| Checkpoint, Blitz | **lost**: play `Damgelose`, stop the clocks (Blitz also the countdown; Checkpoint also silences the player's engine), state timer 0.5, the "in the water" mark set; the end-of-race menu after 0.5 s |
| Circuit | the car is put back at the **last gate passed** (waypoint 0 before the first gate) with that gate's heading, as by the restart's player reset, but its gate count is kept and its start point is not changed; the race goes on |
| Crash Course | lost as in Blitz (`Damgelose`, clocks and countdown stopped), unless the course is already marked finished (`crash-course.md`) |

On entering deep water (the first frame the water timer is not 0) the camera also changes
("Cameras"). MM2 quirks after the player has finished (the 5 s before the results): landing in
water switches a Checkpoint race to "lost", whose menu then never opens because the race is
finished (the game sits there); a finished Blitz shows the end-of-race menu instead of the
results. **Engine:** show the results in both cases.

## Cameras

**Pre-race camera** (Checkpoint, Circuit, Blitz, Crash Course; at every restart): MM2 shows a
camera hovering behind and above the car and blends from it to the player's view over **3.5 s**
with easing type 3 (`../engine/cameras.md`, "Blending between cameras"; A = this camera):

- Pose: polar about the car, pitch = −1.1 rad, heading = atan2(M.Z.x, M.Z.z) of the car's model
  matrix taken once when the camera starts (plus an offset of 0), roll 0 (axes as
  `../engine/cameras.md`, "Definitions"); position = the car's model position + 22.0 × the
  camera's Z axis + (0, 2.0, 0). It keeps that offset from the car each frame (the car is held
  still). Field of view 50°, near plane 3.0 (the camera defaults; no file is read for it).
- Target: the current external view (`../engine/cameras.md`, "Switching views"); if that is the
  bumper camera, the blend goes to the near chase camera, and when it has arrived MM2 blends on to
  the bumper camera in 0.5 s (type 3). If the dash view was on, the dash goes off for the fly-in;
  at the end MM2 blends near chase → bumper in 0.3 s (type 1) and bumper → dash in 0.3 s (type 2)
  and switches the dash display on again (unless the HUD map is in its half-screen mode).
- The pre-race camera counts as running until the view shown is the intended one; Checkpoint and
  Circuit wait for that before their countdown, Blitz and the Crash Course do not. No key changes
  the camera while it runs (`../engine/cameras.md`).

**Post-race camera** (when the player finishes, wrecks out in Checkpoint or Blitz, or a course
ends, as `modes.md` says): the dash view goes off; the far chase camera is updated once and its
position + (0, 3.5, 0) becomes the position of a fixed **point camera**, blended to in 0.8 s (type
3). Each frame it looks at the car's model position (`../engine/cameras.md`, "Look-at pose") and
sets its field of view from the squared distance *q* to it: with *D* = 25, *m* = (*D* × 0.3)²:
FOV = 60 if *q* ≤ *m*; else *f* = (*q* − *m*) / (*D*² − *m*) if *q* < *D*², else 1, and FOV = 60 −
*f* × 35 (it zooms in as the car gets away). Near plane 0.5. It stays until the next restart.
(MM2 also prepares an orbiting camera at this moment that is never shown.)

**Water camera** (every mode, once per water episode, on the first frame the water timer is not
0): the dash view goes off and the same point camera is put at the current camera's position +
(0, 9.0, 0), blended to in 0.8 s (type 3). Its *D* is 70 if no post-race camera has been used since
the car was created, else 25 (MM2 reuses the camera).

## Police in races

The race aimap's `[Police]` list and the cop density (Cops column, `../ai/police.md`, "How many
cops") give the cops; they behave as in Cruise and may chase opponents (`../ai/opponents.md`,
"Police and traffic in races"). Only two Crash Course events look at them: at the end of an
**evade** or **corner** event the event fails if any cop is in sight (`../ai/police.md`, "Caught":
any cop not IDLE, OUT included, with a clear line of sight between points 3.5 m above both cars and
closer than 200 m; `crash-course.md`). Nothing else in any race mode reads the police.

## Random numbers

The race logic itself (settings, set-up, waypoints, gates, standings, results, cameras, every mode
and Crash Course event) draws no random numbers. The game generator is restarted (x = 1) by the AI
reset in step 3 of every restart (`../physics/collision.md`, "Random numbers"), after the racers are
held and before the cars are put back; draws then come from the AI, the physics bumps, Cruise's
start point (`../engine/controls.md`) and the speech system's own generator
(`../audio/speech.md`). A fixed-seed check run of a race therefore needs only that restart.

## Checked against

`tools/game/races.py` (dirty side; report `work/check/races.txt`) reads the shipped data by these
rules:

- Cities SF then London (`sf.cinfo` first, `london.cinfo` from the folder walk); each 12
  Checkpoint, 10 Circuit and 10 Blitz races, all with a waypoint file, an `.aimap` and an
  `.aimap_p`; 13 crash courses each with both event files.
- `mmracedata.csv` 12 rows, `mmcircuitdata.csv` and `mmblitzdata.csv` 10 rows, `mmcrashdata.csv`
  13 rows (`lesson1` … `final13`) in both cities; every row has the 21 fields; Difficulty is 1
  and CarType 0 everywhere; Opponents 0 in every Blitz and Crash Course row, 4 – 7 in Checkpoint
  and Circuit rows; Cops up to 8 (SF `race8` professional); NumLaps 2 – 4 in Circuit rows (3 / 4
  in the unused Checkpoint and Blitz columns); Blitz time limits 18 – 120 s.
- Machine code: the column order of the reader, the integer columns (CarType, TimeofDay,
  Weather, Opponents, Cops, NumLaps), the 15-row array, the session values per mode (Crash Course
  opponents 8.0 and cops 1.0 from its menu, traffic density from the event file), the ground snap
  (+2.0 / −10.0, ground mask, +0.89999998), the order of the restart and the frame, the clock
  modes (game time in single player), the pre-race camera values (2.0, 22.0, 1.1, offset 0, FOV
  50, near 3.0, 3.5 s type 3), the post-race and water point camera (+3.5 / +9.0, 25 / 70, 60 −
  35 *f*, near 0.5, 0.8 s type 3), and the absence of random draws in the mode code.
