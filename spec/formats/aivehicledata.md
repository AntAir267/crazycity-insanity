# Traffic car data: `.aiVehicleData` and the other files of a traffic car

A traffic car type (`va_…`, named in the `.aimap` car mix, `../ai/traffic-population.md`) is made
of these files, looked up through the archives (`ar.md`), case-insensitively:

| File | Needed | Contents |
|---|---|---|
| `tune/vehicle/<name>.aiVehicleData` | yes (see "Missing file") | size, mass, centre of gravity and collision values (below) |
| `geometry/<name>.pkg` | yes | the model (`car-model.md`, "Traffic cars") |
| `geometry/<name>_WHL0.mtx` … `_WHL5.mtx` | WHL0–WHL3 | wheel positions; WHL0 also gives the wheel radius |
| `geometry/<name>_HEADLIGHT0.mtx`, `_HEADLIGHT1.mtx` | optional | headlight glow positions (`night.md`) |
| `tune/banger/<name>_<PART>.dgBangerData` | optional | prop data for `BREAK0`–`BREAK3` parts that can break off a knocked car (none ship; the shipped `<name>_WHL*` and `_HEADLIGHT*` banger files are not read by traffic) |
| `bound/<name>_BOUND.bnd` | no | ships for every type but is not read: a traffic car's collision shape is the box below |

The `.info`, `.vehCarSim` and other player-car files do not exist for traffic types and are not
read.

## Syntax

The text tune-file syntax of `../physics/vehicle-tune.md` ("Syntax"): `type: a`, a root name (not
checked; shipped files say `aiVehicleData`), then a block of `Key value` entries; keys are
case-sensitive, unknown keys are skipped, the last of repeated keys wins. A binary `type: b` form
exists in the parser but is never shipped.

## Keys

