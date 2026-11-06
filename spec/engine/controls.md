# Controls: from keys and pads to the car's inputs

How Midtown Madness 2 turns the player's keyboard or gamepad into the four inputs the vehicle
simulation reads every frame — **throttle** 0..1, **brake** 0..1, **steering** −1 (full left) .. +1
(full right), **handbrake** 0..1 — plus the gear, camera and other keys. The vehicle physics itself
(what the car does with those inputs) belongs to the vehicle spec; cameras are in `cameras.md`.

Units: metres, seconds, radians unless stated. `dt` is the simulated time of the current game frame.
"Car speed" *v* is the absolute value of the car's velocity along its forward axis, in m/s (the
vehicle spec computes it; it is never negative, also when reversing). "mph" = *v* × 2.236025 (MM2's
own factor, slightly off the exact 2.23694).

## Order of one frame

1. Read the devices: which keys/buttons are held, which were pressed this frame, analog axes.
2. Work out the steering value (keyboard or gamepad smoothing, below) and the pedals. If the pedals
   are currently swapped (automatic reverse, below), the throttle value is taken from the brake
   control and the brake value from the throttle control.
3. Quantise the four values (below).
4. Handle the press events (camera keys, gear keys, transmission and reverse toggles, …).
5. Hand the quantised values to the car: brake, throttle, steering, handbrake. Then run the
   automatic-reverse check.
6. Player overrides on the car's inputs (parking brake, disabled or wrecked car), then recompute
   the speed-sensitive steering rates (used by step 2 of the **next** frame), then the
   upside-down check (auto-righting), then the force-feedback update ("Force feedback"; only
   with a force-feedback controller).
7. The physics steps of this frame run with those inputs.

## Default keyboard bindings

MM2 offers five control devices (mouse, keyboard, joystick, gamepad, steering wheel) and keeps a
binding table for each. The keyboard device's defaults (the full table of all five devices is in
"Default binding table" below; the shipped `players/player0.cfg` contains it):

| Key | Action | Kind |
|---|---|---|
| Up arrow | accelerate (throttle) | held |
| Down arrow | brake; reverse when stopped (automatic gearbox) | held |
| Left / Right arrow | steer left / right | held |
| Space | handbrake | held |
| Enter | horn (siren on police cars) | held |
| C | next chase/bumper camera | press |
| V | orbit "cross" view on/off | press |
| D | cockpit (dash) view on/off | press |
| W | letterbox wide view on/off | press |
| Numpad 4 / 6 / 2 / 8 | look left / right / back / forward (bumper and dash views only) | held |
| T | switch automatic ⇄ manual gearbox | press |
| A / Z | shift up / down (manual gearbox only) | press |
| R | toggle reverse ⇄ first gear | press |
| H | gauges overlay (speed, gear, bar gauges) on/off; nothing else of the HUD. While the cockpit (dash) view is shown the overlay is hidden and H cannot turn it on; leaving the dash view brings it back if it was on | press |
| Tab | map mode | press |
| Q | full-screen map | press |
| E | map zoom | press |
| F | map orientation | press |
| Backspace | rear-view mirror on/off | press |
| S / X | next / previous waypoint (race modes) | press |
| 2 / 3 / 4 / 5 | CD player: show, play/stop, previous, next | press |
| I | opponent icons on/off | press |
| Y | chat (multiplayer) | press |

Pause (Esc) is handled outside the table. There is **no reset key** in MM2 (see "Getting back on the
wheels"). The milestone's reset key is our own addition; it should do what MM2's automatic righting
does. Engine choice: the reset key is **G** (keyboard) or the gamepad's Start (standard button
9); Backspace keeps MM2's meaning, the rear-view mirror (`formats/dash.md`).

"Held" controls are read as a state every frame. "Press" controls fire once when the key goes down.
If both steering keys are held, left wins.

`engine/driving.md` also asks for WASD driving. In MM2 those letters are wide view (W), shift up
(A), next waypoint (S) and dash view (D). If WASD drives (W = accelerate, S = brake, A/D = steer,
with exactly the arrow keys' behaviour), those four functions need other keys — an engine choice.

The rest of this document covers only the driving controls; map, HUD, mirror, CD and chat keys
have no driving effect.

## Pedals and handbrake on the keyboard

Throttle, brake and handbrake keys give exactly 1 while held and 0 otherwise — no ramp.

## Keyboard steering

The steering keys move an internal value *s* (starts at 0) toward a target, and the car gets a
shaped version of *s*.

Each frame:

1. Target *T*: −*K* while Left is held, else +*K* while Right is held, else 0. *K* = 1 (stored in
   the player file, below; always 1 in practice).
2. Rate: if |*s*| ≤ |*T*| **and** sign(*s*) = sign(*T*) (with sign(0) = 0), the value is moving out
   toward full lock: rate = *DeltaOut*. Otherwise (returning toward the centre, crossing over, or
   starting from exactly 0) rate = *DeltaIn*. Note the quirk: from rest (*s* = 0) toward a held key,
   the first frame uses *DeltaIn* because sign(0) ≠ sign(*T*); later frames use *DeltaOut*. Keep it.
3. Move *s* toward *T* by rate × dt, without passing *T*.
4. Output steering = sign(*s*) × |*s*|^*Filter*.

*DeltaIn*, *DeltaOut* and *Filter* depend on the car's speed ("speed-sensitive steering rates",
below).

## Speed-sensitive steering rates (`tune/<car>.asNode`)

Each player car has a text file `tune/<BaseName>.asNode` (datParser text: `type: a`, then
`asNode { … }` with `Key value` lines). A missing file or key keeps the default.

| Key | Default | Meaning |
|---|---:|---|
| `SpeedSensitive` | 2 | 0 = always use the Lo values, 1 = always Hi, 2 = blend by speed |
| `SpeedBaseLow` | 5 | blend speed bounds, m/s |
| `SpeedBaseHi` | 100 | |
| `DiscreteSteeringDeltaInLo` / `Hi` | 2.5 / 1.5 | *DeltaIn*, per second |
| `DiscreteSteeringDeltaOutLo` / `Hi` | 3.5 / 2.5 | *DeltaOut*, per second |
| `DiscreteSteeringFilterLo` / `Hi` | 2.0 / 1.0 | *Filter* exponent |
| `JoySensitivityLow` / `Hi` | 0.5 / 1.1 | joystick (below) |
| `JoySteerFilterLow` / `Hi` | 1.0 / 3.0 | joystick |
| `JoyApp`, `JoySteerApproachOutLo/InLo/OutHi/InHi`, `JoySteerAppApp` | 0, 10, 10, 2, 4, 0 | joystick approach mode ("Joystick and steering wheel"; on in 20 of the 21 shipped files) |
| `MouseSensitivityLow/Hi`, `MouseSteerFilterLow/Hi` | 0.6, 1.8, 1.5, 4.0 | mouse device |
| `WheelSensitivity…`, `WheelSteerFilter…`, `WheelApp`, `WheelSteerApproach…`, `WheelSteerAppApp` | as the Joy keys | steering-wheel device (`WheelApp` is 0 in every shipped file) |
| `ScoreWeight` | 1 | not a control |

