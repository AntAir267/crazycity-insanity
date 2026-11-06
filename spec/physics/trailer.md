# Trailers

Some trucks tow a trailer: a second rigid body with its own wheels, held to the truck by a hitch
joint. This page says which cars get one, how it is set up from its files, how it moves each
physics step, how the joint couples it to the truck (exactly), when it comes off, how it collides,
what happens at a reset, what computer-driven trucks do, and how it is drawn.

Companion pages: `vehicle.md` (the step, units, precision) and its pages, which the trailer reuses
for its body (`vehicle-body.md`), its wheels (`vehicle-wheels.md`) and its wheel trains
(`vehicle-powertrain.md`, "Drive trains"); `collision.md`, `car-collision.md` and
`collision-objects.md` for its collisions; `../formats/car-model.md` ("Trailers") for its parts.

Words used here: the **truck** is the towing car, body **A**; the **trailer** is body **B**.
Single precision throughout, every operation rounded to float32 and evaluated as written
(`vehicle.md`, "Precision"); `(a + b) + c` grouping is written out wherever there are three terms.
Constants are float32 values: 0.5, 3.0, 10.0, 10000.0, 0.33333334, 0.041666668, 12.732395 (40/π)
and 3.1415927. Comparisons with `1e-05` and `1e-12` are made against those double values; the
others against float constants.

## 1. Which cars tow a trailer

A car gets a trailer exactly when `../formats/car-model.md` ("Trailers") says so: its package has a
`TRAILER_HITCH` mesh and the origin in `<car>_TRAILER_HITCH.mtx` is not (0, 0, 0). The trailer's
package `geometry/<car>_trailer.pkg` must then exist (MM2 needs its `TRAILER`, `TWHL0` and `TWHL1`
parts). Shipped: `vpsemi`, and the unlisted `vpftruck` and `vplafrance`; `vpcentury` has none.

- **The player's car**: always, except in the multiplayer games named in `car-model.md`.
- **Computer cars** (opponents, police) of such a model: the trailer is created but never moves
  (section 10).
- **Network cars** (multiplayer): full trailers, simulated like the player's (section 10).
- Ambient traffic has no trailers.

## 2. Files and set-up

### 2.1 Files

| File | Use |
|---|---|
| `geometry/<car>_trailer.pkg` | parts `TRAILER` (body), `SHADOW`, `TLIGHT`, `TWHL0`–`TWHL3`, `TRAILER_HITCH` (`car-model.md`) |
| `geometry/<car>_TRAILER_HITCH.mtx` | the truck's hitch point (origin, floats 9–11), in truck model space |
| `geometry/<car>_trailer_TRAILER_HITCH.mtx` | the trailer's hitch point, in trailer space (read only if the trailer has a `TRAILER_HITCH` mesh; else (0, 0, 0)) |
| `geometry/<car>_trailer_TWHL<i>.mtx`, i = 0…3 | wheel pivots, radii and widths, as for car wheels (`vehicle-wheels.md`, "Geometry") |
| `tune/vehicle/<car>.vehTrailer` | body, wheels, wheel trains, hitch overrides (2.2) |
| `tune/vehicle/<car>.dgTrailerJoint` | the joint (2.3) |
| `bound/<car>_trailer_BOUND.bnd` | the trailer's collision shape (2.7) |

Both tune files use the syntax of `vehicle-tune.md`, "Syntax" (case-insensitive file lookup,
case-sensitive keys, unknown keys and blocks skipped, last value wins). Never read: the copies in
the `tune/` folder itself (`tune/vpsemi.vehTrailer`, `tune/vpcentury.vehTrailer`,
`tune/vpcentury.dgtrailerjoint`) and the `Joint { … }` block inside them, the `Offset0`,
`Offset1`, `JointPos` keys (they are not keys of either file), `*_twhl0NEW.mtx`, and the trailer
`.dgBangerData` files (no trailer part ever breaks off).

### 2.2 Keys of `.vehTrailer`

| Key | Type | Default | Use |
|---|---|---|---|
| `Mass` | float | 3000 | body mass |
| `InertiaBox` | vector | 3 4 9 | box for the inertia (`vehicle-body.md`, "Mass and inertia") |
| `CarHitchOffset` | vector | the truck's hitch `.mtx` origin | hitch point on the truck, relative to the truck's **centre of mass** (2.5) |
| `TrailerHitchOffset` | vector | the trailer's hitch `.mtx` origin | hitch point on the trailer, relative to the trailer's origin |
| `WheelFront { … }` | block | wheel defaults of `vehicle-tune.md` | read into wheel 0 |
| `WheelBack { … }` | block | same | read into wheel 2 |
| `Drivetrain { … }` | block | `AngInertia` 5000, `BrakeDynamicCoef` 1, `BrakeStaticCoef` 1.2 | read into train 0 |

The wheel and train keys are those of `vehicle-tune.md`. A file that sets the two hitch keys
overrides the `.mtx` values (shipped `vpsemi.vehTrailer` does: `CarHitchOffset`
(−0.074035, 0.406, 2.248502), `TrailerHitchOffset` (−0.066, 0.576, −5.861418), while the `.mtx`
files have y = 1.5755 and 1.576).

### 2.3 Keys of `.dgTrailerJoint`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `ForceLimit` | float | 0 | the joint breaks when its force exceeds `ForceLimit`·10000 N (0: never) |
| `JointStatus` | int | 2 | bit 1 (value 1): broken; bit 2 (value 2): lean and roll torques and limits are on |
| `RestoreForceLean` | float | 2 | lean spring |
| `DampConstLean` | float | 2 | lean friction (a constant-size torque) |
| `DampLinearLean` | float | 0.9 | read, never applied (5.4) |
| `RestoreForceRoll` | float | 2 | roll spring (5.4; practically never acts) |
| `DampConstRoll` | float | 0.1 | roll friction |
| `DampLinearRoll` | float | 2 | roll damping |
| `LeanLimit` | float | 3.1415927 | lean angle (rad) where the hard lean limit starts (5.5) |
| `LimitElasticityLean` | float | 1 | bounce of the lean limit |
| `LimitElasticityRoll` | float | 0 | bounce of the roll limit |
| `FreeRange` | float | 0.15 | how far the two hitch points may drift apart (m) before they are pulled together (5.8) |
| `FreeLean` | float | 0.1 | lean angle (rad) below which no lean torque acts |
| `FreeRoll` | float | 0.1 | roll angle below which no roll torque acts |

