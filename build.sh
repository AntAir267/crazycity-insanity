#!/usr/bin/env bash
# CrazyCity Insanity build script.
#
#   ./build.sh web [debug]   browser build -> dist/ (page, .js, .wasm, game/ data links, test mod)
#   ./build.sh native [debug]  native game -> build/native/crazycity (debug: crazycity-debug), SDL3 static
#   ./build.sh selftest-native [frames]  the native game's offscreen self-test over orig/ -> build/native/selftest*.png
#   ./build.sh appimage-native  the native game as work/release/CrazyCity_Insanity-native-<version>-x86_64.AppImage
#   ./build.sh test          native command-line test over the real archives in orig/
#   ./build.sh tool          native cctool only (build/native/cctool)
#   ./build.sh testmod       make dist/game/zz_testmod_vpmodbug.ar (renamed copy of vpbug)
#   ./build.sh checks [--verify]  run every checks/*.run -> build/checks/*.csv; --verify compares
#                            each output with checks/expected.sha256 and names the runs that differ
#   ./build.sh uitest        native checks of the player files, progression, mmlang.dll, HUD and map
#   ./build.sh music         music checks and renders over orig/mm2aud.ar -> build/music_*.wav
#   ./build.sh gmbank        remake web/gm/FluidR3_GM_drums_piano.sf2 from work/data/gm/FluidR3_GM.sf2
#   ./build.sh nettest       network game checks: engines in one process over a loopback -> build/net_test.txt,
#                            then over real local sockets (both servers, direct UDP, relay) -> build/net_test_sockets.txt
#   ./build.sh netserver [port] [dir]  the dev signalling / relay server (server/signal.js), default 8821;
#                            with a dir it also serves that folder (the page and the game data)
#   ./build.sh hostserver [port] [dir] [--lan]  the engine's own session server (same protocol, no Node)
#   ./build.sh serve [port]  serve dist/ on 127.0.0.1 (default 8820) with HTTP range requests
#   ./build.sh appimage      the desktop app (desktop/, Electron) as a Linux AppImage
#                            -> work/release/CrazyCity_Insanity-<version>-x86_64.AppImage (no MM2 files in it)
#   ./build.sh clean
# SDL3 (both builds) comes from third_party/SDL3-<version>.tar.gz (the SDL source release; not
# committed): built with cmake into build/sdl3/ for the native game, and given to Emscripten's
# SDL3 port (EMCC_LOCAL_PORTS) for the browser build, so nothing is downloaded.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

ENGINE=engine
BUILD=build
DIST=dist
ORIG=orig
BASE_ARCHIVES=(mm2core.ar mm2tex.ar)
# The audio archives take part when present (the page reads them lazily, file by file).
AUDIO_ARCHIVES=(mm2aud.ar mm2audex.ar)
# The music archive (aud/dmusic/); the browser build links it when present.
MUSIC_ARCHIVES=(mm2aud.ar)

# Sources shared by every build (no SDL, no GL).
CORE_SRC=(
  $ENGINE/core/strutil.cpp
  $ENGINE/core/log.cpp
  $ENGINE/core/game_random.cpp
  $ENGINE/vfs/source.cpp
  $ENGINE/vfs/archive.cpp
  $ENGINE/vfs/vfs.cpp
  $ENGINE/vfs/ar_writer.cpp
  $ENGINE/formats/pkg.cpp
  $ENGINE/formats/mtx.cpp
  $ENGINE/formats/image.cpp
  $ENGINE/formats/texture_lookup.cpp
  $ENGINE/formats/car_info.cpp
  $ENGINE/formats/psdl.cpp
  $ENGINE/formats/inst.cpp
  $ENGINE/formats/city_files.cpp
  $ENGINE/formats/bai.cpp
  $ENGINE/formats/aimap.cpp
  $ENGINE/formats/dat_text.cpp
  $ENGINE/formats/banger.cpp
  $ENGINE/formats/csv_table.cpp
  $ENGINE/formats/cinfo.cpp
  $ENGINE/formats/pathset.cpp
  $ENGINE/formats/ped_anim.cpp
  $ENGINE/formats/race_files.cpp
  $ENGINE/city/psdl_geometry.cpp
  $ENGINE/city/city_data.cpp
  $ENGINE/city/street_props.cpp
  $ENGINE/city/level_objects.cpp
  $ENGINE/ai/road_network.cpp
  $ENGINE/ai/traffic_control.cpp
  $ENGINE/ai/traffic.cpp
  $ENGINE/ai/traffic_react.cpp
  $ENGINE/ai/cable_cars.cpp
  $ENGINE/ai/route_search.cpp
  $ENGINE/ai/driver.cpp
  $ENGINE/ai/driver_planner.cpp
  $ENGINE/ai/police.cpp
  $ENGINE/ai/pedestrians.cpp
  $ENGINE/ai/opponents.cpp
  $ENGINE/ai/opponent_check.cpp
  $ENGINE/game/race_catalogue.cpp
  $ENGINE/game/waypoints.cpp
  $ENGINE/game/race.cpp
  $ENGINE/drive/drive_tune.cpp
  $ENGINE/drive/player_controls.cpp
  $ENGINE/drive/camera_rig.cpp
  $ENGINE/drive/drive_session.cpp
  $ENGINE/drive/force_feedback.cpp
  $ENGINE/model/asset_cache.cpp
  $ENGINE/model/parts.cpp
  $ENGINE/model/vehicle.cpp
  $ENGINE/model/light_bar.cpp
  $ENGINE/fx/image_source.cpp
  $ENGINE/fx/damage_textures.cpp
  $ENGINE/fx/car_fx.cpp
  $ENGINE/fx/particles.cpp
  $ENGINE/fx/effects_world.cpp
  $ENGINE/model/ped_model.cpp
  $ENGINE/physics/car_tune.cpp
  $ENGINE/physics/rigid_body.cpp
  $ENGINE/physics/car.cpp
  $ENGINE/physics/trailer.cpp
  $ENGINE/physics/material.cpp
  $ENGINE/physics/bound.cpp
  $ENGINE/physics/city_collision.cpp
  $ENGINE/physics/contacts.cpp
  $ENGINE/physics/object_contacts.cpp
  $ENGINE/physics/capsule_contacts.cpp
  $ENGINE/physics/collision_world.cpp
  $ENGINE/physics/drive_check.cpp
  $ENGINE/physics/damage.cpp
  $ENGINE/physics/traffic_body.cpp
  $ENGINE/physics/box_contacts.cpp
  $ENGINE/physics/scene.cpp
  $ENGINE/physics/loose_bodies.cpp
  $ENGINE/physics/drive_check_multi.cpp
  $ENGINE/formats/mmlang.cpp
  $ENGINE/game/player_files.cpp
  $ENGINE/game/progression.cpp
  $ENGINE/game/profile_store.cpp
  $ENGINE/game/hud.cpp
  $ENGINE/game/hud_map.cpp
  $ENGINE/audio/sound_random.cpp
  $ENGINE/audio/text_reader.cpp
  $ENGINE/audio/wav.cpp
  $ENGINE/audio/sound_system.cpp
  $ENGINE/audio/soft_mixer.cpp
  $ENGINE/audio/sound3d.cpp
  $ENGINE/audio/car_data.cpp
  $ENGINE/audio/audio_world.cpp
  $ENGINE/audio/car_audio.cpp
  $ENGINE/audio/traffic_audio.cpp
  $ENGINE/audio/world_audio.cpp
  $ENGINE/audio/speech.cpp
  $ENGINE/audio/game_audio.cpp
  $ENGINE/net/settings.cpp
  $ENGINE/net/car_state.cpp
  $ENGINE/net/remote_car.cpp
  $ENGINE/net/check_value.cpp
  $ENGINE/net/texts.cpp
  $ENGINE/net/loopback.cpp
  $ENGINE/net/session.cpp
  $ENGINE/net/net_game.cpp
)

