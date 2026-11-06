# Crash Course

The Crash Course (mode 6) is MM2's driving school: 13 courses per city (*N* = 0 … 12), each made of
one or two **events** (lessons) of nine kinds: jump, clean driving, follow a car, evade the police,
minimum speed, no contact, accelerate, checkpoints and destroy a car. This page specifies the
event files and every kind's rules. Words as in `modes.md` (the list, hold, go, lost, finished,
wrecked, damage rules on, t, "message *n* for *d* s", disable the player) and `races.md`.

## Files

| File (`race/<RaceDir>/`) | Used for |
|---|---|
| `crash<N>data.csv` (Amateur), `crash<N>data_p.csv` (Professional) | the course's events (below) |
| `<Filename>.csv` | an event's waypoint list (`waypoints.md`) |
| `mmcrashdata.csv` | row *N*: time of day, weather, pedestrian density (`races.md`) |
| `crash<N>.aimap`, `.aimap_p` | police, opponents, traffic exceptions |

Session values: opponent number 8, cop density 1.0 (every cop of the aimap), time of day, weather
and pedestrians from `mmcrashdata.csv`; the traffic density *D* is the `AmbDensity` of the **last
row** of the event file (MM2 sets it while reading each row, so the last one stays; the traffic's
*d* = clamp(*D*, 0, 1) × 0.2, `../ai/traffic-population.md`). A course whose event file is missing
has no events (no waypoints, nothing ever finishes; **engine:** refuse to start it) and keeps the
traffic density it had (0 when chosen from the menu, which sets 0 before the file is read).

### Event file

Text: a header line (skipped; its column names vary and are wrong in some files), then one event
per line, at most 255 bytes per line, fields separated by commas as C `strtok` does:

| Field | Name | Read as | Meaning |
|---|---|---|---|
| 1 | Filename | text, at most 31 characters | waypoint list `<Filename>.csv` |
| 2 | Event | integer | the kind, 0 … 9 (below) |
| 3 | Checkpoints | integer | 0: the event has no waypoint list at all; else the list is read |
| 4 | TimeLimit | decimal | the countdown's limit for this event (0 = none, minimum-speed kind) |
| 5 | AmbDensity | decimal | traffic density (last row wins, above) |
| 6 | Misc | decimal | minimum-speed kind: the speed in mph (below 1 → 50) |
| 7 | (extra) | integer | not 0: only the current target gate is shown (`waypoints.md`, "only-target") |
| 8 | (extra) | integer | number of opponents this event uses |

Further fields are ignored. MM2 has room for 10 events (more overwrite memory; **engine:** keep the
first 10) and crashes on a row with fewer than 8 fields (**engine:** missing fields are 0, blank
lines skipped).

**Opponents of event *k*.** With *n*<sub>j</sub> = field 8 of event *j*, event *k* uses opponents
*S* … *S* + *n*<sub>k</sub> − 1 with *S* = *n*<sub>0</sub> + … + *n*<sub>k−1</sub> (in creation
order, `../ai/opponents.md`). They are held with every other racer by the restart and released at
this event's go; the others stay held. The "target car" of the follow and destroy kinds is
opponent *S*.

## Setting up an event

At the start of the course (and again on every restart, which goes back to event 0) and when an
event hands over to the next one:

1. The waypoint pool of 32 slots is made once per session (32 `pt_check` gates).
2. If the event's Checkpoints field is 0, the waypoint list is switched off completely (not drawn,
   not tested, never done). Else its file is read into the pool (`waypoints.md`, 32 slots):
   layout **4** if the kind is 0 (jump), layout **5** otherwise; only-target from field 7; then
   the list's reset (`waypoints.md`, "Reset").
3. The HUD for the kind (`hud.md`): the countdown's limit = the event's TimeLimit (kinds 1 and 2
   keep the limit they had) and the countdown is set to that limit (whether it runs is not
   changed; every event end that matters has stopped it), so each event starts with its own full
   time; whether the countdown is shown and whether the arrow is on:

   | Kind | Countdown | Arrow |
   |---|---|---|
   | 0, 3, 5, 6, 7 | shown | on |
   | 9 | shown | off |
   | 2 | not shown | off (and the opponent icons are switched on) |
   | 8 | shown | off (opponent icons on) |
   | 4 | shown only if TimeLimit ≠ 0 | on |
   | 1 | unchanged | unchanged |
4. State 0, t = 0.

Handing over to the next event happens inside the frame that met the goal; the next event's state
0 runs from the following frame. The car is not moved, the clocks restart at the next go, and some
kinds leave things set from the finished event (see "Quirks").

## The common shape

Every kind runs the same skeleton with its own messages and checks:

