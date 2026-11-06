# Pedestrian models and animations (`anim/`)

The files of a pedestrian model, how MM2 reads them, how it poses a model from an animation
frame, how the animations play, and how a pedestrian is drawn near and far. What the
pedestrians do is `../ai/pedestrians.md`. All files are looked up through the archives
(`ar.md`), case-insensitively, in the folder `anim`.

## Files

A pedestrian model is named in the city or race aimap (`aimap.md`,
`[GoodWeatherPedName / BadWeatherPedName]`); the shipped names are `pedmodel_man`,
`pedmodel_woman` (dry weather) and `pedmodel_manw`, `pedmodel_womanw` (rain). For a name *N*:

| File | Needed | Contents |
|---|---|---|
| `anim/N.csv` | yes | the sequence table: one row per named animation sequence ("Sequence table") |
| `anim/N.skel` | yes | the skeleton: bones, their offsets and hierarchy ("Skeleton") |
| `anim/N.mod` | yes | the model: vertices per bone, triangles, materials (".mod model") |
| `anim/N.shaders` | no | the paint variants: material sets in the `shaders` layout of `pkg.md` ("Paint variants") |
| `anim/N.rays` | no | the far-detail figure: one flat strip per bone ("Far detail") |
| `anim/<anim>.anim` | yes, per row | the animations named in the sequence table ("Animation files") |

