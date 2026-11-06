#include "net/session.h"

#include <algorithm>

#include "core/log.h"
#include "core/strutil.h"
#include "net/check_value.h"
#include "net/net_game.h"
#include "net/texts.h"

namespace cc {
namespace net {

RespawnCounter& programRespawnCounter() {
    static RespawnCounter c;
    return c;
}

Session::Session(Transport* transport, const Vfs* vfs) : t_(transport), vfs_(vfs), respawn_(&programRespawnCounter()) {}

Session::~Session() = default;

Player* Session::find(PlayerId id) {
    for (Player& p : players_)
        if (p.id == id) return &p;
    return nullptr;
}

const Player* Session::find(PlayerId id) const {
    for (const Player& p : players_)
        if (p.id == id) return &p;
    return nullptr;
}

std::string Session::nameOf(PlayerId id) const {
    const Player* p = find(id);
    return p ? p->rec.name : std::string("?");
}

void Session::notice(int text, float seconds, const std::string& name, bool upper) {
    Notice n;
    n.kind = Notice::Primary;
    n.text = text;
    n.name = name;
    n.seconds = seconds;
    n.upper = upper;
    notices_.push_back(n);
}

std::string joinWords(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + " " + b;
}

void Session::noticeTwoLines(int text, const std::string& name, const std::string& line) {
    notice(text, 5.0f, name);
    notices_.back().line = line;
    notices_.back().twoLines = true;
}

bool Session::knownCar(const std::string& name) const {
    if (carNames_.empty()) return true;
    for (const std::string& c : carNames_)
        if (c == name) return true;  // base names compared exactly, case included
    return false;
}

void Session::applyLocks(bool entering) {
    PlayerRecord& r = local_.rec;
    const bool cops = haveSettings_ && settings_.mode == ModeCops;
    if (cops && settings_.subMode == CopsAndRobbers) {
        // Every car but the team's is locked: the team's car with paint 0.
        r.car = r.team == 0 ? "vpcop" : "vpmustang99";
        r.paint = 0;
    } else if (entering && !cops && lockQuery_) {
        const int lock = lockQuery_(r.car, r.paint);
        if (lock == 1) {
            r.car = "vpbug";
            r.paint = 0;
        } else if (lock == 2) {
            r.paint = 0;
        }
    }
    publishRecord();
}

void Session::noticeLine(int text, float seconds, const std::string& name, const std::string& line) {
    notice(text, seconds, name);
    notices_.back().line = line;
}

void Session::chatLine(int text, const std::string& name, const std::string& line) {
    Notice n;
    n.kind = Notice::Chat;
    n.text = text;
    n.name = name;
    n.line = line;
    n.seconds = 15.0f;
    n.sound = true;
    notices_.push_back(n);
}

std::vector<Notice> Session::takeNotices() {
    std::vector<Notice> v;
    v.swap(notices_);
    return v;
}

bool Session::takeGameStarting() {
    const bool b = gameStarting_;
    gameStarting_ = false;
    return b;
}

bool Session::takeGameEnded() {
    const bool b = gameEnded_;
    gameEnded_ = false;
    return b;
}

// ---- sending

bool Session::send(PlayerId to, const Writer& w, bool reliable) {
    if (!t_ || !open_) return false;
    return t_->send(to, reliable, w.bytes());
}

void Session::sendAll(const Writer& w, bool reliable) {
    if (!t_ || !open_) return;
    t_->send(0, reliable, w.bytes());
}

// ---- records

void Session::writeRecord(Writer& w, const PlayerRecord& r) const {
    w.str(r.name);
    w.str(r.car);
    w.u8(uint8_t(r.paint));
    w.u8(uint8_t(r.team));
    uint8_t f = 0;
    if (r.inGame) f |= 1;
    if (r.host) f |= 2;
    if (r.ready) f |= 4;
    if (r.slowLink) f |= 8;
    if (r.cheatCheck) f |= 16;
    if (r.waitForAll) f |= 32;
    w.u8(f);
    w.str(r.city);
    w.u32(r.check);
}

PlayerRecord Session::readRecord(Reader& r) const {
    PlayerRecord p;
    p.name = r.str();
    p.car = r.str();
    p.paint = r.u8();
    p.team = r.u8() & 1;
    const uint8_t f = r.u8();
    p.inGame = (f & 1) != 0;
    p.host = (f & 2) != 0;
    p.ready = (f & 4) != 0;
    p.slowLink = (f & 8) != 0;
    p.cheatCheck = (f & 16) != 0;
    p.waitForAll = (f & 32) != 0;
    p.city = r.str();
    p.check = r.u32();
    return p;
}

void Session::publishRecord() {
    if (vfs_) local_.rec.check = carCheckValue(*vfs_, local_.rec.car);
    local_.rec.host = isHost();
    if (local_.rec.host) local_.rec.cheatCheck = local_.rec.waitForAll = true;  // no menu: always on
    local_.haveRecord = true;
    if (Player* p = find(self_)) {
        p->rec = local_.rec;
        p->haveRecord = true;
    }
    if (!open_) return;
    Writer w(Msg::Record);
    writeRecord(w, local_.rec);
    sendAll(w);
}

void Session::sendRecordTo(PlayerId to) {
    Writer w(Msg::Record);
    writeRecord(w, local_.rec);
    send(to, w);
}

void Session::setReady(bool r) {
    local_.rec.ready = r;
    publishRecord();
}

// ---- settings

void Session::setSettings(const Settings& s) {
    if (!isHost() && open_) return;
    const bool wasCops = haveSettings_ && settings_.mode == ModeCops;
    const int wasSub = settings_.subMode;
    settings_ = limited(s);
    haveSettings_ = true;
    const bool copsChange = (settings_.mode == ModeCops || wasCops) &&
                            !(wasCops && settings_.mode == ModeCops && wasSub == settings_.subMode);
    desc_.mode = settings_.mode;
    desc_.city = settings_.city;
    if (isHost() && t_) t_->describe(desc_);
    if (!open_) return;
    Writer w(Msg::Settings);
    const PackedSettings p = pack(settings_);
    for (uint32_t x : p.w) w.u32(x);
    w.str(p.city);
    sendAll(w);
    if (copsChange) applyLocks();
}

void Session::sendSettingsTo(PlayerId to) {
    if (!haveSettings_) return;
    Writer w(Msg::Settings);
    const PackedSettings p = pack(settings_);
    for (uint32_t x : p.w) w.u32(x);
    w.str(p.city);
    send(to, w);
}

void Session::describe(const SessionDesc& d) {
    const bool sealedNow = desc_.sealed;
    desc_ = d;
    desc_.sealed = sealedNow || d.sealed;
    if (isHost() && t_) t_->describe(desc_);
}

void Session::setSealed(bool s) {
    desc_.sealed = s;
    if (isHost() && t_) t_->describe(desc_);
}

// ---- start

bool Session::canStart(std::string* why) const {
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (!isHost()) return fail("only the host starts the game");
    if (!haveSettings_) return fail("no settings");
    if (game_) return fail("a game is running");
    if (players_.size() < 2) return fail("at least two players");
    for (const Player& p : players_)
        if (p.id != self_ && (!p.haveRecord || !p.rec.ready)) return fail("not everyone is ready");
    return true;
}

bool Session::start() {
    if (!canStart()) return false;
    // A table of 10 slots, cleared; each player in session order takes the first empty one.
    slots_.assign(10, 0);
    for (const Player& p : players_) {
        for (PlayerId& s : slots_)
            if (s == 0) {
                s = p.id;
                break;
            }
    }
    Writer w(Msg::Start);
    for (PlayerId s : slots_) w.u32(s);
    sendAll(w);
    createGame(false);
    return true;
}

bool Session::canJoinRunningGame() const {
    if (game_ || !open_ || !haveSettings_ || isHost()) return false;
    if (settings_.mode != ModeCruise && settings_.mode != ModeCops) return false;
    const Player* h = find(host_);
    return h && h->haveRecord && h->rec.inGame;
}

bool Session::joinRunningGame() {
    if (!canJoinRunningGame()) return false;
    slots_.assign(10, 0);
    createGame(true);
    return true;
}

void Session::createGame(bool lateJoin) {
    if (game_) endGame();
    game_ = std::make_unique<NetGame>(*this, settings_, lateJoin);
    gameStarting_ = true;
}

void Session::endGame() {
    if (!game_) return;
    game_->detach();
    game_.reset();
    gameEnded_ = true;
    local_.rec.inGame = false;
    local_.rec.ready = false;
    if (open_ && !quitting_) applyLocks(true);  // entering the lobby screen
}

void Session::kick(PlayerId id) {
    if (!isHost() || id == self_) return;
    Writer w(Msg::Kick);
    send(id, w);
    if (t_) t_->kick(id);
}

void Session::returnToLobby() {
    if (!isHost()) return;
    Writer w(Msg::ReturnToLobby);
    sendAll(w);
    notice(45, 3.0f);
    setSealed(false);
    endGame();
}

int Session::modeQuitText() const {
    switch (game_ ? game_->mode() : settings_.mode) {
        case ModeCheckpoint: return 151;
        case ModeCircuit: return 109;
        case ModeBlitz: return 98;
        case ModeCops: return 132;
        default: return 157;
    }
}

void Session::quit() {
    if (!open_) return;
    quitting_ = true;
    if (isHost()) {
        Writer w(Msg::HostQuits);
        sendAll(w);
    } else {
        Writer w(Msg::PlayerLeaves);
        sendAll(w);
    }
    endGame();
    if (t_) t_->leave();
    open_ = false;
    closed_ = true;
    closedReason_ = "left";
}

void Session::chat(const std::string& text) {
    if (istartsWith(text, "/rc")) return;  // MM2's developer commands: not sent
    Writer w(Msg::Chat);
    w.str(text);
    sendAll(w);
    if (!istartsWith(text, "/wav")) chatLine(0, local_.rec.name, text);
}

// ---- cheaters (3.7)

void Session::cheatCheck(PlayerId from, uint32_t check, bool joined) {
    if (!isHost() || !vfs_) return;
    Player* p = find(from);
    if (!p || !p->haveRecord) return;
    const Player* h = find(self_);
    if (h && !h->rec.cheatCheck) return;
    // A car name this machine does not know is built as vpcoop: its value is compared (1.3).
    const uint32_t mine = carCheckValue(*vfs_, knownCar(p->rec.car) ? p->rec.car : std::string("vpcoop"));
    if (mine == check || p->cheater) return;
    chatLine(joined ? 43 : 41, p->rec.name);
    p->cheater = true;
    Writer w(Msg::Cheater);
    w.u32(from);
    sendAll(w);
}

bool Session::hostIsCheater() const {
    const Player* h = find(host_);
    return h && h->cheater;
}

bool Session::selfIsCheater() const {
    const Player* p = find(self_);
    return p && p->cheater;
}

// ---- clock (3.6)

void Session::requestTime() {
    if (isHost() || !open_) return;
    Writer w(Msg::TimeRequest);
    send(host_, w);
    timeSentAt_ = now_;
    timeAsked_ = true;
    ++timeRequests;
}

void Session::hostRecordSeen(const PlayerRecord& r, bool changed) {
    // The joiner copies the host's two switches (1.1).
    local_.rec.cheatCheck = r.cheatCheck;
    local_.rec.waitForAll = r.waitForAll;
    // Once, when this machine first learns that the host is in the game (or any changed record
    // of the host that says so).
    (void)changed;
    if (!timeAsked_ && r.inGame) requestTime();
}

// ---- the session's players

void Session::addPlayer(PlayerId id) {
    if (find(id)) return;
    Player p;
    p.id = id;
    if (id == self_) {
        p.rec = local_.rec;
        p.haveRecord = true;
    }
    players_.push_back(p);
}

void Session::removePlayer(PlayerId id) {
    players_.erase(std::remove_if(players_.begin(), players_.end(), [&](const Player& p) { return p.id == id; }),
                   players_.end());
}

// ---- every frame

void Session::pump(double now) {
    now_ = now;
    if (!t_) return;
    TransportEvent ev;
    while (t_->poll(ev)) handle(ev);
    if (game_ && game_->quitWanted) {
        quit();
        closedReason_ = "host lost";
    }
}

void Session::handle(const TransportEvent& ev) {
    switch (ev.kind) {
        case TransportEvent::Opened: {
            self_ = ev.self;
            host_ = ev.host;
            local_.id = self_;
            open_ = true;
            closed_ = false;
            players_.clear();
            for (PlayerId id : ev.roster) addPlayer(id);
            addPlayer(self_);
            if (isHost()) {
                if (haveSettings_) setSettings(settings_);
                t_->describe(desc_);
            }
            applyLocks(true);  // entering multiplayer
            logInfo("network: in the session as player %u (%s), %d players", unsigned(self_),
                    isHost() ? "host" : "guest", int(players_.size()));
            break;
        }
        case TransportEvent::PeerJoined: {
            addPlayer(ev.peer);
            // Records are readable at any time (DirectPlay's player data): every machine sends its
            // own to the newcomer; the host also its settings.
            sendRecordTo(ev.peer);
            if (isHost()) sendSettingsTo(ev.peer);
            logInfo("network: player %u joined", unsigned(ev.peer));
            break;
        }
        case TransportEvent::PeerLeft: {
            if (!open_) break;  // this machine has left already
            const std::string name = nameOf(ev.peer);
            if (game_) game_->queueLeft(ev.peer, name);
            else if (find(ev.peer)) noticeTwoLines(38, name);
            removePlayer(ev.peer);
            logInfo("network: player %u (%s) left", unsigned(ev.peer), name.c_str());
            break;
        }
        case TransportEvent::HostChanged: {
            host_ = ev.host;
            hostVanished_ = true;
            if (isHost()) {
                setSettings(settings_);
                publishRecord();  // the new host marks itself host in its record
            }
            if (game_) game_->queueHostChanged(ev.host);
            break;
        }
        case TransportEvent::Message:
            message(ev.peer, ev.data, ev.reliable);
            break;
        case TransportEvent::Closed: {
            if (ev.reason == "kicked") chatLine(44);
            else if (istartsWith(ev.reason, "refused")) chatLine(0, std::string(), "Cannot join: " + ev.reason.substr(8));
            endGame();
            open_ = false;
            closed_ = true;
            closedReason_ = ev.reason;
            break;
        }
    }
}

void Session::message(PlayerId from, const std::vector<uint8_t>& data, bool reliable) {
    if (data.empty() || from == self_) return;
    Reader r(data);
    const Msg type = Msg(r.u8());
    switch (type) {
        case Msg::Record: {
            Player* p = find(from);
            if (!p) {
                addPlayer(from);
                p = find(from);
            }
            const PlayerRecord rec = readRecord(r);
            if (!r.ok()) return;
            const bool changed = !p->haveRecord || p->rec.inGame != rec.inGame;
            p->rec = rec;
            p->haveRecord = true;
            if (from == host_) hostRecordSeen(rec, changed);
            return;
        }
        case Msg::Settings: {
            if (from != host_) return;
            PackedSettings p;
            for (uint32_t& x : p.w) x = r.u32();
            p.city = r.str();
            if (!r.ok()) return;
            const bool wasCops = haveSettings_ && settings_.mode == ModeCops;
            const int wasSub = settings_.subMode;
            settings_ = unpack(p);
            haveSettings_ = true;
            // Settings for Cops and Robbers, or leaving it: the locks again (1.3 step 1).
            if (settings_.mode == ModeCops || wasCops) {
                if (!(wasCops && settings_.mode == ModeCops && wasSub == settings_.subMode)) applyLocks();
            }
            if (!cityChecked_) {
                cityChecked_ = true;
                if (cityInstalled_ && !cityInstalled_(settings_.city)) {
                    // "Race locale not installed": this machine cannot join.
                    chatLine(84);
                    quit();
                    closedReason_ = "city not installed";
                }
            }
            return;
        }
        case Msg::Start: {
            if (from != host_) return;
            slots_.assign(10, 0);
            for (PlayerId& s : slots_) s = r.u32();
            if (!r.ok()) return;
            createGame(false);
            return;
        }
        case Msg::Chat: {
            const std::string text = r.str();
            if (!r.ok() || istartsWith(text, "/wav")) return;  // sent but not shown
            chatLine(0, nameOf(from), text);
            return;
        }
        case Msg::TimeRequest: {
            if (!isHost()) return;
            Writer w(Msg::TimeReply);
            w.f64(clock());
            send(from, w);
            return;
        }
        case Msg::TimeReply: {
            if (from != host_ || !timeAsked_) return;
            const double hostClock = r.f64();
            const double rtt = now_ - timeSentAt_;
            if (rtt > 0.5) {
                requestTime();  // too slow: asked again
            } else {
                // The full round trip, not half (3.6).
                offset_ = (rtt + hostClock) - now_;
                timeSynced_ = true;
            }
            return;
        }
        case Msg::HostQuits: {
            if (from != host_) return;
            chatLine(modeQuitText());
            quit();
            closedReason_ = "host quit";
            return;
        }
        case Msg::ReturnToLobby: {
            if (from != host_) return;
            chatLine(45);
            endGame();
            return;
        }
        case Msg::Kick: {
            if (from != host_) return;
            chatLine(44);
            endGame();
            if (t_) t_->leave();
            open_ = false;
            closed_ = true;
            closedReason_ = "kicked";
            return;
        }
        case Msg::PlayerLeaves:
            return;  // the transport's notice follows
        case Msg::Cheater: {
            const PlayerId id = r.u32();
            if (!r.ok()) return;
            if (Player* p = find(id)) {
                p->cheater = true;
                if (id == self_) chatLine(46);
                else chatLine(47, p->rec.name);
            }
            return;
        }
        default:
            break;
    }
    (void)reliable;
    if (game_) game_->queueMessage(from, data);
}

}  // namespace net
}  // namespace cc
