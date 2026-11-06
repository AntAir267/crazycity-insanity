# Vehicle wheels: geometry, ground contact, suspension and tyres

Each of the four wheels is a ground probe with a spring-damper suspension and a tyre model with
longitudinal and lateral "deflection" springs. The wheel does its work in two calls per step,
both made by the wheel's group (`vehicle-powertrain.md`, "Drive trains"):

1. **Contact** (before the group solves its spin): ground query, contact frame, contact
   velocities, surface material, suspension force.
2. **Tyre** (after the group has set the wheel's spin): tyre forces, applied to the body.

All per-wheel state is listed in "Wheel state" at the end.

## Geometry

From `geometry/<car>_whl<i>.mtx` (12 float32: box min xyz, box max xyz, centre xyz, origin xyz;
`../formats/pkg.md`):

```
pivot  = origin (floats 9, 10, 11)        model space; this is the wheel centre at rest
radius = |(max.y − min.y) · 0.5|
width  = max.x − min.x
```

## Static load and derived constants

Each wheel gets a fixed "static load" `L0` from the car's mass and the wheel's fore-aft position
(`CoG` = tune `CenterOfGravity`, g = −19.6, cz = pivot.z):

```
L0 = −(g·Mass) · (|cz − CoG.z| / (|cz| + |cz|)) · 0.5
```

From `L0` and the wheel's tune values (`e` = `SuspensionExtent`, `l` = `SuspensionLimit`,
`f` = `SuspensionFactor`):

```
if f < 0.75:  f = 0.75                          (stored back into the tune value)
kk     = 1 / ((l + e)·e)
k      = (f·e + l)·kk·L0                        spring rate
prog   = ((f − 1)·kk·L0) / k                    progressive factor
damp   = 2·(sqrt(k·L0)·SuspensionDampCoef)      written as t = sqrt(k·L0)·coef; damp = t + t
mL     = L0 / g                                 (negative)
Klong  = (L0 + L0) / TireDispLimitLong
Dlong  = t + t  with t = sqrt(Klong·(−mL))·TireDampCoefLong
Klat   = (L0 + L0) / TireDispLimitLat
Dlat   = t + t  with t = sqrt(Klat·(−mL))·TireDampCoefLat
t      = StaticFric·radius·L0
brakeMax     = t·BrakeCoef
handbrakeMax = t·HandbrakeCoef
invOpt2      = 1 / (OptimumSlipPercent·OptimumSlipPercent)
```

With this spring, the suspension force is 0 at full extension, `L0` at compression 0 and grows
progressively above it.

## Steering and brake inputs

At step 3 of `vehicle.md`, from the car inputs (`steer` −1…+1, `brake` 0…1, `handbrake` 0…1,
`throttle` 0…1) and the car's `speed`:

**Speed-sensitive steering factor** `sss`: if `SSSThreshold` = 0 then 1; else if
`speed < SSSThreshold` then `(speed / SSSThreshold)·(SSSValue − 1) + 1`; else `SSSValue`.
`st = sss·steer`.

**Per wheel inputs** (each wheel `set(steerIn, brakeIn, handbrakeIn)`):

| Wheel | steerIn | brakeIn | handbrakeIn |
|---|---|---|---|
| 0, 1 (front) | `st` | `brake` | `−handbrake` if handbrake < 0 else 0 |
| 2 (back left) | `−st` | `brake'` | `hb'·(1 − steer)` if steer > 0 else `hb'` |
| 3 (back right) | `−st` | `brake'` | `hb'·(steer + 1)` if steer < 0 else `hb'` |

with `hb' = handbrake` if handbrake > 0 else 0, and `brake' = 0` if `brake > 0.95` and
`throttle > 0.95` and `speed < 1` (a burnout: brake and throttle together at standstill release
the rear brakes), else `brake`. Note the inner rear wheel gets less handbrake while steering, and
the raw `steer` (not `st`) is used for that.

`set(s, b, h)`:

```
a = −(s·SteeringLimit)
steerAngle  = (1 − a·SteeringOffset·sign(pivot.x))·a
brakeTorque = h·handbrakeMax + b·brakeMax
```

## Contact (first call)

### Wheel frame in the world

```
if SteeringLimit == 0:
    Wl = identity ;  c = pivot
else:
    o  = (sign(pivot.x)·width)·0.5
    Wl = RotY(steerAngle)            rows (cos a, 0, −sin a), (0, 1, 0), (sin a, 0, cos a)
    c  = (pivot.x − o, pivot.y, pivot.z) ;  c = c + o·Wl_X        (steering axis at the inner edge)
(Ww, wc) = (Wl, c)·M                 frame product of vehicle-body.md "Products"
```

`RotY(a)`: `cos a` and `sin a` of the float angle, each rounded to float (`−sin a` is the negated
float). `c + o·Wl_X` is per component `o·Wl_X.c + c.c`. `M` is the car's model frame
(`vehicle.md`, step 5: rows X, Y, Z = body rows, origin = model origin). The frame product is
evaluated even when `Wl` is the identity. Written out, with `p = c`:

```
Ww_i.j = matrix product element (Wl·R)[i][j]          (orders of the "A·B" table)
wc.x = ((p.y·Y.x + p.x·X.x) + p.z·Z.x) + origin.x
wc.y = ((p.y·Y.y + p.x·X.y) + p.z·Z.y) + origin.y
wc.z = ((p.z·Z.z + p.y·Y.z) + p.x·X.z) + origin.z
```

`Ww_X` is the wheel's axle direction, `Ww_Y` its up.

### Ground query

The wheel probes along a segment from above the wheel to below its fully extended position:

```
cTop   = SuspensionLimit + 0.3
top    = wc + Ww_Y·cTop                                (per component cTop·Ww_Y.c + wc.c)
bottom = wc − Ww_Y·(SuspensionExtent + radius)        (per component wc.c − d·Ww_Y.c)
len    = (SuspensionExtent + radius) + cTop
```

The query is the collision spec's **probe** (`collision.md`, "Probe"): `Probe(top, bottom, state,
ignore = this car, mask = ground)`, with one persistent probe `state` per wheel. The wheel uses the
hit's `T` (written `t` here: 0 at `top`, 1 at `bottom`), its `point` `x`, its unit `normal` `n`
(which may face away from `top` when a polygon is crossed from behind; such a hit then fails the
`cosA` test below) and its material (friction, drag, bump height = `height`, bump width = `width`,
depth).

**Flat-world query** (ground height `h`): `dS = top.y − h`, `dE = bottom.y − h`; there is a hit iff
the sign bits of `dS` and `dE` differ (+0 counts as positive, −0 as negative, as in the general
probe); then `t = dS / (dS − dE)`, `x = top + t·(bottom − top)` (per component
`top_c + t·(bottom_c − top_c)`), `n = (0, 1, 0)`, material as in `vehicle.md`.

### Contact frame

The wheel is **grounded** if there is a hit and

```
cosA = (n.z·Ww_Y.z + n.y·Ww_Y.y) + Ww_Y.x·n.x  >= 0.02
Back = Ww_X × n ;   b2 = (Back.x·Back.x + Back.y·Back.y) + Back.z·Back.z  >= 0.02
```

Then `Back = Back·(1/sqrt(b2))` per component (points to the rear along the ground),
`Lat = n × Back` (points to the right along the ground), `N = n`, and the hit point `x` is kept as
the contact point. The crosses are the standard ones, `a × b = (a.y·b.z − a.z·b.y,
a.z·b.x − a.x·b.z, a.x·b.y − a.y·b.x)`. A surface with `|n.y| < 0.001` (a vertical wall) gives no grip (friction 0 below).

### Not grounded

Run the suspension update with "not grounded" (below), set `Fz = Fsusp` and stop. The contact
velocities, material values and contact frame keep their previous values.

### Contact velocities (grounded)

`vel` = velocity of the contact point (`vehicle-body.md`, "Velocity of a point"), then

```
vLat = (vel.z·Lat.z + vel.y·Lat.y) + vel.x·Lat.x
vFwd = ((−Back.z)·vel.z + (−Back.y)·vel.y) + (−Back.x)·vel.x     (forward ground speed)
vN   = (vel.z·N.z + vel.y·N.y) + vel.x·N.x
```

### Surface

From the hit's material: `fric` (friction), `drag`, `bumpH` (height), `bumpW` (width), `depthT`
(depth). A polygon without a material uses MM2's built-in default: friction 1, drag 0, bump
height 0, bump width 1, depth 0 (the collision spec owns the material table).

```
bump  = 0 if bumpH == 0, else see "Bumps" below
matDrag = drag
matFric = fric
target  = depthT if the wheel was "majorly slipping" in its previous tyre update, else 0
rr      = |ω|·radius·0.1
if target > depth:  depth = rr·dt + depth ;  if target < depth: depth = target
elif target < depth: depth = depth − rr·dt ;  if depth < target: depth = target
if the surface is a wall (|n.y| < 0.001):  matFric = 0
matFric = weatherScale · matFric
if matFric < 1:
    if CarFrictionHandling >= 1:  matFric = (1/CarFrictionHandling)·matFric
    else:                         matFric = (1 − CarFrictionHandling)·(1 − matFric) + matFric
