# CrazyCity Insanity

CrazyCity Insanity is an original game engine that plays by **Midtown Madness 2**'s rules. It loads
your own copy of MM2 (its cities, cars, races, sounds and music) and MM2 mods, and drives them with
physics, AI and race rules rebuilt to match the original exactly. The code, menus and HUD are our
own; only the behaviour and the file formats follow MM2.

**No Midtown Madness 2 files are included, and none ever will be. You need your own copy of the
game.** This project is not affiliated with or endorsed by Microsoft, Angel Studios or Rockstar
Games; Midtown Madness is a trademark of Microsoft.

## What works today

- **Single player:** Cruise, Checkpoint, Circuit and Blitz races, the Crash Course, race opponents,
  results, progression and unlocks, MM2's driver profiles (import your own `players` folder).
- **The city:** traffic, police chases, pedestrians, traffic lights, cable cars, drawbridges,
  Underground trains, ferries, breakable props, damage and thrown car parts, trailers.
- **Sound and music:** car, tyre, impact, traffic and world sounds, speech, and MM2's DirectMusic
  soundtrack played by our own synthesiser.
- **Multiplayer:** Cruise, Checkpoint, Circuit, Blitz and Cops and Robbers between browser and
  native players.
- **Two builds from one engine:** a native Linux game (x86-64, glibc 2.34 or later) and a browser
  build (any current browser with WebGL 2). An optional Linux desktop app wraps the browser build.
- **Controls:** keyboard, mouse, gamepads, joysticks and wheels through MM2's own binding table,
  with MM2's force feedback on joysticks and wheels.
- **Mods:** extra `.ar` archives next to your MM2 files are loaded as MM2 loads them.

How it is checked: 97 scripted check runs (driving, crashes, traffic, trailers and a race) must
give the same output bit for bit on every change (their SHA-256 sums are in
`checks/expected.sha256`), and over a thousand automated checks cover the file formats, AI,
sound, music, menus, network rules and the native game. The game itself has **not yet been
play-tested widely**: expect rough edges, and please report anything that behaves differently from
MM2 as an issue ("Behaves differently from MM2"; see [Contributing](CONTRIBUTING.md)). Windows and
macOS have no native build yet; use the browser build there.

## Your MM2 files

Point the game at the folder MM2 is installed in (the one with `mm2core.ar`). It reads:

| File | Needed for |
|---|---|
| `mm2core.ar`, `mm2tex.ar` | everything (required) |
| `mm2aud.ar`, `mm2audex.ar` | sound, speech and music |
| `mmlang.dll` | the game's own texts in your language (English labels otherwise) |
| `players/` | importing your MM2 drivers, records and options (optional) |
| `gm.dls` | the General MIDI instruments of the music (optional; a bundled drum kit and piano are used otherwise) |
| any other `*.ar` | mods (cars, cities, races) |

Nothing is ever written to your MM2 folder; drivers and options are kept separately (native:
`~/.local/share/crazycity-insanity/` and `~/.config/crazycity-insanity/`; browser: the page's own
storage).

## Building and running

Clone the repository, then build one of the two versions with `./build.sh` (run it without
arguments for the full list of commands).

### Native game (Linux)

You need:

- a C++17 `g++` (built and tested with GCC 15), `cmake`, `ninja`, `git`, and the zlib development
  package;
