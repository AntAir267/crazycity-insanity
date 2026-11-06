// The front end's services (app/fe_services.h) over the App and app/game_api.h.
#include <memory>

#include "app/app.h"
#include "app/fe_services.h"
#include "app/game_api.h"
#include "net/net_link.h"

namespace cc {

namespace {

class AppServices : public fe::Services {
public:
    explicit AppServices(App& a) : app_(a) {}
    std::string carList() override { return app_.carListJson(); }
    std::string cityList() override { return app_.cityListJson(); }
    std::string selectCar(int index) override { return app_.selectCar(index); }
    void setPaint(int p) override { app_.setPaint(p); }
    void setLod(int l) override { app_.setLod(l); }
    void setCarOption(const std::string& name, int v) override { app_.setCarOption(name, v); }
    void resetCamera() override { app_.resetCamera(); }
    void setView(int v) override { app_.setView(v); }
    std::string loadCity(int index) override { return app_.loadCity(index); }
    void setCityOption(const std::string& name, int v) override { app_.setCityOption(name, v); }
    std::string dropCar() override { return app_.dropCar(); }
    std::string startDrive(int mode, bool flat) override { return app_.startDrive(mode, flat); }
    void stopDrive() override {
        // Stopping a network game's drive leaves its session (the others see the player leave).
        if (app_.driveView().netActive()) app_.netEnd();
        app_.stopDrive();
    }
    void setDriveOption(const std::string& name, int v) override { app_.setDriveOption(name, v); }
    void setCopDensity(float d) override { app_.setCopDensity(d); }
    void setPointerLock(bool on) override { app_.platform().setPointerLock(on); }
    void setSoundOption(const std::string& name, int v) override { app_.setSoundOption(name, v); }
    std::string soundState() override { return app_.soundJson(); }
    void setMusicMode(int m) override { app_.setMusicMode(m); }
    void setMusicVolume(float v) override { app_.setMusicVolume(v); }
    std::string musicStatus() override { return app_.musicStatusJson(); }
    void clearGmBank() override { app_.setMusicGmBank("", Bytes()); }
    bool audioRunning() override { return app_.host().audioRunning(); }
    std::string raceList() override { return app_.raceListJson(); }
    std::string startRace(int mode, int race, bool pro, int opponents) override { return app_.startRace(mode, race, pro, opponents); }
    void restartRace() override { app_.restartRace(); }
    std::string nextRace() override { return app_.nextRace(); }
    std::string stats() override { return app_.statsJson(); }
    std::string readout() override { return app_.readout(); }
    bool driving() override { return app_.driveView().active(); }
    bool netDriving() override { return app_.driveView().netActive(); }
    void afterStart() override { gameapi::afterStart(); }
    void setEnvironment(int own, int tod, int weather, double traffic, double cops, int laps, double peds) override {
        gameapi::setEnvironment(own, tod, weather, traffic, cops, laps, peds);
    }
    std::string gameBegin(int city, int mode, int race, int car, int paint, int opponents) override {
        return gameapi::begin(city, mode, race, car, paint, opponents);
    }
    std::string gameNextRace(int city, int car, int paint) override { return gameapi::nextRace(city, car, paint); }
    void gameSetViewDistance(double m) override { gameapi::setViewDistance(m); }
    void gamePause(bool on) override { gameapi::pause(on); }
    void gameRestart() override { gameapi::restart(); }
    std::string gameEnd() override { return gameapi::end(); }
    void netPrepare(bool on) override { gameapi::netPrepare(on); }
    void netGameStart(const std::string& netName, const std::string& car, int paint, const std::string& raceDir, int mode, int race) override {
        gameapi::netGameStart(netName, car, paint, raceDir, mode, race);
    }
    std::string netGameEnd() override { return gameapi::netGameEnd(); }
    net::NetLink* netLink() override { return app_.netLink(); }
    void netBegin(const std::string& name, const std::string& car, int paint) override { gameapi::netBegin(name, car, paint); }
    void netEnd() override { gameapi::netEnd(); }
    void netRecord(const std::string& name, const std::string& car, int paint, int team, bool slowLink) override {
        gameapi::netRecord(name, car, paint, team, slowLink);
    }
    void netReady(bool on) override { gameapi::netReady(on); }
    void netSettings(int mode, const std::string& city, int race, int skill, int tod, int weather, int laps, double timeLimit, double peds,
                     int subMode, int gold, int limitType, int limitChoice) override {
        gameapi::netSettings(mode, city, race, skill, tod, weather, laps, timeLimit, peds, subMode, gold, limitType, limitChoice);
    }
    std::string netDefaults(int mode, const std::string& city, int race, int skill) override { return gameapi::netDefaults(mode, city, race, skill); }
    void netDescribe(const std::string& name, const std::string& password, int maxPlayers) override {
        gameapi::netDescribe(name, password, maxPlayers);
    }
    bool netStart() override { return gameapi::netStart(); }
    bool netJoinRunning() override { return gameapi::netJoinRunning(); }
    void netKick(double id) override { gameapi::netKick(id); }
    void netLobby() override { gameapi::netLobby(); }
    void netQuit() override { gameapi::netQuit(); }
    void netChat(const std::string& text) override { gameapi::netChat(text); }
    bool netRestart() override { return gameapi::netRestart(); }
    std::string netState() override { return gameapi::netState(); }
    double clock() override { return app_.clock(); }
    int gamepadButtons() override { return app_.gamepadButtons(); }
    bool controllerFeedback() override { return app_.input().pad.connected && app_.input().pad.feedback; }
    fe::RawInput rawInput() override {
        const InputState& in = app_.input();
        fe::RawInput r;
        for (int k = 1; k < 256; ++k)
            if (in.keyPressed[k]) r.keys.push_back(k);
        const GamepadState& p = in.pad;
        r.padConnected = p.connected;
        if (p.connected) {
            for (int b = 0; b < GamepadState::kButtons; ++b)
                if (p.pressed[b]) r.buttons.push_back(b);
            for (int a = 0; a < 8; ++a) r.axis[a] = p.axis[a];
            r.axisCount = p.axisCount;
            r.hat = p.hat;
        }
        return r;
    }

private:
    App& app_;
};

}  // namespace

std::unique_ptr<fe::Services> makeAppServices(App& app) { return std::make_unique<AppServices>(app); }

}  // namespace cc