# The music module (spec/audio/music.md, spec/formats/directmusic.md): no SDL, no GL.
MUSIC_SRC=(
  $ENGINE/music/dm_files.cpp
  $ENGINE/music/sf2.cpp
  $ENGINE/music/dm_loader.cpp
  $ENGINE/music/synth.cpp
  $ENGINE/music/composer.cpp
  $ENGINE/music/performance.cpp
  $ENGINE/music/music_system.cpp
)

# The engine's own UI (ui/): fonts, text, drawing, layout and widgets; and the front end
# (app/front_end.*: menus, HUD, viewer, multiplayer screens) with its data (app/menu_data.*).
# No SDL and no GL, so cctool's UI checks run them too; the GL backend (ui/gl_backend.cpp) is in
# the browser and native lists. The fonts are compiled in from engine/ui/fonts/ (gen_ui_fonts).
UI_SRC=(
  $ENGINE/core/json_value.cpp
  $ENGINE/ui/font.cpp
  $ENGINE/ui/text.cpp
  $ENGINE/ui/draw.cpp
  $ENGINE/ui/soft_backend.cpp
  $ENGINE/ui/ui.cpp
  $ENGINE/ui/layout.cpp
  $ENGINE/ui/paint.cpp
  $ENGINE/app/menu_data.cpp
  $ENGINE/app/fe_words.cpp
  $ENGINE/app/fe_theme.cpp
  $ENGINE/app/front_end.cpp
  $ENGINE/app/fe_screens.cpp
  $ENGINE/app/fe_net.cpp
  $ENGINE/app/fe_viewer.cpp
)
UI_FONT_GEN=$BUILD/gen/ui_fonts.cpp
# Native networking (sockets, WebSocket client with TLS, the native transport, the embedded session
# server): every native build, never the browser one. Threads; OpenSSL 3 is loaded at run time for
# wss:// (no link-time dependency, so the program runs where it is missing).
NATIVE_NET_SRC=(
  $ENGINE/core/js_value.cpp
  $ENGINE/platform/net_socket.cpp
  $ENGINE/platform/websocket.cpp
  $ENGINE/platform/direct_link.cpp
  $ENGINE/platform/native_transport.cpp
  $ENGINE/platform/host_server.cpp
)
NATIVE_NET_LIBS=(-pthread -ldl)

# SDL3, for both builds.
SDL3_VERSION=3.4.18
SDL3_TARBALL=third_party/SDL3-$SDL3_VERSION.tar.gz
SDL3_SRC=third_party/SDL3-$SDL3_VERSION
SDL3_PREFIX=$BUILD/sdl3/install

# Browser-only sources (platform, renderer, app).
WEB_SRC=(
  $ENGINE/platform/platform_sdl.cpp
  $ENGINE/platform/web_bridge.cpp
  $ENGINE/render/gl_util.cpp
  $ENGINE/render/texture_cache.cpp
  $ENGINE/render/mesh_cache.cpp
  $ENGINE/render/renderer.cpp
  $ENGINE/render/fx_draw.cpp
  $ENGINE/render/camera.cpp
  $ENGINE/ui/gl_backend.cpp
  $ENGINE/city/city.cpp
  $ENGINE/app/app.cpp
  $ENGINE/app/car_view.cpp
  $ENGINE/app/city_view.cpp
  $ENGINE/app/drive_view.cpp
  $ENGINE/app/drive_view_race.cpp
  $ENGINE/app/drive_view_net.cpp
  $ENGINE/app/net_api.cpp
  $ENGINE/platform/web_transport.cpp
  $ENGINE/app/cockpit.cpp
  $ENGINE/app/web_api.cpp
  $ENGINE/app/hud_view.cpp
  $ENGINE/app/ui_api.cpp
  $ENGINE/app/game_api.cpp
  $ENGINE/app/app_services.cpp
  $ENGINE/app/web_host.cpp
  $ENGINE/app/shell_api.cpp
  $ENGINE/platform/web_net_link.cpp
  $ENGINE/app/main_web.cpp
)

