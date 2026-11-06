# HUD map

MM2's driving view can show a top-down map of the city: a small map in a corner, a half-screen
map, or a full-screen map (which pauses a single-player game). Our engine draws the map in its own
style, but **what it shows and how it frames the city** (camera height, orientation, zoom, which
objects get markers and what their colours mean, modes, keys, saved settings) follow MM2 as below,
and it reads MM2's map files so mod cities with their own maps work. Keys and the twelve saved
view bytes are introduced in `../engine/controls.md`; the rest of the HUD is `hud.md`.

`<c>` is the city's `MapName` (`../formats/city-files.md`). Screen sizes *W* × *H* are the window
in pixels.

## Files

| File | What |
|---|---|
| `geometry/hudmap_<c>.pkg` | the map: a flat textured plane lying on the city's ground plane in world coordinates (y = 0), level `H`, material set 0, no normals (drawn unlit). Shipped: `hudmap_sf` (x −2314.7 … 485.3, z −2800.0 … 1632.1, 6 textures), `hudmap_london` (x −1525.0 … 1412.3, z −1104.6 … 985.5, 4 textures). Its textures follow the city's night texture rule (`../formats/night.md`) |
| `geometry/hudmap_square.pkg` | a small square (±6.977 in x and z) with 9 material sets: 0 red dot, 1 blue dot, 2 green dot, 3 grey dot, 4 yellow dot, 5 finish dot, 6 gold, 7 bank, 8 hideout (8 × 8 textures) |
| `geometry/hudmap_tri.pkg` | loaded but never drawn |
| `tune/<c>.mmHudMap` | map settings (below); optional |

If `hudmap_<c>.pkg` does not exist the map is disabled for that game: the mode is set to 0 (and
saved so) and stays off; the map keys do nothing. (MM2 itself probably crashes on Tab in that
case; do not copy that.)

### `tune/<c>.mmHudMap`

Tune-file text (`../physics/vehicle-tune.md`, "Syntax"): `type: a`, then `mmHudMap { … }`. A
missing file or key keeps the default.

| Key | Values | Default | Meaning |
|---|---|---|---|
| `Size` | 2 floats | 0.21 0.25 | small map width and height, fractions of the screen |
| `Pos` | 2 floats | 0.78 0.75 | small map left and top edges, fractions of the screen (top measured from the top) |
| `ZoomIn` | integer, 0 or not 0 | — | the starting zoom: written into saved view byte 11 every time a game starts, so it overrides the saved zoom |
| `ZoomInDist`, `ZoomOutDist` | float | 577, 1195 | camera height (m, world *y*: not relative to the car) when zoomed in / out, small and half-screen maps |
| `ZoomInDistFS`, `ZoomOutDistFS` | float | 786, 1580 | the same for the full-screen map |
| `IconScaleMin`, `IconScaleMax` | float | 34, 52.5 | marker size *s* when zoomed in / out |
| `IconScaleMinFS`, `IconScaleMaxFS` | float | 15, 18 | the same for the full-screen map |
| `Approach Rate` | float | 1.2 | speed of the zoom change (below) |
| `Ocean Color` | 3 floats | — | never used |

Keys are single tokens compared exactly. The two-word keys `Approach Rate` and `Ocean Color`, as
written in the shipped files, therefore never match: the parser sees the unknown word `Approach`
(or `Ocean`) and skips the rest of the line. Only a quoted key (`"Approach Rate" 2.0`) would be
read: the tune-file reader takes a token that **begins** with `"` as everything up to the next `"`
(spaces, tabs and line ends included, the quotes dropped; without a closing quote it runs to the
end of the file), and a `"` inside a word is an ordinary character. So the approach rate is in
effect always 1.2 and the clear colour never comes from the file (see "Drawing").

Shipped files: both cities `Size` 0.21 0.25, `Pos` 0.78 0.75, `ZoomIn` 0, 577 / 1195; London icon
scales 34 / 52.5, full-screen 786 / 1580, 15 / 18; SF 34.090019 / 52.399994, 786 / 1581,
15.07 / 18.

## Modes and keys

| Mode | What |
|---|---|
| 0 | off |
| 1 | small map in a corner |
| 2 | half screen: the 3D view in the top half, the map in the bottom half |
| 3 | full screen: the map fills the screen and the 3D view shrinks into the corner rectangle |

- **Tab**: mode 0 → 1 → 2 → 0. From mode 3 it goes back to the mode before the full map (and, in
  single player, the game stays paused: see "Full-screen map").
