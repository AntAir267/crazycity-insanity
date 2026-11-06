# Vehicle rigid body

The car is one rigid body simulated at its centre of mass. Wheels, engine reaction, aerodynamics,
helpers and collisions only add to the body's accumulators; the body integrates once per physics
step, at the start of the step (`vehicle.md`, step 4). This page gives the body's state, its mass
properties, the integration (MM2 switches between an explicit and a semi-implicit scheme), the
position corrections ("pushes"), and the interface that wheels and collisions use.

## State

| Name | Meaning |
|---|---|
| `m`, `1/m` | mass and its inverse (1/m = 3.4028235e38 if m ≤ 0) |
| `I = (Ix, Iy, Iz)`, `1/I` | principal moments about the body X, Y, Z axes and their inverses (same rule) |
| `R` | rotation, rows X, Y, Z (body axes in world space) |
| `p` | position of the centre of mass |
| `v`, `w` | linear velocity, angular velocity (world space) |
| `P`, `L` | linear momentum, angular momentum (world space) |
| `vmax` | speed limit, 500 m/s |
| `wmax` | angular speed limit per body axis, 4π = 12.566371 rad/s for cars |
| `F`, `T` | force and torque accumulators (world) |
| `Fpost`, `Tpost` | post-step force and torque accumulators (implicit contacts add here) |
| `G`, `H` | linear and angular impulse accumulators (collisions add here) |
| `A`, `B`, `C` | 3×3 implicit-contact matrices, and a flag `implicit` |
| `push` | position correction collected during the current step |
| `pushSum` | pushes applied since the last integration |
| `lastPush` | pushes applied during the previous step (used for contact velocities) |
| `turn` | rotation vector collected for the orientation update |

At reset everything is zero except `R` = identity.

## Mass and inertia

From the tune values `Mass` m and `InertiaBox` (bx, by, bz), with k = 0.083333336:

```
Ix = (bz·bz + by·by)·m·k
Iy = (bz·bz + bx·bx)·m·k
Iz = (by·by + bx·bx)·m·k
```

The world inertia tensor (used by the implicit integration) is `Iw = Rᵀ·diag(I)·R`, computed as
`T = Rᵀ·diag(I)` first (element `T[i][j] = I_j·R[j][i]`) and then `Iw = T·R` with the matrix
product of "Products" below.

## Products

Every sum of three products in this page is evaluated in a fixed order that differs from element
to element (it is what MM2's math library does; the orders were read from the machine code, not
from decompiled C, which does not show the association of x87 sums). "Order a, b, c" means
`(term_a + term_b) + term_c`, each sum rounded to float.

**Matrix product** `P = A·B` (row-matrix product): `P[i][j]` sums `A[i][k]·B[k][j]` over k in the
order:

| | j = 0 | j = 1 | j = 2 |
|---|---|---|---|
| i = 0 | 1, 0, 2 | 1, 2, 0 | 1, 2, 0 |
| i = 1 | 1, 2, 0 | 1, 2, 0 | 1, 0, 2 |
| i = 2 | 1, 0, 2 | 1, 0, 2 | 2, 1, 0 |

**Frame product** `P = A·B` of two frames (rows X, Y, Z and an origin o; `vehicle-wheels.md`
uses it for the wheel frame): the rotation rows are the matrix product above; the origin is
`P.o.c = (sum over k of A.o.k·B[k][c]) + B.o.c`, the sum in the order 1, 0, 2 for c = x and
c = y and 2, 1, 0 for c = z, and B's origin added last.

**Product with a transpose** `Q = A·Bᵀ`: `Q[i][j]` sums `A[i][k]·B[j][k]` over k in the order:

| | j = 0 | j = 1 | j = 2 |
|---|---|---|---|
| i = 0 | 1, 0, 2 | 2, 0, 1 | 1, 2, 0 |
| i = 1 | 1, 2, 0 | 0, 2, 1 | 2, 1, 0 |
| i = 2 | 0, 1, 2 | 0, 2, 1 | 2, 1, 0 |

**Row vector times matrix** `y = x ⋆ M` (`y_j` sums `x_k·M[k][j]`): `y.x` order 2, 1, 0;
`y.y` order 2, 0, 1; `y.z` order 2, 0, 1.

