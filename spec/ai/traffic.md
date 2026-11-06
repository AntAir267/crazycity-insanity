# Ambient traffic

How Midtown Madness 2's ambient traffic works: which cars exist, where they appear, how they drive,
how they react and what happens when they are hit. Traffic cars are scripted: they move along
curves laid through the road network's lanes, not under the vehicle physics, until something knocks
them out of their lane; then they become simple physical bodies until they come to rest.

| Page | Contents |
|---|---|
| `traffic.md` (this page) | terms, frame order, what traffic uses from other specs, randomness, check runs |
| `traffic-population.md` | density setting, car mix, the pool of cars, filling and emptying roads as the player moves |
| `traffic-driving.md` | per-car constants, the rail, speed and gaps, intersections, turns, lane changes |
| `traffic-reactions.md` | noticing and avoiding players, rejoining, horns, police, obstacles, being hit, the physical body |
| `traffic-visuals.md` | position and orientation from the rail and the ground, drawing, wheels, paint, shadow, lights |
| `../formats/aivehicledata.md` | `tune/vehicle/<name>.aiVehicleData` and the other files of a traffic car type |

Related pages by other writers: `road-network.md` (lane paths, the next-road choice, look-ups),
`traffic-control.md` (lights, stop signs, "may a car enter"), `../formats/bai.md`,
`../formats/aimap.md`, `police.md` and `driver.md` (AI cars, yield marks).

## Terms

