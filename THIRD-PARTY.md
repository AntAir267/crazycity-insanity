# Third-party components

The [Unlicense](LICENSE) covers this project's own code and documentation. The parts below come
from others and keep their own licences. No Midtown Madness 2 files are part of this project: the
game reads them from the player's own copy.

| Component | Where it is used | Licence | Notice |
|---|---|---|---|
| **FluidR3 GM** SoundFont (a drum kit and piano trimmed from it), Copyright (c) 2000-2002, 2008 Frank Wen | `web/gm/FluidR3_GM_drums_piano.sf2`: the music's General MIDI instruments when the player has no `gm.dls`; shipped with the browser build and in both AppImages | MIT | [`web/gm/COPYING-FluidR3.txt`](web/gm/COPYING-FluidR3.txt) |
| **IBM Plex Sans** and **IBM Plex Mono**, Copyright 2017-2018 IBM Corporation | `engine/ui/fonts/*.ttf`: the menus' and HUD's fonts, embedded in both builds | SIL Open Font License 1.1 | [`engine/ui/fonts/OFL.txt`](engine/ui/fonts/OFL.txt) (shipped as `licences/IBM-Plex-OFL.txt` with the page and `usr/share/doc/crazycity-insanity/IBM-Plex-OFL.txt` in the native AppImage) |
| **SDL 3**, Copyright (C) 1997-2026 Sam Lantinga | not in the repository: built from the SDL 3.4.18 source release in `third_party/`; statically linked into the native game, compiled into the browser build through Emscripten's SDL port | zlib | `LICENSE.txt` in the SDL source (copied into the native AppImage) |
| **zlib**, Copyright (C) Jean-loup Gailly and Mark Adler | the native game links the system's zlib; the browser build compiles Emscripten's zlib port | zlib | the zlib source |
| **Emscripten** runtime and system libraries | compiled into the browser build (`cci.js`, `cci.wasm`) | MIT / University of Illinois/NCSA | the Emscripten distribution |
| **Electron** and **Chromium** | only in the optional desktop AppImage (`./build.sh appimage`, the `desktop/` shell) | Electron: MIT; Chromium: BSD-style and others | Electron's `LICENSE` and Chromium's `LICENSES.chromium.html`, shipped inside that AppImage under `usr/share/doc/crazycity-insanity/` |
| **OpenSSL 3** | not shipped: the native game loads the system's library at run time, only for `wss://` network servers | Apache 2.0 | the system's OpenSSL package |

The native overlay's 5 × 7 font, the UI's vector icons and the program icon
(`packaging/linux/crazycity-insanity.svg`) are our own drawings and fall under the Unlicense.
