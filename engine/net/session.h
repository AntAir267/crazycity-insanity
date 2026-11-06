// One network session on this machine (spec/game/multiplayer.md section 1): the player records,
// the host's settings, joining, ready, the start slot table, leaving, kicking, the host leaving or
// vanishing, return to lobby, chat, the clock (3.6) and the cheater marks (3.7). The running game
// itself is net/net_game.h; the session hands it its messages.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "net/settings.h"
#include "net/transport.h"
#include "net/wire.h"

namespace cc {
class Vfs;
namespace net {

class NetGame;

// A player record (1.1): every peer can read it; re-sent when it changes.
struct PlayerRecord {
    std::string name = "Player";
    std::string car = "vpbug";
    int paint = 0;
    int team = 1;          // Cops and Robbers team, 0 or 1
    bool inGame = false;   // inside a running Cruise or Cops and Robbers game
    bool host = false;
    bool ready = false;
    std::string city;      // the city (RaceDir) this player has loaded
    uint32_t check = 0;    // car check value (3.7)
    bool slowLink = false; // chose a dial-up connection (3.2)
    // The host's record also carries these two (MM2 has no menu: always on).
    bool cheatCheck = true;
    bool waitForAll = true;
};

struct Player {
    PlayerId id = 0;
    PlayerRecord rec;
    bool haveRecord = false;
    bool cheater = false;
    bool unreachable = false;  // a car-state send failed (3.2): no more states go to it
    float sendTimer = 0.0f;    // τⱼ (3.2)
};

// A HUD notice or chat line; text numbers are mmlang.dll's (formats/mmlang.md). "*name*" + text
// has two forms (multiplayer.md, "Words"): one line "name text line" with single spaces, or
// (`twoLines`) the name as the primary message and "text line" as the secondary line. A chat
// line without a text number is "name: line".
struct Notice {
    enum Kind { Primary, Chat };
    Kind kind = Primary;
    int text = 0;            // 0: only `line`
    std::string name;        // the player's name (empty: none)
    std::string line;        // after the text (a time), or a chat line's own words
    bool twoLines = false;
    float seconds = 0;
    bool upper = false;
    bool sound = false;      // the notice sound
};
// Words joined with one space (no shipped text starts or ends with one).
std::string joinWords(const std::string& a, const std::string& b);

// The network respawn counter (2.1): one per program in MM2. The native tests give each machine
// of a loopback session its own.
struct RespawnCounter {
    int k = 0;
};
RespawnCounter& programRespawnCounter();

class Session {
public:
    Session(Transport* transport, const Vfs* vfs);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // ---- this player
    PlayerRecord& localRecord() { return local_.rec; }
    // Sends the local record to everyone after a change (and computes the car's check value).
    void publishRecord();
    void setReady(bool r);
    // A city's installed test for joiners (1.3 step 1); default: everything is installed.
    void setCityInstalled(std::function<bool(const std::string&)> f) { cityInstalled_ = std::move(f); }
    // The local driver's locks (progression.md, "Locked cars and paint jobs"): 0 free, 1 the car is
    // locked, 2 the paint is locked. None: nothing is locked.
    void setLockQuery(std::function<int(const std::string& car, int paint)> f) { lockQuery_ = std::move(f); }
    // 1.3 step 1, "Car choice and locks", then the record published: in the cops-and-robbers
    // sub-mode the team's car with paint 0; otherwise, when `entering` multiplayer or the lobby
    // outside Cops and Robbers, the driver's locks (a locked car becomes vpbug paint 0, else a
    // locked paint paint 0). Cops and Robbers' other sub-modes and leaving it keep the car.
    void applyLocks(bool entering = false);
    // Real seconds for the spot seeds (5.3 "Spots"); default: the frame time.
    void setRealClock(std::function<double()> f) { realClock_ = std::move(f); }
    double realSeconds() const { return realClock_ ? realClock_() : now_; }
    // The car list's base names (1.3 step 1: a remote car whose name is not among them is built
    // as vpcoop); empty = every name is known.
    void setCarNames(std::vector<std::string> names) { carNames_ = std::move(names); }
    bool knownCar(const std::string& name) const;
    // The network respawn counter (2.1) this machine uses.
    void setRespawnCounter(RespawnCounter* c) { respawn_ = c; }
    RespawnCounter& respawnCounter() { return *respawn_; }

