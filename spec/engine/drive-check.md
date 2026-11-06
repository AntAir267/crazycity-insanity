# Driving check runs

Two independent implementations of the vehicle spec are compared by running the same scripted
drives and comparing their output, step by step. The engine provides these runs as a command-line
tool (native build, no graphics), and the spec writers' reference produces the same files.

## Run file

Plain text, one setting or segment per line, `#` starts a comment.

```
car vpbug            # car base name (tune/<car>.info BaseName)
world flat           # "flat" or a city name, e.g. "london"
start 0 0 0 0        # x y z heading_degrees
paint 0
# segments: duration_seconds then control values; unspecified controls are 0
seg 1.0
seg 20.0 throttle=1
seg 5.0 brake=1
seg 3.0 throttle=0.5 steer=-1 handbrake=1
```

- Heading: the car's forward direction is −Z at heading 0; positive heading turns it to the left
  (counter-clockwise seen from above), i.e. heading 90 faces −X.
- Controls are applied to the car directly as its control inputs, after any keyboard or gamepad
  smoothing: `throttle` 0..1, `brake` 0..1, `steer` −1 (full left) .. +1 (full right),
  `handbrake` 0 or 1, `gear` (optional, manual gearbox: −1 reverse, 0 neutral, 1..n).
  Without `gear` the automatic gearbox and automatic reverse are active as in MM2.
  `throttle` and `brake` are the player's pedals (the keys): `controls.md` steps 2–6 still apply to
  them, i.e. the automatic-reverse pedal swap (so `brake=1` at rest backs the car up), the
  quantisation to 1/127 and 1/255 steps, and the parking-brake rule. The control step runs once per
  game frame, before that frame's physics.
- `world flat` is an endless horizontal plane at y = 0 made of the default road surface, with no
  walls. The vehicle spec must say which surface properties that means.
- The car is placed with its wheels resting on the ground at its natural ride height, at rest.

- `frame F` (optional, before the segments) sets the check-run frame length F in seconds (default
  0.016666668, i.e. 60 fps); `frame F1 F2 …` cycles through several frame lengths, frame k using
  F[(k mod count)], to imitate an uneven browser frame rate. F is rounded to float32. The physics
  splits each frame into steps exactly as `physics/vehicle.md` describes for that F, and the control
  and camera steps run once per frame as usual. Only in a run that has a `frame` line, a segment
  runs until its summed frame time reaches d (frames are counted while the running sum, in float32,
  is below d − 0.5·F_k); runs without a `frame` line keep the round(d / F) rule below unchanged. In a run with a `frame`
  line the CSV's `t` column is the running sum of the physics step times, accumulated in double
  precision. A `frame` line is not supported in runs with extra vehicles yet.
- A segment of d seconds runs round(d / F) frames, where F = 0.016666668 is the check-run frame
  length (so 1 s is 60 frames). A segment's control values, including `gear`, are applied on every
  frame of that segment.

## Output

CSV with a header row, one row per physics step (not per rendered frame), starting with the state
before the first step:

```
t,x,y,z,vx,vy,vz,fx,fy,fz,ux,uy,uz,speed,rpm,gear,w0,w1,w2,w3,c0,c1,c2,c3
```

| Column | Meaning |
|---|---|
| t | simulated time in seconds |
| x y z | car origin position, metres |
| vx vy vz | car velocity, m/s |
| fx fy fz | car forward unit vector (model −Z axis in world space) |
| ux uy uz | car up unit vector |
| speed | forward speed, m/s (velocity · forward) |
| rpm | engine RPM |
| gear | current gear (−1 reverse, 0 neutral, 1..n) |
| w0..w3 | wheel spin rate, radians per second, in the car's wheel order |
| c0..c3 | suspension compression of each wheel, metres (0 = fully extended) |

Floats with 6 significant digits.

## Standard runs

Kept in `checks/` (written by the builder from this list):

