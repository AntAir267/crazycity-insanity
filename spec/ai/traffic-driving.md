# Traffic driving: lanes, speed, gaps, intersections

Part of the ambient traffic spec (`traffic.md` defines the terms and the frame order). This page
says how a traffic car that is driving normally moves: along a **rail** made of curve pieces through
its lane and across intersections, with a speed decided each frame from the speed limit, the car
ahead and the intersection ahead. Traffic cars are **not** physically simulated while they drive:
their position is a point on the rail, and their height and tilt come from the ground
(`traffic-visuals.md`). They become physical bodies only when hit (`traffic-reactions.md`).

The lane geometry (pieces along a lane, the turn piece through an intersection, evaluating a piece,
choosing the next road) is defined in `road-network.md`; this page uses its terms: side B = +1
(travels section 0 → *n* − 1), side A = −1, travel tangent Tan(*i*), start direction S, end
direction E, seglen(*k*), X(*i*) = X[sec(*i*)], open side, exit intersection, outgoing side.
Traffic lights and stop signs are `traffic-control.md`.

All lengths in metres, speeds m/s, times s. *F* is the frame time (`traffic.md`). Arithmetic follows
`../physics/vehicle.md`, "Precision": float32 with one rounding per operation, evaluated exactly in
the grouping written (MM2 runs the FPU at 24-bit precision), `sin`/`atan2` in double precision and
rounded once.

## Per-car constants

Fixed when the pool is created (`traffic-population.md`, "The pool", gives the draw order):

| Constant | Value | Meaning |
|---|---|---|
| lateral offset *o* | sin(*r* × 6.2831) × 0.5, *r* = rnd() | the car drives *o* metres to the side of its lane line, along the section X axes (−0.5 … 0.5) |
| reaction delay *T*₀ | 8 + trunc(*r* × 17), *r* = rnd() (an integer) | frames (8 … 24) a car waits before it reacts to the car in front |
| acceleration *A* | *r* × 3 + 5, *r* = rnd() | m/s², 5 … 8 |
| gap *G* | *r* × 2.5 + 0.5, the **same** *r* | extra distance kept behind the car ahead, 0.5 … 3 m (slow starters keep big gaps) |
| speeding *E* | 0, 8, 6, 4, 2, 0, 8, 6, … in pool order | m/s added to the speed limit |

Speeding comes from a counter *c* (float) that is 0 when the game starts: *E* = *c* + *c*; then
*c* = *c* − 1, and if *c* < 0 then *c* = 4. MM2 never resets *c*, so after the first city load
the sequence continues where it stopped. **Engine:** set *c* = 0 whenever the pool is created.

From the car's type (`../formats/aivehicledata.md`, "Derived numbers"): **front** (origin to front
bumper; `road-network.md` calls it *f*), **back** (origin to rear bumper).

## Car state

| Name | Meaning |
|---|---|
| road, side, lane | where the car is (lane index within its side) |
| list lane | the lane whose car list holds the car (the target lane during a lane change) |
| next road, next side, next lane | where it goes after the intersection ahead (may be "none") |
| *x* | distance along the current lane from its first point (lane distances start at 0) |
| rail kind | 0 on the lane, 1 in the turn piece to the next road, 2 changing lanes |
| *k* | current piece of the lane: between lane points *k* − 1 and *k* (1 … *n* − 1) |
| *u*, ℓ, 1/ℓ | distance into the current piece, its length, and the stored reciprocal |
| *x*_LC | distance on the target lane where a lane change ends (rail kind 2) |
| *v* | speed (never negative) |
| *a* | acceleration being applied |
| *v*ₜ | speed aimed at ("target"); *a* is applied until *v* reaches it |
| look-ahead | 25 normally, 5 after creeping (below) |
| go | 0/1: cleared to enter the intersection ahead |
| queued | 0/1: has joined the stop-sign queue of the intersection ahead |
| react | reaction counter (frames; a signed 16-bit integer in MM2) |
| lane-change allowed | 1 while the car may still try one lane change on this road |
| state | driving (this page) or another behaviour (`traffic-reactions.md`) |
| indicator | 0 off, 1 left lamps, 2 right lamps, 3 both (`traffic-visuals.md`) |

