# Questions from the engine build

Written by the builder while implementing `spec/` against the real data. Each says what is
needed, where, and what was observed. The engine keeps working with the choice noted.

## Archives and mods

1. **Order of mod archives** (`ar.md`, `engine/viewer.md` "Loading game data"). The spec says base
   archives load first and mods after, a later entry replacing an earlier one. In what order does
   MM2 load several mod `.ar` files in its folder (directory order, sorted by name, something else)?
   *Engine choice:* base archives in the order mm2core, mm2tex, mm2aud, mm2audex, then every other
   `.ar` sorted by file name ignoring case.

   **Answered:** MM2 lists every `*.ar` in its folder (max 256), upper-cases the names (ASCII letters)
   and sorts them by byte value; for any path the **first** archive in that order that has it wins.
   The base archives are not special (their own order is MM2AUD, MM2AUDEX, MM2CORE, MM2TEX), so
   `AAA.ar` overrides base files and `zzz.ar` does not. Loose files are never read once an archive
   exists. Also: archive directories are sorted with A–Z compared as a–z (not byte order) and MM2
   binary-searches them. Spec: `ar.md` "Several archives", `engine/viewer.md`.

2. **"A car already in the list is not added twice"** (`car-model.md`, "Which player cars exist").
   Is "the same car" decided by the `.info` file name (`tune/vpbug.info` vs a mod's
   `tune/VPBUG.INFO`) or by its `BaseName=`? *Engine choice:* file name without extension, ignoring
   case. (With a mod that overrides `tune/vpbug.info`, the VFS already gives one file, so this only
   matters for two different files naming the same `BaseName`.)

   **Answered:** by `BaseName`, compared exactly (case-sensitive); the first car with a given
   `BaseName` wins (built-ins first, then `tune/` in archive priority and entry order). The file name
   is irrelevant. A car whose `.info` fails the fixed twelve-line reading (`car-model.md`, "Which player
   cars exist", "Reading a `.info` file") is also skipped. Spec: `car-model.md`.

## Packages and cars

3. **Shadow at low levels of detail** (`car-model.md`, "Levels of detail on cars" and "Showing a
   still car" step 6). The LOD table does not list `SHADOW`. Shipped shadows have only `_H` and
   `_M`, so by the fill rule they are missing at L and VL. Does MM2 draw no shadow when the car is
   at L/VL, or does the shadow use its own level choice? *Engine choice:* the shadow at the car's
   level if present, else `_M`, else `_H` (so the still view always has one).

   **Answered:** the shadow has its own pass and its own choice: always `SHADOW`'s H slot (after the
   upward fill: `_H`, else `_M`, `_L`, `_VL`), drawn for every car whose room is within the room
   limit, whatever the car's level (traffic too). Spec: `car-model.md` "Shadow".

4. **`VARIANT<n>` at L and M** (`car-model.md`). The still recipe draws `VARIANT<s>` (H); the LOD
   table's L/M row does not mention variants. Are variant parts drawn at L/M? None ship.
   *Engine choice:* drawn at H, M and L like the breakables, not at VL.

   **Answered:** yes, at H, M and L at the car's current level (never at VL), but only if the part
   has an `_H` mesh; because levels fill upward only, a variant with only `_H` shows only at H. Spec:
   `car-model.md`.

5. **Traffic wheels below H** (`car-model.md`, LOD table, "wheels only if a detail option is on,
   then at L"). Which option is it and what is its default? *Engine choice:* on (wheels at L).

   **Answered:** it is an internal switch with no menu or config entry, and nothing in the game
   ever turns it on: retail MM2 never draws traffic wheels below H. Spec: `car-model.md` LOD table.

6. **Environment reflection** (`car-model.md`, "Reflection pass"). What projection does MM2 use for
   `refl_dc` on cars (sphere map from the view-space normal, camera-space reflection vector, a
   fixed planar projection…), and what is the "current environment strength" at default settings
   (and is it per city / per time of day)? *Engine choice:* sphere map from the view-space
   reflection vector, added with strength power × 0.6; the car viewer uses `refl_showroom` when it
   exists, else `refl_dc`.

   **Answered:** coordinates come from the world-space vertex normal: u = 0.5 + 0.5·n.x,
   v = 0.5 − 0.5·n.y (view-independent). The strength is always 1.0 (not per city, time or weather):
   the pass adds `refl_dc` × trunc(255·power)/255 (additive one/one), only on `BODY_H` at level H, and
   only while the "Vehicle Reflections" option is on (on by default with a 3D card). The showroom
   uses `refl_showroom`. Spec: `car-model.md` "Reflection pass".

6b. **Trailer condition** (`car-model.md`, "Trailers" and "Showing a still car" step 7). The spec
   attaches a trailer only if the car has a `TRAILER_HITCH` *mesh*, but `vpcentury.pkg` has no
   `TRAILER_HITCH` mesh (only `vpcentury_TRAILER_HITCH.mtx`), while `vpcentury_trailer.pkg` exists and
   the spec's own check renders show vpcentury with its trailer. Does MM2 test the mesh, the `.mtx`,
   or something in `tune/` (`vpcentury.vehTrailer`, `.dgtrailerjoint`)? *Engine choice:* attach when
   the trailer package exists and the car has the hitch mesh **or** the hitch `.mtx`. With that,
   vpcentury's trailer sits at (0, 0, 7.310).

   **Answered:** MM2 tests the mesh: a trailer is attached only if a `TRAILER_HITCH` mesh exists
   and the hitch `.mtx` origin is not (0, 0, 0). The tune files play no part. **vpcentury gets no
   trailer in MM2** (vpsemi, and the unlisted vpftruck and vplafrance, do). Spec: `car-model.md`
   "Trailers".

## Textures

7. **BMP row length** (`tex.md`, ".bmp"). "MM2 assumes rows are exactly the width (rounded up to
   even)": is that the width in pixels rounded up to even and then times bytes per pixel (24-bit:
   `roundup2(w) × 3`), or the row's byte count rounded up to even (`roundup2(w × 3)`)? And does MM2
   honour the header's pixel-data offset (`bfOffBits`) or read straight after the palette?
   *Engine choice:* `roundup2(w) × bytes per pixel`, starting at `bfOffBits` when it is non-zero.
   No shipped file is affected.

   **Answered:** `roundup2(w) × bytes per pixel` (width made even first). Pixels are read from
   `bfOffBits` always, even when it is 0. The 8-bit palette is always 256 entries read from offset 54
   (4-byte entries; 3-byte with the 12-byte header), ignoring the header's size fields. Spec:
   `tex.md` ".bmp".

## Cities

8. **Inverted building walls: count** (`psdl.md`, "Type 11"). The spec says about 1,300 London
   walls have bottom above top. Skipping every type-11 wall whose bottom height is above its top
   skips **1,555** in London and **2** in SF. The "Triangles generated" table counts them: with them
   skipped the totals are London 94,518 (97,628 − 2 × 1,555) and SF 93,113 (93,117 − 2 × 2); every
   other count (all element counts, sfai, variant, city) matches exactly. Is 1,555 right, and are
   any of those walls ones the game does draw (e.g. the 2 in SF)?

   **Answered:** 1,555 (London) and 2 (SF) are right, and the totals 94,518 / 93,113 are confirmed.
   The old "about 1,300" counted only walls with a negative v (1,291; 926 of them 65533–65535); 264
   more have v = 0. The SF two are the same kind (bottom above top, negative v). None is ever visible:
   the facing test passes from outside but the fan winds backwards and is culled; from inside the test
   skips it. Skip them all. Spec: `psdl.md` "Type 11".

9. **Object material-set cap** (`inst.md`, "Variant"). "While loading `.inst` objects the game caps
   the sets it keeps per package at a value taken from the current race setup." Which value, and
   what is it in a plain cruise at each time of day? (It changes which set `variant mod sets`
   picks.) *Engine choice:* no cap; all sets kept.

   **Answered:** the cap comes from a settings field that is always 99 (all modes, times of day and
   weathers). No shipped package has more than 48 sets, so the engine choice (no cap) gives the same
   result; only a mod package with over 99 sets would differ. Spec: `inst.md` "Variant".

10. **Room limit distance** (`inst.md`, "Drawing"). "Objects of a room are drawn only while the room
    is closer than the room limit": distance from the camera to what — the room's bounding-sphere
    near side (as for PSDL levels), its centre, or the PVS? And which room counts for bit-29
    objects that span rooms? *Engine choice:* near side of the sphere around the room's perimeter;
    spanning objects use their own distance (object position − radius) against the room limit.

    **Answered:** room distance = depth of the room's bounding-sphere centre along the view direction
    minus its radius (near side, measured as view depth; the camera's own room counts as −radius); the
    PVS only selects candidate rooms. But the room limit does **not** apply to static objects,
    landmarks or spanning objects at all — only to breakable props (room distance ≤ limit and own
    depth ≤ limit). Spanning objects are listed in the header room's touched neighbours (not the
    header room), drawn once per frame with their own distance and no one-sided test. Object distance
    is also view depth − radius. Spec: `inst.md` "Drawing", "Spanning objects"; `psdl.md` LOD.

11. **Blending of city objects** (`inst.md`, "Drawing"). Static objects use the room cut-off (alpha
    > 100) "in the same pass as the rooms", and rooms are not blended. Are static objects and
    landmarks with alpha textures drawn alpha-blended (the `pkg.md` rule) or only alpha-tested?
    *Engine choice:* the `pkg.md` rule (blended when the texture has alpha or diffuse alpha ≠ 1)
    plus the cut-off.

    **Answered:** blended. MM2 switches blending and the alpha test together, so alpha batches of
    static objects and landmarks are blended **and** tested at > 100 (from the first such batch of the
    model on; whether a model can blend is decided from its material set 0). The rooms are blended
    too: alpha-textured room surfaces are drawn blended and tested at > 100 after the opaque ones.
    Spec: `inst.md`, `pkg.md` "Translucency", `psdl.md` render state.

12. **Sky and fog** (`city-files.md`, "Sky"). Is the sky dome fogged? *Engine choice:* not fogged.
    Also: what colour does the game clear the screen to (it shows through gaps such as the inside
    of blocks)? *Engine choice:* the current fog colour.

    **Answered:** the sky is not fogged (fog is on only during the room/object pass). The screen is
    cleared every frame to the current fog colour (black on the very first frame). Fog range is
    clamped to the far plane: min(start, far − 30) … min(end, far). Spec: `city-files.md` "Sky", "Fog".

