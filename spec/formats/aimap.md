# AI settings (`.aimap`)

Two kinds of text file share the extension:

- the **city aimap**, `city/<c>.aimap`: traffic rules and populations of a city (speed limit, side
  of the road, ambient car models, pedestrian models, traffic-light models);
- the **race aimap**, `race/<c>/<race>.aimap` (or `.aimap_p`): settings for one game mode or race
  (police, opponents, per-road exceptions, optionally a different ambient car mix).

Both are read before the road network (`bai.md`). Each reader knows a fixed set of section headers;
everything else in a file is ignored.

## Which race file

The race name comes from the game mode and the race number *N*:

| Mode | Race name |
|---|---|
| Cruise (free roam) | `roam` |
| Checkpoint race | `race`*N* |
| Circuit race | `circuit`*N* |
| Blitz | `blitz`*N* |
| Crash course | `crash`*N* |
| two multiplayer modes | `multicop`, `croam` |

The file is `race/<c>/<name>.aimap`; at the **Professional** skill level the game reads
`<name>.aimap_p` instead. A missing file leaves every race setting at its default.

## Reading rules (both kinds)

1. The file is read line by line; a line is at most 127 bytes (a longer line is cut and its rest
   read as the next line).
2. A line **starts a section** when it begins with the section's header text; the comparison is
   case-sensitive and covers only the first *k* characters given in the tables below (so
   `[Ambient Types/Density]` matches anything starting `[Ambient Type`). A line that starts no
   section (comments `#…`, blank lines, unknown headers, `#[Subway]`) is skipped.
3. **Value sections**: the line after the header is the value line, parsed with the format shown
   (C `scanf` rules: leading blanks skipped, `%d` integer, `%f` decimal float, `%s` a word). If
   parsing fails the setting keeps its default. The value line is not checked for `#` or `[`.
4. **List sections**: the line after the header holds the entry count (`%d`, 0 if unreadable).
   Then lines are read until that many entries are stored: a line starting with `#` is skipped and
   does not count; a line starting with `[` ends the list early and is **lost** (the next section
   header is not seen); any other line is one entry, parsed with the row format. (A row that does
   not parse leaves undefined fields in MM2; a new engine should skip such a row but still count
   it.)
5. After a section is handled, reading continues with the next line.

Exceptions to rule 5: after `[Ped Pool]` (city) and `[AmbientLaneChanges]` (race) the **value
line** is itself compared with the remaining headers of the same pass (it never matches in practice).

Names (`%s`) are at most 31 characters (MM2 stores them in 32 bytes without a length check).

## City aimap

| Header (characters compared) | Kind, format | Default | Meaning |
|---|---|---|---|
| `[Ped Pool]` (10) | value `%d` | 100 | pedestrian pool size (pedestrian spec) |
| `[Speed Limit]` (13) | value `%f` | 15.0 | base road speed limit (`bai.md`, load step 9) |
| `[Subway]` (8) | value `%s %d` | none, 1 | subway-train car model and cars per train; without this section no trains run (`traffic-control.md`, "Trains") |
| `[Ambients Drive On The Left]` (28) | value `%d` | 0 | non-zero: traffic drives on the left (`bai.md`, load step 8) |
| `[Ambient Types/Density]` (13) | list, rows `%s %f %d` | empty | ambient car types (below) |
| `[GoodWeatherPedName / BadWeatherPedName]` (24) | list, rows `%s %s` | empty | pedestrian model pairs: dry-weather model, rain model (pedestrian spec) |
| `[Traffic Lights]` (16) | value `%s %s` | `sp_traflitsingle_f`, `sp_traflitdual_f` | traffic-light models for approaches with one lane and with two or more lanes (`traffic-control.md`) |

**Ambient types.** Each row: model name (a vehicle in `tune/` and `geometry/`, e.g. `va_cab_l`),
a **cumulative** probability threshold, and an integer (0 in every shipped file; no reader found).
To pick a type for a new ambient car the game draws r in [0, 1) and takes the first row whose
threshold is greater than r (traffic spec). The thresholds therefore rise to 1.0 and a model can
appear in several rows (London lists `va_compact_s` twice). If the **first** row's threshold is
negative, all thresholds are replaced by (1 / count) · (row number + 1), giving equal shares.

## Race aimap