Files in `anim/` that MM2 never reads: `GROG.BAT` (an artist's viewer batch file),
`pedmodel_woman` (no extension, editor window settings), `pedmodel_woman.remap`,
`pedmodel_wolf.skel` (26 bones, no model), the `CVS` folder, and 19 animation files no table
names (`pedanim_*divel`, `*diver`, `*dl2s2`, `*dl2st`, `*dr2s2`, `*dr2st`, `*rn2st`, `*st2rn`,
`*w2bk` of both genders, and `pedanim_manantrnch.anim`, which is not even a valid animation).
`geometry/pedmodel_*.pkg` and `geometry/pedanim_*.pkg` exist too but are never read.

A model is loaded once per session (the first pedestrian that uses it) and shared; animation
files are loaded once per game and shared by every model that names them ("Animation files").

## Sequence table (`.csv`)

Text, read line by line with lines of at most 127 bytes (a longer line is cut and its rest read
as the next line). A line whose first character is `#` is skipped; **every other line is a row**
(also a blank one; none ships). The shipped tables have three `#` comment lines and 24 rows.

A row is split at commas, and **empty fields vanish** (C `strtok`: `a,,b` gives two fields); the
ninth field also ends at a carriage return or line feed. Fields, in order:

| # | Field | Read as |
|---|---|---|
| 1 | sequence name | text, e.g. `WALK` (the behaviour code asks for sequences by these names) |
| 2 | animation file name | text, `anim/<name>.anim` |
| 3 | first frame | integer (`atoi`) |
| 4 | last frame | integer |
| 5 | forward offset | number, **not used** |
| 6 | forward distance *Df* | number (`atof`), metres covered over the sequence |
| 7 | sideways offset | number, not used |
| 8 | sideways distance *Ds* | number, metres moved to the pedestrian's left over the sequence |
| 9 | next sequence | text; optional; the sequence that follows when this one ends |

From each row MM2 derives (float32; *Df* and *Ds* are divided as parsed, in double precision, and
the quotient rounded to float32):

1. *N* = last − first; duration *t* = float(*N* + 1) × 0.03333 (float32 0.033330001), one
   rounding.
2. **Forward speed** *vf* = *Df* / *t*, **sideways speed** *vs* = *Ds* / *t* (m/s). These use the
   row's *N*, before step 4.
3. Load the animation (below).
4. If the animation has *F* ≤ *N* frames, *N* = *F* − 1.
5. **Drift removal** on the animation's frames 0 … *N* (it changes the shared animation data):
   with x₀, z₀ the root translation x and z of frame 0 and x_N, z_N those of frame *N*:
   a = −x₀, b = −z₀; if *N* = 0 then sx = sz = 0, else sx = (−x_N − a) / *N*,
   sz = (−z_N − b) / *N*. Then for *i* = 0 … *N*: x_i = x_i − (−((*i* × sx) + a)),
   z_i = z_i − (−((*i* × sz) + b)) (each operation rounded). Afterwards the root's x and z are 0 in
   frame 0 and frame *N* and the sequence plays on the spot; the behaviour code moves the
   pedestrian with *vf* and *vs* instead.
6. **Frame count** = *N* + 1; **direction** = +1 if *N* > 0, 0 if *N* = 0, −1 if *N* < 0 (never
   in shipped data).
7. After all rows: the next sequence of every row is looked up by exact name; a name that is not
   found leaves the link undefined (MM2 prints an error; none in shipped data). *Our choice*: link
   to the row itself.

The frames played are always 0 … *N* of the animation file: the first-frame field only enters
*N* (every shipped row has first frame 1, so *N* + 1 = last frame).

Running the drift removal again on an animation is harmless only for the same *N* (it then
changes nothing). The shipped tables never name one animation twice, but the rain models name
the dry models' animations with the same frame ranges.

**Shipped values** (both dry and rain versions of a gender share them):

| Sequence | man: frames, *vf*, *vs* | woman: frames, *vf*, *vs* | next |
|---|---|---|---|
| `STAND` | 30, 0, 0 | 29, 0, 0 | `STAND` |
| `STAND2` | 29, 0, 0 | 29, 0, 0 | `STAND2` |
| `STAND_WALK` | 3, 2.1077, 0 | 3, 0.4575, 0 | `WALK` |
| `WALK` | 20, 2.1137, 0 | 20, 1.6307, 0 | `WALK` |
| `WALK_STAND` | 10, 0.3690, 0 | 9, 1.3021, 0 | `STAND` |
| `STAND_ANTIC` | 4, 0.1620, 0 | 7, 0.3638, 0 | `ANTIC` |
| `ANTIC` | 19, 0, 0 | 20, 0, 0 | `ANTIC` |
| `ANTIC2` | 19, 0, 0 | 20, 0, 0 | `ANTIC2` |
| `ANTIC_WALK` | 3, 1.4551, 0 | 3, 0.1425, 0 | `WALK` |
| `WALK_ANTIC` | 9, 0.4400, 0 | 9, 0.8851, 0 | `ANTIC` |
| `ANTIC_LDIVE` | 10, 0, 6.6007 | 10, 0, 6.2706 | `LDIVE_GROUNDL` |
| `LDIVE_GROUNDL` | 10, 0, 6.5407 | 10, 0, 8.7009 | `GROUND_STANDL` |
| `GROUND_STANDL`, `GROUND_STANDL2`, `GROUND_STANDR`, `GROUND_STANDR2` | 44, 0, 0 | 44, 0, 0 | `STAND` |
| `ANTIC_RDIVE` | 10, 0, −6.6007 | 10, 0, −6.2706 | `RDIVE_GROUNDR` |
| `RDIVE_GROUNDR` | 10, 0, −6.5407 | 10, 0, −8.7009 | `GROUND_STANDR` |
| `WALK_RDIVE` | 24, 0, −2.6259 | 24, 0, −2.1146 | `RDIVE_GROUNDR` |
| `WALK_LDIVE` | 24, 0, 2.7803 | 24, 0, 2.1146 | `LDIVE_GROUNDL` |
| `RUN` | 12, 7.1357, 0 | 12, 5.6481, 0 | `RUN` |
| `BACKUP` | 1 (direction 0), 0, 0 | 1 (direction 0), 0, 0 | `BACKUP` |
| `BACKUP_WALK` | 3, 1.2601, 0 | 3, 0.4725, 0 | `WALK` |
| `RUN_WALK` | 9, 3.7684, 0 | 7, 2.2240, 0 | `WALK` |

Frame counts below the last-frame field come from step 4 (the file is shorter): man `STAND2`,
`STAND_WALK`, `STAND_ANTIC`, `ANTIC_WALK`, `WALK_RDIVE`, `BACKUP_WALK`, `RUN_WALK`; woman also
`STAND`, `WALK_STAND`, `WALK_ANTIC`, `WALK_LDIVE`. `GROUND_STANDL2` and `GROUND_STANDR2` are never
played.

## Animation files (`.anim`)

Binary, little-endian:

| Size | Contents |
|---|---|
| u32 | *a* |
| (u32, u32) | only if *a* = 0: frame count *F* (1 … 10,000) and channel count *C* (1 … 1,000) |
| u32 | only if *a* ≠ 0: *F* = *a*, then a bone count *B*; *C* = 3*B* + 3 |
| f32 | stride: root distance travelled along z over the whole animation |
| u8 | flag (1 in every file; not used by pedestrians) |
| *F* × *C* × f32 | frames, channel-major per frame |

A file whose counts are out of range is refused (MM2 prints an error and the sequence has no
animation; does not happen with the tables' files). Every shipped file uses *a* = 0 and 60
channels. Channels 0–2 are the root bone's translation (metres, model space), then three angles
(radians) per bone in skeleton order ("Posing").

At load MM2 applies a **forward fix-up** to channel 2 (root z) of every frame *i* = 0 … *F* − 1:
step = −(stride / *F*) (one division, then the sign), z_i = z_i − ((*i* × step) + 0). Then the
sequence table's drift removal works on the fixed-up values.

Animations are cached by file name for the whole game (never reloaded, not even for another city
or session), so the fix-up runs once per file. The drift removal, however, runs every time a
sequence table row naming the file is read (each session that loads the model, and each model
naming it) on the shared data; with the shipped files every repeat finds x and z already 0 and
changes nothing.

## Skeleton (`.skel`)

Whitespace-separated text tokens:

```
NumBones 19
bone root {
    offset x y z
    [rotmin x y z]
    [rotmax x y z]
    bone spine { … }     (children, any number)
}
```

`NumBones` and its count, then the token `bone` (anything else: not a skeleton, the model has no
bones), then one bone block: the bone's name (at most 63 characters), `{`, `offset` and three
numbers, optionally `rotmin` and `rotmax` with three numbers each (joint limits; used only by a
ragdoll that retail MM2 never creates), then child blocks each introduced by `bone`, then `}`.
Bones are numbered in the order their blocks start (depth first, parent before children); the
first is the root. `offset` is the bone's position relative to its parent bone, in model metres
(the root's offset is replaced by the animation's root translation when posing). MM2 also pairs
bones whose names end in `_l` with the same name ending in `_r` (unused by pedestrians).

All four shipped models have the same 19 bones: `root`, `spine`, `neck`, `head`, `clavicle_r`,
`shoulder_r`, `elbow_r`, `wrist_r`, `clavicle_l`, `shoulder_l`, `elbow_l`, `wrist_l`, `pelvis`,
`hip_l`, `knee_l`, `ankle_l`, `hip_r`, `knee_r`, `ankle_r` (the man's root offset is
(0.00057, 1.14721, 0)).

## `.mod` model

A text model ("version 1.08/1.09"; MM2 also has a reader for a version 2.00 that does not ship).
Whitespace-separated tokens. Header, each a label and an integer:

`version: 1.09`, `verts: V`, `normals: Nn`, `colors: Nc`, `tex1s: T1`, `tex2s: T2`,
`tangents: Tg`, `materials: M`, `adjuncts: A`, `primitives: P`, `matrices: K`.

Then, in this order:

1. *V* lines `v x y z` — vertex positions, **in the frame of the bone they belong to** (the
   bone's origin and axes in the unposed skeleton: rotations zero, offsets added up);
2. *Nn* lines `n x y z` — normals, in the same bone frames;
3. colours: if *Nc* < 2 one line `c r g b a` that is read and ignored (no vertex colours),
   else *Nc* lines `c r g b a`;
4. *T1* lines `t1 u v`, *T2* lines `t2 u v` (texture coordinates; *T2* ignored);
5. *Tg* lines `ts x y z`, then *Tg* lines `tt x y z` (ignored);
6. *M* material blocks:
   ```
   mtl <name> {
       packets: n            (packet layout)      or   adjuncts: a  (global layout)
       primitives: p                                    primitives: p
       textures: t
       illum: <word>
       ambient: r g b
       diffuse: r g b
       specular: r g b
       [texture: <int> <name>]   (if t ≥ 1; a second one if t ≥ 2)
   }
   ```
   Alpha of the three colours is 1, emissive 0, power 0. An untextured material's diffuse and
   ambient r, g, b are multiplied by the night factor of `pkg.md` ("Materials", step 2). These
   materials are used only when the model has no `.shaders` file (never in shipped data).
7. the geometry, in one of two layouts, chosen by the first material: **packet layout** if it
   had `packets:`, else **global layout**.

An **adjunct** is a drawn vertex: (vertex index, normal index, colour index, tex1 index, tex2
index) plus the bone that moves it. The model's *K* = number of bones it uses (≥ 2 means
skinned).

**Packet layout** (`pedmodel_man`, `pedmodel_manw`): for each material in order, its *n* packets:

```
packet na nt nk [nr] {
    adj v n c t1 t2 [slot]      × na   (slot only if nk > 0)
    reskin …                    × nr   (only if the 4th number is present; never shipped)
    tri a b c                   × nt   (indices into this packet's adjuncts)
    mtx b0 b1 …                 (nk ≤ 8 bone numbers; only if nk > 0)
}
```

An adjunct's bone is `mtx[slot]` (bone 0 if *nk* = 0). Lines `mtxv …` and `mtxn …` after the last
material are not read in this layout (their counts agree with the packets' bones in the shipped
files).

**Global layout** (`pedmodel_woman`, `pedmodel_womanw`): *A* lines `adj v n c t1 t2`, *P* lines
`tri a b c` (indices into the whole adjunct list), then `mtxv` and *K* counts: the first count[0]
vertices belong to bone 0, the next count[1] to bone 1, and so on. Each material owns the next
*a* adjuncts and the next *p* triangles of the lists, in material order (triangle indices minus
the material's first adjunct); an adjunct's bone is the bone of its vertex. `mtxn` is not read.

After loading, MM2 moves translucent materials toward the end of the list. A material is
**translucent** if its diffuse alpha is below 1 (never, for a `.mod`: alpha is always 1) or it has
a texture that has alpha (`tex.md`); it counts as **opaque** for the move only if it has a texture
without alpha (an untextured material is neither). Steps, with *M* materials:

1. *j* = *M* − 1; while *j* > 0 and material *j* is not opaque: *j* = *j* − 1.
2. *i* = 0; while *i* < *j*: if material *i* is translucent, swap materials *i* and *j* together with
   their geometry (packets) and set *j* = *j* − 1; then *i* = *i* + 1 (the material swapped into *i*
   is not looked at again).

Only the model's own list moves: the paint variants' material lists are not reordered, and
drawing pairs the model's geometry group *i* (after the move) with material *i* of the chosen
paint variant. So with a `.shaders` file a move changes which variant material a group is drawn
with (MM2 quirk, kept). Shipped models are untextured, so nothing moves.

Shipped: man 117 vertices, 248 adjuncts, 230 triangles, 18 materials in 18 packets; man (rain)
128 vertices, 279 adjuncts in its packets, 260 triangles, 17 materials in 20 packets; woman 123,
696, 232, 17; woman (rain) 127, 723, 241, 16. In the packet layout the header's `adjuncts:` and
`primitives:` (and each material's `primitives:`) are read but not used: the packet lines give the
counts. The rain man's header says `adjuncts: 252` because a material split into several packets
repeats the adjuncts they share; every other header matches its packets or lists.

## Paint variants (`.shaders`)

The `shaders` entry layout of `pkg.md` ("Materials") on its own: set count and materials per set,
then the sets, with the same colour adjustments as a package. Each set is a **paint variant**;
its materials replace the model's, by index. Without the file the model has one variant (its own
materials). Shipped: dry models 48 sets, rain models 24, full-form records, untextured; the
variants change skin, hair and clothing colours. The materials-per-set count equals the model's
material count in every shipped model.

## Far detail (`.rays`)

Whitespace-separated tokens: a count, which must equal the skeleton's bone count (otherwise MM2
prints an error and the model has no far-detail figure: nothing is drawn far away); then per bone
(skeleton order) `a b h origin unused` (three numbers, two integers); then for each paint variant
(in set order) one material index per bone. Shipped: 19 bones; 48 index rows for the dry models,
24 for the rain models.

Each variant's **ray colours** are made at load from its materials: for material *m*, colour =
(255, trunc(r × 178.5), trunc(g × 178.5), trunc(b × 178.5)) (alpha, red, green, blue; each byte
taken modulo 256) with r, g, b its diffuse colour after the adjustments. Bone *i*'s colour in
variant *v* is ray colour [row *v*, index *i*].

## Posing

To pose a model from frame *j* of a sequence's animation (channels *c*₀ …):

1. For each bone *i*: angles (α, β, γ) = channels 3 + 3*i*, 4 + 3*i*, 5 + 3*i*; its local
   rotation (rows, row vectors as everywhere in MM2), with each angle's (cos, sin) = (1, 0) when
   the angle is exactly 0, else the double-precision cosine and sine rounded to float32:

   ```
   row X = ( cγ·cβ,                  sγ,        −(cγ·sβ) )
   row Y = ( sβ·sα − (sγ·cβ)·cα,     cγ·cα,     (sγ·sβ)·cα + cβ·sα )
   row Z = ( (sγ·cβ)·sα + sβ·cα,     −(cγ·sα),  cβ·cα − (sγ·sβ)·sα )
   ```

   and its local position = the bone's skeleton offset.
2. The root's local position = channels 0, 1, 2 (the root translation).
3. World-free pose: bone matrix = local × parent's bone matrix (row vectors: a point in the bone
   frame p maps to p·R_local + t_local, then through the parent), the root's parent being the
   identity. This gives each bone's matrix in model space.
4. A vertex (and its normal, without the translation) is moved by the matrix of its adjunct's bone
   only (rigid skinning, no blending), then by the pedestrian's world matrix (`../ai/pedestrians.md`).

The model's forward is −Z (its face looks along −Z, its left is −X): the behaviour code moves a
pedestrian along −Z of its world matrix and sideways along −X.

Drawing does not feed back into anything, so the float order of steps 3 and 4 is free.

## Playback

A pedestrian's animation state: current sequence, next sequence, frame (integer), paint variant.

- **Start** sequence *q*: current = *q*, frame = 0, next = *q*'s next sequence.
- **Queue** *q* (what the behaviour code does most of the time): next = *q*; the current sequence
  plays on to its end.
- **Clock.** MM2 has one frame accumulator for all pedestrians. Every time a pedestrian is
  updated (`../ai/pedestrians.md`, "Frame order"), at the end of its update: acc = *F* × 30 + acc,
  step = floor(acc) (an integer), acc = acc − step (float32 arithmetic, *F* = frame time), and then
  this pedestrian's animation advances by that step:
  - direction +1: frame = frame + step; if frame ≥ frame count: Start(next) (frame 0; leftover
    frames are dropped);
  - direction 0: if step ≠ 0: Start(next) (so `BACKUP` holds its single frame);
  - direction −1: frame = frame − step; if frame ≤ frame count: Start(next) (never shipped).

  The accumulator and the step are reset to 0 whenever any pedestrian is (re)placed or created.
  Every update counts, also a second update of a pedestrian in one MM2 frame and the update of a
  lost pedestrian (`../ai/pedestrian-walking.md`, "Stepping around").

  So the frames are shared out by position in the update order, not per pedestrian. With
  *F* = float32(1/60), 30*F* is exactly 0.5 in float32: the *j*-th update of a frame gets step 1 or
  0 alternately. With an **odd** number of updates per frame the pattern shifts by one every frame
  and each pedestrian averages 30 frames per second. With an **even** number it repeats exactly:
  the same positions get 0 in every frame, and those pedestrians' sequences never advance — they
  glide on in a frozen pose at their sequence's speeds, and a queued sequence never starts (a
  frozen walker does not stop at the kerb), though a sequence started directly by the behaviour
  code still takes over (at frame 0). MM2 does exactly this; nothing in the pedestrian code breaks
  the pattern. It changes only when the update order changes (a pedestrian moves to another
  road's list at a road end, a turn-back or a step-around reversal; roads join or leave the active
  list; pedestrians are placed), when a placement resets the accumulator while it is not 0, or when
  *F* changes. MM2's *F* is the measured frame time, so in play it jitters and the frozen set
  keeps changing; with a fixed *F* (check runs) the freeze is visible and must be reproduced.
