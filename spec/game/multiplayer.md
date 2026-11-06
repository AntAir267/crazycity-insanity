# Multiplayer: game rules and shared state

MM2's network games: what the host chooses, how a session starts, what every mode does, what each
machine sends to the others and when, and how a machine moves the other players' cars between
their messages. MM2 carried this over DirectPlay; our transport is our own (WebRTC data channels
or a WebSocket relay). This page does **not** specify a wire format. It specifies the **events
and records** that have to reach the other players, their contents, who sends them, how often and
how reliably, and what the receiver does with them, so that a protocol built on it plays like MM2.

Related pages: the race modes themselves (gates, laps, timers, finishing, single-player rules) are
the race-modes spec (`../ROADMAP.md`, milestone 4); the HUD and menus are ours
(`../engine/later-milestones.md`); message texts come from `mmlang.dll` by number
(`../formats/mmlang.md`; numbers are given below as "text *n*"; the lobby and other network
screens' texts are listed there under "Multiplayer").

## Words

- **Session**: one hosted game. **Host**: the machine that created it (or took it over, "Host
  leaves"). **Peer**: any machine in it, the host included.
- **Player id**: a unique, non-zero number per player for the life of the session. MM2 uses it
  for three things that our engine must keep: the order of the start grid, the seed of the
  network respawn, and the collision identity of the player's car (id + 1000; MM2's ids are always
  above 999, so "identity above 999" means "a player's car" — our engine should use that meaning,
  not the number).
- **Local car**: the car the player at this machine drives; **remote car**: the copy of another
  player's car that this machine shows and simulates. A machine has at most 8 remote cars
  (slots 0–7).
- **Guaranteed / unguaranteed**: a message that must arrive, in order, exactly once / one that may
  be lost or arrive late or out of order. All events below are guaranteed except the car state.
- **"*name*" + text *n***: MM2 never glues a name to a text. It uses one of two forms:
  **two lines**, the player's name as the primary message for 5 s and text *n* as the secondary
  line (`hud.md`) — texts 38 and 42 and every "finished in" notice (its secondary line is
  "text *time*", one space between); **one line** "*name* text" with exactly one space — texts
  41, 43 and 47 (posted to the chat, like 46) and 133, 135, 136 and 137 (primary message, 5 s).
  No shipped text starts or ends with a space. Chat lines are "*name*: *text*".
- **Frame**, dt: one game update and its frame time (`../engine/controls.md`).

## 1. The session

### 1.1 Players

Up to **8 players** (the host's "maximum players" setting, default 8; MM2 never shows more than
8 cars). A session has exactly one host.

Each player publishes a **player record** that every peer can read at any time and that is
re-sent when it changes (MM2: DirectPlay player data; ours: a guaranteed "record changed" event):

| Field | Meaning |
|---|---|
| name | the player's network name |
| car | vehicle base name (`tune/<car>.info` `BaseName`) |
| paint | paint job index |
| team | Cops and Robbers team, 0 or 1 (section 5.3) |
| in game | 1 while the player is inside a running game (Cruise and Cops and Robbers set it when their game starts; cleared on "return to lobby") |
| host | 1 for the host's record |
| ready | the player pressed "ready" in the lobby |
| city | the city name (`RaceDir`) the player has loaded |
| car check value | section 3.7 |
| slow link | the player chose a dial-up connection (section 3.2) |

Two further booleans travel in the **host's** record and are copied by every joining machine:
**cheat check** and **wait for all finishers**. MM2 has no menu for either; both are always on.
Keep both on (they are there so a later option can switch them).

### 1.2 Settings the host chooses

The host picks a mode and its settings in the lobby; every peer reads them when it joins and again
whenever the host changes them (MM2 re-sends a "new settings" event). MM2 packs them into three
32-bit words. Our protocol may send them any way, but **every value must be limited exactly as the
packing limits it**, because a joining peer only ever gets the packed values:

| Setting | Values | Packing limit |
|---|---|---|
| mode | 0 Cruise, 1 Checkpoint race, 2 Cops and Robbers, 3 Circuit race, 4 Blitz race (5 and 6 are never used in multiplayer; a Crash Course choice becomes Cruise) | 4 bits |
| city | the city's name (`RaceDir`, `../formats/city-files.md`) | text |
| race number | the race of the chosen mode (0 for Cruise and Cops and Robbers) | 4 bits: races 16 and up arrive as *n* mod 16 |
| time of day, weather | as in the race files | 4 bits each |
| skill level | 0 amateur, 1 professional | 2 bits |
| no damage | 1 = cars take no damage | 1 bit; MM2 never sets it (no menu); Cops and Robbers always clears it |
| laps | Circuit laps (Checkpoint sends 1, Blitz 0) | 4 bits (0–15) |
| time limit | race time limit in seconds (Blitz countdown; the others only start a clock) | truncated to a whole number, then 8 bits: 0–255 s, larger values arrive mod 256 |
| pedestrian density | 0…1 | sent as trunc(*d* × 255), read back as byte × 0.003921569 |
| Cops and Robbers byte | sub-mode, limit, gold mass (5.3) | replaces laps and time limit in mode 2 |

The Cops and Robbers byte: bits 6–7 sub-mode (0 free-for-all, 1 cops and robbers, 2 robber
teams), bits 4–5 gold weight (0, 1, 2 → mass **0, 100, 200**), bits 2–3 limit type (0 none, 1 time,
2 points), bits 0–1 limit choice (time: **5, 10, 20, 30 minutes**; points: **100, 250, 500,
1000**). MM2's labels call the weights weightless, quarter ton and half ton; the masses are 0, 100
and 200 kg.

**Host defaults when a race is picked** (from the race data files, the host's skill level column,
race-modes spec): Checkpoint: time of day, weather, pedestrian density, time limit, and laps = 1;
Circuit: the same plus the file's laps; Blitz: the same without laps. Cruise: pedestrian density
0.25, time of day 1, weather 0. Cops and Robbers sets nothing: the time of day, weather and
pedestrian density in effect stay (those of the mode or race picked before, or the host's own
choices). The files' traffic, police and opponent values are ignored in
multiplayer (1.4). All shipped values fit the packing (check below).

The host also sets a session password (optional) and the maximum player count.

### 1.3 Joining, starting, leaving

1. **Join.** A machine may join if the session is not sealed (step 4), the host's city is
   installed (otherwise MM2 refuses: "race locale not installed", texts 84–86, 403–407) and the
   game versions agree. It publishes its record (the team defaults by the free-for-all rule of
   5.3) and reads the settings.

   **Car choice and locks.** A player picks its car and paint on its own machine under its own
   driver's locks (`progression.md`, "Locked cars and paint jobs"): the network car screen
   treats locks as the single-player car screen does (`menus.md`; leaving it with a locked car is
   refused). The locks are worked out again, and the record published again, at these moments:
   - entering multiplayer, and entering the lobby screen in any mode but Cops and Robbers: the
     driver's locks; a chosen car that is now locked becomes `vpbug` with paint 0, else a locked
     paint becomes paint 0;
   - entering the lobby screen in Cops and Robbers, or receiving settings for it: sub-mode 1
     locks every car except the team's (cops `vpcop`, robbers `vpmustang99`), which becomes the
     car with paint 0; sub-modes 0 and 2 apply the driver's locks and keep the car;
   - receiving settings that leave Cops and Robbers: the driver's locks, car kept.

   Other machines never apply their own locks to a remote player's car: it is built from the
   record's car name and paint.
   A name that matches no car of the receiving machine's car list (base names compared exactly,
   case included) is built as `vpcoop`, MM2's default car, with the sent paint (`set = paint mod
   sets`, `../formats/car-model.md`); the host's car check for that player (3.7) then uses
   `vpcoop`'s value, so unless the player really drives `vpcoop` the host marks it a cheater.
2. **Ready.** Each non-host player toggles "ready" (sent to all). The host can start when every
   other player is ready (and, in a lobby-launched game, at least 2 players are present).
3. **Start.** The host assigns **start slots**: it clears a table of 10 slots and, going through
   the players in the session's player order, gives each the first empty slot (so the host's own
   slot is wherever the host appears in that order). It sends "start" with the slot table; every
   machine (the host too) loads the city and the mode.
4. **Sealing.** At the start of a race mode (1, 3, 4) the host **seals** the session: nobody can
   join until it is unsealed. Cruise and Cops and Robbers stay open, so players can **join a
   running game** (5.1, 5.3). Cops and Robbers seals when its limit ends the game and unseals when
   it restarts. "Return to lobby" unseals.
5. **Leaving.** A player who quits sends "player leaves" (the host sends "host quits", below) and
   leaves the session. Every other machine, on the transport's "player left" notice, removes the
   player from its roster and map, hides and frees its remote car, and shows "*name*" + text 38
   (in Cruise and the race modes only while racing; before the race start it instead lowers the
   count of players being waited for, 5.2). A machine told that **it** has left (kicked) shows
   text 44 and quits to the menus.
6. **Host leaves.** A host that quits sends "host quits" first; every peer shows "the host has
   quit" (Cruise text 157, Checkpoint 151, Circuit 109, Blitz 98, Cops and Robbers 132) and
   quits. If the host disappears without that event the transport makes another peer host: in
   Cops and Robbers every peer then shows text 40, sends "host quits" and quits; in the other
   modes the new host marks itself host in its record and the game continues.
7. **Return to lobby.** The host can send "return to lobby": everyone shows text 45, clears its
   "in game" flag and goes back to the lobby with the session kept.
8. **Kick.** The host can remove a player (MM2: "boot"); the kicked player sees text 44.

### 1.4 What a multiplayer world contains

Set up by the same city and mode loading as single player, with these differences:

- **No traffic, no police, no race opponents.** Before the world is built the traffic density,
  police density and opponent count are forced to 0 (not restored afterwards), so the traffic pool
  is empty (`../ai/traffic-population.md`), no cop is created (`../ai/police.md`, "How many cops")
  and no opponent (`../ai/opponents.md`).
- **Pedestrians** stay, with the session's density. They are **not** shared: every machine places
  and moves its own (pedestrian spec, milestone 6).
- **No cable cars and no network trains**: both switches are off while the world is built (so none
  is created, `../ai/police.md` "The cop's car" mentions this) and back on afterwards.
- Gizmos (path sets, `../formats/pathset.md`): sailboats, drawbridges and path-set trains load as
  in single player (with the mode's race name: `roam` for Cruise, `multicop` for Cops and Robbers,
  `race<N>`, `circuit<N>`, `blitz<N>`). **Ferries are never loaded** in multiplayer. **Parked cars**
  load only in the three race modes. In **London** every drawbridge is switched to its "open"
  mode (the mode its path name prefix `open:` gives, milestone 5) whatever its path name says.
  Nothing here is shared: each machine runs the same deterministic gizmos.
- Traffic lights run on each machine's own clock (`../ai/traffic-control.md`); not shared.
- A **Circuit** race in multiplayer makes the concrete barricades sturdier: in the prop data of
  `sp_barricadeconcl_f` and `sp_barricadeconcr_f` (`tune/banger/*.dgBangerData`) `Mass` and
  `ImpulseLimit2` are multiplied by 26 (once per game, after loading).
- **Trailers**: in multiplayer Cruise and Cops and Robbers **no car has a trailer** (neither the
  local car nor remote cars; `vpsemi`, `vpftruck`, `vplafrance` drive without one); in the race
  modes trailers are attached as in single player (`../formats/car-model.md`, "Trailers").
- **Remote cars** (section 3): one full car per other player, set up like the player's car but
  with collision identity id + 1000 (the local car's is 0), the remote player's car and paint. A
  remote `vpcop` takes its handling from `vpmustang99` like the player's own
  (`../physics/vehicle-tune.md`, "Which files").

### 1.5 Rules for every mode

- **No pause.** In a network game the menu key opens the menu but the game keeps running, and
  losing the window focus does not pause.
- **Throttle cap.** In a network game, when the local car's gear is a forward gear (not reverse,
  not neutral), the throttle the player's input asks for is limited before it reaches the car:
  *t* ≥ 0 → min(*t*, *cap*); *t* < 0 → 0. *cap* is 1.0 except while the local car carries the
  Cops and Robbers gold (5.3). This sits where `../engine/controls.md` applies the throttle input
  (single player: no cap). Brake and steering are not affected.
- **Wreck penalty.** If damage is on (the session's "no damage" bit is 0) and the local car's
  damage exceeds its maximum, the mode (while racing / playing) shows its "5 second penalty" text,
  makes the car undrivable, sends "wrecked" (receivers do nothing with it; the undrivable state
  reaches them through the car state, 3.1 flag 0x80) and waits **5.0 s** (Cops and Robbers: also
  drops the gold, 5.3). Then: damage cleared (`../physics/damage.md`, repair), drivable again,
  "damage repaired" sent (every receiver clears that remote car's damage visuals). The car stays
  where it is.
- **Water and falling through the city** (`../physics/damage.md` / city handlers): the local car
  is reset (race modes: at the last gate it passed, below; Cruise: its reset point; Cops and
  Robbers: 5.3), its race progress kept, and **"car reset"** is sent with the car's new matrix;
  receivers reset that remote car (vehicle reset) and snap it to the matrix (3.4 "Reset"). Race
  modes put the car at the position of the gate the car hit last (gate 0 before any) with heading
  = −(that gate's angle) × π/180 (gates: race-modes spec); the race's gate state is reset as on a
  car reset there.
- **HUD map marks** (`hudmap.md`, car markers): the local car as in single player; each remote
  car in the colour of its slot, whatever the mode or team: slot 0 (239, 0, 0), 1 (255, 255, 0),
  2 (255, 90, 0), 3 (180, 0, 255), 4 (0, 255, 255), 5 (255, 3, 144). MM2 reads slots 6 and 7
  past the end of its colour table (bytes that give (240, 81, 236) with alpha 64 and
  (100, 117, 104) with alpha 109; all the others have alpha 255); use those. A remote car's mark is drawn only while it is present and shown (race modes hide a
  finished player's, 5.2).
- **Chat.** Text typed by a player goes to everyone; receivers show "*name*: *text*" with a
  notice sound. Lines starting with `/wav` are sent but not shown; lines starting with `/rc` are
  not sent (MM2's developer commands; ignore).

## 2. Start positions

### 2.1 Network respawn (Cruise and Cops and Robbers)

Cruise and Cops and Robbers start (and restart) each local car at a random intersection with the
free-roam rule of `../engine/controls.md` ("Free-roam start", steps 1–4, including the rejection
of enclosed rooms and of roads with flag 0x02 or 0x04), with one difference that makes every
player start somewhere else, reproducibly:

1. MM2 keeps a **respawn counter** *k* (0 … 99), shared by all network respawns on the machine for
   the life of the program (it starts at 0 when the game starts and is never reset).
2. For the draw the game-wide generator is set aside: a second generator state is used, seeded
   with the player's id.
3. Each attempt draws *k* + 1 numbers and uses the last one for the intersection index; a rejected
   attempt draws *k* + 1 more, and so on.
4. Afterwards the second state is kept for next time (it is reseeded anyway), the game-wide state
   is restored untouched, and *k* = (*k* + 1) mod 100.

Position = intersection + (0, 2, 0), heading 0. (A start of exactly (0, 0, 0) — impossible with
the shipped cities — would fall back to grid slot positions around (0, 0, 0).)

### 2.2 Race grid

In the race modes the start point S and heading θ are the race's (`race<N>waypoints.csv` etc.,
race-modes spec: start position and start angle, θ = −angle° × π/180). Each car goes to the grid
position of its start slot. In the car's frame (x to the car's right, z backwards, so −z is the
driving direction) the offsets are:

| Slot | Normal (x, z) | Wide (x, z) |
|---|---|---|
| 0 | (2.25, 6) | (2.75, 16) |
| 1 | (−2.25, 6) | (−2.75, 16) |
| 2 | (4.5, 0) | (5.5, 16) |
| 3 | (0, 0) | (0, 16) |
| 4 | (−4.5, 0) | (2.75, 34) |
| 5 | (4.5, −6) | (−2.75, 34) |
| 6 | (0, −6) | (0, 34) |
| 7 | (−4.5, −6) | (5.5, 34) |
| 8, 9 | (0, 0) | (0, 0) |

The **wide** column is used for a car whose bound radius (`../formats/bnd.md`) is greater than 6.0
or that has a trailer; otherwise the normal one. The world position is S + x·X + z·Z with X = (cos θ,
0, −sin θ) and Z = (sin θ, 0, cos θ) (y = S.y). The position is then dropped to the ground: probe
from 7.5 m above it to 15 m below (`../physics/collision.md`, "The query", object mask 0x20); on a
hit the hit point replaces it, otherwise it stays. The car is placed there (vehicle placement rule)
with heading θ, not drivable, damage off, until "Go" (5.2).

(MM2's layout is not symmetric — slots 0 and 1 start 6 m behind the line, 5–7 6 m ahead, and in
the wide layout slots 0, 2 and 3 are only 2.75 m apart. Reproduce it.)

Cruise and Cops and Robbers place **remote** cars at placeholder positions (grid slots around the
origin; (0, 50, 0) in Cops and Robbers); their first car state moves them (3.4).

## 3. Remote cars

### 3.1 The car state

Each machine sends its **local car's state** to the others (unguaranteed). Contents, in MM2's
quantisation (keep it: the receiver's behaviour depends on the rounding):

| Field | Encoding |
|---|---|
| host clock | the host's mode timer value (5.1, 5.2); 0 from non-hosts |
| steering | trunc(*s* × 127), signed byte (*s* = the car's current steering, `../physics/vehicle.md`) |
| throttle, brake | trunc(*t* × 255), trunc(*b* × 255), unsigned bytes |
| gear | the current gear number (only meaningful with flag 0x40) |
| orientation | three angles of the body matrix (rows X, Y, Z): *a* = atan2(Y.z, Z.z), *b* = asin(−X.z), *c* = atan2(X.y, X.x) |
| position | body position (3 floats) |
| acceleration slot | length *L*ₐ = \|v\| and direction trunc(v/\|v\| × 127) per axis (signed bytes) — **MM2 puts the car's velocity here, not an acceleration** |
| velocity | the same encoding of the same velocity v |
| angular velocity | \|ω\| and trunc(ω/\|ω\| × 127) per axis |
| damage | the damage value (float) |
| score | 16 bits: race progress (number of gates passed) or Cops and Robbers points |
| flags | 16 bits, below |
| sequence | a counter the sender increments before every car state it sends (one counter for all recipients) |

(For a velocity of zero the direction is 0/0: MM2 sends whatever the conversion gives; send 0.)

Flags: 0x02 horn sounding; 0x04 gear is reverse; 0x08 siren on (police car); 0x20 handbrake
input > 0.8; 0x40 manual gearbox (the gear field is valid); 0x80 car not drivable; 0x100 **reset**
(sent in the first state after a race restart, 5.2). (Bit values as MM2's; ours may differ.)

### 3.2 When it is sent

Every frame, after the incoming messages have been handled:

- normally, per remote player *j*, a timer τⱼ: if τⱼ > *I*ⱼ the state is sent **to player *j***
  and τⱼ = 0, otherwise τⱼ = τⱼ + dt. *I*ⱼ = **0.05 s**, or **0.1 s** if player *j*'s record says
  "slow link". (So the real period is the first whole number of frames whose time exceeds *I*ⱼ:
  at 60 frames per second every 4th frame, at 30 every 3rd.)
- if the **local** player chose "slow link", one copy to everyone instead, with the same rule and
  *I* = 0.1 s.

States are sent only while in a session and not while the machine is quitting. A send that fails marks that player as unreachable and no more states go to it.

### 3.3 Receiving a car state

For the remote car of the sender (ignore states from unknown players and from oneself):

1. **Order.** If its sequence number is smaller than the last one accepted from that sender,
   drop it.
2. Decode: matrix M from the three angles (X = (cos c·cos b, sin c·cos b, −sin b); Y = (cos c·sin
   b·sin a − sin c·cos a, cos c·cos a + sin c·sin b·sin a, cos b·sin a); Z = (sin c·sin a + cos c·
   sin b·cos a, sin c·sin b·cos a − cos c·sin a, cos b·cos a)) and the position p; A = *L*ₐ ×
   byte/127 per axis (the acceleration slot), V = *L*ᵥ × byte/127, W = *L*_ω × byte/127.
3. If this is the first state ever accepted from that sender: the remote car's body matrix := (M, p).
4. The remote body's velocity := V and its momentum := mass × V (angular velocity is not set).
5. Inputs: steering := byte/127, brake := byte × 0.003921569, throttle := byte × 0.003921569.
6. Damage := the sent value; if the old value was > 1 and the new one < 0.0001, clear the car's
   damage visuals.
7. **Packet interval.** Δ = (network clock now) − (network clock at the previous state from this
   sender; 0 before the first). Keep the last 10 Δ values (all start at **10.0**), T = their mean
   (sum × 0.1).
8. **Target.** p* = p + T·V + 0.5·A·T² (per axis: (p + T·V) + 0.5·A·T²); the target orientation M*
   = M rotated about the unit axis W/\|W\| by the angle T·\|W\| (right-handed), only if \|W\| > 1e−6,
   else M* = M.
9. **Drift test** (not for a reset state): d = distance from the remote car's current position to p
   (taken as 0 when below 2.0). If d ≤ 10: the car is **off course** when its bound radius < d
   (else not). If d > 10: the off-course mark keeps its previous value. (MM2 also writes (M, p)
   into the car here when d > 10, but step 10 immediately puts the old position back, so it has no
   effect.)
10. **Orientation snap** (not for a reset state): the car's body matrix := (M, current position) —
    the orientation jumps to the sent one, the position stays.
11. **Reset state** (flag 0x100): body matrix := (M, p), target := (M, p), curve cleared, not off
    course.
12. **Curve.** A cubic Hermite curve from P0 = the car's current position to P1 = p*, with
    tangents T0 = the car's Z row normalised and T1 = the Z row of M* normalised (each times 1.0):
    q(s) = a·s³ + b·s² + c·s + d with a = 2P0 − 2P1 + T0 + T1, b = −3P0 + 3P1 − 2T0 − T1, c = T0,
    d = P0.
13. Score, host clock and flags are taken: horn on/off, siren on/off (starting or stopping its
    sound), reverse (flag set and gear not reverse → reverse; flag clear and gear reverse → first
    gear), handbrake := 1.0 or 0.0, gearbox manual with the sent gear or automatic, drivable
    state from flag 0x80 (on a change only, by the same switch as a local car's: not drivable →
    neutral and brake 1 at each physics start, `../physics/damage.md`; drivable again → first
    gear if in neutral or reverse).
14. **Frame counts.** *F*total += *F*since; *F*since = 0; *F*avg = *F*total ÷ (sequence number),
    whole-number division. (Because the sender numbers all its states, also those sent to other
    players, *F*avg is the frames per *any* state of that sender, smaller than the frames per state
    received when more than two play. MM2 does this; reproduce.)

If a non-host receives a positive host clock it copies it into its own copy of the host's mode
timer value (5.1).

### 3.4 Every frame, per remote car

1. If no state arrived from it this frame, *F*since += 1.
2. It is declared a **mover** with all four switches (U C V O, `../physics/car-collision.md`,
   1.1), and so is its trailer; in the declaration order they come after the AI's movers, in slot
   order. So a remote car runs the **full vehicle physics** every sample with the last received
   inputs and collides with the local car, the city and everything else exactly as a local car.
3. Smoothing, before the physics: s = *F*since / *F*avg as a float, clamped to 0 … 1 (if *F*avg =
   0: s = 0 when *F*since = 0, else 1). Body matrix := interpolate(current, target, dt): rotation by
   spherical interpolation of the two orientations as unit quaternions (taking the shorter way) by
   the fraction dt, position current + (target − current)·dt. If the car is **off course** its
   position is instead q(s).
4. If the car is in room 0 (outside the city) its room is searched again.

Consequences worth knowing (all MM2's): at the start T is large (the ten initial 10.0 values), so
targets are far ahead for the first second; a car more than 10 m off is not snapped, only pulled
back by the per-frame blend (time constant about 1 s); collisions with a remote car are resolved
on each machine separately, and the remote car's owner corrects it with its next states. Nothing
about collisions is sent.

**Reset event** ("car reset", 1.5): reset the remote car as a vehicle, set its matrix and target to
the sent matrix, clear the curve and the off-course mark.

### 3.5 Who decides what

- Each player's machine is the **only authority for its own car**: position, damage, wreck,
  inputs, its race progress and its finish.
- The **host** decides the shared events: start of the race countdown, race results and end, the
  Cops and Robbers gold (who has it, where it is, where it and the bases go), the Cops and Robbers
  clock and end.
- Everything else (pedestrians, gizmos, lights, props hit, particles) is local to each machine.

### 3.6 Clock

Each machine has a network clock (seconds). A non-host asks the host for the time once, when it
first learns that the host is in the game (the host's record with "in game" set, so in practice
only in Cruise and Cops and Robbers, or any changed record of the host): it sends "time request"
and notes its clock; the host answers "time reply" (carrying the host's clock at sending). If the round trip is
more than 0.5 s the request is repeated; otherwise the clock := round trip + the host's sent clock
(the full round trip, not half). The network clock is used only for the packet interval T (3.3) and
time stamps.

### 3.7 Cheaters

Each car has a **check value**: the check value of `../formats/profiles.md` ("Check value"),
computed over the bytes of `tune/vehicle/<car>.vehcarsim` read as text lines (every byte of the
file in order; a line is cut at a NUL byte if one occurs). A player driving `vpcop` uses the value
of `vpmustang99` (the handling it really drives).

When the cheat check is on (always), the **host**, receiving a "loaded" or "joined" event (5.1,
5.2) that carries the sender's check value, compares it with its own value for the sender's car:
if they differ it shows "*name*" + text 41 (loaded) or 43 (joined), marks the player as a cheater
and sends "cheater" with the player's id. Every receiver marks that player (a cheater who is the
local player shows text 46, others "*name*" + text 47). Effects of a mark: the host ignores a
marked player's "finished" reports, peers ignore "result" events if the host itself is marked,
and a machine whose own player is marked keeps its car undrivable for the rest of the game.

## 4. Events

All guaranteed. "All" = every other peer.

| Event | From → to | Contents | Section |
|---|---|---|---|
| settings changed | host → all (lobby) | the settings of 1.2 | 1.2 |
| ready | player → all (lobby) | ready flag | 1.3 |
| start | host → all | start slot table (10 ids) | 1.3 |
| car state | each → each (unguaranteed) | 3.1 | 3 |
| loaded | each → all | car check value | 5.2, 3.7 |
| reloaded | each → all | — (instead of "loaded" after a restart) | 5.2 |
| joined | joiner → all | car check value | 5.1, 5.3, 3.7 |
| race start | host → all | — | 5.2 |
| finished | non-host → host | finish time | 5.2 |
| result | host → all | player id, finish time | 5.2 |
| race over | host → all | — | 5.2 |
| time up | host → all | — | 5.2 |
| restart | host → all | — | 5.2, 5.3 |
| wrecked / damage repaired | each → all | — | 1.5 |
| car reset | each → all | body matrix | 1.5 |
| chat | each → all | text | 1.5 |
| cheater | host → all | player id | 3.7 |
| time request / reply | non-host → host → requester | host clock | 3.6 |
| host quits / return to lobby / kick | host → all / one | — | 1.3 |
| gold request | non-host → host | — | 5.3 |
| gold carrier | host → all | player id | 5.3 |
| gold dropped | dropper → all | gold position | 5.3 |
| gold delivered | deliverer → all | — | 5.3 |
| gold and bases placed | host → all | gold, bank, hideout positions | 5.3 |
| team / state request | C&R joiner → all | — | 5.3 |
| state reply | host → joiner | carrier (flag, id), gold, bank, hideout positions | 5.3 |
| minutes left | host → all | warning number 0–4 | 5.3 |
| game over | host → all | reason (1 time, 2 points) | 5.3 |

## 5. The modes

### 5.1 Cruise

Free driving together, no goal and no end. Music data `singleroam` (music spec).

1. Start: network respawn (2.1). The game sets "in game" in the player record and sends
   "joined" (with the check value). Remote cars of players already in the game are shown; others
   appear when their "joined" arrives ("*name*" + text 42 on the receivers).
2. First frame: if the session's time limit is > 0 a clock starts (it ends nothing; MM2 has a
   "time up" message for Cruise — text 156, then quit after 5 s — that no machine ever sends).
3. Second frame: start sound, "Go" (text 154, 1.25 s), every car drivable, damage on unless the
   session says "no damage".
4. Playing: wreck penalty (1.5; text 155). Water: car back to its reset point (its last network
   respawn), "car reset" sent.
5. A player who joins later appears at a random intersection of the receiver's choosing until its
   first car state arrives (3.3 step 3 moves it).
6. The host's mode clock value rides in its car states (3.1); clients copy it.

### 5.2 Checkpoint, Circuit and Blitz races

The single-player race rules (race-modes spec) with these multiplayer rules. Music `singlerace`.
Texts:

| Notice | Checkpoint | Circuit | Blitz |
|---|---|---|---|
| ready, set, go | 144, 145, 146 | 101, 102, 103 | 89, 90, 91 |
| 5 second penalty | 147 | 104 | 92 |
| "finished in" — host's own finish | 148 | 105 | 93 |
| "finished in" — own finish, from "result" | 149 | 106 | 95 |
| "finished in" — another player, from "result" | 150 | 107 | 96 |
| "finished in" — another player's report, at the host | 152 | 110 | 99 |
| race over — host's window ran out / own countdown ran out (Blitz) | 143 | 100 | 94 |
| race over / time's up — on "time up" | 153 | 111 | 97 |
| the host has quit | 151 | 109 | 98 |

**Waiting.** Every machine, after its game has loaded, waits **5 s** and then sends "loaded"
(after a restart: "reloaded"). Each machine counts the players it waits for, starting at (players
in the session − 1), lowered by every "loaded"/"reloaded" and by every player who leaves before
the start. Each frame of the wait, in this order: until its own "loaded" has gone out, a machine
only runs its 5 s timer (timer += dt while it is ≤ 5.0; in the first frame it is above, it sends
"loaded" and does nothing else that frame); after that, while its count is above 0 it shows
"waiting for *n* players" (texts 31–37 for *n* = 1–7); once the count is 0 or less the host sends
"race start" and starts its countdown, and the others start theirs when "race start" arrives. So
the host's "race start" goes out at the earliest in the frame after its own "loaded", even if
everyone else loaded first. A "loaded" also makes the sender's remote car appear: it is dropped
to the ground (probe 7.5 m up to 15 m down, as in 2.2) at its current position, reset and shown.
(A player who never loads keeps everyone waiting; MM2 has no timeout.)

**Countdown.** 1.25 s "ready", 1.25 s "set" (two start sounds), then "go": the race timers start —
among them the race clock, counting up from 0, which every finish time is read from (it starts
whatever the time limit) — the local car becomes drivable, and damage is switched on for the local
and the remote cars (unless "no damage"). Blitz also starts its countdown at the session time
limit. In Checkpoint and Circuit the time limit has no effect (MM2 starts a countdown clock in
Checkpoint when the limit is > 0, but that clock holds 0 and stops at once).

**Racing.** Standings each frame (after the network update; not once the results are up), as
`results.md`, "Standings", with
the remote cars as the opponents: a remote car's count is its score field, the local car's is its
waypoint list's count, distances as there. A remote slot's player has **finished** once a result
for it has been entered (clients: its "result" arrived; host: its "finished" arrived); the marks are
cleared at a restart. Present = the slot has a shown remote car.
1. **Local place** = 1 + the number of remote slots that are present and (finished, or have a
   larger count, or an equal count and are strictly closer to the local car's **target** waypoint
   than the local car), + the number of slots not present whose player finished. The HUD place
   display gets (local place, 1 + present slots + absent finished slots) every frame until the local
   car finishes; after that it is no longer updated (it keeps the last values).
2. **Each present remote car's place** (its icon number): if its player finished, its icon and its
   HUD map mark are hidden. Else 1 + the other remote slots ahead of it by the rule of step 1 (the
   distance to waypoint (its count mod *n*)) + 1 if the local car has a larger count, has finished,
   or has an equal count and is strictly closer to the local car's target than this car.

Wreck penalty and water rules of 1.5.

**Finishing, Checkpoint and Circuit.**
1. A car that completes the race stops its clock, gets the post-race camera and enters the
   **slow-down** for 3.0 s: each frame throttle := throttle − (remaining time × 0.2), brake := 1;
   at the end the car is stopped (steering 0, brake 1, not drivable).
2. A non-host sends "finished" with its race time. The host — for its own finish directly, for
   others on "finished" (ignored from a marked cheater) — shows "*name* finished in *time*",
   inserts (id, time) into the **result table** and sends "result" (id, time) to everyone.
   Clients insert results only from "result"; their own "result" shows their finished text and
   restarts the 3 s slow-down (also when the first one has ended: the car, still not drivable,
   gets the throttle reduction and brake 1 again for 3 s and is then stopped again).
3. Result table: kept sorted by time, ascending; a player already in it is not added again.
4. **Finish window** (wait-for-all on): when the first result enters the host's table the host
   starts a 60 s window (clients run a copy that does nothing). When it ends the host sends "time
   up" and, if its own car has not finished, stops it, shows "race over" and enters its own time
   as **86400**; every client whose car has not finished stops it, shows its "race over" text and
   sends "finished" with 86400. 86400 means "did not finish".
5. When the result table holds as many entries as there are players, the host sends "race over",
   stops its car and shows the results; a client in its slow-down shows the results on "race
   over". With wait-for-all off each machine shows the results when its own slow-down ends.
6. Results list: places in table order, 86400 shown as "did not finish".

**Finishing, Blitz.** No finish window and no "race over" event:
1. A car that completes the race stops its clocks and starts two timers: the slow-down *S* = 3 s
   and the results timer *R* = the time that was left on its countdown. Each frame: if *S* ≤ 0
   the car is stopped (steering 0, brake 1, not drivable); then if *R* ≤ 0 the results are shown
   and the car is stopped; otherwise throttle := throttle − *R* × 0.2 (the results timer, not
   *S*), brake := 1, and both timers go down by the frame time. So the host sees the results
   when the time limit expires.
2. Finish reports, results and the result table as in step 2 of Checkpoint, except that a
   client's own "result" sets *R* := 3 s (its results come 3 s after it arrives) and does not
   restart *S*.
3. When a car's countdown reaches 0 before it finished: "time's up" (text 94), the finish line
   is switched off, the clocks stop, and it reports 86400 (a client sends "finished"; the host
   sends "time up", then "result" with its own id and 86400, and enters that into its table);
   then *R* = *S* = 5 s with the frame rule of step 1 (results after 5 s, or 3 s after a client's
   own "result"). A client receiving "time up" before it finished shows text 97, stops its
   countdown, sends "finished" with 86400 and sets *R* := 5 s, *S* keeping its value (60 s before
   any finish since the game was created, else ≤ 0, so the car is stopped at once), then the frame
   rule of step 1.

**Restart** (host, from the results): results and finish marks cleared, timers stopped, every car
undrivable with damage off, "restart" sent, the race reset to waiting, and one car state with the
**reset** flag sent to everyone (receivers snap the car, 3.3 step 11). Clients do the same on
"restart". Then waiting again (with "reloaded").

### 5.3 Cops and Robbers (the gold)

One gold bar lies somewhere in the city. Whoever touches it carries it (it weighs the car down) and
must drive it to their team's base; the gold is knocked loose by hits from other players' cars and
lost in a wreck. Points for picking it up and for delivering it; the game ends at a time or points
limit chosen by the host, or never (limit "none"). Race name `multicop` (no `.aimap` ships for it;
`../formats/aimap.md` defaults). Music `singleroam`. Damage regenerates (below).

**Sub-modes and teams** (team 0 / team 1):

| Sub-mode | Teams | Car | Bases (team 0 → team 1) |
|---|---|---|---|
| 0 free-for-all | team 0 = drivers of a car whose `.info` `Flags` has bit 8 (police car: `vpcop`), team 1 = everyone else; set automatically from the car | any | bank → hideout |
| 1 cops and robbers | chosen in the lobby: 0 cops, 1 robbers | forced: cops `vpcop`, robbers `vpmustang99` (all other cars locked) | bank → hideout |
| 2 robber teams | chosen in the lobby: 0 blue, 1 red | any | blue base → red base |

In sub-modes 1 and 2 the icon above a team 1 player's car is red (0xFFEF0000) and above a team 0
player's blue (0xFF0000EF); in free-for-all players have no team colour. The HUD map does not use
team colours (its marks are by slot, 1.5). Team scores (sum of the members' points,
including one's own) are shown in sub-modes 1 and 2.

**Objects** (waypoint objects of the race-modes spec, drawn with the given models):

| Object | Model | Radius | Drawn at |
|---|---|---|---|
| gold | `wpobj_gold` | 5.0 | its point + (0, 1.5, 0), spinning about +Y at 3 rad/s (angle = 3 × game time) |
| team 1 base: hideout (sub-modes 0, 1) / red (2) | `pt_hideout` / `pt_red` | 12.0 | point + (0, 3.75, 0) |
| team 0 base: bank (0, 1) / blue (2) | `pt_bank` / `pt_blue` | 12.0 | point + (0, 3.75, 0) |

An object's **room** is the room containing its drawn point. No object is ever turned by a spot's
angle: the positions file's `a` is not used, and the heading stays 0.

**Drawing.** The **gold** is drawn at its model's own size (no scale), lit, turned about +Y by
3 × game time at its drawn point. A **base** is a camera-facing billboard: its rotation is the
camera's own (the model's X, Y and Z axes are the camera's X, Y and Z axes, pitch and roll
included), the model is scaled by 12 along X, 7.5 along Y and 12 along Z (radius, height, radius)
before that rotation, and it is placed at its drawn point; unlit. Neither changes face culling
(the world's back-face culling applies); a billboard always shows the camera the model's +Z side.
The base models are 2 × 1 × 0.05 m boards (so 24 m wide and 7.5 m tall, the bottom on the spot);
the gold bar is about 1.2 × 0.4 × 1.6 m.

**Spots.** `race/<c>/multicopwaypoints.csv` is read as a positions file: the first line is a
header and skipped, each further line `x,y,z,a,…` (the four numbers, then four integers and a
text, unused); at most 50 rows are kept; with fewer than 3 the list is empty. A **random spot**
is chosen like this (by the host only):
1. If the list is empty: a random intersection by the free-roam rule but **without** the enclosed
   room and road flag rejections (only deep water and landmark rooms are rejected), using the
   game-wide generator, each attempt drawing *k* + 1 numbers and using the last (*k* = the
   respawn counter of 2.1, not advanced here); the spot is the intersection position itself.
2. Otherwise a coin flip, then (tails) a row, each from a **temporary generator**: the game-wide
   state is set aside, the state is seeded with trunc(*s* × *id*) — *s* = the seconds of real time
   since MM2's frame mark (taken at the end of every frame's game update and at a game reset),
   *id* = the host's own player id — one number
   is drawn (the generator's 15-bit output) and the game-wide state is restored. Odd → heads → as
   in 1; even → tails → reseed the same way (a moment later), draw *x*, row *r* = *x* mod (*n* − 1)
   of the list — the **last row is never chosen**. Neither draw touches the game-wide generator.
   (The seed is a timing accident; any seed from a fast clock plays the same.)

Nothing keeps a spot off a drawbridge: with London's leaves all raised in network games (1.4) an
intersection at a leaf can be chosen (e.g. at Tower Bridge's hinge, (1247.1, 8.0, −148.5)), and
the gold then stays there until someone reaches it, as in MM2. Keep it.

(MM2 also looks for `race/<c>/multicopsets.csv`, but its reader keeps no rows, so the file is
never used. Ignore it.)

**Placing gold and bases** (host; at the start and after every delivery): gold spot G, hideout
spot H ≠ G, bank spot B ≠ H and ≠ G (each redrawn until different; "different" = not equal in all
three coordinates). The gold's **home** := G. All three become visible with nobody carrying the
gold and the arrow (HUD) points at the gold. After a delivery the host sends "gold and bases
placed" (receivers set the three positions, nobody carries, gold visible); at the start the
positions reach the others by "state reply".

**Start.**
1. Every machine: network respawn (2.1); "in game" set; regeneration on; sends "team / state
   request". Free-for-all: the team is set from the car (above) and published.
2. On "team / state request" every machine records the sender's team (colour); the host also
   answers "state reply" with the carrier (if any), gold, bank and hideout positions.
3. The host places gold and bases and starts at once; a non-host waits until "state reply": it
   takes the positions (and, if someone carries the gold, that player picks it up, below), sends
   "joined", and starts. The same works for a player joining a running game.
4. Then: start sound, "go" (text 113), cars drivable, damage on. With a time limit the **host's**
   countdown starts at limit × 60 s (only the host counts down; others learn the end by event).

**Picking up** (each frame, on each machine, for the local car only, while nobody carries the
gold and the local car is not in a wreck or knock-out pause): the car is in the gold's room and
its distance to the gold point (3D) is less than 5.0:
- the **host** takes it at once if at least one other player's car is present (loaded or joined): text 115, the local
  car carries the gold (below), the gold is hidden on this machine, arrow to the own team's base,
  **+25 points**, and the host sends "gold carrier" with its own id;
- a **non-host** hides the gold locally and sends "gold request" (only after its game has
  started).
On "gold request" the host, if nobody carries the gold, sends "gold carrier" with the requester's
id, shows "*name*" + text 133 and lets that player's remote car carry it. On "gold carrier": if it
names the local player: text 134, carry, gold hidden, arrow to own base, +25 points; otherwise
"*name*" + text 135, that remote car carries it. The first request the host sees wins; the others
get nothing (their gold is shown again by the next "gold carrier", "dropped" or "placed").

**Carrying.** While a car carries the gold:
- its body's mass is increased by the gold mass (0, 100 or 200): `m` := `m` + gold and `1/m`
  recomputed (`../physics/vehicle-body.md`, "State"); the moments of inertia, the momenta and the
  velocities are left as they are (the inertia stays that of the car alone). When the gold leaves,
  `m` := `m` − gold the same way;
- if it is the local car, the throttle cap (1.5) is 0.9 for mass 100 and 0.81 for mass 200 (1.0
  for weightless);
- the local car's damage does **not** regenerate;
- on every machine where the gold is shown, it floats: each frame its point := the carrier's
  body position + (0, 2, 0) (so it is drawn 3.5 m above the carrier's centre of mass).

**Losing the gold.** The carrier's own machine decides:
1. **Knocked loose**: the carrier's damage counts an impact (`../physics/damage.md`, "Per impact
   report": the counted step) from another player's car (identity above 999) whose slot's total
   strength is at least **250**. Text 112; then the drop below at the car's body position (its
   centre of mass); then a **2 s** pause in which this car cannot pick up the gold.
2. **Wreck**: the wreck penalty of 1.5 (text 114), with the drop at the car's body position; the
   5 s pause also blocks picking up.
3. **Water or falling through**: the gold returns **home** (forced); the car is reset to its
   start point (its points kept) and "car reset" is sent.
4. **Leaving**: when the carrier leaves the session, the host drops the gold where it floats (its
   point as last set: that remote car's body position + (0, 2, 0)).

**Drop** at point P: nobody carries; the carrier's mass and cap are restored;
damage regeneration back on; the gold goes **home** if forced or if the room at P is deep water
(`../formats/psdl.md`, "Deep water"), else to P (MM2 also asks the road network for the component
at P, but every answer is accepted). The point is dropped to the ground: probe from 2 m above to
10 m below (object mask 0x20); a hit replaces it. The gold is shown there, the HUD gold mark is
off, the arrow points at the gold, and "gold dropped" with the point goes to everyone. Receivers:
"*name*" + text 136, the former carrier's remote car loses the mass, nobody carries, gold shown at
the point.

**Delivering.** Each frame the carrier (local car) checks its **own team's base**: team 1 the
hideout (red), team 0 the bank (blue). Delivered when (the base's room is an enclosed
room — PSDL flag 0x02, `../formats/psdl.md` — or the base's room is the car's room) and the
distance from the car to the base point is less than 12.0: text 116 (team 1) or 117 (team 0),
regeneration on, the car's damage is **fully repaired**, the mass and cap are restored, nobody
carries, **+100 points**, "gold delivered" sent.
Receivers show "*name*" + text 137, repair that remote car, remove its mass, nobody carries. The
host (whether it delivered or received the event) places new gold and bases.

So free-for-all is everyone against everyone, but a police-car driver delivers to the bank while
all others deliver to the hideout.

**Points** are the local car's score (sent in every car state). Pickup +25, delivery +100. The
host counts **scoring events** it has seen (its own +25/+100, and every remote pickup it
registers); the point limit for a remote player or a team is only applied once this count is
above 0.

**Regeneration** (only in this mode): every frame, if the local car moves faster than 5 m/s (speed²
> 25) and its damage is at least 0.01 and it is not carrying the gold, damage := damage −
0.0005 × MaxDamage; if that reaches 0 or less the damage is cleared completely (visuals too).

**End** (host, every frame while playing):
- time limit: when its countdown is below 0.1 s: text 118, session sealed, "game over" (1);
- point limit, free-for-all: if the host's points ≥ limit → text 119; else if some remote player's
  points ≥ limit (and scoring events > 0) → text 120; then sealed, "game over" (2);
- point limit, teams: if either team's total ≥ limit (and scoring events > 0): text 121, sealed,
  "game over" (2).
- Time warnings (time limit only, host): the host keeps *L*, set to limit × 60 s at the start and
  at every restart. Each frame, with *t* = its countdown, the thresholds 1200, 900, 600, 300, 60
  (warnings 0–4) are tested in that order: warning *w* fires when *t* < threshold and threshold <
  *L*; then *L* := *t*, the host shows text 138 + *w* and sends "minutes left" *w*; receivers show
  the same text. So a warning equal to or above the limit is never shown (5 minutes: only 142;
  10: 141, 142; 20: 139–142; 30: all five), and at most one fires per frame (one skipped in a
  single frame is lost).
On "game over" clients show text 130 (time) or 131 (points). Every machine waits **3 s**, then shows
the results:
- teams (sub-modes 1, 2): first the winning team's line and total, then the other (tie: team 0
  first); names by texts 122/123 (cops, robbers: cops won), 126/127 (robbers won), 124/125 (blue,
  red: blue won), 128/129 (red won);
- then the players by points, highest first; the local player is ranked above remote players with
  equal points, remote players with equal points by slot order.

**Restart** (host, from the results): "restart" sent, session unsealed, the host's count of
scoring events := 0 (so stale points in old car states cannot end the new game); on every machine
the points := 0, the host's countdown back to the full limit, the local car to a new network respawn,
then "go" again (step 4 of the start, without a new state request). The gold and the bases stay
where they are; a carrier at the restart loses the gold (home).

## 6. Hookmen (not multiplayer)

The race files' `[Hookmen]` section (`../formats/aimap.md`) creates AI cars that each follow a
fixed route file, like a simple opponent without racing logic. They have nothing to do with
multiplayer or the gold. Only SF `roam.aimap` / `roam.aimap_p` have the section, with count 0, so
no shipped game creates one. Not planned (`../ROADMAP.md`). (MM2 also contains an unused
capture-the-flag AI racer type, created by a count that nothing sets.)

## 7. Files

| File | Use |
|---|---|
| `race/<c>/multicopwaypoints.csv` | Cops and Robbers spots (5.3) |
| `race/<c>/multicopsets.csv` | looked for, never usable (5.3) |
| `race/<c>/<c>_<gizmo>.pathset`, `_<race>.pathset` | gizmos (1.4); `race/london/london_bridge_multi.pathset` is never opened (no mode is named `multi`) |
| `geometry/wpobj_gold.pkg`, `pt_hideout.pkg`, `pt_bank.pkg`, `pt_red.pkg`, `pt_blue.pkg` | Cops and Robbers objects |
| `tune/vehicle/<car>.vehcarsim` | car check values (3.7) |
| `tune/<car>.info` `Flags` | bit 8: police car (free-for-all team); bits 1, 2: big vehicle |
| `tune/banger/sp_barricadeconc{l,r}_f.dgBangerData` | sturdier in multiplayer Circuit (1.4) |
| `jpg/<c>_multicop.jpg` | lobby / loading picture (menus, ours) |
| `mmlang.dll` | texts by number (`../formats/mmlang.md`) |

## Checked against

`tools/game/multiplayer.py` (output `work/check/multiplayer.txt`), all shipped data:

- `multicopwaypoints.csv`: London 46 rows, SF 44 (all kept, capacity 50, no duplicates); rows
  drawn London 0–44, SF 0–42 (the last row of each, London (510.0, 16.1, 622.4) and SF
  (−1850.7, 25.6, −331.6), is never a gold or base spot). Spot heights London −19.9 … 24.6, SF
  0.8 … 105.9. No `multicopsets.csv` ships.
- Path sets: multiplayer games open London `london_bridge.pathset` (Cruise, Cops and Robbers),
  `london_bridge_race0`, `_circuit0`, `_blitz10` for those races, sailboats, `london_train`, and
  parked cars only in races; SF the same minus trains. Never opened in multiplayer: all ferry files,
  the crash-course variants, `london_parkedcar_test`, and `london_bridge_multi.pathset` (its 4 path
  names are identical to `london_bridge.pathset`'s: `giz_bridge01_l`, `giz_waterloo_l`,
  `open:giz_bridge02_l`, `giz_waterloo_l`).
- Settings packing: every shipped race (London and SF: 12 Checkpoint, 10 Circuit, 10 Blitz, both
  skill levels) packs and unpacks without loss; time limits 18 … 120 s (all < 256), laps ≤ 4, race
  numbers ≤ 11. All 108 Cops and Robbers option bytes round-trip.
- Car check values of all 20 shipped cars are distinct (e.g. `vpbug` 9CE8B9A2, `vpmustang99`
  66D6DD02, which `vpcop` sends).
- Program tables: gold masses 0, 100, 200 → caps 1.0, 0.9, 0.81; minutes 5, 10, 20, 30; points
  100, 250, 500, 1000; Hermite rows (2, −2, 1, 1), (−3, 3, −2, −1), (0, 0, 1, 0), (1, 0, 0, 0);
  grid offsets and the 6.0 m wide-layout threshold as tabled in 2.2; handbrake flag threshold 0.8.
- HUD map slot colours read from the program's table (entries 4–11) as listed in 1.5, slots 6 and
  7 with alpha 64 and 109. Time warnings by the rule of 5.3 at 60 frames per second: 5 minutes
  → 142; 10 → 141, 142; 20 → 139–142; 30 → 138–142.
- Models: `pt_bank`, `pt_hideout`, `pt_red`, `pt_blue` only `L` and `VL`, each 2.0 × 1.0 × 0.05;
  `wpobj_gold` `H`/`M`/`L`/`VL`, 1.182 × 0.416 × 1.626.