    // ---- the host
    bool isHost() const { return self_ != 0 && self_ == host_; }
    void setSettings(const Settings& s);  // limited as the packing limits it, then sent
    const Settings& settings() const { return settings_; }
    bool haveSettings() const { return haveSettings_; }
    void describe(const SessionDesc& d);
    const SessionDesc& description() const { return desc_; }
    void setSealed(bool s);
    bool sealed() const { return desc_.sealed; }
    // The host can start when every other player is ready and (in a lobby game) at least two
    // players are present (1.3 step 2).
    bool canStart(std::string* why = nullptr) const;
    // Assigns the start slots, sends "start" and starts the game here too (1.3 step 3).
    bool start();
    void kick(PlayerId id);
    void returnToLobby();
    // ---- everyone
    // Leaves the session (the host sends "host quits" first).
    void quit();
    void chat(const std::string& text);
    // Join a Cruise or Cops and Robbers game that is already running (5.1, 5.3).
    bool canJoinRunningGame() const;
    bool joinRunningGame();

    // ---- every frame (also in the lobby): transport events and messages; game messages are
    // queued for the running game. `now`: this machine's clock in seconds.
    void pump(double now);

    // ---- state
    PlayerId self() const { return self_; }
    PlayerId host() const { return host_; }
    bool open() const { return open_; }
    bool closed() const { return closed_; }
    const std::string& closedReason() const { return closedReason_; }
    const std::vector<Player>& players() const { return players_; }  // session order, local included
    Player* find(PlayerId id);
    const Player* find(PlayerId id) const;
    std::string nameOf(PlayerId id) const;
    int playerCount() const { return int(players_.size()); }
    const std::vector<PlayerId>& slotTable() const { return slots_; }  // 10 ids (0 = empty)
    NetGame* game() { return game_.get(); }
    const NetGame* game() const { return game_.get(); }
    // A game was created (start, or a running game joined): the app builds its world and then
    // attaches it (NetGame::attach). Cleared by the call.
    bool takeGameStarting();
    // The game ended (return to lobby, host quit, kicked): the app tears its world down.
    bool takeGameEnded();
    std::vector<Notice> takeNotices();
    void notice(const Notice& n) { notices_.push_back(n); }
    void notice(int text, float seconds, const std::string& name = std::string(), bool upper = false);
    void noticeLine(int text, float seconds, const std::string& name, const std::string& line);
    // The two-line form for 5 s: `name` primary, text `text` (and `line`) secondary.
    void noticeTwoLines(int text, const std::string& name, const std::string& line = std::string());
    void chatLine(int text, const std::string& name = std::string(), const std::string& line = std::string());

    // ---- for the game
    const Vfs* vfs() const { return vfs_; }
    Transport* transport() { return t_; }
    // Network clock (3.6): `now` plus the offset learnt from the host.
    double clock() const { return now_ + offset_; }
    double localTime() const { return now_; }
    bool send(PlayerId to, const Writer& w, bool reliable = true);
    void sendAll(const Writer& w, bool reliable = true);
    // The cheat check (3.7) of a "loaded" (`joined` false) or "joined" event at the host.
    void cheatCheck(PlayerId from, uint32_t check, bool joined);
    bool hostIsCheater() const;
    bool selfIsCheater() const;
    // A transport-made host (1.3 step 6): set when the old host vanished.
    bool hostVanished() const { return hostVanished_; }
    // Counters for the tests.
    int timeRequests = 0;

private:
    void handle(const TransportEvent& ev);
    void message(PlayerId from, const std::vector<uint8_t>& data, bool reliable);
    void writeRecord(Writer& w, const PlayerRecord& r) const;
    PlayerRecord readRecord(Reader& r) const;
    void sendRecordTo(PlayerId to);
    void sendSettingsTo(PlayerId to);
    void addPlayer(PlayerId id);
    void removePlayer(PlayerId id);
    void createGame(bool lateJoin);
    void endGame();
    void requestTime();
    void hostRecordSeen(const PlayerRecord& r, bool changed);
    int modeQuitText() const;

    Transport* t_ = nullptr;
    const Vfs* vfs_ = nullptr;
    std::function<bool(const std::string&)> cityInstalled_;
    RespawnCounter* respawn_ = nullptr;
    std::function<int(const std::string&, int)> lockQuery_;
    std::function<double()> realClock_;
    std::vector<std::string> carNames_;
    PlayerId self_ = 0, host_ = 0;
    bool open_ = false, closed_ = false;
    std::string closedReason_;
    Player local_;
    std::vector<Player> players_;
    Settings settings_;
    bool haveSettings_ = false;
    bool cityChecked_ = false;
    SessionDesc desc_;
    std::vector<PlayerId> slots_;
    std::unique_ptr<NetGame> game_;
    bool gameStarting_ = false, gameEnded_ = false;
    std::vector<Notice> notices_;
    double now_ = 0, offset_ = 0;
    // Time request (3.6).
    bool timeAsked_ = false, timeSynced_ = false;
    double timeSentAt_ = 0;
    bool hostVanished_ = false;
    bool quitting_ = false;
};

}  // namespace net
}  // namespace cc
