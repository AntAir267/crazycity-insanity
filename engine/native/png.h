// PNG files of captured frames (the native self-test and screenshots).
#pragma once

#include <string>

namespace cc {
namespace native {

// Writes an RGBA image (rows from the top unless `bottomUp`); false on failure.
bool writePng(const std::string& path, int w, int h, const unsigned char* rgba, bool bottomUp);

}  // namespace native
}  // namespace cc
