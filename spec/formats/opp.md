# Opponent route files (`.opp`)

An opponent's route file gives where the opponent car starts, which intersections it drives
through and where it ends. Each `[Opponent]` row of a race aimap (`aimap.md`, "Opponent rows")
names one file in its second field. What the game does with the values is `../ai/opponents.md`.

## Where the file is

`race/<RaceDir>/<name>`, where `<RaceDir>` is the city's race-data name (`city-files.md`) and
`<name>` is the row's second field exactly as written, extension included (the game adds
nothing). Lookups are case-insensitive (`ar.md`). The shipped names follow
`<race>-a-<k>.opp` (amateur) and `<race>-p-<k>.opp` (professional), *k* = the row number, but
any name works: some Crash Course rows name files such as `exam1_3.opp`, `Follow-1.opp` or reuse
a race's file (`race0-a-6.opp`).

A row whose file does not exist: MM2 leaves the opponent's route and its start undefined
(left-over memory; the car stays where its first reset put it). **Engine:** treat it as a file
with no points (below). No shipped row names a missing file.

## Layout

Text, one point per line.

1. **Lines.** The file is read in pieces of at most 127 bytes: a piece ends after a line feed
   (the line feed belongs to the piece) or after 127 bytes, whichever comes first; the end of the
   file, or a byte 0xFF, ends the last piece. Every piece is a line, also an empty one (a lone
   `\r\n` is a line) and the rest of a line longer than 127 bytes.
2. **Header.** The first line is skipped whatever it holds. Shipped files have
   `x,y,z,brake,forward offset,side offset,target speed,speed start,side start`.
3. **Points.** Every following line is one point; *N* = the number of these lines. From each
   line the game takes the first four **tokens**, where tokens are the runs of characters between
   commas (`,` only: consecutive commas count as one separator, and a leading comma is skipped, as
   C's `strtok` does). Each token is converted like C's `atof`: leading white space skipped, the
   longest prefix that forms a decimal number (sign, digits, point, exponent), 0.0 if there is
   none. The values are, in order, **x**, **y**, **z** (each rounded to float) and the **heading**
   *h* in degrees (kept in double precision until used). Further tokens are ignored: the columns
   `brake`, `forward offset`, `side offset`, `target speed`, `speed start`, `side start` are never
   read. A line with fewer than four tokens makes MM2 read a missing token (it crashes);
   **engine:** missing values are 0.0.

## Meaning of the points

| Point | Use |
|---|---|
| 0 | the **start**: reset point *q* = (x, y, z) and start heading θ = float(*h* × 0.017444445), a float product rounded once (*h* as a double times the float constant 0.017444445); only point 0's heading is used |
| 1 … *N* − 2 | **route points**: each gives one route entry, in order |
| *N* − 1 | the **destination** *D* = (x, y, z) |

The heading's sign is the opposite of the police list's (`aimap.md`, police rotation × −0.017444445):
with the placement rule of `../physics/vehicle.md` ("Placing the car in a city") a start heading
of +90 makes the car face −X.

**Route entries.** For route point *k*: room ρ = Find((x, y, z), 0)
(`../physics/city-collision.md`, "Finding the room of a point"); the entry is the index of the
**first intersection** (kind 3) in ρ's component list (`../ai/road-network.md`, "Room lists"). If
ρ's list holds no intersection, MM2 prints an error and leaves that entry unwritten (undefined).
**Engine:** leave the point out (the route has one entry fewer).

The route length is *n* = *N* − 2 (MM2 stores *N* − 2 even when entries were left unwritten; with
the engine's choice it is the number of entries written). The route is an ordered list of
intersection numbers that may repeat an intersection, list two consecutive equal entries, or list
two consecutive intersections no road joins; it is used as it is (`../ai/driver.md` copes).

Room checks: MM2 rejects a point only when its room is negative, which the room search never
returns (0 means outside every room), so every point is used. For a point in room 0 the start and
the destination are taken as they are and a route point finds no intersection.

**Fewer than two points.** MM2 then stores a negative route length and leaves the destination
undefined. **Engine:** *N* = 0: start (0, 0, 0) with heading 0, no route, destination (0, 0, 0);
*N* = 1: point 0 is the start and also the destination, no route. No shipped file has fewer than 6
points.

## Checked against

`tools/ai/opponents.py opp` (dirty side) reads every shipped `.opp` with these rules and resolves
the routes with the room lists of `road-network.md`:

- 612 files (London 324, SF 288), all with the header above; every data line has 9 comma-separated
  columns; no blank lines, no line longer than 127 bytes, no line with fewer than four tokens.
  8,510 (London) and 5,377 (SF) points; routes of 4 … 58 intersections (London, mean 24.3) and 5 …
  42 (SF, mean 16.7).
- Every point of every file is inside a room (none in room 0); every start point is on the road
  network (a road, shortcut or intersection component at its position).
- Route points without an intersection in their room: 16 in London files and 30 in SF files, all
  in files no created opponent uses (`blitz3/4-*.opp`, unused `circuit5-*`, `race5-*-7`,
  `race11-*-6/7`), except London `crash3-0.opp` point 23 (room 689 lists only road 255), named by
  the Crash Course aimaps `crash3` and `crash11`.
- Consecutive equal route entries in used files: London `race2-*-5` (19), `race6-*-4` (315),
  `race9-a-1`, `-a-6`, `-p-1`, `-p-4` (294); SF `race10-p-3` (174) and every `circuit6` opponent
  (37). Consecutive entries with no joining road occur only in unused or Crash Course files (SF
  `crash10-0.opp` 242 → 254, `race6-a-8.opp` 141 → 307).
- Circuit routes: London 131 of 138 files and SF 103 of 119 end at the intersection they start
  with; of the used ones, London `circuit3-p-2 … -5` (254 … 253) and SF `circuit6-*` (37 … 42) do
  not.
- Example: `race/sf/race0-a-0.opp` has 9 points; point 0 is (−1152.0404, 64.850098, −395.00256)
  with heading 90.032082° → θ = 1.5705597 rad; the route is the 7 intersections 61, 60, 37, 186,
  43, 195, 118; the destination is (−1777.4153, 25.707066, −821.31543).
