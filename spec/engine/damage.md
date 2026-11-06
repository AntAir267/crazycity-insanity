# Milestone 4: damage and effects

Cars that show damage the way MM2's do, and the effects that go with driving and crashing. The
damage *amount* already exists (`../physics/damage.md`); this milestone makes it visible and adds
MM2's particle and mark effects.

## In scope

- **Body damage visuals**: how MM2 marks a car's textures/model as it takes damage (the `_dmg`
  texture slots, texel damage, where on the car the damage lands, how it builds up), for the player,
  opponents/police and traffic as MM2 does each, including reset/repair.
- **Parts thrown off**: breakable car parts (`BREAK01…`, bumpers, mirrors, lights, wheels/hubcaps if
  MM2 does) that come off on hard impacts and at the wreck, their launch, their physics as loose
  objects, how long they stay, and how they collide. This includes the game-wide random draws they
  take (`../physics/damage.md` "Parts thrown off"), so check runs with hard wall hits match MM2.
- **Wrecked look**: what a wrecked car looks like (smoke, fire, darkened texture, sounds noted for
  later).
- **Particle system**: MM2's emitter/particle model (`tune/*.asBirthRule` and related), drawing,
  sorting, blending, lifetime, physics of particles, its own random generator.
- **Effects using it**: tyre smoke and dust by surface, engine/damage smoke, sparks on scraping,
  glass shards, debris from props and car parts, water splash, rain particles, anything else MM2
  emits (exhaust? headlight smashing?).
- **Skid marks**: when tyres leave marks, their shape, fading, limits.
- **Props after breaking**: what a knocked-over prop does in MM2 (pieces that fly and settle) instead
  of vanishing as the engine does now.
- **Glass**: the BillFlags glass panes (`street-props.md`) breaking.
- Damage visuals must not change physics; check runs gain thrown parts (and their random draws)
  once both the engine and the reference support them.

## Not in scope yet

Sound (later milestone; note which sounds each effect triggers), race modes, pedestrians.

## Done when

- Crashing a car visibly damages it the way MM2 does; hard hits throw off parts that tumble and
  stay in the street; a wrecked car smokes.
- Tyres smoke and leave marks when sliding; sparks fly when scraping walls; props shatter into
  pieces; glass breaks.
- All check runs (including the hard-hit city runs, now with thrown parts) match the reference
  byte for byte.
- It stays smooth in the browser.