| Header (characters compared) | Kind, format | Default | Meaning |
|---|---|---|---|
| `[AmbientLaneChanges]` (20) | value `%d` | 1 | non-zero: ambient cars may change lanes (traffic spec) |
| `[CopChaseDistance]` (18) | value `%f` | 250 | police chase range (police spec) |
| `[Ambient Types/Density]` (13) | list, rows `%s %f %d` | empty | if not empty, **replaces** the city's ambient types for this session |
| `[GoodWeatherPedName / BadWeatherPedName]` (24) | list, rows `%s %s` | empty | if not empty, replaces the city's pedestrian models |
| `[Exceptions]` (12) | list, rows `%d %f %f` | empty | per-road overrides: road index, traffic density, speed limit (below) |
| `[Police]` (8) | list, rows `%s %f %f %f %f %d %d %f %f` | empty | police cars: model, x, y, z, heading in degrees, two integers, two floats (police spec; missing trailing fields default to 0, 15, 0.5, 50) |
| `[Opponent]` (10) | list, rows `%s %s %f %d %f %f %d %d %d %d %d %f` | empty | opponents (below; `../ai/opponents.md`) |
| `[Hookmen]` (9) | list | empty | hookmen (not in scope) |

`[Speed Limit]`, `[Density]` and `[Ambients Drive On The Left]` appear in many race files but the
race reader does not know them: they have no effect. The police heading is stored as
degrees × −0.017444445 (radians, sign flipped).

**Exceptions.** A road listed in `[Exceptions]` (by road index, `bai.md`) gets the row's speed
limit instead of the city's (even 0), and the traffic spawner treats it specially using the row's
density (traffic spec). Exceptions appear in race, blitz, crash and stunt files; the Cruise
(`roam`) files list none.

**Opponent rows.** The twelve fields, in file order:

| # | Format | Field | If the row stops before it |
|---|---|---|---|
| 1 | `%s` | car model | undefined (the row is unusable) |
| 2 | `%s` | route file name (`race0-a-0.opp`; `opp.md`) | undefined |
| 3 | `%f` | throttle level *k*_thr | 1.0 |
| 4 | `%d` | switch 1 | see below |
| 5 | `%f` | pad *k*_p | 50.0 |
| 6 | `%f` | brake threshold *k*_b | 0.7 (float 0.69999999) |
| 7–11 | `%d` ×5 | switches 2, 3, 4, 5, 6 | see below |
| 12 | `%f` | corner factor *k*_c | 1.0 |

The switches are the driver's six switches in this order (`../ai/driver.md`, "Orders"); each is
stored as "value ≠ 0". The four numbers are set to their defaults before the row is parsed, so a
short row keeps the defaults for the numbers it lacks. The switches are not: MM2 parses them into
six scratch integers that only this reader writes and then copies all six into the row whatever
the parse reached. So a switch the row does not give takes the value that switch had in the
**latest earlier opponent row of the same file** that gave it, and if no earlier row did, it is
**undefined** in MM2 (left-over memory). *Our choice* for the undefined case: switch 1 = 0,
switches 2–5 = 1, switch 6 = 0 (the values MM2 writes into the row before parsing and then
overwrites). The parse result is not checked otherwise.

Shipped data: 536 of the 537 opponent rows have all twelve fields. The one short row is the only
row of `race/sf/stunt0.aimap` (`vpford opp-c0.2 1.00`: three fields, so pad 50, brake 0.7, corner
1.0 and all six switches undefined in MM2); no game mode reads a `stunt` file.

## Checked against

`tools/ai/aimap.py` (dirty side) reads every `.aimap` in `work/data` by these rules:

- `london.aimap`: speed limit 15, drive on the left, 13 ambient types (`va_compact_s` < 0.07,
  `va_diesels_s` < 0.13, … `va_cab_l` < 0.82, `va_compact_s` < 0.98, `va_ddbus_l` < 1.0), 2
  pedestrian pairs, traffic lights `sp_traflitsingle_ped_l` for both, `[Subway]` commented out
  (`#[Subway]` / `#va_ug_l 3`), so no subway trains.
- `sf.aimap`: speed limit 15.00, drive on the right, 11 types (`va_bus_f` < 0.01 … `va_smallsuv_s`
  < 1.0), 2 pedestrian pairs, no `[Traffic Lights]` (defaults `sp_traflitsingle_f` /
  `sp_traflitdual_f`).
- 207 race aimaps (`race/london`, `race/sf`, `.aimap` and `.aimap_p`); 88 have exceptions (e.g.
  `race/sf/race2.aimap`: 12 roads with density 0 and speed limit 0).
- Only `race/london/roam.aimap`, `roam.aimap_p` (and the unused `roambak.aimap`) have ambient
  types: 12 rows without `va_cab_l` (`va_compact_s` < 0.07 … `va_ddbus_l` < 1.0). So **Cruise in
  London uses that list, not the city's**; Cruise in SF uses the city's.
- Cruise files: London `roam.aimap` 20 police, SF `roam.aimap` 19 (`roam.aimap_p` 20); lane changes
  on, chase distance 250 (default) everywhere.
- Every ambient, police, pedestrian and traffic-light model named in these files has a
  `geometry/<name>.pkg`.
