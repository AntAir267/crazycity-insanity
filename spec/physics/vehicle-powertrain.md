# Vehicle powertrain: engine, gearbox, drive trains and brakes

The engine makes a torque from throttle and its own speed. The gearbox picks a ratio. The wheels
are grouped into "trains": one **drive train** that can be coupled to the engine, and up to two
**free trains** for the undriven wheels. Each train has a single angular speed shared by its
wheels (through a slipping differential when it has two or four wheels), integrated once per step
against the brakes and the torque the road puts back through the tyres.

Spin sign: see `vehicle.md` (positive = turning the car backwards). Ratios are positive for
forward gears and negative for reverse, so with the engine coupled
`engine speed = −ratio · train speed` is positive.

## Wheel groups

By `DrivetrainType`:

| Type | Left free train | Right free train | Drive train (wheel order) | Update order |
|---|---|---|---|---|
| 0 (rear drive) | wheel 0 | wheel 1 | 2, 3 | left free, right free, drive |
| 1 (front drive) | wheel 2 | wheel 3 | 0, 1 | left free, right free, drive |
| 2 (four-wheel drive) | — | — | 0, 1, 2, 3 | drive |
| other | — | — | — | MM2 cannot load such a car (see `vehicle-tune.md`) |

Free trains take their constants from the `Freetrain` block, the drive train from `Drivetrain`
(`AngInertia` = `Itr`, `BrakeDynamicCoef`, `BrakeStaticCoef`). Train state: speed `wt` (reset 0)
and differential ratio `diff` (reset 1). Only the drive train can be coupled to the engine; a flag
`coupled` says whether it is (engine update, step 3).

## Engine

### Constants

```
wMax  = MaxRPM·0.10471976
wOpt  = OptRPM·0.10471976
wIdle = IdleRPM·0.10471976
gap   = MaxRPM·0.10471976 − wOpt ;  invGap = 1/(gap·gap)
K     = (1·MaxHorsePower·746) / (wOpt·wOpt·wOpt)          (the 1 is a fixed power scale)
φ1 = 1.618034 ;  φ2 = 0.618034
```

### Torque curves

Full throttle at engine speed `w`:

```
Tfull(w) = ((φ1·wOpt − w)·(φ2·wOpt + w))·K                                  if w <= wOpt
         = ((((D·A)·B)·C)·K)·invGap                                         if wOpt < w <= wMax
         = 0                                                                above wMax
  with D = (w + wMax) − (wOpt + wOpt),  A = φ2·wOpt + w,  B = φ1·wOpt − w,  C = wMax − w
```

(The association matters: the four factors of the middle range are multiplied in exactly the
order D, A, B, C, each product rounded to float.)

This gives exactly `MaxHorsePower` (×746 W) at `OptRPM`, the same torque at standstill as at
`OptRPM`, a peak of 1.25× that torque at half `OptRPM`, and 0 at `MaxRPM` (a soft rev limit).

Closed throttle (engine braking above idle, idle keeping below it), at the current engine speed
`we`:

```
Tzero = ((wIdle − we)·((1·MaxHorsePower·746)/wOpt)·0.75) / (wOpt − wIdle)
```

At throttle `th`: `Teng(we, th) = Tfull(we)·th + (1 − th)·Tzero`.

### State and reset

| Name | Reset | Meaning |
|---|---|---|
| `we` | 0 | engine speed, rad/s |
| `rpm` | `IdleRPM` | displayed engine RPM |
| `torque` | 0 | torque delivered this step |
| `throttle` | 0 | the car's throttle input |
| `waiting` | true | clutch delay (`GCL`) running |
| `gclTimer` | `GCL` | |
| `shiftRpm` | `IdleRPM` | RPM when the delay started |
| `power` | 0 | `torque·we·0.0013404826` (horsepower; display only) |

### Update (step 6 of `vehicle.md`)

```
1. if gearbox.changed and not waiting:  shiftRpm = rpm ;  gclTimer = GCL ;  waiting = true
2. torque = Teng(we, throttle)
3. Clutch, with r = current gear ratio:
       if r == 0 or we < wIdle:                    coupled = false
       else if we > wIdle + wIdle and not coupled:  coupled = true
4. if not coupled:  we = clamp((torque / AngInertia)·dt + we, 0, wMax)
5. rpm = we·9.549296
6. if waiting:
       if gclTimer <= 0:  waiting = false ;  gearbox.changed = false ;  we = rpm·0.10471976
       else:  torque = 0
              rpm = (shiftRpm·gclTimer + (GCL − gclTimer)·rpm) / GCL
              gclTimer = gclTimer − dt
7. power = torque·we·0.0013404826
8. Neutral rock: if the gearbox is in neutral (gear index 1):
       q = (torque / Tfull(wOpt))·AngInertia
       local = (q·AngInertia, 0, 0) for DrivetrainType 1, else (0, 0, −(q·AngInertia))
       local = (local.x·Ix, local.y·Iy, local.z·Iz)          (body inertia)
       body torque.c += (local.z·Z.c + local.y·Y.c) + local.x·X.c       for c = x, y, z
```