13. **Attached props (`xrefs`) in cities** (`pkg.md`, `inst.md`). Which material set do xref props
    use (the owner's variant or 0), and which room do they join for the room limit when "the room
    that contains its position" is a stacked or missing room? *Engine choice:* set 0; the owner's
    room.

    **Answered:** the owner's variant byte (as stored in the `.inst` header), modulo the prop's own
    set count — not set 0. Room: the room containing the prop's own position (owner's room first, then
    the owner's neighbours, then the whole city; stacked rooms need the height in range); if none,
    room 0, which is never drawn. xref props are breakables keeping only position and turn about Y.
    Spec: `inst.md`, `pkg.md` "xrefs".

14. **Junction tunnel bends** (`psdl.md`, "Junction tunnel", raised junction outside walls).
    `oA = (oₓ − 1.414·o_z, 0, o_z − 1.414·oₓ)` is implemented as written; please double-check the
    signs (a 45° turn of `o` would be `(oₓ − o_z, 0, o_z + oₓ)` scaled). It only affects raised
    junction corners.

    **Answered:** the formula is correct as written, for both bendA (words 6–7) and bendB (words
    8–9), signs included; it is not a 45° turn. Spec: `psdl.md` (note added).

15. **Car level-of-detail distances in a city** (`car-model.md`: "thresholds belong to the renderer
    spec"). *Engine choice for dropped cars:* H under 40 m, M under 100 m, L under 200 m, VL beyond.

    **Answered:** cars use the city-object rule: *d* = view depth of the car origin − body radius,
    thresholds T1/T2/T3 from the "Object Detail" setting (40/100/200 at setting 2, the game's built-in
    values; 70/130/200 at setting 3, which MM2's setup picks on modern machines). Cars ignore the room
    limit. Spec: `car-model.md` "Levels of detail on cars", `inst.md` table.

## Driving (milestone 2)

16. **Steering rates: which speed** (`engine/controls.md`, "Speed-sensitive steering rates", step 6
    of "Order of one frame"). The rates are "recomputed every frame after the car's inputs are set,
    from the car speed v", before this frame's physics. *Engine choice:* the unsigned speed of the
    last physics step (vehicle.md step 2 value, the same one the automatic reverse uses).

    **Answered:** the engine choice is right. MM2 recomputes the rates in the player update, which
    runs after the inputs are handed to the car and before the frame's physics, and reads the car's
    speed field: the unsigned |Z · v| written at the start of the last physics step (`vehicle.md`
    step 2) — the same value the automatic reverse and the parking brake read. So frame *N*'s key
    smoothing uses rates computed in frame *N* − 1 from frame *N* − 2's last physics step; 0 after a
    reset; *DeltaIn* 1 / *DeltaOut* 1 / *Filter* 2 before the first recomputation. Spec:
    `engine/controls.md`, "Speed-sensitive steering rates".

17. **Check-run segment length in frames** (`engine/drive-check.md`). How many frames does a segment
    of d seconds run? *Engine choice:* round(d / 0.016666668) frames (1.0 s = 60, 25 s = 1500), one
    control step per frame, and the gear of a `gear=` segment set on every frame of the segment.

    *Answered:* yes, round(d / F) frames with F = 0.016666668, controls (including `gear`) applied
    every frame of the segment. Now stated in `engine/drive-check.md`.

18. **`KsB` when `vN` is exactly 0** (`physics/vehicle-wheels.md`, "Suspension update", bottomed
    out). With `vN = 0`, `j = 0`, `pushF = 0` and `KsB = −(0/0)` is NaN; the tyre call's `Ks > 0`
    test is then false, so no implicit contact is added. Is that MM2's behaviour (NaN stored, no
    contact), or does MM2 guard the division? *Engine choice:* as written (NaN, no contact).

    *Answered:* as written. MM2 divides without a guard; the game runs with all x87 exceptions
    masked, so 0/0 gives NaN, which is stored as `Ks`. The tyre call compares `Ks` with 0 using an
    unordered-aware test, so NaN (and the −0 that `vN > 0` gives) adds no implicit contact, and the
    next step's contact call overwrites `Ks`. Noted in `physics/vehicle-wheels.md`.

19. **Brake check run reaches 23.6 m/s, not 25** (`engine/drive-check.md`, `brake_vpbug`). With 6 s of
    full throttle our vpbug reaches 23.6 m/s (it starts with the 0.25 s clutch delay). The run file
    uses 6 s as the table says; tell us if the reference reaches 25 m/s instead.

20. **Camera ground latch timers** (`engine/cameras.md`, chase step 2): "after 0.1 s" — strictly
    more than 0.1 s, or at least 0.1 s? *Engine choice:* more than 0.1 s.

    **Answered:** strictly more than 0.1 s. Each timer adds the whole frame time *F* (the clamped
    frame duration, not the physics step), once per frame in the camera update after the frame's
    physics; no other clamping. A timer is zeroed only when the *other* one fires, so spells add up
    across interruptions. The latch starts "on ground" with both timers 0 and camera resets leave them
    alone; frames where `TrackBreak` fires do not touch the timers. Spec: `engine/cameras.md`, chase
    step 2, and the frame-time note at the top.

21. **City driving before the collision spec**: Drive mode currently probes the PSDL triangles with
    the default material (friction 1), and where no PSDL surface is found (the car has no body
    collision yet and can drive into a block) a plane at the last ground height. This is a stand-in
    until `physics/collision*.md` and `city-collision.md` are final; no question, just so the
    reference is not compared with city behaviour yet.

    *Resolved:* Drive mode and the check runs now use the final collision (wheels, body, objects,
    cameras, world limits); the stand-in is gone.

## Collision (milestone 3)

22. **City polygon totals are off by a few surfaces** (`physics/city-collision.md`, "Checked
    against"). Every per-kind count in the table matches exactly: rooms (1,341 / 1,171), facade walls
    (9,618 / 12,537), roof triangles (9,619 / 11,699), curb faces road / sidewalk / divided
    (1,084 / 5,002 / 68 and 1,447 / 4,917 / 230) and tunnel walls (239 / 754). The totals are still
    London 41,469 (+1) and SF 49,515 (+143), so the extra polygons are surfaces. Our surface
    counts per element type, London / SF: road surfaces and sidewalks of type 0 1,779 / 2,909;
    sidewalk tops (type 1) 5,133 / 5,014; ground strips 212 / 68; crosswalks 697 / 648;
    intersection fans 3,879 / 3,217; ground fans 3,877 / 5,115; divided-road surfaces 218 / 740
    (plus 44 / 220 median side walls). Dropped for a zero cross product: London 61 (type 1: 15,
    type 6: 31, type 7: 2, type 9: 13), SF 326 (type 0: 189, type 1: 25, type 2: 2, type 6: 31,
    type 7: 2, type 8: 2, type 9: 75). The "p′ index 0" strip rule never fires in shipped data.
    Triangle-mode rooms make 197 polygons (27 London rooms) and 2,125 (77 SF rooms). Which rule are
    we missing? The reference's surface counts per element type would show where.
    *Engine choice:* as written; the native test prints the difference as a note.

    *Answered:* no rule is missing; the old table was wrong. My checker had computed the curb and
    wall heights in double precision, which keeps a few near-degenerate surfaces that single
    precision drops. Redone in single precision, it gives exactly your totals, London 41,469 and
    SF 49,515, and your per-type counts and drop counts. `physics/city-collision.md` now has these
    totals plus the counts per element type, and says the heights must be single precision. Also
    new there: the polygon normal's exact order, L = (n.y² + n.x²) + n.z².

23. **Capsule props: what a found duplicate does** (`physics/collision-object-contacts.md`, 5.3
    "Committing"). The duplicate search finds a record i, but the text does not say what the
    commit then does with it. Replace record i? Update it only if the new contact is deeper? Drop
    the new contact? *Engine choice:* a found duplicate drops the new contact (it is not written
    and no merge rule runs).

    *Answered:* right, the new contact is dropped. The one exception is pass 4, which deletes the
    old record and goes on with the new one. 5.3 now says this.

24. **Edge-edge "previous value" of the depth** (`physics/collision-contacts.md`, 3d step 3: "if
    the test gave up before computing a depth, the previous value stays"). Previous from where,
    and what is it before any test has computed a depth? *Engine choice:* one depth variable per
    contact call (per city test and per object pass), starting at 0, updated by every test that
    computes a depth and carried over to later records.

    *Answered:* right. MM2 keeps one depth variable for the whole edge-against-edge step of a test
    and never sets it before the first test, so the start value is undefined there; 0 is the
    spec's choice. 3d step 3 now says so, and adds that *flag* is always set.

25. **3d step 4, "repeat 1–3 for Q"**: in the repeat of step 3, are the records "P" and "record
    i+1" still P and Q (so a flagged Q test clears *face* on both, and the depth/2 rule looks at
    both depths)? *Engine choice:* yes, P and Q.

    *Answered:* yes. The repeat uses Q's point for the velocity, Q as the record tested and P's
    dir; step 3 then works on P and Q again with the second test's flag and depth. If the second
    test is ok, P (not Q) is marked *done*, and its impact is written only if the first test was
    not ok. 3d step 4 now spells this out.

26. **Breakable props, simplified** (`physics/collision.md`, "Breakable props"). Which x is
    compared with ImpulseLimit2, and which impulse is scaled? *Engine choice:* the impact's
    sticking impulse (the solve of x·K_A = b, before the friction cone and the (1 + e) factor) is
    compared; if it is over the limit, the final impulse (after cone and (1 + e)) is scaled to
    length √ImpulseLimit2, the prop is marked broken, it leaves collision for the rest of the run
    and the renderer stops drawing it. Props with ImpulseLimit2 0 (9 shipped types) break on any
    touch. Pushes from a prop use its own identity (1000 + its object index).

    *Answered:* the compared x is right: the sticking impulse from x·K_A = −vrel, with
    |x|² = (x.z² + x.y²) + x.x². The scaling is not right. MM2 caps that sticking impulse,
    x_cap = x·√(ImpulseLimit2/|x|²), not the final one. The car keeps x_cap, and the rest is solved
    against the prop's moving body. For this milestone (the prop treated as infinitely light),
    J_A = x_cap·w with no friction cone, no (1 + e) factor and no push from that impact. Impacts
    under the limit use the full rule with K_B = 0. Each impact of a test is judged on its own
    (same velocities); the prop breaks if any of them broke it, and the break takes effect after
    the test's whole impact list. Props come back on a race reset, a world-limit reset and at the
    start of each check run. `physics/collision.md`, "Breakable props", has the exact steps.

27. **Water effect: Size and CoG** (`physics/collision.md`, "World limits", water effect). The
    vehicle pages have no "size vector". *Engine choice:* Size = `InertiaBox`, CoG =
    `CenterOfGravity` as written, both taken in the rigid-body frame (so the 64 points are
    centred at body position + R·CoG, not at the centre of mass). Please confirm or correct.

    *Answered:* confirmed. "World limits" now gives the grid formula, the loop order (i outer, k
    inner) and the world-transform summation order. It also notes that MM2 draws 192 random numbers
    while setting the grid up; they come before the generator restarts, so they do not matter.

28. **Model radii** (`physics/collision-objects.md`, "Gathering", step 1, and the trivial test).
    The car's radius is "the radius of its model (largest mesh radius)": which meshes? And a
    prop's radius for the trivial test? *Engine choice:* the car uses the largest mesh radius over
    every mesh of `geometry/<car>.pkg` (all LODs, wheels and parts); a prop uses the largest mesh
    radius of its own package, measured from the model origin but compared at the prop's position
    (its CG).

    *Answered:* no, not every mesh. The car's radius is the radius of its `BODY` part only: the
    largest over `BODY_H/M/L/VL` of the largest vertex distance from the origin (`pkg.md`,
    "Bounding radius"). Wheels and other parts do not count. An object or prop uses its own model
    part (the meshes `inst.md` draws), measured from the model origin; a prop's is used at its
    position (CG), as you have it. A landmark's radius can grow to the distance of its bound box's
    min or max corner (only those two corners; `collision-objects.md`, "Landmarks").
    `physics/collision-objects.md`, "Gathering" step 1, now says this.

29. **City-run placement** (`engine/drive-check.md`, city runs, against `physics/vehicle.md`,
    "Placing the car in a city"). drive-check.md says `start` is where "the car's origin is
    placed", vehicle.md says MM2 sets the body position to q + CoG (origin at q + CoG + R·CoG).
    *Engine choice:* MM2's rule (body = start + CoG unrotated), with the heading's cos and sin
    computed in double precision and rounded to float, as for flat runs. The start lines are
    already "intersection + 2 m".

    *Answered:* MM2's rule, as you did. `engine/drive-check.md` now says so.

30. **Bump randomness in city runs** (`physics/vehicle-wheels.md`, "Bumps"). The spec says bumpy
    surfaces are not used by check runs, but the city runs drive over grass, dirt and sand
    (`materials.mtl` heights 0.04, 0.01, 0.05), so their CSVs depend on `rnd`. With the C
    library's `rand()` the second of two identical runs in one process gave a different CSV.
    *Engine choice:* one shared generator in the style of the game compiler's `rand()`
    (x = x·214013 + 2531011, rnd = ((x >> 16) & 0x7FFF) / 32768), restarted with x = 1 before
    every check run, one call per bump update. The reference needs the same rule (or no bumps in
    check runs) before the city CSVs can be compared.

    *Answered:* your generator is MM2's exactly: x = x·214013 + 2531011, value
    ((x >> 16) & 0x7FFF)·3.0517578e-05, one draw per wheel per step on a bumpy material. MM2 resets
    it to 1 on city load and on every race reset. In the real game the same generator also feeds
    traffic, pedestrians and visual effects (dust, sparks, smoke), so the game's bump sequence
    cannot be matched number for number. The spec rule is your choice: a physics-only generator,
    x = 1 at the start of every check run and again at a world-limit reset (MM2's race reset
    restarts it too); visual effects get their own generator. `physics/collision.md` ("Random
    numbers", "Resets in check runs") and `physics/vehicle-wheels.md` now agree on this. The
    reference must follow the same rule.

31. **Smaller choices we would like confirmed** (no answer needed if they are right):
    - Polygon-edge records (`collision-object-contacts.md` 2.3) never join a corner group (3b
      membership, blocking and suppression skip them), since their depth "is not used anywhere".
    - `dgBangerData` keys (Size, CG, Elasticity, Friction, ImpulseLimit2, YRadius, CollisionPrim,
      CollisionType) are read as datParser text with the usual key: value lines; a missing
      CollisionPrim means 1, missing Elasticity/Friction keep the built-in default's 0.5 / 1.
    - Normalisations whose evaluation order is not listed use the canonical order
      ((x² + y²) + z², then a multiply by 1/√).
    - The object probe's transforms into object coordinates use the canonical order
      (x, then y, then z terms).
    - World-limit resets in check runs: the car goes back to its `start` with the run's placement
      rule; the run's frame and step counters keep going.

    *Answered:* bullets 1 and 5 are right; 2, 3 and 4 need corrections.
    (1) Right: polygon-edge records never join a corner group. Their only role is the 1.03 rule,
    which compares a record with the polygon-edge record before it.
    (2) Wrong: `.dgBangerData` is read mostly by position, not by key (`collision-objects.md`,
    "Reading `.dgBangerData`"). A missing `CollisionPrim` means **0** (the `.bnd` if there is
    one, else the box). The defaults are Friction 0.9, Elasticity 0.5 and CollisionType 0x10. Of
    the 18 files without the key, 2 have a `.bnd`.
    (3) Not always: MM2 uses different summation orders at different sites. Every
    normalisation in the collision pages now gives its order. Polygon normals use
    (y² + x²) + z²; in-plane edge normals, edge normals and in-plane edges use the canonical
    order; the car-edge direction and the line-to-line normal use (z² + y²) + x². All divide 1 by
    √L, then multiply.
    (4) No: the transform uses (w.z·A.z + w.y·A.y) + w.x·A.x per row, and the way back has its own
    orders. `collision-objects.md`, "Probing an object", now has every formula.
    (5) Right. `collision.md`, "Resets in check runs", adds: props come back, the water effect
    goes off, and the generator restarts.

## Rendering (draw distance, landmarks)

32. **One-sided test on landmarks** (`formats/inst.md`, "Drawing": "Static objects and landmarks
    are skipped while the camera is behind them: (camera − position) · Z axis < 0"). Applied to
    landmarks, this hides whole landmark blocks, ground included, whenever the camera is on their
    −Z side (every London landmark has Z = (0, 0, 1), so "south of the centre"). Examples, with the
    block showing only the clear colour: Westminster Abbey (`wl_westabbey_l`, room 57) from
    (300, 30, 200) looking west-south-west; the Houses of Parliament (`wl_parliment_l`, room 62)
    from above the Thames at (560, 250, 250); the Albert Hall, the Albert Memorial and Hyde Park's
    lake (`kl_hydeparklake_l`) from the south. Ant reported these as "water not drawn" (the lake
    and the riverside blocks). MM2 surely shows these buildings from every side. Does the one-sided
    test really apply to landmarks (or only to plain static objects, or with some tolerance)?
    *Engine choice:* plain static objects only; landmarks are always drawn. (SF's garages, which
    Ant also reported, are plain static facades on a closed PSDL block, room 159: from inside the
    block every surface is a back face, so the empty view follows the spec; the block's walls do
    collide, see the report.)

    **Answered:** the engine choice is right; the old wording was wrong. Only plain static objects
    (header bits 24, 25 and 29 all clear) are one-sided. The loader marks every landmark (with or
    without bit 26) and every spanning object (bit 29) as exempt, and breakable props, xref props,
    street props and cars use a visibility test without it. The test itself: hidden when
    (C.x − P.x)·Z.x + (C.z − P.z)·Z.z < 0, with C the camera's world position (eye point), P the
    placement's translation (pivot, not the sphere centre), Z the stored Z axis (length irrelevant);
    no height, tolerance, distance or angle term, 0 counts as visible, always on once a city is
    loaded. (A Z = (0, 0, 1) object is hidden while the camera's z is smaller than the pivot's, i.e.
    north of it: −Z is north. Both examples above have the camera at a smaller z than the pivot,
    so the sign you used matches.)
    The fly-view holes that remain with landmarks exempt are the levels rule, not this test: 41
    London and 53 SF block rooms have no PSDL elements at all and get their ground only from a
    landmark model, and 117 London Underground surfaces plus the Tower, Waterloo and Lambeth bridge
    decks are hidden with texture slot 0 for the same reason. 122 of London's 176 landmark
    placements and 23 of SF's 88 have no `VL` mesh (Westminster Abbey, St Paul's, Hyde Park's lake,
    the Lambeth and Waterloo river sections, Coit Tower, the Hyatt plaza…), and missing levels are
    only filled from below, so beyond T3 (*d* > 200 at settings 2 and 3) they are not drawn and
    their blocks show the clear colour. MM2 behaves the same. No block depends on a plain static
    object for its ground, so the one-sided test can only reveal PSDL ground and roofs. Spec:
    `inst.md` "Kinds", "Drawing" (one-sided objects, missing levels); `psdl.md` "Ground supplied
    by landmarks".

33. **What the dash and bumper views hide of the player's own car** (`engine/cameras.md`: "While
    the bumper or dash camera is active, the car body is not drawn"; `formats/car-model.md`,
    "Shadow"). Is "the car body" only the `BODY` part or the whole car (wheels, decal, breakables,
    light glows)? And is the car's own shadow (drawn in its own pass after the city) hidden too?
    In the dash view the shadow is drawn just under the camera and looks wrong. *Engine choice:*
    in both views the whole player car (all parts, light glows and headlight billboards) is
    hidden; in the dash view its shadow is hidden too; in the bumper view the shadow stays.

    **Answered** (`engine/cameras.md`, `formats/dash.md` "What the dash and bumper views hide"):
    both views hide the whole player car, shadow included; a trailer stays visible. Implemented.

## Cockpit and mirror

34. **The mirror's 100 m far plane cuts the sky** (`formats/dash.md`, "Rear-view mirror";
    `formats/city-files.md`, "Sky" and "Fog"). With the default `FarClip` 100 the mirror's
    projection clips the sky dome (drawn at the camera with depth test off, but still inside the
    far plane) as well as every surface past 100 m, and the rectangle is cleared to black. The
    result is a black band along the horizon between the fogged ground (the fog is clamped to
    70 → 100 m) and the part of the dome that is nearer than 100 m (capture
    `work/one/shots/M_vpbug_chase.png`, London clear noon; the same band at night is barely
    visible). Does MM2 show that band, or does its sky stay whole in the mirror (e.g. the dome
    drawn with its own far plane, or the mirror cleared to the fog colour like the main view)?
    *Engine choice:* as written: black clear, the dome clipped like any other geometry.

    **Answered** (`formats/dash.md`, "Rear-view mirror", Render): the black clear is right and MM2
    does show a band, but the dome is never clipped. The mirror pass sets its camera before
    drawing the city, and the sky is placed from "the camera", i.e. the **mirror camera**:
    (mirror x, mirror y · b + a, mirror z), unscaled, same projection as the rest of the pass (no
    separate far plane, no scaling). The dome model is a hemisphere of radius 42.9 m (every vertex
    of `sky_dome`, `sky_dome_l`, `sky_dawn` is 42.9 m from its origin), so it always lies well
    inside 100 m. The only black left is the gap between the ground's far edge (100 m) and the
    dome's lower rim (42.9 m out, `0.05 · y − a` below eye level): a thin band along open
    horizons when the camera is low; buildings nearer than 100 m cover it elsewhere. If your dome
    is cut, it is probably centred on the main camera or drawn larger than the model.

35. **Which pose the mirror camera hangs on** (`formats/dash.md`, "Rear-view mirror": "car pose
    (physics body)"). The physics body sits at the centre of mass; the drawn model's origin is
    body position + R·CoG (`physics/vehicle.md`, "Centre of mass"; about 0.1 m apart for most
    cars). `cameras.md` defines "car pose" as the model's pose as drawn. Which origin does
    `Position` start from? *Engine choice:* the physics body (its position and rotation), as the
    parenthesis says.

    **Answered** (`formats/dash.md` fixed): the **drawn model's pose**. The mirror camera is its
    local frame times the car's world matrix that the body is drawn with (rotation of the body,
    origin = body position + R·CoG, recomputed after every physics step), not the rigid body's
    own pose at the centre of mass. The parenthesis was wrong.

## Road network and traffic control

36. **Traffic-light poles in the check runs** (`ai/traffic-control.md`, "Setting up a light set"
    step 2; `engine/drive-check.md`; `engine/drive-trace.md` "Exact meanings" 5). The poles are
    breakable street props, so they now stand in every city world, including the city check runs.
    One of the 34 runs changes: `london_w_vpbus` hits the stop sign (object 269) as before and then
    the pole `sp_traflitsingle_ped_l` of the approach at (−255.1, 1.5, 348.1) (its CG point) at
    t ≈ 5.73 s, so its CSV differs from there on; the other 33 are byte-identical. Should the check
    runs (and the reference) have the poles, as MM2's Cruise does? And what object ids do they get
    in the trace? *Engine choice:* poles present; they are appended after the `_ai.inst` objects,
    in light-set order (intersection order, then light order), so their ids follow the
    `_ai.inst` ones (London: 519 poles, the bus's is 442).

    **Answered:** yes, the poles belong in the city runs (and the reference): MM2 creates them
    whenever it loads a city's AI network, in every mode, so they are part of MM2's city. Your ids
    are right: object ids count objects in creation order, and the poles are created after the
    `.inst` and `_ai.inst` objects, in light-set order (intersection order, then light order).
    How they collide (now in `ai/traffic-control.md`, "Setting up a light set" step 2): exactly like
    the `_ai.inst` stop signs — an untouched breakable prop that keeps only its turn about Y,
    position g = sign position + R·CG (CG.x = CG.z = 0 for all three models, so g is CG.y above the
    sign position: 1.67468 for `sp_traflitsingle_ped_l`), room Find(g, 0), bound from its
    `.dgBangerData`: `CollisionPrim 2`, a vertical capsule of radius `YRadius` and length `Size.y`
    centred on g (London 0.072037 × 3.34936; the arm is not solid), `CollisionType` 16, its own
    mass, elasticity, friction and `ImpulseLimit2`. One ordering detail for gathering: MM2 inserts
    each pole at the **front** of its room's object list (head insertion), after the city's objects
    were loaded, so in a room the poles are gathered before that room's `.inst`/`_ai.inst` objects,
    the last created pole first. (MM2 adds every non-static instance at the head of its room's
    list, so the physics writer should check that `physics/collision-objects.md`'s "room's list
    order (objects in load order)" means the same thing; it matters only when several objects of
    one room are gathered.)

37. **"Checked against" numbers that need a definition** (`formats/bai.md`, `ai/traffic-control.md`,
    `ai/road-network.md`). Everything else in those lists matches exactly; these depend on how
    they were counted:
    - "X = Y × (−Z) to 0.02 on all but 20 and 44 sections": the 3D length of X − Y × (−Z) exceeds
      0.02 on 14 London and 40 SF sections (largest component: 14 and 38), all on shortcuts.
    - "Z[0] = Z[1] on 562 of 610 London and 389 of 428 SF roads": to 1e-5 we get 562 and 390;
      exactly equal floats 556 and 387.
    - "poles stand 0.1 m below the intersection centre's height (London 508 of 519)": the
      differences are 0.065–0.075 m (intersection 11: centre y −0.082, signs −0.150); 508 London
      poles are 0.05–0.15 m below.
    - "Every stop approach's sign position matches a `sp_stop_f` in `<c>_ai.inst` to 0.0 m": true
      in x and z; the sign position is 0.15 m lower than the placement (London −0.15 vs −0.00, SF
      14.85 vs 15.00).
    - "every non-loop end's slot equals the road's position in its list (London 1,077 ends, SF
      757)": all 1,074 / 756 non-loop ends match; 1,077 / 757 is the count of all ends that match,
      one end of each loop included.
    - SF intersection 2 example: "turns onto road 114's outgoing lanes" — road 114 is not in
      intersection 2's list (115, 2, 127, 1). Our rules give road 115 side −1 lane 0 → road 127
      side −1 lane 0 (rule 5) and lane 1 → road 2 side +1 or road 127 side −1 lane 1 (rule 6).

    **Answered:** your counts are right; the pages now give them with their definitions.
    `formats/bai.md`: the 3D length of X − Y × (−Z) is ≤ 0.02 on every main-road section and exceeds
    it only on 14 London / 40 SF shortcut sections (I had normalised the cross product first);
    Z[0] equals Z[1] as floats on 556 / 387 roads, 562 / 390 to 1e-5 per component; slots: every end
    of a non-loop road matches (1,074 / 756). `ai/traffic-control.md`: sign positions lie within
    0.09 m of the intersection centre's height (London −0.086 … +0.075, SF −0.086 … −0.050; "0.1 m"
    was a rounding); stop-sign positions equal the `_ai.inst` placements in x and z, 0.15 m lower in
    y. `ai/road-network.md`: the SF intersection 2 example is replaced by the transitions your rules
    produce (road 114 was a mistake).

38. **The re-attach variant's first piece** (`ai/road-network.md`, "Point at a route distance",
    second variant: "its first piece is the curve from P[1] + 2o·X(1) to P[1]"). Which tangents
    and length does that piece use? *Engine choice:* the table's first piece (L = seglen(1),
    T0 = L·S, T1 = L·Tan(1)) with only the two end points replaced.

    **Answered:** confirmed from the machine code: *L* = seglen(1), T0 = L·S (current side),
    T1 = L·Tan(1), u = *s* / *L*; only P0 = P[1] + o·X(1) + o·X(1) (the offset added twice, in that
    order) and P1 = P[1] (no offset) differ. It is evaluated like the other re-attach pieces:
    coefficients with b summed P1 first, position and derivative, **no** clamping of u (the
    placement variant clamps u ≥ 0, the re-attach one does not). `ai/road-network.md` updated.

