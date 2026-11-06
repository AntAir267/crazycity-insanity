# Vehicle physics: overview and the simulation step

This is the entry page of the car-physics spec. It fixes units and conventions, the time step,
the exact order of everything that happens in one physics step, the car's starting state for the
check runs, the meaning of `world flat`, and how to get MM2's numeric behaviour in 32-bit floats.
The parts have their own pages:

| Page | Contents |
|---|---|
| `vehicle-tune.md` | tune files: syntax, keys, defaults, which file feeds what, mod cars |
| `vehicle-body.md` | the rigid body: mass, inertia, integration (explicit and implicit), pushes, contact interface |
| `vehicle-powertrain.md` | engine, gearbox (automatic and manual), drive trains, brakes, differential |
| `vehicle-wheels.md` | wheel placement, ground query, suspension, tyre forces, surface materials |
| `vehicle-helpers.md` | aerodynamics, anti-roll bars, stability helpers ("gyro"), getting unstuck, player controls |
| `trailer.md` | the trailer some trucks tow: set-up, wheels, hitch joint, collisions, detaching |

The aim is that a C++ implementation following these pages produces the same numbers as MM2 for
the same inputs and time steps, and the same numbers as the reference used for `drive-check.md`.

## Units and conventions

- Lengths in metres, time in seconds, mass in kilograms, forces in newtons, torques in N·m,
  angles in radians, angular speeds in rad/s. Engine speeds in the tune files are in RPM.
- **Gravity is −19.6 m/s² along world Y** (twice the real value; this is MM2's choice and every
  tuned number assumes it).
- Horsepower to watts: × 746. RPM to rad/s: × 0.10471976 (π/30); rad/s to RPM: × 9.549296.
- Miles per hour: MM2 converts with **m/s × 2.236025 = mph** (used by the gearbox and the player
  controls below). A HUD may show km/h = m/s × 3.6.
- World: right-handed, +Y up. Model space (`../formats/pkg.md`): +Y up, the car faces −Z, +X is
  its right. A rotation matrix is stored as three rows, the body's X, Y and Z axes expressed in
  world space; a model-space vector `p` becomes `p.x·X + p.y·Y + p.z·Z` (plus position for
  points). "Local to world" always means this row form.
- Wheel order everywhere: wheel 0 = `WHL0` (front left), 1 = `WHL1` (front right), 2 = `WHL2`
  (back left), 3 = `WHL3` (back right), with positions from the `.mtx` files (`vehicle-wheels.md`).
- `sign(x)` is +1, −1 or 0 (0 for x = 0). `clamp(x, a, b)` as usual. `|x|` is the absolute value.

### Wheel spin sign

A wheel's spin rate `ω` is positive when the wheel turns the way that would drive the car
**backwards**: a wheel rolling forward without slip has `ω·radius = −(forward speed)`. All
powertrain equations below use this sign; the check output reports it unchanged.

## Time step

MM2 has no fixed time step. Each rendered frame has a duration `F` (seconds, float32), clamped to
0.0001 ≤ F ≤ 0.1. The physics runs `n` equal sub-steps per frame:

```
n  = min(3, ceil((F − 0.001) / (1/35)))      (1/35 as float32 = 0.028571429)
dt = F / n        invdt = 1 / dt              (both float32)
```

So at 60 frames per second there is one physics step of 1/60 s per frame, at 30 fps two of 1/60 s,
at 10 fps three of 1/30 s. `dt` and `invdt` are the "step time" and "inverse step time" used by
every formula in these pages.

**Check runs** (`drive-check.md`) use F = float32(1/60) = 0.016666668 for every frame, so
n = 1, dt = 0.016666668 and invdt = float32(1/dt).

## One frame

1. **Controls** (once per frame): the player's controls become the car's inputs through MM2's
   game-level control step (`vehicle-helpers.md`, "Player controls"): quantisation, the throttle/
   brake swap while reversing, automatic reverse, the standstill handbrake, gear selection.
2. **Physics**: `n` steps, each as below.
3. **World limits** (cities only, once per frame): the fall-through check and the water timer of
   `collision.md` ("World limits"); either can reset the car.

## One physics step

The car's per-step work, in this exact order. "Accumulators" are the body's force and torque
sums (`vehicle-body.md`); anything added to them during step k is integrated at the start of
step k+1.

1. **Gravity**: add `(0, mass·(−19.6), 0)` to the body force accumulator.
2. **Speed**: `speed = |(Z.z·v.z + Z.y·v.y) + Z.x·v.x|` (v = body velocity; Z = the Z row of the
   model frame of step 5 of the previous step, or of the starting state: the body's Z axis row,
   except after the unstuck helper turned or lifted the body in step 11), `speedMPH = speed ×
   2.236025`. These two values are what "the car's speed" means everywhere else (gearbox,
   controls, aero, steering).
3. **Wheel inputs** (`vehicle-wheels.md`, "Steering and brake inputs"): the steering angle and brake
   torque of each wheel from the car's inputs.
4. **Body integration** (`vehicle-body.md`): add the post-step forces, integrate velocity,
   angular velocity, position and orientation with this step's `dt`, clear the accumulators.
