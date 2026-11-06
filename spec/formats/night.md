# Time of day and weather: what changes

A driving session in MM2 has one **time of day** — 0 morning, 1 noon, 2 evening, 3 night — and one
**weather** — 0 clear, 1 cloudy, 2 fog, 3 rain. Both are fixed for the whole session. Their
combination, `time × 4 + weather` (0–15), selects the sky's material set, the fog row and the
lighting file (`city-files.md`).

At night MM2 does **not** get darker through its lights: the night lighting files have a bright
white key light. The darkness comes from four other things, all described below:

1. every texture without a night version is loaded at half brightness;
2. textures that have a night version (`_ni`) are swapped for it: lit windows, glowing lamps;
3. a "light pool" map multiplies city surfaces down to about 30% between pools of lamp light;
4. black fog closes in from 100 m, and the sky uses its darkest material set.

On top of that come additive glows: street lamps, car headlights and tail lights, traffic lights.

## Summary

| What | Morning | Noon | Evening | Night | Section |
|---|---|---|---|---|---|
| Sky material set, fog row, lighting file | 0–3 | 4–7 | 8–11 | 12–15 | `city-files.md`, below |
| `_ni` textures, the rest at half brightness | — | — | — | yes | 1 |
| Untextured materials × 0.5 | — | — | — | yes | `pkg.md` |
| Ground overlay map | clouds | clouds | **light pools** | **light pools** | 3 |
| Car headlights (and tail lights lit) | — | — | yes | yes | 4 |
| Street-lamp glows | — | — | — | yes | 4 |
| Traffic-light glow meshes | day set | day set | night set | night set | 4 |

Headlights are also on in fog (any time of day). Rain adds wet-road textures (section 1) and
more (section 6).

## 1. Texture variants and night darkening

These rules apply to **every texture loaded while a driving session is being set up**: city
surfaces, buildings, props, cars and traffic, the sky, particle textures and the HUD. They do
not apply in the menus (the showroom shows cars in day colours). Each session loads its textures
afresh, so the rules are evaluated once per session.

For a texture name *n* (looked up as in `tex.md`: `.tex`, then `.tga`, then `.bmp`, then
animated frames, each frame name going through these rules on its own):

1. If the weather is rain, try `<n>_fa` first. If it exists, use it.
2. Otherwise, if it is night, try `<n>_ni`. If it exists, use it **as it is** and stop.
3. Otherwise load `<n>` itself.
4. **At night, every texture that did not come from a `_ni` file** — the plain ones and the
   `_fa` ones — has the red, green and blue of every pixel, in every mip level, halved
   (integer division by 2: 255 → 127). Alpha is unchanged.

So at night a lit window texture (`_ni`) shows at full value while the wall around it, the road,
the car paint and the sky are at half. Shipped `_ni` files: 441 (401 building facades, mostly
with lit windows and doors; 10 HUD map tiles and 8 HUD map dots; 10 glow and flare textures;
6 London Underground surfaces; the traffic cars' indicator and stop-lamp units; two lamp
textures; 2 others). No car body and no sky texture has one. 29 `_ni` files have no day
version; they are only ever seen at night.

The ground overlay maps (section 3) are loaded by a separate path that skips the halving (and
they have no `_ni` or `_fa` versions).

## 2. Lighting and colours at night

- Lights: the night rows of the lighting files (`city-files.md`) are not dark — London clear
  night has a white key light from above (pitch −0.9) and ambient (40, 60, 80). They shade
  building walls (`psdl.md`) and models as at any other time.
- Untextured materials have their diffuse colour halved at night (`pkg.md`, "Materials").
- **Ambient and the number of lights.** The game can run with 0–3 of the file's directional
  lights (a detail setting; every machine above 500 MHz uses 3). With 3 it uses the file's
  ambient. With fewer it uses a fixed ambient that ignores the file: 0 lights → white, 1 light →
  grey (172, 172, 172), 2 lights → grey (94, 94, 94). A new engine should use 3 lights and the
  file ambient.

## 3. Ground overlay map: cloud shadows by day, light pools by night

After drawing a city surface, MM2 can draw it a second time with a large tiling overlay that
darkens it.

- **Map**: `texture/shadmap_day.tga` in the morning and at noon (soft clouds), and
  `texture/shadmap_nite.tga` in the evening and at night (bright round pools on a dark field).
  Both are 128 × 128, white, with the pattern in the alpha channel.