- No frame blending: the pose drawn is always the integer frame (MM2's code for blending two
  frames is never used).

## Drawing

Each pedestrian is a city object in its room's object list (it moves between rooms as it walks)
and is drawn with the room like any object (`../engine/viewer.md`): visibility uses a sphere of
radius 2.0 m around the pedestrian's position. It casts no shadow, is never reflected, and is lit
and fogged like other objects (both levels of detail: MM2 leaves the fog as the object drawing has
it).

- World matrix: the pedestrian's matrix (rows X, Y, Z, position; `../ai/pedestrians.md`).
- **Near** (squared distance from the camera position to the pedestrian's position < 1225, i.e.
  closer than 35 m): pose the current frame and draw the model's triangles with the materials of
  the pedestrian's paint variant.
- **Far** (otherwise): pose the current frame; then for each bone *i* whose `a` is not 0: raise bone
  *i*'s posed position by `h` (in y, model space; this change is kept in the pose for later bones'
  `origin` look-ups), and draw a flat, unlit, untextured, fogged and two-sided (no back-face
  culling) strip in bone *i*'s ray colour through
  four points: P − a·R, P + a·R, O − b·R, O + b·R (a triangle strip in that order), with P bone
  *i*'s position, O the position of bone `origin`, and R the camera's right axis expressed in the
  pedestrian's model frame (the strip faces the camera, like a particle).

