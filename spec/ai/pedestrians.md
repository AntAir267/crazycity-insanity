# Pedestrians

How Midtown Madness 2's pedestrians work: how many exist, where they appear and vanish, how they
walk along the sidewalks and cross at traffic lights, and how they react to the player. They are
scripted figures that walk on the road network's sidewalk lines; they never take part in the
physics and nothing in the game reacts to them.

| Page | Contents |
|---|---|
| `pedestrians.md` (this page) | terms, settings, the pool, filling and emptying roads, frame order, randomness, what pedestrians do not do, check runs |
| `pedestrian-walking.md` | the per-frame update, the sidewalk path, walking, obstacles, road ends, crossing at lights |
| `pedestrian-reactions.md` | noticing the player's car, standing wary or running, diving away, backing against a wall, recovering |
| `../formats/ped-anim.md` | model, skeleton, animation and sequence files; posing, playback and drawing |

Related pages: `road-network.md` (terms, curve evaluation), `../formats/bai.md` (sidewalk lines,
side flags, pedestrian activation table), `../formats/aimap.md` (`[Ped Pool]`, model pairs),
`traffic-control.md` (light sets and the walk phase), `traffic-population.md` (the same room
mechanism for traffic).

## The short version

- A session has a fixed pool of pedestrians (Cruise default 25: pool size 100 × density 0.25).
- Each room has a list of **pedestrian roads**. When the player's car enters a room, the roads
  that join the list get pedestrians, round-robin over both sidewalks, from the free part of the
  pool; roads that drop out of the list are emptied.
- A pedestrian walks along the sidewalk line of one side of one road, at a small random offset
  across the sidewalk, steering toward a point 6 m ahead. At the road's end it turns the corner,
  and at intersections whose lights have a walk phase it may instead cross a street: it walks to
  the kerb, waits for `WALK`, crosses, and hurries when the light shows `NOWALK`.
- It steps around street props and around the player's car when that is slow.
- A player car coming at it makes it stare or run (about 0.75–2.3 s before impact) or dive
  sideways (under 0.75 s); near a building it runs to the wall and backs against it.
- **It is never hit.** Pedestrians are not collision objects: a car that reaches one drives
  through it. Traffic, police and opponents never see them.

## Terms

- Road, section, side (A = −1, B = +1), lane count *L* of a side, section axes X (toward side A),
  Y, Z (against the road direction), tangents T, centre points C, end 0 (at section *n* − 1) and
  end 1 (at section 0): `road-network.md`, "Terms", and `../formats/bai.md`.
- **Sidewalk line** of side *s*: line *L*ₛ of that side (its first sidewalk line), after the load
  procedure (`bai.md`; mirrored cities included). Its points W[0 … *n* − 1] lie at sections
  0 … *n* − 1 **in section order on both sides**, and its line distances Dw[0 … *n* − 1] grow with
  the section index (Dw[0] = 0). "Length of the sidewalk" = Dw[*n* − 1].
- **Kerb line** E[0 … *n* − 1] of side *s*: the first *n* points of the side's edge lines (`bai.md`,
  "Side block").
- A **pedestrian road side** is a side of a main road whose side flags do not have bit 1
  (`bai.md`). Every shipped main road has one sidewalk per side; shortcut roads have none and
  have bit 1 set. MM2 checks only the flag; *our choice* (mods): a side without a sidewalk line is
  never a pedestrian side (MM2 would read past its lines).
- Players, P_c (body position), R_p (model radius): `traffic-reactions.md`, "Terms". The player
  car's **body matrix** has rows X (right), Y, Z (back) and position P_c; **speed** is the car's
  speed of `../physics/vehicle.md` (step "Speed"); **steering** its steering input (−1 full left …
  +1 full right, `../engine/controls.md`); **gear** 0 means reverse; **InertiaBox** its tune
  vector (`../physics/vehicle-tune.md`).
- *F*: frame time (`traffic.md`). rnd(): the pedestrian random generator ("Randomness"); rnd15()
  its raw 15-bit value.
- float32 arithmetic as in `../physics/vehicle.md`, "Precision"; formulas are evaluated as written.

## Settings

