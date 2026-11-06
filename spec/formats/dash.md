# Cockpit (dash) view and rear-view mirror

The dash view is the point-of-view camera `<car>_dash.camPovCS` (`cameras.md`) plus a cockpit
model drawn over the scene: dashboard, roof, steering wheel, three needles and a gear display.
This page covers the cockpit model, its animation and drawing, what the dash and bumper views
hide, and the rear-view mirror. Formats used: `pkg.md`, `tex.md`. `<car>` is the car's
`BaseName`.

## Files

| File | Contents |
|---|---|
| `geometry/<car>_dash.pkg` | the cockpit meshes and their material sets |
| `geometry/<car>_dash_<part>.mtx` | pivot of each part (`pkg.md`, ".mtx"); only the **box centre** (floats 6–8) is used |
| `tune/<car>_dash.asNode` | placement and gauge ranges (keys below) |
| `tune/<car>.mmMirror` | rear-view mirror settings (optional) |

`tune/banger/<car>_dash_*.dgBangerData` files are exporter leftovers; the game never reads them.
Neither does it read `vpcaddie59_dash.asnode`, `vpvwcup_Angel_dash.*` or `vpvw_dune_dash.*`.
All 20 menu cars ship a dash package, `.mtx` files and an `.asNode`.

## The cockpit package

MM2 reads these meshes in this order (sequential lookup, `pkg.md`, "Order matters"), each only
at level `H`:

| Mesh | What it is |
|---|---|
| `damage_needle_H` | damage gauge needle |
| `dash_H` | dashboard and windscreen frame |
| `gear_indicator_H` | small quad showing the current gear |
| `roof_H` | roof lining and sun visors |
| `speed_needle_H` | speedometer needle |
| `tach_needle_H` | rev counter needle |
| `wheel_H` | steering wheel |
| `dash_extra_H` | optional extra layer (none ship) |

then `shaders`. All shipped dash packages have exactly the first seven, and **9 material sets**;
every mesh except the gear indicator is drawn with set 0. The gear indicator's material changes
per set: the textures are `R`, `N`, `One`, `Two`, `Three`, `Four`, `Five`, `Six`, `D` for sets
0–8. All cockpit meshes are stored in one common "dash space", already in place relative to each
other; every texture has an alpha channel (the windows are transparent).

Pivots (box centre of `<car>_dash_<part>.mtx`): `speed_needle`, `tach_needle`, `damage_needle`
and `wheel`. The other `.mtx` files are not used.

## `tune/<car>_dash.asNode`

Text (`type: a`, then `asNode { … }`, `Key value` lines, vectors as three numbers):

| Key | Default | Meaning |
|---|---|---|
| `DashPos` | 0, −0.707, −1.216 | where dash space's origin sits, in camera space |
| `RoofPos` | 0, 0.189, 0 | where the roof mesh's origin sits, in camera space (some cars park it 2–3 m up to hide it) |
| `WheelPos` | 0, 0, 0 | extra offset of the steering wheel, dash space |
| `SpeedOffset`, `TachOffset`, `DmgOffset` | 0, 0, 0 | extra offset of each needle, dash space |
| `SpeedPivotOffset`, `TachPivotOffset`, `DmgPivotOffset` | 0, 0, 0 | correction added to each needle's pivot |
| `WheelPivotOffset` | 0, 0, 0 | correction added to the wheel's pivot |
| `GearPivotOffset` | 0, 0, 0 | offset of the gear indicator, dash space (7 shipped files omit it) |
| `WheelFact` | 0.9 | steering wheel turn per unit of steering, radians |
| `SpeedRotMin`, `SpeedRotMax` | −0.907, 7.0 | needle angles (radians) at the bottom and top of the scale |
| `RPMRotMin`, `RPMRotMax` | −0.907, 7.0 | same for the rev counter |
| `DamageRotMin`, `DamageRotMax` | −0.907, 7.0 | same for the damage gauge |

Camera space is the view camera's frame: +X right, +Y up, the camera looks along −Z.

## Placement: the cockpit is fixed to the camera

Every frame the cockpit takes the pose of the camera used to draw the frame (the dash camera,
`cameras.md`: car pose + `Offset`, `Pitch`, approach lag). With *C* the camera's rotation and *c*
its position, a point *p* in dash space is drawn at `(p + DashPos)·C + c` (row vectors, `pkg.md`).
So the cockpit never moves on screen: it does not follow suspension, bumps or the camera's lag
separately from the view. Each part:

