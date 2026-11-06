// The browser's network link (net/net_link.h) over web/net.js (window.CCINet): the session
// server's WebSocket and the WebRTC data channels live in the page; sessions lists and errors
// come back through cc_netlink_sessions / cc_netlink_error, the session server the page found
// through cc_netlink_set_default.
#pragma once

#include <string>
#include <vector>

#include "net/net_link.h"

namespace cc {
namespace net {

class WebNetLink : public NetLink {
public:
    static WebNetLink& get();
    std::string defaultServer() const override;
    void host(const std::string& server, const SessionDesc& desc) override;
    void join(const std::string& server, const std::string& code, const std::string& password, const std::string& version) override;
    void list(const std::string& server) override;
    void leave() override;
    std::string code() const override;
    std::string inviteUrl(const std::string& code, const std::string& server) const override;
    bool takeSessions(std::vector<SessionInfo>& out) override;
    bool takeError(std::string& line) override;

    // From the page.
    void setDefault(const std::string& url) { default_ = url; }
    void pushSessions(const std::string& json);
    void pushError(const std::string& line) { errors_.push_back(line); }

private:
    std::string default_;
    std::vector<SessionInfo> sessions_;
    bool haveSessions_ = false;
    std::vector<std::string> errors_;
};

}  // namespace net
}  // namespace cc