**Matrix times column** `y = M x` (`y_i` sums `M[i][k]·x_k`): `y.x` order 2, 1, 0; `y.y` order
2, 0, 1; `y.z` order 2, 0, 1.

**Angular momentum** `L = w ⋆ Iw` (`L_j` sums `w_k·Iw[k][j]`): `L.x` order 1, 2, 0; `L.y` order
0, 1, 2; `L.z` order 0, 1, 2.

Element-wise operations (`M·s`, `M + N`, `a + b`, `a + b·s`) have no order to choose: each element
is one operation (`a + b·s` is `b·s` first, then the add).

## Integration

At step 4 of `vehicle.md`:

1. `F += Fpost`, `T += Tpost`.
2. Integrate with step `h = dt` (below).
3. `Fpost = Tpost = 0`; `lastPush = pushSum`; `pushSum = 0`.

### Explicit path (used when `implicit` is false)

```
P = P + G ;  P = P + F·h            (per component: F·h + P)
v = P · (1/m)
L = L + H ;  L = L + T·h
wb_i = ((R_i.z·L.z + R_i.y·L.y) + R_i.x·L.x) · (1/I_i)     for i = X, Y, Z   (R_i = axis row i)
clamp each wb_i to [−wmax, +wmax]; if any was clamped:
    L = (wb_X·Ix)·R_X ;  L = L + (wb_Y·Iy)·R_Y ;  L = L + (wb_Z·Iz)·R_Z
w = wb_X·R_X ;  w = w + wb_Y·R_Y ;  w = w + wb_Z·R_Z
```

(`a = a + s·b` is evaluated per component as `b·s + a`.) The clamp test is `wb < −wmax` → set to
−wmax, else `wb > wmax` → set to wmax.

### Semi-implicit path (used when `implicit` is true)

The implicit contacts collected during the previous step (`A`, `B`, `C`, see "Implicit contacts")
make the velocity change act against the contact normals implicitly:

```
q   = h · (1/m)
T1  = inverse(identity + q·A)                      (A scaled by q, then 1 added on the diagonal)
Jm  = (B·T1)·Bᵀ          ;  Jm = Jm · (−(q·h))      (matrix product, then product with a transpose)
Jm  = Jm + h·C           ;  Jm = Jm + Iw            (Iw from the current R)
Pe  = G + F·h                                       (per component F·h + G)
r   = Pe · (−q)          ;  r = r ⋆ T1 ;  r = B r   ;  r = r + H ;  r = r + T·h
dw  = solve(Jm, r)
u   = dw · h             ;  u = u ⋆ B ;  u = u + Pe ;  u = u · (1/m) ;  u = u ⋆ T1
v   = v + u ;   P = v · m
w   = w + dw ;  L = w ⋆ Iw
```

The products are those of "Products".

`inverse` of a matrix with elements `a b c / d e f / g h i` (rows):

```
c00 = e·i − h·f ;  c01 = d·i − g·f ;  c02 = h·d − g·e
det = c02·c + (c00·a − c01·b)          if det == 0 the matrix is left unchanged
r = 1/det
row 0: ( r·c00,      −((b·i − h·c)·r),   (b·f − c·e)·r )
row 1: ( −(r·c01),    (a·i − g·c)·r,    −((a·f − d·c)·r) )
row 2: ( r·c02,      −((h·a − g·b)·r),   (e·a − d·b)·r )
```

`solve(Jm, r)` is the same routine as the collision response's: `collision.md`, "Solving x·K = b",
with K = Jm and b = r (cofactors, `det = (C02·J2 + C01·J1) + C00·J0`,
`dw_i = ((C_i2·r.z + C_i1·r.y) + C_i0·r.x)·(1/det)`, the same conditioning test and fallback).
With car inertias the fallback never happens. (The matrix is symmetric, so solving `Jm·dw = r` or
`dw·Jm = r` is the same.)

Note the plus sign in `u = u + Pe` after `u = dw·h ⋆ B`: a fully consistent implicit scheme would
subtract that coupling term. MM2 adds it; reproduce it as written.

The angular speed limit is **not** applied on this path.

