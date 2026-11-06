# Route search: how an AI car finds its way to a point

The search a chasing police car runs every frame to find which intersections to drive through to
reach a point (the perp, or a blocking point; `police.md`). Race opponents **never** use it: an
opponent's route is the list of intersections read from its route file (`../formats/opp.md`, `opponents.md`).
Outside the police, only the hookmen and the multiplayer flag-game racers run it (not in scope).
The result goes to the AI driver as part of its orders (`driver.md`, "Orders"). Nothing here draws
random numbers.

The network, its terms and the queries used below are those of `road-network.md` (roads with
sections 0 … *n* − 1, centre points C, section axes X, Y, Z, centre distances CD, half width;
**end 0** at section *n* − 1 and **end 1** at section 0; shortcuts from `<c>_sup.bai`; room lists
and component kinds 0 room, 1 road, 2 shortcut, 3 intersection). Arithmetic is float32 with one
rounding per operation (`../physics/vehicle.md`, "Precision"); three-term sums are grouped as
written.

## The graph

Nodes are the intersections; every road and shortcut listed by an intersection is an edge to the
intersection at its other end, usable in both directions whatever its lanes and side flags. The
edge cost is the road's **length** `CD[n−1] − CD[0]` (`road-network.md`, "Police routing graph").
The **other end** of road *r* seen from intersection *I* is the intersection of end 0 if *r*'s end 1
names *I*, otherwise the intersection of end 1 (so a road that leaves and returns to *I* leads to
*I*). The **roads at *I*** are *I*'s road list in its stored order (angle order, `bai.md` load step
4); that order matters below.

**Road between intersections *A* and *B*** (used by the driver to turn a route into roads): for
each road of *A*'s list in order, and for each road of *B*'s list in order, the first road found in
both lists; with it a **direction flag** = 1 if *A* is that road's end-1 intersection (the car will
travel from section 0 toward section *n* − 1), else 0. No common road: none. With parallel roads
this is the first common one in list order, which need not be the one whose length the search used.

## Inputs and output

