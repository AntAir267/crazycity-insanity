#include "music/music_system.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"

namespace cc {
namespace music {

// ---- the sound random draw (sound-system.md, "Sound random numbers"): ran3 seeded by the clock;
// 32-bit signed arithmetic as the game's runtime does it.
double soundRandom(int64_t seconds) {
    auto wrap = [](int64_t v) { return int32_t(uint32_t(uint64_t(v))); };
    const int32_t MBIG = 1000000000, MSEED = 161803398;
    int32_t ma[56] = {};
    const int32_t s = wrap(seconds < 0 ? -seconds : seconds);
    int32_t mj = wrap(int64_t(MSEED) - s) % MBIG;
    ma[55] = mj;
    int32_t mk = 1;
    for (int i = 1; i <= 54; ++i) {
        const int ii = (21 * i) % 55;
        ma[ii] = mk;
        mk = wrap(int64_t(mj) - mk);
        if (mk < 0) mk = wrap(int64_t(mk) + MBIG);
        mj = ma[ii];
    }
    for (int k = 1; k <= 4; ++k)
        for (int i = 1; i <= 55; ++i) {
            ma[i] = wrap(int64_t(ma[i]) - ma[1 + (i + 30) % 55]);
            if (ma[i] < 0) ma[i] = wrap(int64_t(ma[i]) + MBIG);
        }
    int inext = 0, inextp = 31;
    if (++inext == 56) inext = 1;
    if (++inextp == 56) inextp = 1;
    mj = wrap(int64_t(ma[inext]) - ma[inextp]);
    if (mj < 0) mj = wrap(int64_t(mj) + MBIG);
    ma[inext] = mj;
    return double(mj) * 1e-9;
}

namespace {

// The extension rule of music.md: appended unless the name ends in exactly ".ext" or ".EXT".
std::string withExt(const std::string& name, const char* lower, const char* upper) {
    if (endsWith(name, lower) || endsWith(name, upper)) return name;
    return name + lower;
}

}  // namespace

MusicSystem::MusicSystem() : synth_(kRate), perf_(synth_) {
    soundRandom_ = []() { return soundRandom(int64_t(std::time(nullptr))); };
    applyLevel();
}

MusicSystem::~MusicSystem() = default;

void MusicSystem::setFileReader(FileReader reader) {
    perf_.clear();
    synth_.clearDownloads();
    synth_.reset();  // the old files' samples go away
    player_.reset();
    phase_ = Phase::None;
    reader_ = std::move(reader);
    lib_ = reader_ ? std::make_unique<MusicLibrary>(reader_) : nullptr;
    if (lib_) lib_->setGmBank(gm_ ? gm_ : defaultGm_);
}

bool MusicSystem::hasData() const {
    if (!reader_) return false;
    return reader_("aud/dmusic/csv_files/ui.csv") != nullptr;
}

void MusicSystem::setGmBank(DlsPtr gm) {
    gm_ = std::move(gm);
    applyGmBank();
}

void MusicSystem::setDefaultGmBank(DlsPtr gm) {
    defaultGm_ = std::move(gm);
    applyGmBank();
}

void MusicSystem::applyGmBank() {
    // The player's own bank, else the bundled one (directmusic.md, "General MIDI instruments").
    DlsPtr use = gm_ ? gm_ : defaultGm_;
    if (!lib_ || lib_->gmBank() == use) return;
    // Sounding voices may use the old bank's samples: it stays alive.
    if (lib_->gmBank()) retiredGm_.push_back(lib_->gmBank());
    lib_->setGmBank(use);
    // The menus' music starts again with it; a game keeps its music player (and its downloads)
    // until the next phase.
    if (phase_ == Phase::Menus) startMenus();
}

void MusicSystem::say(const std::string& s) {
    if (log) log(s);
}

void MusicSystem::applyLevel() {
    // music.md, "Volume and balance".
    float m = std::clamp(volume_, 0.0f, 1.0f);
    gain_ = 0.0f;
    if (m > 0.0f) {
        const float x = 200.0f * m;
        const long double g = std::log((long double)x) / std::log((long double)200.0);
        const long double h = std::trunc((g - 1.0L) * 10000.0L);  // hundredths of a dB
        if (h >= -10000.0L) gain_ = float(std::pow(10.0, double(h) / 2000.0));
    }
    // Global balance (linear) and, once the slider moved, the music's own pan (decibels).
    float l = 1.0f, r = 1.0f;
    const float b = std::clamp(balance_, -1.0f, 1.0f);
    if (b > 0) l *= 1.0f - b;
    else if (b < 0) r *= 1.0f + b;
    if (panApplied_) {
        const double h = std::trunc(double(std::clamp(pan_, -1.0f, 1.0f)) * 10000.0);
        if (h > 0) l *= float(std::pow(10.0, -h / 2000.0));
        else if (h < 0) r *= float(std::pow(10.0, h / 2000.0));
    }
    gainL_ = gain_ * l;
    gainR_ = gain_ * r;
}

void MusicSystem::setVolume(float m) {
    volume_ = std::clamp(m, 0.0f, 1.0f);
    applyLevel();
}

void MusicSystem::setBalance(float b, bool sliderMoved) {
    balance_ = std::clamp(b, -1.0f, 1.0f);
    if (sliderMoved && player_) {
        panApplied_ = true;
        pan_ = balance_;
    }
    applyLevel();
}

void MusicSystem::setMode(MusicMode m) {
    if (m == mode_) return;
    mode_ = m;
    if (phase_ == Phase::Menus) {
        // "turning music on starts it the same way (making the player if needed); turning it
        // off, or choosing ambient, stops it at once."
        if (m == MusicMode::Music) startMenus();
        else {
            perf_.clear();
            player_.reset();
            phase_ = Phase::Menus;
        }
    }
    // In a game the option never changes (the in-game audio options have only the sliders): it
    // takes effect at the next phase.
}

void MusicSystem::restartPhase() {
    if (phase_ == Phase::Menus) startMenus();
    else if (phase_ == Phase::Game) startGame(lastSetup_);
}

void MusicSystem::newPlayer() {
    perf_.clear();
    synth_.clearDownloads();
    synth_.resetChannels();
    synth_.setActive(!cd_);
    uint32_t seed = seed_;
    if (seed == 0) seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFF) | 1u;
    perf_.setSeed(seed);
    player_ = std::make_unique<Player>();
    panApplied_ = false;
    applyLevel();
}

void MusicSystem::endPhase() {
    perf_.clear();
    player_.reset();
    phase_ = Phase::None;
}

bool MusicSystem::readTable(const std::string& name, std::vector<std::vector<std::string>>& rows) {
    rows.clear();
    if (!reader_) return false;
    BytesPtr b = reader_("aud/dmusic/csv_files/" + name + ".csv");
    if (!b) {
        logFile(LogLevel::Warn, "aud/dmusic/csv_files/" + name + ".csv", "music table missing");
        return false;
    }
    std::vector<std::string> lines;
    std::string cur;
    for (uint8_t c : *b) {
        if (c == '\n') {
            lines.push_back(cur.substr(0, 999));
            cur.clear();
        } else if (c != '\r') {
            cur += char(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur.substr(0, 999));
    for (size_t i = 1; i < lines.size(); ++i) rows.push_back(split(lines[i], ','));
    return true;
}

int MusicSystem::pickRow(int n) {
    // row = trunc(r × ((n − 0.01) × 0.01) × 100), 0.01 and 100 single precision.
    const long double r = soundRandom_ ? (long double)soundRandom_() : 0.0L;
    const long double c01 = (long double)0.01f, c100 = (long double)100.0f;
    long double v = r * (((long double)n - c01) * c01) * c100;
    int row = int(std::trunc(v));
    return std::clamp(row, 0, std::max(0, n - 1));
}

SegmentPtr MusicSystem::loadSlotSegment(const std::string& name) {
    if (!lib_ || name.empty()) return nullptr;
    SegmentPtr s = lib_->loadSegment(withExt(name, ".sgt", ".SGT"));
    if (s) lib_->downloadSegment(*s, synth_);
    say(std::string(s ? "load " : "missing ") + name);
    return s;
}

void MusicSystem::startMenus() {
    phase_ = Phase::Menus;
    perf_.clear();
    player_.reset();
    if (mode_ != MusicMode::Music || !lib_) return;
    newPlayer();
    std::vector<std::vector<std::string>> rows;
    readTable("ui", rows);
    player_->table = "ui";
    player_->slots.resize(1);
    player_->names.resize(1);
    if (!rows.empty() && !rows[0].empty()) {
        player_->row = 0;
        player_->names[0] = trim(rows[0][0]);
        player_->slots[0] = loadSlotSegment(player_->names[0]);
    }
    player_->current = 0;
    startSlot(0);
    player_->started = true;
    say("menus: " + player_->names[0]);
}

void MusicSystem::startGame(const GameMusicSetup& setup) {
    lastSetup_ = setup;
    phase_ = Phase::Game;
    perf_.clear();
    player_.reset();
    if (!lib_) return;
    if (mode_ == MusicMode::Music) {
        newPlayer();
        Player& p = *player_;
        p.game = true;
        p.race = setup.race;
        p.table = setup.race ? "singlerace" : "singleroam";
        const int nSlots = setup.race ? 7 : 6;
        p.slots.resize(size_t(nSlots));
        p.names.resize(size_t(nSlots));
        std::vector<std::vector<std::string>> rows;
        readTable(p.table, rows);
        if (rows.empty()) return;
        p.row = pickRow(int(rows.size()));
        const std::vector<std::string>& row = rows[size_t(p.row)];
        auto col = [&](int c) { return c >= 1 && size_t(c) <= row.size() ? trim(row[size_t(c - 1)]) : std::string(); };
        // Loaded (and their instruments downloaded) one after another: the race table slot 0 to 6,
        // the roam table in column order (start, return, idle, chase, idle-cop, pause).
        struct Load {
            Role role;
            int slot, col;
        };
        const Load raceLoads[] = {{Start, 0, 1}, {Chase, 1, 5}, {Return, 2, 2}, {Pause, 3, 6},
                                  {Idle, 4, 3},  {IdleCop, 5, 4}, {Results, 6, 7}};
        const Load roamLoads[] = {{Start, 0, 1}, {Return, 1, 2}, {Idle, 3, 3},
                                  {Chase, 2, 4}, {IdleCop, 4, 5}, {Pause, 5, 6}};
        const Load* loads = setup.race ? raceLoads : roamLoads;
        const int nLoads = setup.race ? 7 : 6;
        for (int li = 0; li < nLoads; ++li) {
            const Load& l = loads[li];
            const int slot = l.slot;
            const int c = l.col;
            p.names[size_t(slot)] = col(c);
            p.slots[size_t(slot)] = loadSlotSegment(col(c));
            // Cruise's idle-cop slot is loaded but never used by the rules.
            if (!(l.role == IdleCop && !setup.race)) p.role[l.role] = slot;
        }
        // Motif and band.
        const std::string motifStyle = col(setup.race ? 8 : 7), motifName = col(setup.race ? 9 : 8),
                          bandName = col(setup.race ? 10 : 9);
        if (!motifStyle.empty()) {
            StylePtr st = lib_->loadStyle(withExt(motifStyle, ".sty", ".STY"));
            const Pattern* m = st ? st->findMotif(motifName) : nullptr;
            if (m) {
                auto seg = std::make_shared<Segment>();
                seg->name = motifName;
                seg->motifStyle = st;
                seg->motif = m;
                seg->length = int32_t(m->lengthMeasures ? m->lengthMeasures : 1) * m->sig.measureTicks();
                seg->repeats = m->motif.repeats;
                seg->playStart = m->motif.playStart;
                seg->loopStart = m->motif.loopStart;
                seg->loopEnd = m->motif.loopEnd;
                seg->resolution = m->motif.resolution;
                p.motif = seg;
            } else if (st) {
                logFile(LogLevel::Warn, "aud/dmusic/" + withExt(motifStyle, ".sty", ".STY"), "no motif " + motifName);
            }
        }
        if (!bandName.empty()) {
            p.band = lib_->loadBand(withExt(bandName, ".bnd", ".BND"));
            if (p.band) {
                lib_->downloadBand(*p.band, synth_);
                auto seg = std::make_shared<Segment>();
                seg->name = bandName + " (band)";
                seg->length = 1;
                seg->hasBandTrack = true;
                BandEvent be;
                be.band = p.band;
                seg->bands.push_back(be);
                perf_.playSecondary(seg, Boundary::Immediate, 0);
            }
        }
        p.blockIdleAtStart = setup.race;
        say("game: " + p.table + " row " + std::to_string(p.row) + " (" + p.names[0] + ")");
    } else if (mode_ == MusicMode::Ambient && !setup.multiplayer) {
        newPlayer();
        Player& p = *player_;
        p.game = true;
        p.ambient = true;
        p.table = setup.mapName == "sf" ? "sfambience" : "londonambience";
        p.slots.resize(1);
        p.names.resize(1);
        std::vector<std::vector<std::string>> rows;
        readTable(p.table, rows);
        if (!rows.empty() && !rows[0].empty()) {
            p.row = 0;
            p.names[0] = trim(rows[0][0]);
            p.slots[0] = loadSlotSegment(p.names[0]);
        }
        say("ambience: " + p.names[0]);
    }
}

// ---- switches (music.md, "Kinds of switch")

void MusicSystem::startSlot(int s) {
    if (!player_ || s < 0 || size_t(s) >= player_->slots.size()) return;
    SegmentPtr seg = player_->slots[size_t(s)];
    if (!seg) return;
    if (perf_.isPlaying(seg.get())) return;
    seg->repeats = 1073741824u;
    perf_.playPrimary(seg, Boundary::Beat);
}

void MusicSystem::stopCurrent() {
    if (!player_) return;
    const int c = player_->current;
    if (c >= 0 && size_t(c) < player_->slots.size() && player_->slots[size_t(c)])
        perf_.stop(player_->slots[size_t(c)].get());
}

void MusicSystem::cutTo(int s) {
    if (!player_ || s < 0 || size_t(s) >= player_->slots.size() || s == player_->current) return;
    // MM2 stops the current slot's segment first and gives up when it has none.
    const int c = player_->current;
    if (c < 0 || size_t(c) >= player_->slots.size() || !player_->slots[size_t(c)]) return;
    stopCurrent();
    player_->previous = player_->current;
    player_->current = s;
    say("cut to " + player_->names[size_t(s)]);
    startSlot(s);
}

void MusicSystem::transitionTo(int s, uint8_t command, Boundary b) {
    if (!player_ || s < 0 || size_t(s) >= player_->slots.size() || s == player_->current) return;
    // An empty slot is a transition to nothing with this command and boundary (music.md).
    perf_.transition(player_->slots[size_t(s)], command, b);
    player_->previous = player_->current;
    player_->current = s;
    say(std::string(command == CmdEnd ? "ending to " : "groove to ") + player_->names[size_t(s)]);
}

// ---- each frame

void MusicSystem::frame(const MusicFrame& f) {
    if (!player_ || !player_->game) return;
    Player& p = *player_;
    p.clock += double(f.F);
    if (p.clock < 0) p.clock = 100;
    if (!p.started) {
        if (p.clock >= 1.25) {
            if (p.ambient) {
                if (!f.echo) startSlot(0);
            } else {
                if (p.blockIdleAtStart) p.idleBlocked = true;
                p.current = p.role[Start] >= 0 ? p.role[Start] : 0;
                startSlot(p.current);
                say("start " + p.names[size_t(p.current)]);
            }
            p.idleTimer = 10000.0f;
            p.started = true;
        }
        return;
    }
    if (p.ambient) {
        if (f.echo && !p.stoppedForTunnel) {
            if (p.slots[0]) perf_.stop(p.slots[0].get());
            p.stoppedForTunnel = true;
        } else if (!f.echo && p.stoppedForTunnel) {
            startSlot(0);
            p.stoppedForTunnel = false;
        }
        return;
    }
    // Idle.
    const int idle = p.role[Idle], idleCop = p.role[IdleCop], ret = p.role[Return], chase = p.role[Chase],
              pause = p.role[Pause], results = p.role[Results];
    if (!(p.current == results && results >= 0) && idle >= 0 && ret >= 0) {
        if (f.speed <= 5.0f && !p.idleBlocked) {
            if (p.idleTimer >= 5.0f && p.current != idle && p.current != idleCop && p.current != pause) {
                if (p.current != chase) transitionTo(idle, CmdGroove, Boundary::Measure);
                else if (idleCop >= 0) transitionTo(idleCop, CmdGroove, Boundary::Measure);
            }
            p.idleTimer = p.idleTimer + f.F;
        } else if (p.idleTimer != 0.0f) {
            if (p.current == idle) transitionTo(ret, CmdGroove, Boundary::Measure);
            else if (p.current == idleCop && idleCop >= 0) transitionTo(chase, CmdGroove, Boundary::Measure);
            p.idleTimer = 0.0f;
        }
    }
    // Police chase.
    if (f.chaseCount == 1 && p.prevChase == 0) cutTo(chase);
    else if (f.chaseCount == 0 && p.prevChase == 1) cutTo(ret);
    p.prevChase = f.chaseCount;
    // Big air.
    if (f.airborne && !p.bigAirLatch) {
        if (p.motif) {
            const uint32_t res = p.motif->resolution;
            const Boundary b = (res & 0x2000) ? Boundary::Measure : (res & 0x1000) ? Boundary::Beat
                               : (res & 0x800) ? Boundary::Grid : Boundary::Immediate;
            perf_.playSecondary(p.motif, b, 1);
            say("big air");
        }
        p.bigAirLatch = true;
    } else if (!f.airborne) {
        p.bigAirLatch = false;
    }
}

// ---- race events

void MusicSystem::raceGo() {
    if (player_ && player_->game && !player_->ambient) player_->idleBlocked = false;
}

void MusicSystem::finalLap() {
    if (player_ && player_->game && !player_->ambient) transitionTo(player_->role[Chase], CmdEnd, Boundary::Beat);
}

void MusicSystem::lastGate() { finalLap(); }
void MusicSystem::lastCheckpoint() { finalLap(); }

void MusicSystem::finished() {
    if (player_ && player_->game && !player_->ambient) {
        stopCurrent();
        say("finish: stop");
    }
}

void MusicSystem::wrecked() {
    if (player_ && player_->game && !player_->ambient) {
        perf_.transition(nullptr, CmdEnd, Boundary::Beat);
        say("wreck: ending");
    }
}

void MusicSystem::resultsOpened() {
    if (!player_ || !player_->game) return;
    if (player_->ambient) {
        if (player_->slots[0]) perf_.stop(player_->slots[0].get());
        return;
    }
    transitionTo(player_->role[Results], CmdEnd, Boundary::Beat);
}

void MusicSystem::paused() {
    if (!player_ || !player_->game) return;
    if (player_->ambient) {
        if (player_->slots[0]) perf_.stop(player_->slots[0].get());
        return;
    }
    cutTo(player_->role[Pause]);
}

void MusicSystem::resumed() {
    if (!player_ || !player_->game) return;
    if (player_->ambient) {
        startSlot(0);
        return;
    }
    cutTo(player_->previous);
}

void MusicSystem::cdPlaying(bool on) {
    cd_ = on;
    if (mode_ == MusicMode::Music) synth_.setActive(!on);
}

// ---- output

void MusicSystem::render16(int16_t* out, int frames) { perf_.render(out, frames); }

void MusicSystem::render(float* out, int frames) {
    if (int(buf_.size()) < frames * 2) buf_.resize(size_t(frames) * 2);
    perf_.render(buf_.data(), frames);
    const float kl = gainL_ / 32768.0f, kr = gainR_ / 32768.0f;
    for (int i = 0; i < frames; ++i) {
        out[2 * i] = float(buf_[size_t(2 * i)]) * kl;
        out[2 * i + 1] = float(buf_[size_t(2 * i + 1)]) * kr;
    }
}

std::string MusicSystem::currentSlotName() const {
    if (!player_ || player_->slots.empty()) return std::string();
    const int c = player_->current;
    return c >= 0 && size_t(c) < player_->names.size() ? player_->names[size_t(c)] : std::string();
}

std::string MusicSystem::statusJson() const {
    JsonWriter j;
    j.beginObject();
    j.field("mode", std::string(mode_ == MusicMode::Music ? "music" : mode_ == MusicMode::Ambient ? "ambient" : "off"));
    j.field("data", hasData());
    j.field("phase", std::string(phase_ == Phase::Menus ? "menus" : phase_ == Phase::Game ? "game" : "none"));
    j.field("volume", double(volume_));
    if (player_) {
        j.field("table", player_->table);
        j.field("row", player_->row);
        j.field("slot", currentSlotName());
        j.field("started", player_->started);
        j.field("idleBlocked", player_->idleBlocked);
        j.field("idleTimer", double(std::min(player_->idleTimer, 99999.0f)));
        j.field("chase", player_->prevChase);
    }
    j.field("playing", perf_.nowPlaying());
    j.field("tempo", perf_.tempo());
    j.field("voices", synth_.liveVoices());
    j.field("instruments", synth_.downloadCount());
    j.field("gm", gm_ != nullptr);
    if (gm_) j.field("gmName", gm_->fileName.empty() ? gm_->name : gm_->fileName);
    j.field("gmBundled", !gm_ && defaultGm_ != nullptr);
    j.field("gmMissing", lib_ ? lib_->gmMissing() : 0);
    j.endObject();
    return j.str();
}

}  // namespace music
}  // namespace cc
