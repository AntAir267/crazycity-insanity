# Vehicle helpers: aerodynamics, anti-roll bars, stability helpers, unstuck, player controls

## Aerodynamics (step 8 of `vehicle.md`)

Tune block `Aero`: `AngCDamp` (cd), `AngVelDamp` (vd), `AngVel2Damp` (v2d), `Drag`, `Down`.

Angular damping, per body axis `i` = X, Y, Z (rows of `R`, `w` = body angular velocity in world):

```
wb_i = R_i · w                                  angular velocity about the body axis (orders below)
t_i  = −(sign(wb_i)·cd_i)                        constant ("Coulomb") part
t_i  = (t_i − wb_i·vd_i) − |wb_i|·wb_i·v2d_i
if |wb_i| < |t_i|·dt:  t_i = −(invdt·wb_i)       never overshoot zero in one step
if |w_i| < 1:          t_i = |w_i|·t_i            NB: w_i is the WORLD x/y/z component of w
t_i  = t_i·I_i                                   body inertia
body torque += t_X·X + t_Y·Y + t_Z·Z            (orders below)
```

Sum orders (`X`, `Y`, `Z` = body axis rows, `t` after the multiplication by `I`):

```
wb_X = (X.z·w.z + X.y·w.y) + X.x·w.x
wb_Y = (Y.y·w.y + Y.z·w.z) + Y.x·w.x
wb_Z = (Z.y·w.y + Z.z·w.z) + Z.x·w.x
torque.x += (t_X·X.x + t_Y·Y.x) + t_Z·Z.x
torque.y += (t_Z·Z.y + t_Y·Y.y) + t_X·X.y
torque.z += (t_Z·Z.z + t_Y·Y.z) + t_X·X.z
```