- **Q**: from modes 0 – 2 enter mode 3, remembering the current mode; from mode 3 return to it.
- **E**: zoom in ⇄ out (saved view byte 11). **F**: north-up ⇄ heading-up (byte 10). Both do
  nothing while the map is off.
- The current mode is saved as view byte 1 every time it changes (also 3). At every reset of the
  player's car (game start, restart, after drowning or falling out of the city) the map is set to
  the saved mode. With the shipped profile and any new profile that is 0: **the map starts off**.
  A game that ended in full-screen mode starts the next one in mode 3 (without a pause).
- Crash Course: while a course's countdown runs with the map off, MM2 switches the small map on.

Program defaults: byte 1 = 0, byte 10 = 1 (heading-up), byte 11 = 0, but byte 11 is replaced by
the city's `ZoomIn` at each game start (0 in both shipped cities: zoomed out).

### Interlocks with the other views

- Entering mode 2 turns the letterbox wide view on and the dash view off (the 3D view then uses
  the top half of the screen with the wide view's field of view, `../engine/cameras.md`,
  "Projection"). If the dash view was on, MM2 remembers to bring it back; if the wide view was off
  it is not saved as on.
- Tab from mode 2 to 0: the wide view returns to its saved state, and the dash view comes back if
  it was on when Tab entered mode 2, the camera is still the dash camera, and neither the mirror
  key nor Q was pressed in between. (MM2 keeps one "bring the dash back" mark, set when Tab enters
  mode 2 with the dash on and cleared whenever the dash is switched off, which every view key that
  is not ignored in mode 2 and every car reset do; Tab reads the mark before that.)
- In modes 2 and 3 the wide-view key does nothing; in mode 2 the dash key does nothing; the
  letterbox bars are drawn only in modes 0 and 1.
- After a car reset in mode 2 the wide view follows the saved byte instead of being forced on, so
  the top half shows the normal field of view (an MM2 quirk; keep it).
- Every view key (camera, cross view, wide, dash, mirror, Tab, Q) re-applies the map mode, which
  snaps the camera height and marker size to their targets (cancelling a zoom in progress).

## Map rectangle and projection

With *x* = trunc(*W*·Pos.x), *y* = trunc(*H*·Pos.y), *w* = trunc(*W*·Size.x), *h* =
trunc(*H*·Size.y) (single-precision products, truncated):

| Mode | Map rectangle (left, top, width, height) | 3D view | Map projection |
|---|---|---|---|
| 1 | (*x*, *y*, *w* − 10, *h* − 10); if the car is right-hand drive and the dash view is on, left = 0 | whole screen | vertical field of view 60°, aspect **1.25** (fixed), near 10, far 1600 |
| 2 | (0, trunc(*H*·0.5), *W*, trunc(*H*·0.5)) | (0, 0, *W*, trunc(*H*·0.5)) | 60°, aspect **2.5**, 10, 1600 |
| 3 | (0, 0, *W*, *H*) | the corner rectangle (*x*, *y*, *w*, *h*) (no −10, no right-hand shift) | 60°, aspect 1.25, 10, 1600 |

So with the defaults the small map sits at the **bottom right** (at 640 × 480: left 499, top 360,
124 × 110). The fixed aspect means the map is stretched when the rectangle's own shape differs.
A car is right-hand drive when its `.info` `Flags` has bit 64 (shipped: vpcab, vpcoop, vpcoop2k,
vpdb7); moving the map left keeps it clear of the cockpit's steering wheel.

## Map camera

Each frame the map is drawn (modes 1 – 3):

1. *P* = position of the player's car body (the rigid body's pose, centre of mass; not the drawn
   model), *Z* = its backward axis. *v* = (*Z*.x, 0, *Z*.z) divided by its length (left as zero
   if the length is 0).
2. Orientation: **north-up** (byte 10 = 0): screen right = world +X, screen up = world −Z.
   **Heading-up** (byte 10 ≠ 0): the map turns with the car so that the car's forward direction
   (−*v*) points up on the screen, with no lag. The camera looks straight down.
