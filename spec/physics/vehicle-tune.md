# Vehicle tune files

A car's handling comes from text files in the archives' `tune/vehicle/` folder plus the wheel
pivots in `geometry/` (`vehicle-wheels.md`). This page gives the file syntax, every key the
physics reads, the defaults used when a key or a whole file is missing, and the quirks of how the
values are distributed over the four wheels.

## Which files

For a car with base name `<car>` (the `.info` `BaseName`, `../formats/car-model.md`):

| File | Feeds |
|---|---|
| `tune/vehicle/<car>.vehCarSim` | body, engine, gearbox, drive trains, wheels, anti-roll bars, aerodynamics |
| `tune/vehicle/<car>.vehGyro` | stability helpers |
| `tune/vehicle/<car>.vehStuck` | unstuck helper |
| `geometry/<car>_whl0.mtx` … `_whl3.mtx` | wheel pivots, radii, widths |

File names are looked up case-insensitively through the archive file system (shipped data has
`VPDDBUS.vehcarsim`, `Vpcaddie.vehcarsim`, `vpcab.vehstuck`, …). A mod car adds or replaces these
files in its own archive; which archive wins when several contain the same path is defined in
`../formats/ar.md` ("Several archives"). If a file is missing, every value it would set keeps its
default.

Opponents, police and player cars all read `<car>.vehCarSim`, with one exception:

- **The player's `vpcop`** is set up a second time from `vpmustang99`: after the normal set-up,
  MM2 repeats the whole car-simulation set-up with the name `vpmustang99`, so the `.vehCarSim`
  **and the wheel pivots** (`vpmustang99_whl0…3.mtx`) come from the Mustang, while the gyro, unstuck
  and damage files, the drawn model and the collision bound (`bound/vpcop_BOUND.bnd`) stay those
  of `vpcop`. The bound's material values are re-applied from the car-simulation values at every
  reset, so after the second set-up the bound uses the Mustang's `BoundFriction` and
  `BoundElasticity`. (A command-line switch
  `-tune_car` disables this; ignore it.) Keys missing from `vpmustang99.vehCarSim` keep the value
  read from `vpcop.vehCarSim` in the first set-up (the shipped files have the same keys). Opponent
  and police `vpcop`s use `vpcop.vehCarSim`. The
  check run `accel_vpcop` is a player car and therefore uses the Mustang values. One exception:
  the water-effect sample points (below) are fixed in the first set-up, so they keep `vpcop`'s
  values.