# Native-game sources (platform, renderer, app, the native program; app/main_native.cpp is last).
NATIVE_SRC=(
  $ENGINE/platform/platform_sdl.cpp
  $ENGINE/platform/web_bridge.cpp
  $ENGINE/platform/web_transport.cpp
  $ENGINE/render/gl_native.cpp
  $ENGINE/render/gl_util.cpp
  $ENGINE/render/texture_cache.cpp
  $ENGINE/render/mesh_cache.cpp
  $ENGINE/render/renderer.cpp
  $ENGINE/render/fx_draw.cpp
  $ENGINE/render/camera.cpp
  $ENGINE/ui/gl_backend.cpp
  $ENGINE/city/city.cpp
  $ENGINE/app/app.cpp
  $ENGINE/app/car_view.cpp
  $ENGINE/app/city_view.cpp
  $ENGINE/app/drive_view.cpp
  $ENGINE/app/drive_view_race.cpp
  $ENGINE/app/drive_view_net.cpp
  $ENGINE/app/cockpit.cpp
  $ENGINE/app/hud_view.cpp
  $ENGINE/app/ui_api.cpp
  $ENGINE/app/game_api.cpp
  $ENGINE/app/app_services.cpp
  $ENGINE/app/native_host.cpp
  $ENGINE/platform/native_net_link.cpp
  $ENGINE/core/json_read.cpp
  $ENGINE/native/files.cpp
  $ENGINE/native/binding_names.cpp
  $ENGINE/native/overlay.cpp
  $ENGINE/native/png.cpp
  $ENGINE/app/main_native.cpp
)
# C: older glibc versions of a few functions, so the program starts on glibc 2.34 and later.
NATIVE_C_SRC=(
  $ENGINE/native/glibc_compat.c
)

# -ffp-contract=off: no fused multiply-add, so float32 results match the physics spec.
CXXFLAGS_COMMON=(-std=c++17 -I"$ENGINE" -Wall -Wextra -Wno-unused-parameter -ffp-contract=off)

