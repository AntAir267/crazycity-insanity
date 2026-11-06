# HUD: what the driving screen shows

What MM2 shows over the 3D view while driving, when, and from which values. Our engine draws its
**own** HUD (layout, art, fonts and colours are ours), so this page specifies the information,
its formatting, its timing and its rules, not MM2's look; where MM2's look carries meaning (a
colour that tells something) it is said. The map is `hudmap.md`; the cockpit view's gauges are
`../formats/dash.md`; the keys and the saved view bytes are `../engine/controls.md`; texts are
named by their number in the player's `mmlang.dll` (`../formats/mmlang.md`), never copied.
Race rules (when a race starts, finishes, is lost) belong to the race-modes spec (milestone 4);
this page says what the HUD does at those moments.

## Elements

| Element | Shows | Value |
|---|---|---|
| **Race timer** | minutes, seconds, hundredths | one of the race timers (below) |
| **Speed** | whole mph | trunc(*v* × 2.236025), `../engine/controls.md`, "HUD values"; up to three digits, no leading zeros |
| **Gear** | one character | R reverse, P neutral, D any forward gear in automatic, else the gear number (same page) |
| **Rev bar** | a bar | fraction = engine RPM / `MaxRPM` (same page); drawn as trunc(fraction × bar length) units, nothing at 0 |
| **Damage bar** | a bar going from green through yellow to red | fraction = damage / maximum damage (`../physics/damage.md`), clamp to 1 |
| **Mouse steering marker** | a marker on a bar | only with the mouse control device: the current steering −1 … 1 |
| **Primary message** | one line, for a set time | race messages (below) |
| **Secondary message** | one line under it | completes the primary (a time, a place); cleared with it |
| **Place** | "*p*/*n*" | the player's place and the number of racers |
| **Checkpoints** | "*k*/*N*" | checkpoints passed of the course's total |
| **Lap** (Circuit) | "*k*/*laps*" and one row per finished lap with its time | |
| **Direction arrow** | a 3D arrow over the car | points to the current target checkpoint (below) |
| **Opponent markers** | a marker over each opponent car (or over each Blitz checkpoint), with the opponent's place as a digit | below |
| **CD player** | track number, play/stop | below |
| **Chat** (multiplayer) | up to five lines | lines leave after 15 s without new text |

Speed, gear, rev and damage bars together form the **gauges overlay**, toggled with H (saved view
byte 5). It cannot be switched on while the cockpit (dash) view is shown, whose own needles show
the same values; leaving the dash view brings the overlay back if byte 5 is set.

## Per mode

| Mode | Timer shown | Panel | Arrow | Markers |
|---|---|---|---|---|
| Cruise | none | none | no | opponents if any (none in single player) |
| Checkpoint race | race time, counting up | place and checkpoints | yes | opponents, with their place |
| Circuit | race time, counting up | place, checkpoints, lap, lap times | **no** | opponents, with their place |
| Blitz | time left, counting down (then overtime, below) | checkpoints only | yes | one over each checkpoint (no digit); markers forced on |
| Crash Course | time left (some lessons: none) | checkpoints (some lessons: none) | most lessons | some lessons: markers forced on |
| Cops and Robbers (multiplayer) | time left in timed games | team scoreboard | yes (to the gold or base) | name tags |

Crash Course lessons by event type (`mmcrashdata`/course files, milestone 4): types 0, 3, 5, 6, 7
show timer, arrow and checkpoints; type 9 timer and checkpoints but no arrow; type 4 like 0 but
the timer only when the course has a time limit; types 2 and 8 hide the arrow and the checkpoint
panel and force the markers on, and only type 8 shows a timer. A forced-on marker setting is
also written to the saved view byte 7, so it stays on afterwards.

Gauges, messages, the CD player and the map exist in every mode.

## Timers

The HUD keeps three race timers; each counts up or down from a start value and runs only while
started:

- **Race clock** (counts up from 0): shown in Checkpoint and Circuit; gives the player's split
  and lap times and the player's result time (the time recorded, `progression.md`). It stops when
  the player finishes.
- **Opponent clock** (counts up from 0, started and reset with the race clock): opponents'
  finishing times. It keeps running after the player finishes.
- **Countdown** (counts down from the time limit): shown in Blitz, Crash Course and timed
  multiplayer games. The start value is the race's time limit (race settings, milestone 4).

In single player the clocks advance by the frame time *dt* (the clamped frame step) each frame
the game is not paused, so recorded times are sums of frame steps. (In multiplayer they follow the
real clock.) A countdown that reaches 0 stays at 0 and stops.

**Timer display**: *t* = the shown timer, at least 0; *t*′ = *t* + 0.005; hundredths =
trunc(frac(*t*′) × 100); seconds = trunc(*t*′) mod 60; minutes = trunc(*t*′ × (1/60)) mod 100.
Shown as `MM:SS:hh`, two digits each with leading zeros (the 0.005 rounds to the nearest
hundredth).