| State | Meaning |
|---|---|
| 0 | **First event:** the course's name is shown (the lesson name: text 545 + *N* when the city's `RaceDir` is `sf`, 532 + *N* when it is `london`, nothing in any other city; `../formats/mmlang.md`) each frame for 1.25 s while t ≤ 1.25 (t += *F*); when t > 1.25: the kind's preparations, t = the countdown length (**5.0**; kinds 7 and 9: **2.5**), state 1, play `Startracelow`. **Later event:** reset and stop the clocks (and the countdown where noted), t = 0, release the racers (go), state 2 — which goes on at the next frame, since t − *F* ≤ 0. |
| 1 ready | t −= *F*; at t ≤ 1.25: state 2, t = 1.25, play `Startracelow`; else the kind's "ready" message for 1.25 s. |
| 2 set | t −= *F*; at t ≤ 0 (minimum-speed kind: t < 0): state 3, play `Startracehigh`, the kind's go message for 1.25 s, release the racers (the player made drivable with damage counting by the no-damage option, the event's opponents drivable with damage counting on, `races.md` "Go"), start and reset the clocks and the countdown as noted; else the "set" message. |
| 3 driving | the kind's checks (below) |
| 4 lost | lock the pause menu; t −= *F*; when −1 < t ≤ 0: the end-of-race menu |
| 5 penalty | t −= *F*; when t ≤ 0: the player's damage cleared, state 3 |
| 6 finished | lock the pause menu; t −= *F*; when −1 < t ≤ 0 and no other menu is open: the results |