5. **Model frame**: the car's world matrix = body rotation, with position
   `body position + R·CoG` (see "Centre of mass" below).
6. **Engine** update (`vehicle-powertrain.md`).
7. **Gearbox** update.
8. **Aerodynamics** (`vehicle-helpers.md`).
9. **Wheel groups**, in this order: for drivetrain types 0 and 1 the left free train, the right
   free train, then the drive train; for type 2 only the drive train. Each group: query the ground
   and update the suspension of each of its wheels, solve the group's spin, then compute each
   wheel's tyre forces (`vehicle-powertrain.md`, "Drive trains", and `vehicle-wheels.md`).
10. **Anti-roll bars**: front axle, then back axle (`vehicle-helpers.md`).
11. **Stability helpers** ("gyro") and the **unstuck** helper (`vehicle-helpers.md`), then the
    **water effect** (`collision.md`, "World limits": switched on when the car's room is deep
    water and the model origin is below the water level; while on it adds forces and torques to
    the body accumulators), then the car's **room** is updated from its reference point (centre
    of mass + body Y axis, `collision-objects.md`, "Gathering").
12. **World collision** of the car body (`collision.md`, "The physics step": gather placed
    objects, the car's bound against the city, then against each gathered object). It adds
    impulses and pushes to the body and records the push source (`vehicle-body.md`, "Contact
    interface"). On the flat world the bound is tested against the one ground polygon.
13. **End of step**: keep the model frame of step 5 as the previous matrix and the push source as
    the previous source, then apply the pushes (the body position moves by its accumulated push,
    `vehicle-body.md`).

The check output row for the step is taken after step 13.

**Order of the contributions to the body accumulators** (force F, torque T; float addition is not
associative, so this order is part of the behaviour). After step 4 clears them:
1. engine neutral rock torque (step 6, only in neutral);
2. aerodynamics (step 8): the angular-damping torque, then the drag force, then the downforce;
3. wheel groups (step 9), group by group in the order of step 9, and inside a group wheel by wheel
   in the group's wheel order (`vehicle-powertrain.md`, "Wheel groups"): each wheel adds its tyre
   force and then its torque (`vehicle-wheels.md`, "Tyre (second call)"; `vehicle-body.md`,
   "Forces at a point"). A wheel's implicit contact goes to the separate post accumulators
   (`vehicle-body.md`, "Implicit contacts"), not to F and T;
4. anti-roll bars, front then back (step 10);
5. gyro: drift, handbrake turn, pitch, roll (step 11); then the water effect (step 11).
Collisions (step 12) add impulses and pushes, not forces; the unstuck helper's roll impulses
(step 11) reach the impulse accumulators before them. The post accumulators are added to F and
T at the start of the next step's integration.

### Check output values

For `drive-check.md`, each row is computed from the state after step 13 (row 0 from the starting
state):

| Column | Value |
|---|---|
| `t` | number of steps done × dt |
| `x y z` | model origin = body position + R·CoG (formula in "Centre of mass"), recomputed from the current body state |
| `vx vy vz` | body velocity `v` |
| `fx fy fz` | −(body Z axis row) |
| `ux uy uz` | body Y axis row |
| `speed` | `v · f` (signed; not the unsigned `speed` of step 2) |
| `rpm` | engine displayed RPM (`vehicle-powertrain.md`) |
| `gear` | gear index − 1 |
| `w0..w3` | wheel spin `ω` (sign as in "Wheel spin sign") |
| `c0..c3` | wheel `comp + SuspensionExtent` |

### Centre of mass

The rigid body is simulated at the centre of mass. The car's model origin (what is drawn and
what the wheels hang from) is offset from it by the tune value `CenterOfGravity` (`CoG`) turned
by the body rotation:

```
model origin = body position + CoG.x·X + CoG.y·Y + CoG.z·Z
```

evaluated per component as `(Y_c·CoG.y + Z_c·CoG.z) + CoG.x·X_c` for the x component and
`(Y_c·CoG.y + X_c·CoG.x) + Z_c·CoG.z` for y and z (X_c etc. the matching component of each axis
row), then added to the body position. So the centre of mass sits at model-space position
**−CoG** (with `CenterOfGravity 0 −0.1 0` it is 0.1 m above the model origin). Keep this sign:
the static wheel loads (`vehicle-wheels.md`) use `CoG` as written and the two together give MM2's
weight distribution.

## Starting state for check runs

The run file gives `start x y z heading`. For `world flat`:

- Orientation: a turn about world +Y by `heading` degrees (positive turns the car to its left);
  rows X = (cos h, 0, −sin h), Y = (0, 1, 0), Z = (sin h, 0, cos h), with cos/sin computed in double
  precision from `heading·π/180` and rounded to float32.
- Ride height: `h0 = (d0 + d1 + d2 + d3) × 0.25` with `d_i = radius_i − pivot_i.y` (wheel radius
  and pivot from `vehicle-wheels.md`), summed left to right. The model origin is at
  `(x, y + h0, z)`; `y` is the height of the flat ground (0 in the standard runs). The body
  position is that origin minus `R·CoG` (same component formula as above).
- Everything moving is at rest: body velocity, angular velocity, momenta, accumulators, pushes all
  zero; each wheel's suspension compression 0, compression rate 0, tyre deflections 0, spin 0,
  depth 0, material friction 1; drive trains at rest with differential ratio 1.
- Engine and gearbox in their reset state (`vehicle-powertrain.md`): engine speed 0, displayed RPM
  = `IdleRPM`, gear "1st" (index 2) with the gear-change flag set and the engine's clutch-delay
  (`GCL`) timer running, so the engine gives no torque for the first `GCL` seconds and the
  automatic gearbox cannot shift until then. The drive train starts disconnected from the engine.
- Controls zero, automatic gearbox, reverse mode off.

With these values every wheel starts within about a centimetre of where it settles, and the car
settles by itself during the first steps.

## Placing the car in a city (MM2's rule)

When MM2 puts a car at a start point `q` with heading `θ` (race start, Cruise drop point,
`../engine/controls.md` "Free-roam start"), it sets the **body position to `q + CoG`** (the tune
`CenterOfGravity` added without rotation) and the rotation to the turn by `θ` about world +Y
(rows `(cos θ, 0, −sin θ)`, `(0, 1, 0)`, `(sin θ, 0, cos θ)`), then resets everything else as in
the starting state above. The model origin is therefore at `q + CoG + R·CoG`, and the car falls
from there onto the ground; MM2's start points are slightly above the road for that reason. Check
runs use their own rules (above for `world flat`; below for cities).

**City check runs** (`drive-check.md`): the run's `start` (x, y, z) is the point `q` of the rule
above: body position = q + CoG (unrotated), the heading rotation above with its cosine and sine
computed in double precision and rounded to float (as for flat runs), and every other value the
"Starting state" one. Collision bookkeeping starts empty: the car's room is found from its
reference point with no hint, every wheel's probe state has room hints 0 and an empty polygon
cache, there is no push source, the previous matrix is the starting model frame and the water
effect is off. The car then falls onto whatever is below it.

## The flat world

`world flat` is defined in `collision.md` ("Flat world for check runs"): one endless horizontal
polygon at the ground height, normal (0, 1, 0), made of MM2's asphalt road material (`cobblestone`
in `city/materials.mtl`: friction 0.9, elasticity 0.9, drag 0, width 0, height 0, depth 0), no
rooms, objects, water or fall-through limit. The wheels probe it (`vehicle-wheels.md`,
"Flat-world query") and the car's bound collides with it like with any city polygon. Weather: dry
(friction scale 1.0, `vehicle-wheels.md`).

