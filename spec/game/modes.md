# Mode rules: Checkpoint, Blitz, Circuit and Cruise

Each race mode is a small state machine run once per frame (`races.md`, "One frame", step 1). This
page gives the states of the Checkpoint race, Blitz and Circuit, and what is left of Cruise; the
Crash Course is `crash-course.md`. Words used: **the list** is the race's waypoint list and its
"done", "off", "shown" states (`waypoints.md`); **hold**, **go**, **restart**, the clocks, **lost**
and **finished** are defined in `races.md`; **t** is the state timer; **wrecked** means the
player's car has `damage` > `MaxDamage` (`../physics/damage.md`); **damage rules on** means the
player has not chosen the no-damage option. "Message *n* for *d* s" puts text *n* on the HUD's main
line for *d* seconds (`hud.md`); sounds by their file name (`../audio/speech.md`, "Race-mode
one-shots"). "Disable the player" sets the player's override (throttle 0, brake 1, steering −1,
`../engine/controls.md`, "Player overrides").

## Checkpoint race (mode 1)

Layout 2: the gates 1 … *n* − 2 in any order, then the finish *n* − 1 (`waypoints.md`). Opponents,
cops and traffic from the race data. No time limit.

| State | Each frame |
|---|---|
| 0 wait | While the pre-race camera runs (`races.md`, "Cameras"): nothing. When it has handed over: reset and stop the clocks, t = 2.5, state 1, play `Startracelow`. |
| 1 ready | t −= *F*. If t ≤ 1.25: state 2, t = 1.25, play `Startracelow`. Else message 179 for 1.25 s. |
| 2 set | t −= *F*. If t ≤ 0: state 3, play `Startracehigh`, start the music, message 181 for 1.25 s, **go**, start and reset the clocks. Else message 180 for 1.25 s. |
| 3 racing | **a.** If the list is done: the player has **finished**: state 5; stop the race clock; post-race camera; disable the player; with *k* = the number of opponents finished so far: message 183 + *k* for 5 s (191 if *k* > 7); the HUD place shows *k* + 1 of *m* + 1 (*m* = opponents); stop the music; play `Endofracetag` if *k* = 0, else `Youlose`; the player's place is *k* + 1 and the finished count becomes *k* + 1; add the player's row to the results (`results.md`); with a driver, record the result and save (`progression.md`); set "next race" (`results.md`); t = 5. **b.** Else if damage rules are on and the player is wrecked: the list off; play `Damgelose`; stop the music; silence the player's engine; make the car not drivable (brake and neutral); message 182 for 5 s; stop the clocks; post-race camera; t = 5; state 4. |
| 4 lost | Lock the pause menu. t −= *F*. When −1 < t ≤ 0, and the player is wrecked or the "in the water" mark is set (`races.md`, "Water"), and the race is not finished: open the end-of-race menu. |
| 5 finished | Lock the pause menu. t −= *F*. When −1 < t ≤ 0 and no other menu is open: show the results. |

After the state step, every frame and in every state: the standings (`results.md`); from state 3
on, the opponents' gate counts and finish tests (`results.md`, "Opponents finishing").

## Blitz (mode 4)

Layout 3: all gates in any order, no finish model. No opponents (the data give 0). A countdown with
the race's time limit. Blitz does **not** wait for the pre-race camera.

| State | Each frame |
|---|---|
| 0 | Reset and stop the clocks; out-of-time = false; the HUD shows the countdown; stop the countdown and set it to the limit; t = 5.0; state 1; play `Startracelow`. |
| 1 ready | t −= *F*. If t > 1.25: message 158 for 1.25 s. Else state 2, t = 1.25, play `Startracelow`. |
| 2 set | t −= *F*. If t > 0: message 159 for 1.25 s. Else state 3, play `Startracehigh`, message 160 for 1.25 s, **go**, start the clocks and the countdown, reset the clocks, start the music. |
| 3 racing | **a.** If the countdown < 10 and not out-of-time: the timer warning (sound spec). **b.** If the list is done: if out-of-time: stop the warning; the list off; hide the last waypoint; the poor results speech (`../audio/speech.md`); play `Youlose`; message 161 for 5 s; stop the clocks and the countdown; disable the player; t = 5; state 4 (**lost**). Otherwise the player has **finished**: state 5; post-race camera; disable the player; stop the race clock and the countdown; stop the warning; message 164 for 5 s; stop the music; play `Endofracetag`; place 1; add the player's row to the results; with a driver, record and save; "next race" (`results.md`); t = 5; nothing more this frame. **c.** If the countdown ≤ 0: stop the warning; if not yet out-of-time: out-of-time = true, play `Youlose`, message 161 for 5 s, the HUD stops showing the countdown. The race goes on. **d.** If damage rules are on and the player is wrecked: stop the warning; the list off; hide the last waypoint; play `Damgelose`; the damage-penalty speech; stop the music; silence the engine; car not drivable (brake and neutral); message 162 for 5 s; post-race camera; stop the clocks and the countdown; t = 5; state 4. |
| 4 lost | Lock the pause menu. t −= *F*. When −1 < t ≤ 0: open the end-of-race menu. |
| 5 finished | As in the Checkpoint race. |

