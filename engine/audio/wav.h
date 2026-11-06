// RIFF WAVE files as MM2 reads them (spec/audio/sound-system.md, "WAV files"): the chunk walk
// stops once `fmt ` and `data` have been seen; any chunk other than `fmt `, `data` and `fact`
// before that makes the load fail. Only PCM (format tag 1), 8 or 16 bits, mono or stereo.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cc {
namespace audio {

struct WavInfo {
    int formatTag = 0;
    int channels = 0;
    int rate = 0;
    int byteRate = 0;
    int blockAlign = 0;
    int bits = 0;
    int extraSize = 0;
    size_t dataOffset = 0;
    size_t dataSize = 0;  // as far as the file reaches
    uint32_t frames() const {
        const int bpf = channels * (bits / 8);
        return bpf > 0 ? uint32_t(dataSize / size_t(bpf)) : 0;
    }
};

// False (with *err) when MM2 would refuse the file or we cannot play it.
bool parseWav(const uint8_t* p, size_t n, WavInfo& out, std::string* err);
// The data chunk as interleaved signed 16-bit samples.
void wavToPcm16(const uint8_t* p, const WavInfo& w, std::vector<int16_t>& out);

}  // namespace audio
}  // namespace cc
