// The UI's fonts compiled into the program: build.sh writes build/gen/ui_fonts.cpp from
// engine/ui/fonts/*.ttf (IBM Plex Sans and Mono, SIL Open Font License 1.1; see
// engine/ui/fonts/OFL.txt). Every build that draws the UI (web, native, cctool) links it.
#pragma once

#include <cstddef>

namespace cc {
namespace ui {

struct EmbeddedFont {
    const char* name;  // file name, e.g. "IBMPlexSans-Regular.ttf"
    const unsigned char* data;
    size_t size;
};

// The compiled-in font files (count 0 when the build did not add them).
const EmbeddedFont* embeddedFonts(int& count);

}  // namespace ui
}  // namespace cc