On this path `v` and `w` carry the motion from step to step and `P`, `L` are only rebuilt from
them at the end; the incoming `P` and `L` are not read. So a direct change to `P` or `L` made
between steps (the computer driver scales or zeroes them, `../ai/driver.md`) takes effect only if
the next integration takes the explicit path, i.e. no wheel added an implicit contact in the
previous step (`vehicle-wheels.md`: a grounded wheel adds one whenever its `Ks > 0`, which is the
case unless it hangs at full extension or its spring force came out negative). A car standing or
driving on its wheels therefore takes this path and the change is lost; a car in the air keeps it.

### Both paths continue

```
if v·v > vmax·vmax:  k = (1/sqrt(v·v))·vmax ;  v = v·k ;  P = P·k
p = p + push ;  p = p + v·h
turn = turn + w·h
if turn·turn > 1e-15:  a = sqrt(turn·turn) ;  axis = turn·(1/a) ;  R = R·Rot(axis, a)   (matrix product)
F = T = G = H = 0 ;  pushSum = pushSum + push ;  push = 0 ;  turn = 0
implicit = false ;  A = B = C = 0
```

(`v·v` and `turn·turn` are `(x² + y²) + z²`; `1e-15` is compared against that float.)

`Rot(axis, a)`, axis (x, y, z): c = cos a and s = sin a are computed in double precision (MM2 uses
the x87 `fcos`/`fsin` results, which keep more than single precision); **s is rounded to float,
c is not**. t = float(1 − c) (the subtraction on the unrounded c, rounded once). Products are
floats, left to right:

```
row 0: (float(x·x·t + c),   s·z + y·x·t,         z·x·t − s·y)
row 1: (y·x·t − s·z,        float(y·y·t + c),    s·x + z·y·t)
row 2: (s·y + z·x·t,        z·y·t − s·x,         float(z·z·t + c))
```

`x·x·t` is `(x·x)·t` (`y·x·t` is `(y·x)·t`, and so on); on the diagonal that float is added to the
unrounded c and the sum rounded to float once. Rounding c first (or computing t from a rounded c)
loses most of t's precision for the small angles of one step.

`R·Rot` is the row-matrix product: each body axis is turned by angle `a` about the world `axis`.
The orientation is never re-orthonormalised.

## Pushes

A push is a direct position correction (no velocity). Wheels that bottom out and collisions call
**add push** with a vector `d`, which merges it into `push` so that overlapping requests do not add
up twice:

```
s = (push.y·d.y + push.z·d.z) + push.x·d.x
if s < 0:  push = push + d                      (per component d.c + push.c)
else if (e2 = (d.x·d.x + d.y·d.y) + d.z·d.z) > 0:
    k = s / e2 ;  e = (k·d.x − d.x, k·d.y − d.y, k·d.z − d.z)
    if (e.z·d.z + e.y·d.y) + e.x·d.x < 0:  push = push − e
```

At the end of every physics step (`vehicle.md`, step 13): `p = p + push`,
`pushSum = pushSum + push`, `push = 0`.

## Velocity of a point (wheels)

The velocity of a world point `x` on the body, as the wheels use it (collisions use the plain
`v + ω × r` of `collision.md`, "Quantities", without the push correction):

```
r   = x − p
vel = (r.z·w.y − w.z·r.y,  w.z·r.x − r.z·w.x,  w.x·r.y − w.y·r.x) + v
l2  = (lp.z·lp.z + lp.y·lp.y) + lp.x·lp.x                 (lp = lastPush)
if l2 > 0.0001:
    d = ((lp.z·vel.z + lp.y·vel.y) + lp.x·vel.x)·invdt ;  q = (invdt·invdt)·l2 ;  s = invdt
    if −d <= q:  s = −(d/q)·invdt
    vel = vel + lp·s                (per component lp.c·s + vel.c)
```