```

`weatherScale` is 1 normally; in **rain** (weather 3, `night.md`; MM2 has no snow weather) MM2
uses 0.8, or 0.75 when it is also night (time 3). MM2 sets this value only when a game starts in
rain and never puts it back to 1, so after a rainy game every later game of the same program run
keeps the last rain value, whatever its own weather (the game starts with 1). A new engine has to
choose between this history-dependent value and 1 for every game that is not rainy. **Our choice
(Ant, 2026-10-15):** fix MM2's bug: every game sets `weatherScale` from its own weather (1 when it
is not raining). The check runs have no weather (1). `ω` is the wheel's current spin (from the previous step).

**Bumps** (only when the material's bump height is not 0): the wheel keeps a phase `φ`;
`φ = (((rnd + 0.618)·dt)·s) + φ`, then `φ = fmod(φ, bumpW)` (C `fmod`, exact), and
`bump = sin((φ·6.2831855) / bumpW)·bumpH`, multiplied by `s` if `s < 1`, where
`s = sqrt(vFwd·vFwd + vLat·vLat)` and `rnd` the next number of the game's random generator, below.
The sine is **not** rounded to float before the product with `bumpH` (MM2 multiplies the x87
`fsin` result directly; compute the sine in double precision and round the product once).
Each wheel that stands on a bumpy material draws one number per physics step, when its ground
query is handled (so in the wheel-group order of `vehicle.md`, physics step 9).

**Random generator.** MM2 uses one game-wide generator for the bumps and for most other random
choices: a 32-bit state `x ← x·214013 + 2531011` (mod 2³²), giving the integer `(x >> 16) & 0x7fff`;
`rnd` is that integer times 3.0517578e−05 (1/32768, single precision), so 0 ≤ rnd < 1. When the
game restarts it, and what else draws from it, are in `collision.md` ("Random numbers"): in play
traffic, police, pedestrians and some visual effects share it (most particle effects use a
second state of their own), so bumps there are not reproducible number for number. **For check runs** the
state is 1 when the run starts, only the bumps draw from it, and a reset during the run
(fall-through or water, `collision.md`) restarts it with x = 1, as MM2's race reset does (MM2
restarts it before the cars are reset). City runs drive over bumpy materials (grass, dirt…), so
this matters there.

### Suspension (grounded)

```
newComp = (cTop + radius) − ((len·t − bump) + depth)
```

then the suspension update with "grounded", `newComp` and `cosA`; then `Fz = Fsusp`.

### Suspension update

State: compression `comp` (0 = rest geometry, −SuspensionExtent = fully extended, up to
+SuspensionLimit), compression rate `rate`, force `Fsusp`, implicit stiffness `Ks`, flag
`bottomed`. Inputs: grounded or not, `newComp`, `cosA`.

```
old = comp ;  bottomed = false
if not grounded:  comp = −e ;  ext = true
else:  comp = newComp ;  ext = false ;  if newComp < −e: comp = −e ; ext = true
rate = clamp((comp − old)·invdt, −10, 10)
s    = prog·comp + 1
Fsusp = (comp·k + rate·damp)·s + L0
if Fsusp < 0:                                   (spring would pull: wheel droops freely)
    Fsusp = 0
    a = dt·k
    comp = (old·damp − a·e) / (a + damp)
    rate = (comp − old)·invdt                   (no clamp)
    Ks = 0 ;  done
