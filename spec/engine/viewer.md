# Milestone 1: the viewer

A browser page that opens Midtown Madness 2 game data (including mods) and shows its cities and
cars. It is the first piece of the CrazyCity Insanity engine, so its code is organised the way the
game will need it, not as a throwaway.

## Technology

- C++17, compiled with Emscripten (`~/emsdk`) to WebAssembly. Graphics through WebGL 2 (OpenGL ES
  3.0), window and input through SDL2 (`-sUSE_SDL=2`, already cached). Keep platform code thin so
  a native desktop build can be added later.
- Floats are 32-bit throughout (game math will need to match single-precision results later).
- A `build.sh` in the repo root: `./build.sh web` writes `dist/` (page, `.js`, `.wasm`).
- The dev server is `python3 -m http.server 8820 --bind 127.0.0.1 --directory dist`. For local
  testing `dist/game/` holds symlinks to `orig/*.ar` (create them in `build.sh`; `dist/` is not
  checked in).

## Loading game data

- Read MM2 `.ar` archives directly (spec/formats/ar.md). Nothing is unpacked ahead of time.
- Two ways in:
  - Dev: fetch the archives listed in `dist/game/` (`mm2core.ar`, `mm2tex.ar`; skip audio for now).
  - Public: the person picks their own MM2 folder (directory picker or multi-file input). Files are
    read in the browser and never uploaded.
- Mods are more `.ar` files. Which archive wins for a path that several contain follows MM2's rule
  (`ar.md`, "Several archives"): all archives sorted by upper-cased file name, the first one wins
  (compare paths case-insensitively). The base archives are not special.
- One virtual file system for the whole engine: `open(path)` returns the bytes from whichever
  archive won.

## What it shows

- **City view.** A list of every city found in the data, made as MM2 makes it
  (`../formats/city-files.md`, "City list"): `tune/sf.cinfo` first, then every other `tune/*.cinfo`
  in folder-listing order; a city appears only when its `.cinfo` reads correctly and its `RaceDir`
  is new, and is shown by its `LocalizedName`. Its level files are the ones named by its `MapName`
  (`city/<MapName>.psdl`, `.inst`, …). **Our choice (Ant, 2026-10-15):** after MM2's list, also list
  every `city/*.psdl` that no listed city uses as its `MapName` (e.g. `city`, `sfai`, `variant`),
  marked as a test layout, using the same file name for its other city files. Build the city's
  streets, sidewalks, buildings and other surfaces from the PSDL, place its props from the INST file,
  draw the sky. Textured, with simple lighting. Free-fly camera: WASD + mouse look (pointer lock),
  Shift for fast, Q/E down/up. Show the camera position on screen.
- **Car view.** A list of every car (`geometry/vp*.pkg` and mod cars). Show the car assembled on
  its wheels on a turntable that the mouse can orbit and zoom. Controls for paint variant and level
  of detail.
- **Car in city.** A button to drop the selected car on the street below the camera.
- Missing or broken files must not crash the page: log them in an on-page list and keep going.

## Look

The page's own controls are plain and modern (a small side panel). Do not imitate Midtown Madness 2's
menus or HUD.

## Done when

- London and San Francisco both load and look like cities from street level and from above, with
  correct textures on roads, sidewalks and buildings, and props in place.
- Every stock car shows correctly with all its paint variants.
- A mod car `.ar` dropped in alongside the base archives shows up in the car list.
- Loading a city takes a few seconds at most and flying around stays smooth.

## Draw distance (our option, not MM2 behaviour)

A "Draw distance" setting, used by both the fly camera and Drive mode, remembered per browser:

- **MM2:** far plane, fog and object detail distances exactly as MM2 sets them in a game: the far
  plane is the player's view distance from the profile (600 m with the shipped profile; not the
  camera files' `CameraFar`, which MM2 replaces in a game, `cameras.md` "Projection"), the fog as
  the city files give it, clamped to that far plane, and only the rooms MM2 would draw: the room list of
  `../formats/cpvs.md` (from the city's `.cpvs`, or the room walk when it has none), with everything
  filed in those rooms.
- **Full map:** everything in the city is drawn from anywhere. The far plane reaches past the
  farthest corner of the city's bounds from the camera; fog is off (or starts beyond the far plane);
  rooms, landmarks, static objects and props are never dropped for distance; objects still switch
  between H/M/L/VL by distance as usual so it stays fast, except that an object with no mesh at the
  chosen level uses its nearest more detailed mesh instead of disappearing (many landmarks that
  carry their block's ground have no VL; see `inst.md` "Missing levels"), and a landmark that
  carries its block's ground (it sits in a block room whose PSDL element list is empty, `psdl.md`
  "Ground supplied by landmarks") is never drawn below L, because some VL meshes leave the ground out; the `.cpvs` room list is
  not used (every room is drawn); the sky dome is scaled so it is never
  clipped. Depth precision must hold over the whole range: no z-fighting on curbs, road markings or
  facades at a few kilometres (choose near plane, depth format or depth technique accordingly).
- The fly camera's existing 600 m / 1 km / 2 km / 4 km choices become part of the same setting
  (they draw every room in view within that distance; only "MM2" uses the `.cpvs` room list).

This changes only drawing. Physics, cameras' movement and check runs are unaffected.

## Wall shading (our option, not MM2 behaviour)

A "Wall shading" setting, used by the fly camera and Drive mode, remembered per browser:

- **Soft (default):** the wall light table is built from the current time of day and weather
  exactly as `psdl.md` describes, then each channel c (0–255) is squeezed into the range of MM2's
  built-in default table (118–236): `floor(118 + c·118/255)`. Sunlit 255 becomes 236 (as bright as
  MM2's default lighting ever gets); SF clear noon's darkest entry (73, 73, 93) becomes
  (151, 151, 161) and the k = 6 entry (100, 100, 110) becomes (164, 164, 168). The time of day's
  direction and tint stay visible, but walls are never pure white or very dark, like the look of
  MM2's first race. (An earlier version lifted only the dark end, `F + c·(255 − F)/255` with F = 120;
  it made scenes look too bright and flat.)
- **MM2 exact:** the table as `psdl.md` gives it from the current conditions, unchanged.
- **Flat:** every entry white (walls show their textures unshaded).

Only the PSDL wall light table is affected; models, the ground overlay, night rules and physics are
unchanged.