**Times in messages and tables** (splits, lap rows, results, records screens): for *t* > 0,
*t*′ = *t* + 0.005, hundredths = trunc(frac(*t*′) × 100), *M* = trunc(*t*′) div 60, *S* =
trunc(*t*′) − 60·*M*, written `M:SS:hh` (minutes without padding). *t* ≤ 0 is shown as a dash
placeholder ("no time").

**Time warning** (Blitz and Crash Course countdowns): below 10 s left (and not yet out of time) a
warning beep sounds about once a second, the first at once; at 3 s or less it plays
continuously. It stops at the finish, at time-out, at a wreck and on reset. (Sound file
`Timerwarning`, sound spec.)

## Race messages

"Upper" is the countdown position (high on the screen), "lower" the normal one (low on the
screen); our layout may differ but should keep the two priorities apart. A new primary message
replaces the old one and clears the secondary. When the primary's time runs out both lines are
cleared. Numbers are `mmlang.dll` texts.

**Start countdown** (Checkpoint, Circuit: after the pre-race camera has finished; Blitz: at
once). A countdown timer of 2.5 s (Blitz 5.0 s): while it is above 1.25 s the first text is
shown; for the next 1.25 s the second; then the start text for 1.25 s and the race begins (the
clocks are reset and started). Each countdown text is re-posted every frame (upper line). Sounds:
`Startracelow` at the first and second text, `Startracehigh` at the start.

| | Checkpoint | Circuit | Blitz |
|---|---|---|---|
| first, second, start text | 179, 180, 181 | 165, 166, 167 | 158, 159, 160 |
| finished in place 1 – 8 (5 s, upper) | 183 – 190 | 169 – 176 | win: 164 |
| finished in place 9 or later (5 s, lower) | 191 | 177 | — |
| wrecked (5 s, lower) | 182 | 168 | 162 |
| out of time (5 s, lower) | — | — | 161 |

- Finishing sound: `Endofracetag` for first place (and a Blitz win), `Youlose` otherwise.
  Wrecked: `Damgelose` (Circuit: `Messagenote`).
- Checkpoint race wrecked: the race is lost (timers stop; after 5 s the pause menu). Circuit
  wrecked: the car cannot move for 5 s and is then repaired where it stands, the clocks running
  on (a time penalty).
- Blitz out of time: text 161 with `Youlose`, and the countdown turns into an overtime clock
  counting up from 0; the race goes on, but finishing it then shows 161 again and is a loss.
- **An opponent finishes** (Checkpoint, Circuit; only while the player is still racing): the
  opponent's name in the primary line (5 s, lower) and the place phrase in the secondary line, sound
  `Messagenote`. Names: text list 13 – 20 (opponent *k* is the *k*-th name); phrases: list 21 – 28
  (1st … 8th).
- **Checkpoint passed** (every mode except Crash Course): the race clock as a time (Circuit: the
  current lap's time) for 1 s, lower line.
- **Lap finished** (Circuit): text 63 (lap time), or 62 when the last lap begins, for 1 s, with
  the finished lap's time in the secondary line.
- **In the water** (any mode): the city's water text — `MapName` exactly `london` 642, exactly `sf`
  643 (compared case-sensitively), any other city 30 — for 3 s, upper line. It is posted on the
  first frame of the water episode (the frame the water timer starts and the water camera begins,
  `races.md`) and posted again on every following frame while the timer, before that frame's step,
  is still below 0.1 s, so it shows until 3 s after the last of those frames. What follows (reset,
  loss, put back at a checkpoint) is the mode's water rule (`../physics/collision.md`, "World
  limits"; milestone 4).
- Falling out of the city shows nothing.
- **Crash Course** lessons post their own instructions and results (1.25 s upper for
  instructions, 5 s for results, 3 s for some hints): event type 0 texts 205 – 209, 614, 615;
  1: 210 – 216; 2: 217 – 223; 3: 192 – 198, 652; 4: 231 – 240 (232 a format with the target speed);
  5: 224 – 230; 6: 199 – 204; 7 and 9: 241 – 245, 609; 8: 617 – 622; the pass mark 651. Which text
  when belongs to the Crash Course spec.
- **Multiplayer** (details and timing: `multiplayer.md`): countdown, finishing and other race
  texts 143 – 153 (checkpoint), 89 – 99 (blitz), 100 – 111 (circuit), 112 – 142 (Cops and
  Robbers; 138 – 142 "minutes left", 2 s), 154 – 157 (Cruise); network notices 38 – 47 go to the
  chat lines with an alert sound.

## Place, checkpoints and laps

- **Place** "*p*/*n*", *n* = opponents + 1, updated every frame until the race ends: *p* = 1 +
  the number of opponents that have passed more checkpoints than the player (the player's count
  starts at 1), or have finished, or have passed as many and are nearer (squared distance) to the
  player's current target checkpoint. At the finish it shows the final place.
- **Checkpoints** "*k*/*N*", *N* = the course's points − 1 (the start does not count, the finish
  does): 0/*N* at the start, then 1, 2, … each time another checkpoint is passed, ending at *N*/*N*.
  (MM2 counts by a 32-bit mask of passed points, so a course of more than 32 points can miss
  counts; our engine counts correctly.)