## Precision: reproducing MM2 in 32-bit floats

MM2 sets the x87 FPU to 24-bit precision for the whole game. Every addition, subtraction,
multiplication, division and square root is therefore rounded to float32 precision immediately
(only the exponent range is wider). A C++ implementation reproduces this by:

- Keeping **all** state and intermediates in `float` (IEEE single), one rounding per operation.
- **No fused multiply-add** and no reassociation: compile with `-ffp-contract=off` (and never
  `-ffast-math`); WebAssembly has no FMA by default.
- Evaluating each formula of this spec **left to right as written**: `a + b + c` means
  `(a + b) + c`, `a·b·c` means `(a·b)·c`, a dot product `a·b` means
  `(a.x·b.x + a.y·b.y) + a.z·b.z`, a cross product `a × b` means
  `(a.y·b.z − a.z·b.y, a.z·b.x − a.x·b.z, a.x·b.y − a.y·b.x)`, a matrix product `A·B` (rows)
  means `C[i][j] = (A[i][0]·B[0][j] + A[i][1]·B[1][j]) + A[i][2]·B[2][j]`, and "local to world"
  `p.x·X + p.y·Y + p.z·Z` is summed left to right per component.
- `sqrt` correctly rounded in float32 (`std::sqrt(float)`); `sin`, `cos`, `pow` computed in double
  precision and rounded to float32 once. All constants are the float32 values written here
  (e.g. 0.10471976, 2.236025, 0.083333336); integer-to-float conversions are exact.
- Comparisons on float values exactly as written (`<` versus `<=` matters at the boundaries the
  spec names).

The reference produced for `drive-check.md` follows exactly these rules, so two careful
implementations agree to the last bit in the early steps; after that, differences stay tiny unless
the motion is chaotic (spins, airborne tumbling). MM2 itself sometimes groups sums differently
from the canonical order above; the difference is at most a unit in the last place per operation,
far inside the check tolerances.

## What is not covered yet

- Trailers are in `trailer.md` (only `vpsemi`, and the unlisted `vpftruck` and `vplafrance`, tow
  one; `vpcentury` gets none, `car-model.md` "Trailers"). The plain `vpsemi` check runs are made
  without the trailer; `drive-check.md` "Trailer runs" adds it.
- Network cars. (Damage has no effect on handling, `damage.md`; computer cars and traffic cars
  are in `car-collision.md`.)
- The body-versus-world collision itself is the collision spec (`collision.md` and companions);
  this spec defines what the body provides to it (`vehicle-body.md`, "Contact interface").
