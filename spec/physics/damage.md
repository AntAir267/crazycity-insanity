# Vehicle damage

How much damage a car collects from its collisions, and what that number drives. Every car with a
car simulation has a damage value: the player's car, opponents and police cars (and network cars).
Ambient traffic cars have none (`car-collision.md`, 6).

Damage has **no effect on the physics** of the car itself: it does not change handling, mass or
grip. It drives the dash gauge, the "wrecked" state (which takes the controls away), the parts
thrown off ("Parts thrown off" below) and visual effects (smoke, dents, marks:
`../formats/damage-visuals.md`, `../engine/effects.md`). The one indirect effect on the physics:
a part that breaks off becomes a loose body in the physics world (`loose-bodies.md`), which the
car and other bodies can then hit, and its launch takes numbers from the game-wide random
generator.

Single precision, operations in the order written (`vehicle.md`, "Precision").

## Tune file

`tune/vehicle/<car>.vehCarDamage` (same text syntax as the other tune files, `vehicle-tune.md`;
root name not checked). The keys that matter here:

| Key | Default | Meaning |
|---|---|---|
| `MaxDamage` | 1000000 | the damage limit: above it the car is wrecked |
| `MedDamage` | 500000 | where engine smoke starts (visual) |
| `ImpactThreshold` | 100 | an impact must give more than this to count |
| `RegenerateRate` | 0 | damage removed per second |

All other keys (`SmokeOffset`, `TextelDamageRadius`, the particle keys `Position` … `Color`,
`SmokeOffset2`, `DoublePivot`, `MirrorPivot`) are visual. Shipped values: e.g. `vpbug` 321300 /
150000 / 1500 / 0, `vpcop` 456300 / 389375 / 1500 / 0. Which car's file is used follows the
car's model name; the player's `vpcop` keeps `vpcop.vehCarDamage` (`vehicle-tune.md`).

## State

Per car: `damage` (0 at reset and at every "clear damage"), an **enabled** flag, and 12 **impact
slots**. A slot holds: the other side's identity (`collision-object-contacts.md`, "Push source and
the previous matrix"; empty if none), `peak`, `total` and `timer` (and, for visuals only, where and
how it was hit). A reset or "clear damage" empties every slot.

Enabled: always for computer cars; for the player's car unless the game's no-damage setting is on
(a game option outside this spec).

## Per physics sample, before the collisions

As the last part of the car's own update in the sample (after `vehicle.md` step 11):

1. `damage` = `damage` − dt·`RegenerateRate`; if the result is < 0, `damage` = 0.
2. For each slot in use: `timer` = `timer` − dt; if the result is < 0 the slot is emptied.

3. Once `damage` ≥ `MaxDamage`: the wreck throw ("Parts thrown off", "Wrecked" below).

(The visual effects are updated in this step too, `../engine/effects.md` and
`../formats/damage-visuals.md` give their order; they use the effects generator. The wreck throw
draws from the **game-wide** generator, `collision.md`, "Random numbers", in the engine and in
check runs alike.)

## Per impact report

Every impact the collision response applies to the car (`vehicle-body.md`, "Contact interface":
the impact report; against the city, a placed object, a breakable prop, another car) is, after
waking the unstuck helper, offered to the damage — if the car's damage is enabled:

1. The other side: the side of the impact that is not this car. Its identity names the slot; the
   city has one identity and all fixed objects tested without a body share one
   (`car-collision.md`, 3.3), so all walls of the city share a slot and all landmarks and spanning
   objects share another, while every moving body has its own.
2. Mass ratio: q = 1, unless the other side has a rigid body: then q = m_o/(m_s + m_o) with m_o
   the other body's mass and m_s this car's mass (the sum m_s + m_o rounded, then the division).
3. Impact strength: with J the impulse applied to this car for this impact (already multiplied by
   the impact weight 1/N), J2 = (J.x·J.x + J.y·J.y) + J.z·J.z,
   s = (√J2 · 1.0) · q (the 1.0 is a damage modifier that is always 1).
4. Find the slot of the other side's identity.
   - **No slot yet**: take the first empty slot (none free: the impact is ignored) and set
     identity, `peak` = s, `total` = s, `timer` = 0.2; then **count** the impact (below).
   - **Slot found**, if 1.25·`peak` < s: `total` = s + `total`, `peak` = s, `timer` = 0.2; then
     **count** the impact.
   - **Slot found**, otherwise: if s ≤ `ImpactThreshold`, stop. If this car's `speedMPH`
     (`vehicle.md` step 2 of this sample) < 10 and the other side has no rigid body, stop. Else
     `total` = s + `total` and `damage` is increased by s (as below), without the visuals.
