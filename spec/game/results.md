# Results: finishing order, standings, score, next race

What the race modes compute about who is ahead and who finished, the results table, the score, what
a finished race hands to the records, Circuit lap times, and the "next race" button. Recording
itself (record files, hall of fame, rewards, open races) is `progression.md`; display is `hud.md`.
Words as in `modes.md` and `races.md`; *m* = the number of opponents created.

## Finishing order

One counter, **finished**, starts at 0 at every restart. Each racer who finishes takes place
finished + 1 and the counter goes up:

- **The player** (Checkpoint, Circuit): when the mode sees the list done (`modes.md`); the
  player's place is the number of opponents finished before plus 1. Blitz: always place 1. Crash
  Course: one row, place 1.
- **Opponents finishing** (Checkpoint and Circuit, every frame from "go" on, also after the player
  has finished or lost): for each opponent *i* in creation order, after its gate count
  (`waypoints.md`, "Opponents' gates"): if it has not finished and its finish test fires
  (`../ai/opponents.md`, "The finish test"): finished += 1; its place is finished; its results row
  is added with the opponents' clock (`races.md`, "Clocks"); if the player is still racing (state
  3), the HUD shows the opponent's name (text 13 + *i*) for 5 s with the place phrase (text 20 +
  place) on the second line and `Messagenote` plays.

An opponent that never finishes gets no row.

## Standings (HUD only)

Computed every frame in Checkpoint and Circuit, after the state step. "Count" is a racer's gate
count (the player's: the list's count, `waypoints.md`; an opponent's: `waypoints.md`, "Opponents'
gates"); distances are squared, (dx·dx + dy·dy) + dz·dz in single precision, from a racer's body
position to a waypoint's position.

1. **Player's place** = 1 + the number of opponents that have finished, or have a larger count, or
   have an equal count and are closer to the player's **target** waypoint (`waypoints.md`) than the
   player is (strictly).
2. **Each opponent's place** (for its icon): if it has finished, its finishing place. Else with
   *T* = waypoint (its count mod *n*) and *e* = its distance to *T*: 1 + the number of other
   opponents that have finished, or have a larger count, or have an equal count and are strictly
   closer to *T* than *e*; + 1 more if the player has a larger count, or an equal count and is
   strictly closer to *T*.
3. The HUD place display gets (player's place, *m* + 1): in a Checkpoint race in states 0 – 3, in
   Circuit in states 3 – 6 (so a Checkpoint race freezes it after the finish or a wreck, a Circuit
   does not).

These places never decide anything: results use the finishing order.

## The results table

The title is the mode's name (text 6 Checkpoint, 5 Circuit, 7 Blitz, 12 Crash Course) and the
race's name (`races.md`). One row per finished racer at its place: the place number, the name
(the player: the driver's name, text 3 without a driver; opponent *i*: text 13 + *i*), and the
time as text (`hud.md`, time format); a Crash Course pass shows text 651 instead of a time. The
results also show the reward message when the result unlocked something (`progression.md`). They
are shown 5 s after the finish (`races.md`, "Losing, finishing and the menus").

Times in the rows: the player — the race clock when the mode saw the finish (Circuit: the sum of
its lap times + 0.005, below); opponents — the opponents' clock at their finish frame.

## Score

score = trunc(`ScoringBias`) × *f* × trunc(Difficulty), integers, with `ScoringBias` from the
player's car's `tune/<car>.info` (`../formats/car-model.md`), *f* = 50, 25, 10 for places 1, 2, 3
and 0 for any other place, Difficulty from the race row (`races.md`). Blitz uses place 1 (50). The
Crash Course records 1. Each float is truncated on its own before the integer products.

## What a finished race hands to the records

The detailed rules are `progression.md` ("After a race: what is recorded"); in short, when the
player's result is final:

| Mode | When | Time | Passed | Score |
|---|---|---|---|---|
| Checkpoint | the finish, if run with the race's own cops, traffic, time of day and weather | race clock | passed before, or place ≤ 3 (Amateur) / place 1 (Professional) | as above |
| Circuit | the finish, if also with the race's laps and opponent number | the **best lap** | place ≤ 3 / place 1 (no "passed before" test) | as above |
| Blitz | the finish in time, race number < 12, own settings | race clock | 1 | place 1 |
| Crash Course | the course passed or failed, course number ≤ 12 | 1.0 | 1 / 0 | 1 |

Nothing is recorded after a chat cheat code. Checkpoint and Blitz record only with an active
driver; Circuit runs its recording step in any case (the record and hall-of-fame files decide,
`progression.md`). A lost race records nothing (except a failed Crash Course).

## Circuit lap times

The race clock runs from "go" without stopping at the line. With *L* = the sum of the completed
laps' times (0 at "go"):

- every frame while racing, the HUD's current lap time = race clock − *L*;
- when the start/finish gate completes a lap: lap time = race clock − *L*, then *L* += lap time (each
  in single precision); the HUD shows it ("lap time" or "final lap", `waypoints.md`); on the last
  lap the race clock stops and is set to *L* + 0.005;
- the mode stores a completed lap's time in slot *lap* (`modes.md`, Circuit state 3 b, only with a
  driver, and once more at the finish) and keeps the **best lap**: the first stored lap, then any
  stored lap time below the best (equal does not replace it). The best lap is cleared at every
  restart.

The HUD total at the finish is the sum of the stored slots 1 … laps.

## Next race

The results offer "next race" when:

| Mode | Offered |
|---|---|
| Checkpoint | the race is not the city's last, a driver is active, and the next race is open (`progression.md`, "Open races") |
| Circuit, Blitz | the race is not the city's last of its kind |
| Crash Course | the course number is not 2, 6, 10, 11 or 12 (fixed in the program; courses 3, 7, 11 and 12 open only after others are passed, `progression.md`) |

Choosing it (all modes): race number + 1 (Checkpoint with MM2's "everything open" switch: modulo
the race count), the session's settings re-applied from the new race's row (`races.md`, "The
session's settings": Checkpoint and Blitz keep the pedestrian density; Circuit takes all; the Crash
Course takes time of day, weather and pedestrians from `mmcrashdata.csv`), the driver's `.sav` and
`.cfg` saved (`progression.md`), and the new race loaded and started as a new session. **Crash
Course:** if the new course has not been passed by the driver, the car is replaced by `vpbullet`
when the city's `RaceDir` is `sf` and by `vpcab` otherwise (not with the "everything open" switch).

## Checked against

Machine code: the place factors {0, 50, 25, 10} and the place clamp (> 3 → 0), the separate
truncations, Blitz's fixed place 1, the standings' comparisons (strict "closer", finished counts as
ahead, the player's target as reference), the opponent message (texts 13 – 20 and 21 – 28), the
Circuit lap arithmetic (+0.005) and best-lap rule, the next-race conditions per mode. Data
(`tools/game/races.py`): `ScoringBias` 4 – 100 over the 20 cars (200 / 100 / 40 points for places
1 / 2 / 3 at bias 4, 5000 / 2500 / 1000 at bias 100), Difficulty 1 in every row; the shipped
records (`progression.md`, `tools/game/profiles.py`) agree: SF Blitz 0 and 1 in `vpford` (bias 15)
score 750.
