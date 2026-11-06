// Texture images: .tex, .tga, .bmp decoding to RGBA8 (spec/formats/tex.md).
//
// Row order: rows are kept in the order the renderer samples them, row 0 = v 0. Upload rows in
// this order and use the model's texture coordinates as stored.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc {

struct ImageLevel {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, R G B A
};

struct Image {
    std::vector<ImageLevel> levels;  // level 0 first
    bool hasAlpha = false;   // drawn alpha-blended
    bool clampU = false;     // do not repeat horizontally
    bool clampV = false;     // do not repeat vertically
    bool generateMips = false;  // build a full mip chain at load (square TGAs)
    uint32_t texFlags = 0;   // raw .tex flags
    int texType = 0;         // .tex pixel type, 0 for other formats
    const char* format = "";  // "tex", "tga", "bmp"

    int width() const { return levels.empty() ? 0 : levels[0].width; }
    int height() const { return levels.empty() ? 0 : levels[0].height; }
    size_t byteSize() const;
};

bool decodeTex(const uint8_t* data, size_t size, Image& out, std::string& err);
bool decodeTga(const uint8_t* data, size_t size, Image& out, std::string& err);
bool decodeBmp(const uint8_t* data, size_t size, Image& out, std::string& err);

// Picks the decoder by extension (".tex", ".tga", ".bmp", any case).
bool decodeImage(const std::string& path, const uint8_t* data, size_t size, Image& out,
                 std::string& err);

}  // namespace cc
