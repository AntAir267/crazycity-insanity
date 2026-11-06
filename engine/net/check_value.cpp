#include "net/check_value.h"

#include "core/strutil.h"
#include "game/player_files.h"
#include "vfs/vfs.h"

namespace cc {
namespace net {

uint32_t checkValueOfText(const uint8_t* data, size_t size) {
    game::Mm2Crc crc;
    size_t i = 0;
    while (i < size) {
        // One text line: up to and including '\n'; a NUL ends what the line contributes.
        size_t j = i;
        bool cut = false;
        while (j < size) {
            const uint8_t c = data[j];
            if (c == 0) cut = true;
            if (!cut) crc.add(&data[j], 1);
            ++j;
            if (c == '\n') break;
        }
        i = j;
    }
    return crc.value();
}

uint32_t carCheckValue(const Vfs& vfs, const std::string& car) {
    const std::string name = iequals(car, "vpcop") ? std::string("vpmustang99") : car;
    BytesPtr b = vfs.open("tune/vehicle/" + name + ".vehcarsim");
    if (!b) return 0;
    return checkValueOfText(b->data(), b->size());
}

}  // namespace net
}  // namespace cc