**Car lists.** Every lane (road, side, lane) keeps a list of its cars, front (largest *x*) first,
and knows its back car. "The car ahead" of a car is the one before it in its **list lane's** list.
Insertion rules:

- **sorted insert** (placing a car, lane changes): before the first car whose *x* is smaller than
  the new car's (after every car with equal or larger *x*);
- **append** (arriving on a road, turning round): at the back, whatever its *x*.

A car in the turn piece stays in the list of the lane it is leaving until it arrives. A car changing
lanes is in two lists (old and target lane) until the change ends.

## The rail

The car's reference point follows `road-network.md`, "Lane paths", with *o* = its lateral offset
and *f* = its front distance: the pieces along its lane, then the turn piece to (next road, next
side, next lane), then the next lane's pieces. A car stores its current piece (the four
coefficients per axis, `road-network.md`, "Evaluating a piece (a moving car)"), so each piece is
built once and evaluated every frame (`traffic-visuals.md`, "The solve"). Which form of a piece is
built depends on how it is set up (`road-network.md`, "Pieces along a lane (a moving car)" and
"Pieces set up other ways"): **advancing** from the previous piece uses the table; **starting** on
the rail, the **end of a lane change** and **arriving** on a 2-section road use the other forms (so
a lane's last piece ends at Q(*f*) + *o*·X(*n* − 2) when reached by advancing, and at the own-lane
stop point + *o*·X(*n* − 1) when set up by starting or after a lane change).

Rail kinds: 0 = a lane piece *k*; 1 = the turn piece; 2 = a lane-change piece (`Lane changes`);
3 = a rejoining curve (`traffic-reactions.md`). A knocked or avoiding car keeps its rail kind and
its place in its lane list (a knocked car with its last *x* and speed 0, an avoiding car with *x*
re-derived from its position), so the cars behind treat it as a car on the lane.

### Starting a car on its rail

On the first driving update after a car is placed (or resumes driving, `traffic-reactions.md`):

1. go = 0, queued = 0; lane-change allowed = 1 if *x* ≤ 0.25 × (λ − front), else 0, with
   λ = the lane's length D[*n* − 1]; look-ahead = 25.
2. If there is a next road, compute the turn piece length *L*ₜ (`road-network.md`). If
   *x* > *L*ₜ + (λ − front) the car is already past the intersection: *x* = *x* − (*L*ₜ + (λ −
   front)); take it out of its lane's list, append it to the next lane's list, make next road,
   side and lane the current ones, choose a new next road, and recompute λ and *L*ₜ.
3. If *x* ≥ λ − front: the car is in the turn piece: *u* = *x* − (λ − front), ℓ = *L*ₜ, rail
   kind 1 (MM2 presets the car's position to the centre of the intersection it drives into, so
   that the ground probes start near the right height).
4. Otherwise *k* = the piece containing *x* (`road-network.md`, "Distance along a lane"),
   *u* = *x* − D[*k* − 1], ℓ and the coefficients of piece *k*, rail kind 0 (position preset to
   the straight line between lane points *k* − 1 and *k* at *u*/ℓ, without the offset).
5. 1/ℓ is stored; react = *T*₀; the car is solved (`traffic-visuals.md`) and its room updated.

## One driving update

Every frame, for a car in the driving state (`traffic.md`, "Frame order"):

1. **Speed decision and integration** (`The speed decision`).
2. **Lane-change trigger** (`Lane changes`).
3. If *v* > 0: **advance** along the rail (`Advancing`); if the car is still on the streets,
   solve its position (`traffic-visuals.md`), then the **player check** (`traffic-reactions.md`);
   if that switches the car to avoiding a player, stop here (no step 4).
   If *v* ≤ 0: no advance; only the one-off solve near a player (`traffic-visuals.md`, "When it
   is computed").
4. react = react + 1.

## Target speed

For the car's road and its list lane *l* (*lanes* = the lane count of the car's side):