So in Blitz running out of time does not stop the race: it only marks it lost, and reaching the
last gate afterwards ends it as lost (no results).

## Circuit (mode 3)

Layout 1: the gates in fixed order, waypoint 0 is the start/finish line, the race's laps
(`waypoints.md`). Opponents from the race data (the player may choose fewer), no time limit, no
HUD arrow.

| State | Each frame |
|---|---|
| 0 wait | As the Checkpoint race (waits for the pre-race camera; t = 2.5; `Startracelow`). |
| 1 ready | As the Checkpoint race with message 165. |
| 2 set | As the Checkpoint race with messages 166 (set) and 167 (go). |
| 3 racing | **a.** If damage rules are on and the player is wrecked: make the car not drivable (brake and neutral); play sound instance 4 (`Messagenote` in Circuit, `../audio/speech.md`); message 168 for 5 s; t = 5; state 6. **b.** With a driver: if the HUD's completed-laps number equals the next lap to store (1 at the start), store that lap's time (`results.md`, "Circuit lap times") and add 1 to the next lap. |
| 4 | Never entered in single player. |
| 5 finished | As the Checkpoint race. |
| 6 penalty | t −= *F*. When t ≤ 0: clear the car's damage (as the Cruise repair, `../physics/damage.md`), make it drivable (first gear from neutral or reverse), state 3. The gates still count and the clocks run meanwhile. |

After the state step, in state 3 or 6: if the list is done the player has **finished**: state 5;
the HUD place shows *k* + 1 of *m* + 1; post-race camera; disable the player; stop the race clock;
switch the player's damage counting off; message 169 + *k* for 5 s (177 if *k* > 7); store the last
lap's time; the HUD total = the sum of the stored lap times 1 … laps; stop the music; play
`Endofracetag` if *k* = 0, else `Youlose`; place *k* + 1; the player's results row (the race clock,
`results.md`); with a driver save; record the result (`progression.md`); "next race"; t = 5. Then,
every frame: the standings and, from state 3 on, the opponents' gate counts and finish tests.

The water rule (car back at the last gate, the race goes on) and falling through are in
`races.md`.

## Cruise (mode 0)

The free-roam mode has no waypoints, clocks, standings or arrow. What it does:

- Start: a random AI intersection (`../engine/controls.md`, "Free-roam start"), with the Cruise
  settings (`races.md`, "The session's settings"); traffic from `race/<RaceDir>/roam.aimap`
  (`roam.aimap_p` at Professional) and the city aimap (`../ai/traffic-population.md`), cops from its
  `[Police]` list (`../ai/police.md`).
- State 0 goes to state 1 at once. State 1: if the player is wrecked, the car is held (not drivable,
  brake and neutral), t = 3.0, state 2. State 2: t −= *F*; when t ≤ 0 the damage is cleared, the
  car is drivable again, state 1 (`../physics/damage.md`, "What damage drives"). (The check does not
  look at the no-damage option, but with that option no damage is counted.)
- Water and falling through: a full restart (`races.md`). The pre-race speech line plays at every
  restart (`../audio/speech.md`); the music is the Cruise set (music spec).
- The HUD map shows the cops (`hud.md`).

## Checked against

Machine code of `midtown2.exe`: the state numbers and their order in each mode (including the
Checkpoint and Circuit wait for the pre-race camera and Blitz's immediate 5.0 s count), the
messages (179 – 191, 158 – 164, 165 – 177; Blitz 163 and Circuit 178, "all races beaten", are not
shown by these modes), the 1.25 s / 5.0 s durations, the −1 < t ≤ 0 menu window, the Circuit
5 s penalty and the Cruise 3 s repair, which sound instance each event plays. Data: every shipped
Blitz row has Opponents 0 and a time limit of 18 – 120 s; every Circuit row 2 – 4 laps
(`tools/game/races.py`).