| Setting | Source | Effect |
|---|---|---|
| Pool size *P* | city aimap `[Ped Pool]` (default 100; the shipped files have none, London's is commented out); in MM2 a `pedpool` command-line option overrides it (*engine*: no such option; check runs set the pool size) | number of pedestrians that can exist |
| Density *ρ* | Cruise: menu slider "Pedestrian density" 0 … 1, default 0.25; races: the race's `Peds` column per skill level (`race/<c>/mm*data.csv`, milestone 4) | count = trunc(float(*P*) × *ρ*) |
| Pedestrians off | display option (MM2 switches it on automatically for slow machines and its software renderer) | count 0 |
| Circuit races | game mode | count 0 whatever the density |

The count is fixed for the session. Shipped race densities: 0, 0.1, 0.2, 0.3 or 0.4–0.5 (Blitz),
0 in every Circuit and Crash Course race.

## The pool

When the AI map is set up, after the ambient traffic pool and before the cable cars
(`traffic.md`), MM2 creates *count* pedestrians, numbered 0 … *count* − 1. The model list is the
race aimap's `[GoodWeatherPedName / BadWeatherPedName]` list if it has entries, otherwise the city
aimap's; the **bad-weather** column is used when the weather is rain (`../formats/night.md`),
otherwise the good-weather column. For pedestrian *i* in order:

1. Model = list[trunc(rnd() × number of pairs)] (one draw).
2. Load the model if it is the first use in this session (`../formats/ped-anim.md`).
3. Paint variant = trunc(rnd() × (variant count − 1)) (one draw; so the last variant is never
   used).
4. Gender: female if the model name contains `FEMALE`, `WOMAN`, `GIRL` or `Hooker` (ignoring
   case), else male (only the voice depends on it, "Sounds").
5. Animation state reset (`ped-anim.md`, "Playback": the shared accumulator too).

Shipped: two pairs (`pedmodel_man`/`pedmodel_manw`, `pedmodel_woman`/`pedmodel_womanw`) in both
cities, no race overrides.

MM2 has no safe behaviour for a model that does not load (it draws through an empty model) or for
an empty model list (it reads entry 0 of it). *Our choice* (mods): a pedestrian whose model does
not load is still created, with its draws, but is never placed; with no model list there are no
pedestrians.

The free pedestrians form a last-in-first-out **free list**. On every reset (session start and
race reset), after the generator restart: every road's pedestrian list is emptied, the list of
**active pedestrian roads** is emptied, and pedestrians 0 … *count* − 1 are pushed onto the free
list in that order (so the first taken is the last one); a pushed pedestrian is moved to room 0
(not drawn). Then, after the traffic fill of each player, the pedestrian fill runs for that player
as if it had entered its room from room 0 ("Rooms and pedestrian roads"); the room is the one
containing the player car's reset position (`traffic-population.md` uses the same).

## Rooms and pedestrian roads

The road network gives each room a **pedestrian road list** (`bai.md`, "Activation tables", the
second table). Each road keeps one "has pedestrians for player *i*" bit per player (separate from
the traffic bits) and is either on the active pedestrian road list or not; roads join that list
at the front.

Every frame, in the player loop of "Frame order": if the player's room changed (and the new room is
not 0), **adjust**(old room, new room, player) runs right after the traffic adjust. The room is the
traffic's (`traffic-population.md`): MM2 keeps **one** room memory per player for both adjusts;
room = Find(P_c, memory) with P_c the car's **body position** (the point every pedestrian test
uses, not the model origin), and in the player's bookkeeping the memory takes the room found,
every frame, 0 included (so after a frame in room 0 the next adjust runs from room 0 and empties
nothing).

### Adjust(old, new, player)

1. *Leaving* = roads in old's list not in new's (old's order); *entering* = roads in new's list
   not in old's (new's order). Room 0 has an empty list.
2. For each leaving road that is on the active list: clear this player's bit; if no bit remains,
   **empty** it: every pedestrian on its list, from the front, is taken off the road and pushed on
   the front of the free list (moved to room 0); the road leaves the active list.
3. For each entering road: set this player's bit; if no player's bit was set before, put the road
   at the front of the active list and append it to the **fill list** *E* (else skip it).
4. **Fill** (only if the free list is not empty and *E* is not empty): *i* = 0, *last* = 0. Repeat:
   - road = *E*[*i*]; if its side A is a pedestrian side: take the front of the free list and
     **place** it on (road, side −1); *last* = *i*;
   - if the free list is still not empty and its side B is a pedestrian side: place the next one on
     (road, side +1); *last* = *i*;
   - *i* = *i* + 1, back to 0 at the end of *E*;
   - stop when *i* = *last* or the free list is empty.

   So when two or more entering roads have pedestrian sides, every free pedestrian is placed,
   one per pedestrian side round-robin; when only one has, it gets one round (one or two
   pedestrians); when none has, nothing is placed.