(After a push, the part of the point's velocity that points against the push is removed.)

## Inverse mass along a direction

For a world point `x` and unit direction `n` (used when a wheel bottoms out): take the
inverse-mass matrix `K` of the body at `x` exactly as `collision.md` defines it ("Quantities"),
then

```
u_j = (K[0][j]·n.x + K[2][j]·n.z) + K[1][j]·n.y        for j = 0, 1, 2
e   = (u_0·n.x + u_2·n.z) + u_1·n.y
```

## Implicit contacts

A contact with world point `x`, a force vector `f` and a symmetric 3×3 stiffness matrix `M`
("add implicit contact") does:

```
implicit = true
Fpost = Fpost + f ;  r = x − p ;  Tpost = Tpost + r × f
A = A + M
S = [[0, −r.z, r.y], [r.z, 0, −r.x], [−r.y, r.x, 0]]     (rows)
SM = S·M (matrix product of "Products") ;  B = B + SM
C = C + (rows: r × row_i(SM))
```

Wheels use `M = Ks·n·nᵀ`, which makes `A`, `B`, `C` the blocks of a damper of strength `Ks`
along `n` at `x`. Exactly: with the vector `a = Ks·n` (`a.c = Ks·n.c`), every element takes the
factor `a` of the smaller index and the component `n` of the larger one, so `M` is exactly
symmetric:

```
M = [ a.x·n.x   a.x·n.y   a.x·n.z ]
    [ a.x·n.y   a.y·n.y   a.y·n.z ]
    [ a.x·n.z   a.y·n.z   a.z·n.z ]
```

(`M[1][0]` is `a.x·n.y`, not `a.y·n.x`; the two differ in the last bit.) `S·M` is the matrix
product of "Products" with `S` on the left; row `i` of the `C` term is the cross product
`r × row_i(SM)` = `(r.y·t.z − r.z·t.y, r.z·t.x − r.x·t.z, r.x·t.y − r.y·t.x)` with `t = row_i(SM)`;
`A`, `B`, `C` and `Fpost` are summed element by element (`element + accumulator`).

## Forces at a point

"Add force `f` at point `x`":

```
F.c = f.c + F.c                                   for c = x, y, z
r   = x − p                                       (per component x.c − p.c)
t   = (r.y·f.z − r.z·f.y,  r.z·f.x − r.x·f.z,  r.x·f.y − r.y·f.x)
T.c = t.c + T.c
```

The implicit contact's `Tpost = Tpost + r × f` uses the same `t`. Torques that MM2 adds as a
vector along body axes (aero, gyro, anti-roll, engine rock) have their own formulas in
`vehicle-helpers.md` and `vehicle-powertrain.md`; every contribution is added to the accumulator
component by component as `contribution + accumulator`. The order of the contributions within a
step is in `vehicle.md` ("One physics step"); since float addition is not associative, it must be
followed exactly.

## Contact interface (for the collision side)

How the car body meets the world is in `collision.md` and its companion pages; this page only
lists what the body provides. Collisions run in step 12 of `vehicle.md` and use only:

- **impulse at a point** `j` at world point `x`: `G = G + j`, `H = H + (x − p) × j` (acts at the
  next integration);
- **add push** as above (applied at step 13);
- every impact the collision side resolves against the car (one "impulse at a point" per impact
  of `collision.md` "Collision response" step 7 or "Breakable props" step 4, also when the
  impulse is zero and there is no push) is an **impact report** to the vehicle; it wakes the
  unstuck helper (`vehicle-helpers.md`);
- reads of `p`, `R`, `v`, `w`, `m`, `I`, the model frame, and `lastPush`.

(Collisions do not add implicit contacts: MM2's persistent contact model is switched off in a
race, `collision.md`.)

For the collision side the car also keeps, per physics step:

- the **push source**: of all collision pushes the car received in the current step (one per
  resolved impact, before the merge rule), the identity of the side that gave the one with the
  largest squared length (compared strictly, so the first of equal ones counts; none if no
  non-zero push). At the end of the step it becomes the *previous source* and the current one is
  cleared. Wheel pushes (bottoming out) are not counted. Identities are defined in
  `collision-object-contacts.md` ("Push source and the previous matrix");
- the **previous matrix**: the model frame as used by this step's collisions, kept at the end of
  the step after the collisions and before the push is applied (at reset: the starting model
  frame). `collision-contacts.md` ("Terms") says how it is used.

The car's collision shape and material are defined in `collision.md` ("The car's bound").