Blend (mode 2), recomputed every frame after the car's inputs are set (step 6 of "Order of one
frame"), from the car speed *v*:

  *t* = clamp(*v*, SpeedBaseLow, SpeedBaseHi) / (SpeedBaseHi − SpeedBaseLow)

  X = X_Lo + (X_Hi − X_Lo) × *t*   for each of *DeltaIn*, *DeltaOut*, *Filter*.

This is MM2's formula, not the usual (v − Low)/(Hi − Low): at rest *t* = Low/(Hi − Low) (0.1263
with the shipped 5 and 44.6), and at the high bound *t* is slightly above 1. Keep it exactly.
In mode 2 only the selected control device's values are blended each frame (keyboard and gamepad:
the three Discrete values; joystick or wheel: the values of "Joystick and steering wheel"). Modes
0 and 1 set every device's values from the Lo or the Hi keys. One MM2 quirk in mode 0: the
joystick sensitivity is formed as *U* × `JoySensitivityLow` and **stored back** into
`JoySensitivityLow`, so with *U* ≠ 1 it grows or shrinks every frame. No shipped car uses mode 0
or 1 (all 21 files say 2), so an engine may simply not store it back.

*v* here is the same value the automatic reverse and the parking brake use: the unsigned speed
|Z · velocity| that the physics computed at the start of its **last** physics step (`vehicle.md`
step 2), i.e. in the previous frame's physics, because this frame's physics has not run yet. So the
rates that smooth frame *N*'s keys were computed in frame *N* − 1 from the speed of frame *N* − 2's
last physics step. After a car reset that speed is 0. Before the first recomputation (only the
very first frame) the rates are *DeltaIn* 1, *DeltaOut* 1, *Filter* 2.

Shipped cars use `SpeedBaseLow` 5, `SpeedBaseHi` 44.6 (100 for vpddbus) and their own Discrete
values, e.g. vpbug: DeltaOut 2.573 / 0.8, DeltaIn 5 / 5, Filter 1.2 / 1.2. vpcentury and vpdune
have no `.asNode` and use the defaults. `tune/default.asNode` exists but is empty and unused.

## Gamepad

MM2's "gamepad" device:

| Control | Default |
|---|---|
| Steering | stick X axis, analog |
| Accelerate | button 1 (held, gives 0 or 1) |
| Brake / reverse | button 2 (held, 0 or 1) |
| Horn | button 3 |
| Handbrake | button 4 |
| Dash view | button 5 |
| Wide view | button 6 |
| Shift down / up | buttons 7 / 8 |
| everything else | the keyboard keys above |

Stick reading: axis in −1..1 with a **dead zone** of 0.1 (the "Controller Dead Zone" option): inside
it the axis reads 0; outside, it is rescaled linearly so the edge of the dead zone reads 0 and full
deflection reads ±1: *x* = sign(*r*) × (|*r*| − 0.1) / 0.9 for raw *r*.

Steering on the gamepad uses the same smoothing as the keyboard, with the stick value as the
target: *T* = *x* (no *K*), same *DeltaIn*/*DeltaOut* rule (including the sign(0) quirk), same
speed-dependent rates, output sign(*s*) × |*s*|^*Filter*.

**Mapping to a modern pad (engine choice).** The Web Gamepad API "standard" layout: left stick X =
steering (above); A = accelerate, B = brake, X = horn, Y = handbrake as MM2's buttons 1–4, plus
right/left triggers as analog throttle/brake passed through unchanged (0..1, a dead zone of 0.05
is fine). MM2 itself had no analog pedals on a gamepad, so trigger use is our extension. Note that
the automatic reverse thresholds then matter: reverse engages only with the brake trigger above
0.8, and while reversing, easing the trigger below 0.8 drops straight back to first gear (with
MM2's joystick pedals the same happens).

## The game controller (joystick, gamepad and wheel devices)

Only needed if the engine offers these devices. MM2 uses **one** game controller, chosen once when
the program starts:

- It asks DirectInput (version 7) for the attached devices of the game-controller class (sticks,
  pads and wheels alike) and takes the **first one listed**; any others are ignored. DirectInput
  lists them in the Windows game-controller order (the "preferred device" first). There is no
  later search: a controller plugged in after start-up is not seen, and the game has no way to
  pick another one. **Engine choice:** the first joystick SDL reports at start-up; a menu to pick
  another controller would be our extension.
- The controller is opened for exclusive use while the game window is in front. Its automatic
  centring (the spring many wheels have built in) is switched **off** at once, whatever the
  force-feedback option says ("Force feedback").
- The joystick, gamepad and wheel devices all read this one controller; they differ only in
  their bindings and in how the steering is treated. Without a controller they fall back to the
  keyboard.

### What is read

Once per frame, while the selected device is the joystick, gamepad or wheel, MM2 reads four axes,
the first hat and the buttons. The binding table's components (`players/<name>.cfg`, below) give:

| Component | Value |
|---|---|
| 10 X | X axis: −1 (left) … +1 (right) |
| 11 Y | Y axis: −1 (forward, up) … +1 (back, down) |
| 12 Z | **minus** the Z axis |
| 13 U | always 0 (MM2 never reads a U axis) |
| 14 R | the Z-rotation axis (where many wheels report a pedal and sticks their twist), not negated |
| 15 V | always 0 |
| 16 hat | first hat: −1 when centred, else (36000 − *h*)/36000 with *h* its angle in hundredths of a degree clockwise from up (up 1, right 0.75, down 0.5, left 0.25) |
| 17, 18 | X left half: −X if X < 0, else 0; X right half: X if X > 0, else 0 |
| 19, 20 | Y up half: −Y if Y < 0, else 0; Y down half: Y if Y > 0, else 0 |
| 21 + *n* | button *n* + 1: 1 while held, else 0 (*n* = 0 … 11, so buttons 1 to 12) |

- **Scale.** MM2 has the driver report each axis as an integer *r* in −2000 … +2000 and uses
  *a* = (2·(*r* + 2000))/4000 − 1 in single precision (that is *r*/2000). Z and R get this range
  only on a controller with more than two axes that has them; otherwise MM2 reads a meaningless
  value (not a number) from them. Engine: 0.
