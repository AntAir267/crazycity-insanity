#include "app/native_host.h"

#include <algorithm>

#include "core/strutil.h"
#include "game/player_files.h"
#include "native/files.h"
#include "platform/platform.h"

namespace cc {

namespace {
const char* kPlayers = "cci.players/";
// A relative path inside the players folder (no climbing out of it).
bool safeRel(const std::string& rel) { return !rel.empty() && rel[0] != '/' && rel.find("..") == std::string::npos; }
}  // namespace

void NativeHost::setClipboard(const std::string& text) {
    if (platform) platform->setClipboard(text);
}

std::string NativeHost::clipboardText() { return platform ? platform->clipboard() : std::string(); }

std::string NativeHost::storageGet(const std::string& key) {
    if (startsWith(key, kPlayers)) {
        std::string rel = key.substr(12);
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (!safeRel(rel)) return std::string();
        native::Bytes b;
        if (!native::readFile(native::joinPath(playersDir, rel), b)) return std::string();
        return game::base64Encode(Bytes(b.begin(), b.end()));
    }
    if (key == "cci.mp.name") return settings ? settings->get("net_name") : std::string();  // the network name
    return settings ? settings->get(key) : std::string();
}

bool NativeHost::storageSet(const std::string& key, const std::string& value) {
    if (startsWith(key, kPlayers)) {
        std::string rel = key.substr(12);
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (!safeRel(rel)) return false;
        Bytes b;
        if (!game::base64Decode(value, b)) return false;
        return native::writeFile(native::joinPath(playersDir, rel), native::Bytes(b.begin(), b.end()));
    }
    if (!settings) return false;
    settings->set(key == "cci.mp.name" ? std::string("net_name") : key, value);
    return settings->save();
}

void NativeHost::storageDel(const std::string& key) {
    if (startsWith(key, kPlayers)) {
        const std::string rel = key.substr(12);
        if (safeRel(rel)) native::removeFile(native::joinPath(playersDir, rel));
        return;
    }
    if (settings) {
        settings->set(key, "");
        settings->save();
    }
}

std::vector<std::string> NativeHost::storageKeys(const std::string& prefix) {
    std::vector<std::string> out;
    if (startsWith(kPlayers, prefix) || startsWith(prefix, kPlayers)) {
        for (const std::string& rel : native::listFilesRecursive(playersDir)) {
            if (iendsWith(rel, ".tmp")) continue;
            const std::string k = kPlayers + rel;
            if (startsWith(k, prefix)) out.push_back(k);
        }
    }
    return out;
}

}  // namespace cc