| File | Car | Script |
|---|---|---|
| `accel_<car>.run` | each of the 20 built-in cars | flat; 1 s idle; 25 s full throttle |
| `brake_vpbug.run` | vpbug | flat; 6 s full throttle (reaches about 23.6 m/s), then 6 s full brake |
| `corner_vpmustang99.run` | vpmustang99 | flat; 4 s full throttle; 6 s throttle 0.6 + steer −1 |
| `slalom_vpauditt.run` | vpauditt | flat; 3 s throttle; then 8 × (1 s steer +1, 1 s steer −1) at throttle 0.7 |
| `handbrake_vpcab.run` | vpcab | flat; 4 s throttle; 2 s steer +1 handbrake 1; 3 s nothing |
| `reverse_vpbus.run` | vpbus | flat; 3 s brake (auto reverse); 3 s nothing |
| `manual_vp4x4.run` | vp4x4 | flat; manual gear 1 for 3 s throttle, gear 2 for 3 s, gear 3 for 3 s |

City runs start at the repeatable free-roam intersections from `controls.md` (car placed at the
intersection position + 2 m up, as MM2 places it, then dropped) and drive into whatever is there,
so they cover curbs, walls, slopes, medians and props. The city contains the PSDL, the placed objects
of `../physics/collision-objects.md` (`.inst`, `_ai.inst`, xref props) and the traffic-light poles
of `../ai/traffic-control.md` ("Setting up a light set"); no traffic, no police, and for now
neither the street props (`../formats/street-props.md`) nor the `props.pathset` props
(`../formats/pathset.md`) that MM2's Cruise also has ("Street props in city runs" below):

| File | Car | World | Start (x y z heading) | Script |
|---|---|---|---|---|
| `london_n_vpbug.run` | vpbug | london | −230.0 1.9 333.5 0 | 1 s idle; 12 s full throttle |
| `london_e_vpbug.run` | vpbug | london | −230.0 1.9 333.5 270 | 1 s idle; 12 s full throttle |
| `london_s_vpcab.run` | vpcab | london | −230.0 1.9 333.5 180 | 1 s idle; 12 s full throttle |
| `london_w_vpbus.run` | vpbus | london | −230.0 1.9 333.5 90 | 1 s idle; 12 s full throttle |
| `london_turn_vpmustang99.run` | vpmustang99 | london | −230.0 1.9 333.5 0 | 1 s idle; 3 s throttle; 4 s throttle 0.6 + steer +1; 5 s throttle |
| `sf_n_vpauditt.run` | vpauditt | sf | −995.5 44.5 1291.7 0 | 1 s idle; 12 s full throttle |
| `sf_s_vp4x4.run` | vp4x4 | sf | −995.5 44.5 1291.7 180 | 1 s idle; 12 s full throttle |
| `sf_e_vpsemi.run` | vpsemi | sf | −995.5 44.5 1291.7 270 | 1 s idle; 12 s full throttle |

For a city run, `start` is the point q of MM2's own placement rule (`physics/vehicle.md`, "Placing
the car in a city"): the car is level and at rest with its body position at q + CoG, with the
heading's cosine and sine computed in double precision and rounded to float as for flat runs. It
then falls onto the ground under the normal physics (unlike flat runs, which start resting at ride
height). Every check run (flat or city) restarts any random generator the physics uses before its
first frame, as the vehicle and collision specs describe.