- **Dead zone.** The "Controller Dead Zone" option *d* (0.1 by default, 0 … 0.33 in the menu) is
  handed to the driver for **X and Y only**, as trunc(*d* × 10000) parts in 10 000 of each half
  range: a raw position within *d* of the centre reads 0, and beyond it the reading is stretched
  linearly to reach ±1 at the end, *a* = sign(*a*₀)·(|*a*₀| − *d*)/(1 − *d*). Z, R and the hat have
  none. The driver applies it before MM2 sees the value.
- No other calibration (Windows' own applies before), no saturation setting, no inversion
  option. The only fixed inversions are the ones in the table (Z negated, the up and left halves).

**Axis-bound driving actions.** When a driving action is bound to an axis component:

| Action | Value used |
|---|---|
| steering axis | the component's value as it is (then the shaping below; on the gamepad the smoothing of "Gamepad") |
| throttle | max(value, 0), for the joystick and wheel devices only; on the gamepad device an axis bound to the throttle is never read (engine: throttle 0) |
| brake, handbrake | clamp(value, 0, 1) |
| look-around | clamp(value, 0, 1) |

A button gives 0 or 1 as in "Gamepad". In these three devices the steer-left and steer-right keys
are not used; only the steering axis steers.

**Press actions** bound to a button fire once when the button goes down. A press action bound to
a half axis (17 – 20) is a rare custom binding; MM2 fires it when the axis passes 0.75 on the
**opposite** half (a bug), once per push and at most every 0.8 s (a timer and a latch shared by
all such bindings). **Engine choice:** fire it when the bound half passes 0.75, once until it falls
back.

### Pedals

MM2 has no setting for combined or separate pedals. Its wheel defaults assume the pedals of the
time, **combined on the Y axis**: the accelerator pushes Y toward −1 and the brake toward +1, so
throttle = Y up half and brake = Y down half (pressing both cancels out). The joystick device uses
the same two halves of its stick.

A pedal bound to a whole axis (components 10 – 16) is read **straight**: the axis value of "What
is read" (Z already negated), with no calibration, no learning of where the pedal rests and no
re-scaling; the throttle then takes max(value, 0) and the brake clamp(value, 0, 1), i.e. the
positive half of the axis. A button bound to a pedal gives 0 or 1.

On the customise-controls screen an axis is bound by moving it more than 0.125 from where it was
when the screen started listening: X or Y binds the half it moved into, Z binds the whole Z (12),
R the whole R (14). With separate pedal axes that works badly: a pedal on Y binds a half and only
acts over the part of its travel beyond the centre; a pedal on Z or R binds the whole axis, whose
negative half the throttle and brake rules above cut off, and a pedal that rests at +1 reads as
fully pressed. **Engine choice:** for a controller whose pedals are separate axes (as SDL reports
most wheels), map each pedal over its whole travel to 0 … 1, taking the end where it rests (seen at
start-up) as 0; MM2's dead zone does not apply to pedals.

### Button count for the default bindings

Footnotes ¹ and ² of the default binding table use the **number of buttons the driver reports**
for this controller (DirectInput's button count of the first-listed controller), taken when the
program starts, before any driver's file is read: more than 5 gives the wheel device its button
choices for map, gauges, camera and mirror; more than 7 also its shift buttons. No controller: the
keyboard keys. **Engine:** SDL's button count of the joystick chosen above.

## Joystick and steering wheel

Joystick defaults: X axis steering; Y axis forward half = throttle, back half = brake (both
analog 0..1, dead zone as above); button 1 handbrake, 2 horn, 3 camera, 4 map (full table in
"Default binding table"). The steering of the joystick and wheel devices goes through this
shaping instead of the smoothing above.

**Per-car values** for the joystick (the wheel uses the same keys with `Wheel` instead of `Joy`),
from the car's steering file, blended by speed with *t* as in "Speed-sensitive steering rates"
(X = (X_Hi − X_Lo) × *t* + X_Lo):

| Name | Value |
|---|---|
| *S* | *U* × blend of `JoySensitivityLow` / `Hi`; *U* = 1 / (the "Steering Sensitivity" option, default 1) |
| *F* | blend of `JoySteerFilterLow` / `Hi` |
| *Out* | blend of `JoySteerApproachOutLo` / `Hi` |
| *In* | blend of `JoySteerApproachInLo` / `Hi` |
| *A* | `JoySteerAppApp` (not blended) |
| approach on | `JoyApp` ≠ 0 (a whole number) |

**State:** a hold timer *H* and an approach value *q*, both 0 at the start and shared by the two
devices.

Each frame, with *x* the steering-axis value (dead zone already applied) and dt the frame time:

1. If |*x*| > 0.99: *H* = *H* + dt; otherwise *H* = 0.
2. Target: *y* = clamp(*x* / *S*, −1, 1) if *S* ≤ 1 or |*x*| ≤ 0.99; otherwise (*S* > 1 and the
   axis in its last 1 %) *y* = sign(*x*).
3. **Normal** (approach off, or *H* = 0): *q* = *y*; steering =
   ((|*y*| · *S*)^*F* · (1/*S*)) · sign(*y*) (the reciprocal of *S* formed first, then multiplied).
4. **Approach** (approach on and *H* > 0):
   - rate = *A* · *H* + *In* if |*y*| < |*q*| or sign(*q*) ≠ sign(*y*) (going back toward the
     centre or across it), else rate = *A* · *H* + *Out*;
   - if *q* < *y*: *q* = rate · dt + *q*, but not above *y*; if *q* > *y*: *q* = *q* − rate · dt,
     but not below *y*;
   - steering = |*q*|^*F* · sign(*q*) (no *S* here).
5. Clamp the steering to −1 … 1.

sign(0) = 0. The power is the C library's `pow`. The result goes on to "Quantisation".

What this does: with *S* > 1 the axis is scaled down, steering = |*x*|^*F* / *S* up to
|*x*| = 0.99, so a stick with *S* = 2.5 reaches only about 40 % lock; in the last 1 % of travel the
target jumps to full lock. Without approach (every wheel in the shipped files) the steering snaps
there to *S*^(*F* − 1), clamped (full lock when *F* ≥ 1). With approach (`JoyApp` 1 in 20 of the 21
steering files) it ramps from where it was toward full lock at *Out* + *A*·*H* per second, comes back at
*In* + *A*·*H* if the stick is thrown straight to the other end, and returns at once to the normal
value as soon as the stick leaves the last 1 % (*H* = 0). The switch from step 3 to step 4 is not
continuous (step 3 has the 1/*S* factor, step 4 none): keep it. With *S* < 1 the target reaches
±1 at |*x*| = *S* and the steering then stays at *S*^(*F* − 1): vpddbus (`JoyApp` 0) tops out at
0.94 at rest and 0.82 at 20 m/s. The wheel values of the shipped files often give *F* < 1 and
*S* > 1, so a wheel reaches only |*x*|^*F*/*S* before its last 1 % (vpbug at rest: 0.35, then 0.63
there); that is MM2's wheel steering.

## Mouse

The mouse device steers by **where the pointer is** across the game window, not by how it moves:

1. *p* = *px* · (1/*W*): *px* is the pointer's x in window pixels (0 at the left edge) as of the
   last mouse message the window received (a move or a button press or release), *W* the width of
   the game's picture in pixels; the reciprocal is formed once, the product in single precision.
2. *x* = ((*p* + *p*) − 1) / *M*, with *M* = *U* × the blend of `MouseSensitivityLow` / `Hi` (by
   *t* as in "Speed-sensitive steering rates"; Lo or Hi in modes 0 and 1). The middle of the
   window is straight ahead.
3. steering = clamp(|*x*|^*G* · sign(*x*), −1, 1), *G* = the blend of `MouseSteerFilterLow` / `Hi`;
   the power is the C library's `pow`. Then "Quantisation".

There is no dead zone and no smoothing, and the steer keys are not used. A larger *M* means less
lock: every shipped car has *M* > 1, so at the window's edge the steering stays below full lock
and shrinks with speed (vppanoz: 0.76 at rest, 0.50 at 20 m/s, 0.20 at 44.6 m/s). While a mouse
button is held the window keeps receiving the pointer's position beyond its edges, so the
steering can go further there (to full lock when |2*p* − 1| ≥ *M*). Throttle and brake are the
left and right buttons, 0 or 1. A binding of the mouse's Y axis (component 11) reads the same way
from the pointer's y and the picture's height. MM2's remembered position is (0, 0) until the
pointer first moves over the window, which steers full left; **engine choice:** start in the
middle.

## Quantisation

Before the car receives them, MM2 stores the four values in bytes (for replays) and the car reads
them back from there:

- steering: *q* = trunc(127 × steering) (toward zero, a signed byte), car gets *q* × 0.007874016;
- throttle, brake, handbrake: *q* = trunc(255 × value), car gets *q* × 0.003921569.

(In a network game the throttle the car gets is then capped in forward gears: `../game/multiplayer.md`, 1.5.)

The two factors are the single-precision numbers nearest 1/127 and 1/255, multiplied (not divided),
with the product rounded to single precision. So a full pedal reaches the car as exactly 1.0
(255 × 0.003921569 = 1.000000059, which rounds to 1.0), 0.5 becomes 127 × 0.003921569 =
0.49803925, and full steering stays ±1.
Check runs (`drive-check.md`) go through this step too. The physics spec writes the same control
step out in `physics/vehicle-helpers.md`, "Player controls"; the two describe one rule.

## Automatic reverse (automatic gearbox only)

Gear numbers here: R = reverse, N = neutral, 1, 2, … forward (the vehicle spec's gearbox).
Active when the gearbox is automatic and the "auto reverse" option is on (default on). The
inputs used are the quantised values just given to the car (throttle and brake as the car sees
them, i.e. after any swap).

After the inputs are set each frame:

- If the gear is a forward gear, *v* < 5.0, brake > 0.8 and throttle < 0.1: **swap the pedals**
  and select R.
- Else if the gear is R and throttle < 0.8: **unswap** and select first gear (selecting first
  happens only from R or N).

While swapped (from the next frame on), the brake key drives the car's throttle and the
accelerate key drives its brake. So: hold Down at (nearly) a standstill and the car shifts into
reverse and then backs up with Down; let go of Down and it immediately goes back to first gear
(Up now accelerates forward, braking a backward roll). Note that *v* is unsigned: braking hard
below 5 m/s while still rolling forward also engages reverse.

The swap is also cleared when the gearbox mode is toggled (T: if swapped, first select first gear)
and by the R key.

## Gearbox keys

- **T**: toggle automatic ⇄ manual (see the swap rule above). The starting mode is the
  "Transmission" option (default automatic).
- **A / Z** (manual only; ignored in automatic): one gear up / down along R, N, 1, 2, …, top. Up
  stops at the top gear (the car's manual gear count), down stops at R.
- **R**: if the gear is not R, select R; if it is R, select first gear. This changes the gear
  directly and also clears the pedal swap. Works in both modes.

Automatic shifting itself is part of the vehicle spec.

## Player overrides on the car's inputs

Applied after the inputs are set, every frame:

1. If the player is disabled (e.g. after a race ends): throttle 0, brake 1, steering −1.
2. If damage is enabled and the car is wrecked: throttle 0, brake 0, steering 0
   (`../physics/damage.md`).
3. **Parking brake**: if mph < 4 and the car's throttle is exactly 0, handbrake = 1. (Keeps a
   stopped car from creeping; it also means coasting below 4 mph with no throttle applies the
   handbrake.) "The car's throttle" is the value just given to the car, i.e. after the reversing
   swap. So while backing up with the brake key the parking brake is off; it is on for the single
   frame in which the automatic reverse engages from below 4 mph (that frame the car's throttle is
   still the accelerate key, 0). The value set here is the car's handbrake input for the whole
   frame: the physics and the chase camera (`cameras.md`, step 2) both see it.

## Speed-sensitive steering inside the car

Separately from the key smoothing, the car scales its steering by a factor from its
`.vehcarsim` keys `SSSValue` and `SSSThreshold`: factor = 1 if SSSThreshold = 0; otherwise
1 + (*v* / SSSThreshold) × (SSSValue − 1) below the threshold and SSSValue at or above it. The
vehicle spec applies it; every shipped car except vpvwcup_Angel has SSSThreshold 0 (factor 1).

## Getting back on the wheels

MM2 has no reset key. It rights the car automatically:

- Each frame, if the car's up axis has a vertical component below 0.1 (lying on its side or roof),
  the car is in contact with something (a contact flag the collision code keeps), and *v* < 4.5,
  a timer grows by dt; otherwise the timer is 0.
- When the timer exceeds 1.0 s: the car is reset in place — placed upright at its current position
  raised by 0.5 m, with its current heading (heading = atan2(Zx, Zz) of its backward axis Z, i.e.
  0 when facing −Z, positive turning left), velocities and inputs zeroed. Its damage is kept.
  The car's stored start position is not changed by this.

Our reset key does the same action immediately. (The vehicle's own unstick helper from
`tune/vehicle/<car>.vehStuck` — turning a car stuck against a wall, rolling an upturned car when
the player steers — belongs to the vehicle spec.)

## Force feedback

What MM2 makes a force-feedback stick or wheel do. Everything here happens only while force
feedback is **active**:

- the force-feedback option of the driver's profile is on (`players/<name>.cfg`; a new profile
  has it on), and
- the control device is the **joystick or the wheel** (never the gamepad, mouse or keyboard: MM2
  has no gamepad rumble), and
- the game controller reports force feedback.

### Strengths

*C* = collision strength × the car's collision gain; *R* = road strength × the car's road gain.
The two strengths are the profile's options (menu range 0 … 2, default 1; a value read from a
profile is used as it is). MM2 keeps a collision and a road gain per car but no file sets them:
both are always 1, so *C* and *R* are just the two options.

### The four effects

When the controller is opened at start-up (after its automatic centring was switched off, "The
game controller"), MM2 creates four effects on it if it reports force feedback. One the driver
refuses simply does nothing later.

| Effect | Kind | Axes | Length | Fixed parameters |
|---|---|---|---|---|
| friction | friction (condition) | steering axis X | endless | centre 0, dead band 0, full saturation both sides; the coefficient is set later |
| spring | spring (condition) | X | endless | as the friction: centred, coefficient set later |
| rumble | periodic | X | endless | offset 0, phase 0, no envelope; magnitude and period set later (created with 2000 and 5 s) |
| jolt | periodic | X and Y, direction given as an angle | 0.1 s | magnitude 10000, offset 0, phase 0, period 2 s; strength and angle set for each jolt |

Units: forces and coefficients 0 … 10000 (10000 = the device's full force), times in
microseconds, angles in hundredths of a degree. Each effect's overall gain is 10000 except the
jolt's, which carries the jolt strength. The rumble and the jolt use the **first periodic
waveform the driver offers** (MM2 asks for "a periodic effect", not a particular wave; DirectInput's
usual list puts the square wave first). A square wave at phase 0 pushes at full magnitude for the
first half of its period, so the jolt is a 0.1 s push.

### Sending values

Each effect gets new values through one rule. Products are single precision and every trunc is
toward zero.

- **Friction(*v*)**: coefficient = clamp(trunc(*v* × 10000), 0, 10000), the same on both sides.
  Not scaled by *R*, and played even when both strengths are 0.
- **Spring(*k*)**: coefficient = clamp(trunc(*R*) × trunc(*k* × 10000), 0, 10000) on both sides.
  *R* is cut to a whole number first: a road strength below 1 gives no centring force, 1 to 1.99
  the plain value, 2 double. With *R* = 0 the spring is neither set nor started.
- **Rumble(*P*, *M*)**: period trunc(*P* × 1 000 000) µs, magnitude trunc(*R* × trunc(*M* × 10000))
  (*R* not cut here; above 10000 when *R* > 1, which the device limits). With *R* = 0 the rumble is
  neither set nor started.
- **Jolt(*m*, *angle*)**: strength = trunc(*C* × trunc(*m* × 10000)), direction =
  clamp(trunc(*angle*), 0, 360) × 100, then the jolt is started (once; a running one starts over).
  With *C* = 0 nothing happens. The direction is a polar angle on the X–Y plane: 0 = from straight
  ahead, 9000 = from the right, as DirectInput (and SDL) count the direction a force comes from.
  On a wheel only its sideways share acts, so a jolt kicks the wheel left or right with a random
  share of its strength.

### Each frame

At the end of the player's frame update (after the controls were applied and the overrides, so
before this frame's physics; "Order of one frame", step 6). It reads:

- *now*: a real-time clock in seconds (the sum of the unclamped frame times; it runs on while
  paused); *J*: the time of the last impact jolt (0 after a reset);
- *v*: the car speed in m/s (unsigned, as for the steering rates) and its speedMPH;
- the damage share *d* = clamp((damage − `MedDamage`)/(`MaxDamage` − `MedDamage`), 0, 1)
  (`../physics/damage.md`);
- from the **front-left wheel** (`../physics/vehicle-wheels.md`): `matFric` (after the wall,
  weather and `CarFrictionHandling` adjustments of "Surface"), its material's bump height `bumpH`
  and bump width `bumpW`, its radius, `latSlip` and `longSlip`; these keep the values the wheel
  last had while grounded;
- every wheel's compression rate `rate` ("Suspension update") and how many wheels were grounded in
  their last tyre update.

Remembered between frames, all 0 after a reset: the last friction input *Lf*, the last bump height
and width *Lh*, *Lw*, the last spring value *Lk*. The rumble also remembers the period and
magnitude it last sent.

1. If the game is paused: the force-feedback reset (below), and nothing else this frame.
2. If the friction is not playing, start it; if the spring is not playing, start it.
3. **Friction:** if `matFric` ≠ *Lf*: Friction(0.7 × `matFric`), *Lf* = `matFric`.
4. **Rumble**, by damage:
   - *d* > 0 (damaged beyond `MedDamage`): if at least one wheel is grounded and *now* − *J* > 0.5:
     rumble request with *f* = *v* / (radius × 10) and *m* = *d* × 0.4 (radius × 10 formed first).
     If no wheel is grounded: stop the rumble if it plays. (Grounded within 0.5 s of an impact
     jolt: the rumble is left as it is.)
   - otherwise: if `bumpH` = *Lh*, `bumpW` = *Lw* and *v* = 0, nothing (MM2 compares *v* with a
     remembered speed it never updates from 0). Else *Lh* = `bumpH`, *Lw* = `bumpW`, and if
     `bumpH` ≠ 0, `bumpW` ≥ 0.01 and *v* ≥ 1: rumble request with *f* = *v* / `bumpW` (bumps per
     second) and *m* = `bumpH`; else stop the rumble if it plays.

   **Rumble request(*f*, *m*):** if *f* < 1: stop the rumble if it plays, and nothing else.
   Otherwise start it if it is not playing (not while paused); *P* = 1 / clamp(*f*, 1, 20) (the
   period in seconds) and *M* = clamp(*m*, 0, 1); if *P* or *M* differs from the pair last sent,
   Rumble(*P*, *M*) and remember the pair.
5. **Spring:** *k* = 0; if |`latSlip`| < 0.05 and |`longSlip`| < 0.05: *k* = 1 when *v* > 80,
   else when *v* > 10, *k* = (*v* − 10)/70. If |*Lk* − *k*| > 0.01: Spring(*k*), *Lk* = *k*.
   (So the wheel has no centring below 10 m/s or while the front-left tyre slips, and full
   centring from 80 m/s.)
6. **Wheel jolt:** if any wheel's compression rate is above 1.3 (m/s; tested front right, front
   left, back right, back left), speedMPH > 5 and *now* − *J* > 0.5: draw a random number *r*
   (0 ≤ *r* < 1) and Jolt(*m*, *r* × 360) with *m* = 20 × 0.01 (0.19999999 in single precision,
   so trunc(*m* × 10000) = 1999). *J* is not changed, so this repeats every frame while a wheel
   keeps compressing that fast (landing a jump, hitting a kerb).

So the update runs once per frame, but values reach the device only when they change: the friction
when the front-left wheel's `matFric` changes, the spring when its value moves by more than 0.01,
the rumble when its period or magnitude changes; a jolt is sent and started each time it fires.

### Impact jolts

Whenever the player's car **counts** an impact (`../physics/damage.md`, "Per impact report",
step 5: so only with the car's damage enabled, above `ImpactThreshold`, and at 10 mph or more or
against a moving body), right then during the physics: if force feedback is active, speedMPH > 5
and *now* − *J* > 0.5: draw *r*, set *J* = *now*, and Jolt(*m*, *r* × 360) with
*m* = clamp(`total` × 0.01, 0.1, 1) × 1.0, `total` being the impact slot's total after this
impact (a new slot: the impact strength *s*).

### Reset, pause and pop-ups

- **Force-feedback reset** (at every reset of the player's car, and every frame while the game is
  paused): stop the rumble, start the friction and the spring (starting over if they play) and
  set *Lf*, *Lh*, *Lw*, *Lk* and *J* to 0.
- When one of the in-game pop-up screens opens (the Esc menu, chat, the results and the other
  pop-ups of `../game/menus.md`): stop the rumble, the spring and the friction; a running jolt
  ends by itself. While the game then stays paused, the reset above starts friction and spring
  again every frame with their last values, so the wheel stays stiff; rumble and jolts stay off.
- MM2 stops nothing else. DirectInput itself silences the device when the window loses the
  front. **Engine choice:** stop all effects when no game is running and when the window loses
  focus.

### Random numbers

The jolt angle is drawn from MM2's game-wide generator (`../physics/collision.md`, "Random
numbers"), one draw per jolt, so with a force-feedback controller the jolts shift that sequence.
**Engine choice:** give force feedback its own generator (as for visual effects), so it never
changes the draws the physics uses.

### Fixed values

| Value | Use |
|---|---|
| 0.5 s | spacing after an impact jolt (impact and wheel jolts, damage rumble updates) |
| 0.1 s, 2 s | jolt length and period |
| 5 mph | minimum speed for jolts |
| 0.01, 0.1 … 1 | impact jolt strength = `total` × 0.01, limited to 0.1 … 1 |
| 1.3, 0.2 | wheel jolt: compression-rate limit and strength |
| 0.7 | friction coefficient per unit of `matFric` |
| 0.05 | slip limit for the spring |
| 10, 80 m/s | spring: 0 up to 10 m/s, 1 from 80 m/s, linear between |
| 0.01 | change of the spring value that is sent |
| 1 … 20 Hz | rumble frequency range |
| 10, 0.4 | damage rumble: *f* = *v* / (10 × radius), *m* = 0.4 × *d* |

### Driving SDL's haptic interface (engine choice)

- Values: a force or coefficient *c* of 0 … 10000 becomes *c* × 32767 / 10000; full saturation
  0xFFFF; times in milliseconds (*P* × 1000 for the rumble period, 100 for the jolt, 2000 for its
  period); endless = `SDL_HAPTIC_INFINITY`.
- Friction and spring: `SDL_HAPTIC_FRICTION` and `SDL_HAPTIC_SPRING` with equal left and right
  coefficients, centre 0, dead band 0, on the steering axis (`SDL_HAPTIC_STEERING_AXIS`, or a
  one-axis cartesian direction).
- Rumble and jolt: `SDL_HAPTIC_SQUARE` where supported, else `SDL_HAPTIC_SINE`. The jolt's
  magnitude is its strength (MM2 gives magnitude 10000 and puts the strength in the effect's gain,
  which multiplies it); direction `SDL_HAPTIC_POLAR` with the angle × 100 (same convention).
- Change parameters with an effect update only when MM2 would send them; start and stop as above.
- Autocentre off (`SDL_SetHapticAutocenter(0)`) when the controller is opened, as MM2 does. With
  the force-feedback option off MM2 leaves the wheel without any centring; turning the device's
  autocentre back on in that case is an acceptable engine choice.
- Gamepad rumble is **not MM2**. If the engine offers it, it is our extension: for example the
  jolts as a short rumble of both motors at the jolt strength (100 ms) and the rumble effect as the
  low-frequency motor at *M* × *R*.

## Player car setup notes for the vehicle spec

- MM2 sets up the **player's** vpcop a second time under the name `vpmustang99` (a command-line
  switch turns this off): the tuning file `tune/vehicle/vpmustang99.vehcarsim` **and the wheel
  pivots** `geometry/vpmustang99_whl0…3.mtx` replace vpcop's, while the drawn model, the collision
  bound and `vpcop.vehGyro`, `.vehStuck`, `.vehCarDamage` stay. Opponent and police vpcops are not
  affected. The full rule is `physics/vehicle-tune.md`, "Which files".
- The gearbox starts in first gear after a reset.

## Free-roam start (Cruise mode)

Cruise mode does not have a fixed start point. MM2 drops the car at a **random intersection of the
city's AI road network**, `city/<city>.bai` (intersection records: `city-files.md`, "AI network").

1. Pick an intersection index *i* = (random 15-bit number) mod (*NI* − 1) + 1, *NI* = the number
   of intersections (so intersection 0 is never used). MM2's random numbers come from its global
   generator (state × 214013 + 2531011, mod 2³²; result = (state >> 16) & 0x7FFF), whose state at
   that moment depends on everything that ran before, so the choice is effectively random.
2. Reject it and pick again if the room that contains the intersection's position (normally the
   room number stored in the record; these are the PSDL's intersection rooms)
   - is enclosed (PSDL flag 0x02), or
   - is deep water (`psdl.md`, "Deep water"), or
   - contains a landmark (an `.inst` record with bit 24 in that room), or if
   - any road meeting there has bit 0x02 or 0x04 set in its flags word.
3. Position = intersection position + (0, 2, 0); heading 0 (facing −Z, north). The car is placed
   there at rest with its inputs zeroed and falls the last 2 m onto the street. (How the car's centre
   of gravity relates to this point belongs to the vehicle spec's placement rule.)
4. A city without an AI network: position (0, 20, 0), heading 0.

Shipped data: London has 328 intersections of which 262 pass the tests, SF 214 and 178.

**Drive mode** should use the same eligible set. Pick one at random (MM2-like), or, for repeatable
starts, the eligible intersection with the lowest index ≥ 1 (London: intersection 10 at
(−230.0, −0.1, 333.5), room 1023; SF: intersection 2 at (−995.5, 42.5, 1291.7), room 963).

## Options in `players/<name>.cfg`

MM2 keeps per-player options in a binary file `players/<name>.cfg` (the shipped one is
`player0.cfg`, 2,229 bytes). Little-endian. Only the controls part is described:

| Size | Contents |
|---|---|
| u32 | 1234 |
| u32 | version; the rest is read only if it is 31 |
| f32 | sound-effects volume (1.0) |
| f32 | music volume (1.0) |
| f32 | left/right balance, −1 … 1 (0.0) |
| u32 | audio option bits (sound quality, channel choice and other sound settings; no driving effect; meanings in `../audio/sound-system.md`, "Options in the player file") |
| u32 | number of sound channels (8, 16 or 32; used as a 16-bit value) |
| u32 *n*, *n* bytes | audio device name |
| 56 | other options |
| u32 | control device: 0 mouse, 1 keyboard, 2 joystick, 3 gamepad, 4 wheel (2–4 fall back to keyboard if no such device is connected) |
| u32 | gearbox: 1 automatic, 0 manual |
| f32 | "physics realism" (unused by the game) |
| u32 | auto reverse: 1 on |
| u32 | joystick hat ("point of view") switch of the controls screen, offered only for a joystick with a hat (no driving effect; `../formats/profiles.md`) |
| u32 | force feedback on |
| f32 | force-feedback collision strength |
| f32 | force-feedback road strength |
| f32 | the mouse device's divisor *M* ("Mouse"; rewritten by the game every frame; ignore) |
| f32 | controller dead zone (0.1) |
| f32 | unused |
| f32 | keyboard steering target *K* (1.0) |
| f32 | steering sensitivity factor *U* = 1 / "Steering Sensitivity" (1.0) |
| 170 × 12 | binding table: for device *d* (0–4) and action *a* (0–33), entry *d* × 34 + *a* = u32 kind (1 held, 2 analog, 3 press), u32 input device (2 mouse, 3 keyboard, 4 joystick/pad), u32 component (DirectInput keyboard scan code; mouse: 1 left button, 2 right button, 10 X axis, 11 Y axis; joystick: 10 X, 11 Y, 12 Z, 13 U, 14 R, 15 V, 16 hat, 17/18 X left/right half, 19/20 Y up/down half, 21 + *n* = button *n* + 1) |
| 12 | view settings, one byte each (below) |

**View settings** (the last 12 bytes):

| Byte | Meaning | New profile | Shipped `player0.cfg` |
|---|---|---|---|
| 0 | external camera entry: 0 near chase, 1 bumper, 2 far chase (`cameras.md`) | 0 | 0 |
| 1 | HUD map mode: 0 off, 1 small corner map, 2 half screen, 3 full screen | 0 | 0 |
| 2 | wide (letterbox) view on | 0 | 0 |
| 3 | dash view on | 0 | 0 |
| 4 | rear-view mirror on when a game starts (`formats/dash.md`) | 0 | 0 |
| 5 | gauges overlay on (the H key) | 0 | 1 |
| 6 | orbit view on (written, never used: a reset always leaves the orbit view) | — | 0 |
| 7 | opponent icons on (the I key) | 1 | 1 |
| 8, 9 | unused | 0, — | 0, 0 |
| 10 | map orientation (the F key) | — (program default 1) | 1 |
| 11 | map zoom (the E key) | — (program default 0) | 0 |

"—": a new profile does not set the byte; the value the program started with stays (bytes 0–3 are
also 0 and 10, 11 are 1 and 0 before any profile is loaded). When a profile is loaded, these bytes
become the current view settings. While playing, the game keeps them up to date as the player
changes the camera, map mode, wide view, dash view, gauges, icons, map orientation and zoom, and it
writes them back into the file when a game ends and when it moves on to the next race. The mirror
is the exception: Backspace switches the mirror for the current game only and does not change
byte 4. Where they take effect: bytes 0, 2, 3 and 5 at every reset (`cameras.md`, "Switching
views"); byte 1 when the map is reset (so the map starts **off** with the shipped file); byte 4
when the game sets up its HUD; byte 7 when the opponent icons are set up (some race modes force
them on); bytes 10 and 11 whenever the map is drawn. Byte 11 is replaced at every game start by
the city's `ZoomIn` setting, and byte 1 may also be 3 (a game left in the full-screen map starts
in it); see `../game/hudmap.md`.

### Default binding table

The actions (0 – 33), the `mmlang.dll` text that names each one on the customise-controls screen
(`../formats/mmlang.md`; the screen lists the actions in this order, 0 to 33), and the default
input of each device. Keys are named by their key (DirectInput scan code in brackets where it is
not obvious); "button *n*" and the axes are the game controller's (input device 4), "mouse …" the
mouse's (input device 2), every other entry a keyboard key (input device 3).

| Action | Text | Mouse (0) | Keyboard (1) | Joystick (2) | Gamepad (3) | Wheel (4) |
|---|---:|---|---|---|---|---|
| 0 map mode | 296 | Tab | Tab | button 4 | Tab | button 5 ¹ |
| 1 full map | 298 | Q | Q | Q | Q | Q |
| 2 map zoom | 299 | E | E | E | E | E |
| 3 map orientation | 300 | F | F | F | F | F |
| 4 gauges overlay | 297 | H | H | H | H | button 4 ¹ |
| 5 steering axis | 282 | mouse X axis | the main-row 8 key (9) | X axis | X axis | X axis |
| 6 steer left | 283 | Left | Left | Left | Left | Left |
| 7 steer right | 284 | Right | Right | Right | Right | Right |
| 8 throttle | 280 | left mouse button | Up | Y axis, up (forward) half | button 1 | Y axis, up half |
| 9 brake | 281 | right mouse button | Down | Y axis, down half | button 2 | Y axis, down half |
| 10 handbrake | 307 | Space | Space | button 1 | button 4 | button 1 |
| 11 camera | 276 | C | C | button 3 | C | button 3 ¹ |
| 12 cross view | 277 | V | V | V | V | V |
| 13 horn | 279 | Enter | Enter | button 2 | button 3 | button 2 |
| 14 – 17 look left / right / back / forward | 286, 285, 287, 288 | numpad 4, 6, 2, 8 | same | same | same | same |
| 18 wide view | 289 | W | W | W | button 6 | W |
| 19 dash view | 290 | D | D | D | button 5 | D |
| 20 gearbox mode | 278 | T | T | T | T | T |
| 21 shift up | 291 | A | A | A | button 8 | button 7 ² |
| 22 shift down | 292 | Z | Z | Z | button 7 | button 8 ² |
| 23 reverse | 293 | R | R | R | R | R |
| 24 next waypoint | 294 | S | S | S | S | S |
| 25 previous waypoint | 295 | X | X | X | X | X |
| 26 – 29 CD show, play/stop, previous, next | 301, 302, 304, 303 | 2, 3, 4, 5 | same | same | same | same |
| 30 mirror | 305 | Backspace | Backspace | Backspace | Backspace | button 6 ¹ |
| 31 look-around axis | 306 | hat | hat | hat | hat | hat |
| 32 opponent icons | 308 | I | I | I | I | I |
| 33 chat | 309 | Y | Y | Y | Y | Y |

¹ Only if the game controller MM2 found at start-up has more than 5 buttons (the count its
driver reports; "Button count for the default bindings"); otherwise the
keyboard key (Tab, H, C, Backspace). ² Only with more than 7 buttons; otherwise A and Z. The
shipped `player0.cfg` has the keyboard keys in all six places.

The kind of each entry follows from the action: actions 5 – 10, 13 – 17 and 31 are **held**
actions (a key or button gives kind 1, an axis or half-axis or the hat kind 2); every other action
is a **press** action (kind 3) and takes only keys and buttons. The look-around axis is the
controller's hat on every device. On the keyboard device the steering-axis entry is not used
(that device steers with actions 6 and 7, "Keyboard steering").

MM2 builds this table for all five devices when it starts, before any driver's file is read, and
again when the customise-controls screen's Defaults is chosen; a driver's `.cfg` then replaces it,
and a new driver takes the table in effect (`../formats/profiles.md`).

The engine need not read this file; its defaults are the table above. If it does, the shipped
file selects the gamepad device with automatic gearbox and auto reverse on.

## HUD values

What MM2's driving HUD shows (our HUD looks different, the values are these):

- **Speed**: mph, truncated to a whole number: trunc(*v* × 2.236025). MM2 has no km/h setting.
  (A km/h display, if we add one, is *v* × 3.6.)
- **RPM**: a bar filled to engine RPM / `MaxRPM`. RPM is the engine's angular velocity × 60/(2π);
  `IdleRPM` and `MaxRPM` come from the `Engine` block of the car's `tune/vehicle/<car>.vehcarsim`
  (vpbug: idle 750, max 8500).
- **Gear**: one character: `R` in reverse, `P` in neutral (MM2 shows P, never N), `D` in any forward
  gear while automatic, else the forward gear number 1–8.
- **Damage** bar (out of scope).

## Checked against

- Default bindings decoded from the binding builder for all five devices; the shipped
  `orig/players/player0.cfg` holds exactly those defaults (device 3, automatic, auto reverse on,
  dead zone 0.1, *K* 1, *U* 1). `tools/game/profiles.py` rebuilds the default binding table
  above (with the kind rule) and finds all 170 entries of the shipped file equal to it, the wheel
  in its keyboard-key variant (`work/check/profiles.txt`).
- All 22 shipped `tune/vp*.asNode` files parsed; keys listed above are the only ones.
- `vpbug` at rest: *t* = 5 / 39.6 = 0.1263, *DeltaOut* = 2.573 + (0.8 − 2.573) × 0.1263 = 2.349,
  *DeltaIn* = 5, *Filter* = 1.2.
- `tools/game/controls_ff.py` (`work/check/controls_ff.txt`): the 21 car steering files
  (`tune/vp*.asNode` without the `_dash` camera files) all use `SpeedSensitive` 2; `JoyApp` is 1 in
  all but vpddbus, `WheelApp` is 0 in all. Typical joystick values (vppanoz, vpbug, vpcop): *S*
  2.5, *F* 1.0126 at rest to 1.1126 at 44.6 m/s, *Out* 2.572, *In* 1.0, *A* 0.127; vpcoop and
  vpcoop2k: *S* 5.54 at 20 m/s, *Out* = *In* = 1, *A* 0. Approach run at 20 m/s and 60 frames/s
  (stick 0.5, then pinned at +1): vppanoz steering 0.1931 (normal), then 0.2261, 0.2683 … 0.8855
  after 0.25 s and full lock on the 19th pinned frame (0.32 s); thrown to −1 it comes back at about
  1.1 per second (0.718 after 0.25 s) and drops to 0 the frame the stick is released; with the
  approach off it snaps from 0.1931 to 1. vpcoop ramps at 1 per second (0.589 after 0.5 s). Normal
  rule tops: vpddbus joystick 0.9356 at rest and 0.8212 at 20 m/s (= *S*^(*F* − 1)); vpbug wheel at
  rest 0.3460 at |*x*| = 0.99 and 0.6305 at full deflection; vpcop wheel at 44.6 m/s 0.3709, then
  1.
- Force feedback from `city/materials.mtl` (8 materials): friction coefficient
  trunc(0.7 × friction × 10000) = 4550 (deepwater 0.65), 4760 (water 0.68), 5250 (dirt 0.75),
  6300 (grass, sand, cobblestone, `_default` 0.9), 6649 (wood 0.95: 0.7 × 0.95 rounds below
  0.665), 7000 for MM2's built-in default material (friction 1). Rumble only on deepwater, grass,
  dirt and sand (bump height ≠ 0 and width ≥ 0.01): magnitude 600, 400, 100, 500 (× *R*); grass
  (width 0.45) at 2 m/s gives 4.44 Hz, period 225000 µs, and from 9 m/s on the 20 Hz limit
  (period 50000 µs).
- Spring coefficient with *R* = 1: 0 up to 10 m/s, 1428 at 20, 5000 at 45, 10000 from 80 m/s;
  *R* = 0.5 gives 0 everywhere, *R* = 2 gives 2856 at 20 m/s. Impact jolt strength with *C* = 1:
  1000 (total 5, the 0.1 floor), 5000 (total 50), 10000 (total ≥ 100); wheel jolt 1999
  (20 × 0.01 rounds to 0.19999999 in single precision); jolt length 100000 µs, period 2000000 µs.
- Mouse (same tool): every car's *M* is above 1 at all speeds (lowest 1.0253 for vpftruck,
  vpmoonrover and vpvwcup_Angel at rest), so the window edge never gives full lock: vppanoz at
  20 m/s gives −0.4968, −0.2477, 0, +0.2477, +0.4968 for the pointer at 0, ¼, ½, ¾ and the full width;
  the usual values (`MouseSensitivity` 1.3 / 2.6, `MouseSteerFilter` 0.6 / 1.3) give 0.7692 at
  rest, 0.5273 at 20 m/s and 0.2438 at 44.6 m/s at the edge.
