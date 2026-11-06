# Sound events: what plays when, and who produces the event

An index over the sound pages. For each game event: the sound it plays, the page with the sound's
rules, and the game page that produces the event. Events that no game page exposes yet are listed
at the end; the builder needs them wired when the producing system is built.

## The sources that compete for the 4 slots

Every positioned source of the game asks for one of the 4 slots of `sound-system.md` ("3D
sources"). Priorities (a 2D source adds 1,000,000):

| Source | 2D/3D | Priority | Drop-offs (m) | Doppler k | Page |
|---|---|---:|---|---|---|
| player's own car (engine, horn, tyres, impacts …) | 2D | 9 (+1,000,000) | — | — | `car-sounds.md` |
| ambient city object, ferry, drawbridge leaf, Underground train | 3D | from its file (12 shipped) | from its file | 0.0176315 | `world-sounds.md` |
| police car, siren sounding (keeps its slot past max) | 3D | 10 | 0–150 | 0.0176315 | `car-sounds.md`, `traffic-sounds.md` |
| opponent / network car | 3D | 9 | 0–150 | 0.0176315 | `car-sounds.md` |
| traffic car | 3D | 8 | 0–100 | 0.035263 | `traffic-sounds.md` |
| cable car | 3D | 8 | 0–100 | 0.0176315 | `world-sounds.md` |
| pedestrian voice | 3D | 8 | 0–40 | (no pitch) | `traffic-sounds.md` |
| police car, siren off | 3D | 7 | 0–150 | 0.0176315 | `car-sounds.md` |

So the player's car always holds a slot and the other three go to the nearest sources of the
highest priority ("Getting and losing a slot"): near the water the gulls and tug horns (12) take
slots from cars, a chasing cop (10) from opponents and traffic.

Not slot sources (plain 2D samples): race speech and the Crash Course instructor, race-mode and
menu one-shots (`speech.md`), rain and thunder (`world-sounds.md`).

## Events and sounds

### Player and AI cars

| Event | Sound | Rules | Produced by |
|---|---|---|---|
| every car update: engine RPM | engine samples (volume window and pitch per RPM) | `car-sounds.md` "Engine" | `../physics/vehicle-powertrain.md` |
| gear index enters / leaves reverse | gear click (`clutch wave`); semi/bus reverse beeper while in reverse | `car-sounds.md` "Gear click", "Semi and bus extras" | `../physics/vehicle-powertrain.md` "Gear slots"; `../engine/controls.md` |
| horn control held (player) | horn loop | `car-sounds.md` "Horn" | `../engine/controls.md` |
| horn press on a light-bar car | siren on/off | `car-sounds.md` "Siren" | `../ai/police.md` "Lights" |
| stopped big vehicle | air-brake blow | `car-sounds.md` "Semi and bus extras" | `../physics/vehicle.md` step 2 |
| wheels on the ground, sliding | rolling and skid loops by surface | `tyre-impact-sounds.md` "Surface sounds" | `../physics/vehicle-wheels.md`, `../physics/collision.md` "Materials" |
| damaged wheel turning | tyre wobble thump | `tyre-impact-sounds.md` "Tyre wobble" | `../physics/damage.md` |
| hard suspension compression | suspension thump | `tyre-impact-sounds.md` "Suspension thump" | `../physics/vehicle-wheels.md` |
| a counted impact (car) | impact samples by the other side's id and force | `tyre-impact-sounds.md` "Impact sounds" | `../physics/damage.md` "Per impact report" step 5 |
| an impact on a traffic body | impact samples; stuck horn (25 %); driver shout | `tyre-impact-sounds.md`, `traffic-sounds.md` | `../physics/car-collision.md` 3.4; `../ai/traffic-reactions.md` |
| falling into water | no splash sound in MM2 (impact id 22 only plays for a prop whose `ColliderId` is 22) | `tyre-impact-sounds.md` "Scraping, splashes …" | — |
| all wheels off the ground | airborne flag (music big-air motif) | `car-sounds.md` "Airborne flag" | `../physics/vehicle-wheels.md` |
| player damaged out | engine silenced (Checkpoint, Blitz); `Damgelose` (`Messagenote` in Circuit); Blitz damage-penalty line | `car-sounds.md`, `speech.md` | `../physics/damage.md` "Wrecked"; race modes (milestone 4) |
| player car's room enclosed / not | echo state on / off: echo copies everywhere, rain quieter, ambient areas | `sound-system.md` "Echo" | `../formats/psdl.md` "Room flags" 0x02 |

### Traffic, police, pedestrians

| Event | Sound | Rules | Produced by |
|---|---|---|---|
| every traffic car update | traffic engine loop (pitch by speed) | `traffic-sounds.md` "Traffic cars" | `../ai/traffic.md` "One traffic car update" |
| a traffic car starts avoiding a player | honk (≈ 50 %), then maybe a driver shout | `traffic-sounds.md` | `../ai/traffic-reactions.md` "Avoiding a player" |
| cop enters FOLLOW | siren starts (counted for the music when the perp is the local player) | `car-sounds.md` "Siren", `traffic-sounds.md` "Police" | `../ai/police.md` "Following or apprehending" |
| the perp escapes | siren stops; with explosion: explosion and wind-down | `car-sounds.md` "Siren" | `../ai/police.md` "The perp escapes" |
| cop reset | siren off | `car-sounds.md` | `../ai/police.md` "Parking (reset)" |
| pedestrian dives, runs, backs up | pedestrian scream | `traffic-sounds.md` "Pedestrian voices" | pedestrian AI (milestone 6, not specified) |

### City and weather

| Event | Sound | Rules | Produced by |
|---|---|---|---|
| game set-up with the ambient option, city `london`/`sf` | city ambient objects (gulls, buoys, tug horns, street cable, station voices) | `world-sounds.md` "City ambience" | option bits `sound-system.md` |
| ferry exists | engine loop, horn every 5–10 s | `world-sounds.md` "Ferries" | ferries (milestone 5) |
| drawbridge leaf starts / ends moving | motor loop and bell | `world-sounds.md` "Drawbridges" | drawbridges (milestone 5) |
| Underground train leaves / stops | `LondonTube` (in tunnels only) | `world-sounds.md` "Underground trains" | path-set trains (milestone 5) |
| cable car speed changes | start, loop, stop, go-bell | `world-sounds.md` "Cable cars" | cable cars (milestone 5) |
| weather rain; view change | rain loop (exterior / interior) | `world-sounds.md` "Rain and thunder" | `../formats/night.md`; `../engine/cameras.md` "Switching views" |
| rain at night | thunder every ~16 s | `world-sounds.md` | `../formats/night.md` |

### Race modes and menus

| Event | Sound | Rules | Produced by |
|---|---|---|---|
| game start / restart | pre-race line (1.5 s delay); Crash Course intro | `speech.md` "Pre-race", "Crash Course" | race modes (milestone 4) |
| countdown | `Startracelow` ×2, `Startracehigh` | `speech.md` "Race-mode one-shots" | race modes |
| gate passed | `Waypoint` / `Lastwaypoint`; Crash Course location line | `speech.md` | race modes (gates) |
| only the finish left | final-checkpoint line (music switches) | `speech.md` | race modes (gates) |
| finish | `Endofracetag` / `Youlose` | `speech.md` | race modes |
| results screen | results line or unlock line; every slot released | `speech.md`; `sound-system.md` "Pause, results and reset" | race modes, unlocks (milestone 10) |
| Blitz clock (Crash Course: clean-driving and no-contact events) below 10 s | `Timerwarning` once a second, looped below 3 s | `speech.md` | race modes |
| HUD message | `Messagenote` | `speech.md` | HUD (milestone 9) |
| menu focus, choice, slider; section entered; race setting; car shown | `Moveselector`, `Selectionmade`, `Switch`; UI voices; `<car>_select` | `speech.md` "Menu one-shots" | menus (milestone 9, our own design) |
| pause | every sample stopped; loops restart after | `sound-system.md` "Pause" | `../engine/controls.md` |
| options changed | gain, balance, stereo, music/ambient | `sound-system.md` "Options" | menus; `../engine/controls.md` player file |

## Events not exposed yet

The sound pages need these from systems that are not specified or do not yet hand them over:

- **Impact report detail** (`tyre-impact-sounds.md`, "Events"): the impulse vector J and the other
  side's impact sound id (collision bodies need an id: 0 by default, the prop's `AudioId`, a loose
  body's id, 1000 + n for a network car); the wheel's material `sound` number with its last ground
  hit.
- **Traffic**: a knocked car's impulse J per impact (for the stuck horn and the shout); a car
  leaving the streets (its source reset) (`traffic-sounds.md`).
- **Pedestrians** (milestone 6): dive start, run start, back-up end, and the model name (male or
  female voice).
- **Moving level objects** (milestone 5): ferry positions, bridge leaf start/end of movement,
  train waiting/moving and the middle car's position, cable car speed and position.
- **Race modes** (milestone 4): game reset, countdown states, gate passed (with the gate kind),
  the "finish only" moment, finish place, damage out, Blitz clock, results with the unlock check.
- **Menus and HUD** (milestone 9): focus moves, value chosen, slider moved, menu section entered,
  race setting chosen, car shown, HUD message with sound.
- **Network games** (milestone 14): the network horn bit, `Carhorn1double`.

## Shipped files nothing plays

`tools/audio/sounds.py unused` lists every WAV no reference reaches (436 of 2,503): the 11 kHz
copies in `aud/aud11/` of `mm2audex` (226; MM2 never switches to them), unplayed speech categories,
`Amb3D` files named by no ambient file, older engine and impact takes, the `AudiSelect`-style car
clips. Data files never read: `aud/cardata/opponent/policesiren.csv`, `default_surfaceice.csv`,
`engineparamsPlay/Opp.csv` (see `car-sounds.md`), `aud/cardata/ambient/subwaycar.csv`, the `aud/ambient`
`waves`, `maleped*`, `femaleped*` files, `aud/spchdata/CNRSF.csv`, the Crash Course texture files,
the "Copy of …" and `.bat` files.

## Checked against

`tools/audio/sounds.py` imports every topic tool (`car_sounds`, `tyre_impact`, `traffic_sounds`,
`world_sounds`, `speech`) and resolves each sample name the pages can build against the archives;
its summary per category is in `work/check/audio/sounds_summary.txt` (see "Checked against" of
each page for the missing names and why they are harmless).
