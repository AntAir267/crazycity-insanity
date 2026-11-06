# Traffic population: how many cars, which ones, where they appear and vanish

Part of the ambient traffic spec (`traffic.md` is the index and defines the terms: roads, sides,
lanes, the traffic generator `rnd()`, frame time `F`). This page covers the settings, the city's car
mix, the fixed pool of traffic cars, and the rule that fills roads with cars when the player enters a
room and empties them when the player leaves.

MM2 does not spawn traffic by distance or by visibility. Each city room carries a list of **traffic
roads**; while the player is in a room, exactly those roads carry traffic. When the player crosses
into another room, the roads that drop out of the list are emptied at once and the roads that join
it are filled at once, in the same frame, whether or not the camera can see them. Between those
moments cars only drive; a car that would drive onto a road outside the list turns round instead
(`traffic-driving.md`, "Leaving the populated area").

## Settings

| Setting | Values | Effect |
|---|---|---|
| Traffic density *D* | 0 … 1; Cruise default 0.5 (menu slider "Traffic density", range 0–1) | *d* = clamp(*D*, 0, 1) × 0.2 cars per 8 m of lane; *D* = 0 turns traffic off completely |
| Pool size *N* | 300 (a game setting with no menu entry) | number of traffic cars that exist; never more are on the streets |

Race modes take *D* from the race's own data (one value per difficulty); this milestone needs only
Cruise. The engine's "Off" option is *D* = 0. With *D* = 0 MM2 creates no traffic cars at all (and
reads no traffic data).

Useful numbers: *d* = 0.1 at the default (one car per 80 m of lane on average), 0.2 at full (one
per 40 m), 0.05 at a quarter (one per 160 m).

## The car mix

The file formats are in `../formats/aimap.md`. For traffic:

- The **mix** is the race file's `[Ambient Types/Density]` list if it has entries (Cruise:
  `race/<c>/roam.aimap`, or `roam.aimap_p` at the professional level), otherwise the city file's.
  Entries are (name, cumulative threshold, integer); the integer is not used; a negative first
  threshold means an even mix (`aimap.md`). Names may repeat.
- `[AmbientLaneChanges]` (race file, default on) switches lane changes (`traffic-driving.md`).
- `[Exceptions]` (race file) give some roads their own traffic density and speed limit; the Cruise
  files have none.
- The city's `[Speed Limit]` becomes the roads' limits at load (`bai.md`, "Load procedure").

## The pool

At city load MM2 creates the pool of *N* traffic cars, indices 0 … *N* − 1, and gives each its
type, its fixed per-car numbers and its looks. In this order:

1. For every car, in index order: its **lateral offset** draw and its **reaction-delay** draw
   (`traffic-driving.md`, "Per-car constants").
2. Then for every car, in index order:
   1. **Type**: *r* = rnd(); the type is the first mix entry (in file order) whose threshold is
      greater than *r*. If no entry qualifies (thresholds below 1) the car gets no type; MM2 leaves
      such a car unusable — the engine must never place it (keep it out of the free list).
   2. Load the type's model and data (`../formats/aivehicledata.md`; each type is loaded once and
      shared).
   3. **Paint**: two paint-set draws, the second kept (`traffic-visuals.md`, "Paint").
   4. **Driving temperament**: the acceleration-and-gap draw and the speeding step
      (`traffic-driving.md`, "Per-car constants").

Every car starts in the **free list**. On every reset (start of the session and every race reset)
MM2 first restarts the random generator (`traffic.md`, "Randomness"), takes every car off the
streets (a car that is physically simulated is released first, `traffic-reactions.md`), resets each
car's lane state, and pushes the cars onto the free list in index order 0 … *N* − 1. The free list is
last-in-first-out: cars are taken from the front and returned to the front, so right after a reset
the first car placed is car *N* − 1, then *N* − 2, and so on. Then the streets are filled for every
player as if the player had just entered its room from room 0 (below).

## Rooms and traffic roads