- **Effect**: the surface's colour is multiplied by the map's alpha / 255. That is between
  0.71 and 1.0 for the day map and between 0.31 and 1.0 for the night map, where 1.0 is the
  middle of a light pool. (MM2 does this by turning the map black, inverting its alpha and
  alpha-blending it over the surface; the result is the same multiply. Fog stays on for this
  pass, so in fogged distance the darkening fades out.)
- **Texture coordinates**, generated from vertex positions (wrapping):
  - city surfaces from the PSDL (world coordinates): u = (x + y) / 256, v = (y + z) / 256;
  - placed city models — static objects, landmarks and spanning objects (`inst.md`), i.e. the
    buildings — in their own model coordinates: u = (x + y) / 128, v = (y + z) / 128.
  The overlay does not move over time. Breakable props, cars and other movers never get it.
- **Which batches**: only those whose texture has the cloud-shadow flag of the current detail
  level (`tex.md`, flags: 0x2 at the high setting — the one every current machine gets —
  0x4 at the medium setting, none at the low setting), and never textures with an alpha
  channel. For placed models MM2 tests the textures of material set 0 even when another set is
  shown. Of London's 330 surface textures 316 carry flag 0x2 without having alpha, so at high
  detail almost the whole city — roads, pavements and walls — gets the overlay.

This pass is the main reason MM2's night streets look dark between lamps.

## 4. Glows

### The glow pass

After all city surfaces, buildings, props and vehicles, MM2 draws glows for the objects in rooms
closer than the room limit (300 m at the usual detail settings, `inst.md`):

- additive blending (source + destination, each × 1);
- depth test on, depth writes off;
- no lighting, **no fog** (glows show through the night fog);
- textures as loaded (section 1 applies, so at night the `_ni` glow textures are used).

A **billboard** below is a square facing the camera: centre *p*, corners p ± h·right ± h·up with
the camera's right and up axes, *h* = half-size, texture 0..1 across, vertex colour as given.

### Street lamps and other lit props (night only)

A prop's data file `tune/banger/<prop>.dgBangerData` (text, `Key value` lines) may list
`NumGlows n` followed by *n* `GlowOffset x y z` lines. At night, for each glow of each such
prop reached by the glow pass, MM2 draws a billboard:

- centre = the prop's placement applied to (`CG` + `GlowOffset`), where `CG` is the prop's
  centre-of-gravity line from the same file (offsets are measured from it);
- half-size 1.5 m × (0.99 + 0.01·r), with *r* a new random number in [0, 1) every frame (a faint
  flicker);
- colour white; texture `s_yel_glow` (its `_ni` version, as it is night).

Shipped props with glows: 19 — street and park lamps (`sp_lightstreet_l`, `sp_lightstreet_f`,
`sp_lightstreet_rt_f`, `sp_lightpark_f`, `sp_lightthames_l`, `sp_lighttall_l`, `sp_light_tall_l`,
`sp_lightfwy_f`, `sp_lightglowing_l`, `sp_chinalight_f`), lit banners (`sp_lightbanner_l`,
`sp_lightbanrb_f`, `sp_lightbanrg_f`, `sp_lightbanrr_l`, `sp_lightbanr_rainbo_f`), the small
building lights attached through `xrefs` (`sp_light_white_f`), a road-works horse (`sp_sawhrslt_f`)
and two breakable parts of `sp_civcentrexit_f`. Example: `sp_lightstreet_l` has CG
(0, 5.30, 0) and glow offset (−1.93, 4.19, −0.03), so the glow sits 9.5 m up, out on the arm.
Street lamps are placed along the streets by the prop rules (`street-props.md`).

The lamp models themselves also switch to `_ni` textures (`sp_light_ni`,
`p_lightpost_thames_card_l_ni`).

### Traffic lights