- normal road: limit + *E*;
- freeway road (road flag 0x04): limit + (((*lanes* − *l* − 1) × 5) + *E*) — lanes with lower index
  are faster by 5 m/s each (next to the centre line in SF; at the kerb in London, whose lanes the
  road network mirrors).

limit = the road's speed limit after loading (`bai.md`, "Load procedure", step 9: 15 in both
cities, 27.5 on freeways, the race file's exception value on exception roads).

"**Raise the target**" below means: *v*ₜ = target speed + 0.0001 and *a* = *A*.

## Distance to another car

Δ(*C*) from this car to car *C* (*C*'s values with subscript *C*; λ = lane lengths):

| this car | *C* | Δ |
|---|---|---|
| any | on another road (road ids differ) | (ℓ − *u*) + *x*_C |
| lane | lane or turn piece | *x*_C − *x* |
| lane | changing lanes | *s* = λ_src − front (λ_src = the length of *C*'s source lane, this car's side; front = **this** car's front), *q* = (*s* × 0.25) + *u*_C; if *C*'s source lane = this car's lane: *q* − *x*, else ((*q* × λ_mine) / λ_src) − *x* |
| turn piece | lane | *x*_C − *x* |
| turn piece | turn piece | *u*_C − *u* |
| turn piece | changing lanes | 9999 |
| changing lanes | lane or turn piece | *x*_C − (*x*_LC − (ℓ − *u*)) |
| changing lanes | changing lanes | with *j* = the piece of *C*'s source lane containing *x*_C: (D_mine[*j*] − *x*) − (D_C[*j*] − *x*_C) |
| any | rejoining (rail kind 3, `traffic-reactions.md`) | straight-line distance between the two cars' origins (3D) |

## The speed decision

*v*₀ = the speed at the start of the frame; Δ_last = 99999 (the last Δ measured this frame).

1. dEnd = distance from the front bumper to the end of the list lane: on the lane,
   λ − (*x* + front); changing lanes, ((λ − front) + (ℓ − *u*)) − *x*_LC; in the turn piece, 9999.
2. If dEnd ≥ look-ahead, or the car is in the turn piece: **open road** (step 3); otherwise steps 4
   and 5.
3. **Open road.**
   - If there is a car ahead *C*: Δ = Δ(*C*). If Δ < 20: if react > *T*₀, **follow** *C* at Δ.
     If Δ ≥ 20: if target speed > *v*ₜ or *a* < *A*, raise the target.
   - Else if the car is in the turn piece: find the car it will end up behind. Δ = 99999; if the
     next lane's list has a back car that is not this car: Δ = Δ(that car), candidate = it. Then for
     each lane of the current road and side, in order, its front car *H*: if *H* is in its turn
     piece, is not this car, has the same next road and next lane, and Δ(*H*) < Δ: Δ = Δ(*H*),
     candidate = *H* (negative distances included). If 0 < Δ < 20: if react > *T*₀ follow the
     candidate at Δ. Otherwise, if target speed > *v*ₜ or *a* < *A*, raise the target.
   - Else: if target speed > *v*ₜ, raise the target.
   Continue at step 6 (no indicator update on this path).
4. **Permission.** If go = 0: ask the intersection ahead (`Intersections`). If it lets the car in,
   go = 1, and if *v* < 0.01 and dEnd < 0.5: react = *T*₀ − 3.
5. **Approach.**
   - **go = 0 (stop at the line).** *C* = the car ahead; if *C* is in the knocked state
     (`traffic-reactions.md`) *C*′ = the car ahead of *C*, else *C*′ = *C*. If *C*′ exists, is not
     in its turn piece and is on the same road: Δ = Δ(*C*′); if Δ < 20: if react > *T*₀, follow
     at Δ using *C*'s data (MM2 passes the car directly ahead even when it measured the distance to
     the one beyond it); if Δ ≥ 20: if target speed > *v*ₜ or *a* < *A*, raise the target.
     Otherwise (no such car):
     - **creep** if react > *T*₀, *v*₀ ≤ 2 and dEnd > 1: *v* = 2, look-ahead = 5, raise the target;
     - else **brake to the line** if *v*ₜ ≠ 0: *a* = −((*v*₀ × *v*₀) / (*h* + *h*)) with
       *h* = dEnd − 0.25, then *v*ₜ = 0 (the deceleration is computed once and then held);
     - else (already braking) if *v* < 0.2 and dEnd < 0.5: *v* = 0.
   - **go = 1 (cleared).** *C* = the car ahead. None: if target speed > *v*ₜ and react > *T*₀, raise
     the target. Otherwise Δ = Δ(*C*): if Δ ≥ 20 and react > *T*₀: raise the target if target
     speed > *v*ₜ, else if *v*ₜ > 0 and *a* = 0, *a* = *A*. If Δ < 20, *C*'s *v*ₜ ≠ this car's
     *v*ₜ and react > *T*₀: follow *C* at Δ (equal targets: nothing).
   - **Indicator**: if there is a next road: turn class of (current road → next road)
     (`road-network.md`, "Choosing the next road", rule 4): right → indicator 2, left → 1,
     straight → unchanged.
6. **Integrate.** *v* = (*F* × *a*) + *v*. If (*v* < *v*ₜ + 0.05 and *a* < 0) or (*v*ₜ < *v* and
   *a* > 0): *v* = *v*ₜ and *a* = 0. If the car ahead exists, its *v* < 0.01, this car's *v* < 0.01
   and Δ_last < 20: react = 0. Then *u* = (*F* × *v*) + *u* and *x* = (*F* × *v*) + *x*.

The car never reverses. It brakes only through following, the line rule and the snap at the
target, so its deceleration is unlimited (an ungranted car close to the line stops almost at once).

### Follow *C* at Δ

*v*_C, *a*_C, back_C: the car ahead's speed, acceleration and back distance.

1. *w* = *v*_C − 2.5, clamped to 0 … 999 (aim 2.5 m/s below the car ahead).
2. *m* = back_C + front (bumpers touching).
3. If Δ < *m*: if *C* is changing lanes, *a* = (*v* × *v*) × −0.16666667, *v*ₜ = 0; otherwise
   *v* = 0, *a* = 0, *v*ₜ = 0. Done.
4. If Δ < *m* + *G* (inside the gap; computed (back_C + front) + *G*):
   - if *w* > *v*: *a* = *A*;
   - else: *v* = *v* × 0.75; if *a*_C ≤ 0: *a* = 0, *v*ₜ = 0, done; else *a* = *a*_C if
     *a*_C ≤ *A*, otherwise *A*;
   - *v*ₜ = *w* if *w* ≤ target speed, else target speed. Done.
5. Otherwise (Δ ≥ *m* + *G*):
   - if *w* > *v*: *a* = *A*;
   - else: *M* = (*G* + back_C) + front; *h* = Δ − *M*, then *h* = *h* − 2 if both cars are in turn
     pieces or both are changing lanes; *a* = ((*w* × *w*) − (*v* × *v*)) / (*h* + *h*);
   - *v*ₜ = target speed if target speed < *w*, else *w*. (No +0.0001 here.)

So a car closing on a stopped car brakes to reach 0 exactly *G* metres behind it; inside the gap it
sheds 25 % of its speed per frame; a faster car behind a slower one closes to under 20 m, falls back
to 2.5 m/s below it, drops behind 20 m and accelerates again (it "surges").

## Intersections

### May the car enter?

Asked each frame while go = 0 and the car is within the look-ahead of its line (rail kind 0 or 2).
The **approach** is the car's road end at its exit intersection; its control (stop sign, light,
none, yield) and light are defined in `traffic-control.md`.