# build/gen/ui_fonts.cpp: the font files of engine/ui/fonts/ as byte arrays (ui/font_data.h).
gen_ui_fonts() {
  mkdir -p "$BUILD/gen"
  if [[ ! -f "$UI_FONT_GEN" || -n "$(find "$ENGINE/ui/fonts" -newer "$UI_FONT_GEN" -print -quit)" || "$0" -nt "$UI_FONT_GEN" ]]; then
    echo "== fonts -> $UI_FONT_GEN"
    python3 - "$UI_FONT_GEN" "$ENGINE"/ui/fonts/*.ttf <<'PY'
import os, sys
out, files = sys.argv[1], sorted(sys.argv[2:])
lines = ['// Generated by build.sh from engine/ui/fonts/ (IBM Plex, SIL Open Font License 1.1).',
         '#include "ui/font_data.h"', 'namespace cc {', 'namespace ui {', 'namespace {']
for i, f in enumerate(files):
    data = open(f, 'rb').read()
    lines.append('const unsigned char font%d[%d] = {' % (i, len(data)))
    for k in range(0, len(data), 32):
        lines.append(','.join(str(b) for b in data[k:k + 32]) + ',')
    lines.append('};')
lines.append('const EmbeddedFont fonts[] = {')
for i, f in enumerate(files):
    lines.append('    {"%s", font%d, sizeof font%d},' % (os.path.basename(f), i, i))
lines += ['};', '}  // namespace', 'const EmbeddedFont* embeddedFonts(int& count) {',
          '    count = int(sizeof fonts / sizeof fonts[0]);', '    return fonts;', '}', '}  // namespace ui', '}  // namespace cc', '']
tmp = out + '.tmp'
open(tmp, 'w').write('\n'.join(lines))
os.replace(tmp, out)
PY
  fi
}

build_tool() {
  gen_ui_fonts
  mkdir -p "$BUILD/native"
  local out="$BUILD/native/cctool"
  local newest
  newest=$(find "$ENGINE" -name '*.cpp' -o -name '*.h' | xargs ls -t | head -1)
  if [[ ! -x "$out" || "$newest" -nt "$out" || "$0" -nt "$out" || "$UI_FONT_GEN" -nt "$out" ]]; then
    echo "== native: g++ -> $out"
    # Built to a temporary name and moved into place only on success; a failed build removes the
    # old binary, so nothing goes on with a stale cctool.
    rm -f "$out" "$out.tmp"
    if ! g++ "${CXXFLAGS_COMMON[@]}" -O2 "${CORE_SRC[@]}" "${MUSIC_SRC[@]}" "${UI_SRC[@]}" "$UI_FONT_GEN" "${NATIVE_NET_SRC[@]}" $ENGINE/tools/ui_screens.cpp $ENGINE/tools/cctool.cpp $ENGINE/tools/test_ai.cpp \
      $ENGINE/tools/test_effects.cpp $ENGINE/tools/test_peds.cpp $ENGINE/tools/ped_tool.cpp $ENGINE/tools/ui_test.cpp \
      $ENGINE/tools/test_audio.cpp $ENGINE/tools/sound_tool.cpp $ENGINE/tools/traffic_tool.cpp $ENGINE/tools/music_tool.cpp $ENGINE/tools/gm_trim.cpp \
      $ENGINE/tools/test_races.cpp $ENGINE/tools/race_tool.cpp $ENGINE/tools/test_net.cpp $ENGINE/tools/test_net_sockets.cpp \
      $ENGINE/tools/net_tool.cpp -lz "${NATIVE_NET_LIBS[@]}" -o "$out.tmp"; then
      rm -f "$out.tmp"
      echo "== ERROR: cctool failed to build (no $out)" >&2
      exit 1
    fi
    mv "$out.tmp" "$out"
  fi
}

base_archive_paths() {
  local a
  for a in "${BASE_ARCHIVES[@]}"; do
    if [[ ! -f "$ORIG/$a" ]]; then
      echo "missing $ORIG/$a (copy the MM2 game files into orig/)" >&2
      exit 1
    fi
    echo "$ORIG/$a"
  done
}

link_game_data() {
  mkdir -p "$DIST/game"
  local a
  for a in "${BASE_ARCHIVES[@]}"; do
    ln -sfn "../../$ORIG/$a" "$DIST/game/$a"
  done
  # The player's own words and profiles for local testing (the page reads them like a visitor's
  # own MM2 folder; they are never part of a public build).
  if [[ -f "$ORIG/mmlang.dll" ]]; then ln -sfn "../../$ORIG/mmlang.dll" "$DIST/game/mmlang.dll"; fi
  if [[ -d "$ORIG/players" ]]; then ln -sfn "../../$ORIG/players" "$DIST/game/players"; fi
  for a in "${AUDIO_ARCHIVES[@]}"; do
    if [[ -f "$ORIG/$a" ]]; then ln -sfn "../../$ORIG/$a" "$DIST/game/$a"; fi
  done
  # A developer's own General MIDI bank next to the game files (never shipped).
  rm -f "$DIST/game/gm.dls"
  if [[ -f "$ORIG/gm.dls" ]]; then ln -sfn "../../$ORIG/gm.dls" "$DIST/game/gm.dls"; fi
}

audio_archive_paths() {
  local a
  for a in "${AUDIO_ARCHIVES[@]}"; do
    if [[ -f "$ORIG/$a" ]]; then echo "$ORIG/$a"; fi
  done
}

write_game_index() {
  # dist/game/index.json lists the archives present (base archives first, then mods by name).
  (
    cd "$DIST/game"
    local first=1
    printf '{"archives":['
    for f in $(ls -1 | grep -i '\.ar$' | sort -f); do
      if [[ $first -eq 0 ]]; then printf ','; fi
      first=0
      printf '{"name":"%s","size":%s}' "$f" "$(stat -L -c %s "$f")"
    done
    printf ']'
    if [[ -f mmlang.dll ]]; then printf ',"mmlang":true'; fi
    if [[ -d players ]]; then
      printf ',"players":['
      first=1
      for f in $(cd players && find -L . -type f | sed 's|^\./||' | sort); do
        if [[ $first -eq 0 ]]; then printf ','; fi
        first=0
        printf '"%s"' "$f"
      done
      printf ']'
    fi
    if [[ -e gm.dls ]]; then printf ',"gm":{"name":"gm.dls","size":%s}' "$(stat -L -c %s gm.dls)"; fi
    printf '}\n'
  ) > "$DIST/game/index.json.tmp"
  mv "$DIST/game/index.json.tmp" "$DIST/game/index.json"
}

make_testmod() {
  build_tool
  mkdir -p "$DIST/game"
  echo "== test mod: renamed copy of vpbug -> $DIST/game/zz_testmod_vpmodbug.ar"
  # shellcheck disable=SC2046
  "$BUILD/native/cctool" mkcarmod --deflate "$DIST/game/zz_testmod_vpmodbug.ar" \
    vpbug vpmodbug "Test Mod Beetle" $(base_archive_paths)
}

build_web() {
  local mode="${1:-release}"
  if [[ -z "${EMSDK:-}" ]]; then
    if [[ -f "$HOME/emsdk/emsdk_env.sh" ]]; then
      # shellcheck disable=SC1091
      source "$HOME/emsdk/emsdk_env.sh" > /dev/null 2>&1
    fi
  fi
  command -v em++ > /dev/null || { echo "em++ not found (install emsdk in ~/emsdk)"; exit 1; }
  mkdir -p "$DIST" "$BUILD/web"
  local opt=(-O2)
  local link_extra=()
  if [[ "$mode" == "debug" ]]; then
    opt=(-O0 -g)
    link_extra=(-sASSERTIONS=2 -sSAFE_HEAP=1)
  fi
  sdl3_web_port
  local flags=("${CXXFLAGS_COMMON[@]}" "${opt[@]}" -fno-exceptions -sUSE_SDL=3 -sUSE_ZLIB=1)

  # Compile each source to an object (only when changed).
  local objs=() src obj
  gen_ui_fonts
  for src in "${CORE_SRC[@]}" "${MUSIC_SRC[@]}" "${UI_SRC[@]}" "$UI_FONT_GEN" "${WEB_SRC[@]}"; do
    obj="$BUILD/web/$mode/${src//\//_}.o"
    objs+=("$obj")
    mkdir -p "$(dirname "$obj")"
    if [[ ! -f "$obj" || "$src" -nt "$obj" || -n "$(find "$ENGINE" -name '*.h' -newer "$obj" -print -quit)" ]]; then
      echo "  em++ $src"
      em++ "${flags[@]}" -c "$src" -o "$obj"
    fi
  done

  echo "== web: link -> $DIST/cci.js"
  em++ "${opt[@]}" "${objs[@]}" -o "$DIST/cci.js" \
    -sUSE_SDL=3 -sUSE_ZLIB=1 \
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sSTACK_SIZE=1MB \
    -sENVIRONMENT=web \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAPF32 \
    '-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$stringToNewUTF8,$stringToUTF8' \
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
    "${link_extra[@]}"
  sdl3_web_check

  cp web/index.html web/style.css web/app.js web/audio.js web/net.js "$DIST/"
  rm -f "$DIST/ui.js" "$DIST/ui.css" "$DIST/mp.js" "$DIST/mp.css"
  # The UI's fonts are compiled in; their licence goes with them.
  mkdir -p "$DIST/licences"
  cp engine/ui/fonts/OFL.txt "$DIST/licences/IBM-Plex-OFL.txt"
  # The bundled General MIDI drum kit and piano (FluidR3, MIT) with its notice.
  mkdir -p "$DIST/gm"
  cp web/gm/FluidR3_GM_drums_piano.sf2 web/gm/COPYING-FluidR3.txt "$DIST/gm/"
  link_game_data
  make_testmod
  write_game_index
  echo "== done: serve with  ./build.sh serve 8820  (range requests; python3 -m http.server works too)"
}

# ---- SDL3 and the native game

sdl3_source() {
  if [[ ! -f "$SDL3_SRC/CMakeLists.txt" ]]; then
    [[ -f "$SDL3_TARBALL" ]] || {
      echo "missing $SDL3_TARBALL (the SDL $SDL3_VERSION source release, github.com/libsdl-org/SDL; not committed)" >&2
      exit 1
    }
    tar xzf "$SDL3_TARBALL" -C third_party
  fi
}

# Emscripten's SDL3 port built from the local SDL source, every time (its own download, a
# different SDL version, is never used): EMCC_LOCAL_PORTS points it at third_party, and since emcc
# keeps an older copy when that copy looks newer, a cached port whose REVISION.txt is not the local
# source's is cleared first (the port's source and its built library; emcc copies and rebuilds).
sdl3_cache_revision() {
  local d
  for d in "$1"/ports/sdl3/SDL-*/; do
    if [[ -f "$d/REVISION.txt" ]]; then cat "$d/REVISION.txt"; return; fi
  done
}
sdl3_web_port() {
  sdl3_source
  export EMCC_LOCAL_PORTS="sdl3=$ROOT/$SDL3_SRC"
  local cache want have
  cache="$(em-config CACHE)"
  want="$(cat "$SDL3_SRC/REVISION.txt")"
  have="$(sdl3_cache_revision "$cache")"
  if [[ "$have" != "$want" ]]; then
    echo "== SDL $SDL3_VERSION: Emscripten's sdl3 port (${have:-not from the local source}) cleared for $SDL3_SRC"
    embuilder clear sdl3 > /dev/null
    rm -rf "$cache/ports/sdl3"
  fi
}
# After the link: the port emcc used must be the local source (fails the build otherwise).
sdl3_web_check() {
  local cache want have h v=""
  cache="$(em-config CACHE)"
  want="$(cat "$SDL3_SRC/REVISION.txt")"
  have="$(sdl3_cache_revision "$cache")"
  h="$cache/sysroot/include/SDL3/SDL_version.h"
  if [[ -f "$h" ]]; then
    v="$(sed -n 's/^#define SDL_MAJOR_VERSION *\([0-9]*\).*/\1/p' "$h").$(sed -n 's/^#define SDL_MINOR_VERSION *\([0-9]*\).*/\1/p' "$h").$(sed -n 's/^#define SDL_MICRO_VERSION *\([0-9]*\).*/\1/p' "$h")"
  fi
  if [[ "$have" != "$want" || "$v" != "$SDL3_VERSION" ]]; then
    echo "SDL mismatch: the web build used '${have:-?}' (headers ${v:-?}), not $want from $SDL3_SRC" >&2
    exit 1
  fi
  echo "== SDL: web build on $want (as the native game)"
}