## Ambient traffic

39. **Props are drawn at their centre of gravity** (`formats/inst.md`, "Drawing";
    `physics/collision-objects.md`, "Props"; `ai/traffic-control.md` step 2). Every prop mesh is
    modelled around its `CG`: `sp_stop_f` y ±1.61 with CG y 1.614, `sp_lightstreet_l` ±5.29 with
    5.30, the light poles ±3.88 with 3.88. Drawn at the placement they sink by CG.y (half the stop
    sign under the pavement, lamp glows 4 m above the lamp head, traffic-light glow meshes — which
    are drawn with the placement matrix — floating above the housings). *Engine choice:* breakable
    props (`.inst` bit 25, xref props, light poles) are drawn with their stored rotation at the
    prop position placement + R·CG (the full-matrix R of collision-objects.md); glows keep using
    the placement. Please confirm for the props spec.

    **Answered:** confirmed for the bodies, with one correction for glows. Every breakable prop
    (bit 25 in both placement forms, xref props, street and path-set props, traffic lights) stores
    placement position + R·CG as its position when it is created, R being the full matrix it was
    created with, and it is drawn with that stored matrix directly (nothing is subtracted; the meshes
    are modelled around the CG). Its distance and room-limit tests use that position too.
    Glow **sprites** (`GlowOffset`, e.g. the street-lamp glows) are placed with the same stored,
    CG-shifted matrix, not the placement: keeping them at the placement puts the lamp glow about
    5.3 m too low. Only the traffic lights' glow **meshes** (`REDGLOW…`, `WALK…`, …) are drawn at
    the placement (stored matrix minus R·CG), as you have them. Spec: `inst.md` "Drawing" ("Where
    breakable props are drawn").

40. **A state's "start" and its per-frame part on the same frame?** (`ai/traffic-reactions.md`,
    "States": "a start that runs on its first frame … and then its per-frame part"). *Engine
    choice:* on a state's first frame the start runs and then, in the same frame, the per-frame
    part (driving: start on the rail, then one driving update).

    *Answered:* confirmed. In a car's update, if the current state's tick counter is 0 its start
    runs and then, in the same update, its per-frame part. `ai/traffic-reactions.md`, "States", now
    also says when each counter is incremented and reset, and that a state change made during an
    update (or by a collision in the physics steps) takes effect at the next update.

