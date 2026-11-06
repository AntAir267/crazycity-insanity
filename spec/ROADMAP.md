# Roadmap: MM2 features not yet specified

Milestones 1–3 (viewer, driving, traffic and cops) are specified in this folder. This page lists
what Midtown Madness 2 still does that `spec/` does not yet describe, grouped into milestones,
with the game files each one reads and a rough size. Paths are inside the game archives
(`ar.md`); `<c>` is a city, `<car>` a vehicle base name, `<N>` a race number.

Sizes are for the spec work: **S** about one page, **M** two or three pages, **L** four or more
(compare `physics/vehicle-*.md` together, about 1,600 lines). The order is a suggestion.

## Milestone 4: race modes and opponents

The single-player modes around the car, and the computer racers. Race modes now specified in
`game/races.md`, `game/waypoints.md`, `game/modes.md`, `game/crash-course.md`, `game/results.md`.

| Feature | Files | Size |
|---|---|---|
| Race catalogue: what the menus do with the race counts and names of each city (the city list itself, its names, level files and race folder are specified: `formats/city-files.md`, "City list") | `tune/*.cinfo`, `tune/<car>.info` (scoring and unlock fields) | S |
| Race settings per race and skill level (car class, time of day, weather, opponents, cops, traffic and pedestrian density, laps, time limit, difficulty) | `race/<c>/mmracedata.csv`, `mmblitzdata.csv`, `mmcircuitdata.csv`, `mmcrashdata.csv` | S |
| Waypoints and checkpoint gates: position files, radius, gate line, hit test, ordered/unordered/finish-locked layouts, laps, the heading fix-up | `race/<c>/race<N>waypoints.csv`, `blitz<N>waypoints.csv`, `circuit<N>waypoints.csv`; models `geometry/pt_check.pkg`, `pt_finish.pkg` | M |
| Opponents (specified: `ai/opponents.md`, `formats/opp.md`): route files, the opponent rows of the race file (car, throttle, look-ahead, brake threshold, switches, corner factor), laps, repair, finishing, physics distance rule | `race/<c>/<race>-a-<k>.opp` and `-p-` (Professional), `race/<c>/<race>.aimap` / `.aimap_p` `[Opponent]` | M |
| Mode rules: Checkpoint, Blitz, Circuit (start, countdown, timers and warnings, standings, finish, out-of-time, wreck and water rules, penalties, next race) and the rest of Cruise (music and speech cues, map icons) | the files above | L |
| Crash Course: event list per course, the event types by number, pass and fail rules (time, damage, pursuit, speed, distance to the leader), default cars | `race/<c>/crash<N>data.csv`, `crash<N>data_p.csv`, the event files named in them (`longjump.csv`, `corner.csv`, `slalom.csv`, `exam*.csv`, `final*.csv`, …), `crash<N>.aimap`, `crash<N>.pathset`, `crash<N>-<k>.opp` | M |
| Police in race modes: counts from the race settings, the pursuit check that fails a course | `race/<c>/<race>.aimap` `[Police]` | S |
| Race cameras: pre-race fly-in, post-race camera | `tune/camera/<car>_far.camTrackCS` (start point) | S |
| Results and scoring: standings, score from difficulty, place and the car's scoring bias | `tune/<car>.info` | S |

## Milestone 5: moving level objects and props that move

| Feature | Files | Size |
|---|---|---|
| Drawbridges: leaves, timing, modes chosen by the path name, open/closed rooms | `race/<c>/<c>_bridge.pathset`, `<c>_bridge_<race>.pathset`; `geometry/giz_bridge01_l.pkg` | S |
| Ferries and sailboats: path following, speeds, static two-point paths | `race/<c>/<c>_ferry[_<race>].pathset`, `<c>_sailboat.pathset`; `geometry/giz_carferry01_f.pkg`, `giz_sailboat01_f.pkg` | S |
| Parked cars: which slots stay empty, models, paint | `race/<c>/<c>_parkedcar[_<race>].pathset`; `geometry/giz_pcar01_l.pkg`, `giz_pcar02_l.pkg` | S |
| Subway trains on path sets: cars per train, speed, stations and stops | `race/<c>/<c>_train.pathset`; `geometry/va_ug_l.pkg` | S |
| Cable cars and network trains: driving speed and braking, next road, size, what a hit does | `city/<c>.bai` cable and subway lines; `city/<c>.aimap` `[Subway]`; `geometry/va_cablecar_f.pkg`, `tune/vehicle/va_cablecar_f.aiVehicleData`, `tune/banger/va_cablecar_f.dgBangerData` | M |
| Props after they break: the pool of loose props, break-off parts, launch velocities, their collisions with the city, cars and each other, return on reset | `tune/banger/<name>.dgBangerData` and `<name>_BREAK<nn>.dgBangerData`; `BREAK<nn>` parts of the prop packages | M |