# SDL3 as a static library (video: X11, Wayland, offscreen; audio: PipeWire, PulseAudio, ALSA,
# all loaded when present; joysticks, haptics, the file dialog) in build/sdl3/install.
build_sdl3() {
  if [[ -f "$SDL3_PREFIX/lib/libSDL3.a" ]]; then return; fi
  sdl3_source
  command -v cmake > /dev/null || { echo "cmake not found" >&2; exit 1; }
  command -v ninja > /dev/null || { echo "ninja not found" >&2; exit 1; }
  echo "== SDL $SDL3_VERSION: cmake -> $SDL3_PREFIX"
  mkdir -p "$BUILD/sdl3"
  cmake -S "$SDL3_SRC" -B "$BUILD/sdl3/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$ROOT/$SDL3_PREFIX" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_GPU=OFF -DSDL_VULKAN=OFF \
    -DSDL_CAMERA=OFF -DSDL_X11_XSCRNSAVER=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_OSS=OFF \
    -DSDL_LIBURING=OFF -DSDL_KMSDRM=OFF > "$BUILD/sdl3/cmake.log" 2>&1 || {
      tail -20 "$BUILD/sdl3/cmake.log"
      exit 1
    }
  ninja -C "$BUILD/sdl3/build" > "$BUILD/sdl3/build.log" 2>&1 || { tail -20 "$BUILD/sdl3/build.log"; exit 1; }
  cmake --install "$BUILD/sdl3/build" > "$BUILD/sdl3/install.log" 2>&1
}

native_version() {
  local sha
  sha=$(git -C "$ROOT" rev-parse --short HEAD 2> /dev/null || echo local)
  echo "$(date +%Y.%m.%d)-$sha"
}

build_native() {
  local mode="${1:-release}"
  build_sdl3
  gen_ui_fonts
  local out="$BUILD/native/crazycity" opt="-O2"
  if [[ "$mode" == "debug" ]]; then
    out="$BUILD/native/crazycity-debug"
    opt="-O0 -g3 -D_GLIBCXX_ASSERTIONS"
  fi
  local version
  version=$(native_version)
  mkdir -p "$BUILD/native/obj/$mode"
  # A ninja file for the native game (header dependencies and parallel compiles); only
  # app/main_native.cpp gets the version, so a new commit recompiles one file.
  local nf="$BUILD/native/$mode.ninja"
  {
    echo "builddir = $BUILD/native/obj/$mode"
    echo "cflags = ${CXXFLAGS_COMMON[*]} $opt -I$SDL3_PREFIX/include"
    # libstdc++ static (any distribution's will do), libgcc and glibc from the system.
    echo "ldflags = $SDL3_PREFIX/lib/libSDL3.a -lz -lm -ldl -pthread -static-libstdc++ -Wl,--as-needed"
    echo "rule cxx"
    echo "  command = g++ \$cflags \$extra -MMD -MF \$out.d -c \$in -o \$out"
    echo "  depfile = \$out.d"
    echo "  deps = gcc"
    echo "  description = g++ \$in"
    echo "rule cc"
    echo "  command = gcc -std=gnu17 -O2 -Wall -Wextra -MMD -MF \$out.d -c \$in -o \$out"
    echo "  depfile = \$out.d"
    echo "  deps = gcc"
    echo "  description = gcc \$in"
    echo "rule link"
    echo "  command = g++ \$in -o \$out \$ldflags"
    echo "  description = link \$out"
    local objs=() src obj
    for src in "${CORE_SRC[@]}" "${MUSIC_SRC[@]}" "${UI_SRC[@]}" "$UI_FONT_GEN" "${NATIVE_NET_SRC[@]}" "${NATIVE_SRC[@]}"; do
      obj="$BUILD/native/obj/$mode/${src//\//_}.o"
      objs+=("$obj")
      echo "build $obj: cxx $src"
      if [[ "$src" == *main_native.cpp ]]; then echo "  extra = -DCC_VERSION=\\\"$version\\\""; fi
    done
    for src in "${NATIVE_C_SRC[@]}"; do
      obj="$BUILD/native/obj/$mode/${src//\//_}.o"
      objs+=("$obj")
      echo "build $obj: cc $src"
    done
    echo "build $out: link ${objs[*]} | $SDL3_PREFIX/lib/libSDL3.a"
    echo "default $out"
  } > "$nf.tmp"
  if ! cmp -s "$nf.tmp" "$nf"; then mv "$nf.tmp" "$nf"; else rm -f "$nf.tmp"; fi
  echo "== native ($mode): ninja -> $out"
  ninja -j "${JOBS:-12}" -f "$nf"
  # The bundled General MIDI drum kit and piano (FluidR3, MIT) next to the program.
  mkdir -p "$BUILD/native/gm"
  cp -u web/gm/FluidR3_GM_drums_piano.sf2 web/gm/COPYING-FluidR3.txt "$BUILD/native/gm/"
  if [[ -d data/fonts ]]; then mkdir -p "$BUILD/native/fonts" && cp -ru data/fonts/. "$BUILD/native/fonts/"; fi
}

