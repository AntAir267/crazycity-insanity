# Cameras: chase, bumper, dash and how MM2 moves between them

Every camera below is updated once per game frame, after the car's inputs are set and all physics
steps of the frame have run (it follows the car's new pose). Its `dt` is the whole frame time *F*
(the clamped frame duration of `physics/vehicle.md`, "Time step"), not the physics step time. Several smoothing
steps work per frame rather than per second, so, as in MM2, the camera's feel depends a little on
the frame rate; do not "fix" this.

Conventions as in `pkg.md`: right-handed, +Y up, metres. A camera is a pose with axes X (right),
Y (up), Z and a position; it **looks along −Z**. "Up" is (0, 1, 0). The car's pose ("car matrix"
*M*: axes *M.X* right, *M.Y* up, *M.Z* backward, origin *M.p*) is the car model's pose as drawn.
*M*·*q* means the car-space point *q* in world space: *q.x*·*M.X* + *q.y*·*M.Y* + *q.z*·*M.Z* + *M.p*.
Rotations are right-handed about the given axis (a positive turn about Up turns −Z toward −X,
i.e. counter-clockwise seen from above).

## The cameras of a player car and their files

All tuning lives in text files `tune/camera/<name>.<kind>` (datParser text: `type: a`, then
`<kind> { … }` with `Key value` lines; a vector is three numbers). Names are looked up without
regard to case. `<car>` is the car's `BaseName`.

| View | File | Kind |
|---|---|---|
| near chase | `<car>_near.camTrackCS` | chase |
| far chase | `<car>_far.camTrackCS` | chase |
| indoor chase (automatic, see below) | `<car>_ind.camTrackCS` | chase |
| bumper | `<car>.camPovCS` | point of view |
| dash (cockpit) | `<car>_dash.camPovCS` | point of view |
| orbit "cross view" | none (built-in values) | orbit |

A missing file, or a missing key, leaves that camera with the built-in defaults listed below
(this happens to shipped cars too: `vpvwcup_far` is shipped as `vpvwcup__far` and is therefore
missing; only vpbug and vpbus ship an `_ind` file). Other files in `tune/camera/` (`*_POV.camPovCS`,
`vpdune.camTrackCS`, …) are never read.

The files are read in the order near, far, ind, bumper, dash.

## Switching views

- **C** cycles the "external" list in this order: near chase → bumper → far chase → near chase.
  From the dash or orbit view, C returns to the current entry of the list without advancing (and
  the dash view goes off).
- **D** toggles the dash view (switches instantly); D again returns to the current external entry.
  D is **ignored** while the orbit view is shown, while the map is in its half-screen mode, and
  while a pre-race or post-race camera runs (race modes). While the wide view is on, D cannot turn
  the dash view on (nothing changes).
- **V** toggles the orbit view (the dash view goes off). V again returns to the current external
  entry — **unless the orbit view was entered from the dash view**: then V returns to the dash view.
  That "bring the dash back" mark is the same one the half-screen map uses (Tab, below): it is the
  dash view's own on/off state, which every view key that is not ignored sets again at its end
  (on: mark set; off: mark cleared), and V or Tab into the half-screen mode set it after turning
  the dash off if the dash was on. So W, Backspace (rear-view mirror), Tab or Q pressed in the orbit
  view clear it, and V then returns to the external entry. (D, H, E and F are ignored there or do
  not touch the views, so they keep it.)
- **W** toggles the wide (letterbox) view. It is **ignored** while the map is in its half-screen or
  full-screen mode and while the dash view is shown.
- **Q** (full-screen map) and **Tab** (map modes) leave the wide view as it is, with one exception:
  Tab into the half-screen map mode draws the 3D view in the wide view's letterbox and turns the
  dash view off; Tab out of it restores the wide view as it was before and the dash view if it was
  on before. (HUD map details are a later milestone.)
- While a pre-race or post-race camera runs, no key changes the camera.
- Changes through C and V, and the return from dash, blend from the old camera to the new one over
  **0.8 s** with easing type 3 (see "Blending between cameras"). Entering the dash view (with D or
  with V from the orbit view) is instant.