3. Height *h* and marker size *s* move toward their targets:
   - targets: zoomed in → `ZoomInDist` and `IconScaleMin`; zoomed out → `ZoomOutDist` and
     `IconScaleMax` (the `FS` keys in mode 3);
   - rate: (`ZoomOutDist` − `ZoomInDist`) × 1.2 per second for *h*, (`IconScaleMax` −
     `IconScaleMin`) × 1.2 per second for *s* (FS values in mode 3; 1.2 is the approach rate);
   - each frame *h* moves toward its target by rate × *dt* and stops at it; the same for *s*. A
     full zoom change therefore takes 1/1.2 = 0.83 s. *dt* is the frame time, which keeps running
     while the game is paused, so the zoom also finishes on the paused full-screen map.
   - When the mode is set (any of 1 – 3), *h* and *s* jump straight to their targets.
4. The camera sits at (*P*.x, *h*, *P*.z), *h* a world height whatever the car's own height: the
   car is always at the centre of the map.

## Drawing

In this order, into the map rectangle:

1. Clear it (colour and depth) to the background ("sea") colour: **London** (the city name
   compared without case) (0.92, 0.84, 0.778), i.e. RGB (234, 214, 198); **every other city**
   (0.084, 0.68, 0.92), i.e. (21, 173, 234). Each channel is trunc(value × 255). These constants
   are fixed in the program; the files' `Ocean Color` is ignored.
2. The map plane, unlit, with depth test and alpha blending, in world coordinates.
3. The markers, each drawn on top of the map (see below): checkpoints, police, opponents, the
   player, then the Cops-and-Robbers markers (multiplayer).

In mode 3, the 3D view is then drawn into the corner rectangle (cleared to black first).

**Car markers** are flat triangles in the ground plane, pointing the way the car faces, centred
15 m above the car, length 2*s* and width 1.4*s* (tip at the front, *s* the current marker size;
corners at −1, and ±0.7 at +1, along the car's axis, times *s*). They are drawn without depth
test, flat coloured:

| Marker | Colour |
|---|---|
| the player | a black triangle 1.3 times the size, with a yellow (255, 255, 0) one on top |
| each single-player opponent | purple (180, 0, 255) |
| a police car | red (255, 0, 0), only while that officer is doing something (not idle: chasing, etc., `../ai/police.md`) |
| network players | one colour per player slot (multiplayer spec) |

**Checkpoint markers** are the `hudmap_square` dot, scaled by *s* / 7.51, world-aligned, at the
checkpoint's position with height max(y, 0) + 10, alpha-blended, one material set per state. With
*i* the checkpoint's number, *n* the count, "current" the target the direction arrow points to
(`waypoints.md`) and "hit" whether it has been passed:

- **Circuit**: checkpoint 0 (start and finish line) → finish dot; others: not hit → yellow if
  current, else green; hit → grey.
- **Other modes**, *i* < *n* − 1: hit → no marker; current → yellow; otherwise green, except when
  the list has the Crash Course "only-target" option (`waypoints.md`) → none.
- The last checkpoint (*i* = *n* − 1): in a checkpoint race the finish dot once every other
  checkpoint is hit (until then none); in Blitz and Crash Course the same rule as the others;
  otherwise none.

Point 0 of a non-circuit race (the start) is never marked. Markers outside the map rectangle are
simply not visible (there are no edge pointers).

**Cops and Robbers** (multiplayer): the gold → gold dot; the two bases → bank and hideout dots (or
blue and red dots in one game variant). Details belong to the multiplayer spec.

## Full-screen map

In single player, pressing Q to enter the full-screen map **pauses the game**, and pressing Q
again unpauses it. (Physics, AI and timers stop; the map's own zoom still animates.) Leaving the
full-screen map with Tab instead does not unpause: the game stays frozen until Q is pressed twice
(MM2 quirk). In multiplayer the full-screen map does not pause.

## Checked against

- `tools/game/hudmap.py` reads both shipped `.mmHudMap` files with the tune-file rules (two-word
  keys skipped), computes the map rectangles at 640 × 480 and 1024 × 768 and the background
  colours, and measures the map and marker models (`work/check/hudmap.txt`): small map at
  640 × 480 = (499, 360, 124, 110), half map (0, 240, 640, 240), full-screen corner (499, 360,
  134, 120); London background (234, 214, 198), SF (21, 173, 234).
- The map planes are flat (all y = 0) and cover each city's streets (`hudmap_sf` x −2314.7 …
  485.3, z −2800.0 … 1632.1); `hudmap_square` is ±6.977 with 9 material sets.
- At the default heights the visible ground is 2·*h*·tan 30° tall (and 1.25 or 2.5 times as wide):
  666 m zoomed in and 1,380 m zoomed out on the small map, 908 m and 1,824 m on the full-screen
  map (London; SF 1,826 m).
