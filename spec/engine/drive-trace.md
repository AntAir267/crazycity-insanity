# Driving trace (for finding differences)

An optional, more detailed output for a check run. When the ordinary CSV of two implementations
differs, both produce this trace for the same run and the first differing line shows which
calculation drifted. It is a debugging aid: the format matters, the file is not shipped.

## How to ask for it

The check-run tool takes a trace flag (the engine: `cctool drive --trace <file.run>`; the
reference: `drivecheck.py --trace`). It writes the trace instead of the CSV, to standard output.

## Format

Plain text, one record per line, fields separated by single spaces. Every float is written as the
exact bits of its single-precision value, in C's `%a` hexadecimal form of the float converted to
double (for example `0x1.99999ap-4`), so equal lines mean bit-identical values. Integers are decimal.

Records appear in this order for every physics step `k` (0-based), each step starting with `step`:

| Record | Fields |
|---|---|
| `step k t` | step number, simulated time |
| `ctl throttle brake steer handbrake gear` | the car's control values used in this step (after the control step) |
| `body px py pz vx vy vz wx wy wz` | body position, linear velocity, angular velocity at the start of the step, before integration |
| `rot m00 m01 m02 m10 m11 m12 m20 m21 m22` | body rotation (rows are the X, Y, Z axes) at the start of the step |
| `wheel i hit T nx ny nz px py pz mat obj comp spin` | after the wheel's probe and suspension update in this step: hit 0/1; T; normal; hit point; material code (−1 if none); object id (−1 = city polygon, else the placed object's index in the order objects were loaded); suspension compression; spin rate |
| `force fx fy fz tx ty tz` | force and torque sums collected in this step, just before world collision |
| `contact n` | number of impacts the world collision produced in this step |
| `impact j kind px py pz nx ny nz depth obj` | for each impact in the order they are resolved: kind (`city`, `obj`, `prop`), point, normal, depth, object id (−1 for city) |
| `push px py pz` | the push applied at the end of the step (0 0 0 if none) |
| `end px py pz vx vy vz wx wy wz` | body state after the step |
| `eject part id Px Py Pz Hx Hy Hz` | a part of the car thrown off in this step, in a city run (`../physics/loose-bodies.md` 4): the part name (`BREAK01`, `WHL0`, …), the object id of its moved-prop slot, the momentum it was given and the angular impulse added (after step 5 there) |
| `break obj n id…` | a prop broken in this step: its object id, its `NumParts` n, then the object ids of the slots it became (one for n = 0, else one per piece, in piece order) |
| `loose id px py pz vx vy vz wx wy wz` | one per loose body that is a mover with switch U after the step, in mover order: its slot's object id, then its body's position, velocity and angular velocity after the step |

The `eject` and `break` records of a step come right after its `end`, in the order the events
happened, then the `loose` records. Two records are written between steps, where they happen:
`gone id` when a loose body outside the player's rooms is removed at the start of a frame's physics
(`loose-bodies.md` 5.1), and `rest id` when a loose body is let go after a frame's samples (5.2).
An opponent run also writes one `opp` record per frame before that frame's first `step`
(`drive-check.md`, "Opponent runs").

Wheels are listed in the car's wheel order. A wheel group that is not updated in a step (none in
MM2's cars) still writes its line with `hit 0` and zeros. Floats that are NaN or infinite are
written as `nan`, `inf`, `-inf`.

## Exact meanings

These settle the points the table leaves open; both implementations follow them.

1. `step k t`: t = float(k) · dt, the time at the start of the step.
2. `ctl`: the car's inputs after the control step (pedal swap, quantisation, standstill handbrake);
   gear is the transmission's current gear at the start of the step, in the CSV convention.
3. `wheel`: hit = 1 whenever the probe returned a hit, even if the wheel's ground test then rejects
   the normal. With no hit, T, normal and point are 0, mat −1, obj −1. `comp` is the raw
   compression (not compression + SuspensionExtent as in the CSV). `spin` is the spin at that
   moment: the previous step's solved value, before this step's group solve.
4. Material code = 1 + the material's position in the city material table (the default material
   prints 1; a city polygon stored with code 0 also prints 1). On a probe-cache hit it is the
   material kept from the previous query. A prop's own material, which is not in the table, prints 0.
5. Object id = position among the placed objects that can collide, in load order: `<city>.inst`
   records in file order, then `<city>_ai.inst`; each record's own object first (a landmark or
   spanning object with a bound, or a prop with a `.dgBangerData`), then its package's xref props in
   xref order. Records that make no collidable object (plain statics, landmarks without a bound) are
   not counted. (London: the subway landmarks wl_subway01_h…l are 194–198.) After all of these come
   the traffic-light poles (`../ai/traffic-control.md`, "Setting up a light set"), in creation
   order (intersection order, then light order), every pole counted, including one whose room is 0;
   a pole whose model has no `.dgBangerData` makes no object and is not counted. A moved-prop slot
   (`../physics/loose-bodies.md`, 1) has the object id 100000 + its slot number (0 … 39), whatever
   it currently holds.
6. `force`: the force and torque accumulators just before world collision, i.e. everything added
   after this step's integration cleared them (water effect included).
7. `impact`: city impacts in resolve order, then each gathered object's impacts in gathering order;
   capsule impacts are written after their normal is flipped for the hand-over. Kind `prop` for any
   untouched prop (they go through the breakable rule), `obj` for landmarks and spanning objects,
   `loose` for an impact with a loose body or a moved prop (pair impacts with loose bodies come
   after the city impacts and before the gathered objects', `car-collision.md` 1.2).
8. `push`: the body's push accumulator just before it is applied at the end of the step (after the
   merge rule).
9. Float text: C's `%a` of the float converted to double (trailing zeros dropped, zero as `0x0p+0`).
   Comparison tools compare values, so other exact hexadecimal spellings are also acceptable.

## Runs with extra vehicles

For a run with extra vehicles (`drive-check.md`, "Extra vehicles") the trace has a different,
simpler layout. For every physics step k:

| Record | Fields |
|---|---|
| `step k t` | as above |
| `veh N p px py pz vx vy vz wx wy wz` | one per vehicle, in number order, at the start of the step: p = 1 if it has a rigid body, then its body position, velocity and angular velocity; p = 0 for a traffic car without a body, then its matrix origin, its velocity as the CSV defines it (`drive-check.md`, vxN) and 0 0 0 |
| `carimpact a b px py pz nx ny nz depth` | every car-to-car impact in resolve order (`car-collision.md` 3.4): a and b are the vehicle numbers of the impact's own side A and side B (a box pair can file the later vehicle first, `car-collision.md` 3.2), then the impact's point, normal (out of B into A) and depth |
| `vend N px py pz vx vy vz wx wy wz` | one per vehicle that has a rigid body after the step, in number order: its body state after the step |

Floats as in "Format".