41. **Which cars a light change wakes** (`ai/traffic-control.md`, "The cycle"; traffic-driving.md:
    "react = 0 for this car and every car behind it in its list lane with v < 0.01"). For a wake
    by a light change there is no "this car". *Engine choice:* on every lane of the woken road's
    arriving side, every car with v < 0.01 gets react = 0.

    **Answered:** almost: for each lane of the woken side, in lane order, MM2 starts at the lane
    list's **front** car (furthest along) and walks back through the list to its end; every car met
    with speed < 0.01 gets react = 0, faster cars are skipped but the walk continues. The step from
    a car to the one behind it uses that car's link for its **target** lane, so a car in the middle
    of a lane change leads the walk into its target lane's list (MM2 quirk). The woken side is the
    road's arriving side at the intersection: side B unless the intersection is the road's end 1
    (then side A; a loop road gives side A). The same walk is the "this car and every car behind
    it" of a free approach (control 3), starting at that car. `ai/traffic-control.md`, "The cycle",
    now defines "Waking a road".

42. **Section of a traffic car's obstacle registration** (`ai/traffic-reactions.md`, "Being seen by
    other AI": "on its road at the section under it … the next road's side when it is on the next
    road"; `ai/driver-planner.md` §16). *Engine choice:* the section is "Section index of a car on a
    road" (1 … n − 1) of road-network.md; a car counts as being on the next road while it is in its
    turn piece and its room's component is the next road; the intersection when its room's
    component is an intersection.

    *Answered:* the intersection part is right; the rest differs. The section is `section(p, s′)`
    of road-network.md, "Section of a point on a road" (not "Section index of a car on a road"),
    with p = the car's model origin, clamped to 1 … n − 1; s′ = the car's side if the road found is
    its current (rail) road, else its next side. The road comes from "Component at a position,
    preferring a road", second form, with q = the car's current road and the car's own room memory
    as hint. Being on the next road is not tied to the turn piece: it is whenever that lookup
    returns another road than the current one (in practice the next road once the car's room is
    one of its rooms). A car already registered in an intersection is not moved to another
    intersection's list. Registration happens after every update in every state and at the end of
    the start on the rail, unless the car's room is 0. Lists are push-front, singly linked, ending
    with an empty link. `ai/traffic-reactions.md`, "Being seen by other AI", has the full rule and
    two MM2 slips with engine choices.

43. **The lane-change piece's end point** (`ai/traffic-driving.md`, "Lane changes" step 4: "the
    position and derivative of that piece at (x_LC − D_T[j3 − 1]) / g"). *Engine choice:* the
    piece's moving-car coefficients (road-network.md, "Evaluating a piece (a moving car)") at that
    parameter.

    *Answered:* confirmed. MM2 stores the temporary piece's coefficients with the moving-car rule and
    evaluates position and derivative with the moving-car formulas; the parameter is computed as a
    division, τ = (x_LC′ − D_T[j3 − 1]) / g with x_LC′ = x_LC clamped to 0 … λ_T, not clamped
    itself. `ai/traffic-driving.md`, "Lane changes" step 4, now says so.

## Car-to-car collision and damage

44. **A wrecked player in free roam** (`physics/damage.md`, "What damage drives": "what each game
    mode does with a wrecked player … is the game modes' spec"). Until that spec exists a wrecked
    player keeps zero controls for good. *Engine choice:* our reset key (G) also clears the damage.

    *Answered:* MM2's free-roam mode has its own rule (`physics/damage.md`, "What damage drives"):
    when the player's car is wrecked it becomes not drivable for 3.0 s (brake 1 and neutral every
    frame on top of the zeroed controls; unstuck helper and water effect paused), then the damage is
    cleared and the car is drivable again (neutral or reverse → first gear), without being moved.
    Your reset key clearing the damage is an addition of ours and fine.

45. **The six `cc_*` run files** (`engine/drive-check.md`, "Standard car-to-car runs": "the exact files
    are kept with the reference outputs"). I could not read those, so `checks/cc_*.run` are written
    from the table: headon `police vpcop 0 0 -80 180`; tbone `ai vpbug 33 0 -50 90`; rearend
    `traffic va_sedans_s 0 0 -40 0`; sideswipe start −2.4 0 0 0, `traffic va_taxi_f 0 0 40 0
    speed=20`; bus `traffic va_compact_s -32 0 -25 270 speed=8`; chain `traffic va_sedans_s 0 0 -30 0`
    and `traffic va_compact_s 0 0 -38 0` (the table gives no positions for the chain). Please drop
    the reference files into `checks/` if they differ.

46. **Computer cars in check runs: CarFrictionHandling and police catch-up** (`physics/car-collision.md`
    5.4 and 5.7). The run's timeline is the "driver". *Engine choice:* the touched-player mark sets
    CarFrictionHandling 2.0/1.0 every frame as 5.4 says; the catch-up (×1.03 momentum) is not applied,
    since "when the police logic is in that mode is the police spec's" and check runs have no police
    logic.

    *Answered:* both choices are right and now written in `engine/drive-check.md` ("Extra
    vehicles"): the timeline stands in for the driver in its ordinary driving state, so the
    CarFrictionHandling rule (2.0 after touching the player, from the previous frame's mark, else
    1.0, every frame) applies; the catch-up does not (it needs a pursuit mode). The reference
    simulator lacked the CarFrictionHandling rule; with it, `cc_headon_vpbug` and
    `cc_tbone_vpmustang99` are bit-identical to yours.

47. **Box pairs with zero offsets** (`physics/car-collision-boxes.md` section 8). Not implemented as a
    separate copy: such boxes go through the offset case (different sum groupings only). No shipped
    data reaches it (traffic `CG` and `.bnd` box centres are never zero).

    *Answered:* MM2 really has the separate copy (`physics/car-collision-boxes.md` 8); only sum
    groupings differ, so a box with an offset of exactly (0, 0, 0) on both sides would differ in the
    last bits. No shipped data reaches it (as you say); your shortcut is acceptable until a mod
    needs it, but keep section 8 as the reference.

48. **Push source of untouched props** (`physics/car-collision.md` 3.3: "landmarks, spanning objects,
    untouched props and traffic cars without a body — share one" identity). Our props record pushes
    under their own identity (as `collision-object-contacts.md` was implemented, and the 34 check runs
    match the reference that way). Which is right? *Engine choice:* unchanged (own identity per prop).

    *Answered:* your choice is right and `physics/car-collision.md` 3.3 was wrong. An untouched
    prop is handed a body of its own before the response (`collision-object-contacts.md` 5.10), so
    its pushes are booked under that body's identity. The same holds for a traffic car without a
    body: it gets its traffic body before the response, so the push the other car receives in that
    test is booked under the **traffic body's** identity, not the shared fixed-object one (3.3
    rewritten). Only landmarks and spanning objects use the shared identity. **Engine change
    needed:** when a gathered traffic car becomes physical in a test, both sides' push-source
    records of that test's impacts use the new body's identity. This changes `cc_bus_vpbus` and
    `cc_chain_vpbug` from the first contact on (new reference CSVs).

49. **Where gathered traffic cars sit in a mover's gathering order** (`physics/car-collision.md` 2.3:
    "listed in the room of its centre"). *Engine choice:* after the static objects of the gathered
    rooms, in pool order (flat world: run-file order), within the limit of 32.

    *Answered:* from the machine code, every room list runs newest first in two parts
    (`physics/collision-objects.md`, new section "Room lists"): non-static objects (props, light
    poles, traffic cars without a body, …) go to the very front whenever they are filed into a room
    (creation, every room change, and for untouched props and poles again at every race reset, in
    creation order), static objects (landmarks, spanning entries) form the back part, also newest
    first. So a traffic car sits in front of the room's props and statics (and of moving objects
    that entered earlier); the flat world keeps the check-run convention (run-file order).
    collision-objects.md's old "(objects in load order)" was wrong; changing it changes none of the
    8 city runs (checked), but the engine should follow the new order (gathering and object
    probes).