## Checked against

`tools/ai/peds.py` (dirty side) reads every file of `anim/` with these rules:

- The four tables give 24 sequences each with the frame counts, speeds and links listed above;
  `LDIVE` and `RDIVE`, which the behaviour code also asks for, are not in any table.
- 48 animation files are named by the tables, 19 are not; all 67 but `pedanim_manantrnch` parse
  to their last byte (60 channels; e.g. `pedanim_manwalk`: 20 frames, stride 1.5518;
  `pedanim_manrun` 12 frames, stride 2.9696). After the fix-up and the drift removal the root x
  and z of the first and last played frame are below 2.1e−8 in every sequence.
- Models: man (packet layout) 117 vertices / 18 materials, woman (global layout) 123 / 17; every
  packet's bones agree with the trailing `mtxv` runs (0 mismatches); drawn adjuncts / triangles /
  packets: man 248 / 230 / 18, rain man 279 / 260 / 20 (header `adjuncts: 252`), woman 696 / 232,
  rain woman 723 / 241 (the global layouts equal their headers); every model has 19 bones,
  `matrices: 19`, as many materials per paint variant as model materials, and a `.rays` file with
  one index row per variant.
- Posed renders (`work/check/ped_<model>_poses.png`: `STAND`, `WALK` frames 0/5/10/15, `RUN`,
  `WALK_ANTIC`, the dives, `GROUND_STANDL`, `BACKUP`) show correctly assembled figures, which
  confirms the bone-frame vertices, the angle order and the hierarchy;
  `ped_<model>_variants.png` shows paint variants near and the far-detail strips.