- the **SDL 3.4.18 source release**: download `SDL3-3.4.18.tar.gz` from SDL's releases page
  (github.com/libsdl-org/SDL/releases) and put it in `third_party/` (as
  `third_party/SDL3-3.4.18.tar.gz`, or unpacked as `third_party/SDL3-3.4.18/`). The build compiles
  SDL from it and links it statically. SDL uses whatever video and audio development packages are
  installed, so install the X11 and/or Wayland and the PipeWire, PulseAudio or ALSA development
  packages first (SDL's `docs/README-linux.md` in the tarball lists them per distribution).

```
./build.sh native            # -> build/native/crazycity
build/native/crazycity       # asks for your MM2 folder once and remembers it
build/native/crazycity --mm2 /path/to/Midtown\ Madness\ 2
```

The game opens on its main menu. `crazycity --help` lists the command-line options (another MM2
folder, importing your drivers again, starting a game straight away for testing).

**A single-file AppImage:** `./build.sh appimage-native` writes
`work/release/CrazyCity_Insanity-native-<version>-x86_64.AppImage`. It also needs
`appimagetool-x86_64.AppImage` and `appimage-runtime-x86_64` from the AppImage project's releases
(github.com/AppImage/appimagetool and github.com/AppImage/type2-runtime) in `third_party/appimage/`.

### Browser build

You need [emsdk](https://emscripten.org/docs/getting_started/downloads.html) (installed in
`~/emsdk`, or with its `emsdk_env.sh` sourced; built and tested with Emscripten 6.0), the same
`third_party/SDL3-3.4.18.tar.gz` (Emscripten's SDL port is built from it), the native tools above
(the build also makes a small test mod with the native `cctool`), and your MM2 files in `orig/` (at
least `mm2core.ar` and `mm2tex.ar`, plus the audio archives, `mmlang.dll` and `players/` if you
have them; the development page loads them from there).

```
./build.sh web               # -> dist/
./build.sh serve 8820        # then open http://127.0.0.1:8820/
```

In the page, **Open my MM2 folder** reads an MM2 folder from your computer instead; the files never
leave the browser. For network games, serve the page with the session
server instead: `./build.sh netserver 8821 dist` (needs Node.js, no packages; tested with Node 22).

## Controls

The default keys are MM2's: **arrow keys** drive (Down brakes, and reverses when stopped),
**Space** handbrake, **Enter** horn, **C** camera, **V** orbit view, **D** cockpit view, **T**
automatic/manual gearbox, **A**/**Z** shift up/down, **R** reverse, **Tab** map, **Q** full-screen
map, **Backspace** mirror, **S**/**X** next/previous waypoint, **Esc** pause menu. Options ›
Controls picks the device (keyboard, mouse, gamepad, joystick or wheel) and Customise controls
changes every binding, as in MM2. In the native game F11 or Alt+Enter switches full screen.

## Licence

Our code and documentation are released into the public domain under [the Unlicense](LICENSE).
The bundled third-party parts (fonts, the General MIDI bank, SDL and others) keep their own
licences: see [THIRD-PARTY.md](THIRD-PARTY.md). Midtown Madness 2 and its files belong to their
owners and are not part of this project.

---

# Working on the code

See also [CONTRIBUTING.md](CONTRIBUTING.md).

## How the code is organised

Top level: `spec/` (the behaviour specification, below), `engine/` (the C++17 engine, both
builds), `web/` (the browser page's thin shell),
`server/` (the session server for network games, Node), `desktop/` (the optional Electron desktop
app), `checks/` (the check runs and their expected sums), `packaging/linux/` (desktop entry and
icon), `build.sh` (every build and test command), `third_party/` (not in git: SDL, AppImage tools,
Electron).

The engine (`engine/`, C++17) builds natively (`cctool`, the tests and check runs; the native game
`build/native/crazycity`, below), for the browser (Emscripten + WebGL, `dist/`) and as a Linux
desktop app around the browser build (`./build.sh appimage`, below). `./build.sh` lists the
commands: `web`, `native`, `selftest-native`, `appimage-native`, `tool`, `test`, `checks`, `uitest`,
`music`, `serve`, `appimage`. It reads the player's own MM2 files (`orig/*.ar`, or the
whole MM2 folder opened in the page) and any mod `.ar` beside them; nothing of MM2 ships.

| Part | Where | What it covers |
|---|---|---|
| Files | `vfs/`, `formats/`, `model/` | `.ar` archives with mod override order, every MM2 file format the game reads, car and prop models |
| City | `city/` | PSDL rooms and room finding, `.inst` objects, street and path-set props, decals, visibility lists, the moving level objects (boats, drawbridges, Underground trains, ferries, parked cars) |
| Physics | `physics/` | the car (engine, gears, wheels, suspension, body), city and object collisions, car-to-car contacts, damage and thrown parts, loose bodies and broken props, trailers; `drive_check*.cpp` runs the check files |
| AI | `ai/` | the road network, traffic (lane driving, reactions, physical traffic cars), traffic lights, police, race opponents and their planner, cable cars, pedestrians |
| Game | `game/` | race modes (Checkpoint, Circuit, Blitz, Crash Course), waypoints, results and progression, player profiles (`.cfg`/`.sav`), HUD and HUD map |
| Effects | `fx/` | particles, sparks, smoke, skid marks, damage visuals |
| Sound | `audio/` | the mixer, 3D slots and echo, car, tyre, impact, traffic and world sounds, speech |
| Music | `music/` | DirectMusic segments, styles and bands, the composer and performance, the DLS/SF2 synthesiser (bundled FluidR3 drums and piano when the player has no `gm.dls`) |
| Drive | `drive/`, `app/` | the drive session (controls, cameras, the frame order), the viewer, Drive mode and games, the web API |
| Network | `net/`, `platform/web_transport.*`, `platform/native_transport.*`, `platform/host_server.*`, `server/` | network games (Cruise, Checkpoint, Blitz, Circuit, Cops and Robbers): sessions, shared settings and car states, remote cars; our own transport (browser: WebRTC; native: WebSocket relay and direct UDP), the session server in Node and in the engine (`./build.sh netserver`, `hostserver`, `nettest`) |
| Drawing | `render/`, `platform/` | the WebGL / OpenGL ES 3.0 renderer (one for both builds), SDL3 input and windowing |
| UI | `ui/` | the engine-drawn UI of both builds: TrueType fonts (IBM Plex, embedded) with kerning and a glyph atlas, text wrapping and ellipsis, layout (rows, columns, grids, layers), painting (rounded boxes, shadows, gradients, backdrop blur), focus with keyboard and gamepad, mouse and touch, text fields with the clipboard; a GL backend and a CPU one for the tests |
| Front end | `app/front_end*`, `app/fe_*.cpp`, `app/menu_data.*`, `app/game_api.*` | every screen on the UI: the viewer panel, main menu, races, car, Crash Course, drivers and import, record, statistics, options and controls, loading, HUD and map frame, pause menu, results with Next race, the multiplayer screens; the program's hooks are `ui/host.h` (storage, file choices, clipboard) and `net/net_link.h` (the session server) |
| Native | `app/main_native.cpp`, `app/native_host.*`, `native/`, `packaging/linux/` | the native program: the MM2 folder from disk, profiles as files, the front end, command-line games for testing, the debug overlay, the self-test, the AppImage |
| Page | `web/` | a thin shell: `index.html`, `style.css` and `app.js` (the archives, the browser's storage and file choices, the clipboard, drag and drop, test hooks), `audio.js`, `net.js` (network games) |
| Desktop | `desktop/` | the Electron shell of the AppImage: window, local server, the player's MM2 folder, self-test |

**How a frame fits together** (Drive mode and games): the controls are read, then the AI frame
runs in MM2's order (traffic cars, pedestrians, opponents or police, the late traffic step with
cable cars and lights), the level objects move, then the physics steps run for the player, trailers,
traffic bodies, opponents and loose bodies in one scene, followed by effects, sound and music,
and drawing.

## Tests and check runs

Most tests read your own MM2 files from `orig/` (at least `mm2core.ar` and `mm2tex.ar`; the sound
and music tests also `mm2aud.ar` and `mm2audex.ar`, the profile tests `mmlang.dll` and `players/`).

| Command | What it checks |
|---|---|
| `./build.sh test` | the file formats, the AI (road network, traffic, police, opponents), effects, pedestrians, sound and race modes over the real archives |
| `./build.sh uitest` | player files, progression, `mmlang.dll`, the HUD and map, then every menu screen without a window (`cctool uiscreens`, PNGs in `build/uiscreens/`) |
| `./build.sh music` | the music system: file parsing, the composer, renders to `build/music_*.wav` |
| `./build.sh nettest` | network games: sessions and modes over a loopback, then over real local sockets (both servers, direct UDP, relay) |
| `./build.sh selftest-native` | the native game offscreen: main menu, Cruise and a race, audio, every control device |
| `./build.sh checks` | the 97 check runs (`checks/*.run`) to `build/checks/*.csv` |

**Check runs.** Each `checks/*.run` is a scripted drive (a car, a world, timed inputs) whose output
CSV must not change by a single bit unless a change means to change the behaviour. Run them before
and after your change and compare, or compare against the expected sums:

```
./build.sh checks --verify     # runs all 97 and compares them with checks/expected.sha256
```

It lists every run whose output differs and exits non-zero. If your change is meant to change some
runs, say which and why in the pull request.

## The behaviour specification

`spec/` describes in plain language how Midtown Madness 2 behaves, one page per area: the file
formats (`spec/formats/`), physics (`spec/physics/`), AI (`spec/ai/`), the game's rules and menus
(`spec/game/`), sound and music (`spec/audio/`) and the engine around them (`spec/engine/`), with
the numbers, equations and order of operations the engine needs. The engine is written from it:
code comments cite its pages and sections (for example `physics/loose-bodies.md` 6.1), a change
that alters behaviour follows the spec, and points the spec leaves unclear are noted in
`spec/QUESTIONS.md` together with the choice the engine made until they are settled.
`spec/ROADMAP.md` lists the milestones.

If you think the spec itself is wrong, open an issue ("Behaves differently from MM2") that names the
page and says what the original game does, and how you saw it.

## Network games (development)

MM2's multiplayer rules run on our own transport: browser peers talk
over WebRTC data channels, with a small WebSocket server for hosting, joining by a four-letter code
and relaying when WebRTC cannot connect. `./build.sh netserver 8821 dist` runs that server
(`server/signal.js`, Node, no dependencies) and serves the page on the same port; the page's
Network tab then hosts or joins (`?join=CODE` in the address joins at once, `?net=ws://…/net`
picks another server). `./build.sh nettest` runs two engine instances in one process over a
loopback transport and checks the session rules, the car-state sync and the modes.

**Native networking** (`NATIVE_NET_SRC` in `build.sh`; every native build, never the browser one;
threads, and OpenSSL 3 loaded at run time for `wss://` only, so nothing extra is linked):

- `platform/native_transport.*`: `NativeTransport`, the native `net::Transport`. A WebSocket
  (`ws://`, or `wss://` through OpenSSL with the certificate and name checked) to the session
  server: host, join by code, the game list, signalling, the binary relay. Game messages use
  `net.js`'s framing, so browsers and native players read each other. Between native players it
  looks for a direct UDP path (`platform/direct_link.*`: candidates swapped through the server,
  LAN and 127.0.0.1 addresses plus the address the server saw, simultaneous probes for hole
  punching, a small acknowledged channel for guaranteed messages, car states sent once). It falls
  back to the relay when no path is found or a path dies. A browser peer always goes through the
  relay. `LanSearch` finds LAN-mode servers on the local network.
- `platform/host_server.*`: `HostServer`, `server/signal.js` ported into the engine (the same
  protocol, answer for answer; the nettest compares the two servers byte for byte). It serves
  `crazycity --server` / `cctool netserver` (`./build.sh hostserver [port] [dir] [--lan]`) and an
  in-game "host on this computer" (`startThread()`). It listens on 127.0.0.1 unless LAN mode is
  chosen. On the same port over UDP it answers "where do my datagrams come from" (no STUN service)
  and, in LAN mode, LAN game searches.
- Protocol 2 (both servers): a `welcome` {proto, udp} on connecting; `host`/`join` carry the
  machine's capabilities (`webrtc`; `native`, `udp`), passed on in `opened` and `joined`, so
  `net.js` never offers WebRTC to a native player; `ping`/`pong`; an unguaranteed relayed message
  (flags bit 0 clear) is latest-wins while the receiver's socket is backed up. `signal.js` also
  takes `--tls-cert/--tls-key` (wss), `--no-udp`, `--test-ids` (repeatable ids and codes) and
  `--port 0`.
- `cctool netclient <ws://…/net> (--host | --join CODE) … <archives>` is a headless native player
  (a scripted driver that prints what it sees of the others' cars), used to check cross-play.
- `./build.sh nettest` also runs the sessions over real local sockets: the engine's server with
  direct UDP and with the relay only, `signal.js` (when Node is there), and car states lost and
  late on the direct path. Logs go to `build/net_test_sockets.txt`. It also runs the socket-level
  checks: the codec, the two servers' answers, the guaranteed channel losing 30 % with its relay
  fallback, a browser's framing, `wss://` with a test certificate, and LAN search.

**In the native game** (`build/native/crazycity`): `App` keeps one `net::Transport` for the
session, a `WebTransport` in the browser and a `NativeTransport` natively
(`App::setNetConfig`, `nativeTransport()`), and can run the engine's server on its own thread
for "host on this computer" (`App::netStartLocalServer(port, lan)`, `netLocalServer()->urls()`).
The Multiplayer screens host and join through `platform/native_net_link.*` (below); for testing
the command line plays network games too:
`--host-game` (of `--mode` cruise/checkpoint/circuit/blitz/cops, `--city`, `--race`, on this
computer; `--lan` opens it to the local network), `--join CODE`, `--net ADDRESS` (another server),
`--name`, `--password`, `--start-with N` (Enter starts at once; a guest's Enter readies), `--relay`;
the overlay shows the code, the players and how each is reached. `crazycity --server [--port N]
[--lan] [--static dir]` is the headless server (`net::serverMain`). Settings in `native.cfg`:
`net_server` (default `ws://127.0.0.1:8821/net`), `net_name`, `net_direct` (1), `net_udp_port` (0 =
any), `net_host_port` (8821), `net_host_lan` (0), `net_ca_file` and `net_verify_tls` (wss).
`--offscreen` runs it without a window (tests). The self-test hosts a game on this computer.

**The Multiplayer screens natively** (`platform/native_net_link.*`, the front end's `net::NetLink`):
"Host on this computer" (on by default, `net_host_here`) starts the engine's session server for the
game and hosts on it, "Let players on my network join" is its LAN mode (`net_host_lan`); the
lobby then names the addresses friends type under Server. Otherwise hosting and joining use the
server under Server (remembered as `net_server`). Find games asks that server and the local network
(LAN-mode servers, each game joined on its own server). Each other player's row says how it is
reached (direct with its round trip, relayed, or a browser through the relay); a connection that
fails says why. The multiplayer name is `net_name`.

## UI tests (development)

`./build.sh uitest` runs the profile, progression, mmlang and HUD checks and then `cctool
uiscreens`: the front end without a window or GPU against the player's own profiles, `mmlang.dll`
and archives. It walks the main menu with keys, opens every screen with clicks and leaves it with
Esc, types a driver's name (with the clipboard) and deletes the driver, starts a game (loading
screen, HUD, pause menu, results, Next race), lays the menus out at three window sizes, checks the
sliders' numbers, and draws every screen to `build/uiscreens/*.png`. In the
page, `window.CCITest` (`web/app.js`) lists the UI's labelled nodes and their places
(`CCITest.tree()`), clicks them as pointer events (`CCITest.click('PLAY')`), presses keys, types
text and runs frames on demand (`?timer` keeps the main loop running in a hidden pane).

## Desktop app (Linux AppImage)

`./build.sh appimage` builds the web version, puts it in an Electron 38 shell (`desktop/`) and
writes `work/release/CrazyCity_Insanity-<version>-x86_64.AppImage` (0.1.0, about 110 MB). It needs
emsdk (for the web build), `rsvg-convert` (the icon), Electron's `dist` folder in
`third_party/electron` and `appimagetool-x86_64.AppImage` with `appimage-runtime-x86_64` in
`third_party/appimage` (`third_party/` is not in git: unpack Electron's linux-x64 release zip into
`third_party/electron`, and take `appimagetool` and its runtime from the AppImage project's
releases). Notices inside: `usr/share/doc/crazycity-insanity/` (Electron's
MIT licence, Chromium's `LICENSES.chromium.html`, the FluidR3 notice).

- **No MM2 files are in it.** The AppImage holds Electron, `desktop/*.js`, `server/signal.js` and
  the web build without `dist/game`; the build refuses to pack when an archive, `mmlang.dll`,
  players file, `gm.dls`, executable or link is in the AppDir.
- **The MM2 folder comes from the player.** On the first start (or when the remembered folder is
  gone) a native folder dialog asks for the folder Midtown Madness 2 is installed in (the one with
  `mm2core.ar` and `mm2tex.ar`; the folder just above it works too). The path is kept in
  `~/.config/crazycity-insanity/config.json`, and the files are read from there as the page asks:
  every `.ar` in it (mods included), `mmlang.dll`, the `players` folder (for the Drivers screen's
  import) and an optional `gm.dls`. Ctrl+Shift+O picks another; the page's own "Open my MM2
  folder" works too and is remembered.
- **A local server in the app** (`desktop/local_server.js` over `server/signal.js`) serves the page,
  the game files (byte ranges, so the audio is read lazily) and the WebSocket `/net` on
  `127.0.0.1:28620`, so single player and hosting or joining network games work as in the
  browser. The port is remembered because the page's storage (drivers, records, options, kept in
  `~/.config/crazycity-insanity/`) belongs to its address. "Let friends on my network join"
  (off by default) opens the page and `/net` to the local network; the game files are only ever
  served to this computer.
- F11 full screen, Ctrl+Shift+O change the MM2 folder, Ctrl+Shift+R reload; the window opens on the
  front end (`?play`) and keeps its size.
- **Chromium's sandbox** is kept where ordinary programs may create user namespaces. Ubuntu 24.04
  and later forbid that through AppArmor, and Chromium's setuid helper cannot work from an
  AppImage, so there `AppRun` starts the app with `--no-sandbox` (the page runs with context
  isolation, no Node, a content security policy, and navigation limited to its own address).
  `CCI_SANDBOX=off` forces it off.
- **Self-test:** `./CrazyCity_Insanity-0.1.0-x86_64.AppImage --selftest out.png [--mm2 <MM2 folder>]`
  loads the game in a window that is never shown (with a throwaway profile), waits for the main
  menu with the data loaded, drives a few seconds of Cruise, writes `out.png` and `out-menu.png`,
  prints what it found and exits with 1 on failure. From a checkout, after `./build.sh web`:
  `third_party/electron/electron desktop --selftest out.png --mm2 orig`.

## Native game (development)

`./build.sh native` builds `build/native/crazycity` (and `./build.sh native debug` an unoptimised
`crazycity-debug`): the same engine, renderer and GLSL ES 300 shaders as the page, in a window of
its own through SDL3 (linked statically) with an OpenGL ES 3.0 context. SDL comes from
`third_party/SDL3-3.4.18.tar.gz` (the SDL source release, not committed); the browser build uses
Emscripten's SDL3 port built from the same source, so both share `platform/platform_sdl.cpp`.

- **Game data:** the player's MM2 folder is read from disk (`--mm2 DIR`, else the folder remembered
  in `~/.config/crazycity-insanity/native.cfg`, else the system's folder dialog, asked once). Every
  `.ar` in it (mods too), `mmlang.dll`, `players/` and an optional `gm.dls`; the archives are read
  range by range as the engine needs them. The FluidR3 drum and piano bank ships next to the binary.
- **Profiles:** kept as MM2's own files in `~/.local/share/crazycity-insanity/players/`; on the first
  start (or with `--import-players`) the MM2 folder's `players/` is imported. MM2's folder is never
  written to.
- **Menus:** the program opens on the engine-drawn front end, the same screens as the page's
  (mouse, keyboard, gamepad); Quit closes it. The viewer's folder and file buttons use the system's
  dialogs (the MM2 folder, mod archives, a players folder to import, a `gm.dls` or SoundFont) and
  text fields use the system clipboard. The menus' own settings (draw distance, the viewer's
  options) are kept in `native.cfg`.
- **Controls:** a game drives by the driver's binding table (`players/<name>.cfg`, as MM2's
  controls screen sets it) for its control device (mouse, keyboard, joystick, gamepad, wheel), in the
  browser too; Options > Customise controls shows and changes it, and the Keys box lists the
  bindings in use. `--device` and `--bind wheel.throttle=axis:z` change it from the command line,
  `--list controls` shows it. Wheels with separate pedal axes work once the pedals are bound to
  their axes (each pedal over its whole travel from where it rests: an engine choice). Joysticks
  and wheels steer through MM2's shaping with its approach mode, the mouse device by the pointer's
  place; the dead zone and steering sensitivity are the driver's. Force feedback is MM2's
  (friction, centring spring, rumble and jolts) on a
  joystick or wheel that reports it, with the driver's option on, never on a gamepad. The viewer's
  Drive mode keeps the engine's own keys.
- **Testing:** the command line can still start a game straight away, through the front end: `--city
  sf --mode checkpoint --race 3 --car vpbug --paint 1 --skill pro` (`--list
  cities|races|cars|drivers|controls`, `--help`); `--overlay` (or F1) shows the debug text overlay.
  F11 or Alt+Enter switch full screen (the window size is remembered), Ctrl+Q quits.
- **Tests:** `./build.sh selftest-native` runs `crazycity --selftest` offscreen (SDL's offscreen
  video driver, an EGL pbuffer; dummy audio): it imports the profiles, draws the main menu, drives
  Cruise and a race through the front end, writes `build/native/selftest_menu.png`, `selftest.png`
  and `selftest_race.png`, checks the audio output, and drives every control device through the
  binding table (virtual gamepad, wheel with force feedback recorded, joystick, injected mouse and
  keys); it exits non-zero on a failure.
- **Release:** `./build.sh appimage-native` makes
  `work/release/CrazyCity_Insanity-native-<version>-x86_64.AppImage` (the program, the FluidR3 bank,
  the licences, `packaging/linux/` desktop entry and icon; no MM2 files) with the AppImage tool and
  runtime in `third_party/appimage/` (or `third_party/`). It runs on glibc 2.34 or later (`native/glibc_compat.c`).

## Credits and licences

The full list with licence texts is in [THIRD-PARTY.md](THIRD-PARTY.md).

- **General MIDI drum kit and piano** (`web/gm/FluidR3_GM_drums_piano.sf2`, used by the music
  when the player has not added their own `gm.dls`): trimmed from the Fluid (R3) SoundFont,
  Copyright (c) 2000-2002, 2008 Frank Wen, released under the MIT licence; full notice and the
  list of contributors in `web/gm/COPYING-FluidR3.txt`. Made with `cctool gmtrim`.
- **SDL 3** (both builds; linked into the native game): Copyright (C) 1997-2026 Sam Lantinga,
  zlib licence; the AppImage carries its `LICENSE.txt`.
- **IBM Plex Sans and IBM Plex Mono** (the UI's fonts, embedded in both builds from
  `engine/ui/fonts/`): Copyright 2017-2018 IBM Corporation, SIL Open Font License 1.1
  (`engine/ui/fonts/OFL.txt`, shipped as `licences/IBM-Plex-OFL.txt` with the page and in the
  AppImages).
- The native overlay's 5 × 7 font, the UI's vector icons and the icon
  (`packaging/linux/crazycity-insanity.svg`) are our own drawings.