## Milestone 6: pedestrians

Specified in `ai/pedestrians.md`, `ai/pedestrian-walking.md`, `ai/pedestrian-reactions.md` and
`formats/ped-anim.md` (the `.remap` file turned out to be unused).

| Feature | Files | Size |
|---|---|---|
| Pedestrian models, skeletons and animations | `anim/pedmodel_*.skel`, `.mod`, `.shaders`, `.rays`, `.csv`, `.remap`; `anim/pedanim_*.anim` | M |
| Population: pool size, model pairs for dry and rainy weather, where they appear (sidewalk lines, pedestrian activation table) | `city/<c>.aimap` `[Ped Pool]`, `[GoodWeatherPedName / BadWeatherPedName]`; race files' overrides; `city/<c>.bai` | M |
| Walking, crossing at lights, dodging cars | `city/<c>.bai`; traffic-light walk phases (`ai/traffic-control.md`) | M |

## Milestone 7: sound

Specified in `audio/` (`audio/sound-system.md`, `audio/events.md` and the pages they list).

| Feature | Files | Size |
|---|---|---|
| Sound files: lookup, sample rates, the two sample folders; mixer, volume and pitch limits, tunnel echo | `aud/aud22/**`, `aud/aud11/**` | S |
| Player and opponent car sounds: engine samples by RPM, horn, gear clunk, surface and skid loops, tyre wobble, suspension thumps, impacts by force, semi and bus sounds, sirens | `aud/cardata/player/<car>.csv`, `aud/cardata/opponent/<car>.csv`, `default.csv`, `engineparamsPlay.csv`, `engineparamsOpp.csv`, `default_surfacedry.csv`, `default_surfacewet.csv`, `tirewobble.csv`, `suspensionaudio.csv`, `default_impacts.csv`, `londonpolicesiren.csv`, `sfpolicesiren.csv`, `aud/cardata/shared/vehtypes.csv`, `semidata.csv` | M |
| Traffic and pedestrian sounds: engine loops, horn patterns, driver and pedestrian voices | `aud/cardata/ambient/<car>_engine.csv`, `<car>_horn.csv`, `default_*.csv`; `aud/creaturedata/*.csv` | M |
| City ambience: 3D ambient objects with their points, areas (outdoors, tunnels), one-shot timers; ferry, bridge, subway, cable car sounds; rain and thunder | `aud/ambient/<c>ambientcontainer.csv` and the files it lists (`ferry.csv`, `drawbridge.csv`, `subwaycar.csv`, `londonriver.csv`, …); `aud/aud22/Amb3D/` | M |
| Race speech: announcers per city, pre-race, checkpoint, results and unlock lines, Crash Course instructor | `aud/spchdata/<c>.csv`, `aud/spchdata/<voice>/*.csv`, `aud/aud11/<voice>/` | S |
| Game and menu one-shots: countdown, waypoint, timer warning, lose, menu clicks | `aud/aud22/*.wav` | S |

## Milestone 8: music

Specified in `audio/music.md` (rules) and `formats/directmusic.md` (files, composing, synthesiser).

| Feature | Files | Size |
|---|---|---|
| Music playback: segments, styles, instrument banks, bands, motifs, transitions | `aud/dmusic/*.sgt`, `*.sty`, `*.dls`, `*.bnd` | L |
| Music rules: which song per game, start, idle when slow, cop chase, last checkpoint, pause, results, big-air motif; ambience instead of music; menu music | `aud/dmusic/csv_files/singlerace.csv`, `singleroam.csv`, `ui.csv`, `sfambience.csv`, `londonambience.csv` | S |

## Milestone 9: HUD, front end and text

Specified in `game/hud.md`, `game/hudmap.md`, `game/menus.md` and `formats/mmlang.md`
(function and data; the look is ours).

| Feature | Files | Size |
|---|---|---|
| HUD: gauges overlay (speed, gear, damage, RPM), messages, timers, direction arrow, place, checkpoint and lap displays, small-screen art | textures `DIGITAC_*`, `DIGI_*`, `DAMAGE`, `DAMAGE_LABLE`, `SPEED_TICKS` (and `_half` versions); `geometry/hudarrow01.pkg` | M |
| HUD map: size, position, zoom, icons, ocean colour, full-screen map | `tune/<c>.mmHudMap`; `geometry/hudmap_<c>.pkg`, `hudmap_tri.pkg`, `hudmap_square.pkg` | S |
| Menus, showroom, results, load screens, options | `tune/menu.csv`, `tune/widget.csv`, `jpg/*.jpg` (menu backgrounds, `<car>_show.jpg`, `<c>_<mode><N>.jpg`, `splash.jpg`) | L (our own design may replace much of it) |
| Text, fonts and intro video | `mmlang.dll` string tables (game text, font descriptions); `logos.avi` | S |

