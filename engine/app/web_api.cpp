// Functions the page calls (through Module.ccall). Strings returned stay valid until the next
// call that returns a string.
#include <emscripten.h>

#include <string>
#include <vector>

#include "app/app.h"
#include "platform/web_bridge.h"

using cc::theApp;

namespace {
std::string g_result;
const char* ret(std::string s) {
    g_result = std::move(s);
    return g_result.c_str();
}
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void cc_clear_archives() { theApp().clearArchives(); }

// The page keeps the archive bytes in window.CCI.archives[jsId].
EMSCRIPTEN_KEEPALIVE void cc_add_archive(const char* name, int jsId, double size) {
    theApp().addArchive(name, cc::makeJsArchiveSource(jsId, size));
}

EMSCRIPTEN_KEEPALIVE const char* cc_mount() { return ret(theApp().mount()); }
EMSCRIPTEN_KEEPALIVE const char* cc_car_list() { return ret(theApp().carListJson()); }
EMSCRIPTEN_KEEPALIVE const char* cc_select_car(int index) { return ret(theApp().selectCar(index)); }
EMSCRIPTEN_KEEPALIVE const char* cc_car_info() { return ret(theApp().carInfoJson()); }
EMSCRIPTEN_KEEPALIVE void cc_set_paint(int c) { theApp().setPaint(c); }
EMSCRIPTEN_KEEPALIVE void cc_set_lod(int l) { theApp().setLod(l); }
EMSCRIPTEN_KEEPALIVE void cc_set_car_option(const char* name, int v) { theApp().setCarOption(name, v); }
EMSCRIPTEN_KEEPALIVE void cc_reset_camera() { theApp().resetCamera(); }
EMSCRIPTEN_KEEPALIVE void cc_set_view(int v) { theApp().setView(v); }
EMSCRIPTEN_KEEPALIVE const char* cc_city_list() { return ret(theApp().cityListJson()); }
EMSCRIPTEN_KEEPALIVE const char* cc_load_city(int index) { return ret(theApp().loadCity(index)); }
EMSCRIPTEN_KEEPALIVE void cc_set_city_option(const char* name, int v) { theApp().setCityOption(name, v); }
EMSCRIPTEN_KEEPALIVE const char* cc_drop_car() { return ret(theApp().dropCar()); }
// Places the city camera (yaw/pitch in radians; yaw 0 looks north, -Z).
EMSCRIPTEN_KEEPALIVE void cc_set_city_camera(float x, float y, float z, float yaw, float pitch) {
    theApp().setCityCamera(x, y, z, yaw, pitch);
}
EMSCRIPTEN_KEEPALIVE const char* cc_start_drive(int mode, int flat) { return ret(theApp().startDrive(mode, flat != 0)); }
EMSCRIPTEN_KEEPALIVE void cc_stop_drive() {
    // Stopping a network game's drive leaves its session (the others see the player leave).
    if (theApp().driveView().netActive()) theApp().netEnd();
    theApp().stopDrive();
}
EMSCRIPTEN_KEEPALIVE void cc_set_drive_option(const char* name, int v) { theApp().setDriveOption(name, v); }
// Cruise's cop density 0 … 1 as the slider gives it (not rounded: the cop count is trunc(d × N)).
EMSCRIPTEN_KEEPALIVE void cc_set_cop_density(double d) { theApp().setCopDensity(float(d)); }
EMSCRIPTEN_KEEPALIVE const char* cc_stats() { return ret(theApp().statsJson()); }
EMSCRIPTEN_KEEPALIVE const char* cc_police() { return ret(theApp().policeJson()); }
EMSCRIPTEN_KEEPALIVE const char* cc_peds() { return ret(theApp().pedsJson()); }
EMSCRIPTEN_KEEPALIVE const char* cc_drive_debug() { return ret(theApp().driveDebugJson()); }
// Sound options: "effects", "ambient", "commentary" (0/1), "volume" (0-100), "balance" (-100-100).
EMSCRIPTEN_KEEPALIVE void cc_set_sound_option(const char* name, int v) { theApp().setSoundOption(name, v); }
EMSCRIPTEN_KEEPALIVE const char* cc_sound() { return ret(theApp().soundJson()); }

// Music (spec/audio/music.md): 0 off, 1 music, 2 city ambience; volume 0..1.
EMSCRIPTEN_KEEPALIVE void cc_music_set_mode(int mode) { theApp().setMusicMode(mode); }
EMSCRIPTEN_KEEPALIVE void cc_music_set_volume(float v) { theApp().setMusicVolume(v); }
EMSCRIPTEN_KEEPALIVE const char* cc_music_status() { return ret(theApp().musicStatusJson()); }
// Race and pause events (testing until the race modes and the pause menu exist).
EMSCRIPTEN_KEEPALIVE void cc_music_event(const char* name) { theApp().musicEvent(name); }
// The player's own General MIDI bank (gm.dls or a SoundFont), kept in window.CCI.archives[jsId];
// jsId < 0 clears it. bundled = 1: the page's own fallback bank (used when the player has none).
EMSCRIPTEN_KEEPALIVE const char* cc_music_set_gm(const char* name, int jsId, double size, int bundled) {
    cc::Bytes data;
    if (jsId >= 0 && size > 0) {
        auto src = cc::makeJsArchiveSource(jsId, size);
        data.resize(size_t(size));
        if (!src || !src->read(0, data.data(), data.size())) data.clear();
    }
    return ret(theApp().setMusicGmBank(name, data, bundled != 0));
}

// Audio output: the page pulls interleaved stereo float frames at cc_audio_rate().
EMSCRIPTEN_KEEPALIVE int cc_audio_rate() { return theApp().audioRate(); }
EMSCRIPTEN_KEEPALIVE float* cc_audio_render(int frames) {
    static std::vector<float> buf;
    if (frames < 0) frames = 0;
    if (buf.size() < size_t(frames) * 2) buf.resize(size_t(frames) * 2);
    theApp().renderAudio(buf.data(), frames);
    return buf.data();
}
// Race modes: the loaded city's races, start (mode: 1 Checkpoint, 3 Circuit, 4 Blitz, 6 Crash
// Course; opponents −1 = the race's number), restart, the overlay state.
EMSCRIPTEN_KEEPALIVE const char* cc_race_list() { return ret(theApp().raceListJson()); }
EMSCRIPTEN_KEEPALIVE const char* cc_start_race(int mode, int race, int pro, int opponents) {
    return ret(theApp().startRace(mode, race, pro != 0, opponents));
}
EMSCRIPTEN_KEEPALIVE void cc_restart_race() { theApp().restartRace(); }
EMSCRIPTEN_KEEPALIVE const char* cc_next_race() { return ret(theApp().nextRace()); }
EMSCRIPTEN_KEEPALIVE const char* cc_race_state() { return ret(theApp().raceJson()); }
// Scripted checks: n frames of dt seconds now (also while the page is hidden).
EMSCRIPTEN_KEEPALIVE void cc_step_frames(int n, float dt) { theApp().stepFrames(n, dt); }

}  // extern "C"