if comp > l:                                    (bottomed out)
    j = 0 ;  if vN < 0:  j = −(vN / invMassAlong(x, N))      (vehicle-body.md)
    pushF = invdt·j·0.25
    KsB   = −(pushF / vN)
    add push N·((comp − l)·cosA)                (vehicle-body.md, "Pushes")
    bottomed = true ;  comp = l
    s    = prog·comp + 1
    rate = clamp((comp − old)·invdt, −10, 10)
    Fsusp = (comp·k + rate·damp)·s + L0 ;  Fsusp = pushF + Fsusp
    Ks = KsB ;  done                            (vN = 0 gives KsB = −(0/0) = NaN: see below)
if not ext:  Ks = (dt·k)/cosA + s·damp
else:        Ks = 0
```

(`e` = SuspensionExtent, `l` = SuspensionLimit, `k`, `prog`, `damp`, `L0` from "Static load".) A
wheel that is not grounded therefore relaxes toward full extension over a few steps instead of
snapping down.

MM2 does not guard the division in `KsB`: with `vN` exactly 0 it stores NaN (all floating-point
exceptions are masked), and with `vN > 0` it stores −0. In both cases the tyre call's `Ks > 0`
test is false and no implicit contact is added. Use IEEE arithmetic without traps and keep the
comparison as written (a NaN compares false).

The check output reports `comp + SuspensionExtent` (0 = fully extended).

## Tyre (second call)

If the wheel is not grounded: set `slipping = false`, `slide = 0`, both deflections `dL = dT = 0`,
both tyre forces 0 and `roadTorque = 0`; skip to "Rotation".

Otherwise, with the spin `ω` just set by the group:

### Implicit suspension damping

If `Ks > 0`: add an implicit contact (`vehicle-body.md`) at the contact point `x` with normal `N`,
stiffness `Ks` (its matrix `M` as given there) and force `f.c = ((k·rate)·dt)·N.c` if `comp < l`,
else `f = 0`.

### Slips

```
roll = ω·radius
latSlip  = 0 if vLat == 0 ;  sign(vLat) if |vFwd| < |vLat| ;  else vLat / |vFwd|
sV       = roll + vFwd                                   (longitudinal slip speed)
longSlip = 0 if sV == 0 ;    sign(sV) if |vFwd| < |sV| ;   else sV / |vFwd|
gL = (sign(sV)·Fz) / Klong        gT = (sign(vLat)·Fz) / Klat     (deflection at full grip per unit μ)
stepT = dt·vLat        stepL = dt·sV
muS   = StaticFric·matFric
```

### Friction curve

`curve(slip)` with `s = |slip|`, `opt = OptimumSlipPercent`, `muS` as above,
`muK = SlidingFric·matFric`:

```
fpar = (opt + opt − s)·invOpt2·muS·s
if s <= opt:        return (fpar, (s·0.5)/opt)
if muK < fpar:      return (fpar, ((muS − fpar) + (fpar − muK)·0.5)/(muS − muK))
return (muK, 1)
```

The first value is the friction coefficient, the second a 0…1 "how much it slides" value used for
effects (skid marks, smoke) and the depth logic.

### Friction coefficients

Longitudinal: start with `muL = muS`, `tL = muS·gL`, `visL = 0`. Use the curve
(`(muL, visL) = curve(longSlip)`, then `tL = muL·gL`) when

- `|longSlip| <= opt`, or
- `stepL <= 0` and (`dL <= tL` or `stepL < tL − dL`), or
- `stepL > 0` and (`tL <= dL` or `tL − dL < stepL`).

Otherwise keep the static values (the tyre is still winding up its deflection).

Lateral: the same with `muT`, `tT = muS·gT`, `visT`, `latSlip`, `stepT`, `dT`.

Combined: `mx = |longSlip|`, `longDom = true`; if `latSlip > mx` then `mx = latSlip`,
`longDom = false`; else if `mx < −latSlip` then `mx = −latSlip`, `longDom = false`.
`muMax = muS`; if `opt <= mx`: if `longDom` then `muMax = muL` and, if `muL < muT`,
`tT = muL·gT`; else `muMax = muT` and, if `muT < muL`, `tL = muT·gL`.

### Deflections

`relax = |roll|·dt·0.1`. Longitudinal (the lateral one is identical with `stepT`, `tT`, `dT`):

```
nd = stepL + dL ;  effL = stepL
if stepL < 0:
    if tL > nd:  cand = relax + dL ;  dL = tL if tL < cand else cand ;  stickL = false ;  effL = 0
    else:        dL = nd ;  stickL = true
