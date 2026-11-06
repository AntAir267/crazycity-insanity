# Waypoints and gates

Every race in MM2 is a list of **waypoints** read from a small text file: the first one is where the
player starts, the others are the **gates** the player has to drive through. This page specifies
the file, how a waypoint becomes a gate (its line across the road), the exact test that decides
whether a car has crossed it, the five ways a race uses its list (any order, fixed order, laps …),
the target the HUD arrow points at, and the counting of the opponents' gates that the standings
use. What each game mode does when the gates are done is `modes.md` and `crash-course.md`; the
session around it is `races.md`.

Arithmetic is single precision with one rounding per operation, in the order written (MM2 runs the
x87 at 24-bit precision), unless a step says otherwise. "Body position" P and "rows" X, Y, Z of a
car are its rigid-body position (centre of mass) and rotation rows (`../physics/vehicle-body.md`);
Z points backwards (the car faces −Z). `InertiaBox` is the car's tune vector (`../physics/vehicle-tune.md`).

## Which file

| Mode | File | Read with |
|---|---|---|
| Checkpoint race *N* | `race/<RaceDir>/race<N>waypoints.csv` | the race list (50 slots) |
| Circuit *N* | `race/<RaceDir>/circuit<N>waypoints.csv` | the race list |
| Blitz *N* | `race/<RaceDir>/blitz<N>waypoints.csv` | the race list |
| Crash Course event | `race/<RaceDir>/<Filename>.csv`, `<Filename>` from the event row (`crash-course.md`) | the event list (32 slots) |

`<RaceDir>` is the city's race folder (`../formats/city-files.md`, "City list"). Names are looked up
in the archives without regard to case (`../formats/ar.md`).

## The position file

Text, one header line, then one waypoint per line:

```
x,y,z,a,radius,frame rate,state changes,texture changes,msg
-1163.801880,64.850098,-402.126556,-90.912460,13.000000,0,0,0,
```

