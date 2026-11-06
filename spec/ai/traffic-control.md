# Traffic control: traffic lights, stop signs, entering intersections

How MM2 controls traffic at intersections: which approaches have lights or stop signs, where the
light poles stand and what they show, the light cycle, the rule a car applies before it drives
into an intersection, and the stop-sign queue. The network itself is `spec/formats/bai.md` and
`road-network.md`; how cars slow down and stop is the traffic spec.

## Approaches and their control

An **approach** is a road end where traffic arrives at an intersection: end 0 of a road for its
side B traffic, end 1 for its side A traffic (`bai.md`). Each end record carries a **control**:

| Control | Meaning | London | SF |
|---|---|---:|---:|
| 0 | stop sign | 45 | 36 |
| 1 | traffic light | 519 | 533 |
| 2 | yield (never allowed through, see below) | 0 | 0 |
| 3 | none (free) | 516 + 140 shortcut ends | 189 + 98 |

Every intersection with a stop approach has exactly one, and almost all of them (London 45 of
45, SF 34 of 36) are signalised junctions where a side street or alley joins: the side street
gets the stop sign, the main approaches the lights.

## Light sets

An intersection gets a **light set** if at least one approach to it has control 1 (London 218
sets, SF 182). The set has one **light** per such approach, numbered in the order of the
intersection's road list *after* the shortcut merge (`bai.md`, load step 4); each such approach
remembers its light number (the end record's light slot).

### Setting up a light set

Done once at load, in intersection order, after the shortcut merge (`bai.md`, step 5):

1. **Kind.** Let *sources* = the number of roads in the list whose end 0 names this intersection
   or whose side A has lanes (a road ending here counts even without side B lanes, and so does a
   shortcut ending here), and *blocked exits* = the number of roads that leave the intersection on a
   side with side-flag bit 0 **set** (a road whose end 1 is here: its side B; otherwise its side A,
   only if that side has lanes). Kind 2 ("paired") if the intersection has exactly 4 roads,
   4 sources and 4 blocked exits; else kind 1 ("all approaches") if the number of lights equals
   *sources*; else kind 0. Kind 2 never occurs in the shipped cities (it would need four one-way
   exits that ambient traffic may not use); London has 128 sets of kind 1 and 90 of kind 0, SF
   116 and 66.
2. **Light poles.** Every light gets a pole, whatever its sign position (MM2 has no test for
   (0, 0, 0); no shipped light has that position, only approaches without control do). For each
   light, in light order: model = the city aimap's first
   `[Traffic Lights]` name if the arriving side (side B for end 0, side A for end 1) has at most one
   lane, else the second name (`aimap.md`; SF: `sp_traflitsingle_f` / `sp_traflitdual_f`, London
   `sp_traflitsingle_ped_l` for both). Each pole is a breakable street prop (an untouched prop that
   keeps only its turn about Y, exactly like the compact-placement `sp_stop_f` props of
   `<c>_ai.inst`; `../physics/collision-objects.md`, "Props"):
   - **turn**: X = (dx, 0, dz)·(1/√(dx² + dz²)) with (dx, dz) = facing point − *p* (*p* = the
     approach's sign position; X = 0 if that length is 0), Y = (0, 1, 0), Z = (−X.z, 0, X.x);
   - **base point** *p*: the model's origin for the glow meshes ("What the lights show");
   - **prop position** (its centre of gravity, where its body meshes are drawn and its bound sits):
     g = *p* + R·CG, computed as g.x = *p*.x + ((Z.x·CG.z + X.x·CG.x) + 0),
     g.y = *p*.y + ((0·CG.z + 0·CG.x) + CG.y), g.z = *p*.z + ((Z.z·CG.z + X.z·CG.x) + 0), with `CG`
     from `tune/banger/<model>.dgBangerData`. The shipped poles have CG.x = CG.z = 0, so g = *p* +
     (0, CG.y, 0): 1.67468 m up for `sp_traflitsingle_ped_l`, 3.892483 for `sp_traflitsingle_f`,
     3.881819 for `sp_traflitdual_f`;
   - **room**: Find(g, 0) (`city-collision.md`; the global search). MM2 puts the pole at the
     **front** of that room's object list (it inserts at the head; the poles are created after the
     city's `.inst` and `_ai.inst` objects, in intersection order then light order, so in a room
     with several poles the last created comes first);
   - **collision**: as every prop (`../physics/collision-objects.md`): bound from its
     `.dgBangerData`. All three models have `CollisionPrim 2`, i.e. a vertical capsule centred on g
     with radius `YRadius` and length `Size.y` (`sp_traflitsingle_ped_l`: 0.072037 and 3.34936;
     `sp_traflitsingle_f`: 0.264185 and 7.784967; `sp_traflitdual_f`: 0.275063 and 7.763638), so
     only the post is solid, not the arm; `CollisionType` 16 (wheels do not drive on it); mass,
     elasticity, friction and `ImpulseLimit2` from the same file. It is knocked over like any
     breakable prop (props spec).
   Its light state starts at 2. Poles belong to MM2's city in every mode, so they are part of the
   city check runs.
3. **Kind 2 only:** for *i* = 0 … lights − 1, the approach at this intersection of the road in list
   slot *i* gets turn rule (3 | the previous approaches' rules | its own) — 3 on the shipped
   data's zeros. Rule 3 sends ambient traffic straight across (`road-network.md`, rule 1).

### Light states

| State | Meaning | Meshes shown (glow pass) |
|---|---|---|
| 1 | red | `REDGLOW` + `NOWALK` |
| 2 | yellow | `YELLOWGLOW` + `NOWALK` |
| 3 | green | `GREENGLOW` + `NOWALK` |
| 4 | red, pedestrians walk | `REDGLOW` + `WALK` |
| 5 | red, walk ending | `REDGLOW` + `NOWALK` |

Only state 3 lets cars in (below).

### The cycle

Each set has a current light *c*, a phase (normal or walk), a timer *t* and the cycle time
C = 6.0 s (fixed in MM2).

**Reset** (when the session starts and at every AI reset): *c* = 0, phase normal, *t* = 0; light 0
→ 3, every other light → 1; kind 2: light 2 → 3 as well. (Light 0 does not wake its cars, see
below.)

**Each frame** (with dt the frame time in seconds, *F* of `traffic.md`; all sets are updated once
per frame after all AI vehicles, cable cars and trains, in intersection order):

1. *t* = *t* + dt.
2. Normal phase:
   - if *t* ≤ C: if *t* > C − 3, light *c* → 2 (yellow) (kind 2: also light (*c* + 2) mod 4).
   - if *t* > C: light *c* → 1 (kind 2: also (*c* + 2) mod 4); *c* = *c* + 1; *t* = 0. If *c* equals
     the light count: kind 1 or 2 → walk phase, every light → 4; kind 0 → *c* = 0. If still in the
     normal phase: light *c* → 3 (kind 2: also (*c* + 2) mod 4), and the cars on the road in **list
     slot** *c* of the intersection are woken (kind 2: also slot (*c* + 2) mod 4, on the side chosen
     for slot *c*'s road). Note the slot is not necessarily the road of light *c* — the shipped data
     has 227 London and 200 SF lights for which they differ.

   **Waking a road** (also used by the always-green rule below): the side is the road's arriving
   side at this intersection (side B if the intersection is not the road's end 1, else side A).
   For each lane of that side, in lane order, start at the front car of the lane's car list (the car
   furthest along) and walk back through the list; every car met whose speed is below 0.01 m/s gets
   its reaction counter set to 0 (traffic spec), the others are left alone but the walk goes on.
   MM2 steps from a car to the one behind it using the link for that car's **target** lane, so a car
   in the middle of a lane change leads the walk into its target lane's list.
3. Walk phase:
   - if *t* > C: reset (above).
   - else if *t* > C − 3: every light → 5.

So in the normal phase each approach gets 3 s of green and 3 s of yellow in turn, going round
the intersection counter-clockwise (list order); kind 1 sets then hold all red for 6 s while
pedestrians cross (3 s `WALK`, 3 s `NOWALK`). A kind 1 set with *k* lights repeats every 6*k* + 6 s,
a kind 0 set every 6*k* s. All sets start together at a reset; there is no other coordination
between intersections.

Light sizes, London: 30 sets with 1 light, 102 with 2, 60 with 3, 25 with 4, 1 with 5. SF: 9, 43,
83, 46, 1.

### What the lights show

The glows are **meshes of the pole's own model**, not billboards: each of the ten glow meshes
(`REDGLOWDAY_H`, …, `NOWALK_NIGHT_H`) is a set of flat textured quads (4 vertices each: one quad per
lamp head; `sp_traflitdual_f`'s two heads give two quads per mesh) at fixed positions in the model,
with textures `s_red_glow`, `s_yel_glow`, `s_grn_glow`, `s_grn_glow_walk_l`,
`s_red_glow_dontwalk_l` (their `_ni` versions at night, `night.md`).

**Where they are drawn.** The pole's body meshes (`H`, `M`, `L`, `VL`, breakable parts, shadow) are
modelled around the prop's centre of gravity and drawn with the prop matrix at g (rotation R,
position g). The glow meshes are modelled around the pole's **base**: MM2 draws them with the same
rotation R but position g − R·CG, i.e. at the base point *p*:
position = g − ((X·CG.x + Z·CG.z) + Y·CG.y) per component (X, Y, Z = the rows of the stored
turn-only matrix). Both line up: e.g.
`sp_traflitsingle_f` body vertices span y −3.89 … +3.89 around g (so 0 … 7.78 m above *p*, lamp
heads 6.0–7.8 m up), and its `GREENGLOWDAY_H` quad spans y 5.65 … 7.12 above *p*, over the lower
lamp; `sp_traflitsingle_ped_l` body −1.67 … +1.67 around g, green glow 1.65 … 3.13 above *p*.
Drawing a glow mesh with the prop matrix (at g) puts it CG.y too high (3.9 m above the SF lamps);
drawing the body at *p* sinks the pole by CG.y.

**When.** In the glow pass (`night.md`, "Glows": additive, depth test on, depth writes off, no
lighting, no fog), for every pole that has not been knocked down and whose room is within the room
limit: the two meshes of its state from the table above, the `…DAY` versions in the morning and at
noon and the `…NIGHT` versions (larger quads: about 4.5 m square for the SF lamps instead of about
1.5 × 1.5–2.5 m) in the evening and at night, the `_H` mesh of each (there are no other levels). If
either mesh is missing from the model, nothing is drawn for that pole (`sp_traflitsingle_l` and
`sp_traflitsingle_corner_l` lack `WALK`/`NOWALK`; the models the cities use have all ten). Lamp
glows (`NumGlows`) are drawn as for any prop (the shipped poles have none).

A knocked-down pole stops drawing its glows, but its light keeps cycling and traffic still obeys it.

## May a car enter the intersection?

This is the **signal and sign part only**: the answer the intersection gives. Ambient cars and
cable cars ask it when they reach the end of their lane; the traffic spec says when they ask, how
they brake, and adds the car-side checks (an accident on the next road, room on the exit lane,
crossing traffic, cars ahead) before a car actually goes. The answer depends on the approach the
car is on (its road and the end its side leaves by; the control is the end record's second u16,
`bai.md`):

1. If the road's **always green** flag is set: yes (and every road's cars are woken on the side with
   the same number as the car's side: side B lanes if the car is on side B). This is a debug switch;
   it is never set in play.
2. Else if the road's **held** flag is set: no. Opponents and police set it while they cross an
   intersection (their specs): holding an intersection sets the flag on every road whose approach
   to it has control 0 or 1, releasing clears it. AI resets clear it.
3. Else by control:
   - **0, stop sign**: only once the car is almost stopped — speed < 0.5 m/s **and** remaining
     distance to its stop point < 1.5 m (the distance the traffic spec passes in). Then the car
     joins the intersection's queue (once; below) and may go if it is in the intersection's go
     group. Otherwise no.
   - **1, traffic light**: yes if the approach's light is in state 3, else no (yellow means stop).
   - **2, yield**: no (MM2 prints an error); a yield approach would block its traffic forever.
   - **3, none**: yes (and the car and every car behind it in its lane list are woken as above).

### The stop-sign queue

Each intersection keeps a **waiting list** and a **go group**, both ordered, both empty after a
reset.

- **Joining**: a car is appended to the waiting list the first time it asks at a stop approach
  (it remembers that it has joined until it has crossed the intersection).
- **Asking**: if the go group is empty, the first waiting car moves to the go group; then, if that car
  is an ambient car (not a cable car), every other waiting car whose current road is the same as
  its own moves to the go group too, keeping their order. A car
  may go if it is in the go group.
- **Leaving**: when a car finishes its path through the intersection from a stop approach, it is
  removed from the go group (MM2 prints an error if it was not there). A car removed from the world
  (despawned) is taken out of both lists.

## Stop signs

The stop-sign poles are the breakable props of `city/<c>_ai.inst` (`inst.md`; all `sp_stop_f`).
Every stop approach's sign position is, in x and z, exactly the position of one of them (London 45
of 67 signs, SF 36 of 40; the sign position's y is 0.15 m lower); the other signs (22 and 4) belong to no approach. The AI does not look at the
props: a knocked-down sign still stops traffic, and the extra signs stop nobody.

## The player and the lights

Nothing reads a light state except the "may enter" rule above (ambient cars and cable cars).
The player is never checked against lights or stop signs, and running a red light is not an
offence (police offences are the police spec's).

## Trains (notes for later)

Now specified in full: `cable-cars.md`.

- **Cable cars** (SF): roads with a cable-car line on a side (21 roads, both sides). At load, if
  the session enables cable cars, every intersection at which exactly one road leaves on a side
  with a cable-car line starts one cable car (model `va_cablecar_f`) on that road and side; it runs
  along the cable-car lines from road to road and obeys the "may enter" rule like an ambient car.
- **Subway trains** (London): side B of 28 roads has a subway line (the Underground). Trains run
  only if the session enables them **and** the city aimap has a `[Subway]` entry (car model, cars per
  train); an intersection where exactly one road leaves on a subway line starts a train there and
  one where exactly one road arrives on one ends it. The shipped `london.aimap` has the entry
  commented out (`#va_ug_l 3`), so retail London has no network trains. (It does have path-set
  trains, `race/london/london_train.pathset`: `../audio/world-sounds.md`, "Underground trains".)

## Checked against

`tools/ai/bai.py` (dirty side) builds the light sets by these rules:

- London: 218 sets (kind 1: 128, kind 0: 90, kind 2: 0), 519 lights, all `sp_traflitsingle_ped_l`.
  SF: 182 sets (116, 66, 0), 533 lights: 356 `sp_traflitdual_f`, 177 `sp_traflitsingle_f`.
  (sources, lights, kind) for London: (1,1,1) 16, (2,2,1) 52, (3,3,1) 41, (4,4,1) 19, (2,1,0) 14,
  (3,2,0) 44, (4,3,0) 17, and a few larger.
- Every light's facing point is exactly 1.0 m from its sign position. Sign positions lie within
  0.09 m of their intersection centre's height (sign y − centre y from −0.086 to +0.075 m in London,
  −0.086 to −0.050 in SF).
- Every stop approach's sign position equals the x and z of a `sp_stop_f` placement in
  `<c>_ai.inst` exactly; its y is 0.15 m lower than the placement's (London −0.15 against 0.00,
  SF 14.85 against 15.00).
- No turn rule 3 arises (no kind 2 sets).
- The models `sp_traflitsingle_f`, `sp_traflitdual_f` and `sp_traflitsingle_ped_l` each contain
  `REDGLOWDAY`, `YELLOWGLOWDAY`, `GREENGLOWDAY`, `WALK_DAY`, `NOWALK_DAY` and the five `…NIGHT`
  meshes (`_H` only), plus `BREAK01`/`BREAK02` parts.
- Maps with light (green: kind 1, yellow: kind 0) and stop-sign (red) positions:
  `work/check/bai_london.png`, `bai_sf.png`, zoomed `bai_<c>_zoom0/1.png`.