Not in the file, fixed: the roll limits −0.3 and +0.3 rad. Derived when the file is read:
`cosFreeLean` = cos(`FreeLean`), computed in double precision and rounded to float. The value of
`JointStatus` is taken as written: a file with 0 switches the lean torques and limits off; a file
with an odd value makes the trailer start detached until the first reset (which always attaches
it, section 3). Shipped `vpsemi.dgTrailerJoint`: `ForceLimit` 40, `JointStatus` 2, lean 2 / 2 /
0.9, roll 2 / 0.1 / 2, `LeanLimit` 3, both elasticities 0, `FreeRange` 0.14, `FreeLean` 0.1,
`FreeRoll` 0.1. `vpftruck` and `vplafrance` have neither tune file and use the defaults.

### 2.4 Set-up order

1. The truck is set up first (`vehicle-tune.md`, "Order of set-up").
2. Defaults; then `.vehTrailer`, then `.dgTrailerJoint`. Call the pair **loaded** if both files
   exist (they open; what they contain, even a file the reader cannot use, does not matter).
3. Trailer offset `o` = `CarHitchOffset` − `TrailerHitchOffset`, per component.
4. Body: mass and inertia from `Mass` and `InertiaBox` (`vehicle-body.md`, "Mass and inertia"),
   speed limit 500 m/s, angular speed limit **5 rad/s** per body axis (not the cars' 4π).
5. Wheels 0…3, in order: pivot, radius and width from `<car>_trailer_TWHL<i>.mtx`; then the wheel's
   static load and constants (2.5). If a wheel's `.mtx` is missing, MM2 leaves the values of the
   previous wheel in place (it reads into a buffer that still holds them); reproduce that
   (`vpftruck`, `vplafrance`: wheels 2 and 3 become copies of wheel 1).
6. If loaded: trains 1, 2 and 3 copy train 0's three values; wheel 1 copies wheel 0 and wheel 3
   copies wheel 2, every key except `HandbrakeCoef` and `WobbleLimit` (as `vehicle-tune.md`, "How
   the values reach the wheels"), and each copied wheel's constants are computed again (2.5).
7. The joint is set up (state of 5.1; reset in section 3).
8. The hitch load on the truck (2.6).

### 2.5 Body frame, wheels and trains

The trailer has no centre-of-gravity offset: its rigid-body position **is** its model origin, and
its frame (rows of the body rotation, origin = body position) is what its wheels hang from, what
its bound is placed with and what is drawn. The truck's hitch point, on the other hand, is taken
relative to the truck's **centre of mass** (`vehicle.md`, "Centre of mass"), not its model origin:
with `vpsemi`'s `CenterOfGravity` (0, −0.1, 0.15) the hitch the physics uses sits 0.1 m higher and
0.15 m further forward than the model's numbers say. Keep this.

The four wheels are ordinary wheels (`vehicle-wheels.md`: frame, ground query, suspension, tyre,
implicit damping, bottoming) with these differences:

- The wheel frame is built from the trailer's frame.
- Static load: `L0 = (((−(m·g))·a)·0.5) / (a + a)`, with m = the trailer's mass, g = −19.6 and
  a = |pivot.z| (a quarter of the trailer's weight; NaN for a pivot with z = 0). The other
  constants follow from `L0` as for car wheels ("Static load and derived constants"), including
  the `SuspensionFactor` floor.
- No `CarFrictionHandling` step in "Surface" (the material friction is only scaled by the
  weather).
- The ground probe ignores nothing (cars and trailers are never probed anyway).
- The wheel bottoming impulse uses the trailer body's own inverse mass (`vehicle-body.md`, "Inverse
  mass along a direction"), not the joint's (section 6).
- Only wheels 2 and 3 get inputs (4.1); wheels 0 and 1 keep steering angle 0 and brake torque 0.

Each wheel sits alone on its own wheel train (train *i* holds wheel *i*): a free train without
engine (`vehicle-powertrain.md`, "Drive trains"), with the values of 2.2/2.4, a train of one wheel
(differential 1, the spin is the train speed). Its compliance term `Kc = Mass·0.005` uses the
**truck's** `Mass` tune value (the trains belong to the truck's simulation), e.g. 17.5 for `vpsemi`.

The trailer has no engine, gearbox, aerodynamics, anti-roll bars, stability or unstuck helpers,
water effect, damage or impact reports.

### 2.6 The hitch load on the truck

Only if the sign bits of wheel 0's and wheel 2's pivot z are equal (both wheels on the same side
of the trailer origin; **not** the case for `vpsemi`, whose wheels 0/1 are at z = −2 and 2/3 at
z = 3.16, so nothing below happens for it):

```
W   = −(g·m)                       zm = (w2.z + w0.z)·0.5          tz = TrailerHitchOffset.z
d   = |tz − zm|
Lax = (|tz|·W) / d
Lw  = ((|zm|·W)·0.25) / d          every trailer wheel: L0 := Lw, constants recomputed
q   = ((CarHitchOffset.z − FL.z)·Lax) / (BL.z − FL.z)       FL, BL = the truck's wheels 0 and 2 pivot z
truck wheels 2 and 3: add q·0.5        truck wheels 0 and 1: add (Lax − q)·0.5
```

"Add x" to a truck wheel: `L = x + L0`; if `L < 1` then `L = 1`; `L0 := L` and the wheel's
constants are recomputed from it ("Static load and derived constants"). The truck keeps these loads
for the rest of the game. (`Lax` is the share of the trailer's weight the axle carries and `Lw` a
quarter of the hitch's share: MM2 has the two levers swapped. Keep it.)

### 2.7 Collision shape, material and radius

The bound is `bound/<car>_trailer_BOUND.bnd`, loaded as an ordinary object bound (`bnd.md`),
placed with the trailer's frame. Its material is the bound's first material, found by name in the
city's material list (`bnd.md`, "Bound materials are shared by name"): the shipped files all name
`default`, i.e. the built-in default (friction 1.0, elasticity 0.5). The trailer does **not** use
the car rule of `collision.md` ("The car's bound"). Its radius for gathering and the mover broad
test is the radius of its `TRAILER` meshes (H, M, L, VL; the rule of `collision-objects.md`,
"Gathering", step 1).

## 3. Reset

At every reset of the truck (race start, world-limit reset, any car reset), right after the truck
itself has been reset and placed:

1. The trailer body is reset (`vehicle-body.md`, "State": everything 0), its rows set to the
   truck's body rows X, Y, Z and its position to the truck's **body position** `c` plus the turned
   offset `o`:
   `x = ((Y.x·o.y + Z.x·o.z) + o.x·X.x) + c.x`, `y = ((Y.y·o.y + X.y·o.x) + Z.y·o.z) + c.y`,
   `z = ((Y.z·o.y + X.z·o.x) + Z.z·o.z) + c.z`.
2. The joint is attached again (bit 1 of the status cleared) and its point `p` is the truck's
   hitch point: `p` = anchor A (5.1) from the truck's body.
3. The four trains and wheels are reset (`vehicle-wheels.md`, "Wheel state"; trains at rest).
4. Its room is found from its position (`city-collision.md`, with its previous room as the hint).

Not reset: the trailer's previous matrix and previous push source (`car-collision.md` 3.3) keep the
values of the last sample before the reset. In MM2 the very first previous matrix is the frame in
which the trailer was created, from the truck's model frame while the truck still stands at its
set-up point (the world origin) before the race places it, so MM2's first city test of the trailer
sweeps from there. **Our choice:** the first previous matrix is the trailer's frame at its first
placement (step 1), with no push source, in the game as in check runs; later resets keep them as
MM2 does. The wheel inputs are set again in the next step.

## 4. The trailer's update

The trailer is the mover declared right after the truck (6.1); its update runs right after the
truck's (`vehicle.md`, "One physics step", steps 1–11 and the damage step), once per physics step:

1. **Inputs** — only while the joint is attached: with the truck's steering input `steer`, brake
   input `brake`, handbrake input `hb` and its speed-sensitive factor at its speed of this step
   (`vehicle-wheels.md`, "Steering and brake inputs"): `st = sss·steer`;
   `f2 = 1 − st` if `st > 0` else 1; `f3 = st + 1` if `st < 0` else 1;
   wheel 2: set(−st, brake, f2·hb); wheel 3: set(−st, brake, f3·hb) — the wheel `set` of
   `vehicle-wheels.md` with the trailer wheel's own `SteeringLimit`, `BrakeCoef`, … So the rear
   trailer wheels brake with the truck's brake and handbrake and steer against the truck's
   steering (`vpsemi`: `SteeringLimit` 0.18). While detached nothing is set: the wheels keep the
   inputs they had at that moment.
2. **Gravity**: `F.y = m·(−19.6) + F.y`.
3. **Room** = Find(body position, room) (before the integration).
4. **Integration** of the body (`vehicle-body.md`, "Integration", both paths, with the 5 rad/s
   limit).
5. **Wheel trains** 0, 1, 2, 3 in this order, each as `vehicle-powertrain.md` "Drive trains"
   (brakes, contact call, spin, tyre call).
6. **Joint update** (section 5), while attached.

## 5. The joint update

### 5.1 State and terms

| Name | Meaning |
|---|---|
| `offA`, `offB` | `CarHitchOffset` (truck body frame) and `TrailerHitchOffset` (trailer frame) |
| `p` | the joint point (world), from the previous update (or the reset) |
| `status` | bit 1 broken, bit 2 lean and limits on (2.3) |

Per body (A truck, B trailer): rows X, Y, Z of R, position, v, w, mass, 1/m, I, 1/I, and its
accumulators F, T (force, torque) and G, H (impulse, angular impulse) as they stand at this moment
(the truck's after its own step, the trailer's after its wheels). From `vehicle-body.md`: the
inverse-mass matrix K_X(P) of a body at a point is `collision.md`'s ("Quantities"); matrix
inverses are `vehicle-body.md`'s `inverse`; `Rot` and the "Matrix product" orders are
`vehicle-body.md`'s.

**Anchors**: anchor A = `R_A·offA + posA`, summed
`((Y.x·o.y + Z.x·o.z) + o.x·X.x) + pos.x`, `((Y.y·o.y + Z.y·o.z) + X.y·o.x) + pos.y`,
`((Y.z·o.y + Z.z·o.z) + X.z·o.x) + pos.z` (o = offA, rows of A);
anchor B = `((Y.x·o.y + Z.x·o.z) + X.x·o.x) + pos.x`, `((X.y·o.x + Y.y·o.y) + Z.y·o.z) + pos.y`,
`((X.z·o.x + Y.z·o.y) + Z.z·o.z) + pos.z` (o = offB, rows of B).

**Order table J.** The joint's 3×3 products `P·Q` sum `P[i][k]·Q[k][j]` over k in this order
(written as `vehicle-body.md` writes its tables, "a, b, c" = `(t_a + t_b) + t_c`); zero elements of
P or Q are multiplied and added like any other:

| | j = 0 | j = 1 | j = 2 |
|---|---|---|---|
| i = 0 | 0, 1, 2 | 1, 2, 0 | 1, 2, 0 |
| i = 1 | 1, 0, 2 | 2, 1, 0 | 2, 1, 0 |
| i = 2 | 0, 2, 1 | 2, 1, 0 | 2, 1, 0 |

`skew(r)` is the matrix with rows (0, −r.z, r.y), (r.z, 0, −r.x), (−r.y, r.x, 0). Mᵀ is the
transpose; "M[i][j] + N[i][j]" sums are element by element.

### 5.2 The steps

0. If `status` has bit 1: nothing. If the keyboard's B key went down this frame while a Ctrl key
   (left or right) is held: set bit 1 and stop (7.2).
1. `K = K_A(p) + K_B(p)`; `M = inverse(K)` (the effective mass of the joint point).
2. `rA = p − posA`, `rB = p − posB`; `SA = skew(rA)`, `SB = skew(rB)`.
3. World inertias with table J: `IwA = (R_Aᵀ·diag(I_A))·R_A`, `IiA = inverse(IwA)`; the same for
   B. (`diag(I)` has the moments on the diagonal and zeros elsewhere.)
4. If `status` has bit 2: the lean and roll torques of 5.4, which add to T_A and T_B and give
   `lean`, `leanRate`, `axis`, `roll`, `rollRate`. Otherwise these are 0, 0, (1, 0, 0), 0, 0.
5. Force and torque sums: `fA.c = invdt·G_A.c + F_A.c`, `tA.c = invdt·H_A.c + T_A.c` (T_A as left
   by step 4), likewise `fB`, `tB`.
6. The trailer's acceleration terms at the joint point (w = w_B, r = rB):
   ```
   u   = (r.z·w.y − r.y·w.z,  r.x·w.z − r.z·w.x,  r.y·w.x − r.x·w.y)          (w × r)
   cB  = (u.z·w.y − u.y·w.z,  u.x·w.z − u.z·w.x,  u.y·w.x − u.x·w.y)          (w × u)
   lnB = fB.c·(1/m_B)
   L.x = (Iw[2][0]·w.z + Iw[0][0]·w.x) + Iw[1][0]·w.y      (Iw = IwB; L.y, L.z likewise with column 1, 2)
   g   = (L.z·w.y − L.y·w.z,  L.x·w.z − L.z·w.x,  L.y·w.x − L.x·w.y)
   τ   = tB − g
   α.c = (τ.z·Ii[2][c] + τ.y·Ii[1][c]) + Ii[0][c]·τ.x        (Ii = IiB)
   sB.c = (α.z·S[2][c] + α.y·S[1][c]) + S[0][c]·α.x          (S = SB; this is α × rB)
   ```
   The truck's terms `cA`, `lnA`, `sA` are the same with A's values, except the angular momentum
   sums: `L.x = (Iw[1][0]·w.y + Iw[0][0]·w.x) + Iw[2][0]·w.z`,
   `L.y = (Iw[0][1]·w.x + Iw[1][1]·w.y) + Iw[2][1]·w.z`, `L.z = (Iw[0][2]·w.x + Iw[1][2]·w.y) + Iw[2][2]·w.z`.
7. Relative acceleration of the joint point (B minus A):
   `a.x = ((((cB.x + lnB.x) + sB.x) − lnA.x) − cA.x) − sA.x`,
   `a.c = ((((lnB.c + cB.c) + sB.c) − lnA.c) − cA.c) − sA.c` for c = y, z.
8. Relative velocity: `vA`, `vB` = the point velocities of A and B at `p` (`collision.md`,
   "Quantities"); `k3 = invdt·0.33333334`;
   `dv = (k3·(vB.x − vA.x), (vB.y − vA.y)·k3, (vB.z − vA.z)·k3)`;
   `h = (dv.x + a.x, a.y + dv.y, a.z + dv.z)`.
9. The joint force on the truck (the trailer gets −f):
   `f.x = (h.y·M[1][0] + h.z·M[2][0]) + M[0][0]·h.x`,
   `f.y = (h.y·M[1][1] + h.z·M[2][1]) + M[0][1]·h.x`,
   `f.z = (h.z·M[2][2] + M[0][2]·h.x) + h.y·M[1][2]`.
   It is the force that makes the relative acceleration of the joint point zero at the next
   integration and removes a third of the relative velocity per step.
10. If `status` has bit 2: the limits of 5.5 (they may change f and add angular impulses).
11. Break test: if `ForceLimit > 0`: with `L = ForceLimit·10000`, if
    `L·L < (f.x·f.x + f.z·f.z) + f.y·f.y`, set bit 1 of `status`. The rest of this update still
    runs; from the next update on the joint is detached (section 7).
12. Torques of the force, with the cross product `r ⊗ f = (f.z·r.y − r.z·f.y, r.z·f.x − r.x·f.z,
    r.x·f.y − f.x·r.y)`: `T_A.c = (rA ⊗ f).c + T_A.c`; with `r = posB − p`,
    `T_B.c = (r ⊗ f).c + T_B.c`.
13. Turn correction of the force (5.7) → f′.
14. `F_A.c = f′.c + F_A.c`; `F_B.c = (−f′.c) + F_B.c`.
15. The joint point and the free range (5.8).

All of steps 4–14 only add to accumulators, so they act at the next integration of both bodies;
step 15 moves both bodies at once.

### 5.3 Notes on the force

- The relative velocity term is `invdt/3` of the velocity difference: a hitch that separates is
  pulled together over about three steps; there is no spring on the position except 5.8.
- Gravity is not in F at this moment (it was added before the integration and used up), which does
  not matter: it accelerates both bodies equally.
- Collision impulses of the previous step are already integrated, so `G` and `H` are usually 0
  here (the truck's unstuck helper can add some, `vehicle-helpers.md`).

### 5.4 Lean and roll torques (step 4)

With `FA` = `E·R_A` and `FB` = `E·R_B` (E the identity, table J; two reference frames MM2 keeps
at the identity; the product equals the rotation up to the sign of a zero) and X, Y, Z the rows
of FA, `bz` = row Z of FB:

```
zx = (X.z·bz.z + X.y·bz.y) + bz.x·X.x            (the trailer's Z axis in the truck's frame)
zy = (Y.z·bz.z + Y.x·bz.x) + Y.y·bz.y
zz = (Z.z·bz.z + Z.x·bz.x) + Z.y·bz.y
τ = (0, 0, 0)
if |zz| < cosFreeLean:                                         (more than FreeLean apart)
    lean = 0 if not (zz < 1);  3.1415927 if not (zz > −1);  else acos(zz)
    if not (lean > 1e−05):  l = (1, 0, 0)
    else:  k = invlen(zy, −zx, 0) ;  l = (k·zy, (−zx)·k, 0·k)
    axis.c = (l.z·Z.c + l.y·Y.c) + l.x·X.c                       (world; = direction of Z_B × Z_A)
    dw = w_B − w_A
    leanRate = −((dw.x·axis.x + dw.y·axis.y) + dw.z·axis.z)
    rollRate = −((dw.y·bz.y + dw.z·bz.z) + dw.x·bz.x)
    pr = rollRate·bz ;  dp = (pr.x + dw.x, dw.y + pr.y, dw.z + pr.z)   (dw without its roll part)
    lA = √((offA.x² + offA.y²) + offA.z²) ;  lB likewise ;  a = lA, or 1 if lA ≤ 0 ;  b = lB, or 1 if lB ≤ 0
    k  = (b·a) / ((A.(1/m)·lB) + (B.(1/m)·lA))
    if not ((dp.x² + dp.z²) + dp.y² > 1e−12):  d = 0
    else:  k′ = invlen(dp) ;  d = (k′·dp.x, dp.y·k′, dp.z·k′)
    coulomb = d·((k·10)·DampConstLean)
    spring  = axis·((RestoreForceLean·lean)·(12.732395·k))
    τ = spring − coulomb
if FreeRoll < |roll| and (RestoreForceRoll ≠ 0 or DampConstRoll ≠ 0 or DampLinearRoll ≠ 0):   (roll = 0 here)
    the roll torque below is added to τ and roll is computed
T_A.c = (−τ.c) + T_A.c ;   T_B.c = τ.c + T_B.c
```

`invlen(v)`: `L = (v.x·v.x + v.y·v.y) + v.z·v.z`; 0 if L = 0, else `1/√L`. `acos(x)`:
`s = √((1 + x)·(1 − x))` in float, then `atan2(s, x)` computed in extended (double) precision and
rounded to float once. Squares `x²` are `x·x`.

So beyond `FreeLean` (0.1 rad, about 6°) the trailer is pulled back in line with the truck by a
spring `RestoreForceLean·lean·12.732395·k` along the axis that turns the trailer's Z axis toward
the truck's, and its relative turning is resisted by a constant-size friction torque
`10·k·DampConstLean` (whatever the turning speed, as long as it is above 10⁻⁶ rad/s). The "lean"
is the full 3-D angle between the two Z axes, so it includes the yaw of a turn and a jack-knife:
for `vpsemi` k = 4764.85, a spring of 60668 N·m per radian and a friction torque of 95297 N·m.
Within 0.1 rad nothing acts. MM2 also forms a linear damping term `−(7.1364965·k·DampLinearLean)·dp`
(7.1364965 = 2√(40/π), set at program start) and then never uses it.

**Roll** (reached only when `FreeRoll` is negative, because the roll angle is tested before it is
computed: it is a value of this update only, 0 at the start of every update, so a roll computed in
one update never feeds the next update's test — shipped data never reaches it): with `q.c = (FB[c][2]·axis.z +
FB[c][1]·axis.y) + FB[c][0]·axis.x` (rows of FB) and
`cc = (l.z·zy − l.y·zz, l.x·zz − l.z·zx, l.y·zx − l.x·zy)`:
`roll = atan2((q.x·cc.x + cc.z·q.z) + cc.y·q.y, (q.x·l.x + q.z·l.z) + q.y·l.y)` (kept unrounded
for the torque, stored rounded); `iz = 1/(B.invI.z + A.invI.z)`;
`sg = 1` if `rollRate > 1e−05`, −1 if `rollRate < −1e−05`, else 0;
`s = (((DampLinearRoll·rollRate)·(iz·7.1364965)) + ((sg·DampConstRoll)·(iz·10))) +
((roll·RestoreForceRoll)·(iz·12.732395))` (the roll times `RestoreForceRoll` rounded once);
`τ = (s·bz.x + τ.x, τ.y + s·bz.y, τ.z + s·bz.z)`. Without the lean block (|zz| ≥ cosFreeLean)
axis is (1, 0, 0), `rollRate` 0 and τ (0, 0, 0), but `l` is left uninitialised by MM2; an
implementation may use l = (1, 0, 0) there (shipped data never gets there).

### 5.5 Limits (step 10)

```
eL = 0 ;  active = false
if not (lean < LeanLimit):
    e = ((lean − LeanLimit)·3) + leanRate ;  if e > 0:  eL = e ;  active = true
eR = 0 ;  call = false
if roll > RollMax:
    e = ((roll − RollMax)·3) + rollRate ;  if e > 0:  eR = e ;  call = true
else if roll < RollMin:
    e = ((roll − RollMin)·3) + rollRate ;  if e < 0:  eR = e ;  call = true
if call or active:  the limit impulse below
```

(RollMax = 0.3, RollMin = −0.3.) Since `roll` is 0 in practice, only the lean limit acts: when the
trailer is folded past `LeanLimit` (3 rad, 172°, for `vpsemi`; π by default, reached only at
exactly 180°) and is still folding further.

The limit impulse:

```
W  = IiA·SA + IiB·SB                       (table J products, summed element by element)
Kq = (W·M)·W  (table J) ;  Kq[i][j] = (Kq[i][j] + IiA[i][j]) + IiB[i][j]
jf = dt·f (per component)
hA.c = ((invdt·H_A.c) + T_A.c)·dt ;  hB likewise          (T as left by step 4)
r = p − posA :  c = (jf.z·r.y − jf.y·r.z,  r.z·jf.x − jf.z·r.x,  jf.y·r.x − r.y·jf.x)
                LA = (c.x + hA.x, hA.y + c.y, hA.z + c.z)
r = p − posB :  c = (r.y·jf.z − r.z·jf.y,  r.z·jf.x − jf.z·r.x,  jf.y·r.x − r.y·jf.x)
                LB = hB − c
ΔA.x = (LA.x·IiA[0][0] + LA.z·IiA[2][0]) + LA.y·IiA[1][0]
ΔA.c = (LA.z·IiA[2][c] + LA.y·IiA[1][c]) + LA.x·IiA[0][c]       (c = y, z) ;  ΔB likewise with LB, IiB
Δ = ΔA − ΔB
imp = (0, 0, 0)
if eL ≠ 0:
    dd = (Δ.z·axis.z + Δ.y·axis.y) + Δ.x·axis.x ;  if dd > 0:  eL = dd + eL
    q.c = (Kq[2][c]·axis.z + Kq[1][c]·axis.y) + Kq[0][c]·axis.x
    den = (q.z·axis.z + q.y·axis.y) + q.x·axis.x ;  x = eL/den if den ≠ 0 else 0
    imp = axis·((LimitElasticityLean + 1)·x)
if eR ≠ 0:
    dr = (bz.z·Δ.z + bz.y·Δ.y) + bz.x·Δ.x
    if (eR > 0 and dr > 0) or (not (eR > 0) and dr < 0):  eR = dr + eR
    q = ((Kq[1][0]·bz.y + Kq[2][0]·bz.z) + bz.x·Kq[0][0],
         (Kq[0][1]·bz.x + Kq[1][1]·bz.y) + Kq[2][1]·bz.z,
         (Kq[0][2]·bz.x + Kq[1][2]·bz.y) + Kq[2][2]·bz.z)
    den = (q.z·bz.z + q.y·bz.y) + q.x·bz.x ;  x = eR/den if den ≠ 0 else 0
    imp = imp + bz·((LimitElasticityRoll + 1)·x)
H_A.c = (−imp.c) + H_A.c ;  H_B.c = imp.c + H_B.c
q = imp·W as  ((W[1][0]·imp.y + W[2][0]·imp.z) + imp.x·W[0][0],
               (W[0][1]·imp.x + W[1][1]·imp.y) + W[2][1]·imp.z,
               (W[0][2]·imp.x + W[1][2]·imp.y) + W[2][2]·imp.z) ;  q = invdt·q
e = q·M with the same sum pattern ;  f = e + f (per component)
```

The angular impulse goes to the bodies' impulse accumulators (it acts at their next integration);
the force is corrected so that the hitch point does not take the impulse.

### 5.6 The break test

Step 11. With `vpsemi`'s 40 the limit is 400 000 N: a hard crash of the truck (or a crash of the
trailer) easily exceeds it, and the trailer comes off (section 7). With the default 0 the joint
never breaks by force.

### 5.7 Turn correction (step 13)

```
hdt = dt·0.5 ;  θ.c = (w_A.c + w_B.c)·hdt ;  t2 = (θ.x·θ.x + θ.z·θ.z) + θ.y·θ.y
if not (t2 > 1e−05):  f′ = f ;  done
sq = √t2 ;  inv = 1/sq ;  ax = θ·inv ;  half = sq·0.5
Mr = K_B(p) · Rot(ax, half)          (vehicle-body.md matrix product and Rot; K_B(p) is step 1's)
dd = (ax.x·f.x + ax.z·f.z) + ax.y·f.y ;  fp = (ax.x·dd, ax.y·dd, dd·ax.z) ;  fo = f − fp
q.c = (Mr[0][c]·fo.x + fo.z·Mr[2][c]) + fo.y·Mr[1][c]
gg = (q.x + fp.x, q.y + fp.y, fp.z + q.z)
s = 1 − t2·0.041666668 if t2 < 1, else s = 2·(sin(half)·inv)    (sine unrounded, the product rounded once)
f′ = (s·gg.x, gg.y·s, gg.z·s)
```

This was meant to turn the part of the force across the average spin axis by half a step's
rotation. MM2 builds the rotation into a matrix it never cleared, which still holds the trailer's
inverse-mass matrix of step 1: the cross part of the force is therefore **multiplied by
K_B(p)·Rot**, which shrinks it by a factor of about 10⁻³. So whenever the two bodies spin together
faster than about 0.38 rad/s (t2 > 10⁻⁵ at 60 steps per second; any real turn or pitch), the joint
transmits almost only the part of its force along the spin axis (for a turn: the vertical part, the
weight on the hitch) and the trailer is pulled round mainly by the free-range correction of 5.8.
The torques of step 12 use the uncorrected f. Keep all of this: it is how MM2's trailers drive.

### 5.8 Joint point and free range (step 15)

```
a = anchor A, b = anchor B (5.1, current bodies)
p = ((b.x + a.x)·0.5, (b.y + a.y)·0.5, (b.z + a.z)·0.5)        (the joint point for the next update)
d = b − a ;  L = (d.x·d.x + d.y·d.y) + d.z·d.z
if not (L > FreeRange·FreeRange):  done
len = √L ;  n = (d.x/len, d.y/len, d.z/len) ;  s = (len − FreeRange)·0.5
posA.c = (s·n.c) + posA.c ;   posB.c = posB.c − s·n.c
```

The two bodies are moved toward each other, each by half of the distance beyond `FreeRange`: a
direct position change, not a push (no `lastPush`, no velocity change). The truck's model frame of
this step (`vehicle.md`, step 5) is not recomputed, so the truck's collisions in this step and its
drawing still use the frame before the move.

## 6. Collisions

### 6.1 In the physics step

- **Mover**: the player's trailer is declared right after the player's car, with all four switches
  U C V O (`car-collision.md` 1.1, row 2). Its centre for the mover broad test (2.1 there) and for
  gathering is its **body position** (no Y row added), its radius the radius of 2.7, its room the
  one of 4 step 3.
- **City** (C): its bound against the city polygons as the car body (`collision.md`, "Body against
  the city"; `collision-contacts.md`), placed with its body frame as it stands at the collisions
  (after this step's free-range move of 5.8; the trailer has no separate model frame), with its
  previous matrix and push source (6.3); impacts with
  A = the city and B = the trailer, the trailer's side material from 2.7.
- **Other movers** (V): every pair test of `car-collision.md` 3 with a later mover; the pair
  **truck–trailer is skipped while the joint is attached** (only that pair: a trailer never skips
  any other mover, and the truck skips only its own trailer). Once detached they are tested like any
  two vehicles: two polygon bounds, the polygonal method, the truck as side A.
- **Gathered objects** (O): props, moved props, landmarks and spanning objects
  (`collision-objects.md`; `loose-bodies.md` 6.3) as for the car, the trailer as side A. A prop it
  breaks follows `collision.md` "Breakable props" with the trailer's inverse mass as K_A.
- **End of the step**: the push is applied (`vehicle-body.md`, "Pushes"), **then** the previous
  matrix is set to the body frame (so it contains the push, as for a physical traffic car,
  `car-collision.md` 3.3 and 6.3 step 6) and the step's push source becomes the previous one.
- The trailer has no impact reports: no damage, no unstuck helper, no "touched the player" mark (it
  is not the player's car), no sound or particles here.

### 6.2 The inverse mass of a jointed body

While the joint is attached, every collision response (`collision.md` "Resolving one impact",
"Breakable props"; `car-collision.md` 3.4) that needs the inverse-mass matrix of the **truck or the
trailer** at a contact point P uses this matrix instead of the body's own K (wheel bottoming does
not, 2.5). For the body S (A or B) at P, with the joint point `p` of the last joint update:

```
KS = K_S(P)                                (the body's own, collision.md)
Mj = inverse(K_A(p) + K_B(p))
rj = p − posS ;  rP = P − posS ;  Sj = skew(rj) ;  SP = skew(rP)
C1 = R_S·SP (table J) ;  row i of C1 times 1/I_i (each element)
C  = Sjᵀ·(R_Sᵀ·C1)  (table J, inner product first) ;  C[i][i] = 1/m_S + C[i][i]
E  = Cᵀ·(Mj·C)      (table J, inner product first)
K  = KS − E  (element by element)
```

It is the inverse mass of the body when the other body hangs on the hitch (it is smaller than
the body's own: a hit on the truck also has to move the trailer). Only the collided body receives
the impulse and the push; the joint transmits the rest through its next update (5.2 step 8).
After the joint breaks, both bodies use their own K again.

### 6.3 Push sources and identities

The trailer has its own identity (`collision-object-contacts.md`, "Push source and the previous
matrix"; `damage.md` slots): a truck colliding with its detached trailer books damage under the
trailer's identity, and the mass ratio of `damage.md` uses the trailer's mass.

## 7. Detaching

### 7.1 Breaking

The joint is detached when its force exceeds the limit (5.6) or when the player presses Ctrl+B
(7.2). While detached:

- the joint update does nothing (no forces, no limits, no free-range correction);
- the trailer's rear wheels keep the inputs they last had (a trailer that came off while braking
  keeps braking);
- both bodies use their own inverse mass in collisions, and the truck and its trailer collide with
  each other (6.1);
- the trailer is still a mover, simulated and drawn, wherever it ends up.

The next reset of the truck (section 3) attaches it again at its place behind the truck.

### 7.2 The Ctrl+B key

MM2 reads the keyboard in the joint update: when the B key went down this frame (it is down now
and was not in the previous frame's keyboard state) and either Ctrl key is held, the joint
detaches. It is a hidden feature; it works in every mode and applies to every trailer joint that
is updated in that frame (also network players' trailers, locally). Our engine binds "detach
trailer" to Ctrl+B by default; with Ctrl held, B only detaches (whatever B does on its own, such as
our wide-view toggle, is not triggered). In a frame with several physics steps the first step's joint update
takes it. Check runs: the `detach` control (`../engine/drive-check.md`).

## 8. Order of operations

Per physics step, in the multi-mover order of `car-collision.md` 1.2, with the truck as mover 1 and
its trailer as mover 2:

1. Truck: `vehicle.md` steps 1–11, then its damage step (`damage.md`).
2. Trailer: section 4 (inputs, gravity, room, integration, trains 0–3, joint update). The joint's
   torques and forces are added to the truck's accumulators after all of the truck's own
   contributions (`vehicle.md`, "Order of the contributions"): lean torque, then (only at the lean
   limit) the angular impulse, then the joint torque, then the joint force; to the trailer's after
   its wheels' in the same order. The free-range correction moves both positions last.
3. Other movers' updates (opponents, police, traffic bodies, loose bodies).
4. Gathering for each mover with C or O (truck, trailer, …).
5. Collisions per mover in order: the truck (city; later movers except its attached trailer;
   gathered objects), the trailer (city; later movers; gathered objects), then the rest.
6. End of the step: the truck's step 13 (`vehicle.md`), the trailer's 6.1 end, then the others.

Once per frame, after the steps: the truck's world limits (`collision.md`, "World limits"; a reset
there resets the trailer too, section 3). The trailer itself has no world limits: a detached
trailer that falls out of the city keeps falling until the truck is reset.

## 9. Things that look like mistakes (keep them)

- The truck's hitch is taken from its centre of mass, not its model origin (2.5).
- `vpsemi`'s trailer has four wheels; the front pair stands under the trailer 2 m behind the
  hitch (radius 0.5, pivots from hand-made `.mtx` files) and carries load like the rear pair.
- The hitch load on the truck is computed with the two lever arms swapped, and for `vpsemi` not at
  all (2.6).
- A missing trailer-wheel `.mtx` repeats the previous wheel (2.4 step 5).
- The trailer's wheel trains use the truck's mass for their compliance (2.5).
- The lean is the 3-D angle between the Z axes, so the "lean" spring straightens a turning
  trailer; it acts only beyond 0.1 rad and with a constant-size friction torque (5.4).
- The linear lean damping is computed and dropped; the roll torque and roll limit are unreachable
  with non-negative `FreeRoll` (5.4, 5.5).
- The turn correction multiplies the cross part of the joint force by the trailer's inverse-mass
  matrix instead of a rotation (5.7).
- The free-range correction moves both bodies by the same amount whatever their masses (5.8), and
  only the joint force's velocity term (a third per step) ties their velocities.
- Collisions see the jointed inverse mass but push only the collided body (6.2).
- The previous matrix of the trailer survives resets (section 3).
- A detached trailer keeps its last wheel inputs (7.1); Ctrl+B detaches every updated trailer.

## 10. Computer trucks, traffic and network trucks

- **Opponents and police** set up with a towing model get their trailer created and placed (and
  re-placed at every reset of the car), and its joint is attached to the car's collisions, but the
  trailer is never declared as a mover: it never updates, never collides and is never collided
  with, and stands where it was placed (drawn there, section 11). The joint is never updated either,
  so its point stays the car's hitch at the last reset. The car's collisions still use the jointed
  inverse mass of 6.2 with that point and the frozen trailer body, which makes the computer truck
  behave as if heavier in impacts. Because the trailer's object is not collidable, other vehicles
  drive through it. (No shipped race uses a towing model as an opponent or police car; mods can.)
  This holds in the game and in check runs (`police`/`ai` lines of a towing model, placed by
  section 3 from that car's starting body).
- **Ambient traffic** never has trailers.
- **Network cars** (multiplayer): their trailers are declared as movers right after their car and
  simulated as in this page.

## 11. Drawing

The trailer is a placed object of its room (it moves to the front of a new room's list whenever
its room changes, `collision-objects.md` "Room lists") and is drawn with its frame (2.5) and the
truck's paint set modulo the trailer's own set count (`car-model.md`, "Paint"):

- at level H: `TRAILER` at H; `TLIGHT` (at H) while the **truck's brake input** is above 0.1 (an
  ordinary mesh drawn with the body, not a glow); the wheels `TWHL0`–`TWHL3` (each only if it has a
  mesh) placed with the wheel's frame of its last contact call (`vehicle-wheels.md`, "Wheel frame
  in the world": the steering turn and the pivot, **without** spin and without suspension travel;
  the wheels do not turn when drawn);
- at M, L, VL: the `TRAILER` mesh of that level only;
- the `SHADOW` part in the shadow pass like a car's (`car-model.md`, "Shadow");
- no reflection pass, no headlight, siren or night glows, no `TRAILER_HITCH`;
- it is not part of the player's car model, so the views and passes that hide the player's car
  keep it: the dash and bumper views, and the rear-view mirror, which hides only the car model
  (`../formats/dash.md`; with `vpsemi`'s mirror 4.0 m up and looking back, the trailer fills much of
  the mirror).

The level is chosen like any placed object's (`inst.md`, "Drawing"), with the trailer's radius.
Sounds of the semi and its trailer belong to the sound milestone (`aud/cardata/shared/semidata.csv`).

## 12. Check runs

`../engine/drive-check.md`, "Trailer runs": a `trailer on` line adds the car's trailer, placed by
section 3 from the car's starting body; a `detach=1` control presses Ctrl+B. The reference
simulator reproduces this page (`tools/sim/trailer.py`, `trailerjoint.py`).

## Checked against

- **Which cars** (`tools/physics/trailers.py` over every shipped `vp*.pkg`, output
  `work/check/trailer_setup.txt`): `vpsemi`, `vpftruck`, `vplafrance` tow; `vpcentury` has a
  trailer package but no `TRAILER_HITCH` mesh. Only `vpsemi` has the two tune files.
- **`vpsemi`**: mass 2000, box (2.5, 3.6, 10.5) → I = (20535, 19416.7, 3201.67); offset
  o = (−0.008035, −0.17, 8.10992); wheels `TWHL0` (−0.85, 0.46, −2) and `TWHL1` (0.841, 0.46, −2),
  radius 0.5, width 0.5; `TWHL2` (0.838391, 0.464909, 3.16375) and `TWHL3` (−0.846689, 0.464909,
  3.16375), radius 0.465207, widths 0.674248 / 0.667316; every wheel L0 = 9800 (front k = 98000,
  rear k = 29697); trains 5000 / 1 / 1.2, compliance 17.5; no hitch load on the truck; lean factor
  k = 4764.85 (|offA| 2.28606, |offB| 5.89002, truck mass 3500); bound 14 points / 14 polygons,
  material `default` (1.0 / 0.5), bound radius 7.30033, `TRAILER` radius 7.77825.
- **`vpftruck`, `vplafrance`** (defaults): mass 3000, box (3, 4, 9); hitches from the `.mtx` files
  (−0.0740347, 1.57554, 2.2485) and (−0.0658834, 1.576, −5.86142); wheels 2 and 3 repeat wheel 1
  (no `TWHL2`/`TWHL3` files); the hitch load applies: each trailer wheel L0 = 5153.04, and the truck's
  wheels gain 205.9 each at the front and 18888.0 each at the rear (`vplafrance`: 9800 → 10005.9
  and 28688); `vpftruck`'s
  own `.vehCarSim` ends up with `Mass` 0.1 by the tune rules (a particle block read at root level),
  which the trailer trains then use.
- **Exact arithmetic**: the joint update was read from the machine code with a symbolic x87
  evaluator along each of its paths (normal; lean limit; no lean/limits with no spin correction and
  no free-range move; the roll part with a negative `FreeRoll`), and the jointed inverse-mass matrix
  likewise. The reference implementation (`tools/sim/trailerjoint.py`) agrees **bit for bit** with
  those formulas on random states: 30 + 30 + 15 + 10 states for the four paths and 40 points for the
  inverse-mass matrix (`work/notes/trailer/check_joint.py`, `check_invmass.py`).
- **Runs** (`work/checks-ref/trailer_*.csv`): at rest the rig settles with the trailer origin
  0.038 m above the ground; full throttle reaches 30.78 m/s in 15 s with the trailer 7.97 m behind;
  the tight turn holds a lean of 0.406 rad; backing up with full lock folds the trailer to
  1.03 rad (59°) in 12 s and tips it (up axis y 0.90); the London wall run breaks the joint at
  6.483 s when the truck hits a wall at 15.6 m/s, after which the trailer runs into the stopped
  truck; the Ctrl+B run detaches at 7.017 s and the braking truck is hit by its own trailer
  (335 steps of truck–trailer contacts).