Reading rules (MM2's own, keep them):

1. Lines are read at most 127 bytes at a time; a longer line is cut there and its rest is read as
   the next line. The first line is skipped whatever it holds (the column names differ between
   files and are never checked).
2. On every following line the fields are taken in order with "comma" as the separator, the way C
   `strtok` does it: a run of commas counts as one separator and leading commas are skipped (so an
   empty field shifts the rest left).
3. Fields 1–4 are decimals (C `atof`): **x, y, z** (metres) and **a**, the heading in **degrees**.
   Fields 5–8 are integers (C `atoi`, so `13.000000` gives 13 and `8.7` gives 8): field 5 the
   **radius** *r*, field 6 the **sphere flag** (called "frame rate" in the files), fields 7 and 8
   unused. The rest of the line after field 8, up to the carriage return or line feed, is a
   message text that nothing uses (empty in every shipped file).
4. A line with fewer than eight fields makes MM2 crash (it reads a missing field). **Engine:**
   treat missing fields as 0. A blank line counts as a line too (in MM2 it crashes): **engine:**
   skip it.
5. A radius of 0 becomes **15** (a negative radius is kept). The radius is then used as a float.

**Slots.** The list has a fixed number of slots: 50 for races, 32 for crash-course events. MM2
stores at most slots − 1 lines; when the (slots − 1)-th line is stored it also appends one more
waypoint at (0, 0, 0) with heading 0, radius 0 (→ 15), flag 0, and ignores any further lines. So
a race file of 49 or more lines ends with an extra gate at the world origin (no shipped file comes
near: the longest has 46 lines).

**Missing file.** The list is empty: the race starts at (0, 0, 0) with heading 0 and can never be
finished. **Engine:** refuse to start the race and say which file is missing.

### Zero headings filled in

A heading of exactly 0 (or a NaN) in the file means "aim along the route". While the list is built,
in file order: after waypoint *i* is made (*i* ≥ 2), if waypoint *i* − 1 still has heading 0, its
heading becomes

```
a(i−1) = −57.295776 × atan2(x(i−1) − x(i), z(i−1) − z(i))
```

with the two differences in single precision, `atan2` and the product in at least double
precision, the result rounded to single. Waypoint 0 is never changed, nor waypoint 1 if waypoint 2
is missing. In a **Circuit** list one more rule runs at the last waypoint *n* − 1: if its file
heading is 0, it gets −57.295776 × atan2(x(n−1) − x(0), z(n−1) − z(0)) (it aims back towards the
start line). The other lists leave the last waypoint's 0 as it is.

The **start** of the race is waypoint 0: its position and its heading (never filled in) — see
`races.md`, "Placing the cars".

## The gate of a waypoint

A waypoint with position (x, y, z), heading *a* and radius *r* is a vertical gate whose line runs
across the direction of travel, *r* to each side of the centre:

```
h = a × 0.017453292
c = cos h, rounded to single           s = sin h, kept unrounded
A = (x + (c × r), z + (s × r))          B = (x − (c × r), z − (s × r))
```

(*c* × *r* is rounded once; *s* × *r* is computed from the unrounded sine and rounded once; then
the sums and differences are rounded.) Heights are not used: the gate is a line on the map,
infinitely tall. A heading changed by the fill-in rule recomputes A and B.

The heading in degrees also orients the waypoint's model and is the heading a car is put down
with at that waypoint (start, circuit water reset): turn angle θ = *a* × −0.017453292.

**Drawn as** (material set 1 of the package in the Crash Course, 0 everywhere else, `../formats/pkg.md`):
`geometry/pt_check.pkg` (a race's last waypoint in a Checkpoint list:
`pt_finish.pkg`; in a Circuit list waypoint 0 is `pt_finish` and the rest `pt_check`; every Blitz
and Crash Course gate is `pt_check`), scaled by *r* across and along and by 7.5 upwards, placed at
(x, y + 3.75, z) and turned by θ about +Y. A gate is drawn only while it is **shown** (below).

## Has a car crossed a gate?

The test asks whether the car's footprint, taken as three short line segments on the map, crosses
the gate line A–B.

**Segment test** T(p1, p2, q1, q2, *t*): does segment p1–p2 cross segment q1–q2, with tolerance *t*?
(2D points (x, z).)

1. Bounds of each segment, each widened by *t*: lo_x(p) = min(p1.x, p2.x) − *t*, hi_x(p) =
   max(p1.x, p2.x) + *t*, the same for z and for q. Here min(u, v) = u if u < v or the comparison
   is unordered, else v; max(u, v) = u if u > v, else v.
2. Δp = p1.x − p2.x, Δq = q1.x − q2.x. Slopes: m_p = 0 if Δp = 0 or NaN, else (p1.z − p2.z) / Δp;
   m_q likewise. Offsets: b_p = p1.z − m_p × p1.x, b_q = q1.z − m_q × q1.x.
3. Crossing point (X, Z):
   - if Δp = 0 (or NaN): X = p1.x, Z = m_q × p1.x + b_q;
   - else if Δq = 0 (or NaN): X = q1.x, Z = m_p × q1.x + b_p;
   - else X = (b_q − b_p) / (m_p − m_q), Z = X × m_p + b_p.
4. Crossed when lo_x(p) ≤ X, X is not above hi_x(p), lo_z(p) ≤ Z, Z not above hi_z(p), and the same
   four for q. A NaN X or Z fails the "lo ≤" tests (parallel lines give ±∞ or NaN and fail).

It is a line intersection followed by a box check, not a true segment test; keep it as it is.

**The player's car** (every frame, against one gate A–B), with hx = `InertiaBox`.x × 0.5, hy =
`InertiaBox`.y (the whole height), hz = `InertiaBox`.z × 0.5:

1. Front point F = (P.x + (−hz) × Z.x, P.z + (−hz) × Z.z); rear point R = (P.x + (hz + 2.0) × Z.x,
   P.z + (hz + 2.0) × Z.z) (hz + 2.0 rounded first). So the long segment reaches 2 m behind the
   tail.
2. Crossed if T(R, F, A, B, hx), or T(P − hy·Y, P + hy·Y, A, B, hx) (x and z parts only:
   (P.x + (−hy) × Y.x, P.z + (−hy) × Y.z) and (P.x + hy × Y.x, P.z + hy × Y.z)), or
   T(P − hx·X, P + hx·X, A, B, 0) (likewise along X, no tolerance).

The second segment lies along the car's up axis, so it is almost a point for a car on its wheels
and long for a car on its side or roof. The pose is the one the physics left at the end of the
previous frame (`races.md`, "One frame").

**Sphere gates.** In an unordered Crash Course list (layout 4 below) a waypoint whose sphere flag is
not 0 is passed when ((dz·dz + dy·dy) + dx·dx) < *r* × *r*, with d = P − (x, y, z) (body position,
3D; a NaN passes nothing).

**An opponent's car** (the standings count, below) uses the same three tests with other sizes:
sx, sy, sz = the opponent's `InertiaBox`; long segment from (P.x + (−sz) × Z.x, P.z + (−sz) × Z.z)
to (P.x + sz × Z.x, P.z + sz × Z.z) with tolerance sx × 5.0; up segment ± (sy × 5.0) along Y with
tolerance sx × 5.0; side segment ± (sx × 5.0) along X with no tolerance. So opponents get a gate
five car-widths wide.

## The five layouts

A race list is used in one of five ways (MM2's "type"):

| Layout | Used by | Order | End |
|---|---|---|---|
| 1 laps | Circuit | fixed: 1, 2, …, *n* − 1, then 0 ends a lap | after the race's laps |
| 2 checkpoints + finish | Checkpoint race | any order among 1 … *n* − 2; the last (*n* − 1) is the finish and counts only when all others are passed | the finish |
| 3 any order | Blitz | any order among 1 … *n* − 1 | when all are passed |
| 4 any order + finish | Crash Course, jump events | as layout 2, but sphere gates allowed | the finish |
| 5 fixed order | Crash Course, every other event | 1, 2, …, *n* − 1 | the last |

### State

| Item | Meaning |
|---|---|
| passed(*i*) | per waypoint |
| shown(*i*) | per waypoint: drawn, and eligible as an arrow target |
| count | passed waypoints, **starting at 1** (waypoint 0 counts) |
| target | the waypoint the arrow points at; in layouts 1 and 5 the only gate that can be passed |
| last passed | the index of the waypoint passed last (0 at the start) |
| lap, laps | layout 1: laps completed (0 at the start) and laps to drive (the race's lap count); 1 or 0 elsewhere, unused |
| done | all gates finished |
| off | the gate test is switched off (the race was lost) |
| only-target | Crash Course option (`crash-course.md`): only the target is shown |

### Reset (race start and every restart)

1. off = done = false; target = 1; last passed = 0; count = 1; passed(*i*) = false for all.
2. shown(*i*) = true for all, or false for all with only-target.
3. Layouts 2, 4, 5: waypoint 0 hidden; layout 2 with *n* ≥ 3 also hides the finish.
   Layout 3: waypoint 0 hidden. Layout 1: every waypoint shown, lap = 0.
4. Every layout except 1: passed(0) = true.
5. With only-target: all hidden, then the target shown.
6. The HUD arrow is switched on, except in Circuit (which never shows it) and the Crash Course
   events that switch it off (`crash-course.md`).

A crash-course list that held more waypoints in an earlier event keeps the extra slots passed and
hidden.

### Passing a gate

When waypoint *i* is passed ("register *i*"): last passed = *i*; passed(*i*) = true; shown(*i*) =
false; count += 1; the HUD shows the split time (race clock, Circuit: the lap time so far;
not in Crash Course) and the gate sound plays (sound spec, `../audio/speech.md`, "Race-mode
one-shots").

### Every frame

Skipped while the list is empty or off. Otherwise, with the player's pose:

**Layout 3 (Blitz).** Find the lowest *i* with not passed(*i*) whose gate the car crosses. If there
is one and not done: register *i*; if some waypoint is still not passed, pick the **nearest
target** (below); else done = true.

**Layouts 2 and 4.** Find the lowest *i* with not passed(*i*) that the car crosses (layout 4: the
sphere test for waypoints with the sphere flag, the gate test for the others). If found and not
done:
- *i* = *n* − 1 (the finish) and count = *n* − 1 (all others passed): done = true.
- 0 < *i* < *n* − 1: register *i*. Then in a Checkpoint race: if count = *n* − 1, show the finish;
  pick the nearest target. In the other modes: if *i* was the target, step the target forward
  (below). Then with only-target and not done: show the target. If count = *n* − 1: show the
  target, and the "final checkpoint" cues fire (music and speech, `../audio/speech.md`).
- The finish crossed too early is ignored (nothing happens, it stays not passed).

Only the lowest crossed index counts in a frame; two gates crossed together count on consecutive
frames if the car still crosses the second one.

**Layout 1 (Circuit).** If not done the HUD lap clock is updated. Test only the target's gate. If
crossed and not done: hide the target; register it; then
- if the target is 0 (the start/finish line): lap += 1; if lap = laps: done = true; else if lap =
  laps − 1 the "final lap" message and music cue, else the "lap time" message; the lap time is
  posted (`results.md`, "Circuit lap times"); if not done: passed(*i*) = false for all and every
  waypoint shown again (a new lap);
- else if the target is *n* − 1 and lap = laps − 1: the "final checkpoint" cues;
- target = target + 1, wrapping to 0 after *n* − 1.

**Layout 5.** Test only the target's gate. If crossed and not done: hide it, target += 1, register
the old target; if count = *n*: done = true. With only-target and not done: show the new target.

After the layout's step, if not done the arrow points at the target (below); once done the arrow
is switched off.

### Nearest target and the S / X keys

**Nearest target** (Checkpoint and Blitz, after each gate): among waypoints 1 … *n* − 1 that are not
passed and are shown, take the one with the smallest d² = ((dz·dz + dy·dy) + dx·dx), d = waypoint
position − the player's body position, strictly smaller than the best so far (starting at 1.0e9; a
NaN counts as smaller). If none qualifies the target stays. (MM2 only replaces the target while it
is not 0, which it never is in these layouts.)

**S / X** (next / previous waypoint, `../engine/controls.md`) work only in a Checkpoint race: if
count = *n* − 1 the target becomes the finish. Otherwise step the target by +1 (S) or −1 (X)
modulo *n*; landing on 0 or *n* − 1 gives 1 (S) or *n* − 2 (X); stop at the first not-passed
waypoint, or give up when back at the starting target. **Step the target forward** (layout 4)
is the S rule.

**The HUD arrow** points at the target's position (x, y, z) every frame while the list is not done
(drawing: HUD spec, `hud.md`).

## Opponents' gates (for the standings)

The race keeps, per opponent *k*, a gate count (starting at 1) and, in a Checkpoint race, a set of
passed indices. Once per frame from "go" on (`races.md`, "One frame", step 1), before the
opponent's finish test (`../ai/opponents.md`, "The finish test"):

- **Checkpoint race:** for *i* = 1 … *n* − 1 not in *k*'s set, if ((dz·dz + dy·dy) + dx·dx) ≤ 2500.0
  (d = opponent body position − waypoint; 50 m, NaN passes) and the opponent crosses gate *i*
  (opponent sizes above): add *i* to the set, count += 1, stop.
- **Circuit:** if the opponent crosses gate (count mod *n*), count += 1 (no distance limit, fixed
  order, counting on across laps).

These counts only feed the standings (`results.md`); finishing is the opponent's own finish test.
MM2 keeps the set as a 32-bit mask with bit (*i* mod 32), so in a list longer than 32 waypoints
gate *i* and gate *i* − 32 share a bit; reproduce (no shipped Checkpoint list has more than 13).

## What the race gives the opponent finish test

When a Checkpoint race or Circuit is set up, the finish line of `../ai/opponents.md` is set from the
list: F = the position of waypoint *n* − 1 (Checkpoint) or waypoint 0 (Circuit), U = (0, 0, 1)
turned about +Y by θ = *a* × −0.017453292 of that waypoint, i.e. (sin θ, 0, cos θ) (each rounded to
single). Blitz and Crash Course do not set it (it keeps the last value).

## Checked against

`tools/game/races.py` (dirty side) reads, by these rules, every position file a menu can reach
in both cities (12 Checkpoint, 10 Circuit and 10 Blitz races per city, and every crash-course event
file named by the 26 + 26 event files) and draws each list with its gate lines on the city map
(`races.py plot` → `work/check/races/<RaceDir>_<race>.png`; report `work/check/races.txt`):

- 122 lists, 1,114 waypoints; every line after the header has the eight numeric fields and an
  empty message; no list reaches its slot limit (longest race list 23 waypoints, longest event
  list 24); no shipped waypoint has a radius of 0 or a sphere flag; radii 3 … 36. Ten waypoint
  headings are exactly 0, all where the fill-in never looks, so it never fires: London `race2`
  waypoint 0, and the last waypoint of SF `race11`, of the SF event lists `follow`, `exam1_3` and
  `final` (each read at both skill levels, so counted twice) and of London `blitz0` and `blitz8`.
- The heading as the facing of a car put down there (θ = *a* × −0.017453292, car facing −Z) points
  within 90° of the direction from the previous waypoint for 646 of the 846 gates of the ordered
  lists (median |cos| 0.937), so the gate line runs across the road as described.
- Driving a box the size of `vpbug`'s `InertiaBox` (2, 2, 3) straight from each waypoint to the
  next with the crossing test above crosses every one of the 846 gates through their centres;
  0.6 *r* off centre 722 are crossed, against 298 when every gate is turned by 90° (the test does
  depend on the gate direction). The drive: the box upright (Y = (0, 1, 0)), facing from waypoint
  *i* − 1 to waypoint *i* (Z = −(that direction), X = its right side), its body position on the
  line between the two centres shifted by the offset along X, at y = 0, moved in steps of 0.5 m from
  4 m before waypoint *i* − 1 to about 8 m past waypoint *i*; a gate counts as crossed if the test
  passes at any step. (These counts only show that the gate direction matters; a different path
  gives different counts.)
- Missing files: none (every race named in a `.cinfo` and every event file exists).
