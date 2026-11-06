// A game around a drive, as the front end starts and ends it (app build only: needs the App):
// the race menu's environment choice, starting a Cruise or race session with the driver's
// records, next race, pause, restart, the game end, the front end's network games, and the
// network session's calls (the local record, the host's settings, ready, start, kick, chat,
// lobby, restart, leave). Replies are JSON like app/menu_data.h's.
#pragma once

#include <cstdint>
#include <string>

namespace cc {
namespace gameapi {

// After menu::start(): the network car choice follows the current driver's locks.
void afterStart();

void setEnvironment(int own, int timeOfDay, int weather, double traffic, double cops, int laps, double peds);
std::string begin(int cityIndex, int mode, int race, int carIndex, int paint, int opponents);
std::string nextRace(int cityIndex, int carIndex, int paint);
void setViewDistance(double m);
void pause(bool on);
void restart();
std::string end();
// The front end's network games.
void netPrepare(bool on);
void netGameStart(const std::string& netName, const std::string& car, int paint, const std::string& raceDir, int mode, int race);
std::string netGameEnd();
int debugRaceJump();
void hudDemo(int what, double a, double b, double c);

// ---- the network session (spec/game/multiplayer.md)
void netBegin(const std::string& name, const std::string& car, int paint);
void netEnd();
void netRecord(const std::string& name, const std::string& car, int paint, int team, bool slowLink);
void netReady(bool on);
void netSettings(int mode, const std::string& city, int race, int skill, int timeOfDay, int weather, int laps, double timeLimit,
                 double pedDensity, int subMode, int goldWeight, int limitType, int limitChoice);
std::string netDefaults(int mode, const std::string& city, int race, int skill);
void netDescribe(const std::string& name, const std::string& password, int maxPlayers);
bool netStart();
bool netJoinRunning();
void netKick(double id);
void netLobby();
void netQuit();
void netChat(const std::string& text);
bool netRestart();
std::string netState();

}  // namespace gameapi
}  // namespace cc