run_selftest_native() {
  build_native release
  local frames="${1:-240}"
  rm -rf "$BUILD/selftest"
  mkdir -p "$BUILD/selftest"
  "$BUILD/native/crazycity" --selftest "$BUILD/native/selftest.png" --frames "$frames" --mm2 "$ORIG" \
    --config-dir "$BUILD/selftest/config" --data-dir "$BUILD/selftest/data"
}

make_appimage_native() {
  build_native release
  local version appdir out tool runtime
  version=$(native_version)
  appdir="$BUILD/appimage/AppDir"
  out="work/release/CrazyCity_Insanity-native-$version-x86_64.AppImage"
  # The AppImage tool and runtime: third_party/appimage/ (shared with the desktop app's packaging),
  # else third_party/.
  tool=third_party/appimage/appimagetool-x86_64.AppImage
  runtime=third_party/appimage/appimage-runtime-x86_64
  if [[ ! -x "$tool" || ! -f "$runtime" ]]; then
    tool=third_party/appimagetool-x86_64.AppImage
    runtime=third_party/appimage-runtime-x86_64
  fi
  [[ -x "$tool" && -f "$runtime" ]] || { echo "missing appimagetool-x86_64.AppImage or appimage-runtime-x86_64 in third_party/appimage/ or third_party/ (not committed)" >&2; exit 1; }
  rm -rf "$appdir"
  mkdir -p "$appdir/usr/bin" "$appdir/usr/share/doc/crazycity-insanity" "$appdir/usr/share/applications" \
    "$appdir/usr/share/icons/hicolor/256x256/apps" "$appdir/usr/share/icons/hicolor/scalable/apps"
  cp "$BUILD/native/crazycity" "$appdir/usr/bin/crazycity"
  strip "$appdir/usr/bin/crazycity"
  cp -r "$BUILD/native/gm" "$appdir/usr/bin/gm"
  if [[ -d "$BUILD/native/fonts" ]]; then cp -r "$BUILD/native/fonts" "$appdir/usr/bin/fonts"; fi
  # Licences: FluidR3 (MIT), SDL (zlib) and the UI's IBM Plex fonts (OFL 1.1), which the program carries.
  cp web/gm/COPYING-FluidR3.txt "$appdir/usr/share/doc/crazycity-insanity/"
  cp engine/ui/fonts/OFL.txt "$appdir/usr/share/doc/crazycity-insanity/IBM-Plex-OFL.txt"
  cp "$SDL3_SRC/LICENSE.txt" "$appdir/usr/share/doc/crazycity-insanity/LICENSE-SDL3.txt"
  # The desktop entry and the icon: another part's desktop/ when there is one, else ours.
  local desk=packaging/linux/crazycity-insanity.desktop icon_svg=packaging/linux/crazycity-insanity.svg
  if [[ -f desktop/crazycity-insanity.desktop ]]; then desk=desktop/crazycity-insanity.desktop; fi
  if [[ -f desktop/crazycity-insanity.svg ]]; then icon_svg=desktop/crazycity-insanity.svg; fi
  cp "$desk" "$appdir/crazycity-insanity.desktop"
  cp "$desk" "$appdir/usr/share/applications/crazycity-insanity.desktop"
  if [[ -f desktop/crazycity-insanity.png ]]; then
    cp desktop/crazycity-insanity.png "$appdir/crazycity-insanity.png"
  else
    rsvg-convert -w 256 -h 256 "$icon_svg" -o "$appdir/crazycity-insanity.png"
  fi
  cp "$icon_svg" "$appdir/usr/share/icons/hicolor/scalable/apps/crazycity-insanity.svg"
  cp "$appdir/crazycity-insanity.png" "$appdir/usr/share/icons/hicolor/256x256/apps/crazycity-insanity.png"
  ln -s crazycity-insanity.png "$appdir/.DirIcon"
  cat > "$appdir/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/crazycity" "$@"
APPRUN
  chmod +x "$appdir/AppRun"
  mkdir -p work/release
  rm -f "$out"
  echo "== AppImage -> $out"
  ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream --runtime-file "$runtime" "$appdir" "$out" > "$BUILD/appimage/appimagetool.log" 2>&1 || {
    tail -20 "$BUILD/appimage/appimagetool.log"
    exit 1
  }
  ls -l "$out"
  # The newest glibc symbol version the program needs (its oldest supported glibc).
  objdump -T "$BUILD/native/crazycity" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1 | sed 's/^/glibc baseline: /'
}

