#include "net/texts.h"

#include "core/strutil.h"

namespace cc {
namespace net {

std::string netText(int id) {
    if (id >= 31 && id <= 37) return strf("Waiting for %d player%s", id - 30, id == 31 ? "" : "s");
    switch (id) {
        // Lobby and in-game notices (31 – 47); a name is printed in front of most.
        case 38: return "has left the game";
        case 40: return "The host left; the game ends";
        case 41: return "is using a modified car (loaded)";
        case 42: return "has joined the game";
        case 43: return "is using a modified car (joined)";
        case 44: return "You were removed from the game";
        case 45: return "Back to the lobby";
        case 46: return "Your car does not match the host's; you cannot drive";
        case 47: return "is marked as a cheater";
        case 84: case 85: case 86: return "The host's city is not installed";
        // Blitz.
        case 89: return "Ready";
        case 90: return "Set";
        case 91: return "Go!";
        case 92: return "Wrecked: 5 second penalty";
        case 93: return "You finished in";
        case 94: return "Time's up!";
        case 95: return "You finished in";
        case 96: return "finished in";
        case 97: return "Time's up!";
        case 98: return "The host has quit";
        case 99: return "finished in";
        // Circuit.
        case 100: return "Race over";
        case 101: return "Ready";
        case 102: return "Set";
        case 103: return "Go!";
        case 104: return "Wrecked: 5 second penalty";
        case 105: return "You finished in";
        case 106: return "You finished in";
        case 107: return "finished in";
        case 109: return "The host has quit";
        case 110: return "finished in";
        case 111: return "Race over";
        // Cops and Robbers.
        case 112: return "The gold was knocked loose!";
        case 113: return "Go!";
        case 114: return "Wrecked: you lost the gold";
        case 115: return "You have the gold!";
        case 116: return "Gold delivered to the hideout!";
        case 117: return "Gold delivered to the bank!";
        case 118: return "Time is up";
        case 119: return "You reached the point limit!";
        case 120: return "A player reached the point limit";
        case 121: return "A team reached the point limit";
        case 122: return "Cops";
        case 123: return "Robbers";
        case 124: return "Blue team";
        case 125: return "Red team";
        case 126: return "Robbers";
        case 127: return "Cops";
        case 128: return "Red team";
        case 129: return "Blue team";
        case 130: return "Time is up";
        case 131: return "The point limit was reached";
        case 132: return "The host has quit";
        case 133: return "has the gold";
        case 134: return "You have the gold!";
        case 135: return "has the gold";
        case 136: return "dropped the gold";
        case 137: return "delivered the gold";
        case 138: return "20 minutes left";
        case 139: return "15 minutes left";
        case 140: return "10 minutes left";
        case 141: return "5 minutes left";
        case 142: return "1 minute left";
        // Checkpoint race.
        case 143: return "Race over";
        case 144: return "Ready";
        case 145: return "Set";
        case 146: return "Go!";
        case 147: return "Wrecked: 5 second penalty";
        case 148: return "You finished in";
        case 149: return "You finished in";
        case 150: return "finished in";
        case 151: return "The host has quit";
        case 152: return "finished in";
        case 153: return "Race over";
        // Cruise.
        case 154: return "Go!";
        case 155: return "Wrecked: 5 second penalty";
        case 156: return "Time is up";
        case 157: return "The host has quit";
        // Mode names (formats/mmlang.md, "Race and car choices").
        case 5: return "Circuit";
        case 6: return "Checkpoint";
        case 7: return "Blitz";
        case 8: return "free-for-all";
        case 9: return "cops and robbers";
        case 10: return "robber teams";
        case 11: return "Cops and Robbers:";
        case 80: return "Cruise";
        case 264: return "Cops";
        case 265: return "Robbers";
        case 266: return "Blue";
        case 267: return "Red";
        case 499: return "did not finish";
        default: break;
    }
    return strf("[%d]", id);
}

}  // namespace net
}  // namespace cc
