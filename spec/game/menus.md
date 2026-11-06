# Menus: screens, choices and what they set

MM2's front end and in-race menus, by **function**: which screens exist, how the player moves
between them, what each lets the player choose (values, defaults, locks) and what that sets for
the game. Our engine draws its own menus in its own style and may arrange them differently, but
it should offer the same choices with the same effects, so a player can do everything MM2 allows
and MM2 profiles behave the same. Labels come from the player's `mmlang.dll` by number
(`../formats/mmlang.md`). Profiles, records and unlocks are `progression.md` and
`../formats/profiles.md`; the HUD is `hud.md`.

## Game settings the menus produce

Starting a game hands these to the game (they are also what the race modes and the AI specs call
"the race settings"):

| Setting | Values |
|---|---|
| city | one of the city list (`../formats/city-files.md`, "City list") |
| game mode | 0 Cruise, 1 Checkpoint, 3 Circuit, 4 Blitz, 6 Crash Course (2 and 5 are multiplayer), numbering of `../formats/profiles.md` |
| race | race number within the mode, from 0 |
| skill level | Amateur or Professional (from the driver) |
| car, paint | a car of the car list and one of its colours |
| gearbox | automatic or manual |
| time of day | 0 – 3 (morning, noon, evening, night; `../formats/night.md`) |
| weather | 0 clear, 1 cloudy, 2 fog, 3 rain (`../formats/night.md`) |
| traffic density, pedestrian density, cop density | 0 … 1 each |
| opponents | 1 – 10 (Circuit; others from the race file) |
| laps | 1 – 8 (Circuit; Checkpoint is always 1) |
| time limit, difficulty | from the race file only |

**Defaults when a mode or race is chosen** (the race list resets to race 0 when the mode changes;
the same defaults are loaded again, for the driver's last mode and race, every time the menus are
entered, after each game and when a driver is chosen):

- Cruise: pedestrians 0.25, traffic 0.5, cops 1.0, weather 0, time of day 1.
- Checkpoint, Circuit, Blitz: every value from the race's row of the race-settings file for the
  driver's skill level (`mmracedata.csv`, `mmcircuitdata.csv`, `mmblitzdata.csv`, milestone 4):
  opponents (not Blitz), cops (the count, used as the cop setting), pedestrian density, traffic
  ("ambient") density, time of day, weather, time limit, difficulty; laps from the file for
  Circuit, 1 for Checkpoint.
- Crash Course: weather, time of day and pedestrians from `mmcrashdata.csv`; traffic 0,
  opponents 8, cops 1.0.

## Screens and flow

```
start ─► intro movie ─► Main
Main ─┬─ Crash Course ─► Crash Course city (London / San Francisco) ─► Crash Course menu ─┬─► Car ─► game
      │                                                                              └─► game (fixed car)
      ├─ Single player ─► Race menu ─► Car ─► game
      ├─ Quick race ─► Car ─► game (with the race menu's current settings)
      ├─ Multiplayer ─► network screens (multiplayer spec)
      ├─ driver list (choose the current driver), New driver, Delete driver
      ├─ Driver record, Race statistics
      ├─ Options ─► Audio / Graphics / Controls (─► Customise controls) / About
      └─ Quit (confirm)
game ─► results or pause menu ─► back to the Race menu (or the Crash Course menu after a course;
        multiplayer: the network arena)
```

A navigation bar is available on most screens: options, minimise, help, quit, back. "Back"
(Escape) returns to the screen the player came from. The intro movie is `../formats/mmlang.md`,
"Intro movie". A first start without drivers creates the default driver silently
(`progression.md`, "Saving").

### Main

- **Driver list**: the drivers of `players.dir` in order; choosing one makes it current
  (`progression.md`). A panel shows the current driver: skill level (text 82 Amateur / 81
  Professional), last race (text 80 for Cruise, 79 for Cops and Robbers, 78 for a crash course,
  else the race's name from the city's `.cinfo` lists), last car (its `Description`), control
  device (list 580 – 584) and, for a Professional only, the total score (`progression.md`).
- **New driver**: a name (at most 18 characters) and Amateur / Professional (both always
  available; Amateur preselected). Refused: an empty name (nothing happens), a duplicate name
  (message), more than 18 drivers (message).