- A new game or a reset (including the free-roam resets after falling through the city or
  landing in water) shows the view that was in use, without a blend: the current external entry
  (near chase in a new player profile), or the dash view if it was on — and then the external
  entry becomes the bumper camera, so D later returns to the bumper; the wide view and the gauges
  overlay stay as they were. The orbit view is never kept over a reset (back to the external
  entry). At the first game these settings come from the player file (`controls.md`, "Options in
  `players/<name>.cfg`", the view settings).
- While the bumper or dash camera is active, the car body is not drawn.

**Indoor chase camera.** Only for cars whose `.info` `Flags` value has any of the bits 1, 2 or 16
(vpbus, vpddbus, vpcentury, vpsemi). Each frame, if the car's room is enclosed (PSDL flag 0x02),
or the car's room contains a landmark (`.inst` bit 24) and a vertical probe from the camera
position straight up 100 m hits the city, and the active camera is the near or far chase, MM2
switches to the indoor chase camera (blend 1.0 s, type 3). When neither condition holds any more,
it switches back to the current external entry (blend 1.0 s, type 3).

## Projection

- **Field of view**: the active camera's `CameraFOV`, the **vertical** angle in degrees; the
  horizontal angle follows from the viewport's width/height. Shipped values are about 70
  (a few 56–60).
- **Near plane**: 0.5 m for chase cameras and 0.1 m for bumper and dash cameras whenever their
  file was read (the file's `CameraNear` is ignored). With a missing file the built-in near is
  used: 1.0 (chase), 3.0 (bumper/dash).
- **Far plane**: one value shared by all cameras: the `CameraFar` of the last camera file read for
  the car, i.e. the dash file: 600 for 16 of the 20 menu cars; vpvwcup 1000, vpcoop2k and vp4x4
  400, vpford 352.27. **But** in a game MM2 then replaces it, once the camera files are read, with
  the player's view distance (the graphics option, 100 – 1000 m, `../formats/profiles.md`; half of
  it with the software renderer), and again whenever the in-race view distance slider moves; so in
  play the far plane is the view distance (600 with the shipped profile) and `CameraFar` never
  shows. The city fog is clamped to it (`city-files.md`, "Fog").
- **Wide view (W)**: a letterbox. The picture covers rows 0.18·*H* to 0.84·*H* of the screen
  (height 0.66·*H*), black above and below, with a vertical field of view of 70° whatever the
  camera says. W again restores the full screen.
- During a blend, field of view and near plane are interpolated (below).

## Chase camera

### Keys

| Key | Default (no file) | Used for |
|---|---|---|
| `Offset` | 0, 1.9, 7.7 | goal position: sideways, height, distance behind the track point |
| `TrackTo` | 0, 0.8, 0 | the car-space point the camera follows and looks at ("track point") |
| `CollideType` | 0 | wall avoidance: 0 none, 1 as below, 2 a single-ray variant (not specified; no shipped file uses it) |
| `MinMaxOn` | 1 | keep the camera between floor and ceiling |
| `TrackBreak` | 0 | stop following the car's turning: 0 never, 1 while the car is tipped (up axis Y component < 0.35), 2 always |
| `MinAppXZPos`, `MaxAppXZPos` | 1.8, 12 | horizontal position approach rate at high / low speed |
| `MinSpeed`, `MaxSpeed` | 5, 35 | speed range (m/s) of that change |
| `AppInc`, `AppDec` | 15, 10 | how fast the rate itself rises / falls, per second |
| `AppYPos` | 5 | vertical position approach rate |
| `AppXZPos` | 0 | only the starting value of the horizontal rate (recomputed every frame) |
| `AppRot`, `AppXRot` | 30, 10 | rotation approach rates: heading and roll / pitch (`AppXRot` 0 = use `AppRot`) |
| `AppApp` | 0.7 | per-frame smoothing factor of the approach speeds |
| `ApproachOn`, `AppAppOn` | 1, 1 | approach on (else snap), speed smoothing on |
| `AppPosMin`, `AppRotMin` | 0.25, 0.01 | soft zone near the goal (position m, angle rad) |
| `MaxDist`, `MinDist` | 11, 7.93 | camera distance from the track point is kept between these |
| `LookAt` | 1 | 0..1: how much the camera aims at the track point rather than copying the goal orientation |
| `VertOffset` | 0.6 | how high above the track point to aim (below) |
| `HillMin`, `HillMax`, `HillLerp` | −0.56, 0.56, 0.05 | tilt with the slope (below) |
| `ReverseOn` | 1 | 1 swing round when reversing, −1 always look from the front, 0 off |
| `RevOnApp`, `RevOffApp` | 2, 4 | swing speeds, rad/s |
| `CameraFOV` | 60 | |
| `CameraNear`, `CameraFar` | 1.0, (shared) | see "Projection" |
| `BlendGoal` | 1 | see "Blending" |
| `LookAbove` | — | read but overwritten every frame |
| `MinHardSteer`, `DriftDelay`, `FrontRate`, `RearRate`, `FlipDelay`, `SteerOn`, `SteerMin`, `SteerAmt`, `RevDelay`, `BlendTime` | | read but never used |

Every shipped chase file sets CollideType 1, MinMaxOn 1, ApproachOn 1, AppAppOn 1, LookAt 1 (two
use 0.993), TrackBreak 0 or 1.

### State

Current pose *cur* (what is drawn), goal pose *tgt*, smoothed ground normal *N*, swing angle θ,
swing flag *rev* with direction *rdir* and timer *rt*, ground latch *g* with an air and a ground timer, break flags
*b1*, *b2*, last track point *P_old*, last goal position *G_old*, horizontal rate *a*, six approach
speeds (three position axes, three angles), and a "fresh" flag.

**Reset** (when the camera becomes the destination of a switch, or the car is reset): *cur* := *tgt*,
fresh := true, *b2* := false, *rev* := false, *rt* := 0, *rdir* := +1.

### Each frame

**1. Fresh frame.** If fresh: θ := 0.

**2. Read the car.** All car values here are the ones the car's physics used this frame: the
car's inputs as delivered by `controls.md` (quantised, after the reversing pedal swap and after the
player overrides such as the standstill parking brake), its gear, and its speed *v*. *s* = the car's
steering input. "Reverse gear" means gear R only (neutral does not count).