run_test() {
  build_tool
  local archives=() audio=()
  mapfile -t archives < <(base_archive_paths)
  mapfile -t audio < <(audio_archive_paths)
  archives+=("${audio[@]}")
  # Mod archives already made for the web build take part too.
  if [[ -d "$DIST/game" ]]; then
    local f
    for f in "$DIST"/game/*.ar; do
      [[ -e "$f" ]] || continue
      local b
      b=$(basename "$f")
      case " ${BASE_ARCHIVES[*]} ${AUDIO_ARCHIVES[*]} " in *" $b "*) continue ;; esac
      archives+=("$f")
    done
  fi
  "$BUILD/native/cctool" test "${archives[@]}"
  run_uitest
}

run_uitest() {
  build_tool
  # shellcheck disable=SC2046
  "$BUILD/native/cctool" uitest "$ORIG" $(base_archive_paths)
  # The engine-drawn menus (app/front_end.h): keys, clicks, slider numbers, PNG snapshots.
  # shellcheck disable=SC2046
  "$BUILD/native/cctool" uiscreens "$ORIG" "$BUILD/uiscreens" $(base_archive_paths)
}

run_checks() {
  # Old outputs go first, before anything can fail: a failed build or run never leaves CSVs from
  # an earlier run behind to be compared.
  rm -rf "$BUILD/checks"
  mkdir -p "$BUILD/checks"
  local fail=""
  build_tool || fail="cctool failed to build"
  if [[ -z "$fail" ]]; then
    local archives
    archives="$(base_archive_paths)" || fail="the MM2 archives are missing"
    # shellcheck disable=SC2086
    if [[ -z "$fail" ]] && ! "$BUILD/native/cctool" drives checks "$BUILD/checks" $archives; then
      fail="a check run failed (cctool drives exit status)"
    fi
  fi
  if [[ -z "$fail" ]]; then
    # Every checks/*.run must have given a non-empty CSV.
    local run name missing=0
    for run in checks/*.run; do
      name="$(basename "$run" .run)"
      [[ -s "$BUILD/checks/$name.csv" ]] || { echo "  missing or empty: $BUILD/checks/$name.csv" >&2; missing=$((missing + 1)); }
    done
    [[ $missing -eq 0 ]] || fail="$missing check run(s) wrote no CSV"
  fi
  if [[ -n "$fail" ]]; then
    rm -rf "$BUILD/checks"
    echo "== ERROR: checks failed: $fail; $BUILD/checks removed (nothing to compare)" >&2
    exit 1
  fi
  echo "== checks: $(ls "$BUILD"/checks/*.csv | wc -l) runs written to $BUILD/checks"
  if [[ "${1:-}" == "--verify" ]]; then verify_checks; fi
}

# ./build.sh checks --verify: every run's CSV against checks/expected.sha256 (the SHA-256 of each
# output as it must be); names each run that differs, is missing or has no expected sum, and exits
# 1 when any does.
verify_checks() {
  local expected=checks/expected.sha256 sum name want have bad=0 ok=0
  [[ -f "$expected" ]] || { echo "== ERROR: $expected missing" >&2; exit 1; }
  declare -A seen=()
  while read -r want name; do
    [[ -z "$name" ]] && continue
    seen["$name"]=1
    if [[ ! -f "$BUILD/checks/$name" ]]; then
      echo "  MISSING  $name" >&2
      bad=$((bad + 1))
      continue
    fi
    have=$(sha256sum "$BUILD/checks/$name" | cut -d' ' -f1)
    if [[ "$have" == "$want" ]]; then
      ok=$((ok + 1))
    else
      echo "  DIFFERS  $name" >&2
      bad=$((bad + 1))
    fi
  done < "$expected"
  for sum in "$BUILD"/checks/*.csv; do
    name=$(basename "$sum")
    if [[ -z "${seen[$name]:-}" ]]; then
      echo "  NO SUM   $name (not in $expected)" >&2
      bad=$((bad + 1))
    fi
  done
  if [[ $bad -ne 0 ]]; then
    echo "== ERROR: verify: $ok run(s) match $expected, $bad do not (listed above)" >&2
    exit 1
  fi
  echo "== verify: all $ok runs match $expected"
}

serve() {
  local port="${1:-8820}"
  echo "== serving $DIST on http://127.0.0.1:$port/ (Ctrl-C stops)"
  python3 - "$port" "$DIST" <<'PY'
import functools, http.server, os, re, sys
port, root = int(sys.argv[1]), sys.argv[2]
class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()
    def do_GET(self):
        rng = self.headers.get('Range')
        path = self.translate_path(self.path)
        m = re.match(r'bytes=(\d*)-(\d*)$', rng.strip()) if rng else None
        if not m or not os.path.isfile(path):
            return super().do_GET()
        size = os.path.getsize(path)
        a, b = m.group(1), m.group(2)
        if a == '':
            start, end = max(0, size - int(b or 0)), size - 1
        else:
            start, end = int(a), (int(b) if b else size - 1)
        end = min(end, size - 1)
        if start > end:
            self.send_response(416)
            self.send_header('Content-Range', 'bytes */%d' % size)
            self.end_headers()
            return
        self.send_response(206)
        self.send_header('Content-Type', self.guess_type(path))
        self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, end, size))
        self.send_header('Content-Length', str(end - start + 1))
        self.end_headers()
        with open(path, 'rb') as f:
            f.seek(start)
            left = end - start + 1
            while left > 0:
                chunk = f.read(min(left, 1 << 20))
                if not chunk:
                    break
                self.wfile.write(chunk)
                left -= len(chunk)
    def log_message(self, *args):
        pass
http.server.ThreadingHTTPServer(('127.0.0.1', port), functools.partial(Handler, directory=root)).serve_forever()
PY
}

run_nettest() {
  build_tool
  # shellcheck disable=SC2046
  "$BUILD/native/cctool" testnet "$BUILD/net_test.txt" $(base_archive_paths)
}

net_server() {
  local port="${1:-8821}"
  if [[ -n "${2:-}" ]]; then
    node server/signal.js --port "$port" --static "$2"
  else
    node server/signal.js --port "$port"
  fi
}

host_server() {
  build_tool
  local port="${1:-8821}" args=()
  args=(--port "$port")
  if [[ -n "${2:-}" && "${2:-}" != "--lan" ]]; then args+=(--static "$2"); fi
  if [[ " $* " == *" --lan "* ]]; then args+=(--lan); fi
  "$BUILD/native/cctool" netserver "${args[@]}"
}

run_music() {
  build_tool
  local a
  for a in "${MUSIC_ARCHIVES[@]}"; do
    [[ -f "$ORIG/$a" ]] || { echo "missing $ORIG/$a (copy the MM2 game files into orig/)" >&2; exit 1; }
  done
  mkdir -p "$BUILD"
  # The player's own General MIDI bank if one was put next to the game files (never shipped),
  # else the bundled FluidR3 drum kit and piano.
  local gm=(--gm web/gm/FluidR3_GM_drums_piano.sf2)
  for a in "$ORIG/gm.dls" "$ORIG/GM.DLS"; do
    if [[ -f "$a" ]]; then gm=(--gm "$a"); break; fi
  done
  "$BUILD/native/cctool" music all "$BUILD" "${gm[@]}" "$ORIG/${MUSIC_ARCHIVES[0]}"
}

make_gmbank() {
  build_tool
  local src=work/data/gm/FluidR3_GM.sf2
  [[ -f "$src" ]] || { echo "missing $src (the full Fluid R3 GM SoundFont; never committed)" >&2; exit 1; }
  "$BUILD/native/cctool" gmtrim "$src" web/gm/FluidR3_GM_drums_piano.sf2 22050
}