| Input | Meaning |
|---|---|
| start point *S* and the start car's Z row *Zs* | the searching car's body position and back axis |
| start room hint | the car's current room |
| goal point *G* | where to go |
| goal room hint | the room the goal is known to be in (the perp's current room for the police) |
| start mode | **free** (the car may set off toward either end of its road; the police always use this) or **keep heading** (the route must continue the way the car faces) |

MM2's search also takes a goal direction, which it never reads.

Output: a list of intersection numbers and its count *k* (0 = no route). The caller's list must
hold the longest possible route; the police keep 100 entries (the longest shortest route in the
shipped cities is 35 intersections, plus at most two start entries).

Working state: a cost and a predecessor per intersection, an **open list** (an ordered list of
intersections; *Insert(v)* puts *v* at the **front** unless it is already in the list, in which
case it stays where it is), and a **goal list** of up to 16 intersections.

## Placing the start and the goal

Both points are placed with "All components at a position" of `road-network.md` (no preferred
road): up to five components each. *Ls* = the start's list with *ρs* its room, *Lg* = the goal's
list with *ρg* its room; *s₀* and *g₀* are their first entries. Two components are equal when kind
and number are equal. **Neighbours(ρ)**: the distinct non-zero rooms named across the edges of
*ρ*'s perimeter (`../formats/psdl.md`, perimeter entries), in perimeter order.

## The search

1. *k* = 0; every intersection's cost = 9999999.0; open list empty.
2. Place start and goal. If any component of *Ls* equals any component of *Lg*: **no route**
   (*k* = 0; the car is already on the goal's road or intersection). Stop.
3. **Seed** from *s₀*:
   - **Room** (kind 0; the car is off the network): the start node is *virtual* (no intersection).
     First pass: for each room of Neighbours(*ρs*) in order, for each entry of its list in order:
     if it is an intersection *I*: cost[*I*] = √(dx·dx + dz·dz) with (dx, dz) = *S* − *I*'s centre
     (x and z), Insert(*I*); then, whatever its kind, if the entry equals *g₀*: no route, stop.
     If the first pass inserted nothing, second pass: for each neighbour room in order, for each
     road entry (kind 1 only) in order: its end-0 intersection, then its end-1 intersection, each
     with cost = the same flat distance from *S* to that intersection's centre, Insert.
   - **Road or shortcut *r*, free start**: *d* = distance of *S* along *r*'s centre line
     (`road-network.md`, "Other road queries"); cost[end-0 intersection] = length(*r*) − *d*,
     Insert it; cost[end-1 intersection] = *d*, Insert it (a road with both ends at one
     intersection keeps the second value). Start node virtual; *k* = 1.
   - **Road *r*, keep heading**: the car's facing on *r* (`road-network.md`, "Facing", with the
     section from "Section of a point" limited to *n* − 1): if it goes in the road direction,
     *behind* = end-1 intersection and *ahead* = end-0 intersection, otherwise the reverse.
     list[0] = behind, list[1] = ahead, *k* = 2, start node = ahead; seed around *ahead* skipping
     *behind*.
   - **Intersection *I*, free start**: list[0] = *I*, *k* = 1, start node = *I*; seed around *I*
     skipping nothing.
   - **Intersection *I*, keep heading**: heading *h* = *Zs* if the **first player's** car is in
     reverse gear, else −*Zs* (MM2 looks at the player, not at the searching car). Over *I*'s roads
     in order, ignoring roads whose side B has no sidewalk line (every shortcut): *d* = C[1] − C[0]
     if *I* is the road's end-1 intersection, else C[*n*−2] − C[*n*−1]; *v* = ((d.z·h.z) + (d.y·h.y))
     + d.x·h.x; keep the first road whose *v* is strictly greater than the best so far (best starts
     at −999999.0); if none, the first road of the list. list[0] = *I*, list[1] = that road's other
     end, *k* = 2, start node = list[1]; seed around list[1] skipping *I*.

   **Seed around *A* skipping *B***: for each road at *A* in order: *v* = its other end seen from
   *A*; if *v* is *B*, skip; cost[*v*] = length of the road (**assigned**, not the minimum: with
   parallel roads the last one in *A*'s list sets the cost); Insert(*v*). Then cost[*A*] = 0
   (*A* itself is not inserted, unless a road from *A* back to *A* inserted it).
4. Every intersection's predecessor = the start node.
5. **Goal list**, for each component of *Lg* in order:
   - room: for each room of Neighbours(*ρg*), each entry of its list in order: a road (kind 1)
     adds its end-0 intersection, then its end-1 intersection, each only if not yet in the goal
     list; then, whatever its kind, if the entry equals *s₀*: no route, stop;
   - road or shortcut: add its end-1 intersection, then its end-0 intersection (no duplicate test);
   - intersection: add it.
6. If the start node is in the goal list: if *k* = 1 then *k* = 0; stop. (A keep-heading start
   keeps *k* = 2: [behind, ahead] or [*I*, neighbour].)
7. **Expand** while the open list is not empty:
   1. *u* = the open entry with the smallest cost: walk the list from the front with best =
      9999999.0 and take an entry only if its cost is **strictly** smaller than the best so far
      (on equal costs the entry nearer the front wins).
   2. If *u* is in the goal list: found = *u*; leave the loop.
   3. Remove *u* from the open list. For each road at *u* in order: *v* = its other end seen from
      *u*; *c* = length(road) + cost[*u*]; if *c* < cost[*v*]: cost[*v*] = *c*, Insert(*v*),
      predecessor[*v*] = *u*.

   There is no closed set: an intersection taken out of the list goes back in if a cheaper cost
   reaches it later.
8. If the open list runs empty without reaching a goal: **no route**, *k* = 0. (MM2 then reads an
   undefined value; a goal list is empty, and the goal therefore unreachable, when the goal point
   lies in one of 145 SF / 67 London rooms — almost all blocks — whose neighbours carry no road.)
9. **Write the list**: *h* = the number of predecessor steps from found back to the start node;
   *k* = *k* + *h*; list[*k* − 1] = found, list[*k* − 2] = its predecessor, … down to
   list[*k* − *h*]; the start node itself would land at list[*k* − *h* − 1].
   - Room start (*k* was 0): the start node would be written one place **before** the list; MM2
     writes the number 9999 there (`police.md` notes what that overwrites). Nothing is stored.
   - Free road start (*k* was 1): list[0] receives the virtual start and is replaced in step 10.
   - Intersection or keep-heading start: the start node is already where it belongs.
10. **First entry**: if *s₀* is a road or shortcut: list[0] = its end-1 intersection if list[1] is
    its end-0 intersection, otherwise its end-0 intersection (the end the car turns its back on).
    If *s₀* is an intersection: list[0] = *s₀*.

Shapes of the result:

| Start | List |
|---|---|
| room | first intersection reached, …, goal intersection |
| road, free | end turned away from, first end driven to, …, goal |
| road, keep heading | behind, ahead, …, goal |
| intersection, free | *I*, …, goal |
| intersection, keep heading | *I*, chosen neighbour, …, goal |

The goal intersection is the first goal reached; for a goal on a road, either of its ends.

## Behaviour that must be kept exactly

The seeding by assignment, the front insertion, the strict-less scan, the missing closed set and
the list orders decide which of several parallel roads' costs is used and which of two routes of
equal cost is taken. A search that only finds "a shortest route" gives different routes in the
shipped cities (London intersections 198 → 199: MM2's cost is 31.081 through the last of three
parallel roads, not the shortest 29.104).

## Deviations from MM2

- No route reachable: count 0 (MM2: undefined).
- The room-start write before the list (step 9) is not done (`police.md`: MM2's write has no
  observable effect on the police).
- "Distance along" past the last section and a section index of *n* in the keep-heading facing
  test read past their tables in MM2; use the road length and *n* − 1.
- MM2 prints a warning for component kinds outside 0–3; they cannot occur.

## Users

The police (`police.md`, "Driving orders"): free start, their own body position and Z row, their
own current room and the perp's current room as hints, a 100-entry list; every result is handed to
the driver at once. The keep-heading mode is used by other AI racers, not in this milestone.

## Checked against

`work/notes/police/route_proto.py` (dirty side) implements this page with its own `.bai` reader and
the PSDL room search of `tools/sim/city.py`:

- Constants in MM2's machine code: start cost and scan start 9999999.0, heading best −999999.0,
  virtual start 9999, goal list 16 entries; no random-generator calls anywhere in the search.
- Graph: SF 214 intersections, 379 roads + 49 shortcuts (66 intersections re-sorted when shortcuts
  join); London 328, 540 + 70 (91 re-sorted). Every road is listed by both its end intersections;
  SF has 1 and London 3 roads that start and end at the same intersection; London intersections 198
  and 199 are joined by three roads.
- Searches between Cruise cop spots: SF cop 0 → cop 1 gives [40, 50] (cost 230.397); SF cop 2
  (standing on a shortcut) → cop 0 gives [183, 168, 174, 41, 40]; London cop 0 → cop 1 gives
  [29, 249, 280, 138, 140]; London intersection 198 → 199 gives [198, 199] at cost 31.081; London
  intersection 7 → a point on road 5 (which ends at 7) gives no route (step 6).
- 600 random searches (300 per city) agree with an independent shortest-path search seeded the
  same way, and every consecutive pair of each result is joined by a road. Longest result over all
  intersection pairs: 26 (SF), 35 (London).
- `tools/ai/police.py route sf|london` runs the search from every Cruise cop spot to the next spot
  and to three fixed road points and draws the routes (`work/check/police_routes_<city>.png`):
  SF 75 routes and 1 empty, London 78 and 2 empty; each empty one is a goal on the road or
  intersection next to the start (steps 3 and 5 stop: start and goal components touch). Routes
  follow the street grid and use shortcuts across parks.
