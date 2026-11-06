// Our English stand-ins for the network game's mmlang.dll texts (formats/mmlang.md, "Multiplayer";
// which text shows when: game/multiplayer.md). The page shows the player's own mmlang.dll string by
// number when it has one; these are only for the plain overlay and the native tests.
#pragma once

#include <string>

namespace cc {
namespace net {

std::string netText(int id);

}  // namespace net
}  // namespace cc