**Damage, thrown parts and loose bodies.** Every car in a check run keeps its damage
(`../physics/damage.md`; the game's no-damage setting is off), so hard hits throw parts off, and
props that break move as loose bodies (`../physics/loose-bodies.md`). In a city run all of it
happens as in the game: thrown parts and broken props are movers after the player's car, the
player's rooms of `loose-bodies.md` 5.1 are its room and that room's neighbours, a reset brings
everything back. The launch of a thrown part and the wreck throw's choices draw from the run's one
generator (the one the bumps use), so they shift the bump numbers that follow. The flat world has
no rooms: a part thrown there is made in room 0, takes its eight draws, and never moves or
collides (the same as MM2 does for a car outside every room). The CSV has no columns for loose
bodies; the trace has records for them (`drive-trace.md`).

### Street props in city runs

The street props and the `props.pathset` props **remain excluded** from the standard city runs for
now: the reference has not switched them on, and adding them changes five of the eight city runs
(`london_s_vpcab`, `london_w_vpbus`, `sf_n_vpauditt`, `sf_s_vp4x4`, `sf_e_vpsemi` run into street
props or path-set props; `london_w_vpbus` from its first step: the bus's roof starts 0.34 m inside
the bound of the street tree `sp_tree1_s` of road 80, whose canopy overhangs the start). Drive mode
should have them, as MM2's Cruise does; only the check-run tool leaves them out until then. When
they are switched on, a city run includes them as follows (nothing else changes):

- Every street prop and every `props.pathset` prop of the city is created as those pages say
  (decals are not collidable and play no part; there is no race path set in Cruise).
- Room lists at the start of the run are the post-reset order of `../formats/street-props.md`,
  "Creation order": in each room the poles, then the path-set props, then the `_ai.inst` and `.inst`
  props, then the street props (each newest first), then the static objects.
- Trace object ids (`drive-trace.md`, "Exact meanings" 5) keep their current numbers; the new
  objects are counted **after the poles**: first the street props in creation order, then the
  `props.pathset` props in creation order, each only if it exists (has its `.dgBangerData`).

MM2's Cruise also has the gizmos of `race/<c>/<c>_<gizmo>.pathset` (parked cars, ferries, boats,
the moving bridges); they are `../game/level-objects.md` and are not part of the check runs.

## Extra vehicles (car-to-car runs)

A run may add more vehicles. Runs without them are unchanged in every respect (file, output,
trace).

```
car vpbug
world flat
start 0 0 0 0
police vpcop 0 0 -80 180            # vehicle 2: a police car (full car simulation, box bound)
traffic va_sedans_s 3 0 -40 0 speed=10   # vehicle 3: a traffic car driving straight at 10 m/s
seg 1.0
seg 9.0 throttle=1
seg2 1.0                             # vehicle 2's own control timeline
seg2 9.0 throttle=1
```

