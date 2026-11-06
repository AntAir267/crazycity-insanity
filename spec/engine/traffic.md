# Milestone 3: traffic and cops

Free-roam driving in a living city: ambient traffic that drives the streets the way MM2's does,
and police that wait and chase like MM2's cops. Mods that add traffic cars or change a city's
road network work.

## In scope

- **Road network** from each city's AI files (`city/<c>.bai`, `<c>_sup.bai`, `<c>.aimap`): roads,
  lanes, intersections, which way traffic drives, speed limits.
- **Traffic control**: traffic lights (models, placement, timing, day/night glows from `night.md`)
  and stop signs as MM2 has them.
- **Ambient traffic**: the city's traffic car types and densities from `.aimap`, spawned and removed
  around the player as MM2 does; lane following, speed, gaps, turning at intersections, obeying
  lights and stop signs, reacting to the player (braking, honking state for later sound, swerving),
  and what happens when they are hit (they become physical cars that can be pushed, spun, flipped).
  Traffic cars use their own paint sets, wheels turn, brake lights and indicators work, headlights at
  night (`night.md`).
- **Car-to-car collisions**: player with traffic, traffic with traffic, player and cops, exactly as
  MM2's physics resolves them.
- **Cops in Cruise** (`../ai/police.md`): police cars parked at the fixed spots of the Cruise race
  file (they never patrol); what makes one chase (a player within 75 m in the half-plane in front of
  it: MM2 checks no offences), how they pursue (route search through the road network, driving,
  up to three per car, the nearest one switching to blocking in front of the player once within
  25 m), light bars (visuals now,
  siren sound later), when they give up (more than 250 m away: stop where they are) or drop out
  (wrecked, deep water), and that being caught has no consequence in Cruise beyond the
  collisions.
- **Options in the side panel**: traffic density (MM2's settings plus Off), cops on/off (cop
  density 1 or 0, `../ai/police.md`, "How many cops").
- **Check runs**: deterministic city runs with traffic and cops (fixed random seed) so the engine and
  a reference can be compared, as for driving.

## Not in scope yet

Pedestrians, opponents and race modes, sound, damage visuals, trains/cable cars/subway trains
(note them if the road files describe them), multiplayer.

## Done when

- London and SF streets have traffic that looks and behaves like MM2's: right density, lanes, stops at
  lights, turns, and gets knocked around when hit.
- Driving in front of a parked cop starts a chase; cops follow through the streets, block, push
  through the collisions and give up as in MM2 (`../ai/police.md`).
- Collisions with traffic and cops behave like MM2.
- It stays smooth in the browser.