Nothing else moves pedestrians between the free list and the streets: a pedestrian stays on the
streets until its road is emptied or the next reset. In practice the whole pool is out on the
roads around the player, and each room change re-places only the pedestrians freed by roads it
left.

### Placing a pedestrian on (road *r*, side *s*)

*L* = lane count of side *s*; W, Dw its sidewalk line; *n* the section count.

1. Current road = previous road = *r*; side = previous side = *s*; wall flag = 0.
2. Distance *x* = rnd() × Dw[*n* − 1] (one draw).
3. Lateral offset *o* = sin(rnd() × 6.2831) × ((b[2*L* + 1] − b[2*L*]) × 0.5 − 0.5), with b the band
   edges of side *s* (`bai.md`; side A's own for side A) (one draw; float32 constant 6.2831001).
4. Matrix = identity; segment *k* = the segment of *x* on the sidewalk line (`pedestrian-walking.md`,
   "Sidewalk geometry").
5. Direction: if rnd() < 0.5 then *d* = +1 else *d* = −1 (one draw); previous direction = *d*.
6. Heading *h* = H(*x*, *L*, *d*) (`pedestrian-walking.md`, "Sidewalk heading").
7. Position = W[*k* − 1] + (W[*k*] − W[*k* − 1]) × ((*x* − Dw[*k* − 1]) / (Dw[*k*] − Dw[*k* − 1]))
   per component (all three).
8. Build the segment's curve with offset *o* (`pedestrian-walking.md`, "The curve"), then set the
   position's x and z to the curve at u = (*x* − Dw[*k* − 1]) × R (R = the curve's inverse length).
9. Ground: probe from (x, y + 5, z) to (x, y − 5, z) (`../physics/collision.md`, "Ground queries",
   mask "ground", nothing ignored; the probe's room hints are the road's first room); on a hit,
   y = the hit point's y.
10. Mode = 0 (walking), step = 0, "seen" mode and step = −1 (so the first update runs the step's
    start).
11. Put the pedestrian at the **front** of road *r*'s pedestrian list.
12. Matrix from *h* (`pedestrian-walking.md`, "Matrix").
13. Reset its animation state (and the shared accumulator), start `WALK`, then frame =
    trunc(rnd() × `WALK`'s frame count) (one draw).
14. Move it to the road's first room.
15. Target point for *x* + *d* × 6 (`pedestrian-walking.md`, "Target point").
16. Nearest-player distance = 100000, speed = 0, dive start = 0; the voice is reset.

So a placed pedestrian appears anywhere along its sidewalk, already walking, at a random point of
its walk cycle. Placement looks at nothing else: not the camera, the player or other pedestrians.

## Frame order

Inside the AI update of each frame (`traffic.md`, "Frame order"):

1. Players (list order): room tracking, the traffic adjust, the **pedestrian adjust**, the
   player's AI bookkeeping (direction of its velocity, 1/speed: below).
2. Traffic cars.
3. **Pedestrians**: for each road on the active pedestrian road list, from the front (most
   recently activated first): *p* = the road's first pedestrian; while *p* exists: update *p*
   (`pedestrian-walking.md`, "One update"), then *p* = the pedestrian after *p* **in the list *p*
   is in now**; stop when *p* is this road's first pedestrian again.

   A pedestrian that moved to another road during its update was put at the front of that road's
   list, so the walk continues into the other road's list and updates its pedestrians too
   (possibly a second time this frame). MM2 keeps this; it can in principle loop forever between
   two roads (fan patches cap it). *Our choice*: reproduce the walk, but update no pedestrian more than
   once in a frame (skip any already updated and continue along the list).
4. Opponents, police, cable cars, trains, light sets.

The player bookkeeping used by pedestrians, per player and refreshed in step 1: **v̂** = the body's
linear velocity direction (unit length, (0, 0, 0) if zero), and **inverse speed** = 1 / speed if
speed > 0.001, else 0.

Pedestrians run once per frame with the frame time *F*; they have no part in the physics steps.

## What pedestrians do not do

- **They are never hit.** A pedestrian has no collision bound and is not collidable; the player's
  car, traffic and props pass through it. MM2 contains an unused ragdoll system for pedestrians
  (a mod enables it); retail MM2 never creates it. The only defence is the reactions of
  `pedestrian-reactions.md`.
- **Nobody stops for them.** Pedestrians register nowhere: traffic only sees lane lists and
  intersection control (`traffic-reactions.md`), police and opponents only vehicles, props and
  players. Light sets do not wait for them; the walk phase happens on its own clock
  (`traffic-control.md`).
- **They do not avoid each other.** MM2 has code for pedestrians stepping around each other, but it
  is unreachable; they walk through one another.
- They feed nothing back: no other system reads a pedestrian's state (in MM2 they only share the
  random generator).