**Goal met** (the kind's success): if this is the last event: **pass** the course; else, if the
course has more than one event, hand over to the next event (and nothing more this frame).
**Pass** and **fail**: the instructor's result line (`../audio/speech.md`, "Crash Course"); the
results get one row, place 1, the player's name and text 651 when passed (`results.md`); the result
is recorded (`progression.md`: time 1.0, score 1, passed 1 or 0; not for a course number above 12
or after a cheat code); a pass is followed by the reward check (`progression.md`).

**Time out** (where a kind checks it): the countdown reads ≤ 0: stop the timer warning, stop the
clocks, message 244 for 5 s.

## The kinds

The shipped courses use kinds 0, 2, 3, 4, 5, 7, 8 and 9 (`tools/game/races.py`); 1 and 6 are
specified for mods.

### 0 Jump ("hit all checkpoints", layout 4)

Messages: ready 206, set 207, go 208. First event state 0 also stops and resets the countdown. At
go (first event) only the player is made drivable: **its damage counting stays off** and no
opponent is released (later events release normally). Checks in state 3:

1. List done: finished mark; state 6; post-race camera; disable the player; stop the race clock and
   the countdown; goal met (pass: play `Endofracetag`); message 209 for 5 s; t = 5.
2. Else countdown ≤ 0: stop the warning; the list off; hide the last waypoint; fail; play
   `Youlose`; message 614 for 5 s; stop the clocks; disable the player; t = 5; state 4.
3. Else damage rules on and wrecked: stop the warning; the list off; hide the last waypoint; play
   `Damgelose`; car not drivable (brake and neutral); message 615 for 5 s; fail; post-race camera;
   stop the clocks and the countdown; t = 5; state 4.

### 1 Clean driving (not shipped)

No course name: state 0 shows message 210 for 1.25 s and at once resets and stops the clocks,
stops and resets the countdown, t = 5, state 1, `Startracelow`. Ready 211, set 212, go 213 (with
the countdown started). Checks in state 3:

1. Countdown < 10 and not finished: the timer warning.
2. Countdown ≤ 0 and not finished: finished mark; state 4; post-race camera; disable the player;
   stop the race clock and the countdown; message 214 for 1.25 s; stop the warning; t = 5.
3. List done: message 215 for 1.25 s; finished mark; state 6; post-race camera; disable the
   player; stop the race clock and the countdown; t = 3; stop the warning.
4. Damage rules on and wrecked: play `Messagenote`; message 216 for 5 s; t = 5; state 5 (the
   penalty clears the damage).

It records nothing and never hands over.

### 2 Follow the car

The target car is opponent *S*. State 0 hides the countdown each frame; ready 218, set 219, go 220;
the countdown is not started. Checks in state 3:

1. Wrecked (whatever the no-damage option): t = 3, state 5 (a 3 s penalty, then the damage is
   cleared).
2. Else if (the target car's finish test fires, `../ai/opponents.md`, or the list is done) and the
   squared distance between the player's and the target's body positions, (dx·dx + dy·dy) + dz·dz,
   is below 100.0 (10 m): t = 5; goal met; then finished mark, message 221 for 5 s, state 6, stop
   the race clock, disable the player, post-race camera.
3. Then if that squared distance is above 10000.0 (100 m): t = 5; message 222 for 5 s with 223 on
   the second line; disable the player; fail; state 4.

The opponent finish test uses the finish line of the last Checkpoint or Circuit race of the
program run (the Crash Course sets none, `../ai/opponents.md`); with none it never fires and only
the list ends the chase.

### 3 Evade the police

State 0 (first event) does not touch the clocks. Ready 193; while in state 1 the HUD map is switched
on if it is off (`hud.md`). Set 194. Go: message 195 for 1.25 s (first event) or 652 for 3 s (later
event); the clocks and the countdown start. Checks in state 3:

1. Wrecked (whatever the option): t = 3, state 5; the checks go on.
2. List done: finished mark; state 6; post-race camera; disable the player; stop the race clock and
   the countdown. If **no cop is in sight** (`races.md`, "Police in races"): message 196 for 5 s;
   goal met (pass: play `Endofracetag`). If a cop is in sight: message 197 for 5 s; fail; play
   `Youlose`; the list off; hide the last waypoint; stop the clocks; state 4. t = 5.
3. Else countdown ≤ 0: the list off; hide the last waypoint; play `Youlose`; message 198 for 5 s;
   fail; stop the clocks; disable the player; t = 5; state 4.

### 4 Minimum speed

*S* = the event's Misc (below 1 → 50) in mph, *T* = 1.0 s. State: reached (false) and under-time
*u* (0), both reset only by the course restart. The player's speed is the car's `speedMPH`
(`../physics/vehicle.md`). State 0 (first event) also stops and resets the countdown; ready: message
232 with *S* written as a whole number, for 2.5 s; no "set" message; go: message 233, and the clocks
and the countdown start only if the TimeLimit is not 0. Checks in state 3, in order:

1. List done: finished mark; post-race camera; disable the player; stop the race clock and the
   opponents' clock; t = 5. If a cop is in sight: message 197; fail; play `Youlose`; state 4 (end of
   checks). Else state 6; goal met (pass: play `Endofracetag`); stop the race clock and the
   countdown; message 234 for 5 s; end of checks.
2. TimeLimit ≠ 0 and time out: the list off; hide the last waypoint; fail; play `Youlose`; stop the
   clocks; disable the player; t = 5; state 4; end of checks.
3. If not reached: **a.** if the list's count ≥ 2 (a gate was passed before reaching *S*): message
   235 for 3 s with 236 on the second line; disable the player; stop the race clock and the
   countdown; t = 5; fail; state 4 (the checks go on). **b.** If still not reached and speed > *S*:
   reached = true, message 237 for 3 s.
4. If *u* > 0 and speed > *S*: *u* = 0.
5. If reached and speed < *S*: *u* = *F* + *u*; if *u* > *T*: reached = false; message 238 for 5 s
   with 239; disable the player; t = 5; fail; stop the race clock and the countdown; state 4; end
   of checks.
6. Wrecked (whatever the option): t = 5; message 240 for 5 s; fail; state 4.

(Comparisons: "speed > *S*" and "*u* > *T*" are strict; "speed < *S*" counts a NaN speed as below.)

### 5 No contact

State 0 (first event) also stops and resets the countdown; ready 225; set 226 — and every frame of
state 2 that does not end it sets the **player's** damage limits to `MaxDamage` 10.0, `MedDamage` 5.0
and `ImpactThreshold` 0 (so any impact wrecks the car; they stay until the game session ends: a
restart keeps them, the next race loads the car with its own values); go
227 with the countdown started. Checks in state 3:

1. Countdown < 10 and not finished: the timer warning.
2. Countdown ≤ 0 and not finished: finished mark; state 4; post-race camera; disable the player;
   stop the race clock and the countdown; message 228 for 1.25 s; fail; stop the warning; t = 5;
   end of checks.
3. List done: finished mark; state 6; post-race camera; disable the player; stop the race clock and
   the countdown; goal met; message 229 for 1.25 s; t = 3; stop the warning (the checks go on).
4. Damage rules on and wrecked: message 230 for 5 s; play `Youlose`; fail; t = 5; state 4.

### 6 Accelerate (not shipped)

State 0 always shows message 199 for 1.25 s while t ≤ 1.25 (t += *F*), then t = 5, state 1,
`Startracelow` (no later-event path). Ready 200, set 201, go 202 (the HUD shows the countdown; the
clocks and the countdown start). Checks in state 3:

1. Wrecked (whatever the option): t = 3, state 5; the checks go on.
2. List done: state 69, t = 2.0; end of checks.
3. Else countdown ≤ 0: the list off; hide the last waypoint; play `Youlose`; fail; message 203 for
   5 s; stop the clocks; disable the player; t = 5; state 4.

State 69: t −= *F*; when t < 0: finished mark, state 6, post-race camera, disable the player, stop
the race clock and the countdown, message 204 for 5 s, goal met (pass: play `Endofracetag`), t = 5.

### 7 and 9 Checkpoints

Kind 7 shows the arrow, kind 9 does not. State 0 also stops and resets the countdown and uses a
**2.5 s** countdown; ready 241, set 242, go 243 with the countdown started. No timer warning. Checks
in state 3:

1. List done: stop the warning; goal met; play `Endofracetag`; finished mark; state 6; post-race
   camera; disable the player; stop the race clock and the countdown; message 609 for 5 s; t = 5.
2. Else time out: the finished mark is **cleared**; the list off; hide the last waypoint; fail;
   play `Youlose`; stop the clocks; disable the player; t = 5; state 4.
3. Else damage rules on and wrecked: stop the warning; the list off; hide the last waypoint; play
   `Damgelose`; car not drivable; message 245 for 5 s; fail; post-race camera; stop the clocks and
   the countdown; t = 5; state 4.

### 8 Destroy the car

The target car is opponent *S*. State 0 (first event) does not touch the countdown (a later event
stops and resets it); ready 617, set 618; go 619 with the countdown started, and — if the session
has any opponent — the target's `MaxDamage` := 150000.0 and `MedDamage` := 75000.0. Checks in state
3:

1. If opponent **0**'s `MaxDamage` ≤ the target's `damage` (MM2 compares with opponent 0's limit
   even when the target is another opponent): the target is made not drivable (brake and neutral);
   finished mark; t = 5; goal met; message 620 for 5 s; state 6; stop the clocks and the countdown;
   disable the player; post-race camera.
2. Else if the target car's finish test fires: t = 5; message 621 for 5 s with 622; stop the clocks
   and the countdown; disable the player; fail; state 4.
3. Else time out: fail; play `Youlose`; stop the clocks and the countdown; disable the player;
   t = 5; state 4.
4. Else damage rules on and wrecked: t = 3; state 5.

**Engine:** with no opponents the target does not exist (MM2 reads an invalid opponent); skip
checks 1 and 2.

## Water, falling through, restart

Landing in water (`races.md`, "Water") while the finished mark is clear: play `Damgelose`; stop the
clocks and the countdown; t = 0.5; the "in the water" mark; state 4. With the mark set nothing
happens. Falling through the city and "restart" restart the course from event 0 (both events'
opponents held again; the minimum-speed state cleared).

## Quirks to keep

- **Hand-over after a finish.** Kinds 0, 3, 4, 5 and 6 set the finished mark, the post-race camera
  and the player-disabled override before handing over to the next event, and nothing clears them
  until a restart; the next event then runs with the player disabled. Kinds 2, 7, 8 and 9 hand over
  first. The shipped two-event courses start with kinds 7, 2 and 8 (London `crash7` and `crash12`,
  SF `crash7`), so it never shows with the shipped data. Kind 8's hand-over does leave the finished
  mark set, so water in SF `crash7`'s second event is ignored.
- A first-event jump never counts damage (above).
- No contact: the player's damage limits stay changed until the session ends (restarts included),
  and a later-event no-contact never sets them.
- No contact, step 3: the list can be done and the player wrecked in the same frame: the pass is
  recorded and then a fail.

## Checked against

`tools/game/races.py` reads all 52 event files (13 courses × 2 skill levels × 2 cities):

- every row has the eight fields; kinds used: SF 0, 2, 3, 4, 5, 7, 8; London 0, 2, 3, 4, 5, 7,
  9; two-event courses London `crash7` (7, 2), `crash12` (2, 7), SF `crash7` (8, 3); field 8 is 1
  for every follow (2) and destroy (8) event and 0 otherwise; only-target (field 7) only in SF
  `crash4` (`oneeighty`, kind 7) and London `crash4` (`map`, kind 9); minimum speeds 50 (SF) and
  40 (London); time limits 0 (SF `crash2`, minimum speed) to 200 s; traffic densities 0 – 0.7.
- every named waypoint file exists (`corner0waypoints`, `frogger0waypoints` in SF use race-style
  names), 2 – 24 waypoints each, well under 31.
- Machine code: the field order and types, the 10-event array, the kind table, the layout choice
  (4 only for kind 0), the countdown lengths (5.0 / 2.5), the strict corner go, 1.0 s under-speed
  time, default 50 mph, 10 m / 100 m follow distances, 150000 / 75000 damage, the no-contact limits
  10 / 5 / 0, the cop check only in kinds 3 and 4, the timer warning only in kinds 1 and 5.