Traffic-light models carry ten extra meshes: `REDGLOWDAY`, `YELLOWGLOWDAY`, `GREENGLOWDAY`,
`WALK_DAY`, `NOWALK_DAY` and the same five ending `NIGHT`. The `…DAY` set is used in the morning
and at noon, the `…NIGHT` set in the evening and at night. The meshes for the current signal
state are drawn in the glow pass (additive) at every time of day (which two meshes each state
shows, the light cycle, and where the meshes are placed — at the pole's base point, not at the
prop's centre of gravity: `../ai/traffic-control.md`, "What the lights show"). Traffic lights with glow
offsets also get the lamp glows above.

### Cars

When headlights are on (evening, night, or fog), all cars light up; braking and so on work at any
time. Everything here is drawn in the glow pass.

Player, opponent and police cars (every car with a car simulation, network cars included), in
this order, each with the car's world matrix:

| Mesh / glow | Shown when |
|---|---|
| `TLIGHT` | braking (the car's brake input is not 0) |
| `BLIGHT` | braking |
| `TLIGHT` again | headlights on, so braking with headlights on draws it **twice** (double brightness, additive) |
| `RLIGHT` | reverse gear (gear index 0) |
| headlight glows | headlights on (police cars with the light bar on: always, flashing, `../ai/police.md`) |

The light meshes are always drawn from their H slot (after the fill rule, so the shipped `_L`
meshes), whatever level the car itself is drawn at. MM2 also keeps four "lamp working" switches
per car (rear right, rear left, front left, front right; `TLIGHT` and `BLIGHT` need both rear
ones), and has a routine that would switch one off by the side of an impact, but nothing ever
calls it, and every reset or clearing of the damage switches all four on: so the lamps always
work.

Traffic cars:

| Mesh / glow | Shown when |
|---|---|
| `TLIGHT` | once when braking (acceleration < 0) or standing still (speed exactly 0), and once more when headlights are on (so twice, double brightness, when both hold) |
| `SLIGHT0` / `SLIGHT1` | left / right indicator (or both: hazard lights), blinking: on while `(k + floor(16·t)) & 8` is non-zero, *t* = a clock summing the frame times, *k* a per-car number 0–15 (one-second period) |
| headlight glows | headlights on |

Traffic glows are drawn for every traffic car in a room closer than the room limit, in view or not
and at any level of detail; the exact rules are in `../ai/traffic-visuals.md`, "Lights".

`HLIGHT` meshes are loaded but never drawn, and player cars never draw `SLIGHT0/1`.

**Headlight glows** are two billboards per car:

- centres: the origins of `HEADLIGHT0.mtx` and `HEADLIGHT1.mtx` placed with the car. Traffic
  cars use `HEADLIGHT0`'s origin and the same point with x negated (only if the car has a
  `HEADLIGHT1` mesh), and move both by the same 0.2 m toward the camera, along the direction from
  the car's origin to the camera;
- the light points along the car's forward axis (−Z). With *d* the vector from the light to the
  camera, c = dot(normalised *d*, forward). If c < 0 nothing is drawn; otherwise half-size =
  0.2 × √(25 · c³) = c^1.5 metres (1 m when looking straight into it);
- colour: 0.95 × the light's colour. Player cars: the diffuse colour of the material of the
  first batch of the `HEADLIGHT0` / `HEADLIGHT1` mesh in the current paint set (`vpbug`: 0.996,
  0.973, 0.835). Traffic: white;
- texture `lt_glow` (`lt_glow_ni` at night).

Headlights do not light the road or other objects: there is no dynamic lighting from cars.
(Police sirens add rotating lights and lens flares at any time of day; vehicle spec.)

## 5. Sky and fog

- **Sky**: material set `time × 4 + weather` of the sky package (`city-files.md`). Its texture
  names encode the condition: `sky_<w><t>_f` (SF) and `skylondon_<w><t>_l` (London) with
  weather *w* = `c` clear, `p` cloudy, `f` fog, `r` rain and time *t* = `a` morning, `n` noon,
  `d` evening, `m` night. The night ones are dark blue-black (London clear night averages
  (30, 28, 50)), and at night they are halved again by rule 4 of section 1. The sky is drawn
  without fog.
- **Fog**: the row for the condition (`city-files.md`). London clear night: black, from 100 m
  to 400 m; rainy night (1, 0, 3) from 35 m to 120 m. The screen is cleared to the fog colour.

## 6. Weather (notes; details later)

- **Rain** (3): `_fa` textures replace roads, crossings, pavements, grass verges and road decals
  (74 files, all wet-looking; 51 of each city's surface textures and 11 model textures have
  one); a rain particle system from `tune/rain.asBirthRule` with texture `ptx_rain` (200
  particles); tyre grip × 0.8 (× 0.75 at night); wet-surface sounds.
  **Where rain shows.** The particles are updated every frame whatever happens below; drawing
  them is decided once per frame, after the city's rooms are drawn, from the room R that contains
  the camera position (found as for any point, `../physics/city-collision.md`, with no hint):
  1. If R has PSDL flag 0x02 (enclosed: tunnels, `psdl.md`), no rain is drawn this frame.
  2. Otherwise, if R holds a landmark (the "has a landmark" mark, `inst.md`: set when a landmark
     is placed with R as its header room), MM2 probes a vertical segment from
     S = (c.x, c.y + 100, c.z) down to E = c, c = the camera position: the probe of
     `../physics/collision.md`, "Ground queries" (city polygons, then placed objects), with the
     wheels' ground mask (`../physics/collision-objects.md`, "Probing an object"), ignoring
     nothing and with no saved state (no room hints, no polygon cache). If it hits anything, no
     rain is drawn this frame. So in a room with a landmark, rain stops under the landmark (a
     bridge, a roof) or anything else the probe meets overhead; in a room without one there is no
     probe at all, and rain shows even under a deck or freeway of another room.
  3. Otherwise the emitter point is set to c + 10·Y − 10·Z, with Y and Z the camera's up and
     back axes (the camera looks along −Z, `../engine/cameras.md`), i.e. 10 m above and 10 m in
     front of the camera (exactly: the row vector (0, 10, −10, 1) times the camera's world
     matrix), and the rain is drawn. The point is used by the following particle updates, so
     while the rain is hidden new drops keep appearing at the last point set (they are just not
     drawn).
- **Fog** (2): headlights on; fog rows with short ranges (London foggy noon 15–75 m).
- **Cloudy** (1): only the sky set, fog row and lighting file change.
- Pedestrian models can differ between good and bad weather (`city/<c>.aimap`); there is a
  `snow.asBirthRule` in `tune/` but no snow weather.

## What the engine needs

To match MM2's night:

1. Apply section 1 when loading textures for a session: `_ni` swap, otherwise RGB ÷ 2 at night;
   `_fa` swap in rain. This includes PSDL, buildings, props, cars, sky and particles.
2. The ground overlay pass with `shadmap_nite` (evening and night) and `shadmap_day` (otherwise),
   over PSDL surfaces and placed static models whose texture has flag 0x2 and no alpha.
3. Fog from the condition's row (black at night) and the sky's condition set.
4. The glow pass: lamp glows at night, traffic-light glow meshes (day or night set), car tail
   lights and headlight billboards when headlights are on.
5. Untextured material colours × 0.5 at night (`pkg.md`).
6. Lighting from the condition's file with all three lights and the file ambient.

## Checked against

- Code facts above come from static reading of the game binary.
- `mm2tex.ar` `texture/`: 441 `_ni` files, 74 `_fa` files (table in section 1); `shadmap_day.tga`
  and `shadmap_nite.tga` are 128 × 128 32-bit, RGB all 255, alpha 182–255 (day, mean 216) and
  79–255 (night, mean 178). Decoded pairs (`work/check/contact_night_tex.png`): `cl_gracewin_f_ni`
  is the same stone wall darker with yellow windows; `r1_f_fa` is the same road darker and
  glossy.
- Sky packages: `sky_dome` and `sky_dome_l` have 16 sets in the order `ca, pa, fa, ra, cn, pn, fn,
  rn, cd, pd, fd, rd, cm, pm, fm, rm`; the `…m` (night) textures average (6, 6, 23) SF and
  (30, 28, 50) London, the `…a` ones (151, 132, 148) and (111, 107, 128).
- `tune/banger/`: 19 of 995 prop data files have `NumGlows` 1 (none has more).
- Lighting files: night ambients London (40, 60, 80) clear, (60, 60, 40) cloudy, (5, 5, 5) foggy,
  (40, 30, 30) rainy; SF the same except foggy (100, 100, 100) and rainy (80, 80, 80).
- London PSDL textures: 330, of which 322 have flag 0x2 (6 of them with alpha, so 316 get the
  overlay; 55 also have 0x4), 131 have a `_ni` version and 51 a `_fa` version. SF: 324, 117
  `_ni`, 51 `_fa`.
- `vpbug` `HEADLIGHT0_H` material: untextured, diffuse (0.996, 0.973, 0.835).