## Randomness

MM2 draws pedestrian numbers from the game-wide generator (`traffic.md`, "Randomness"), between
the other systems' draws. As for traffic, the **engine** gives pedestrians their **own** generator
of the same kind, restarted with x = 1 (a) just before the pool is created and (b) at every reset,
before the first pedestrian adjust. Draws, each made even when its value is not used:

| When | Draws |
|---|---|
| pool creation, per pedestrian in order | model, paint variant |
| placing a pedestrian | distance, offset, direction, start frame |
| leaving the back-to-the-wall pose (`pedestrian-reactions.md`) | offset |
| reaching a road end at an intersection with a light set of kind ≠ 0 | one raw 15-bit draw (`pedestrian-walking.md`, "Choosing where to go") |
| starting to wait at a kerb | one (`STAND` or `STAND2`) |
| becoming wary while walking with no wall behind | one (run or stare) |

Within a frame, draws happen in the update order of "Frame order".

## Sounds (notes for the sound milestone)

Each pedestrian has a 3D sound source at its position, audible from 0 to 40 m, with a voice set
`default_mpedvoice…` (male) or `default_fpedvoice…` (female) from `aud/creaturedata`. It plays an
"avoidance" voice when it starts a dive, when it starts running to a wall while dodging, and when it
leaves the back-to-the-wall pose. Details belong to the sound spec.

## Check runs

A pedestrian check run fixes the city traffic run's settings (`traffic.md`, "Check runs with
traffic") plus pool size and density, and writes per frame one row per pedestrian on the streets in
update order: frame, index, road, side, direction, previous road, side and direction, segment *k*,
distance, offset, heading, position, mode, step, wall flag, crossing choice, current and next
sequence, animation frame. Pedestrians do not affect any other output of a run. Useful runs: the
player parked at a signalised corner for 60 s (walking, corners, crossings in the walk phase), and
the player driving slowly and then fast along a sidewalk-lined street (wary, running, dives, walls).

## Engine choices collected

- Pedestrian generator separate from every other system (above).
- No pedestrian is updated twice in one frame (above).
- A pedestrian whose heading becomes NaN in MM2 is "lost" (`pedestrian-walking.md`, "Stepping
  around"); it still advances the shared animation clock.
- Reads past MM2's arrays: the sidewalk heading with a line number the side lacks reads its last
  line; locate's W[−1] on a side without lanes uses segment 1 (`pedestrian-walking.md`).
- Mods: sides without a sidewalk line, models that do not load, empty model lists (above).
- Knocked props are skipped by the obstacle test while the engine makes them vanish.
- The viewer's density slider re-creates the pool and resets the pedestrians at once, like the
  traffic slider (MM2 fixes the count for the session).
- A wall probe that misses while running to a wall during a dive counts as not yet at the wall
  (`pedestrian-reactions.md`).
- An unknown next-sequence name links a sequence to itself (`../formats/ped-anim.md`).

## Checked against

`tools/ai/peds.py` (dirty side):

- Both city aimaps: pool 100 (default), pairs `pedmodel_man pedmodel_manw` and
  `pedmodel_woman pedmodel_womanw`; no race aimap has a pedestrian list.
- Pedestrian activation tables: SF 1,172 rooms, 1,066 with roads (mean 6.6, at most 20 roads);
  London 1,342 rooms, 1,115 with roads (mean 7.0, at most 22). Every SF list and 1,024 of London's
  are subsets of the room's traffic list.
- Main-road sides: every one has one sidewalk; pedestrians allowed on 690 SF and 868 London sides
  (68 and 212 have bit 1), 75.5 km and 77.9 km of walkable sidewalk.
- Light sets with a walk phase (kind 1): SF 116 of 182, London 128 of 218 (`traffic-control.md`).
- Race densities (the `Peds` field of each half of `mm<mode>data.csv`, field 7 Amateur / field 17
  Professional, `../game/races.md`): SF Checkpoint 0–0.3 (both halves), SF Blitz 0.1–0.4 Amateur
  and 0.1–0.5 Professional; London Checkpoint 0 except one row at 0.1 in each half, London Blitz
  0.1–0.2; every Circuit and Crash Course 0.
- Fresh-start fill with the Cruise pool of 25 in every room: SF places all 25 in 1,054 of 1,171
  rooms, 2 (one road) in 12, none in 105; London all 25 in 1,113 of 1,341, 2 in 2, none in 226.