1. **Control**: the answer of `traffic-control.md`, "May a car enter the intersection?", for the
   car's approach, with dEnd as the remaining distance to the stop point. Traffic's side of it:
   - "the car is woken" means react = 0 for this car and every car behind it in its list lane
     with *v* < 0.01 (light changes and the always-green switch wake cars the same way);
   - joining the stop-sign queue sets queued = 1; arriving on the next road leaves the queue;
   - the **held** flag is the "AI ahead" mark of `driver.md`, "Yield marks": opponent and police
     drivers clear their marks just before they update and set them just after, on every road
     meeting an intersection ahead on their route window (up to three roads ahead) whose control at
     that intersection is a stop sign or a light. Traffic updates before them (`traffic.md`, "Frame
     order"), so cars see the marks set at the end of the previous frame. The mark is per road, so
     it also holds cars approaching that road's other end.
2. If the control said yes, three checks on the car's side, in this order:
   1. **Accident**: if any car registered in the intersection, or registered on any section of the
      next road, is not in its normal driving state (`traffic-reactions.md`): blocked.
   2. **Room on the exit lane**: *B* = the back car of the next lane's list (none: room). *acc* =
      back_B; then for *C* = the car ahead of this one, then the car ahead of *C* (each in its own
      list lane), while *C* exists, is on the same road and *x* < *x*_C: if *C*'s next road is this
      car's next road, *acc* = len(*C*) + *acc*. Room if *x*_B > len(this car) + *acc*, with
      len(*Y*) = (*G*_Y + front_Y) + back_Y. (Cars ahead going to the same road but to another lane
      count too.)
   3. **Crossing conflict** (skipped if the current or next road is a freeway): for every other road
      *R* of the exit intersection and every lane *l* < *R*'s lane count of **this car's** side,
      *H* = the front car of that lane (this car's side); if *H* has go = 1: *p* = the last point of
      *H*'s lane on *R*, *q* = the first point of *H*'s next lane on its next road (taken from
      **this car's** next side); σ(*y*) = the sign (−1, 0, 1) of
      ((*y*.z − *c*.z) × X.z + (*y*.y − *c*.y) × X.y) + (*y*.x − *c*.x) × X.x with this car's position
      *c* and matrix X row; if σ(*p*) ≠ σ(*q*): conflict (the other car's path crosses this car's
      line of travel).
   If 1 or 2 fails, choose the next road again (it draws random numbers, so a car waiting for room on
   a green light draws every frame). If 3 fails, wait. If all pass: go = 1.
3. Once go = 1 nothing is checked again until the car arrives on the next road: a car cleared on
   green drives through when the light turns red; yellow means stop however close the car is.

### Waiting

A car that may not go brakes so that its front bumper stops 0.25 m before the end of its lane (the
intersection's mouth), or stops behind the car ahead. React timing, in frames:

- first car at the line, cleared while stopped: moves on the 4th frame after the clearing frame;
- a car stopped 0.5–1 m short of a light: react was set to 0 when the light turned green (light
  changes reset the stopped cars of that approach, `traffic-control.md`), so it moves *T*₀ + 1
  frames later;
- a queued car: react stays 0 while its leader stands, so it starts *T*₀ + 1 frames after its leader
  moved off.

These are frame counts: in seconds they depend on the frame rate (at 60 frames per second *T*₀ is
0.13–0.4 s). MM2's react counter is a signed 16-bit integer: after 32 767 frames without a reset it
wraps negative and the car stops reacting to the car ahead for about as long (about 9 minutes of
free driving at 60 fps). **Engine:** keep the wrap.

## Advancing along the rail

After the speed decision, if *u* > ℓ the car moves on to the next piece (one step per frame; a car
fast enough to pass a whole short piece in one frame catches up over the next frames):

- **Lane piece finished** (rail kind 0): *k* = *k* + 1, *u* = *u* − ℓ.
  - If there is no next road, or *k* ≤ *n* − 1: build lane piece *k* (the last piece if
    *k* = *n* − 1; if that piece's length is ≤ 0, build the turn piece instead, rail kind 1).
  - Else (*k* = *n*, the last piece is done): build the turn piece, rail kind 1 (*u* keeps the
    overshoot).
- **Turn piece finished** (rail kind 1): **arriving** (below).
- **Lane-change piece finished** (rail kind 2): `Lane changes`.

1/ℓ is stored with every new piece.

### Arriving on the next road

1. go = 0, look-ahead = 25, lane-change allowed = 1.
2. If the approach had a stop sign, leave the intersection's stop-sign queue; queued = 0.
3. Take the car out of its lane's list (it is the list's front car).
4. **Leaving the populated area.** If the next road is not an active road
   (`traffic-population.md`):
   - if the **current** road's side A (−1) is not open (MM2 tests side A whatever side the car is
     on): the car **leaves the streets**: its physical body (if any) is released, *v* = 0, it is
     taken out of its intersection's lists and returned to the free list. Done.
     **Engine:** test the side the car would turn onto (on a road whose side B has no lanes MM2
     would give a car arriving on side A an invalid lane).
   - otherwise it **turns round**: side = −side; if lane ≥ the new side's lane count, lane =
     list lane = lane count − 1; append the car to that lane's list of the **current** road. It
     then drives the same road back from its start: it reappears on the other side of the road it
     was on, at the edge of the populated area.
   Otherwise append the car to the next lane's list and make next road, side and lane current.
5. Choose the next road.
6. *k* = 1; *x* = *u* − ℓ (the overshoot past the turn piece); *u* = *x*; build lane piece 1; rail
   kind 0; indicator = 0.

## Choosing the next road

`road-network.md`, "Choosing the next road", gives the rules; its random draws come from the
traffic generator (`traffic.md`). Traffic asks for a next road:

- when a car is placed (`traffic-population.md`; if it gets none, filling stops for that lane);
- when it arrives on a road (step 5 above) and when a lane change ends;
- every frame the intersection blocks it (accident or no room on the exit lane);
- when it starts on its rail past the end of its road (`Starting a car on its rail`, step 2);
- when it rejoins traffic after being knocked (`traffic-reactions.md`).

When the rules find no road, the lane-0 and last-lane rules set "no next road" and the others leave
the previous choice in place. A car without a next road keeps advancing through its last piece and
MM2 reads past the end of the road's data. **Engine:** stop the car at the end of its last piece
(*v* = 0) and ask again each frame. (Every open lane end of the shipped cities has at least one next
road.)

## Lane changes

Allowed when the race file's `[AmbientLaneChanges]` is non-zero (`aimap.md`; default 1, on in every
shipped mode).

**Trigger** (driving update step 2): if allowed, lane-change allowed = 1 and *x* > 0.25 × λ (λ =
the current lane's full length D[*n* − 1]): try once, then lane-change allowed = 0. The rail kind,
the speed and the distance to the intersection are not checked.

**Target** (only if the road's centre length is more than 60 m; no random draw): *t* = 0.25 × λ;
count(*j*) = the number of cars in lane *j*'s list, from the front, before the first car with
*x* ≤ *t* (this car counts in its own lane; a car changing lanes counts in both). best =
count(lane), target = lane; if lane > 0 and count(lane − 1) < best: target = lane − 1,
best = count(lane − 1); if lane < lanes − 1 and count(lane + 1) < best: target = lane + 1. Ties keep
the lane; the lower lane is tried first. No check of the cars beside or behind, of the next turn, or
of speed. A car alone on an empty two-lane road changes (1 car against 0).

**The lane-change piece** (if target ≠ lane; D_L, D_T = the distances of the current and the target
lane, P_T the target lane's points; "the piece containing *s*" as in `road-network.md`):

1. *j* = the piece of the current lane containing *x*; *m* = D_T[*j*] − (D_L[*j*] − *x*) (the car's
   distance mapped onto the target lane); sorted-insert the car into the target lane's list at
   distance *m* (it stays in its old lane's list too); list lane = target.
2. P0, T0 = the position and derivative of the current piece at *u* × (1/ℓ).
3. *e* = ((λ − front) × 0.25) + 30; *j*₂ = the current lane's piece containing *e*;
   *x*_LC = D_T[*j*₂] − (D_L[*j*₂] − *e*) (the end, mapped onto the target lane).
4. *j*₃ = the target lane's piece containing *x*_LC; build a temporary piece *j*₃ of the target
   lane with length *g*: if *j*₃ < *n* − 1: *g* = seglen_T(*j*₃), P0 = P_T[*j*₃ − 1],
   P1 = P_T[*j*₃], T0 = *g* × Tan(*j*₃ − 1), T1 = *g* × Tan(*j*₃) (also for *j*₃ = 1); if
   *j*₃ = *n* − 1: *g* = seglen_T(*n* − 1) + **back** (MM2 adds the back distance where the last
   piece subtracts the front one), P0 = P_T[*n* − 2], P1 = the target lane's own stop point
   (P[*n*−1] − P[*n*−2])·*w* + P[*n*−2] with *w* = 1 − (front / seglen_T(*n* − 1))
   (`road-network.md`, "Pieces set up other ways"), T0 = *g* × Tan(*n* − 2), T1 = *g* × E. Then
   P0 = (*o* × X(*j*₃ − 1)) + P0 and P1 = (*o* × X(*j*₃)) + P1. P1, T1 = the position and derivative
   of that piece at τ = (*x*_LC′ − D_T[*j*₃ − 1]) / *g* with *x*_LC′ = *x*_LC clamped to 0 … λ_T (a
   division, not × 1/*g*): its coefficients are
   computed and stored exactly as for a moving car's piece (`road-network.md`, "Evaluating a piece
   (a moving car)") and evaluated with the position and derivative formulas of
   `traffic-visuals.md`, "The solve", step 2 (no clamp of τ); the stored coefficients are then
   replaced by the lane-change piece's in step 6.
5. *u* = 0, ℓ = *x*_LC − *x*, 1/ℓ stored.
6. T0 and T1: each normalised (l² = (x × x + y × y) + z × z, g = 1 / √l², 0 if l² = 0, each
   component g × c) and then scaled (each component ℓ × c). Build the piece from P0, P1, T0, T1;
   rail kind 2; indicator = 2 if target > lane, else 1. (Higher lane numbers are on the driver's
   right in both cities, because the road network mirrors London's lanes, so the lamps match the
   direction.)

The piece is about 30 m long. During it the car follows the car ahead in the target lane, *x* keeps
growing, and dEnd uses the lane-change form. A lane change is never aborted.

**End of the lane change** (rail kind 2 finished): *x* = (*u* − ℓ) + *x*_LC; *k* = the target lane's
piece containing *x*; *u* = *x* − D_T[*k* − 1]; build lane piece *k* of the target lane; take the
car out of the old lane's list; lane = target; choose the next road; rail kind 0; indicator = 0.

## Checked against

`tools/ai/traffic.py sim` runs six cars (shipped types and their per-car constants) as a queue on a
400 m lane ending at a light that is red for the first 20 s, by the rules of this page in one
dimension (distance along the lane; `work/check/traffic_queue.csv` and `traffic_queue.png`):

- All cars start from rest at the spacing of `traffic-population.md` (one per 40 m at full density)
  and accelerate at 5.15–7.85 m/s² toward 15–23 m/s; at the red light the first car stops with its
  front bumper 0.33 m before the line (aim 0.25 m; the step-wise integration stops a little short);
  the others stop 0.62–2.88 m (their gap *G*) behind the car ahead.
- After the green each car moves off a reaction delay after the one ahead; the first passes 10 m/s
  1.85 s after the green, the sixth 6.3 s after.
- Behind a slower car the faster ones surge between about 12.5 and 17 m/s with a period of about
  1.5 s (the 20 m band of "Follow").

`tools/ai/traffic.py route <city> <seconds>` drives one car (va_sedans_s, its offset and
acceleration from the generator) for 300 s along the real lanes, using `road-network.md`'s pieces,
turn pieces and next-road rules (through `tools/ai/bai.py`), all approaches green
(`work/check/traffic_route_<city>.csv`, `.png`): SF 4.5 km, 13 intersections, the path continuous
everywhere (no frame moves the car more than 5 cm beyond *v* × *F*); London 4.5 km, 39
intersections, keeping left, with one jump of 0.42 m at the start of a turn piece on road 191 (the
last piece ends with the X axis of point *n* − 2, the turn piece starts with that of point *n* − 1;
on a road that bends at its end the offset moves the car sideways).