- **Circuit**: the checkpoint row shows "*i*/(*count* − 1)" with *i* the number of the checkpoint
  just passed; the lap row "1/*laps*" at the start and "*k* + 1/*laps*" after lap *k*; one row
  per lap ("1.", "2.", …), hidden until that lap starts, then showing that lap's running time,
  fixed when the lap ends.

## Direction arrow

A green or yellow arrow floating in front of the camera points horizontally toward the current
target checkpoint:

- Placement (camera space): at (0, 2.5, −6.1), drawn unlit and over everything. Direction: *d* =
  the target moved to the camera's height, taken into camera space, normalised. The model's **−Z**
  end is the tip (`hudarrow01` points at z −1.18, its tail ends at +0.74), so its local axes in
  camera space are *Z* = −*d*, *X* = (*Z*.z, 0, −*Z*.x), *Y* = *Z* × *X* (*X* and *Y* are not
  normalised: when *d* has a vertical part the arrow comes out slightly narrower and flatter).
  Then the whole arrow is turned by −20° (right-handed) about the camera's X axis through its own
  position, which tips the point **down**: a target straight ahead gives a tip direction (0, −0.342,
  −0.940).
- **Colour**: green while the target is in front of the camera (camera-space *z* < 0), yellow when
  it is behind (*z* > 0); unchanged at exactly 0. So it turns yellow when the target is more than
  90° to the side, not 45°.
- **Target**: the waypoint list's current target, updated every frame; how the target is chosen
  (nearest after each gate, the S / X keys, stepping in order) is `waypoints.md`, "Nearest target
  and the S / X keys".
- **Hidden**: in modes without an arrow (above), once the waypoint list is done, and while the
  HUD is hidden (below). Model `geometry/hudarrow01.pkg`, material set 0 green, set 1 yellow
  (`hudarrow_blitz01` and `hudarrow_cc01` ship but are never used).

## Opponent markers

On or off with I (saved view byte 7; on for a new profile). Each marker is a camera-facing
triangle 4 – 8 m above the object, in the object's colour, and, when it has a place number 1 – 8,
a digit above that:

- single-player opponents: purple (180, 0, 255); in Checkpoint and Circuit the digit is the
  opponent's current place (its finishing place once finished);
- Blitz: a cyan (0, 255, 255) marker 5 m above every checkpoint, no digit;
- multiplayer: each player's name over their car when within 300 m of the camera.

There is no distance limit in single player.

## CD player

MM2 can play the tracks of an audio CD in the drive while driving (keys 2 – 5: show/hide, play
or stop, previous, next; tracks stop at the first and last, no wrap; while it plays the game's
music is muted). It is unavailable when the MM2 CD itself is in the drive. Our engine may offer
the player's own music in its place or leave it out.

## When the HUD is hidden

- While a look key (numpad) is held in the bumper or dash view: everything except the race timer
  is hidden (gauges, cockpit, panel, CD player, messages, arrow), and shown again on release.
- The pause menu (Esc, single player) hides the same and stops the clocks; closing it restores
  them. In multiplayer Esc does not pause and the clocks run on.
- Results and other in-race menus hide the same parts.
- There is no key that hides the whole HUD (H is only the gauges overlay).

## Small screens

MM2 uses half-size digit and bar art when the screen is narrower than 640 pixels (`*_half`
textures). This is a matter of look; our layout scales as it likes.

## Checked against

- `tools/game/mmlang.py` (`work/check/mmlang.txt`): the shipped record times format as stated —
  26.145267 s shows 00:26:15 (message form 0:26:15), 49.547646 s shows 00:49:55, and 59.996 s
  rounds up to 01:00:00; a time of 0 shows the placeholder in messages.
- The same tool confirms that every text number named on this page, `menus.md` and
  `progression.md` exists in the shipped `mmlang.dll` (402 numbers, none missing), and that the HUD
  textures `DIGI_0` … `DIGI_9`, `DIGI_COLON`, `DAMAGE`, `DAMAGE_LABLE`, `SPEED_TICKS` and their
  `_half` versions are all in `mm2tex`.
- `hudarrow01.pkg` has two material sets, `s_hudarrow_green` and `s_hudarrow_yellow`;
  `hudarrow_blitz01` (red/yellow) and `hudarrow_cc01` (violet/yellow) also ship. Its 7 vertices
  lie in one plane (y −0.11): the tip (0.002, −0.11, −1.177), the head ±0.58 wide at z −0.29, the
  tail ±0.23 at z 0.741; with the −20° turn a target straight ahead gives the tip direction
  (0, −0.342, −0.940) (`tools/game/mmlang.py`).