So after every gear change the engine delivers no torque for `GCL` seconds while the displayed
RPM slides from the old to the new value, and the automatic gearbox cannot shift again until the
delay ends (it is the engine that clears `gearbox.changed`). The clamp in 4 is: below 0 → 0,
above `wMax` → `wMax`.

A mod car with a `geometry/<car>_engine.mtx` pivot rocks a drawn engine part instead; MM2 then
uses that file's floats 6–8 as the axis of the neutral-rock torque. No shipped car has one; an
implementation may ignore it.

## Gearbox

### Gear slots

Gear index 0 = reverse, 1 = neutral, 2 = 1st, 3 = 2nd, …; `AutoNumGears` (automatic) or
`ManualNumGears` (manual) is the number of slots including reverse and neutral (≤ 8). The check
output's gear = index − 1. State: `gear` (reset 2), `changed` (set at reset, see `vehicle.md`),
`timer` (reset 0), `automatic` (true unless the player chose manual).

**Set gear** `g`: if `g != gear` and (manual, or `g < AutoNumGears`): `timer = 0`,
`changed = true`, `gear = g`.

### Ratios

`ratioFromMph(v) = OptRPM / ((C·v) / (radius0·6.2831855))`, where `radius0` is the radius of
the drive train's first wheel (wheel 2 for type 0, wheel 0 for types 1 and 2) and
`C = 26.822401` (float32 `0x41D69447`; MM2 computes it at start-up as the single-precision
product 1609.344 × 0.016666668, which is one unit in the last place above float32(26.8224)).
This is the engine-to-wheel ratio that puts the engine at `OptRPM` at `v` mph (C converts mph to
metres per minute).

For a table with `N` slots (done once for the automatic table with `AutoNumGears` and once for the
manual table with `ManualNumGears`):

```
ratio[0] = −ratioFromMph(Reverse)
ratio[1] = 0
ratio[2] = ratioFromMph(Low)
ratio[N−1] = ratioFromMph(High)                       (overwrites ratio[2] when N = 3)
if N − 2 > 2:
    n = N − 3
    step = pow(ratio[N−1] / ratio[2], 1 / n)          (division and 1/n in float; pow in double, rounded)
    for i = 1 … n − 1:
        e = i − ((i − (N − 2) + 1)·i·GearBias) / n    (float arithmetic; the integers converted exactly)
        ratio[2 + i] = pow(step, e) · ratio[2]                 (pow in double; the product of
                                                                that double and the float ratio[2]
                                                                rounded once to float)
```

With `GearBias` 0 the forward ratios are geometric; a positive bias moves the middle gears toward
the top gear.

### Automatic shift points

For `g` from 2 to `AutoNumGears − 2` (with `auto` the automatic ratio table):

```
q  = auto[g+1] / auto[g]
hi = MaxRPM ;  if q·OptRPM < hi:  hi = OptRPM / q
lo = OptRPM
while 1 < hi − lo:
    mid = (hi + lo)·0.5
    P1 = Tfull(mid·0.10471976)·(mid·0.10471976)
    P2 = Tfull(mid·0.10471976·q)·(mid·0.10471976·q)
    if P1 < P2:  hi = mid  else  lo = mid
shift = (hi + lo)·0.5
up[g]        = (UpshiftBias + 1)·shift
downMin[g+1] = (1 − DownshiftBiasMin)·shift·q
downMax[g+1] = (1 − DownshiftBiasMax)·shift·q
```

Then `up[AutoNumGears − 1] = MaxRPM`, `downMin[2] = downMax[2] = 0`. (The shift point is where
the power in the next gear equals the power in this gear; the arguments `mid·0.10471976` and
`mid·0.10471976·q` are each rounded to float before `Tfull`.)

### Update (step 7 of `vehicle.md`)

```
if no wheel is grounded:  stop (the timer does not advance either)
if automatic and gear > 1 and timer > GearChangeTime and not changed:
    if gear >= AutoNumGears − 1 or rpm <= up[gear]:
        if gear >= 3 and rpm < throttle·downMin[gear] + (1 − throttle)·downMax[gear]:
            set gear (gear − 1)
    else:
        set gear (gear + 1)
timer = dt + timer
```