## Milestone 10: profiles, records and unlocks

Specified in `formats/profiles.md` and `game/progression.md`.

| Feature | Files | Size |
|---|---|---|
| Player list and save files; the remaining option block of the player file (the 56-byte block; the sound values and the saved view settings are in `engine/controls.md`, "Options in `players/<name>.cfg`") | `players/players.dir`, `players/<name>.sav`, `players/<name>.cfg` | S |
| Records per city: best times, scores, passed races | `players/<c>/<name>.rec`, `players/<c>/amateur.dat`, `pro.dat` | S |
| Unlocks: cars and paint jobs earned per race type, which races open when, settings unlocked after passing | `race/<c>/<c>_rewards.csv`; `tune/<car>.info` (`UnlockScore`, `UnlockFlags`); `tune/*.cinfo` | S |

## Milestone 11: room visibility (specified)

Specified in `formats/cpvs.md` (file layout, run-length coding, the visibility rule, the fallback
without a file); what remains is building it (`engine/viewer.md`, "Draw distance").

## Milestone 12: damage visuals and effects

| Feature | Files | Size |
|---|---|---|
| Body damage textures and the damage radius | `_dmg` texture slots of `geometry/<car>.pkg`; `tune/vehicle/<car>.vehCarDamage` | S |
| Parts that break off: per impact, and wheels, hubs, fenders and engine at a wreck (which part and when, and the launch, are in `physics/damage.md`, "Parts thrown off"; left: the loose part's own physics as a prop) | `BREAK*`, `VARIANT*`, `WHL*`, `HUB*`, `FNDR*`, `ENGINE` parts and their `.mtx` files | S |
| Particle system: birth rules, update, ground plane, drawing | `tune/*.asBirthRule`, `tune/effects/*.asBirthRule`, `BirthRule` blocks in `tune/banger/*.dgBangerData`; textures `ptx_*`, `fxpt<N>` | M |
| Effects that use it: wheel dust, grass, spray and smoke by surface; engine and exhaust smoke; sparks; glass shards; prop debris; rain (where it is hidden and the emitter point are in `formats/night.md` §6) | `city/materials.mtl` particle fields; `tune/rain.asBirthRule`; `geometry/<car>_exhaust0.mtx`, `_exhaust1.mtx`; `tune/effects/Engine Smoke Rule.asBirthRule` | M |
| Skid marks | texture `tire_track` | S |
| Car wheel details: drawn wheels (camber, wobble, the anti-roll axle tilt). (The tail and brake light switches never turn off in MM2, `formats/night.md` "Cars".) | `TLIGHT`, `BLIGHT`, `WHL*` parts of `geometry/<car>.pkg` | S |

## Milestone 13: trailers (specified: `physics/trailer.md`)

| Feature | Files | Size |
|---|---|---|
| Trailer body, hitch joint, trailer wheels, the load the trailer puts on the tractor | `geometry/<car>_trailer.pkg`, `<car>_trailer_*.mtx`, `tune/<car>.vehTrailer`, `tune/<car>.dgTrailerJoint`, `tune/vehicle/<car>.dgTrailerJoint`, `bound/<car>_trailer_BOUND.bnd` (shipped for vpsemi, vpcentury, vpftruck, vplafrance; MM2 attaches a trailer only to vpsemi, vpftruck and vplafrance, `formats/car-model.md` "Trailers") | M |

## Milestone 14: multiplayer

Specified in `game/multiplayer.md` (rules, settings, shared state; the transport is ours).

| Feature | Files | Size |
|---|---|---|
| Multiplayer Cruise, Checkpoint, Blitz, Circuit and Cops and Robbers: rules, start points, the gold, what is shared between players | race names `multicop` and `croam` (`formats/aimap.md`; no `.aimap` ships for them), `race/<c>/multicopwaypoints.csv`, `race/london/london_bridge_multi.pathset`; `jpg/<c>_multicop.jpg` | L (the network layer is our own) |

## Not planned

- `[Hookmen]` rows in race files (no shipped mode uses them).
- Snow (`tune/snow.asBirthRule` ships, but the game has no snow weather).
- The leftovers listed in `formats/city-files.md`.