5. **Count** (for a new or a stronger impact): if `ImpactThreshold` < s and (10 ≤ `speedMPH` or the
   other side has a rigid body): `damage` = s + `damage` (if that is < 0, 0), the visual damage
   effects start (sparks if `speedMPH` > 15, marks, chips: `../formats/damage-visuals.md`), and
   if s ≥ 10000 one body part is thrown off ("Parts thrown off"). An impact sound plays when
   s > 0.001 (`../audio/tyre-impact-sounds.md`, "Impact sounds"). A counted impact of the
   player's car also triggers its force-feedback jolt (`../engine/controls.md`, "Impact jolts").

So against fixed things a car must be going at least 10 mph for an impact to count, while impacts
with moving bodies always count; an impact above the threshold counts once per contact unless it
is 25 % stronger than the strongest one of the last 0.2 s with the same side, in which case it
counts again — and impacts of a lasting contact that are not stronger still count as long as each
is above the threshold.

## What damage drives

- **Wrecked**: `damage` > `MaxDamage` (strictly).
  - The player's car, while damage is enabled and it is wrecked: the controls give throttle 0,
    brake 0, steering 0 every frame (`../engine/controls.md`, "Player overrides"). Race modes have
    their own rules (`../game/modes.md`, `../game/crash-course.md`). **Free roam (cruise)**: the game checks once per frame, after
    the controls and before the physics; when the player's car is wrecked it starts a 3.0 s timer
    and makes the car **not drivable**: every frame, at the start of the physics, the car's brake
    is set to 1 and the gearbox to neutral (on top of the zeroed controls), and while not drivable
    the car's unstuck helper and water effect are not updated. Each frame the timer is reduced by
    the frame time; when it is no longer above 0 the damage is cleared (`damage` = 0, every slot
    emptied, the visual damage reset) and the car is drivable again: if the gearbox is in neutral
    or reverse it is put into first gear. The car is not moved or reset otherwise; it drives on
    from where it stopped. (The timer runs while the game is not paused.)
  - Opponents and police: while wrecked, the computer driver gives throttle 0, brake 0, steering 0
    every frame (and stops driving its route); a car with the route option for recovery clears its
    damage again after 5 s of real time. What a wrecked police car means for the chase is the
    police spec's.
- **Dash**: the damage needle shows `damage` against `MaxDamage` (`../formats/dash.md`).
- **Visuals** (later): smoke from `MedDamage` on, dents and texture damage from counted impacts,
  parts thrown off (below), wobbling drawn wheels. None affects the car's own physics.

## Parts thrown off

Which parts come off and when. A thrown part becomes a loose body (`loose-bodies.md`, 4); it
no longer belongs to the car, and the car's own simulation does not change.

**The car's loose parts.** At load MM2 builds two lists, each part only if it has an H mesh
(after the fill rule, `../formats/pkg.md`):

- **body parts**: `BREAK0`, `BREAK1`, `BREAK2`, `BREAK3`, `BREAK01`, `BREAK12`, `BREAK23`,
  `BREAK03`, then `VARIANT<c>` for the car's paint set *c* (`../formats/car-model.md`);
- **running gear**: `WHL0`, `HUB0`, `WHL1`, `HUB1`, `WHL2`, `HUB2`, `WHL3`, `HUB3`, `FNDR0`,
  `FNDR1`, `ENGINE`.

Each part starts attached, with its place p = the origin of its `<car>_<PART>.mtx` (car space).
Every part of a shipped car has its `.mtx` (no shipped car has `HUB`, `ENGINE` or, except
`vpmoonrover` and `vppanoz`, `FNDR` meshes, so those parts do not exist). Without the file MM2
leaves p unset (in practice the place read for the last earlier part of the same list, whose
buffer is reused; undefined for a list's first part); *engine choice*: p = (0, 0, 0).
A thrown part is no longer drawn with the car (fenders, wheels, hubs and the engine check this
each frame); it becomes a loose body (below). A reset or "clear damage" (also the cruise
recovery) puts every part back, removes the parts' loose bodies and moved props
(`loose-bodies.md`, 7) and re-arms the wreck throw.

**One body part per strong impact.** In the **count** step of an impact report (above), if
s ≥ 10000: take the attached body part whose p is nearest the slot's contact point, by
d2 = ((dz·dz + dy·dy) + dx·dx), d = p − contact per component, starting from a best of 100000 and
taking a part only if its d2 is strictly smaller (so the first of equal parts wins; a part 316 m
or more away is never taken), and throw it (below). No attached body part: nothing. The slot's
contact point is the contact position of the impact that **opened** the slot, turned into car
space with the car's world matrix of that moment; a stronger impact that counts again on the same
slot keeps that old point. With that matrix's rows X, Y, Z and origin o and the contact position P,
MM2 first forms the inverse's translation t.k = −((R.x·o.x + R.y·o.y) + R.z·o.z) for R = X, Y, Z
(k = x, y, z), then the point's component k = ((R.z·P.z + R.y·P.y) + R.x·P.x) + t.k (not P − o
turned).