- Vehicles are numbered in file order: the `car` line is vehicle 1 (the player's car, as before),
  then every `police`, `ai` or `traffic` line is the next number (2, 3, …).
- `police <car> x y z heading` and `ai <car> x y z heading`: a computer car of base name `<car>`,
  simulated as `../physics/car-collision.md` section 5 says for a police car or an opponent (the
  only difference is the unstuck helper's `TimeThresh`, 0.75 or 0.5). It is placed with the same
  rule as vehicle 1 (`world flat`: resting at ride height; a city: MM2's placement rule). Its
  controls come from its own segments `seg<N> duration controls…` (N = its number; same syntax
  as `seg`), which run in parallel with vehicle 1's timeline; after its last segment all its
  controls are 0. Its values are applied directly as the car's inputs on every frame before the
  physics: throttle (also the engine's throttle), brake, steering, handbrake — no quantisation,
  no pedal swap, no automatic reverse, no standstill handbrake; the automatic gearbox works as
  usual, and `gear=g` sets gear g + 1 (`vehicle-powertrain.md`, "Set gear") on each frame of that
  segment. The engine scale of its unstuck helper is 1. If its model is vehicle 1's model it uses
  vehicle 1's bound (`car-collision.md` 3.1). The timeline stands in for the computer driver in its
  ordinary driving state, so the driver's own physical side effect applies: every frame, before the
  physics, its `CarFrictionHandling` = 2.0 if it carries the "touched the player" mark from the
  previous frame's physics, else 1.0 (`car-collision.md` 5.4; the mark is cleared on every mover
  at the start of each frame's physics and set by any collision test between vehicle 1 and it
  that gives an impact). The police catch-up (`car-collision.md` 5.7) is not applied: it depends on
  a pursuit mode that check runs do not have.
- `traffic <type> x y z heading speed=<v>`: an ambient traffic car of `.aiVehicleData` type
  `<type>` (default speed 0: parked). Its model origin starts at (x, y, z) — y is the road height,
  0 on the flat world — with the heading's rotation (rows as for vehicle 1, cosine and sine in
  double precision rounded to float). While it has no body it follows a straight lane along its
  forward direction f = −(Z row): once per frame, before the physics, d = v·F + d (d = 0 at the
  start, F the frame length), origin = (f.x·d + x, y, f.z·d + z); its lane speed is v. It becomes
  physical, comes to rest and is released exactly as `car-collision.md` sections 4 and 6 say;
  after release it stays where it came to rest with lane speed 0 (the lane behaviour MM2 would
  resume belongs to the traffic spec and is not part of check runs), and it becomes physical again
  if something hits it.
- Movers (`car-collision.md` 1.1) in check runs: vehicle 1, then the `police`/`ai` cars in file
  order, then the physical traffic cars in pool order; traffic cars without a body are gathered by
  every mover that gathers, in file order (subject to the broad test), and only on the flat world
  (city runs with extra vehicles use the rooms as in the game).
- The frame: vehicle 1's control step, the computer cars' inputs, the traffic lanes; then the
  physics samples (`car-collision.md` 1.2); then, once per frame, the release check of physical
  traffic cars (`car-collision.md` 6.5), then vehicle 1's world-limit checks.
- The run's length is vehicle 1's timeline. The random generator is shared and restarted as
  before.

**Output.** The CSV header adds, for each extra vehicle N in number order, the columns
`xN,yN,zN,vxN,vyN,vzN,fxN,fyN,fzN,uxN,uyN,uzN,speedN,stateN`, and every row adds the matching
values after vehicle 1's:

| Column | Meaning |
|---|---|
| xN yN zN | model origin: a computer car's as for vehicle 1; a traffic car's matrix origin (its body position when physical) |
| vxN vyN vzN | body velocity while it has a body; for a traffic car without a body (on its lane, or after release) (s′·Z.x, s′·Z.y, s′·Z.z) with s′ = −s, s its current lane speed (the run's `speed` on its lane, 0 after release) and Z its matrix's Z row — so after release each component is −0·Z.c: −0 (printed `-0`) where Z.c is +0 or positive, +0 (printed `0`) where Z.c is −0 or negative |
| fxN fyN fzN, uxN uyN uzN | forward (−Z row) and up (Y row) of its current matrix |
| speedN | v · f (signed) |
| stateN | computer car: 1. Traffic car: 0 on its lane (never hit), 1 physical, 2 released upright (MM2 would send it back to its lane), 3 released wrecked |

Floats with 6 significant digits as before.

### Standard car-to-car runs

| File | Vehicles | Script |
|---|---|---|
| `cc_headon_vpbug.run` | vpbug; police vpcop facing it 80 m ahead | both: 1 s idle; 9 s full throttle |
| `cc_tbone_vpmustang99.run` | vpmustang99; ai vpbug crossing from the right (33, 0, −50, heading 90) | both: 1 s idle; 9 s full throttle |
| `cc_rearend_vpcab.run` | vpcab; traffic va_sedans_s parked 40 m ahead | 1 s idle; 5 s full throttle; 4 s nothing |
| `cc_sideswipe_vpauditt.run` | vpauditt at x = −2.4; traffic va_taxi_f at 20 m/s starting 40 m behind | 1 s idle; 4.5 s throttle; 1 s throttle + steer 0.4; 3.5 s throttle 0.5 |
| `cc_bus_vpbus.run` | vpbus; traffic va_compact_s crossing at 8 m/s (−32, 0, −25, heading 270) | 1 s idle; 9 s full throttle |
| `cc_chain_vpbug.run` | vpbug; traffic va_sedans_s (0, 0, −30, heading 0) and va_compact_s (0, 0, −34, heading 0), parked | 1 s idle; 9 s full throttle |

The run files are in `checks/`.

## Opponent runs (the computer driver)

A run may hand vehicle 1 to the computer driver as a race opponent (`../ai/opponents.md`). Runs
without an `opponent` line are unchanged in every respect.

```
car vpbug
world sf
opponent race0-a-0.opp 0.70 0 50.0 0.70 1 1 1 1 0 1.0
seg 45.0
```

- `opponent <file> <throttle> <sw1> <pad> <brake> <sw2> <sw3> <sw4> <sw5> <sw6> <corner>`: the
  eleven values are fields 2–12 of an `[Opponent]` row (`../formats/aimap.md`, "Opponent rows"),
  in that order, all required; the row's car is the run's `car`. The opponent's index is 0. Only
  city worlds; not together with extra vehicles or a `frame` line (yet).
- The route file is looked up in the run file's folder first, then as `race/<world>/<file>` in the
  game data (case-insensitive), and read as `../formats/opp.md` says, with the city's room lists
  (`../ai/road-network.md`).
- The car is set up as an opponent (`opponents.md`, "The opponent's car"): full simulation, the box
  bound (no polygon bound is loaded in such a run), the AI unstuck values (`TimeThresh` 0.5), the
  run's `paint` (default 0, opponent 0's paint), regenerate off (the run is not a Circuit).
- The run's `start` line is not used. The car is placed at the file's start point with its start
  heading (`opp.md`; the rotation rows from cos θ and sin θ computed in double and rounded to float,
  as for other city runs), the ground snap of `opponents.md` ("Start position", step 2) is applied
  and the car placed again; then the random generators restart as for every run.
- Each frame: the opponent's update (`opponents.md`, "One frame": on the first frame its orders
  with laps 1; then the driver's frame; then the fall-through test), then the physics samples.
  There is no player control step and none of the player's world-limit checks (no −50 m or water
  reset: the car is never placed again).
- There is no player: "avoid players" finds nothing and `CarFrictionHandling` is 1.0. For the loose
  bodies' rooms (`../physics/loose-bodies.md` 5.1) vehicle 1 is declared as the player's car is.
  There is no traffic, and props are not registered with the roads in check runs, so the road and
  intersection obstacle lists are empty and the avoid switches find nothing; the AI-ahead marks
  have no effect. The driver's real-time clock is the sum of the frame lengths since the start.
- **Output**: the CSV header adds, after `c3`, the columns
  `state,mode,w,lap,tx,ty,tz,steer,throttle,brake,hbrake`: the opponent's state (0 RACING,
  1 DISABLED), the driver's mode (0 DRIVE, 1 TURN_BACK, 2 RECOVER, 3 HOLD), its waypoint index
  *w* and lap, its target point *T*, and the car's steering, throttle, brake and handbrake inputs.
  Each row shows these values as they are during that row's step, i.e. after the opponent update
  of the frame the step belongs to; the first row (the state before the first step) shows those
  of the first frame. Floats with 6 significant digits.