| Key | Type | Used for |
|---|---|---|
| `Mass` | float | kg, mass of the body of a knocked car (`../ai/traffic-reactions.md`, "The body") |
| `Size` | vector | collision box dimensions x (width), y (height), z (length), m; also the box inertia of the body |
| `CG` | vector | centre of the collision box relative to the model origin (the body's centre of mass stays at the origin) |
| `Spring` | float | wheel spring of a knocked car (N per m of compression) |
| `Damping` | float | wheel damper of a knocked car (N per m/s) |
| `Limit` | float | wheel travel: the probe reaches wheel radius + `Limit` beyond the pivot; compression −`Limit` when off the ground |
| `RubberSpring` | float | tyre stick-slip stiffness of a knocked car's wheels (sideways and forward) |
| `RubberDamp` | float | tyre stick-slip damping |
| `MaxDamage` | float | read, but has no effect (MM2 compares it with a damage value that stays 0) |
| `Elasticity`, `Friction` | float | read, never used: every traffic box has elasticity 0.5 and friction 1.0 |
| `PtxThresh`, `MaxAng` | float, vector | read, never used |

**Defaults.** MM2 gives these keys no default values: a key missing from the file leaves whatever
the memory held. All shipped files used by traffic set every key; `va_cablecar_f`,
`va_garbagetruck` and `va_ug_l` (not used as traffic) lack `CG`. **Engine:** a missing key is 0
(vector 0 0 0).

**Missing file.** MM2 reports "No file for '<name>' for vehicle data" once and the car uses the
**first** traffic type that was loaded successfully in this session (data slot 0). **Engine:** the
same, or skip the type in the mix with a warning if no type has loaded yet.

At most 32 different traffic types can be loaded (MM2 has 32 slots and does not check); each type
is loaded once and shared by all cars of that type.

## Derived numbers

From the file (single precision, each operation rounded):

- **Collision box**: min = CG + (−Size × 0.5), max = CG + Size × 0.5 per component; radius
  √((hz × hz + hy × hy) + hx × hx) with h = Size × 0.5.
- **front** = −min.z (origin to front bumper), **back** = max.z (to rear bumper),
  **left** = −min.x, **right** = max.x. Used by driving and placement
  (`../ai/traffic-driving.md`) and the ground probes (`../ai/traffic-visuals.md`).
- **Wheel positions**: the origin (floats 9–11) of `<name>_WHL0.mtx` … `_WHL5.mtx`; a missing file
  gives (0, 0, 0) and no wheel (car-model.md lists WHL0–WHL3 as required for traffic).
- **Wheel radius**: |(max.y − min.y) × 0.5| of `WHL0.mtx` (floats 1 and 4).

## Shipped values

`tools/ai/traffic.py data`:

| Type | Mass | Size (x y z) | CG (x y z) | Elast. | Fric. | front | back | left | right | wheel r | wheels |
|---|---|---|---|---|---|---|---|---|---|---|---|
| va_2siter_s | 570.47 | 1.86 1.18 4.44 | 0.03 0.76 0.15 | 0.10 | 0.50 | 2.06 | 2.37 | 0.90 | 0.96 | 0.347 | 4 |
| va_2sitersport_l | 979.27 | 1.91 1.20 4.26 | 0.02 0.77 0.07 | 0.50 | 0.50 | 2.06 | 2.20 | 0.93 | 0.98 | 0.347 | 4 |
| va_bus_f | 11761.62 | 2.60 3.27 13.85 | −0.00 1.88 0.45 | 0.10 | 0.50 | 6.48 | 7.38 | 1.30 | 1.30 | 0.529 | 6 |
| va_cab_l | 1000.09 | 1.99 1.69 4.63 | 0.00 1.05 0.05 | 0.50 | 0.50 | 2.26 | 2.37 | 0.99 | 0.99 | 0.389 | 4 |
| va_compact_s | 530.76 | 1.76 1.17 3.55 | 0.00 0.75 −0.06 | 0.90 | 0.10 | 1.83 | 1.72 | 0.88 | 0.88 | 0.272 | 4 |
| va_cooper_l | 653.78 | 1.71 1.20 3.19 | 0.00 0.80 0.02 | 0.50 | 0.50 | 1.58 | 1.61 | 0.86 | 0.86 | 0.272 | 4 |
| va_ddbus_l | 1092.88 | 2.66 4.37 8.70 | −0.02 2.35 −0.24 | 0.10 | 0.50 | 4.59 | 4.11 | 1.35 | 1.31 | 0.498 | 4 |
| va_diesels_s | 581.25 | 2.66 2.92 9.76 | 0.00 1.89 1.05 | 0.90 | 0.10 | 3.83 | 5.93 | 1.33 | 1.33 | 0.458 | 4 |
| va_euro_l | 850.53 | 1.91 1.24 3.60 | 0.02 0.77 −0.10 | 0.10 | 0.50 | 1.89 | 1.70 | 0.93 | 0.98 | 0.347 | 4 |
| va_eurocargo_l | 2941.87 | 2.30 2.31 5.54 | 0.01 1.58 0.20 | 0.10 | 0.50 | 2.57 | 2.97 | 1.14 | 1.15 | 0.458 | 4 |
| va_eurovan_f | 690.98 | 2.27 1.75 5.00 | 0.00 1.15 0.14 | 0.90 | 0.10 | 2.36 | 2.64 | 1.13 | 1.14 | 0.353 | 4 |
| va_largesuv_s | 495.84 | 2.19 1.45 4.72 | −0.00 1.00 0.09 | 0.90 | 0.10 | 2.27 | 2.45 | 1.09 | 1.09 | 0.364 | 4 |
| va_limo_f | 525.47 | 2.35 1.41 7.59 | 0.02 0.93 0.10 | 0.91 | 0.15 | 3.69 | 3.89 | 1.16 | 1.20 | 0.381 | 4 |
| va_minivan_s | 1993.50 | 2.23 1.88 4.75 | 0.00 0.92 0.08 | 0.10 | 0.50 | 2.29 | 2.46 | 1.11 | 1.11 | 0.315 | 4 |
| va_pickup_f | 509.85 | 2.16 1.33 5.27 | −0.00 0.99 0.28 | 0.91 | 0.15 | 2.36 | 2.92 | 1.08 | 1.08 | 0.364 | 4 |
| va_sedans_s | 585.10 | 1.84 1.12 4.29 | 0.00 0.82 0.17 | 0.91 | 0.15 | 1.97 | 2.31 | 0.92 | 0.92 | 0.301 | 4 |
| va_smallsuv_s | 586.09 | 2.17 1.29 3.88 | 0.00 0.92 0.08 | 0.91 | 0.15 | 1.86 | 2.02 | 1.08 | 1.09 | 0.364 | 4 |
| va_taxi_f | 539.64 | 1.94 1.61 4.94 | 0.02 0.97 0.12 | 0.91 | 0.15 | 2.34 | 2.59 | 0.95 | 0.99 | 0.336 | 4 |
| va_ultrasport_l | 769.26 | 1.86 1.02 4.05 | 0.03 0.63 −0.08 | 0.50 | 0.50 | 2.11 | 1.94 | 0.90 | 0.96 | 0.347 | 4 |
| va_wedelivertruck_f | 586.96 | 2.47 2.13 5.29 | 0.00 1.42 0.11 | 0.91 | 0.15 | 2.53 | 2.76 | 1.24 | 1.24 | 0.386 | 4 |

(Not used as traffic: `va_cablecar_f` 7572.56 kg, `va_garbagetruck` 5418.78 kg, `va_ug_l`
13082.49 kg; all three without `CG` and without wheel pivots. Also shipped and never read:
`tune/vehicle/va_pickup_f.asNode` and `va_taxi_f.asNode`, older copies with root name `asNode`.)

Every shipped file has `Limit` 0.07 and `MaxAng` 0 0 0, except `va_garbagetruck` (`MaxAng` 1 0 0).
The other values follow fixed ratios in most files (`Spring` = `MaxDamage` / 4,
`Damping` = 0.07 × `Spring`, `RubberSpring` = 10 × `Damping`, `RubberDamp` = `RubberSpring` / 20,
`PtxThresh` = `MaxDamage`), so they were generated by a tool; `va_euro_l`, `va_eurocargo_l` and
`va_wedelivertruck_f` break the `Spring` ratio, `va_diesels_s` the `RubberSpring` one.

## Checked against

- All 23 `.aiVehicleData` files parse with the syntax above (`tools/ai/traffic.py data`); every
  key of the table is present in the 20 traffic types used by the shipped mixes.
- Bumper distances against the models: `va_sedans_s` front 1.97 / back 2.31 m, body length 4.29 m;
  `va_bus_f` 6.48 / 7.38 m (13.85 m bus, 6 wheels).
