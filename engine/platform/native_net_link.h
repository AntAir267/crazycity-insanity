// The native program's network link (net/net_link.h) for the Multiplayer screens: hosting,
// joining and the game list through the App's native transport (platform/native_transport.*),
// "host on this computer" with the engine's own session server (platform/host_server.*, LAN mode
// on request), LAN games found with LanSearch, how each player is reached (direct UDP or the
// relay), and the settings kept in native.cfg: net_server, net_direct, net_udp_port,
// net_host_port, net_host_lan, net_host_here, net_ca_file, net_verify_tls (net_name is the
// multiplayer name, kept through the UI's storage: app/native_host.cpp).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "net/net_link.h"

namespace cc {
class App;
namespace native {
class Settings;
}
namespace net {

class NativeTransport;
class LanSearch;
struct NativeNetConfig;

class NativeNetLink : public NetLink {
public:
    NativeNetLink(App& app, native::Settings* settings);
    ~NativeNetLink() override;
    // The transport's settings from native.cfg into the App (the next session uses them).
    void applySettings();

    std::string defaultServer() const override;
    void host(const std::string& server, const SessionDesc& desc) override;
    void join(const std::string& server, const std::string& code, const std::string& password, const std::string& version) override;
    void list(const std::string& server) override;
    void leave() override;
    std::string code() const override;
    std::string inviteUrl(const std::string& code, const std::string& server) const override;
    bool takeSessions(std::vector<SessionInfo>& out) override;
    bool takeError(std::string& line) override;

    void service() override;
    bool canHostHere() const override { return true; }
    bool hostHere() const override;
    void setHostHere(bool on) override;
    bool lan() const override;
    void setLan(bool on) override;
    bool startLocalServer(std::string& error) override;
    void stopLocalServer() override;
    bool localServerRunning() const override;
    std::string localServerUrl() const override;
    std::vector<std::string> localServerUrls() const override;
    std::string linkTo(double playerId) const override;
    std::string statusLine() const override;
    void rememberServer(const std::string& typed) override;

private:
    NativeTransport* transport() const;
    void save();
    App& app_;
    native::Settings* settings_;
    std::vector<std::string> errors_;
    std::string lastFailure_;          // the transport's last error, reported once
    bool stopServerOnLeave_ = false;
    int stopIn_ = 0;                   // frames until the server stops after leaving
    // The game list: its own transport (no session needed) and the LAN search.
    std::unique_ptr<NativeTransport> lister_;
    std::unique_ptr<LanSearch> lan_;
    double listStarted_ = 0;
    bool listing_ = false;
    std::vector<SessionInfo> listed_;
};

}  // namespace net
}  // namespace cc
