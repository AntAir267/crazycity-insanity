# Milestone 2: driving

Drive any car (stock or mod) around any city with Midtown Madness 2's handling. The aim is that a
mod car tuned for MM2 drives the same way here.

## In scope

- **Drive mode** in the viewer: pick a car and a city, press Drive. The car starts at the city's
  free-roam start point (or under the camera, as with "drop car").
- **Car physics** from the car's own tune files: engine, gearbox (automatic by default, manual
  as an option), drivetrain, brakes and handbrake, tyres, suspension, aerodynamics, the stability
  helpers MM2 has, and getting unstuck/righted. Same math, constants, order of operations and
  time step as MM2.
- **Collisions** with the city: driving on roads, sidewalks, curbs, grass and slopes; hitting
  walls and buildings; surface types that change grip or drag; driving into static props. Props
  that MM2 lets you knock over may stay fixed for this milestone.
- **Controls:** keyboard (arrows and WASD), and a gamepad if one is connected, with MM2's input
  smoothing, automatic reverse and handbrake. A key to reset the car onto its wheels.
- **Cameras:** chase camera with MM2's follow behaviour from the car's camera tune files, plus a
  far chase and bumper view; a key cycles them. The free-fly camera stays available.
- **HUD:** speed, gear and RPM. Our own design, not MM2's dials.
- **Headless check runs** as described in `drive-check.md`, so the engine can be compared number
  for number with a reference.

## Not in scope yet

Traffic, pedestrians, cops, opponents, knock-over props, damage, sound, race modes, multiplayer.

## Done when

- Every stock car drives, shifts and steers; mod cars use their own tune files.
- The standard check runs in `drive-check.md` produce output that matches the spec writers'
  reference within the tolerances given there.
- Driving around London and San Francisco works: curbs bump, walls stop the car, grass is slower,
  hills and ramps behave, the car never falls through the world.
- It stays smooth in the browser.