- **Delete driver** (confirm): refused with a message for the last driver.
- **Driver record**: per race type (checkpoint, circuit, blitz) the summed scores and passed
  counts over London and San Francisco, and the driver's details (`progression.md`, "Totals").
- **Race statistics**: the hall of fame of a city (`../formats/profiles.md`): for each race the
  five best times at Amateur, the five best times at Professional, or the five best Professional
  scores (one of the three at a time; the Amateur score table is never shown), chosen with the
  race type (blitz, circuit, checkpoint) and the city (San Francisco, London).
- **Replays**: MM2's main menu has a hidden replay button; replays are not part of this spec.

### Race menu

1. **Mode**: Blitz, Circuit, Checkpoint, Cruise (Cops and Robbers only when hosting a network
   game). A mode with no races in the chosen city is unavailable and the choice falls back to
   Cruise.
2. **Race**: the city's race names of that mode (`.cinfo`). Races that are not open
   (`progression.md`, "Open races") are listed but cannot be chosen; the next/previous buttons
   skip them. Blitz and circuit races are all open.
3. **City**: the city list; changing it reloads the race lists and settings of that city.
4. **Environment**: time of day, weather, traffic density, cop density, pedestrian density;
   Circuit also laps (1 – 8) and opponents (1 – 10, at most the race file's opponent count).
   Laps and opponents are not shown for Checkpoint and Blitz.
5. A preview picture of the race (`jpg/<c>_map<mode><N>.jpg`, e.g. `sf_maprace3`, `london_maproam`;
   hidden when missing).
6. **Choose car** → Car screen.

**Locked settings.** Until the current driver has passed a Checkpoint, Circuit or Blitz race, its
environment settings are fixed at the race's defaults (shown but not editable) — so a first
win always counts for the records. Once passed:

- Checkpoint and Blitz: time of day, weather, traffic, cops and pedestrians can be changed;
- Circuit: time of day, weather, cops, pedestrians, laps and opponents can be changed, but traffic
  stays fixed.

Cruise is never locked. Without a driver, or in a mod city (no records), everything is open and
editable. Changing cops, traffic, time of day or weather (in Circuit also laps or opponents) away
from the race's defaults means the result is not recorded; the pedestrian density does not
matter (`progression.md`, "After a race").

**The cop control** is a 0 … 1 density slider like traffic and pedestrians (19 steps, below), also
in the race modes. Choosing a race puts the row's `Cops` **count** itself into the cop setting
(0, 1, 2 … up to 8); the slider shows any count of 1 or more as full and leaves the count untouched
until the player moves it. Any move writes the slider's own value (0 … 1) into the setting in place
of the count: an arrow key steps by 1/19 from the shown position (so the first step down from a
count of 3 gives 18/19, and a step up at the top writes exactly 1.0 although nothing moves on
screen), a click inside the bar sets the clicked fraction. The count comes back whenever the race's
row is loaded again: choosing the race, its mode or its city, "next race", and every time the menus
are entered (after each game, and when a driver is chosen), which reload all of the race's
settings, so a changed setting lasts for the next game only. In play any value of 1 or more
means every cop (`../ai/police.md`, "How many cops"), so 1.0 and a count of 3 drive the same; but a
race whose count is 2 or more is never recorded once the slider has been touched, and a count of 0
or 1 records again only with the slider back at exactly 0 or 1 (stepping 19 times up from 0 ends at
0.9999998; one more press at the top gives 1.0).

The skill level is the driver's: it is chosen when the driver is created and the menus offer no
way to change it later.

### Car screen

- **Car**: every car of the car list (`../formats/car-model.md`, "Which player cars exist") in list
  order, shown by `Description`, next/previous wrapping around. `Flags` and `Order` do not filter
  or sort the list.
- **Paint**: the car's `Colors` names, wrapping around; each car remembers the paint last chosen
  for it.
- **Gearbox**: automatic (text 634) or manual (633).
- **Facts**: horsepower, top speed, durability and mass from the `.info`, shown as bars scaled per
  value between 0.5 × the smallest and 1.1 × the largest value of all cars in the list.
- **Showcase**: a picture of the car, `jpg/<car>_show.jpg`.
- The car turns on a stand, viewed from `UIDist` metres (`.info`), the camera moving there at
  21 m/s.
- **Locks**: a locked car or a locked paint job (`progression.md`, "Locked cars and paint jobs") is
  shown darkened with a lock sign (MM2 shows a per-car picture saying how to unlock it:
  `<car>_lck`, `<car>_lck<paint>`, with `_p` variants for Professional); starting with it is
  refused with a message. Coming back to this screen while the chosen car is locked selects the
  last unlocked car the player looked at (or the first unlocked one).
- **Go** → loading → game. Before starting, the current driver's `.sav` and `.cfg` are written
  (`progression.md`, "Saving").

### Crash Course

- First the city: London or San Francisco (fixed, the two shipped cities).
- Three sections: the 13 lessons (names: texts 532 – 544 London, 545 – 557 San Francisco), and the
  city's blitz and checkpoint races. Each lesson shows its state from the records: not tried,
  passed, tried but not passed. Lessons that are not open (`progression.md`) cannot be chosen.
- A lesson not yet passed must be driven in its fixed car — vpbullet in San Francisco, vpcab in
  London, paint 0 — and starts at once; once passed, the player picks the car first.
- Lesson settings come from the crash-course data (above).

### Options

Changes take effect while the screen is open; **Done** keeps them and writes the driver's `.cfg`;
**Cancel** (or back) restores the values from the `.cfg`; **Defaults** (confirm) resets the screen.

**Audio** (bits of `../formats/profiles.md`, "Audio option bits"):

| Choice | Values | Default |
|---|---|---|
| sound effects on | on/off | on |
| race commentary on | on/off | on |
| music on | on/off (switching it on turns city ambience off) | off |
| city ambience instead of music | on/off (switching it on turns music off) | on |
| sound device | the system's sound devices | the default device |
| channels | mono, stereo, surround (only with enough positional voices) | stereo |
| sound quality | low (8 channels), medium (16), high (32) | high |
| effects volume, music volume | 0 … 1 | 1, 1 |
| balance | −1 … 1 | 0 |

**Graphics** (the graphics block of the `.cfg`): textured sky (on/off), car reflections (on/off),
pedestrians (on/off; MM2 stores the choice, but no effect of it in the game was found — the
pedestrian density setting is what counts), view distance (100 … 1000 m: the far plane of the 3D
view in a game, fog clamped to it, `../engine/cameras.md`), lighting quality (0 – 3,
whole steps), texture quality (the levels the video memory allows; the recommended one marked),
object detail (4 levels, list 574 – 577), cloud shadows (none / low / high). Also the display
adapter, renderer (software, hardware, hardware with transform and lighting) and resolution, which
MM2 keeps outside the player files and applies after a restart. **Defaults** runs MM2's automatic
detection, which picks from processor speed, video memory and main memory: texture quality 0 below 2
MB of video memory, else 1, or 2 above about 6 MB (at most 1, or 0, on machines with little main
memory); pedestrians off below 300 MHz or in software; view distance / object detail / cloud shadows
400 m / 0 / none, 600 / 0 / low, 800 / 2 / low, 1000 / 3 / high for effective processor speeds below
300, 500, 800 MHz and above (software rendering counts 150 MHz less); lighting 1 + one step above
300 MHz + one above 500 MHz; textured sky with hardware rendering when there is more than about 3 MB
of video memory (in software above 400 MHz); reflections with hardware rendering above 300 MHz. Our
engine picks its own defaults for these and may ignore the detection.

**Controls**: control device (mouse, keyboard, joystick, gamepad, wheel; only mouse and keyboard
when no game controller is connected), automatic reverse (on/off), joystick hat as look-around,
force feedback (on/off, collision strength 0 … 2, road strength 0 … 2; only with force-feedback
hardware), steering sensitivity 0.5 … 2 (mouse, joystick and wheel only), controller dead zone
0 … 0.33, calibrate, and **Customise controls**: the binding table of
`../engine/controls.md` (34 actions, names texts 276 – 309), warning when a key is already used.
The meanings of these values are `../engine/controls.md`.

Sliders move in 19 equal steps between their limits.

**About**: credits and the product number.

## Loading screen

While a game loads MM2 shows a picture with a progress bar: `jpg/<c>_<mode><N>.jpg` (`<mode>`:
`roam`, `race`, `circuit`, `blitz`, `crash`, `multicop`; `<N>` the race number, none for `roam` and
`multicop`; e.g. `sf_race3.jpg`, `london_roam.jpg`); when going back to the menus, `splash.jpg`.
A missing picture falls back to `loading.jpg`. The bar shows the loading progress in percent.

## In-race menus

**Pause menu** (Escape; single player pauses the game, multiplayer does not): resume (Escape again),
restart (the lesson's restart in a crash course), options (audio: effects, music, balance;
controls: steering sensitivity, dead zone, force-feedback strengths, control device; graphics:
object detail, view distance — applied at once —, lighting 1 – 3, cloud shadows, reflections,
textured sky), quit to the race menu, exit to Windows. F4 restarts directly from the menu.

**Keys while driving** (besides the bindings of `../engine/controls.md`): Escape opens the pause
menu; F1 opens the customise-controls screen (pausing a single-player game); F2 pauses or
resumes a single-player game without a menu (the HUD is hidden while paused); F4 restarts the
race at once (the whole game is reset to its start, in any mode); F6 shows the player roster
(multiplayer). In multiplayer the chat key opens the chat box; in single player it only opens
with F7 while several modifier keys are held (a hidden combination).

**Results** (after a race): a table of up to 10 rows — place, name, and the time (`hud.md`, time
format), the score, or "did not finish" (text 499) — and one message line (the reward message,
`progression.md`, "Rewards"). The player's row uses the driver's name (without a driver: the
network name). Buttons: restart, **next race**, race menu, exit to Windows (multiplayer: the
roster). Next race is available in Checkpoint only when there is a driver and the next race is
open; in Blitz and Circuit unless this was the city's last race of the mode; in Crash Course by
the lesson rules. Choosing it writes the driver's `.sav` and `.cfg`, loads the next race's
default settings and starts it.

## Cheats and hidden switches

- Typing `/blubber` into the chat box raises the limit on the bounciness of collisions
  (elasticity product, `../physics/collision.md`, "Impact material") from 1.0 to 4.0 until the game
  is reset (MM2 also hands the same 4.0 to a part of the player's car; what that part does was not
  traced), and marks the session as cheating: from then on nothing is recorded
  (`progression.md`). No other chat word does anything. Our engine may support it.
- A switch that opens every race and makes every setting editable, and one that unlocks every car
  and paint job, exist in MM2 but no menu or key sets them.
- MM2 has unused screens (race environment, city selection, Cops and Robbers settings, driver
  select) that never open.

## Menu layout files (MM2's look; not needed)

`tune/menu.csv` (columns: menu name, background, id, x, y, w, h; the name and background are
ignored) gives each screen's rectangle on a 640 × 480 layout, and `tune/widget.csv` (menu name,
menu id, widget name, widget number, x, y, w, h, description; names ignored) each control's
rectangle by its creation order in the screen; a 0 keeps the program's own value. Both are read
with a header line skipped, lines up to 128 characters, comma-separated, numbers as `atoi`
(several shipped rows have shifted columns). They only position MM2's own art; our engine does
not use them. Screen ids: 1 main, 2 options, 3 audio, 4 graphics, 5 controls, 7 race, 8 car,
9 showcase, 10 – 12 network, 17 new driver, 19 driver record, 20 race statistics, 34 about,
39 crash course, 40 crash-course city, 41 customise controls, and message boxes.

## Checked against

- `tools/game/menus.py` (`work/check/menus.txt`): all 20 shipped cars have `jpg/<car>_show.jpg`
  (compared ignoring case; the file of vp4x4 is `vp4X4_show.jpg`); both cities have every loading
  picture and preview the rules above name (80 each: roam, multicop, race 0 – 11, circuit 0 – 9,
  blitz 0 – 9 with their `map` previews, crash 0 – 12), plus an unused `<c>_crash13.jpg`;
  `splash.jpg` and `loading.jpg` exist.
- Car fact ranges over the shipped cars: horsepower 100 – 1000 (bar 50 – 1100), top speed 25 – 240
  (12.5 – 264), durability 200,000 – 2,000,000, mass 850 – 8500; `UIDist` 5 – 14.
- `menu.csv` (15 rows; id 17 appears three times) and `widget.csv` (294 rows for 43 screens, 8 rows
  with shifted columns) parse with the stated rules.