50. **Parked cops on and off the network** (`ai/police.md`, "Parking": "13 of SF's 19 and 13 of
    London's 20 Cruise spots are in block rooms"). With `driver.md`'s basic look-up (first road or
    intersection entry of the room; else a shortcut whose on-road value is < 3) my engine gets London
    15 RECOVER / 5 DRIVE and SF 11 RECOVER / 8 DRIVE. London: 12 spot rooms have no component at all,
    3 more (cops 10, 11, 16) list only shortcuts the spot is off; SF: cops 2, 5, 11, 17 sit in rooms
    with shortcuts only (off them), cop 8 is on shortcut 380. Is "block rooms" a different count (PSDL
    room type), or should the look-up differ? The cops behave the same either way (they hold the
    brake), so nothing is blocked.

    **Answered:** your counts are right; the spec's "13 / 13" was wrong (it counted a different
    look-up, the route search's, against block rooms). The mode after the parking orders comes from
    the driver's basic look-up of the body position (spot + CoG) with no room hint, exactly as you do
    it: London 5 DRIVE / 15 RECOVER, SF 8 DRIVE / 11 RECOVER (the professional list's extra SF spot is
    on road 0: 9 / 11). Spec: `ai/police.md` "Parking (reset)".

51. **Traffic obstacle section numbers** (`ai/traffic-reactions.md`, "Being seen by other AI": "at
    the section under it"; `ai/driver-planner.md` §16 reads side A's list of raw section q at entry
    N − q and §18.5 converts a car's section with N − sx). *Engine choice:* a side B car is filed at
    its raw section (the "section index of a car on a road" query), a side A car at N − raw; a car in
    an intersection counts as section N of the road it is leaving (so §18.5's sx = N case applies).

    *Answered:* two different numbers are involved. (1) **Filing** (the lists §16 reads): side B at
    `section(p, +1)` and side A at `section(p, −1)` = n − raw, both clamped to 1 … n − 1 (Q42); a
    car whose room component is an intersection is in that intersection's list, on no road section
    (§16 reads it at q = 0). (2) **§18.5's sx** is not the filing section: it is the car's piece
    index k on its current (rail) road in its own driving order — 1 … n − 1 on the lane, n while it
    is in its turn piece — with the road being the car's rail road and the direction its side. So
    "sx = N" means "in the turn piece", not "registered in an intersection". `ai/traffic-reactions.md`,
    "Being seen by other AI", now defines both (registration and "route place").

52. **EXPAND step 3 versus step 4** (`ai/driver-planner.md` §7). The route corridor step (§14) can set
    FINAL = 1. Is step 4 then also run for the same record (two tests in sequence), or are steps 3
    and 4 the two branches of one test? *Engine choice:* one test (if/else) on FINAL as it was at the
    start of EXPAND.

    **Answered:** one test. MM2 reads FINAL once at the start of EXPAND and runs either the FINAL = 0
    branch or the FINAL = 1 branch; a FINAL set by the route corridor step applies from the next
    record. Your engine choice is MM2's. Spec: `ai/driver-planner.md` §7.

53. **Police livery without cable cars** (`ai/police.md`, "The cop's car": paint 0 when the city has
    cable cars running). The engine has no cable cars yet. *Engine choice:* a city "has cable cars"
    when any road side has a cable-car line (SF: paint 0 "Police Special", London: paint 1).

    **Answered:** MM2 uses the number of cable cars it created: one per intersection at which exactly
    one of its roads has a cable-car line on the side leaving that intersection (side B where the
    intersection is the road's end 1, side A where it is end 0); cable cars are switched off only
    during a multiplayer set-up. Paint 0 if at least one such intersection exists, else paint 1. For
    the shipped cities this equals your choice (SF 0, London 1); it differs only for a mod whose cable
    lines are closed loops (no end intersection: paint 1). Spec: `ai/police.md` "The cop's car".

54. **Smaller police gaps** (*engine choices*, for confirmation): (a) a world reset also empties the
    police roster (the spec resets every cop but says nothing of the roster); our player's own
    world-limit reset (fell / sank) does not reset the cops or traffic. (b) Props knocked loose
    disappear in this engine, so the planner skips broken props (MM2 would steer round the moving
    prop). (c) `driver-planner.md` §14 step 3 reads *a_s* for s = 2, which does not exist; I use 0.
    (d) The perp's extents (police.md, "Blocking": back = max.z, left = −min.x) for a player car are
    taken from its collision bound's box. (e) Flashing headlights start from the car's forward
    direction whenever the light bar is off (not "the last time headlights were drawn normally").
    (f) Lens flare squares use the spec's 1.33 aspect, so they stretch on wider screens.

    **Answered:** (a) Right, both parts: MM2's world reset empties the roster before it resets the
    cops (and sets the generator to 1); the player's own fall/sink respawn is not a world reset and
    leaves cops and traffic alone. Added to `ai/police.md` "Parking (reset)". (b) Fine as an engine
    choice (MM2 keeps a knocked prop in its road list and tests it at its current position).
    (c) Not 0: MM2 reads the values stored after the slot-1 entries, so *a*₂ is junction 0's turn sign
    τ₀ as a number (1.0, −1.0 or 0.0) and *J*₂ is junction 0's arc centre *G*₀; this happens in §14
    step 3 (when the slot-3 road, the driver's last *S*₀, exists) and in the end-of-road tests of
    rules B1/B2. Spec: `ai/driver-planner.md` §14 "Junction values of slot 2". (d) Right: the player
    car's extents come from its collision bound's box (min/max of the bound's points). (e) Fine as an
    engine choice (only the phase of the flashing differs from MM2's "wherever they last pointed").
    (f) That is what MM2 does too: its flare screen space is a fixed −1.33 … 1.33 × −1 … 1, so on wider
    screens MM2's flares stretch the same way.

55. **Which props the AI obstacle walk registers** (`ai/road-network.md`, "Obstacles": "every
    breakable prop (`.inst` bit 25, xref and street props)"; `formats/street-props.md`, "When they
    exist": the walk meets street, `.inst`, xref and **path-set** props and the traffic-light poles).
    *Engine choice:* every breakable object filed in the room by the time the network loads, poles
    and path-set props included, walked in room-list order (newest first). Could road-network.md name
    the path-set props and poles too, so the two pages agree?

56. **Loose bodies, small gaps** (`physics/loose-bodies.md`; *engine choices*, for confirmation):
    (a) 5.1 "the pool drops, without letting go": I free the pool element but leave the object
    marked as having a body, so it is neither simulated nor gathered (drawn where it lies) until its
    slot is handed out again or a reset. Is that MM2's state, or does the object become collidable?
    (b) 6.1 with the rest detector's state not 1 (frozen for the rest of the frame), I skip the
    integration, as for a physical traffic car; integrating with all accumulators cleared would only
    set `lastPush` to 0. Same for loose bodies?
    (c) 6.2 says a capsule or sphere copy is tested against the city as the box of its bounding
    box; I take that box as (±YRadius, ±(Size.y/2 + YRadius), ±YRadius) (sphere ±YRadius) and use
    it for the pair and gathered-object tests too (seven shipped props: `sp_parkmtr_f`,
    `sp_bollard_stone_l`, two palm trees, two freeway supports, the cenotaph).
    (d) Damage, "One body part per strong impact": the contact point is turned into car space as
    d = P − origin, ((d.x·X.x + d.y·X.y) + d.z·X.z) for x, likewise with Y and Z; the order is not
    written. Only the choice of the nearest part can depend on it.
    (e) A part whose `<car>_<PART>.mtx` is missing (hubs, which have no .mtx in the shipped data)
    gets p = (0, 0, 0); a part without prop data is thrown with the default prop data.
    (f) In the scene's gathering a room's lane cars (newest filed first) still come before the
    room's objects, so a moved prop that entered the room after a lane car is gathered after it,
    not by filing time. No check run has both.

    **Answered** (6663616, done in the engine): (a) yes, MM2's state; the dangling link is not
    reproduced (`loose-bodies.md` 5.1). (b) A frozen body still runs integration steps 1 and 3
    (lastPush = pushSum, pushSum = 0), for loose bodies and physical traffic cars alike (6.1, and
    `car-collision.md` 6.3). (c) The box is for the city only; a capsule copy against a polygon side
    uses the capsule test of `collision-object-contacts.md` 5 with the capsule's previous matrix H₀
    (6.3, "Capsules in these tests"). (d) Inverse translation first, then the rows
    (`damage.md`). (e) Confirmed as engine choices (`damage.md`). (f) One list per room: lane cars,
    moved props and pedestrians in filing order (5.4); the engine stamps both from one clock.

57. **Level objects and cable cars, small gaps** (`game/level-objects.md`, `ai/cable-cars.md`;
    *engine choices*, for confirmation): (a) Only Cruise exists in the engine, so every kind reads
    its Cruise file (`<city>_<word>.pathset`); the race-variant lookup is in place for later.
    (b) Drawbridge leaves are not registered as intersection obstacles ("The AI and the bridges"):
    our obstacle walk takes the city's breakable props only, and the leaves are not among them.
    (c) The cable cars' draws (lane offsets, accelerations) continue the level objects' generator
    after their last draw; MM2's pedestrians draw in between, which the engine does not have yet.
    (d) With the cable cars, the drive frame now runs traffic, police, then the cable cars and the
    light sets (traffic.md "Frame order" steps 5-6); before, the light sets advanced ahead of the
    police. (e) The viewer (no drive running) moves the level objects with its own frame time.

    **Answered** (6663616, done in the engine): (b) the obstacle walks take every prop object,
    leaves, train cars and ferries included (`level-objects.md`, "AI obstacles"); the engine's
    counts match (SF 2 leaves, 2 registrations; London 17 train cars, 23 registrations). Doing this
    found an engine bug: the second and third car of each train were filed under the wrong object
    numbers (one car never placed, one ferry moved as a train car); fixed. (c) Discard the traffic
    and pedestrian pools' creation draws first (`cable-cars.md` step 3). (e) Confirmed (*Engine*).

## Damage visuals and effects

58. **Small gaps in `formats/damage-visuals.md`** (*engine choices*, for confirmation):
    (a) **The slot's normal.** The impact normal of `collision.md` points out of side B into side A;
    the car is A against objects and props but B against the city (and either side in car pairs).
    The slot stores the normal turned to point away from the other side (into this car's side), so
    sparks fly off the wall rather than into it. Which way does MM2's stored normal point?
    (b) **Smoke point between the offsets** (`DoublePivot` 0): q.c = ((o2.c − o1.c)·f) + o1.c with f
    the effects draw. Is that MM2's operand order? And with `DoublePivot` 1, the alternation (and
    the draw for `DoublePivot` 0) happens only in updates where the smoke level is 1–4 (as
    `engine/effects.md` step 1 reads), not in every damage update.
    (c) **Brush choice**: "lowest bit 0 → large brush, 1 → small brush".
    (d) **A pending mark request at a clear damage** is dropped (a reset car shows no marks until
    its next counted impact). MM2 may keep the request and carry it out on the clean car at the
    next damage update.
    (e) **Fenders and the wobble.** `car-model.md` "Fenders" builds `FNDR0`'s frame from the drawn
    `WHL0`'s X axis, so a wobbling front wheel would tilt its fender too. The engine's fender code
    still uses the fender pivot turned by the steering (not the spec's frame), so fenders do not
    wobble yet. Confirm that MM2's fenders follow the wobble.

    **Answered:** (a) MM2 does not turn it: the slot keeps the impact record's normal exactly as
    filed (out of side B into side A), whichever side the car is. Against the city (car = B) it
    points from the car into the wall or ground, so MM2's sparks fly into the surface; against
    objects and props (car = A) it points away from them; in a car pair both cars store the same
    vector. Drop the flip. Spec: `formats/damage-visuals.md` "Per-impact effects",
    `engine/effects.md` "Sparks" (Trigger). (b) Yes to both: q.c = ((o2.c − o1.c)·f) + o1.c, and
    the draw (`DoublePivot` 0) or the alternation (`DoublePivot` 1) happens only in damage updates
    that smoke (level 1–4 **and** the car has `fxpt8`). The alternation starts with `SmokeOffset`
    when the car is set up and a clear damage does not restart it. Spec: `formats/damage-visuals.md`
    "What the damage amount drives". (c) Right: lowest bit 0 → large brush, 1 → small. (d) MM2
    keeps it: no clear damage (reset, cruise or opponent recovery, network reset) drops a pending
    request, only the car's set-up does; the round is carried out at the next damage update on the
    refilled copies, at the stored car-space point. Spec: `formats/damage-visuals.md` "When marks
    are made". (e) Confirmed: the fender frame's X is the drawn wheel matrix's X after the spin and
    the wobble turn (origin: that matrix's position), so a wobbling front wheel tilts its fender
    (the frame is then not orthonormal, Y stays the car's). Line added to
    `formats/damage-visuals.md` (wobbling wheels); `formats/car-model.md` "Fenders" unchanged.

59. **Small gaps in `engine/effects.md` and `formats/particles.md`** (*engine choices*, for
    confirmation):
    (a) **Spark tails.** The step computes **t** from the old **p** and then moves **p** by T·**v**, so
    the drawn line is about 1.036·T·|**v**| long (about 25 cm at 30 steps a second and 7 m/s), not
    "under a centimetre" as the note says. The engine follows the order as written (sparks show as
    short streaks). Which is MM2's?
    (b) **How long a knocked prop is active** (its particles live only while it is): the engine's
    broken props vanish at once, so as a *placeholder* a prop counts as active for 4 s after the hit
    (its emitter spews and updates per frame, then goes). The prop spec will replace this.
    *(Settled since by `physics/loose-bodies.md`: a prop is active while its loose body is a mover;
    the merged engine activates the emitter when a broken prop's copy or piece, or a moved prop hit
    again, becomes a mover, and removes it when the body is let go.)*
    (c) **Alpha test reference** of the effects pass and skid marks: "alpha test on" without a value;
    the engine drops texels with alpha 0 only.
    (d) **`sp_tree1_s_BREAK06`** names its block `asBirthRule`; read by key it is not a `BirthRule`
    (the survey's 991 of 992), so that tree emits nothing. If MM2 reads prop data by position
    (`physics/collision-objects.md`), it would read that block anyway: does it?
    (e) **Chip textures** use the material's texture as named (a `_dmg` slot's chip shows the damage
    image), not the slot's working copy.
    (f) **Missing `fxpt8`**: no smoke particles, but the smoke point draw of `damage-visuals.md` still
    happens each smoking update (so the effects sequence does not depend on the texture).
    (g) **Rain changes mid-drive**: the splash-over-smoke copy is made when the drive starts; a
    weather change during a drive switches the rain on or off but leaves the smoke rule as it was.

    **Answered:** (a) The order as written is MM2's: the tail is set from the old head, then the
    head moves by T·**v** with the new **v**, so sparks are streaks about 1.036·T·|**v**| long (up to
    about 25 cm). The "under a centimetre" note was wrong; fixed in `engine/effects.md` "Sparks".
    (b) No fixed time, and the note above needs one correction. The emitter belongs to the body
    (the pool element), not to the object, and its activation (reset, texture, `InitialBlast`
    burst) happens **every time an object gets a body** (`physics/loose-bodies.md` 2.1): also an
    untouched prop at the start of any test that gives impacts, even one that does not break (that
    burst is never shown, as the body is let go at the end of the test, but its births take their
    effects-generator draws, before that test's sparks and chips); each piece of a broken prop;
    a moved prop lying still that is hit again; a thrown part. A prop that moves as one piece keeps
    the body it got at the start of the test, so its burst is that one: there is no second
    activation when the copy becomes a mover. A prop broken into pieces never shows its own burst
    (its body is let go); the pieces activate with their own data. Each frame, in the
    `loose-bodies.md` 5.2 step, a loose mover with switch U whose body that step keeps is updated
    with F and registered for drawing; the particles go as soon as the body is let go or dropped.
    Spec: `engine/effects.md` "Props" and "Order of work and of the random numbers". (c) Reference
    0 with test "greater" (only alpha 0 is dropped), in the effects pass and in the decal pass
    (skid marks): the engine's choice is MM2's. Spec: `engine/effects.md` "Drawing order",
    `formats/particles.md` "Drawing". (d) By position: the block's name and every key are skipped
    unchecked, so MM2 would read `sp_tree1_s_BREAK06`'s block like the others (`TexNumber` 5: 25
    particles, then 1.24 a second). But no shipped prop loads that file: `sp_tree1_s` has
    `NumParts` 5, so only its pieces 1–5 are used. All 992 files have the 24 rule lines in the
    reader's order. Spec: `formats/particles.md` "Prop data blocks" (new); counts in
    `engine/effects.md` "Checked against" corrected (6 × `TexNumber` 5, BirthFlags 965 × 0).
    (e) Right: chips use the paint set's own textures, never the working copies. Spec:
    `formats/damage-visuals.md`, `engine/effects.md` "Body chips". (f) Not MM2's: with `fxpt8`
    missing the whole smoking step is skipped, point draw and alternation included, so the effects
    sequence does depend on the texture (only for mods; `fxpt8` ships). Spec: `engine/effects.md`
    "Engine smoke and exhaust", `formats/damage-visuals.md`. (g) MM2 makes the copy once per game,
    at the end of the game's set-up (after the cars, whose set-up reads the eight rules), and the
    rules are read again from their files when the next game's cars are set up, so the copy lasts
    exactly one game. MM2's weather never changes during a game, so what a mid-drive change does
    is the engine's own choice; yours is fine. Spec: `engine/effects.md` "Wheel particles".

## Pedestrians

60. **The shared animation clock with an even number of updates** (`formats/ped-anim.md`,
    "Playback": "successive pedestrians alternately get steps of 0 and 1: each still averages 30
    frames per second"). With *F* = 1/60 each update adds 0.5, so when the number of pedestrians
    updated per frame is **even** and the update order is stable, the same pedestrians get step 0
    in every frame: their sequences never advance (they glide on with a frozen pose) while the
    others advance every frame. The "averages 30" holds only for an odd count (e.g. the Cruise 25),
    or when *F* or the order changes. Does MM2 really do this (e.g. 50 pedestrians at 60 fps), or is
    there something else that breaks the pattern? *Engine:* implemented exactly as written.

61. **Sidewalk heading H with a line number the side does not have** (`ai/pedestrian-walking.md`,
    "Sidewalk heading"; placement passes λ = the pedestrian's side's lane count). On a one-way road
    (side A with 0 lanes, side B with *L* ≥ 1, very common in London) a pedestrian placed on side B
    with *d* = −1 reads side A's line *L*, which does not exist (side A has only its sidewalk,
    line 0); MM2 reads past the array. *Engine choice:* a λ past the side's lines reads its last
    line (the sidewalk). What does MM2 actually read there?

62. **Zero-length pieces and other guards** (*engine choices*, for confirmation): (a) a crossing
    piece of length 0 between two different roads (would give R = ∞ and a NaN target) uses ℓ = 1, as
    the same-road case does; (b) locate with *d* = −1 and *k* = 0 (only after `x = −a` with a exactly
    0, which does not trigger the road end) reads segment 1 instead of W[−1]; (c) a side without a
    sidewalk line is not a pedestrian side whatever its flags (none ships); (d) a pedestrian whose
    model does not load is created (its draws are made) but never placed, and a city/race with no
    pedestrian model list gets no pedestrians (MM2 would index an empty list); (e) knocked props are
    skipped by the "obstacle ahead" test, as the police planner does (Q54 b): in this engine a
    knocked prop disappears, so stepping round where it stood would look odd.

63. **"Segment 1 or n − 1 of the current road on either side"** (`ai/pedestrian-walking.md`, step 2,
    the accident test while waiting). The traffic registration lists of side A are numbered against
    the road (`traffic-reactions.md`, "Being seen by other AI": side A at n − raw). *Engine choice:*
    the list with that index on both sides (side B's list *s*, side A's list *s*, *s* = 1 for
    *d* = +1, *n* − 1 for *d* = −1), taking the sentence literally. Is that what MM2 reads, or side
    A's list at *n* − *s*?

64. **The intersection ahead on a crossing piece** (`ai/pedestrian-walking.md`, "Obstacle ahead":
    "if *k* is 0 or *n*: the props of the intersection ahead (the road's end 0 intersection if
    *d* = +1, end 1's otherwise)"). A pedestrian on the crossing piece with *d* = +1 is entering the
    road at its section-0 end (end 1's intersection), so "end 0" is the far end of the new road.
    *Engine:* as written (end 0 for *d* = +1). Please confirm it is not the intersection being
    crossed.

65. **`.mod` adjunct counts** (`formats/ped-anim.md`, "Shipped": man (rain) "128, 252, 260, 17 in
    20 packets"). 252 is the header's `adjuncts:`; the packets of `pedmodel_manw` hold 279 adjuncts
    (a material split into several packets repeats shared adjuncts). The other three models' packet
    or list totals equal their headers. Worth a word in the page so a reader does not compare the
    drawn count with 252.

66. **Translucent materials of a `.mod`** (`formats/ped-anim.md`: MM2 moves materials whose diffuse
    alpha is below 1 or whose texture has alpha toward the end of the list). Not implemented (no
    shipped model is affected; the renderer blends per batch anyway). With a paint variant file the
    move would also change which variant material a geometry group is drawn with: is the variant's
    material list reordered in the same way, or only the model's?

67. **Smaller pedestrian choices** (*engine*, for confirmation): (a) room tracking for the pedestrian
    adjust uses the player car's model origin and its own room memory, like the traffic adjust;
    (b) the far strips are drawn without fog (unlit, untextured; MM2's fog state for them is not
    stated); (c) there is no `pedpool` command-line option in the browser (the aimap value, default
    100, is used); (d) the side panel's density slider re-creates the pool and resets the
    pedestrians at once, like the traffic slider. Also an observation, as specified: a pedestrian
    backed against a wall stays there while the car stands still (v̂ = 0 is not "moving away").

102. **Race pedestrian densities in "Checked against"** (`ai/pedestrians.md`: "Race densities: SF
    Blitz 0.1–0.5, Checkpoint 0–0.1; every Circuit and Crash Course 0; London Checkpoint 0 except
    two rows at 0.1"). Reading `race/sf/mmracedata.csv` with `races.md`'s rules gives SF Checkpoint
    Peds up to **0.3** (amateur/professional, races numbered from 0: races 4 and 5 at 0.2 / 0.2 and 0.3 / 0.3, race 10
    at 0.3 / 0.3); London Blitz is 0.1–0.2. The Circuit and Crash Course rows and London's
    Checkpoint rows agree. Could the sentence be checked? (The engine takes the column as read.)
    Also: the results' "next race" has no engine path yet; `DriveView::startRace` takes a
    `nextRace` flag so Checkpoint and Blitz keep the session's density once it is wired.

## HUD, menus, profiles (milestones 9 and 10)

68. **Default bindings the spec does not list** (`engine/controls.md`, `formats/profiles.md`). Our
    program-default binding table (used for a driver created in a browser that never imported an
    MM2 profile) equals the shipped `player0.cfg` everywhere controls.md specifies it (keyboard
    keys, joystick and gamepad axes and buttons). The rest differs: the keyboard device's steering
    axis (action 5; shipped kind 1, device 3, component 9), the look-around axis (action 31; shipped
    2/4/16 on all five devices), the mouse device's actions 5, 8, 9 (shipped 2/2/10, 1/2/1, 1/2/2)
    and the wheel device's actions 5, 8, 9, 10, 13 (shipped like the joystick's, but no camera or
    map buttons). Could controls.md give the full default table of all five devices? *Engine
    choice:* unspecified entries are empty (kind 0) or the keyboard key; `cctool uitest` reports them.

69. **Text numbers whose order is not given** (`formats/mmlang.md`): which of 5 – 12 is which mode
    (we use 6 Checkpoint, 5 Circuit, 7 Blitz, 12 Crash Course from `results.md`, and 80 Cruise);
    which of 357 – 364 and 625 – 632 are the time-of-day and weather choices; the headings 344 – 355;
    the HUD panel labels 251 – 270 (place, checkpoint, lap); the binding-name pieces 271 – 274 and
    310 – 324; the mode list 585 – 589; and whether the cloud-shadow list is 660 none, 661 low,
    576 high. *Engine choice:* our own English labels wherever the number is not certain; 660 /
    661 / 576 as none / low / high.

70. **Rewards file, "at most 32 lines are used"** (`formats/profiles.md`): does the limit count the
    accepted rows only, or every line read (comments, unknown words and unknown cars included)?
    *Engine choice:* accepted rows.

71. **Quoted keys in `.mmHudMap`** (`game/hudmap.md`: only a quoted `"Approach Rate"` would be read).
    The tune-file syntax (`physics/vehicle-tune.md`) does not describe quotes. *Engine choice:* a
    token that starts with `"` runs to the next `"` and is one token without the quotes.

72. **HUD map camera height** (`game/hudmap.md`): the key table calls `ZoomInDist` / `ZoomOutDist`
    the "camera height above the car", the camera step puts the camera at (*P*.x, *h*, *P*.z).
    *Engine choice:* the step (absolute height *h*).

73. **Direction arrow axis** (`game/hud.md`): `hudarrow01` spans z −1.18 … 0.74. *Engine choice:*
    the model's −Z is the arrow's forward axis (tip), and "tilted 20° down" turns the tip down
    (−20° about the camera's X). Please confirm both.

74. **Graphics "view distance"** (`formats/profiles.md`: "view distance (far clip) in metres").
    What does MM2 do with it: replace the camera files' `CameraFar`, clamp it, or set only the
    city's draw distance and fog? *Engine choice:* games started from the front end use it as the
    draw distance (far plane, fog clamped to it); the viewer keeps its own draw-distance setting.

75. **Water text moment** (`game/hud.md`, "In the water (any mode)"): *engine choice:* posted on the
    first frame the player's water timer is not 0 (the moment the water camera also starts,
    `game/races.md`), once per water episode.

76. **Smaller HUD and front-end choices** (*engine choices*, for confirmation): (a) map keys Tab,
    M (MM2's Q), N (MM2's E), O (MM2's F), plus I, F2, F4, because the driving keys took MM2's
    letters; (b) new drivers start with the gauges overlay off (view byte 5 = 0) and the map off, as
    MM2; (c) leaving the half-screen map brings the dash view back whenever it was on when the map
    took over; (d) importing an MM2 players folder replaces the drivers kept in the browser (no
    merge); (e) the full-screen-map pause quirk is kept (Tab out leaves the game paused until M is
    pressed twice); (f) when every driver's `.sav` fails during a session, `players.dir` is written
    with no drivers (the next start makes the default driver); (g) MM2's menu art (`jpg/*`,
    `menu.csv`, `widget.csv`) and the HUD textures are not used: the page draws its own design.

77. **Order of the control action names** (`formats/mmlang.md`: "276 – 309 the 34 control action
    names of the binding screen (same order as the binding table)"). Shown next to the shipped
    `player0.cfg` binding table, text 276 + *a* does not name action *a*: the entry bound to Tab
    (action 0, map mode) gets text 276, the H entry (action 4) text 280, and so on, and the texts do
    not read as those actions. Is there a different order (or a mapping table in the binding
    screen)? *Engine choice:* the customise-controls table uses our own action names until this is
    settled.

## Music (milestone "Music")

78. **Volume example** (`audio/music.md`, "Volume and balance"). The example says m = 0.1 gives
    −56.54 dB, but the formula gives log(20)/log(200) = 0.5654, (0.5654 − 1) × 100 = −43.46 dB
    (−56.54 is 100 × 0.5654 with the sign flipped). *Engine:* the formula (−43.45 dB after the
    truncation to hundredths).

    **Answered** (d6bde33): the example was wrong; music.md now says 0.1 is −43.45 dB.

79. **Reference counts** (`formats/directmusic.md`, "References", "Checked against"). The files
    hold 1,865 `LIST DMRF` (62 style, 1,803 DLS collection), not 3,216 (62 + 3,154); all 1,865
    resolve by file name with matching GUIDs. Also, 23 of the 1,818 band entries with flags 0xE3 have
    **no** collection reference (the page says "always with a collection reference"); they download
    nothing and send bank, program, CC 7 and CC 10, so they play whatever instrument of that patch
    another band downloaded. *Engine:* as just described.

    **Answered** (d6bde33): 1,865 references (62 style, 1,803 collection); the 23 0xE3 entries without a reference are in the ambience and `Groover*` bands and name patches no collection holds.

80. **The queue time** (`formats/directmusic.md`, "Starting a primary segment": *now* = the present
    plus the output latency). How large is that latency in MM2 (it moves every boundary decision)?
    *Engine:* *now* is the next sample to be rendered (the browser's own output buffer comes after).

    **Answered** (d6bde33, directmusic.md "Playback: time", "Clocks"): the render point is the next sample to render; *now* is the render point or, if later, the time of the latest MIDI event already handed to the synthesiser (events are handed over once less than 75 ms after the render point); the present is 80 ms before the render point.

81. **How far ahead tracks are played** (prepare time). It decides when the random draws happen
    and whether notes with negative time offsets (323 notes; 9 of them about −31,700 ticks) are on
    time. *Engine:* 1000 ms ahead; a note that would start before its own segment starts is moved
    to the segment's start.

    **Answered** (d6bde33, "Composing ahead"): tracks are composed up to the present + 1000 ms; each event takes effect at its own time, or at the render point if that has passed (no moving to the segment's start).

82. **Order the slots are loaded** (`audio/music.md`, "Setting up a game's music"). With "latest
    downloaded first" the order decides which collection wins a patch that two collections share.
    *Engine:* the table's role order (start, chase, return, pause, idle, idle-cop, results), then the
    motif style, then the band file.

    **Answered** (d6bde33): race slots in row order (0 to 6), roam slots in column order (start, return, idle, chase, idle-cop, pause), then the motif style, then the band; an already downloaded instrument of a collection keeps its place.

83. **Small synthesiser choices** (`formats/directmusic.md`, "Synthesiser"; for confirmation):
    (a) the articulation pan (0.1 % units, ±500) becomes −64…64 as value × 64 / 500;
    (b) "the voice ends at the first block after its volume falls below −80 dB" — I test the volume
    envelope's level alone (level × 96/10 − 9600 < −8000), not the voice's whole gain;
    (c) the velocity-to-attack scaling is applied before the fs/1000 minimum;
    (d) blocks are 32 samples with gains ramped across each (the page allows per-sample evaluation).

    **Answered** (d6bde33): (a) trunc((scale >> 12) / 125); (b) the volume envelope alone, as built; (c) the fs/1000 minimum first, then the velocity scaling (a scaled attack can end below the minimum).

84. **Transition to an empty slot** (a missing segment file). *Engine:* current/previous change as
    for any transition, but the music is left as it is (no ending to silence).

    **Answered** (d6bde33, music.md "Kinds of switch"): a transition to an empty slot is a transition to nothing with that transition's command and boundary; a cut with an empty current slot does nothing; a stop with an ending when nothing plays stops every segment, the motif included.

85. **"Unless its segment is already playing"** (`audio/music.md`, "Start slot s"). Does a segment
    queued behind a transition count as playing? *Engine:* yes.

    **Answered** (d6bde33): no; a segment is playing only once the present has reached its start, and until the present reaches its end or stop time.

86. **The music option changed during a game** (MM2's in-game audio options). The page does not
    say what happens to a game's music player when music or ambient is switched on in the middle of
    a game. *Engine:* the game's music set-up runs again (new player, new row, 1.25 s start).

    **Answered** (d6bde33): the in-game audio options have only the three sliders; the music option never changes during a game.

87. **Renders clip**: with MM2's DLS samples normalised to full scale, bands at CC 7 = 101…127 and
    the mix rules as written (voice gains clamped to unity after +6 dB, summed at half scale with
    16-bit saturation, then doubled), `SunRoofStart` peaks at full scale with about 3 % of its
    samples clipped (`EnemyStart` 0.8 %). Is that what MM2 sounds like, or is a factor missing
    (e.g. the +600 or the doubling)? *Engine:* as written.

    **Answered** (d6bde33): the saturation is MM2's (every add clamps); that is how MM2 sounds.

## Sound (milestone "Sound")

88. **The end of a wind-down** (`audio/car-sounds.md`, "Each update while the siren sounds", 3D
    steps 1-2). When the wind-down step stops the current sample and sets the mode off, step 2 of
    the same update ("play the current sample looped if it is not playing") would start it again,
    and with the mode off nothing would ever stop it. Does MM2 restart it there? *Engine:* step 2
    only plays while the mode is not off.
89. **Echo copy at switch-on** (`audio/sound-system.md`, "Echo copies", step 1). The echo voice is
    "placed at the instance's current play position" but only a later play repeat starts it. So a
    loop that is already running when the car enters a tunnel (the engine samples, a skid) has no
    echo until it is started again? *Engine:* as written (not started until a play repeat).
90. **Echo queue full** ("delay × 180 entries"). When a kind's queue is full, is the new repeat
    dropped or does it overwrite the oldest? *Engine:* dropped.
91. **Two measurements in a slot's first frame** (`audio/sound-system.md`, "Distance": "measured
    once per frame … by the step that uses them"). A 3D source that asks in step 1 and gets a slot
    is updated by the manager in step 3 of the same frame. *Engine:* both steps measure (the
    second ΔD covers the time between the two steps, i.e. 0 for a listener that did not move).
92. **The wrecked cop's explosion every frame** (`ai/police.md`, "One frame of police" step 6: "a
    wrecked cop repeats this every frame"). The police code calls "the perp escapes" once (the cop
    is OUT afterwards); the sound layer repeats the explosion and the siren stop every frame for a
    wrecked OUT cop, which gives car-sounds.md's "the explosion plays again each time it ends".
    Should the police code repeat the call itself? *Engine:* the sound layer repeats it.
93. **Speech clips from a lazily read archive** (`audio/speech.md`, "Clips are streamed"). In the
    browser a clip's file may first have to be fetched. *Engine:* the line starts when its file has
    arrived (at most 3 s later, else it is skipped) and counts as speaking meanwhile, so the queue
    waits for it.
94. **Is playing** (`audio/sound-system.md`, "Samples"). *Engine:* the mixer's own play position
    (file frames at the playback rate, advanced by the frame time at the start of each frame) decides
    when a once-played sound has ended, not the output's clock, so the rules run the same natively and
    in the browser; the output follows the mixer's commands.

## Trailers

95. **Trailers, small gaps** (`physics/trailer.md`; *engine choices*, for confirmation):
    (a) Section 3 says the game's first previous matrix is the frame in which the trailer was
    created, built from the truck's model frame. The engine creates the trailer with the truck but
    places and attaches it at the first placement (race start, drop point), and that starting frame
    with no push source is the previous matrix of its first sample, as in check runs; a matrix from
    before the placement would give the first city test a sweep from wherever the car was set up.
    Later resets keep the previous matrix and source, as written.
    (b) The trailer's own collision matrix in a step is its body frame after the free-range
    correction of 5.8 (it has no separate model frame like the truck's step-5 frame). Right?
    (c) 5.4 "FreeRoll < |0|": the roll angle is taken as a per-update value that starts at 0, so a
    roll computed in one update (negative `FreeRoll`) never feeds the next update's test.
    (d) Check runs: `tlean` keeps the last joint update's value across a reset (a reset runs no
    joint update; it starts at 0); `tbroken` is 0 again once a reset has re-attached the trailer.
    (e) "Loaded" (2.4 step 2) means both tune files exist, even if one of them fails to parse.
    (f) Drawing: the trailer stays visible in the dash and bumper views (QUESTIONS 33's answer) and
    is drawn in the rear-view mirror with the other cars. Does MM2's mirror show it?
    (g) Keys: our B is MM2's W (wide view); with Ctrl held, B detaches the trailer and does not
    toggle the wide view.
    (h) Computer trucks (section 10) get their frozen trailer in the game (police from the race
    file) and in check runs (`police`/`ai` lines of a towing model), drawn where it stands.

## Race modes and opponents (milestone 4)

96. **Zero headings in the shipped waypoint files** (`game/waypoints.md`, "Checked against": "no
    shipped waypoint has a zero heading"). The engine reads 10 headings of exactly 0.000000:
    London `race2` waypoint 0, and the last waypoint of SF `race11`, `follow`, `exam1_3`, `final`
    (each event list counted twice) and London `blitz0`, `blitz8`. All sit where the fill-in never
    looks (waypoint 0, a non-Circuit list's last waypoint), so "the fill-in never fires" holds; the
    native test checks the 10. Could the page say so? Also: the 0.6 r off-centre drive gives 742
    crossed (325 with gates turned 90°) with the engine's sideways-shifted straight path against the
    page's 722 / 298; the method is not described, so only the 846 centre crossings and the 646
    gates facing within 90° (median |cos| 0.937, both as on the page) are checked.

97. **The player's road for "avoid players"** (`ai/driver-planner.md` §16 step 3: "the road the
    player is currently tracked on (the AI map's player tracking)"). No page specifies that
    tracking. *Engine choice:* the component at the player's body position with its room as the
    hint (`road-network.md`, position look-up): a road or shortcut gives its index, an intersection
    or nothing gives none.

98. **An opponent's "current road"** (`driver-planner.md` §16 step 4 and §18.5 say "its S₀";
    `driver.md`, "What others read", says the window road of the slot recorded in the planner's
    working point 0, and the section on S₀ with which of three roads is S₀). *Engine choice:* the
    obstacle scan (§16 step 4) uses the window road of record 0's slot; window_place (§18.5) uses
    S₀ and the section on S₀ converted with the opponent's own side.

99. **Traffic placement near opponents** (`ai/traffic-population.md`, "Placing" step 4: the route
    point "(placement variant; height 0)" and "squared distance < 2500"). *Engine choice:* the
    point's y is 0 and the distance is 3D, (dz·dz + dy·dy) + dx·dx. With y = 0 an opponent on an
    SF hill (y ≈ 60) never blocks a position; is the height really 0 in the distance, or is it a
    horizontal distance? And in the world reset, are the opponents put back before or after the
    traffic fills the streets? *Engine:* the opponents first, so the fill sees their start points.

100. **Race-mode details the pages leave open** (*engine choices*, for confirmation):
    (a) "Stop the clocks" stops all three clocks (race, opponents', countdown).
    (b) The Checkpoint / Blitz water rule's "lose sound" is `Youlose`.
    (c) Blitz "kept" opponent number: the row's number (0 in every shipped row).
    (d) Crash Course: a go that starts the countdown also resets it to the event's limit (kinds
        other than follow and a minimum-speed event without a limit), so a later event (SF
        `crash7`'s evade) gets its full time; "start and reset … as noted" could also mean the
        countdown runs on from where the first event left it.
    (e) The lesson name at an event's state 0: text 532 + N in London, 545 + N in every other city.
    (f) The gate models' material set: 1 (`CCStand`) in the Crash Course, 0 (`CheckpointStand`)
        in the other modes; the pages do not say which set is drawn.
    (g) The player-disabled override sits after the automatic-reverse check and before the parking
        brake (`controls.md` order); the wrecked override stays where the vehicle builder put it.
    (h) No contact's damage limits "stay for the session (also in later races)": the engine
        creates the player's car anew for every race start, so they last until the next start.
    (i) The opponents' repair clock ("now", `driver.md`) is the page's real time in the viewer and
        the sum of frame lengths in check runs (as `drive-check.md` says).
    (j) Room-waking opponents (`opponents.md` "One frame" step 3) add their room and its neighbours
        after the player's rooms: a room already listed is not added again, its neighbours follow
        the player rule (stop at the first neighbour already listed), 20 rooms at most.
    (k) MM2's S / X keys are N / X in the engine (S drives).

101. **Who draws the race gates and arrow.** The engine draws the waypoint gates as world objects
    (`waypoints.md`, "Drawn as") and leaves the arrow, the HUD map and the menus to the HUD/menu
    milestone; the viewer shows a plain text overlay meanwhile.

## Physics follow-ups

103. **A frozen traffic car's wheels** (`physics/car-collision.md` 6.3). The answer to 56(b) says a
    frozen body (rest detector state ≠ 1) skips only the motion of step 3. Do steps 4 and 5 (the
    four wheels, the lane matrix and room) still run for it? *Engine:* they are skipped, as before:
    only the bookkeeping of step 3 runs. The wheels would only add their forces to F, which the
    detector clears in the next sample unless an impact wakes the car, when MM2 would integrate
    them once.

    **Answered** (coordinator; `car-collision.md` 6.3 step 3, `loose-bodies.md` 6.1 step 3): MM2
    does not skip them. A frozen traffic car runs its four wheels and copies its body frame into the
    lane matrix (and updates its room) every sample; the release keeps that lane matrix and does
    not copy the body frame again. Loose bodies run their matrix and room steps every sample (the
    engine already did). Done in the engine; no check run changed.

## Music, after the answers

104. **A new primary's cut and its own early notes** (`formats/directmusic.md`, "Starting a primary
    segment": "every note still sounding at the new start — the motif's included — is cut there";
    "Composing ahead": a note pulled early by its time offset may sound before its own segment
    begins). Does the cut at the new start also cut the new segment's own notes that started early
    (a pick-up note with a negative offset), or only the old music's and the secondaries'?
    *Engine:* the new segment's own notes are spared.

## Menus follow-ups

105. **The race menu's cop control** (`game/menus.md`, "Screens and flow" step 4 and the settings
    table: "cop density 0 … 1"; `progression.md`: "the cop count is compared as a number with the
    cops setting"). The pages do not name the control. *Engine:* a 0 … 1 slider in 19 steps like
    the other densities in every mode. A race starts at its row's `Cops` count (1 or more, shown as
    full) and keeps that exact number until the slider is moved, so an untouched race is still
    recorded; a moved slider gives its own value (1.0 then differs from a count of 2). Texts 606 /
    607 (off / on) are not used for it. Is that MM2's race-menu control, and what value does MM2
    store when the slider is moved back to full?

    **Answered** (cb89a9a, `menus.md` "The cop control"): yes, a 0 … 1 slider over the stored count;
    any move writes the slider's value (arrow steps of 1/19 in single precision from the shown
    position, exactly 1.0 for a step up at the top; a click sets the fraction), the count only comes
    back when the race's settings are loaded again (also after each game and when a driver is
    chosen). Done in the front end (c7683c0).

## Multiplayer (milestone 14)

(Numbered 106 – 123 at the merge into master.)

106. **Player ids and the "player's car" identity** (`game/multiplayer.md`, "Words"). Our session
    server gives each player a random id above 999 (unique in the session); the loopback test
    hub counts from 1001. *Engine choice:* a remote car's collision identity is 30000 + its slot
    (its trailer 30500 + slot), and "identity above 999" is the test "is one of those"; the local
    car keeps the engine's 10001.

107. **The gold's mass and the inertia** (5.3 "Carrying": "mass re-set with the new value"). *Engine:*
    the body's mass and its inertia are both set again from `Mass` + gold and `InertiaBox` by the
    rule of `physics/vehicle-body.md` (and back from `Mass` alone when the gold leaves). Or does MM2
    change only the mass and keep the old inertia?

108. **The host's own wait before "race start"** (5.2 "Waiting"). The host's count can reach 0 before
    its own 5 s have passed. *Engine:* the host sends "race start" only once its own "loaded" has
    gone out too.

109. **The race clock without a time limit** (5.2 "Countdown": "the race clock starts if the time
    limit > 0"). With a time limit of 0 (no shipped race; mods) a Checkpoint or Circuit finisher
    would report 0 s. *Engine:* follows the page (Blitz always runs its clocks). Is that MM2?

110. **Standings in network races** (5.2 "Racing"). *Engine:* the local car's place counts a remote
    car ahead if its player is in the result table, or its score (gates) is larger, or equal and
    it is closer to the local car's **target** waypoint than the local car (the single-player
    rule's distance, `results.md` step 1). A finished local player shows no place.

111. **Cops and Robbers time warnings** (5.3 "End": "when the host's countdown falls below 1200, 900,
    600, 300 and 60 s"). *Engine:* only crossings count: a warning above the starting time is never
    shown (a 5-minute game shows 141 at once, then 142). Or does MM2 show every warning whose time
    is above the countdown (138 – 141 together at the start)?

112. **Scoring events at a Cops and Robbers restart** (5.3 "Points", "Restart"). *Engine:* the host's
    count of scoring events goes back to 0 at a restart; otherwise a remote player's points from
    before the restart, still in its last car state, end the new game at once.

113. **The spot row's random number** (5.3 "Spots", tails: "row r = (random number) mod (n − 1)").
    *Engine:* the game-wide generator; the coin flip is a temporary generator seeded with the
    network clock in milliseconds; heads = odd.

114. **Where knocked-loose and wrecked gold drops** (5.3 "Losing the gold" 1 and 2). *Engine:* P = the
    carrier's body position (centre of mass); a leaving carrier's gold drops where it floats on the
    host (that remote car's body position + (0, 2, 0)).

115. **Drawing the Cops and Robbers objects** (5.3 "Objects"). *Engine choices:* the gold mesh at its
    own size (no scale); the bases like gates (scaled by the radius 12 across and along, 7.5 up),
    turned by their spot's heading (the positions file's `a`; a receiver finds it in its own spot
    list, since only positions travel; 0 for an intersection spot), and drawn from both sides (the
    sign models are one-sided).

116. **A remote car that is not drivable** (3.3 step 13). *Engine:* it gets brake 1 and neutral like
    a local car that is not drivable, and first gear when it becomes drivable again.

117. **Network players' colours on the HUD map** (`hudmap.md`: "one colour per player slot").
    *Engine:* our own palette by remote slot; team colours (0xFFEF0000 / 0xFF0000EF) in Cops and
    Robbers sub-modes 1 and 2.

118. **Host defaults for Cops and Robbers** (1.2 gives Cruise's only). *Engine:* the same as Cruise
    (pedestrians 0.25, noon, clear).

119. **"*name*" + text** (1.3, 5.2, 5.3). Some of MM2's strings start with a space, others do not
    (38 does not: "Ant" + 38 runs together). *Engine:* a space goes between unless one side has it.

120. **London's open bridges and the gold** (1.4, 5.3 "Spots"). With every leaf open, the intersection
    on Tower Bridge (1247.1, 8.0, −148.5) can be the gold's spot (the intersection rule rejects only
    deep water and landmark rooms); it then lies under a raised leaf, out of reach. Is that MM2's
    behaviour too (the engine keeps it)?

121. **A client's own "result" after its slow-down ended** (5.2 "Finishing" 2: "restarts the 3 s
    slow-down"). *Engine:* the slow-down runs again (the car stays undrivable meanwhile, throttle
    reduced, brake 1), then the car is stopped again.

122. **Blitz time up at the host** (5.2 "Finishing, Blitz" 3: "the host enters its own and also sends
    time up"). *Engine:* the host also sends "result" with its own 86400, so the clients' tables
    have it.

123. **Our transport's engine choices** (not MM2's): guaranteed messages carry a per-peer sequence
    number so they arrive in order whichever path they took (data channel or the server's relay);
    a slow-link player's "one copy to everyone" is one message per recipient on our transport, with
    one sequence number for the copy; a hidden browser page catches its network game up in 1/60 s
    frames (1.5: no pause).

124. **Which lobby text is which** (`formats/mmlang.md`, "Multiplayer": "64 – 73, 83 – 88, 398 – 427,
    438, 506 – 517, 641, 657 lobby, session and network texts"). The front end's Multiplayer screens
    need labels for the gold weights, the limit choices (minutes, points), the session list, the
    chat box and the session's mode/weather/time/difficulty summary; `multiplayer.md` names only the
    notices (31 – 47, 89 – 157), and `mmlang.md` the mode list (585 – 589), the sub-modes (8 – 10)
    and the team names (264 – 267). Could the page give each of those lobby numbers its role (as it
    does for the race menu)? *Engine choice:* the mode list, sub-modes, team names, time of day,
    weather, skill and waiting texts by number; our own English for everything else in the lobby.

## Native platform

125. **How MM2 drives its force-feedback effects** (`engine/controls.md` and `formats/profiles.md`
    name the options: force feedback on, collision strength 0 … 2, road strength 0 … 2; menus.md
    offers them "only with force-feedback hardware"). Which DirectInput effects does MM2 create
    (constant force, spring, damper, periodic road texture, jolts), what game values feed them
    (impact strength, surface, speed, steering), and how do the two strengths scale them? *Engine
    choice* (`drive/force_feedback.h`): a jolt when the car's velocity changes by more than
    1.5 m/s in one frame at over 40 m/s² (strength = collision × (Δv − 1) / 10, at most 1, for
    90 + 160 × strength ms); a light road rumble from the speed, the wheels' bump heights and
    their sliding (× road / 2); on wheels a centring spring of road × (0.25 + 0.75 × min(speed /
    25 m/s, 1)) / 2; nothing while paused or with the option off. Gamepads get it as rumble,
    wheels and joysticks through SDL's haptic interface.

126. **A pedal on a whole axis, and the mouse device** (`engine/controls.md`, the binding table:
    components 10 – 15 are whole axes, 17 – 20 half axes). How does MM2 read throttle or brake bound
    to a whole axis (a wheel's separate pedals, which rest at one end): from the calibration, from
    the axis's range, as a half? And how does the mouse device steer (the X-axis entry: pointer
    position, movement, its own shaping with the `Mouse…` keys of `.asNode`)? *Engine choices*
    (`drive/player_controls.h`): a whole axis bound to a pedal reads 0 at the end it rested on
    when the controller was first read and 1 at the other end (an axis resting in the middle reads
    its positive half); the mouse device steers by the pointer's place across the window (centre =
    straight) through the joystick shaping with the Mouse values and U. Also: a pedal bound to a
    button takes the button's analog value (gamepad triggers), and an SDL gamepad's buttons are
    numbered in the Web "standard" layout (button 1 = A … 8 = right trigger), joysticks and wheels
    as the device numbers them.
