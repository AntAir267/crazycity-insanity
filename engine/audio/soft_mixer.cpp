#include "audio/soft_mixer.h"

#include <cmath>

namespace cc {
namespace audio {

int SoftMixer::createBuffer(const int16_t* pcm, uint32_t frames, int channels, int rate) {
    int id;
    if (!freeBuffers_.empty()) {
        id = freeBuffers_.back();
        freeBuffers_.pop_back();
    } else {
        id = int(buffers_.size());
        buffers_.emplace_back();
    }
    Buffer& b = buffers_[size_t(id)];
    b.pcm.assign(pcm, pcm + size_t(frames) * size_t(channels));
    b.channels = channels;
    b.rate = rate;
    b.frames = frames;
    b.used = true;
    return id;
}

void SoftMixer::destroyBuffer(int id) {
    if (id < 0 || size_t(id) >= buffers_.size() || !buffers_[size_t(id)].used) return;
    for (size_t v = 0; v < voices_.size(); ++v)
        if (voices_[v].used && voices_[v].buffer == id) stopVoice(int(v));
    Buffer& b = buffers_[size_t(id)];
    b.pcm.clear();
    b.pcm.shrink_to_fit();
    b.used = false;
    freeBuffers_.push_back(id);
}

int SoftMixer::startVoice(int buffer, bool loop, double offset, double rate, float gl, float gr) {
    if (buffer < 0 || size_t(buffer) >= buffers_.size() || !buffers_[size_t(buffer)].used) return -1;
    int id;
    if (!freeVoices_.empty()) {
        id = freeVoices_.back();
        freeVoices_.pop_back();
    } else {
        id = int(voices_.size());
        voices_.emplace_back();
    }
    Voice& v = voices_[size_t(id)];
    const Buffer& b = buffers_[size_t(buffer)];
    v = Voice();
    v.used = true;
    v.buffer = buffer;
    v.loop = loop;
    v.pos = offset * double(b.rate);
    if (b.frames > 0 && v.pos >= double(b.frames)) v.pos = loop ? std::fmod(v.pos, double(b.frames)) : double(b.frames);
    v.rate = rate;
    v.gl = v.cl = gl;  // a voice starts at its level (as a DirectSound buffer does)
    v.gr = v.cr = gr;
    ++started_;
    return id;
}

void SoftMixer::setVoice(int id, double rate, float gl, float gr) {
    if (id < 0 || size_t(id) >= voices_.size() || !voices_[size_t(id)].used) return;
    Voice& v = voices_[size_t(id)];
    v.rate = rate;
    v.gl = gl;
    v.gr = gr;
}

void SoftMixer::stopVoice(int id) {
    if (id < 0 || size_t(id) >= voices_.size() || !voices_[size_t(id)].used) return;
    voices_[size_t(id)] = Voice();
    freeVoices_.push_back(id);
}

int SoftMixer::sounding() const {
    int n = 0;
    for (const Voice& v : voices_)
        if (v.used && !v.ended) ++n;
    return n;
}

void SoftMixer::mix(float* out, int frames, int outRate) {
    if (frames <= 0 || outRate <= 0) return;
    const float inv = 1.0f / float(frames);
    for (Voice& v : voices_) {
        if (!v.used || v.ended) continue;
        const Buffer& b = buffers_[size_t(v.buffer)];
        if (b.frames == 0) {
            v.ended = true;
            continue;
        }
        const double step = v.rate * double(b.rate) / double(outRate);
        const double end = double(b.frames);
        const int16_t* p = b.pcm.data();
        const float dl = (v.gl - v.cl) * inv, dr = (v.gr - v.cr) * inv;
        float cl = v.cl, cr = v.cr;
        for (int i = 0; i < frames; ++i) {
            if (v.pos >= end) {
                if (v.loop) v.pos = std::fmod(v.pos, end);
                else {
                    v.ended = true;
                    break;
                }
            }
            const uint32_t k = uint32_t(v.pos);
            const float t = float(v.pos - double(k));
            const uint32_t k1 = k + 1 < b.frames ? k + 1 : (v.loop ? 0 : k);
            cl += dl;
            cr += dr;
            if (b.channels == 2) {
                const float l = (float(p[2 * k]) + (float(p[2 * k1]) - float(p[2 * k])) * t) * (1.0f / 32768.0f);
                const float r = (float(p[2 * k + 1]) + (float(p[2 * k1 + 1]) - float(p[2 * k + 1])) * t) * (1.0f / 32768.0f);
                out[2 * i] += l * cl;
                out[2 * i + 1] += r * cr;
            } else {
                const float s = (float(p[k]) + (float(p[k1]) - float(p[k])) * t) * (1.0f / 32768.0f);
                out[2 * i] += s * cl;
                out[2 * i + 1] += s * cr;
            }
            v.pos += step;
        }
        v.cl = v.gl;
        v.cr = v.gr;
    }
}

}  // namespace audio
}  // namespace cc
