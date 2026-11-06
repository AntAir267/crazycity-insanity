// What the front end (app/front_end.h) needs from the running program: the viewer's views and
// options, a game's start and end, the drive's HUD state, sound and music, the network session.
// The App implements it (app/app_services.cpp); cctool's UI tests have their own (no GL, no
// drive). Replies are the engine's JSON, as the web page read them.
#pragma once

#include <string>
#include <vector>

namespace cc {
namespace net {
class NetLink;
}

namespace fe {

// This frame's raw input, for the customise-controls screen (controls.md's binding components).
struct RawInput {
    std::vector<int> keys;     // DirectInput scan codes that went down this frame
    bool padConnected = false;
    std::vector<int> buttons;  // controller buttons (0-based: "button 1" is 0) that went down
    float axis[8] = {};        // controller axes, −1 … 1
    int axisCount = 0;
    int hat = 0;               // hat directions: 1 up, 2 right, 4 down, 8 left
};

class Services {
public:
    virtual ~Services() = default;
    // ---- the viewer's views
    virtual std::string carList() = 0;            // [{index, name, description, group, mod, archive, colors}]
    virtual std::string cityList() = 0;           // [{name, map, raceDir, test}]
    virtual std::string selectCar(int index) = 0; // car info {loaded, ...}
    virtual void setPaint(int p) = 0;
    virtual void setLod(int l) = 0;
    virtual void setCarOption(const std::string& name, int v) = 0;
    virtual void resetCamera() = 0;
    virtual void setView(int v) = 0;              // 0 car, 1 city
    virtual std::string loadCity(int index) = 0;  // {loaded, summary, message}
    virtual void setCityOption(const std::string& name, int v) = 0;
    virtual std::string dropCar() = 0;
    virtual std::string startDrive(int mode, bool flat) = 0;
    virtual void stopDrive() = 0;                 // (leaves a network game's session)
    virtual void setDriveOption(const std::string& name, int v) = 0;
    virtual void setCopDensity(float d) = 0;
    virtual void setPointerLock(bool on) = 0;
    // ---- sound and music
    virtual void setSoundOption(const std::string& name, int v) = 0;
    virtual std::string soundState() = 0;
    virtual void setMusicMode(int m) = 0;
    virtual void setMusicVolume(float v) = 0;
    virtual std::string musicStatus() = 0;
    virtual void clearGmBank() = 0;
    virtual bool audioRunning() = 0;              // the browser has started the audio output
    // ---- the viewer's race card
    virtual std::string raceList() = 0;
    virtual std::string startRace(int mode, int race, bool pro, int opponents) = 0;
    virtual void restartRace() = 0;
    virtual std::string nextRace() = 0;
    virtual std::string stats() = 0;              // draw calls, triangles, frame times
    virtual std::string readout() = 0;            // the camera / car position line
    // ---- the drive
    virtual bool driving() = 0;
    virtual bool netDriving() = 0;
    // ---- a game (app/game_api.h)
    virtual void afterStart() = 0;
    virtual void setEnvironment(int own, int tod, int weather, double traffic, double cops, int laps, double peds) = 0;
    virtual std::string gameBegin(int city, int mode, int race, int car, int paint, int opponents) = 0;
    virtual std::string gameNextRace(int city, int car, int paint) = 0;
    virtual void gameSetViewDistance(double m) = 0;
    virtual void gamePause(bool on) = 0;
    virtual void gameRestart() = 0;
    virtual std::string gameEnd() = 0;
    virtual void netPrepare(bool on) = 0;
    virtual void netGameStart(const std::string& netName, const std::string& car, int paint, const std::string& raceDir, int mode,
                              int race) = 0;
    virtual std::string netGameEnd() = 0;
    // ---- the network session
    virtual net::NetLink* netLink() = 0;          // null: no network games in this build
    virtual void netBegin(const std::string& name, const std::string& car, int paint) = 0;
    virtual void netEnd() = 0;
    virtual void netRecord(const std::string& name, const std::string& car, int paint, int team, bool slowLink) = 0;
    virtual void netReady(bool on) = 0;
    virtual void netSettings(int mode, const std::string& city, int race, int skill, int tod, int weather, int laps, double timeLimit,
                             double peds, int subMode, int gold, int limitType, int limitChoice) = 0;
    virtual std::string netDefaults(int mode, const std::string& city, int race, int skill) = 0;
    virtual void netDescribe(const std::string& name, const std::string& password, int maxPlayers) = 0;
    virtual bool netStart() = 0;
    virtual bool netJoinRunning() = 0;
    virtual void netKick(double id) = 0;
    virtual void netLobby() = 0;
    virtual void netQuit() = 0;
    virtual void netChat(const std::string& text) = 0;
    virtual bool netRestart() = 0;
    virtual std::string netState() = 0;
    // ---- program
    virtual double clock() = 0;
    virtual int gamepadButtons() = 0;             // buttons of the first game controller
    virtual bool controllerFeedback() = 0;        // the controller has force feedback or rumble
    virtual RawInput rawInput() = 0;
};

}  // namespace fe
}  // namespace cc