**Wrecked.** In the per-sample damage step, after the slot timers: if `damage` ≥ `MaxDamage`
and this has not happened since the last reset or clearing, it happens now, once, by this
car's `speedMPH`:

1. The launch speed centre of the running-gear list becomes 1.3 × the car's speed (m/s,
   `vehicle.md` step 2), single precision.
2. `speedMPH` > 100: throw every attached running-gear part, in list order.
3. Else > 75: draw k = (15-bit random value) & 3 and throw wheel k, then hub k; again for a
   second k (it may be the same pair: then nothing more is thrown); then draw f = value & 1 and
   throw `FNDR<f>`.
4. Else > 50: one pair as in 3.
5. Else nothing is thrown (but it still counts as done).

(The 15-bit value is the generator's raw value before scaling, `collision.md`.) A part that
does not exist or is already thrown is skipped without any draw.

**Throwing a part.** The part becomes a loose body (`loose-bodies.md`, 4: a moved-prop slot in
the car's room with a rigid body), drawn with the part's meshes and the car's paint set,
its physical data from the prop data named `<car>_<PART>` (`tune/banger/`, e.g.
`vpbug_WHL0.dgBangerData`: mass 92.2; every shipped menu car has a data file for each of its
parts, only the unlisted `vplafrance` has none; without one MM2 gives the part data number −1,
which makes the thrown part read a data entry beyond its table: undefined; *engine choice*: the
default prop data). Its
placement is the part's place p followed by the car's world matrix (no rotation of its own).
Then eight random numbers r1 … r8 in [0, 1), in this order:

```
dir  = (2·r2 − 1, r1, 2·r3 − 1)          L2 = (dir.z·dir.z + dir.x·dir.x) + dir.y·dir.y
n    = 0 if L2 = 0, else 1/√L2           dir = dir·n          (per component)
s    = ((hi − lo)·r4) + lo               hi = A + B, lo = A − B
P    = dir·s                             (the prop's linear momentum is SET to this)
axis = (2·r6 − 1, r5, 2·r7 − 1)          L2 = (axis.z·axis.z + axis.y·axis.y) + axis.x·axis.x
n    = 0 if L2 = 0, else 1/√L2           axis = axis·n
k    = ((hi2 − lo2)·r8) + lo2            hi2 = C + D, lo2 = C − D
H    = H + axis·k                        (added to the prop's angular impulse accumulator)
```

with A = 4, B = 1 for body parts and A = 1.3 × speed, B = 1 for the running gear (set by the
wreck throw, the only time those parts are thrown), C = 2, D = 1. P and H are the rigid-body
momentum and angular impulse of `vehicle-body.md`, so the part leaves with velocity dir·s / m: a
body part of mass m at 3 to 5 / m m/s, a 92 kg wheel thrown at 100 mph (A ≈ 58) at about
0.6 m/s. The direction always points up or level (dir.y ≥ 0). Like any loose body it then
falls, collides and comes to rest (`loose-bodies.md`).

**Random numbers.** r1 … r8, and the wreck throw's choices k and f, come from the **game-wide**
generator (`collision.md`, "Random numbers"), the one the wheel bumps use, not from the effects
state: in the engine and in check runs alike, so a thrown part shifts the bump sequence that
follows. The eight draws are taken even when the part's body cannot become a mover (the car's
room is 0, e.g. on the flat world of check runs).

## Order within a physics sample

Car update (vehicle steps 1–11, its room) → damage regeneration, slot timers, the wreck throw →
the other movers' updates → collisions (each impact: response, then the impact report: unstuck
wake, then damage as above, with a strong counted impact's body-part throw) → end of sample. A
part thrown during a sample becomes a mover only at the end of that sample (`loose-bodies.md`,
5.3).

## Things that look like mistakes (keep them)

- Damage counts only impacts whose impulse is large; resting contacts with tiny impulses never
  count, but a long scrape along a wall at speed counts every sample whose impulse is above the
  threshold.
- All city walls share one slot, and all fixed objects share another.
- The 0.2 s slot timer is refreshed only by a stronger impact, not by repeated ones.
- Thrown parts get their launch as momentum, not velocity, so heavy parts barely move; a stronger
  repeat impact throws the part nearest the slot's first contact point; the wreck throw happens
  at `damage` ≥ `MaxDamage` although "wrecked" needs `damage` > `MaxDamage`.