- The driving trace (`drive-trace.md`) of such a run adds one record per frame, written after the
  opponent update and before the frame's first `step` record:
  `opp state mode w lap s0 s1 s2 tx ty tz`, with s0 … s2 the road numbers of the driver's window
  (−1 for none; shortcuts numbered after the main roads, `../formats/bai.md`).

### Standard opponent runs

| File | Car | Opponent line | Script |
|---|---|---|---|
| `opp_sf_race0_vpbug.run` | vpbug | `opponent race0-a-0.opp 0.70 0 50.0 0.70 1 1 1 1 0 1.0` (SF `race0.aimap`, row 0) | 45 s |

## Trailer runs

A run may tow the car's trailer (`../physics/trailer.md`). Runs without it are unchanged.

```
car vpsemi
world flat
start 0 0 0 0
trailer on                 # the car's trailer (an error if the car has none, trailer.md 1)
seg 1.0
seg 6.0 throttle=1
seg 2.0 throttle=0.3 detach=1    # Ctrl+B is pressed on the first frame of this segment
seg 5.0 brake=1
```

- `trailer on` attaches the car's MM2 trailer, set up as `trailer.md` section 2 says. The car is
  placed exactly as without it (flat: resting at ride height; city: MM2's placement rule), then the
  trailer is placed from the car's starting body by `trailer.md` section 3 (its previous matrix is
  its starting frame, no previous push source). It is not lowered to the ground: it settles by
  itself (its wheels are within a few centimetres of the ground).
