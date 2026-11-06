// The sound system's voices mixed in software into the app's one audio output (App::renderAudio,
// with the music): interleaved stereo float at the output rate (the music's 22,050 Hz, MM2's own
// mixing rate). Each voice reads its file at its playback rate (linear interpolation) with one
// gain per channel; gain changes ramp over one output block so level changes do not click.
#pragma once

#include <cstdint>
#include <vector>

#include "audio/sound_system.h"

namespace cc {
namespace audio {

class SoftMixer : public AudioBackend {
public:
    int createBuffer(const int16_t* pcm, uint32_t frames, int channels, int rate) override;
    void destroyBuffer(int id) override;
    int startVoice(int buffer, bool loop, double offsetSeconds, double playbackRate, float gainL, float gainR) override;
    void setVoice(int voice, double playbackRate, float gainL, float gainR) override;
    void stopVoice(int voice) override;

    // Adds every sounding voice into `out` (frames × 2 floats at outRate).
    void mix(float* out, int frames, int outRate);
    // Voices sounding now (started and not yet at their end).
    int sounding() const;
    int started() const { return started_; }

private:
    struct Buffer {
        std::vector<int16_t> pcm;
        int channels = 1, rate = 22050;
        uint32_t frames = 0;
        bool used = false;
    };
    struct Voice {
        int buffer = -1;
        bool loop = false, used = false, ended = false;
        double pos = 0;   // frames of the buffer
        double rate = 1;  // relative to the buffer's own rate
        float gl = 0, gr = 0, cl = 0, cr = 0;
    };
    std::vector<Buffer> buffers_;
    std::vector<int> freeBuffers_;
    std::vector<Voice> voices_;
    std::vector<int> freeVoices_;
    int started_ = 0;
};

}  // namespace audio
}  // namespace cc