- `dash_H`: dash space as is.
- `roof_H`: its own origin at `RoofPos` instead of `DashPos`: `(p + RoofPos)·C + c`.
- `gear_indicator_H`: dash space + `GearPivotOffset`; material set = the current gear (below).
- needles: rotated about their pivot, then moved by their offset:
  `p' = (p − P)·Rz(−a) + P + Offset`, where *P* = the part's `.mtx` centre + its `…PivotOffset`,
  *a* the needle angle (below), `Offset` the `SpeedOffset`/`TachOffset`/`DmgOffset`; then
  dash space as above.
- `wheel_H`: the same with *P* = wheel `.mtx` centre + `WheelPivotOffset`, angle
  *a* = steering × `WheelFact`, offset `WheelPos`.

`Rz(t)` turns by *t* about +Z (the axis pointing back at the driver), right-handed: rows
(cos t, sin t, 0), (−sin t, cos t, 0), (0, 0, 1). Because the needles turn by −*a*, a growing
angle sweeps them **clockwise** as the driver sees them, and steering right (+1) turns the wheel
clockwise.

## Needle angles

Each needle is computed fresh every frame from the current value; there is no smoothing:

```
f = max(value, floor) / top
a = (RotMax − RotMin) · f + RotMin
a = clamp(a, min(RotMin, RotMax), max(RotMin, RotMax))
```

| Needle | value | floor | top | angles |
|---|---|---|---|---|
| speed | the car's speed in mph (always ≥ 0; `vehicle.md`, step 2) | 0 | **160** | `SpeedRot…` |
| rev counter | the engine's displayed RPM (`vehicle-powertrain.md`) | **800** | **8000** | `RPMRot…` |
| damage | damage taken so far | 0 | the car's damage limit (`../physics/damage.md`) | `DamageRot…` |

160 mph and 8000 RPM are the same for every car; the dial artwork is drawn to match each car's
`…RotMax`. *f* is rounded to a float before the multiply. Example, vpbug at 60 mph: f = 0.375,
a = 3.614 × 0.375 = 1.355 rad.

## Gear indicator

The material set index is the gearbox's current gear index (`vehicle-powertrain.md`: 0 reverse,
1 neutral, 2 first, …), so it shows `R`, `N`, `One`, `Two`, … It is the same with the automatic
gearbox (it shows the gear number). Gear index 8 (`D`) cannot occur: at most 8 slots exist, so
the highest index is 7.

## Steering wheel

*a* = *s* × `WheelFact`, where *s* is the steering value the car receives this frame, after the
keyboard/gamepad smoothing and quantisation (`controls.md`; −1 full left, +1 full right). With
`WheelFact` 0.9 (every shipped car) full lock turns the wheel 0.9 rad ≈ 52°.

## Drawing

The cockpit is drawn after the whole 3D scene (city, cars, glows); the HUD map and the mirror are
drawn after it, on top. It uses:

- the same field of view and aspect as the scene, near plane **0.01 m** (far unchanged); the
  previous near plane is restored afterwards;
- **depth test off and depth writes off**: it covers everything, in this order: dash, roof, gear
  indicator, speed needle, rev needle, damage needle, `dash_extra`, steering wheel;
- **no lighting**: colour = texture × white; material colours are ignored;
- alpha blending on (source alpha / inverse source alpha) for every part, discarding alpha 0;
- no fog; back faces culled as usual (`pkg.md`).

Night (`night.md`): the cockpit textures are loaded with the session, so at night they get the
half-brightness rule. No dash texture has a `_ni` version: MM2 has no lit gauges; the whole
cockpit is simply darker.

## When the cockpit is shown

- Only while the dash view is active (D, `cameras.md`), and only if its package loaded.
- **Not while a look key is held** (left, right, back, and their diagonals): the camera turns
  (`cameras.md`, "Each frame", step 3) and both the cockpit and the HUD disappear until the key
  is released. "Look forward" counts as not looking.
- In the dash view the 2D speedometer/rev-counter overlay of the outside views is hidden; the
  rest of the HUD (map, timers, positions) stays.

## What the dash and bumper views hide

While the dash or the bumper camera is active, the player's car model is invisible: body,
wheels, shadow, decal, breakable parts, tail-light and headlight glows, siren lights. A trailer
is a separate object and stays visible (seen when looking back). The bumper view draws no cockpit.

## Rear-view mirror

A small second picture of the world, looking backwards, in the top-right corner of the screen.