*Reverse swing* (only when `ReverseOn` = 1):
- In reverse gear:
  - If the car's handbrake input > 0.5: *a* := 0 (the current horizontal approach rate, not the
    wanted one; step 5 of this same frame then raises it by `AppInc`·dt again). This happens every
    frame the condition holds, at any speed. The handbrake input is 1 when the player holds the
    handbrake key **or** when the parking brake rule of `controls.md` applies (mph < 4 and the
    car's throttle exactly 0).
  - If not *rev* and the car's throttle input ≥ 0.05: *rt* += dt; then if *rt* ≥ 2.0: *rev* := true,
    *rdir* := +1 if *s* ≤ 0.1 else −1. While reversing through the automatic-reverse swap, the
    car's throttle input is the brake key. *rt* is not reset while the throttle is below 0.05; it
    simply stops growing.
- In any other gear: if *rev*: *rdir* := −1 if *s* ≤ 0.1 else +1; if |θ| > 3.1405928 then
  θ := *rdir*·π; *rev* := false. Always *rt* := 0.

With `ReverseOn` = −1: *rev* := true.

*Break test*: if (`TrackBreak` = 1 and *M.Y.y* < 0.35) or `TrackBreak` = 2: *b1* := true, *g* := false,
and skip the rest of step 2 (the timers below do not change that frame). Otherwise, with *w* = the
number of wheels touching the ground after this frame's physics:
- if *w* ≤ 2: air timer += dt; if air timer > 0.1 (strictly): *g* := false, ground timer := 0;
- else: ground timer += dt; if ground timer > 0.1 (strictly): *g* := true, air timer := 0.

dt is the frame time *F* (no further clamping). A timer is zeroed only by the other branch, so short
spells add up: e.g. 0.06 s in the air, a moment on the ground, 0.06 s in the air again releases the
latch. *g* starts true and both timers 0 when the camera is created; camera resets leave them alone.
Then *b1* := (|angular momentum of the car body|² > 2.25·10⁶ and not *g*).

**3. Slope tilt.** Ground normal *n* from the wheels' contact normals (vehicle spec): front = mean of
the touching front wheels' normals (zero if neither touches), rear likewise; *n* = mean of front and
rear, using only those with squared length > 0.01; if neither, *n* = Up. Then *N* := *n* if fresh,
else *N* + (*n* − *N*)·`HillLerp` (per frame); normalise *N*.

- α = angle between Up and *N* (0 when their cosine exceeds 0.9999999).
- *w* = clamp(α / (π/4), 0, 1).
- If α ≥ 0.01: *h* = *w* · (normalise(Up × *N*) · normalise(*M.X*)); else *h* = *w*.
  (*h* > 0 means the road rises ahead of the car.)
- *e* = sign(*h*) · (1 − cos(π · min(|*h*|, 0.5))) — ranges −1..1.
- Tilt *k* = −`HillMax` · *e* if *e* ≥ 0, else `HillMin` · *e*.

**4. Goal.** *P* = *M*·`TrackTo`; Δ = *P* − *P_old*; *P_old* := *P*.

- If (not *g* and (*b1* or *b2*)): *b1* := *b2* := true and *G* = *G_old* + Δ (the goal moves with
  the car without turning).
- Otherwise *b2* := false and: *b* = normalise(*M.Z.x*, 0, *M.Z.z*) (behind the car, flat),
  *r* = Up × *b* (the car's right, flat), and
  *G* = *P* + *b*·max(`Offset.z`, 0.01) + *r*·`Offset.x` + (0, `Offset.y`, 0).
- *G_old* := *G*.
- *P′* = *P* + (0, 0.4, 0).
- *A* = (`Offset.y` − 0.8)·`VertOffset` (the aim height; replaces the file's `LookAbove`).
- *tgt* := look-at from *G* toward *P′* (definition at the end).

*Swing angle θ*: with `ReverseOn` = 1, θ moves toward 0 at `RevOffApp`·dt when not *rev*, or toward
*rdir*·π at `RevOnApp`·dt when *rev*, never passing the target. With −1: θ = π if *rev* else 0.
With 0: unchanged.

*Pitch* φ = *k*·(1 − 2·min(|θ|/π, 1)) (with `ReverseOn` = 0: φ = *k*). Looking from the front, the
slope tilt is mirrored.

If θ ≠ 0 or φ ≠ 0: turn *tgt* (its axes and its position) about *P′*: first by −φ about the axis
Up × *tgt.Z*, then by θ about Up.

Finally *tgt* := look-at from *tgt*'s position toward *P′* + (0, *A*, 0).

**5. Horizontal approach rate.**
- *f* = 0 if `MinSpeed` = `MaxSpeed` or *v* ≤ `MinSpeed`; 1 if *v* ≥ `MaxSpeed`; else
  (*v* − `MinSpeed`)/(`MaxSpeed` − `MinSpeed`).
- Wanted rate *a\** = `MaxAppXZPos` + (`MinAppXZPos` − `MaxAppXZPos`)·*f* if `MinAppXZPos` ≠ 0,
  else `MaxAppXZPos`. If `ReverseOn` = 0 and the car is in reverse gear, *a\** = 20.
- *a* moves toward *a\** by `AppInc`·dt when rising, `AppDec`·dt when falling, never passing it.

So the camera follows tightly at low speed and lags more at high speed.

**6. Approach.** If `ApproachOn` = 0 or fresh: *cur* := *tgt*. Otherwise:

- *P₀* = *M*·`TrackTo` (without the 0.4).
- Position: each coordinate of *cur*'s position approaches the same coordinate of *tgt*'s
  ("approach step" below): x and z with rate *a*, y with `AppYPos`, soft zone `AppPosMin`, each axis
  with its own speed.
- Distance clamp: if `MaxDist` ≠ 0, `MinDist` ≤ `MaxDist`, and the position differs from *P₀*: if it
  is farther than `MaxDist` from *P₀*, move it along the line from *P₀* to exactly `MaxDist`; then if
  nearer than `MinDist`, to exactly `MinDist`. Distances are 3D, *P₀* is this frame's track point
  (from the car pose after this frame's physics). This runs every frame whatever *a* is: when *a*
  is small (just after a := 0, or while it recovers) the approach hardly moves the camera and the
  clamp alone drags it after the car, so the camera is never farther than `MaxDist` from the
  track point (every shipped chase file has `MaxDist` ≠ 0 and `MinDist` ≤ `MaxDist`).
- Orientation: take the angles (pitch, heading, roll) of *cur* and of *tgt* (definition below).
  Bring each target angle near the current one: if current > π/2 and target < −π/2, target += 2π; if
  current < −π/2 and target > π/2, target −= 2π.
- If `LookAt` ≠ 0: build the look-at pose from *cur*'s new position toward *P₀* + (0, *A*, 0), take
  its angles (same 2π adjustment against the current angles) and set
  target angles := (1 − `LookAt`)·target + `LookAt`·look-at angles.
- Each angle approaches its target: roll and heading with `AppRot`, pitch with `AppXRot` (or
  `AppRot` if `AppXRot` is 0), soft zone `AppRotMin`, each with its own speed.
- *cur*'s axes are rebuilt from the three angles; its position is the one just computed.

**7. Floor and ceiling** (if `MinMaxOn`). Let *y₀* be *cur*'s height at the **start** of this frame
and (*x*, *z*) its new horizontal position. Probe the city's collision geometry along a vertical
segment from (*x*, *y₀*, *z*) up to *y₀* + 5: a hit whose surface normal has Y component < 0.7 is a
ceiling at hit height − 0.5. Probe down from (*x*, ceiling or *y₀*, *z*) to *y₀* − 5: a hit whose normal
has Y component > 0.7 is a floor at hit height + 0.5. If both exist and ceiling < floor, change
nothing. Otherwise lower the camera to the ceiling if above it, then raise it to the floor if below it.

**8. Walls** (`CollideType` 1). *d* = normalise(*cur.p* − *P₀*). With the current projection, half
sizes *hw* = near·tan(fov/2)·aspect + 0.33 and *hh* = near·tan(fov/2) + 0.33 (fov vertical, aspect
width/height). For the four corners *c* = ±*hw*·*cur.X* ± *hh*·*cur.Y*, probe the city collision
from *P₀* + *c* to *P₀* + *c* + *d*·`MaxDist`; keep the smallest hit fraction *t* (0..1 along the
segment) among hits whose normal *m* satisfies *m*·*d* < −0.00001. If any:
*L* = *t*·`MaxDist` + near − *R*, and if *L* < |*cur.p* − *P₀*|, set *cur.p* := *P₀* + *d*·*L*.
*R* = 0.33, except 1.11 for the near and far chase cameras while the car is in an enclosed room
(PSDL flag 0x02). The probes ignore the player's own car (the city's static geometry; the
collision spec defines what a probe sees).

**9.** fresh := false.

The camera never re-orients after steps 7–8; only its position moves.

### What MM2 shows when reversing

Examples with vpbug's near camera (`AppInc` 2.5, `MaxAppXZPos` 8, goal distance √(4.06² + 1²) ≈
4.18 m, `MinDist` 3.95, `MaxDist` 5.3, `RevOnApp` 2, automatic gearbox):

- **Backing away from a standstill** (brake key held, with or without steering): on the frame the
  automatic reverse engages, the car's throttle is still 0 and it is below 4 mph, so the parking
  brake makes the handbrake input 1 and *a* is zeroed (that frame only; from the next frame the
  brake key is the throttle). *a* then climbs back at 2.5 per second, reaching 8 after about 3.2 s
  (the far camera, `AppInc` 15, recovers in under a second). Meanwhile the camera hangs back and is
  dragged at up to 5.3 m from the track point by the distance clamp: the car looks a little smaller
  than usual but never runs away from the camera.
- **After 2 s of reverse throttle** the swing starts: θ goes from 0 to ±π at 2 rad/s (about 1.6 s),
  the camera circles the car, on its right side when *s* ≤ 0.1 (straight or steering left), else on
  its left, and ends in front of the car looking back past it along the direction of travel, still
  between 3.95 and 5.3 m from the track point. Steering only picks the side; it is read once, when
  the swing starts.
- **Leaving reverse** (releasing the brake key returns to first gear at once): θ goes back to 0 at
  `RevOffApp` (4 rad/s). If the swing had completed, θ is first set to −π (if *s* ≤ 0.1) or +π, which
  picks the side the camera returns by. If the swing had not started, nothing visible happens.
- **Handbrake held while in reverse** (J-turns): *a* stays at `AppInc`·dt (about 0.04 at 60 frames
  per second), so the camera stands nearly still and is only dragged by the distance clamp while
  the car spins.
- **Driving forwards** never triggers either effect: both need gear R. The only forward-driving
  cases are those where the automatic reverse engages: holding the brake key until below 5 m/s
  (the swing starts after 2 more seconds of holding it), or tapping it while crawling below 4 mph
  (one frame of *a* := 0, then the 1–3 s recovery above, even if the player accelerates forward
  again at once). Rolling backwards in a forward gear does nothing.

## Point-of-view cameras (bumper and dash)

### Keys

| Key | Default (no file) | Used for |
|---|---|---|
| `Offset` | 0, 1.6, 0.7 | camera position in car space |
| `Pitch` | 0 | tilt up (+) about the camera's own X, radians |
| `ReverseOffset` | 0, 1.7, 0.75 | position used only in a look-back mode that the game never turns on |
| `POVJitterAmp` | 0 | never used |
| approach keys (`ApproachOn`, `AppAppOn`, `AppXZPos`, `AppYPos`, `AppRot`, `AppXRot`, `AppApp`, `AppPosMin`, `AppRotMin`, `TrackTo`, `MaxDist`, `MinDist`, `LookAt`) | 1, 1, 28, 28, 28, 0.5, 0.7, 0, 0, (0, 0.8, 0), 1.8, 1.74, 0 | as for the chase camera |
| `LookAbove` | 0 | aim height used when `LookAt` ≠ 0 |
| `CameraFOV` | 60 | |

### Each frame

1. *tgt* := the car's pose with its position at *M*·`Offset`.
2. If `Pitch` ≠ 0: turn *tgt*'s axes by `Pitch` about *tgt.X* (position unchanged).
3. Look angle ψ = 2π × the look direction from the look keys: left 0.25, right 0.75, back 0.5,
   forward 0, back-left 0.375, back-right 0.625, forward-left 0.125, forward-right 0.875, none 0
   (left wins over right). If ψ ≠ 0, turn *tgt*'s axes by ψ about *tgt.Y* (0.25 = look left). The HUD
   is hidden while ψ ≠ 0.
4. Approach exactly as step 6 of the chase camera, with rate `AppXZPos` for x and z (no speed
   dependence), `AppYPos` for y, and the look-at target *P₀* + (0, `LookAbove`, 0). 35 of the 47
   shipped point-of-view files have `ApproachOn` 0 (camera rigidly fixed to the car); the others
   lag slightly (e.g. vpbullet's bumper: `AppXRot` 7.86).

No floor, ceiling or wall handling. If the screen's width/height is below 1.3, MM2 multiplies the
dash camera's `Offset.z` by 0.7352941 when loading it.

The dash view also shows a cockpit model (`geometry/<car>_dash.pkg` placed by
`tune/<car>_dash.asNode`): see `formats/dash.md`, which also covers the rear-view mirror.

## Orbit camera ("cross view")

A camera circling the car at fixed polar coordinates, no file. Values: height 2.5, distance 10,
azimuth 2.5 rad, incline 0.25 rad, not locked to the car's heading, field of view 50, near 0.1.
Pose: angles (pitch = −incline, heading = azimuth, roll 0) → axes (so it looks down at the car);
position = car origin + distance × *Z* + (0, height, 0). When locked to the car, heading = azimuth +
atan2(*M.Z.x*, *M.Z.z*). (A second orbit preset exists — distance 38.13, incline 0.321, locked to
the car's heading — but MM2 never lets the player select it.) While the orbit view is shown, six
keys move it, each frame they are held, with Δ = 2.0 and dt the frame time: Delete / Page Down turn
the azimuth by −/+ (dt·Δ)·0.3, End / Home change the incline by −/+ (dt·Δ)·0.3, Page Up / Insert
change the distance by −/+ 2·(dt·Δ); with either Shift key held the rates are dt·Δ for the angles
and (dt·Δ)·5 for the distance. Then the incline is clamped to [−π, π] and the distance to
[0.5, 200]. The changes stay for the rest of the game. Optional for our engine.

## Blending between cameras

When the view changes from camera *A* to camera *B* with duration *D* and easing type *E*:

- *B* is reset and updated once immediately. Remaining time *r* := *D*; goal γ := *A*'s `BlendGoal`
  (1 in every shipped file; other values are not specified).
- Each following frame both cameras update normally, then *r* −= dt. If *r* ≤ 0, the blend ends and
  *B* is shown from this frame on.
- Otherwise progress *p* = 1 − clamp(*r*/*D*, 0, 1). Field of view and near plane = *A*'s + (*B*'s −
  *A*'s)·*p*.
- Eased *e*: type 1: 1 − cos(*p*·π/2); type 2: sin(*p*·π/2); type 3: (1 − cos(*p*·π))/2.
- Pose: let *C* = the car's origin. For each of *A*, *B*: distance *d* = |position − *C*|, focus
  *F* = position − *Z*·*d* (the point *d* ahead of the camera), angles (pitch, heading, roll). Bring
  each of *B*'s angles within π of *A*'s: if *A* − *B* > π, *B* += 2π; if *A* − *B* < −π, *B* −= 2π.
  Blend distance, angles and focus linearly with *e*. The shown camera has the axes of the blended
  angles and position *F* + *d*·*Z* (with *Z* from the blended angles).

So a blend swings around the car rather than sliding in a straight line.

## Definitions

**Look-at pose** from *E* toward *Q*: *Z* = normalise(*E* − *Q*); *X* = normalise(*Z.z*, 0, −*Z.x*)
(always level); *Y* = *Z* × *X*; position *E*.

**Angles of a pose** (pitch *a*, heading *b*, roll *c*): *a* = asin(−*Z.y*), *b* = atan2(*Z.x*, *Z.z*),
*c* = atan2(*X.y*, *Y.y*). Back to axes, with *sa* = sin *a* etc.:

- *X* = (*cc*·*cb* + *sc*·*sa*·*sb*, *sc*·*ca*, *sc*·*sa*·*cb* − *cc*·*sb*)
- *Y* = (*cc*·*sa*·*sb* − *sc*·*cb*, *cc*·*ca*, *cc*·*sa*·*cb* + *sc*·*sb*)
- *Z* = (*ca*·*sb*, −*sa*, *ca*·*cb*)

**Approach step** of a value *u* toward *g* with rate *k*, soft zone *m* and its own speed *w*
(starting at 0):
1. δ = |*g* − *u*|; speed σ = δ, but σ = δ²/*m* if *m* ≠ 0 and δ ≤ *m*. (MM2's step also has an
   upper limit — σ = the limit when the limit is not 0 and δ exceeds it — but every camera passes
   limit 0, so it never applies.)
2. If the smoothing is on (`AppAppOn`): *w* := (σ − *w*)·`AppApp` + *w* (once per frame, not scaled
   by dt); then if *w* < 0 while σ > 0, or *w* > 0 while σ < 0, *w* := 0. The speed used is then
   σ := *w*. (*w* can only turn negative with an `AppApp` outside [0, 1]; every shipped file has 0.7
   or 1.0, so the reset to 0 never happens with shipped data, but mod files can reach it: a
   negative smoothed speed is not used to move the camera backwards, it stops it for that frame
   and the smoothing starts again from 0.)
3. Move *u* toward *g* by σ·*k*·dt, never past *g*.

## Checked against

- All 49 shipped `.camTrackCS` and 47 `.camPovCS` files parse with the keys above; no other keys.
- vpbug near chase: `Offset.y` 1.0 and `VertOffset` 1.0 give aim height 0.2, the same as the file's
  (ignored) `LookAbove` 0.2; far: (1.8 − 0.8) × 0.6 = 0.6, again equal to its `LookAbove`.