elif tL < nd:    cand = dL − relax ;  dL = tL if cand < tL else cand ;  stickL = false ;  effL = 0
else:            dL = nd ;  stickL = true
```

### Forces

```
FT = −(dT·Klat) − effT·invdt·Dlat
FL = −(Klong·dL) − effL·invdt·Dlong
f2  = FT·FT + FL·FL
cap = (muMax·Fz)·(muMax·Fz)
if cap < f2:
    if (not stickL and opt < |longSlip|) or (not stickT and opt < |latSlip|):
        q = −sqrt(cap / (vLat·vLat + sV·sV)) ;  FT = q·vLat ;  FL = q·sV
    else:
        q = sqrt(cap / f2) ;  FT = q·FT ;  FL = q·FL
slide = (stickT ? 0 : visT) if stickL ;  else (stickT ? visL : max(visL, visT))   (max: visL if visT <= visL)
slipping = slide > 0.5
roadTorque = FL·radius
dragT = −(|vLat|·TireDragCoefLat·Fz·vLat·matDrag)
dragL = −((depth + 1)·−(|vFwd|·TireDragCoefLong·Fz·matDrag·vFwd))
force.c = (((Fsusp·N.c + FT·Lat.c) + (−FL)·Back.c) + dragT·Lat.c) + dragL·Back.c     (each component)
```

Add `force` at the contact point `x` to the body (`vehicle-body.md`, "Forces at a point"): one
force-and-torque add per wheel, force first. (The sum order above is the machine code's; it starts
with the suspension force.)
`FL` is the force along −Back (forward) that the road puts on the tyre; `roadTorque` is what the
group's spin solver feels back from the road next step.

### Rotation

`rotAngle = dt·ω + rotAngle` (for drawing; the drawn wheel also moves up by
`comp − visualDisp`, `vehicle-helpers.md`).

## Wheel state

| Name | Reset value | Meaning |
|---|---|---|
| `comp`, `rate`, `Fsusp`, `Fz`, `Ks`, `bottomed` | 0 | suspension |
| `steerAngle`, `brakeTorque` | 0 | inputs |
| `grounded` | false | |
| `x`, `N`, `Lat`, `Back` | — | contact point and frame |
| `vLat`, `vFwd`, `vN` | 0 | contact velocities |
| `matFric` | 1 | |
| `matDrag`, `depth`, `bump`, `φ` | 0 | surface |
| `dL`, `dT` | 0 | tyre deflections |
| `FL`, `FT`, `roadTorque` | 0 | tyre forces |
| `latSlip`, `longSlip`, `slide` | 0 | |
| `slipping` | false | "majorly slipping" |
| `ω` | 0 | spin, set by the group |
| `rotAngle` | 0 | drawing only |