(Scaling by the world component in the fifth line, not the body one, is MM2's; keep it.)

Then drag and downforce (in this order, after the torque), with `speed` from step 2 of
`vehicle.md`:

```
body force += v · (−(speed·Drag))               (per component v·k + F)
body force += Y · (−(speed·speed·Down))         (Y = body up axis)
```

So drag is `Drag·|forward speed|·v` against the velocity and downforce pushes along the car's own
down direction.

## Anti-roll bars (step 10)

One per axle: front = wheels (0, 1), back = wheels (2, 3), from `AxleFront`/`AxleBack`:

```
k = TorqueCoef·Iz ;  t = sqrt(k·Iz)·DampCoef ;  d = t + t          (Iz = body inertia about Z)
```

Update, if `k != 0` (left wheel `a`, right wheel `b`):

```
diff = (a.comp − visualDisp(a)) − (b.comp − visualDisp(b))
tq   = −(diff·k + (a.rate − b.rate)·d)
body torque += Z·tq                              (Z = body axis row, the car's roll axis)
visualDisp(w) = clamp-below-0-to-0 of (w.radius·0.05·w.Fsusp)/w.L0, at most w.radius·0.3
```

(`visualDisp` is how far MM2 lowers the drawn wheel into its tyre; here it also feeds the bar:
compute `x = (radius·0.05·Fsusp)/L0`; if `x < 0` use 0; if `x <= radius·0.3` use `x`, else
`radius·0.3`.)

## Stability helpers ("gyro", step 11)

From `<car>.vehGyro`. `allDown` = 1 if all four wheels are grounded, else 0 (MM2 computes
grounded-count / 4 in integer arithmetic). `wt` = the drive train's speed (`vehicle-powertrain.md`).
`Iy`, `Ix`, `Iz` = body inertia. All three add to the body torque accumulator.

**Drift** (always active when `Drift > 0`): with `st = sss·steer` (`vehicle-wheels.md`):

```
tq = −(((allDown·(|st|·st))·(−wt))·(Iy·Drift))
body torque += Y·tq
```

A yaw push in the steering direction that grows with the drive train speed.

**Handbrake turn** (when `|handbrake| > 0.01` and `Spin180 > 0` or `Reverse180 > 0`):

```
c  = Spin180 if wt < 0 else Reverse180         (wt < 0 = rolling forward)
tq = −(allDown·steer·−wt·Iy·c)                 (raw steer)
body torque += Y·tq
```

**Air levelling** (when `|brake| > 0.01` and `Pitch > 0` or `Roll > 0`): with
`f = brake·brake·(1 − allDown)` (so only while not all wheels touch the ground):

```
kp = (Ix·Pitch)·f ;  py = Z.y          body torque.c += kp·(py·X.c)      for c = x, y, z
kr = −((Roll·Iz)·f) ;  ry = X.y        body torque.c += kr·(ry·Z.c)
```

(each component computed as written and added to the accumulator; the drift, handbrake-turn,
pitch and roll terms are added in that order). No shipped car uses `Pitch` or `Roll`.

## Unstuck helper (step 11, after the gyro)

From `<car>.vehStuck` (`Turn`, `Rotation`, `Translation`, `TimeThresh`, `PosThresh`,
`MoveThresh`); `P2 = PosThresh·PosThresh` and `M2 = MoveThresh·MoveThresh` are computed when the
file is read. State: `state` (INACTIVE, WATCH, TURN, ROLL, LIFT), `timer`, `start`.

**Waking.** Every impact report (`vehicle-body.md`, "Contact interface": one per impact the
collision side resolves against the car, whatever its impulse and push) does: if `state` is
INACTIVE, `state = WATCH` and `start = body position` (the position at that moment, before the
step's pushes are applied). It does not touch `timer`. Reports come from the collisions of step
12, so the helper first runs at step 11 of the next step. It never wakes on the flat world unless
the bound touches the ground polygon.

**Deactivate** means `state = INACTIVE`, `timer = 0`. A reset of the car (start of a check run,
world-limit reset) also deactivates it.

Each step while not INACTIVE, with `p` = body position, `s` = `start`,
`dx = s.x − p.x`, `dy = s.y − p.y`, `dz = s.z − p.z`, `d3 = (dz·dz + dy·dy) + dx·dx`,
`dxz = dx·dx + dz·dz`, `steer` and `throttle` the car's steering and throttle inputs, and
`turnWanted` = `(throttleScale·0.75) < throttle and |steer| > 0.5`, where `throttleScale` is the
car's throttle scale: 1.0 for the player's car (set to 1.0 when the car is reset; nothing else
writes it), and for computer cars the driver's throttle level *k*_thr, written every frame
(`car-collision.md` 5.3, `../ai/driver.md`). The scale has no other use in the vehicle:

```
if d3 > M2:  deactivate ;  done
WATCH:
    timer = dt + timer
    if not (dxz > P2) and no wheel grounded and timer >= TimeThresh
       and (Translation > 0 or Rotation > 0):
        state = ROLL if Rotation > 0 else LIFT ;  done
    if not (dxz > P2) and timer >= TimeThresh and turnWanted:
        state = TURN ;  done
    if not (timer > TimeThresh):  done
    deactivate
LIFT:   (put the car back on its wheels in one go)
    with Z = body Z row:  X = (Z.z − 0, 0, 0 − Z.x)       (so a zero component comes out +0)
    l2 = (X.z·X.z + 0·0) + X.x·X.x ;  k = 0 if l2 == 0 else 1/sqrt(l2) ;  X = X·k (per component k·X.c)
    Y = (0, 1, 0)
    Zn = (X.y·Y.z − X.z·Y.y,  X.z·Y.x − Y.z·X.x,  Y.y·X.x − X.y·Y.x)
    body rows = X, Y, Zn ;  body position y = position y + Translation
    deactivate
ROLL:   (roll it over with impulses)
    if body Y row's y > 0.7:  deactivate ;  done
    G.y = ((|steer|·m)·Translation) + G.y                 (linear impulse, vehicle-body.md)
    q = (steer·m)·Rotation ;  H.c = q·Z.c + H.c           (Z = body Z row, c = x, y, z)
    car throttle = 0 ;  car brake = 1
TURN:   (stuck nose-in while steering: rotate on the spot)
    if not turnWanted:  deactivate ;  done
    a = (|steer|·Turn)·steer ;  if gear == reverse (index 0): a = −a
    R = R·RotY(−(dt·a))                                   (matrix product of vehicle-body.md "Products")
```

`m` is the body mass. `RotY` is the rotation of the wheel frame (`vehicle-wheels.md`): rows
`(cos, 0, −sin)`, `(0, 1, 0)`, `(sin, 0, cos)` with cosine and sine each rounded to float (MM2's
rotation about a coordinate axis, not the general `Rot` of `vehicle-body.md`). "No wheel grounded"
is the grounded flags of the four wheels from their last contact call. Comparisons are written so
that a NaN goes the way MM2's does (`not (x > y)` is true for a NaN). ROLL stays in ROLL until the
car is upright (Y row's y above 0.7) or has moved away; TURN until the conditions end. The
throttle and brake set by ROLL last until the next frame's controls overwrite them. The model
frame of step 5 is not recomputed after LIFT or TURN, so the next step's speed (`vehicle.md`
step 2) still uses the previous frame's Z row.

No check run reaches TURN, ROLL or LIFT (the city runs wake the helper on wall contacts, and it
goes back to INACTIVE after at most `TimeThresh`), so the check outputs do not exercise those
branches.

## Player controls

What MM2 does between the player's controls and the car, once per frame before the physics. This
is the vehicle-side view of `../engine/controls.md` (steps 2–6 of its frame), which owns the
controls and describes the same rule; the steps are repeated here because the check runs depend
on them. The check runs feed their `throttle`, `brake`, `steer`, `handbrake` (and `gear`) values
here as the player's pedals, steering and handbrake after keyboard/gamepad smoothing.

State: `reverseMode` (false at start), the gearbox mode (automatic unless the run or the player
selects manual).

```
1. Quantise (MM2 records every frame's controls as bytes):
     q255(x) = float(trunc(x·255) & 255)·0.003921569        (trunc toward zero, x·255 in float)
     q127(x) = float(trunc(x·127) as signed 8-bit)·0.007874016
     a = q255(accelerate) ;  b = q255(brake) ;  h = q255(handbrake) ;  s = q127(steer)
2. Swap while reversing: if reverseMode: throttle = b, brake = a  else throttle = a, brake = b
3. Car inputs: throttle, brake, steer = s, handbrake = h
4. Automatic reverse (automatic gearbox only):
     if gear >= 2 and speed < 5 and brake > 0.8 and throttle < 0.1 (0.1 as a double):
         reverseMode = true ;  set gear 0
     else if gear == 0 and throttle < 0.8:
         reverseMode = false ;  set gear 2
5. Standstill handbrake: if speedMPH < 4 and throttle == 0:  handbrake = 1
6. Manual gearbox: the player's up/down shift requests change the gear by one slot
   (manual: up while gear < ManualNumGears − 1, down while gear > 0). For check runs, a segment's
   `gear = g` sets gear index g + 1 directly (set gear, `vehicle-powertrain.md`).
```

`speed` and `speedMPH` are the values from the last physics step (step 2 of `vehicle.md`,
computed before that step's integration). Because of step 1, `throttle=0.7` becomes 0.69803923,
`0.5` becomes 0.49803925 and `steer=0.5` becomes 0.496063, while 0, ±1 and 0.6 are unchanged.
Because of steps 2 and 4, holding the brake at low speed engages reverse and then the brake
control drives the car backwards; releasing it returns to 1st gear. The swap in step 2 uses
`reverseMode` as left by the previous frame.

For check runs: a run that uses `gear` in any segment is in manual mode for the whole run (no
automatic shifting, no automatic reverse); segments without `gear` keep the current gear. Runs
without `gear` use the automatic gearbox and steps 4–5 as above. Step 5 applies in both modes.

## Resetting the car onto its wheels

`../engine/controls.md` ("Getting back on the wheels") describes MM2's automatic righting and the
milestone's reset key. The vehicle side of that reset:

1. Heading `θ = atan2(Z.x, Z.z)` (body Z axis row; atan2 in double, rounded).
2. New body position: `(p.x, p.y + 0.5, p.z) + CoG` (CoG added unrotated, as MM2 does).
3. Reset everything as at the start of a run (`vehicle.md`, "Starting state"), except that the
   orientation is the turn by `θ` about world Y (rows `(cos θ, 0, −sin θ)`, `(0, 1, 0)`,
   `(sin θ, 0, cos θ)`) and the position is the one from step 2. The car then drops onto its
   wheels.

(The "in contact" condition of MM2's automatic righting reads a car-simulation field that MM2 only
ever clears, never sets, so in MM2 the automatic righting does not actually fire. The reset key is
our own addition and simply performs the steps above.)