# ------------------------------------------------------------------ the desktop app (AppImage)
#
# Electron 38 (its dist folder) in third_party/electron and appimagetool + its runtime in
# third_party/appimage (gitignored; nothing is downloaded). The AppDir holds Electron, the app
# (desktop/*.js, server/signal.js, the web build without dist/game), the icons, the .desktop entry,
# AppRun and the notices. The player's MM2 folder is chosen when the app first starts.
APP_ID=crazycity-insanity

make_appimage() {
  local electron=third_party/electron
  local tool=third_party/appimage/appimagetool-x86_64.AppImage
  local runtime=third_party/appimage/appimage-runtime-x86_64
  [[ -x "$electron/electron" ]] || { echo "missing $electron/electron (Electron 38's dist folder; see README)" >&2; exit 1; }
  [[ -f "$tool" && -f "$runtime" ]] || { echo "missing $tool or $runtime (see README)" >&2; exit 1; }
  command -v rsvg-convert > /dev/null || { echo "rsvg-convert not found (librsvg2-bin)" >&2; exit 1; }
  local ver
  ver=$(node -p "require('./desktop/package.json').version")
  build_web release

  local appdir="$BUILD/appimage/CrazyCity_Insanity.AppDir"
  local lib="$appdir/usr/lib/$APP_ID"
  local app="$lib/resources/app"
  local doc="$appdir/usr/share/doc/$APP_ID"
  echo "== appimage: AppDir -> $appdir"
  rm -rf "$appdir"
  mkdir -p "$lib" "$doc" "$appdir/usr/share/applications"
  # Electron, renamed; its default app and setuid helper are of no use here.
  cp -a "$electron/." "$lib/"
  mv "$lib/electron" "$lib/$APP_ID"
  rm -f "$lib/resources/default_app.asar" "$lib/chrome-sandbox"
  mv "$lib/LICENSE" "$doc/LICENSE.electron"
  mv "$lib/LICENSES.chromium.html" "$doc/LICENSES.chromium.html"
  # The app: the main process, the page's preload, the server and the web build (never dist/game).
  mkdir -p "$app/desktop" "$app/server" "$app/web"
  cp desktop/*.js "$app/desktop/"
  rsvg-convert -w 256 -h 256 desktop/icon.svg -o "$app/desktop/icon.png"
  cp server/signal.js "$app/server/"
  node -e "const p = require('./desktop/package.json'); p.main = 'desktop/main.js'; process.stdout.write(JSON.stringify(p, null, 2) + '\n');" > "$app/package.json"
  tar -C "$DIST" --exclude=./game -cf - . | tar -C "$app/web" -xf -
  cp web/gm/COPYING-FluidR3.txt "$doc/"
  cat > "$doc/README.txt" <<EOF
CrazyCity Insanity $ver

An original driving game on Midtown Madness 2's rules. It plays with the player's own copy of
Midtown Madness 2: the app asks for the MM2 folder on its first start and reads the game's files
from there. No Midtown Madness 2 file is part of this AppImage.

Settings: ~/.config/$APP_ID/ (config.json; the drivers, records and options live in the same folder).

Notices: LICENSE.electron (Electron, MIT), LICENSES.chromium.html (Chromium and the libraries in
Electron), COPYING-FluidR3.txt (the bundled General MIDI drum kit and piano, MIT).
EOF
  # Icons, the .desktop entry and AppRun.
  local s
  for s in 16 24 32 48 64 128 256 512; do
    mkdir -p "$appdir/usr/share/icons/hicolor/${s}x${s}/apps"
    rsvg-convert -w $s -h $s desktop/icon.svg -o "$appdir/usr/share/icons/hicolor/${s}x${s}/apps/$APP_ID.png"
  done
  mkdir -p "$appdir/usr/share/icons/hicolor/scalable/apps"
  cp desktop/icon.svg "$appdir/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"
  cp "$appdir/usr/share/icons/hicolor/256x256/apps/$APP_ID.png" "$appdir/$APP_ID.png"
  ln -s "$APP_ID.png" "$appdir/.DirIcon"
  sed "s/^X-AppImage-Version=.*/X-AppImage-Version=$ver/" desktop/appimage/$APP_ID.desktop > "$appdir/$APP_ID.desktop"
  cp "$appdir/$APP_ID.desktop" "$appdir/usr/share/applications/"
  cp desktop/appimage/AppRun "$appdir/AppRun"
  chmod +x "$appdir/AppRun"

  # Nothing of MM2 may be in it: no archives, mmlang.dll, players files, gm.dls, executables or
  # links out of the AppDir (dist/game is a folder of links to orig/).
  local bad
  bad=$(
    find "$appdir" \( -iname '*.ar' -o -iname '*.dll' -o -iname '*.exe' -o -iname 'gm.dls' -o -iname 'players.dir' \
      -o -iname '*.sav' -o -iname '*.cfg' -o -ipath '*/players/*' -o -path "$app/web/game*" \) -print
    find "$appdir" -type l ! -path "$appdir/.DirIcon" -print
  )
  if [[ -n "$bad" ]]; then
    echo "refusing to pack: files that must not ship are in the AppDir:" >&2
    echo "$bad" >&2
    exit 1
  fi

  mkdir -p work/release
  local out="work/release/CrazyCity_Insanity-$ver-x86_64.AppImage"
  rm -f "$out"
  echo "== appimage: appimagetool -> $out"
  ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream --runtime-file "$runtime" "$appdir" "$out"
  chmod +x "$out"
  echo "== done: $out ($(du -h "$out" | cut -f1))"
}

case "${1:-}" in
  web) build_web "${2:-release}" ;;
  native) build_native "${2:-release}" ;;
  selftest-native) run_selftest_native "${2:-240}" ;;
  appimage-native) make_appimage_native ;;
  test) run_test ;;
  tool) build_tool ;;
  testmod) make_testmod; write_game_index ;;
  checks) run_checks "${2:-}" ;;
  uitest) run_uitest ;;
  serve) serve "${2:-8820}" ;;
  music) run_music ;;
  gmbank) make_gmbank ;;
  nettest) run_nettest ;;
  netserver) net_server "${2:-}" "${3:-}" ;;
  hostserver) shift; host_server "$@" ;;
  appimage) make_appimage ;;
  clean) rm -rf "$BUILD" "$DIST" ;;
  *)
    sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