- **Road**, **section**, **side** (B = +1 travels the road's direction, A = −1 against it), **lane**,
  **open side**, **exit intersection**, **outgoing side**: `road-network.md`, "Terms". Lane 0 is next
  to the centre line in the file data; for cities that drive on the left the road network mirrors
  the lanes at load, making lane 0 the kerb lane (`bai.md`, "Load procedure").
- **Traffic road list** of a room: `bai.md`, "Activation tables" (the first of the two per-room
  tables).
- **Active road**: a road currently populated with traffic (`traffic-population.md`).
- **Pool**, **free list**: the fixed set of traffic cars, and those not on the streets.
- **State** of a car: driving 0, knocked 2, rejoining 3, avoiding 4, out 6
  (`traffic-reactions.md`, "States").
- *F*: the frame time in seconds (`../physics/vehicle.md`, "Time step": the clamped frame
  duration). Traffic behaviour runs once per frame with step *F*; only knocked cars' bodies run in
  the physics steps.
- rnd(): the next number in [0, 1) from the traffic random generator (below).

## Frame order

1. **AI update** (once per frame, before the frame's physics steps):
   1. For each player (in the order players were added): room tracking and, on a room change,
      filling and emptying roads (`traffic-population.md`, "Rooms and traffic roads"); then the
      player's own AI bookkeeping.
   2. **Traffic cars**: for each active road, most recently activated first; for side A (−1), then
      side B (+1); for each lane in order; for each car in that lane's list from the front
      (furthest along) to the back: update the car if this lane is its list lane (the next car in
      the list is remembered before the update, because the update may move the car to another
      list). Each car is updated at most once per frame; a car in a list of a road that is not
      active is not updated.
   3. Pedestrians (`pedestrians.md`, "Frame order").
   4. Opponents and police (`police.md`, `driver.md`; their yield marks are cleared before and set
      after them).
   5. Cable cars and subway trains (`cable-cars.md`).
   6. The traffic body bookkeeping: drop bodies that need it, declare every knocked car with a
      body as a mover for this frame's physics (`traffic-reactions.md`); the indicator blink clock
      adds *F*. Then the light sets advance (`traffic-control.md`), so cars see a light change on
      the next frame.
2. **Physics steps** of the frame (player car, opponents, police, knocked traffic and other movers;
   contacts that knock traffic cars, `traffic-reactions.md`, "Who collides with what").
3. After the steps: each traffic body that has come to rest (or fallen below y = −100) ends its
   physics (`traffic-reactions.md`, "End of physics").

**One traffic car update:**

1. The state's behaviour (each state has a "start" for its first frame): driving
   (`traffic-driving.md`, "One driving update"), knocked, rejoining, avoiding
   (`traffic-reactions.md`); out does nothing.
2. Wheel angle (`traffic-visuals.md`, "Wheels"); sound position (later).
3. Obstacle registration (`traffic-reactions.md`, "Police, sirens and other cars").
4. If the car's speed is not 0: its room = Find(position + Y row, its room)
   (`traffic-visuals.md`, "Room").

## What traffic uses from other specs

| Need | Where |
|---|---|
| lanes, lane distances, section frames, lane pieces, turn pieces, piece evaluation, distance along a lane, point at a route distance, next-road choice, turn class | `road-network.md` |
| per-room traffic road lists, road flags (0x04 freeway, 0x08 level), side flags, lane counts, speed limits after load, intersections and their road order | `../formats/bai.md` |
| car mix, `[AmbientLaneChanges]`, exceptions | `../formats/aimap.md` |
| may a car enter, light states, stop-sign queue, light changes waking cars | `traffic-control.md` |
| component (road / intersection) at a position or in a room, on-road tests, section of a point | `road-network.md`, "Room lists" and "Other road queries" |
| AI-ahead (yield) marks | `driver.md`, "Yield marks" |
| room of a point | `../physics/city-collision.md`, "Finding the room of a point" |
| ground probe | `../physics/collision.md`, "Ground queries" |
| rigid body integration, box inertia | `../physics/vehicle-body.md` |
| contacts and impulse response between a mover and a traffic car's box | `../physics/collision.md` and the car-to-car collision spec (`traffic-reactions.md`, "What the car-to-car physics needs") |
| player car: position (model origin and body), radius, horn button, speed | `../physics/vehicle.md`, `../engine/controls.md` |

## Police and traffic

Police cars are not traffic cars: they come from the race file's police list and are driven by the
AI driver (`police.md`, `driver.md`). The two meet only through the yield marks, the obstacle
registrations (police steer around traffic), and collisions (a police car knocks a traffic car like
any other mover). Traffic ignores sirens and police states.

## Randomness

MM2 uses one game-wide generator for everything (`../physics/vehicle-wheels.md`, "Random
generator": x ← x × 214013 + 2531011 mod 2³², value ((x >> 16) & 0x7FFF) × 3.0517578e-05). It
restarts it with x = 1 at the start of every traffic reset (session start and race reset), before the
streets are filled; the pool is created earlier, after the city load (which also restarts it) and
after other set-up draws. Police and pedestrians draw from it between traffic's draws, and so do
car parts thrown off, glass panes, the night flicker of glowing props and force feedback (most
visual effects use a second state of the same generator and sounds their own;
`../physics/collision.md`, "Random numbers"), so MM2's own traffic cannot be reproduced number for
number.

**Engine:** traffic has its **own** generator of the same kind (police and other systems have their
own too). It is restarted with x = 1 (a) just before the pool is created and (b) at every traffic
reset, before the streets are filled. Only these draws use it, and each must be made even when its
value is not used:

| When | Draws, in order |
|---|---|
| pool creation, car 0 … *N* − 1 | lateral offset, reaction delay (`traffic-driving.md`, "Per-car constants") |
| then car 0 … *N* − 1 | type (if the mix has entries), paint twice (the second kept), acceleration-and-gap |
| filling roads, per placed car in placement order | exception roads: the position draw; then the next-road choice's draws |
| next-road choice (placing, arriving, after a lane change, every frame an approach is blocked by an accident or a full exit lane, starting past the lane end, rejoining on a road) | `road-network.md`, "Choosing the next road" (0, 1, 2 or 3 draws) |
| every frame of avoiding a player | one draw (`traffic-reactions.md`, "Each frame") |
| a breakable part knocked off (never with shipped data) | 8 draws |

Within a frame, draws happen in the car update order of "Frame order". Horn chances use the sound
system's own numbers, not this generator. The indicator blink phase and the street-lamp glow flicker
(`../formats/night.md`) do not use it.

## Check runs with traffic

Traffic adds deterministic city runs to `../engine/drive-check.md`, under the same rules (fixed
*F* = float32(1/60), the player car driven by a control timeline). A traffic run fixes: city,
player start and controls, traffic density *D*, pool size *N* = 300, time of day and weather, the
race file (Cruise: `roam`) and the traffic generator restarted as above (pool creation, then the
reset at frame 0). It writes, per frame, one row per traffic car on the streets in update order:
frame, pool index, state, road, side, lane, list lane, rail kind, *k*, *x*, *u*, *v*, *a*, *v*ₜ,
go, react, indicator, position (x, y, z) and the matrix's Z row, plus one row per filling or
emptying event (frame, road, entering or leaving, cars placed). Two useful runs: the player parked
at a fixed start for 60 s (filling, lights, queues, turns, lane changes, the populated-area edge),
and a city drive-check timeline (room changes, avoiding, knocks).

## Engine choices collected

- Traffic generator separate from every other system (above).
- Speeding counter reset whenever the pool is created (`traffic-driving.md`).
- A car without a next road stops at the end of its last piece and asks again each frame
  (`traffic-driving.md`, "Choosing the next road").
- The populated-area edge tests the side the car would turn onto (`traffic-driving.md`,
  "Arriving on the next road").
- A pool car whose type draw matches no mix entry is never placed (`traffic-population.md`).
- Missing `.aiVehicleData` keys are 0 (`../formats/aivehicledata.md`).
- Blink clock starts at 0 with the session; per-car blink phase any fixed 0 … 15
  (`traffic-visuals.md`).
- MM2 quirks kept on purpose (each marked where it is described): the react counter's 16-bit wrap,
  the wreck mark that lasts the session, the lane-0 length in the turn piece's start, the
  avoidance manoeuvre that turns toward the player, the lane-change last piece adding the back
  distance, tail lights drawn twice at night.

## Checked against

- The per-page sections. Totals over the shipped data: 20 traffic types used by the cities' mixes,
  all with complete data files; SF 1,087 and London 1,187 rooms carry traffic road lists.
- `tools/ai/traffic.py` (dirty side): `data`, `aimap`, `population`, `sim`; outputs in
  `work/check/traffic_*`.
