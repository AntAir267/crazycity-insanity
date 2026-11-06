# Pedestrian reactions to the player

Part of the pedestrian spec (`pedestrians.md` has the terms and the frame order;
`pedestrian-walking.md` the state, the update and the walking rules). Pedestrians react only to the
**nearest player's car**; traffic, police and opponents are ignored (except traffic accidents at a
crossing, `pedestrian-walking.md`). There are three modes: 0 walking (with the slow-car rules of
walking and crossing), 1 **wary** (stare, or run along the sidewalk, or run to a wall) and 2
**dodging** (dive sideways, or run to a wall).

Player values (nearest player): P_c, body matrix rows X, Z, speed, gear, steering, InertiaBox,
R_p, v̂ and inverse speed (`pedestrians.md`, "Terms" and "Frame order"). The car's **travel
axis** F and **right axis** S: if gear = 0 (reverse) F = +Z and S = −X, else F = −Z and S = X.

## Choosing the mode

Step 3 of the update (`pedestrian-walking.md`, "One update"), with g = the current sequence:

1. If g is `BACKUP` (backed against a wall): with e = *p* − P_c (3D), if
   (e.z·v̂.z + e.y·v̂.y) + e.x·v̂.x < 0 (the car is moving away) and *q* > (2R_p)² (computed as
   (R_p + R_p) squared): mode = 0, *o* = sin(rnd() × 6.2831) × 1.5 (one draw), curve(*k*, *o*).
   Either way the mode choice ends here. A car standing exactly still has v̂ = (0, 0, 0), so the
   pedestrian stays at the wall while it stands; any movement makes v̂ a unit vector again.
2. Else if g is one of `ANTIC_LDIVE`, `ANTIC_RDIVE`, `WALK_LDIVE`, `WALK_RDIVE`, `LDIVE_GROUNDL`,
   `RDIVE_GROUNDR`, `GROUND_STANDL`, `GROUND_STANDR`, or g is `RUN` and mode equals the wall flag
   *w* (as numbers: mode 0 with *w* = 0, or mode 1 with *w* = 1): keep the mode. (MM2 also lists
   `LDIVE` and `RDIVE`, which no table has.)
3. Else, if *q* < 1225 (within 35 m):
   - **Collision course**: e = *p* − P_c (3D); ahead = (F.z·e.z + F.y·e.y) + F.x·e.x; if speed > 0
     and InertiaBox.z × 0.25 < ahead < 20.0 and |lat| < InertiaBox.x × 0.5 + 2.0 with
     lat = (S.z·e.z + S.y·e.y) + S.x·e.x (all strict): time τ = (ahead − 2.0) × inverse speed.
     Then: speed < 1.0 → mode 0; τ < 0.75 → mode 2; τ < 2.3 → mode 1; otherwise mode 0.
   - Else **coming this way**: the same with ahead < 35.0 and |lat′| < InertiaBox.x × 0.5 + 4.0,
     lat′ = ((−X.z)·e.z + (−X.y)·e.y) + (−X.x)·e.x (−X even in reverse; only |lat′| matters).
     If it holds: speed < 1.0 → mode 0; τ < 2.3 → mode 1; otherwise mode 0.
   - Else mode 0.
4. Else (farther than 35 m) mode 0.

So a pedestrian becomes wary of a car heading at it from up to 35 m and up to about 2.3 s away,
dodges when the impact is under 0.75 s away (within 20 m and a narrower corridor), and calms down
as soon as the car is no longer on course — except while diving, lying on the ground, getting up,
running to a wall, or backed against one.

## The wall behind the sidewalk

Modes 1 and 2 first look for a wall, unless the pedestrian is on a crossing piece (*k* = 0 or *n*):
a probe (`../physics/collision.md`, "Ground queries", mask "ground", a second probe state of the
pedestrian, hint its room) along the road's section axis X[*k*]: for side +1 from
S = (X + X) + *p* to E = *p* − X × 10, for side −1 from S = *p* − (X + X) to E = X × 10 + *p*
(per component), both raised by 1 in y. It starts 2 m toward the road and ends 10 m away from it,
behind the sidewalk. *w* = 1 if it hits (a building), else 0; the hit point is H. "Near" means
|*p* − H| < 1.25 (3D).

**Back up** (side-dependent version, mode 1): side +1: *p*.x = (X.x × 0.2) + H.x,
*p*.z = (X.z × 0.2) + H.z, *h* = atan2(X.x, X.z); side −1: *p*.x = H.x − X.x × 0.2, *p*.z likewise,
*h* = atan2(−X.x, −X.z); start `BACKUP`. The pedestrian stands 0.2 m off the wall facing the road.
**Back up** (mode 2 version): always the side +1 formulas, also on side −1 (MM2 quirk: there it
faces the wall and stands 0.2 m inside it).

## Wary (mode 1)

**On a crossing piece** (*k* = 0 or *n*): at the start (mode differs from the seen mode): queue by
the current sequence: `WALK` → `WALK_ANTIC`, `STAND` → `STAND_ANTIC`, `STAND_WALK` →
`WALK_ANTIC`, `WALK_STAND` → `STAND_ANTIC`; seen mode = mode. Every frame:
*h* = atan2(P_c.x − *p*.x, P_c.z − *p*.z): it stops (when the sequence ends) and watches the car.

**On a sidewalk segment:** wall probe (sets *w*). At the start:

- *w* = 0 (open sidewalk):
  - current `WALK`: if rnd() < 0.5 (one draw): start `WALK_ANTIC` (stop and watch). Otherwise
    start `RUN` and pick a run direction: a = C[*k*] − C[*k* − 1] (road centre points);
    *d*_new = +1 if (F.z·a.z + F.y·a.y) + F.x·a.x > 0, else −1 (run the way the car travels). If
    *d*_new ≠ *d*: *d* = *d*_new; *x* = locate(); *h* = H(*x*, 0, *d*) (`pedestrian-walking.md`,
    "Sidewalk heading"), and if *d* = +1 the heading is turned half round: *h* − 3.14 if *h* > 0,
    else *h* + 3.14 (MM2 quirk: such a runner starts facing the car and turns away over the next
    frames).
  - otherwise queue by the current sequence: `STAND` → `STAND_ANTIC`, `STAND_WALK` → `WALK_ANTIC`,
    `WALK_STAND` → `STAND_ANTIC`.
- *w* = 1 (a wall): if not near: *h* = atan2(−X.x, −X.z) for side +1, atan2(X.x, X.z) for side −1
  (away from the road) and start `RUN`; if near: back up (mode 1 version).
- seen mode = mode.

Every frame (also the first, after the start):

- *w* = 1: if the current sequence is `RUN` and the pedestrian is near: back up (mode 1 version).
  Otherwise nothing (no turning).
- *w* = 0: if the current sequence is `RUN`: do the whole walking behaviour
  (`pedestrian-walking.md`, "Walking": locate, obstacles, the 6 m target, road ends) at running
  speed. Otherwise *h* = atan2(P_c.x − *p*.x, P_c.z − *p*.z) (watch the car).

## Dodging (mode 2)

**Dive start** (from a sidewalk with no wall, or on a crossing piece):

1. e = *p* − P_c (3D). Reverse flag = (gear = 0). Facing axis G = −Z if reverse, else +Z; side
   axis S as above (−X if reverse, else X).
2. *h* = atan2(G.x, G.z): the pedestrian turns to face the oncoming car (G points from the car
   back along its travel).
3. **Dive start offset** δ = S·e, summed as (S.z·e.z + S.y·e.y) + S.x·e.x on a sidewalk and
   (S.x·e.x + S.z·e.z) + S.y·e.y on a crossing piece. δ > 0: the pedestrian is to the right of the
   car's path.
4. Dive side: steering > 0.85 → right; steering < −0.85 → left; otherwise δ > 0 → left, else
   right. ("Left" and "right" are the pedestrian's own: facing the car, its left is the car's right.)
5. Start `WALK_LDIVE` / `WALK_RDIVE` if the current sequence is `WALK`, else `ANTIC_LDIVE` /
   `ANTIC_RDIVE`; play the avoidance voice.

**On a sidewalk segment:** wall probe. At the start: *w* = 0 → dive start; *w* = 1 → if not near:
*h* = atan2(−X.x, −X.z) (for both sides), voice, start `RUN`; if near: back up (mode 2 version).
Seen mode = mode. Every frame (also the first):

- current `RUN`: if near (with this frame's probe; a missed probe counts as not near — MM2 reads a
  stale value there, *our choice*): back up (mode 2 version). Stop.
- current `BACKUP`: stop.
- otherwise the **dive control** below.

**On a crossing piece:** at the start: dive start; seen mode = mode. Every frame: the dive control.

**Dive control** (every frame of a dive, the car's current matrix): G and S by the reverse flag of
the dive start; e = *p* − P_c; lat = (S.x·e.x + S.z·e.z) + S.y·e.y (where it is now across the
car's path); v = (current sequence's sideways speed) × 0.03333; *f* = its animation frame.

- current `LDIVE_GROUNDL` or `RDIVE_GROUNDR` (sliding on the ground): if
  |lat| < |((*f* × v) + δ) + 2.54|: latFactor = 5.0;
- else if *f* > 12 and |lat| < |(*f* × v) + δ|: latFactor = 3.0;
- *h* = atan2(G.x, G.z) in every case.

latFactor multiplies the sideways speed in the update's move (`pedestrian-walking.md`, "One
update", step 6): a pedestrian that has not yet got as far from the car's line as its dive should
have taken it (the car may have turned) dives 3 or 5 times as fast. The dive sequences then run
their table links: dive → `LDIVE_GROUNDL`/`RDIVE_GROUNDR` (sliding sideways 6.5–8.7 m/s) →
`GROUND_STANDL`/`GROUND_STANDR` (getting up, 1.5 s) → `STAND`. Only then does the mode choice run
again.

## Recovering

When the mode returns to 0, the walking start (`pedestrian-walking.md`, "Walking", step 1) queues
the way back to walking: `ANTIC`/`WALK_ANTIC` → `ANTIC_WALK`, `RUN` → `RUN_WALK`, `BACKUP` →
voice and `BACKUP_WALK`; a pedestrian standing (`STAND`) queues `STAND_WALK`. A backed-up pedestrian
leaves the wall only through rule 1 of "Choosing the mode", with a new random offset. After a dive
or a run the pedestrian is wherever it ended up (possibly in the road or against a building): the
walking rules steer it back to its sidewalk path, whose offset is only renewed when it passes to
the next segment.

## Being hit

Never: pedestrians are not physical (`pedestrians.md`, "What pedestrians do not do"). A car that
reaches a pedestrian — one that dived the wrong way, a car faster than the reaction, a car turning
into it, or the car standing still on it — drives through the figure, which carries on with its
behaviour. There is no damage, no sound, no score, and no effect on the car.

## Checked against

- The thresholds (35 m, 20 m, 6 m boxes, 0.75 s, 2.3 s, 1.25 m, 0.2 m, 0.85, 2.54, 12 frames,
  factors 3 and 5, speed 1.0) are float constants read from the machine code
  (`work/notes/peds/peds-notes.md` holds the trail).
- The sequences named here exist in all four tables; the dive and run speeds are in
  `../formats/ped-anim.md` ("Shipped values"): a man's dive moves him 2.2 m in 0.33 s, then 2.18 m
  more sliding.