- **On/off**: whether the mirror is on when a game starts comes from the player file (byte 4 of the
  view settings, `../engine/controls.md`; off in a new profile and in the shipped `player0.cfg`).
  Backspace toggles it during the game; MM2 does not save that toggle, so the next game starts with
  the file's setting again. **Our choice (Ant, 2026-10-15):** remember the last toggle per browser
  (the engine's saved option) and start each game with it. When on, it is drawn in **every** view, not only the cockpit.
- **Settings**: `tune/<car>.mmMirror` (`type: a`, `mmMirror { … }`); every car gets a mirror, with
  these defaults where the file or a key is missing:

| Key | Default | Meaning |
|---|---|---|
| `Position` | 0, 1.4, −1.0 | mirror camera position, car space |
| `Size` | 0.3, 0.16 | width and height as fractions of the screen's width and height |
| `Fov` | 10 | vertical field of view, degrees |
| `Aspect` | 2.0 | width/height used by the projection |
| `NearClip` | 1.2 | near plane, metres |
| `FarClip` | 100 | far plane, metres |

  Shipped files (11 cars: vp4x4, vpauditt, vpbus, vpcaddie, vpcentury, vpcop, vpdb7, vpddbus,
  vpford, vppanoz, vpsemi) change only `Position` and `NearClip` (1.2–5.8; long vehicles clip
  their own body).
- **Rectangle**: width *w* = trunc(screen width × `Size.x`), height *h* = trunc(screen height ×
  `Size.y`) pixels; left edge at screen width − *w* − 1, top edge at pixel row 1.
- **Camera**: the car model's pose as drawn (origin = body position + R·CoG, `vehicle.md`,
  "Centre of mass"; the same pose the body is drawn with, not the physics body at the centre of
  mass) × a local frame at `Position` with axes X = (1, 0, 0),
  Y = (0, 1, 0), Z = (0, 0, −1) in car space. It looks along the car's +Z (backwards) while its
  X stays the car's right, so the picture is mirrored left-to-right like a real mirror. Because
  that frame is mirrored, triangle winding flips: MM2 swaps the culling direction while drawing
  the mirror.
- **Render**: clear the rectangle (colour black, depth far), then draw the full city view —
  sky, streets, buildings, props, other cars, glows, fog — from that camera, with the player's
  car model hidden. No cockpit, no HUD. It is drawn after the cockpit, so it sits on top of it.
  - Everything that the main view does relative to "the camera" uses the mirror camera here:
    the sky dome is centred on the **mirror camera** (`city-files.md`, "Sky": (cam x,
    cam y · b + a, cam z)), unscaled, with the mirror's own projection. The dome is a hemisphere of
    radius 42.9 m, so the 100 m far plane never cuts it.
  - Fog is clamped to the mirror's far plane like the main view's (`city-files.md`, "Fog"):
    with `FarClip` 100, from min(start, 70) to min(end, 100).
  - Ground and buildings beyond 100 m are clipped, and the rectangle was cleared to **black**,
    not to the fog colour. Between the last drawn ground (100 m away) and the dome's lower rim
    (42.9 m away, `0.05 · cam y − a` below eye height for London and SF) the black clear shows
    through wherever nothing nearer covers it: a thin black band along open horizons, most
    visible when the camera is low (near world y = 0). MM2 shows that band; keep it.
  - The main view hides the same gap because it is cleared to the fog colour and its far plane
    is 600 m.

## Checked against

- `tools/formats/dash.py` draws a cockpit by the rules above (`work/check/dash_vpbug.png`,
  `contact_dash.png`): vpbug at 60 mph shows the speed needle just past the "60" mark, gear 3 shows
  "2", steering 0.5 turns the wheel clockwise; vp4x4 and vpcab (right-hand drive, wheel on the
  right, gear 0 shows "R") line up with their dial art.
- All 24 `_dash.pkg` files (20 menu cars and 4 others): the seven meshes in the order above,
  9 material sets, gear-indicator sets `R N One Two Three Four Five Six D`; all 103 cockpit
  textures have alpha, none has a `_ni` version.
- 24 × 7 `_dash_*.mtx` files. vpbug centres: speed needle (−0.456, 0.232, 0), rev needle
  (−0.353, 0.167, 0), damage needle (−0.519, 0.167, 0), wheel (−0.421, 0.232, 0.230).
- 23 `_dash.asNode` files; `WheelFact` 0.9 in all; `GearPivotOffset` missing in 7.
- 11 `.mmMirror` files: all `Size` 0.3 × 0.16, `Fov` 10, `Aspect` 2, `FarClip` 100.