- `detach=1` (a segment control, default 0): the Ctrl+B key goes down on the first frame of the
  segment (`trailer.md` 7.2); the joint comes off in that frame's first physics step.
- The frame and the physics step follow `trailer.md` section 8: the car's control step, then the
  physics steps (car, trailer, collisions as `car-collision.md` 1.2 with the trailer as mover 2),
  then the car's world-limit checks; a world-limit reset puts both back (`trailer.md` section 3).
  City runs include everything the plain city runs include (placed objects, poles, loose bodies).
- Not defined together with extra vehicles or a `frame` line. No driving trace is defined for
  trailer runs yet.

**Output.** The CSV header adds after vehicle 1's columns:
`tx,ty,tz,tvx,tvy,tvz,tfx,tfy,tfz,tux,tuy,tuz,tspeed,tw0,tw1,tw2,tw3,tc0,tc1,tc2,tc3,tlean,tbroken`:

| Column | Meaning |
|---|---|
| tx ty tz | the trailer's body position (= its model origin) |
| tvx tvy tvz | its body velocity |
| tfx tfy tfz, tux tuy tuz | its forward (−Z row) and up (Y row) |
| tspeed | velocity · forward |
| tw0..tw3 | spin of its wheels `TWHL0`…`TWHL3` (sign as for the car) |
| tc0..tc3 | their `comp + SuspensionExtent` |
| tlean | the lean angle of the last joint update that ran (`trailer.md` 5.4: 0 while within `FreeLean`; 0 before the first update; a reset does not change it; after the joint came off it keeps its last value) |
| tbroken | 1 while the joint is off (from the step it came off until a reset re-attaches it), else 0 |

Floats with 6 significant digits.

### Standard trailer runs

| File | Car | World | Script |
|---|---|---|---|
| `trailer_accel_vpsemi.run` | vpsemi + trailer | flat | 1 s idle; 15 s full throttle |
| `trailer_turn_vpsemi.run` | vpsemi + trailer | flat | 1 s idle; 4 s throttle; 8 s throttle 0.6 + steer −1 |
| `trailer_jackknife_vpsemi.run` | vpsemi + trailer | flat | 1 s idle; 8 s throttle; 1 s throttle + steer 0.6; 5 s brake + steer 1 + handbrake |
| `trailer_reverse_vpsemi.run` | vpsemi + trailer | flat | 1 s idle; 3 s brake (automatic reverse); 12 s brake + steer 1 (backing up on full lock folds the trailer) |
| `trailer_detach_vpsemi.run` | vpsemi + trailer | flat | 1 s idle; 6 s throttle; 2 s throttle 0.3 with `detach=1`; 5 s brake (the free trailer runs into the truck) |
| `trailer_wall_london_vpsemi.run` | vpsemi + trailer | london, −230.0 1.9 333.5 heading 180 | 1 s idle; 12 s full throttle (the truck hits a wall at 6.5 s; the joint breaks) |

The run files are in `work/checks-ref/` with the reference CSVs (`tools/sim/drivecheck.py`).

## Tolerance

Positions within 1 cm and speeds within 0.01 m/s over the first 10 simulated seconds, and the same
gear changes at the same steps. Larger drift later in a run is reported, not failed, because
single-precision rounding differences grow over time. Any difference in the first second is a spec
question, not noise.