The road network gives each city room a **traffic road list** (`../formats/bai.md`, "Activation
tables"). For every player MM2 keeps its current room. Every frame, before any traffic moves (`traffic.md`, "Frame order"):

1. Room = Find(P_c, player's previous room) (`../physics/city-collision.md`, "Finding the room of
   a point"), with P_c the player car's **body position** (the rigid body's position, its centre of
   mass; not the model origin).
2. If the room is not 0 and differs from the previous room: **adjust** (below) with old = previous
   room, new = room.
3. Store the room as the player's previous room, **every frame, 0 included** (so after a frame in
   room 0 the next adjust runs from room 0: nothing leaves, and only roads no player has populated
   are filled). The pedestrians use this same room and memory (`pedestrians.md`, "Rooms and
   pedestrian roads").

At a reset the room is Find(q + CoG, 0), the body position the car is placed at
(`../physics/vehicle.md`, "Placing the car in a city"), and it becomes the stored room.

Each road keeps one "wanted" bit per player (four players at most). Each populated road is on the
**active road list**, which new roads join at the front.

### Adjust(old room, new room, player)

1. *Leaving* = the roads in old's list that are not in new's list (in old's list order).
   *Entering* = the roads in new's list that are not in old's list (in new's list order).
   Room 0 has an empty list.
2. For each leaving road that is on the active list: clear this player's wanted bit; if no bit is
   left, **empty** the road (below) and take it off the active list. (A leaving road not on the
   active list is not touched.)
3. Total lane length *L*, a float starting at 0: for each entering road, unless it is an exception
   road (race file `[Exceptions]`): if side +1 carries traffic (side flag bit 0 clear),
   *L* = ((centre length − 5) × lanes(+1)) + *L*; then the same for side −1. Centre length is the
   road's centre-line length (last cumulative centre distance minus the first); "− 5" is the 5 m
   left free at the road's ends.
4. Target count *n* = trunc(*L* × *d* / 8) (MM2: ftol((*L* × *d*) × −0.125), then negated);
   spacing *s* = *L* / (*n* + 1). Two running values start at 0 for this adjust: the
   accumulator *a* and the carry *c*.
5. For each entering road in order, skip it if any player's wanted bit was already set (another
   player has it populated); set this player's bit; put the road at the front of the active list;
   then for side −1, then side +1, unless that side has traffic flag bit 0 set:
   - **Exception road** (listed in the race file): per-lane count *k* = trunc(*L*₀ × density / 8)
     where *L*₀ is the length of lane 0 of side +1 (whichever side is being filled) and density is
     the exception's value. For each lane *l* = 0 … lanes − 1 of the side: *g* = (length of lane *l*)
     / (*k* + 1); for *i* = 1 … *k*: if the free list is empty stop this lane; take the front car,
     draw *r* = rnd() and place it (below) at distance
     ((*i* × *g*) − *g* × 0.5) + ((*g* − 10) × sin(*r* × 6.28)) × 0.5.
   - **Normal road**: lane length λ = centre length − 5 (the same for every lane of the road). For
     each lane *l* = 0 … lanes − 1: *a* = λ + *a*; offset *o* = 0; while *a* > *s* + 0.001:
     *a* = *a* − *s*; *o* = (*s* − *c*) + *o*; *c* = 0; if the free list is empty stop this lane;
     take the front car and place it at distance min(max(*o*, 0), λ − front bumper of that car).
     After the lane: *c* = *a*.
   So the cars sit every *s* metres along all entering lanes laid end to end (lane after lane,
   side −1 before side +1, road after road), the first one *s* into the first lane.
6. **Placing** a car at distance *x* on (road, side, lane):
   1. Set its road, side, lane (current and list lane) and its distance along the lane = *x*.
   2. Choose its next road (`traffic-driving.md`, "Choosing the next road"). If that fails, stop
      filling this lane (the car stays in the free list).
   3. Compute the point *p* of its route at distance *x* (`road-network.md`, "Point at a route
      distance", placement variant; height 0).
   4. If any race opponent's position is closer than 50 m to *p* (squared distance < 2500), skip
      this position: the car stays at the front of the free list and is used for the next position.
      (Cruise has no opponents.) Exactly: for each opponent in index order, d = its body position
      − *p* with *p*.y = 0 (the placement point has height 0, so d.y is the opponent's own height),
      (d.z·d.z + d.y·d.y) + d.x·d.x < 2500.0 or NaN skips. So an opponent more than 50 m above
      or below y = 0 (on SF's hills) never blocks a position; reproduce. At a world reset the
      opponents are put back at their start points (the AI reset, `opponents.md`) before
      this fill, so the fill sees their start points.
   5. Otherwise take the car off the free list and **start** it: lane state reset (speed 0,
      state "driving", reaction counter = its reaction delay, wheel
      angle 0, rejoin attempt counter 1, breakable parts back on; the indicator keeps its old
      value), sorted-insert it into the lane's list at its distance; its first driving update sets
      up its rail (`traffic-driving.md`, "Starting a car on its rail").
   Placement does not look at the camera, other traffic, the player or props.

Every car placed therefore starts standing still and accelerates from rest.

### Emptying a road

First every section list of traffic registrations on the road (both sides, `traffic-reactions.md`,
"Being seen by other AI") is emptied. Then for side +1, then side −1; each lane in order; each car in the lane's list from the front: take
it out of the lane list, out of its destination intersection's stop-sign lists and, if it is
registered in an intersection, out of that registration; if this lane is the car's
own list lane (a car changing lanes is in two lists and is handled once): if it has a physical body,
that body ends its physics (`traffic-reactions.md`, "End of physics"; it may become a wreck); then
return the car to the front of the free list. A returned car is moved to room 0 (not drawn).

### Other ways cars leave the streets

- At the end of an intersection crossing whose next road is not populated and whose road has no
  traffic on the other side (`traffic-driving.md`, "Arriving on the next road").
- Nothing else: knocked cars, wrecks and cars that gave up ("out") stay where they are until their
  road is emptied or the next reset. A car that gave up while rejoining a road it could not drive on
  is in no lane list, so emptying roads never finds it: it stays (visible, still) until the next
  reset (`traffic-reactions.md`).

## Shipped mixes

| City | Mix source in Cruise | Entries (threshold) |
|---|---|---|
| SF | `city/sf.aimap` (`race/sf/roam.aimap` has none) | va_bus_f .01, va_eurovan_f .13, va_limo_f .26, va_taxi_f .36, va_wedelivertruck_f .40, va_2siter_s .46, va_compact_s .53, va_diesels_s .62, va_largesuv_s .70, va_sedans_s .89, va_smallsuv_s 1.0 |
| London | `race/london/roam.aimap` | va_compact_s .07, va_diesels_s .13, va_largesuv_s .2, va_sedans_s .33, va_smallsuv_s .4, va_2sitersport_l .46, va_cooper_l .53, va_eurocargo_l .60, va_ultrasport_l .66, va_euro_l .73, va_compact_s .93, va_ddbus_l 1.0 |
| London races | `city/london.aimap` | as above but va_largesuv_s .23, va_ultrasport_l .62, **va_cab_l .82**, va_compact_s .98 |

So London Cruise traffic has no black cabs (27 % compacts), London races do. SF: 19 % sedans,
13 % limousines, 12 % vans, 1 % buses.

## Checked against

`tools/ai/traffic.py aimap` and `population` (data in `work/data`):

- Both city files and both `roam.aimap` files parse with the rules above; 88 race files have
  `[Exceptions]` (all with density 0, i.e. roads closed to traffic for that race), only London's
  `roam.aimap` (and an unused `roambak.aimap`) carries its own mix.
- Traffic road lists: SF 1,172 rooms (1,087 with roads, up to 61 roads per room, mean 20.4),
  London 1,342 rooms (1,187 with roads, up to 60, mean 16.3). Total entering lane length on a fresh
  start: SF mean 10.6 km (max 35.8 km), London 4.7 km (max 16.7 km).
- Cars placed on a fresh start in a room (pool 300): SF mean 131 at *D* = 0.5, 223 at *D* = 1
  (often limited by the pool), 66 at *D* = 0.25; London 59, 117 and 29.
