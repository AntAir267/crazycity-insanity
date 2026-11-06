// MM2's music rules (spec/audio/music.md) over the DirectMusic player of this module: the music
// player made per phase (the menus or one game), the tables and their random row, the slots, the
// 1.25 s start, idle, police chase cuts, the big-air motif, race events, pause, the ambient
// option, menus, the CD player, and the output volume and balance.
//
// The module's boundary: options and events in, `render()` out (stereo at 22,050 Hz). The sound
// system can later mix this output into its own; for now the app sends it to a small Web Audio
// path (app/music_output).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "music/dm_loader.h"
#include "music/performance.h"
#include "music/synth.h"

namespace cc {
namespace music {

enum class MusicMode { Off, Music, Ambient };

struct GameMusicSetup {
    // singlerace (Checkpoint, Blitz, Circuit, Crash Course; multiplayer Checkpoint, Blitz,
    // Circuit) or singleroam (Cruise; multiplayer Cruise and Cops and Robbers).
    bool race = false;
    bool multiplayer = false;
    std::string mapName;  // the level (MapName): "sf" exactly picks the SF ambience
};

// What one frame of a game hands the music (values of the previous frame).
struct MusicFrame {
    float F = 0;            // frame time (s)
    float speed = 0;        // the player car's speed (m/s)
    int chaseCount = 0;     // traffic-sounds.md, "The chase count"
    bool airborne = false;  // music.md, "Airborne flag"
    bool echo = false;      // sound-system.md, "Echo": the car is in an enclosed room
};

// The sound random draw (sound-system.md, "Sound random numbers") for a clock value.
double soundRandom(int64_t seconds);

class MusicSystem {
public:
    static constexpr int kRate = 22050;

    MusicSystem();
    ~MusicSystem();

    // The archives changed: the file cache is dropped (and any player with it).
    void setFileReader(FileReader reader);
    bool hasData() const;
    // The General MIDI set (the player's own gm.dls or a bundled bank); null = none (those
    // instruments stay silent). Takes effect for the next music player; the current phase is
    // set up again.
    void setGmBank(DlsPtr gm);
    const DlsPtr& gmBank() const { return gm_; }
    // The bundled bank (FluidR3 GM's drum kit and piano), used when the player has none.
    void setDefaultGmBank(DlsPtr gm);

    // Options (the player file's bits are exclusive: music, ambient, or neither). Changed only
    // outside a game (menus); during a game it takes effect at the next phase.
    void setMode(MusicMode m);
    MusicMode mode() const { return mode_; }
    void setVolume(float m);
    float volume() const { return volume_; }
    // The balance option; `sliderMoved` also pans the music output for the player's life.
    void setBalance(float b, bool sliderMoved);
    // The music player's random generator: a check run starts it at 1; 0 = from the clock.
    void setSeed(uint32_t seed) { seed_ = seed; }
    // The sound random draw used for the table row (default: soundRandom(time(nullptr))).
    void setSoundRandom(std::function<double()> r) { soundRandom_ = std::move(r); }

    // Phases.
    void startMenus();
    void startGame(const GameMusicSetup& setup);
    void endPhase();
    bool inGame() const { return player_ && player_->game; }

    // One frame of a game (not paused): music.md, "Each frame of a game".
    void frame(const MusicFrame& f);
    // Race events (music.md, "Race events").
    void raceGo();
    void finalLap();        // Circuit: the final lap begins
    void lastGate();        // Circuit: the last gate before the finish on the final lap
    void lastCheckpoint();  // Checkpoint: only the finish remains
    void finished();
    void wrecked();         // Checkpoint, Blitz
    void resultsOpened();
    void paused();
    void resumed();
    void cdPlaying(bool on);

    // Output: interleaved stereo at kRate, volume and balance applied.
    void render(float* out, int frames);
    void render16(int16_t* out, int frames);

    // For the page and tests.
    std::string statusJson() const;
    std::string currentSlotName() const;
    std::string nowPlaying() const { return perf_.nowPlaying(); }
    Performance& performance() { return perf_; }
    DlsSynth& synth() { return synth_; }
    MusicLibrary* library() { return lib_.get(); }
    std::function<void(const std::string&)> log;  // switches, for tests

private:
    enum Role { Start, Chase, Return, Pause, Idle, IdleCop, Results, RoleCount };
    struct Player {
        bool game = false, ambient = false, race = false;
        std::vector<SegmentPtr> slots;
        int role[RoleCount] = {-1, -1, -1, -1, -1, -1, -1};
        std::vector<std::string> names;  // per slot
        SegmentPtr motif;
        BandPtr band;
        int current = 0, previous = 0;
        bool started = false;
        double clock = 0;
        float idleTimer = 10000.0f;
        int prevChase = 0;
        bool bigAirLatch = false;
        bool idleBlocked = false, blockIdleAtStart = false;
        bool stoppedForTunnel = false;
        int row = -1;
        std::string table;
    };

    void newPlayer();
    bool readTable(const std::string& name, std::vector<std::vector<std::string>>& rows);
    int pickRow(int n);
    SegmentPtr loadSlotSegment(const std::string& name);
    void startSlot(int s);
    void cutTo(int s);
    void transitionTo(int s, uint8_t command, Boundary b);
    void stopCurrent();
    void applyLevel();
    void say(const std::string& s);
    void restartPhase();
    void applyGmBank();

    FileReader reader_;
    std::unique_ptr<MusicLibrary> lib_;
    DlsPtr gm_, defaultGm_;
    std::vector<DlsPtr> retiredGm_;  // banks replaced while voices may still play them
    DlsSynth synth_;
    Performance perf_;
    std::unique_ptr<Player> player_;
    enum class Phase { None, Menus, Game } phase_ = Phase::None;
    GameMusicSetup lastSetup_;
    MusicMode mode_ = MusicMode::Off;
    float volume_ = 1.0f, balance_ = 0.0f;
    bool panApplied_ = false;
    float pan_ = 0.0f;
    bool cd_ = false;
    uint32_t seed_ = 0;
    std::function<double()> soundRandom_;
    float gain_ = 1.0f, gainL_ = 1.0f, gainR_ = 1.0f;
    std::vector<int16_t> buf_;
};

}  // namespace music
}  // namespace cc