`rpm` is the engine's displayed RPM, `throttle` the car's throttle input. In manual mode only the
player changes gear (`vehicle-helpers.md`).

## Drive trains

### Update (step 9 of `vehicle.md`, once per train, trains in the order given above)

Let the train have `nw` wheels `W[0..nw−1]` (in the table's order), speed `wt`, inertia `Itr`.
`r` is the current gear ratio, `E` the engine. "Engine coupled" means this is the drive train and
`coupled` is true.

```
1. Brakes:   bc = BrakeStaticCoef if wt == 0 else BrakeDynamicCoef
             brake = 50 ;  for each wheel: brake = W.brakeTorque·bc + brake
2. Drive:    if engine coupled:
                 te  = r·E.torque
                 tau = (r·wt + E.we)·E.AngInertia·invdt·r
             else te = tau = 0
             tau = tau + te
             for each wheel: tau = tau − W.roadTorque          (from the wheel's previous tyre update)
3. Net torque (it acts against the spin: positive net reduces wt):
             stop = false
             if wt == 0:
                 if tau < 0:  net = tau + brake ;  if net > 0: net = 0
                 else:        net = tau − brake ;  if net < 0: net = 0
             else:
                 stop = |tau| <= |sign(wt)·brake|
                 net  = tau + sign(wt)·brake
4. Compliance: if engine coupled:  Kc = r·E.AngInertia·r + 0.02
               else:               Kc = Mass·0.005          (car mass)
5. Contact:  for each wheel in order: the wheel's contact call (vehicle-wheels.md)
6. Differential (only changes diff):
             aw = |wt|
             dmax = (1.03·aw + (50 − aw)·1.25)·0.02  if aw < 50  else 1.03
             dmin = 1 / dmax
             if nw < 2 or (wt < 0.001 and −0.001 < wt):  diff = 1
             else:
                 imb = 0 ;  for k = 0 … nw/2 − 1:  imb = (W[2k].roadTorque − W[2k+1].roadTorque) + imb
                 target = imb / ((0 + Itr)·wt) + diff
                 d = dmax ;  if target <= dmax: d = target ;  if target < dmin: d = dmin
                 diff = (diff·9 + d)·0.1
7. Integrate: wn = ((−net) / (dt·Itr + Kc))·dt + wt
             if stop and the sign of wn is opposite to wt (wn < 0 < wt or wt < 0 < wn):  wn = 0
8. Engine:   if engine coupled:
                 lim = E.wMax / |r| ;  a = |wn|
                 wn  = (a if a <= lim else lim)·sign(wn)
             wt = wn
             if engine coupled:
                 ewant = −(r·wn)
                 if ewant >= 0:
                     if E.wMax < ewant:  ewant = E.wMax ;  wt = −(ewant / r)
                     E.we = ewant
                 else:  wt = 0                              (engine would turn backwards)
9. Wheel spins: for k = 0, 2, … while k < nw − 1:
                 W[k].ω = diff·wt ;  W[k+1].ω = (1/diff)·wt
             if nw is odd: W[nw−1].ω = wt
10. Tyres:   for each wheel in order: the wheel's tyre call (vehicle-wheels.md)
```

Notes:

- The effective inertia in step 7 is `dt·Itr + Kc`, so the tune's train inertia acts scaled by
  the step time, and the engine's inertia enters through the gear ratio squared.
- Step 2's middle term (`(r·wt + E.we)…`) is zero while engine and train turn together; it makes
  the engine's stored speed snap into the wheels when the clutch closes.
- The free trains have no engine, so they only feel the road torque and their brakes; the 50 N·m
  constant is a rolling resistance that always applies.
- The differential lets the two wheels of a pair turn at `diff` and `1/diff` times the train speed;
  `diff` drifts toward the torque imbalance, limited to 1.25 at standstill and 1.03 above 50 rad/s.
  For four-wheel drive the pairs are (0, 1) and (2, 3), sharing one `diff`.
- MM2 also computes per-wheel spin limits for an iterative clamp, but with its constants that clamp
  never changes `wn`; it is omitted here on purpose.

### Brakes in short

Each wheel's `brakeTorque` (`vehicle-wheels.md`) is `StaticFric·radius·L0·(BrakeCoef·brake +
HandbrakeCoef·handbrake)` (plus the input distribution rules there). The train adds them, scaled
by its static coefficient when stopped and its dynamic one when turning, plus 50 N·m. A braking
torque can stop a train in one step but not reverse it (step 7).
