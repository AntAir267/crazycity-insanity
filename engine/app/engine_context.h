// What the views share: file system, asset caches, renderer, platform.
#pragma once

namespace cc {

class Vfs;
class AssetCache;
class TextureCache;
class MeshCache;
class Renderer;
class Platform;
namespace audio {
class SoundSystem;
}

struct EngineContext {
    Vfs* vfs = nullptr;
    AssetCache* assets = nullptr;
    TextureCache* textures = nullptr;
    MeshCache* meshes = nullptr;
    Renderer* renderer = nullptr;
    Platform* platform = nullptr;
    double time = 0;  // seconds since start
    // Draw distance (viewer.md): 0 = MM2 (the profile's view distance, 600 m shipped), > 0 = that
    // many metres, -1 = full map. The page remembers it per browser.
    int drawDistance = 0;
    // Sound (spec/audio): the mixer; soundReady once the audio archives' data files have arrived
    // (lazily read in the browser); audioExtras when mm2audex.ar (voices 2 and up) is loaded.
    audio::SoundSystem* sound = nullptr;
    bool soundReady = false;
    bool audioExtras = false;
};

}  // namespace cc
