# Later milestones: requirements

The general rule for every milestone below: behave exactly as MM2 does with the player's own MM2
data and mods, and be checkable against a reference where it is deterministic. Our own look applies
only where noted (menus and HUD). Details and data files per item are in `../ROADMAP.md`.

## Race modes and opponents
Every MM2 single-player mode: Cruise (complete), Checkpoint, Blitz, Circuit, Crash Course, with
their race catalogue per city, settings (`mm*data.csv`), waypoints and gates, opponents and their
driving, police in races, countdown, standings, finish, results and scoring, pre/post-race cameras,
amateur/professional differences. Check runs extended to opponents with a fixed seed.

## Moving level objects
Drawbridges, ferries, sailboats, parked cars, path-set trains, cable cars (SF) and anything else MM2
moves along paths, with collisions as MM2 has them.

## Pedestrians
Models, animations, population, walking and crossing, dodging the player, what happens when hit, as
MM2 does it.

## Sound
All of MM2's sound from the player's MM2 audio archives: player car (engine, gears, tyres, surface,
impacts, horn, siren), traffic and police, pedestrians, city ambience, race speech and one-shots,
3D positioning and mixing as MM2 does, in the browser (Web Audio). Specified in `../audio/`
(start at `../audio/sound-system.md`; event index `../audio/events.md`).

## Music
MM2's soundtrack from the player's files (DirectMusic segments, styles, DLS banks) with MM2's music
rules (when tracks change: menus, cruising, chases, big air, results), in the browser.

## HUD, menus, profiles, records, unlocks
Functionally what MM2 offers (HUD information, HUD map, race selection, car selection, options,
profiles, records, unlock progression) but in **our own visual design**, not imitating MM2's menus
or HUD. Data formats (HUD map, player files, records, rewards) exactly as MM2 reads them, so a
player's MM2 profile could be imported.

## Trailers
The semi's and other trucks' trailers: joint, physics, collisions, visuals, as MM2 does.

## Multiplayer
MM2's multiplayer game rules (modes such as cops and robbers, capture the flag, races) with our own
network layer (browser: WebRTC/WebSocket). Specify MM2's rules; the transport is ours.