- **Water-effect inputs.** The "CoG" and "Size" of the water effect (`collision.md`, "World
  limits") are this car's `CenterOfGravity` and `InertiaBox`. The 64 points are taken in the
  rigid-body frame, whose origin is the centre of mass, so their grid is centred at body
  position + R·CoG (one CoG offset beyond the centre of mass), as written.

The shipped data also contains `<car>_opp.vehcarsim`, `<car>_cop.vehcarsim`, `*_Angel*`, `*OLD`
and `Copy of …` files: MM2 never reads them (no code builds those names). The `.info` file's
`Horsepower`, `Top Speed`, `Mass` and similar keys are menu text only and do not affect physics.

## Syntax

Plain text, read as whitespace-separated tokens (spaces, tabs, line ends):

```
type: a
vehCarSim {
  Mass 1000.000000
  InertiaBox 2.000000	2.000000	3.000000
  Aero {
    Drag 0.500000
  }
  ...
}
```

1. The first two tokens must be `type:` and `a` (a binary variant `type: b` exists but is never
   shipped; it may be rejected).
2. The next token is the root name (`vehCarSim`, `vehGyro`, `vehStuck`); it is not checked.
3. Then a block: `{`, entries, `}`. Inside a block, each entry is a key token followed by its
   value tokens, or a key followed by a nested block `{ … }`. A stray `{` is ignored; `}` ends the
   current block; end of file ends everything.
4. Keys are matched **case-sensitively** against the tables below.
5. Values: an integer key reads one token as a decimal integer (optional sign, then digits; it
   stops at the first other character). A float key reads one token as a decimal number (the
   longest prefix that is a valid number, as `strtod`), stored as float32. A vector key reads
   three such float tokens.
6. Unknown key: if the next token is `{`, the whole balanced block is skipped; otherwise the rest
   of the current line is skipped.
7. Repeated keys: the last one wins.

## Keys and defaults: `.vehCarSim`

Root block:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `Mass` | float | 2000 | kg |
| `InertiaBox` | vector | 2 1 3 | box dimensions (m) whose inertia the body gets (`vehicle-body.md`) |
| `CenterOfGravity` | vector | 0 0 0 | offset between centre of mass and model origin (`vehicle.md`) |
| `BoundFriction` | float | 0.3 | friction of the car body against the world (given to the collision side) |
| `BoundElasticity` | float | 0.2 | elasticity of the car body against the world (collision side) |
| `DrivetrainType` | int | 0 | 0 = rear-wheel drive, 1 = front-wheel drive, 2 = four-wheel drive; MM2 fails on any other value (no wheels and no gear ratios); treat other values as 0 and warn |
| `SSSValue` | float | 1 | steering factor at and above `SSSThreshold` |
| `SSSThreshold` | float | 0 | speed (m/s) where the steering factor reaches `SSSValue`; 0 disables |
| `CarFrictionHandling` | float | 1 | how surfaces with friction below 1 affect this car (`vehicle-wheels.md`) |

Block `Aero`:

| Key | Type | Default |
|---|---|---|
| `AngCDamp` | vector | 0 0 0 |
| `AngVelDamp` | vector | 0 0 0 |
| `AngVel2Damp` | vector | 0 0 0 |
| `Drag` | float | 0 |
| `Down` | float | 0 |

Block `Engine`:

| Key | Type | Default |
|---|---|---|
| `AngInertia` | float | 1 |
| `MaxHorsePower` | float | 200 |
| `IdleRPM` | float | 750 |
| `OptRPM` | float | 5000 |
| `MaxRPM` | float | 8000 |
| `GCL` | float | 0.25 |

Block `Trans`:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `ManualNumGears` | int | 7 | gear slots in manual mode, counting reverse and neutral (so 7 = R, N, 5 forward) |
| `AutoNumGears` | int | 6 | gear slots in automatic mode, counting reverse and neutral |
| `Reverse` | float | 20 | mph in reverse gear at `OptRPM` |
| `Low` | float | 20 | mph in 1st gear at `OptRPM` |
| `High` | float | 75 | mph in the top gear at `OptRPM` |
| `GearBias` | float | 0.5 | spacing of the middle gears |
| `UpshiftBias` | float | 0.05 | |
| `DownshiftBiasMin` | float | 0.05 | |
| `DownshiftBiasMax` | float | 0.3 | |
| `GearChangeTime` | float | 0.8 | seconds after a shift before the automatic may shift again |

Gear slot counts must be 3…8.

Blocks `Drivetrain` and `Freetrain` (same keys):

| Key | Type | Default |
|---|---|---|
| `AngInertia` | float | 5000 |
| `BrakeDynamicCoef` | float | 1 |
| `BrakeStaticCoef` | float | 1.2 |

Blocks `WheelFront` and `WheelBack` (same keys):

| Key | Type | Default |
|---|---|---|
| `SuspensionExtent` | float | 0.2 |
| `SuspensionLimit` | float | 0.1 |
| `SuspensionFactor` | float | 1 |
| `SuspensionDampCoef` | float | 0.1 |
| `SteeringLimit` | float | 0.39 |
| `SteeringOffset` | float | 0 |
| `BrakeCoef` | float | 1 |
| `HandbrakeCoef` | float | 1 |
| `CamberLimit` | float | −1 |
| `WobbleLimit` | float | 0 |
| `TireDispLimitLong` | float | 0.075 |
| `TireDampCoefLong` | float | 0.25 |
| `TireDragCoefLong` | float | 0.02 |
| `TireDispLimitLat` | float | 0.075 |
| `TireDampCoefLat` | float | 0.25 |
| `TireDragCoefLat` | float | 0.05 |
| `OptimumSlipPercent` | float | 0.14 |
| `StaticFric` | float | 2 |
| `SlidingFric` | float | 1.9 |

`CamberLimit` and `WobbleLimit` only tilt the drawn wheel and have no physical effect.

Blocks `AxleFront` and `AxleBack` (same keys):

| Key | Type | Default |
|---|---|---|
| `TorqueCoef` | float | 0 |
| `DampCoef` | float | 0 |

## How the values reach the wheels and trains

MM2 reads each wheel block into **one** wheel and then copies it, incompletely:

- `WheelFront` is read into wheel 0 (front left), `WheelBack` into wheel 2 (back left).
- Wheel 1 (front right) then takes from wheel 0, and wheel 3 (back right) from wheel 2, every
  wheel key **except `HandbrakeCoef` and `WobbleLimit`**. The right-hand wheels therefore always
  have `HandbrakeCoef 1` and `WobbleLimit 0`, whatever the file says. (So a car with
  `HandbrakeCoef 2` brakes its left rear wheel twice as hard as its right rear wheel when the
  handbrake is pulled. Reproduce this.)
- `Freetrain` is read into the left free train and copied completely to the right free train.
- `Drivetrain` feeds the drive train. `AxleFront`/`AxleBack` feed the front/back anti-roll bar.

## Keys and defaults: `.vehGyro`

| Key | Type | Default |
|---|---|---|
| `Drift` | float | 0 |
| `Spin180` | float | 0 |
| `Reverse180` | float | 0 |
| `Pitch` | float | 0 |
| `Roll` | float | 0 |

## Keys and defaults: `.vehStuck`

| Key | Type | Default |
|---|---|---|
| `Turn` | float | 1.57 |
| `Rotation` | float | 0.39 |
| `Translation` | float | 0.1 |
| `TimeThresh` | float | 0.3 |
| `PosThresh` | float | 1.25 |
| `MoveThresh` | float | 1.75 |

## Order of set-up

Derived constants must be computed in this order (several depend on earlier ones):

1. Read `.vehCarSim`.
2. For each wheel 0…3: pivot, radius and width from its `.mtx`; then its load constants
   (`vehicle-wheels.md`, "Static load and derived constants") — wheels 1 and 3 still have default
   tune values at this point; it does not matter because step 6 recomputes them.
3. Body mass and inertia from `Mass` and `InertiaBox`; angular speed limit 4π rad/s per axis.
4. Copy wheel 0 → 1 and 2 → 3 (except the two keys above) and recompute 1 and 3; copy the free
   train.
5. Assign wheels to trains by `DrivetrainType` (`vehicle-powertrain.md`).
6. Engine constants, then gearbox ratios and shift points (they use the radius of the drive
   train's first wheel), then anti-roll constants, then the load constants of wheels 1, 3, 0, 2.
7. Read `.vehGyro` and `.vehStuck`.

Step 2's load-constant computation can permanently raise `SuspensionFactor` to 0.75
(`vehicle-wheels.md`); since step 4 copies the already raised value, the result is the same as
applying the rule once per wheel.
