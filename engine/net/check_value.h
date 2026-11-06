// Car check values (spec/game/multiplayer.md 3.7): the check value of formats/profiles.md over the
// bytes of tune/vehicle/<car>.vehcarsim read as text lines. A vpcop driver sends vpmustang99's.
#pragma once

#include <cstdint>
#include <string>

namespace cc {
class Vfs;
namespace net {

// The bytes in order, each line (up to and including its line feed) cut at a NUL byte.
uint32_t checkValueOfText(const uint8_t* data, size_t size);
// 0 when the file is missing.
uint32_t carCheckValue(const Vfs& vfs, const std::string& car);

}  // namespace net
}  // namespace cc
