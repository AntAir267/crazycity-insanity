// The front end's words: the player's own mmlang.dll text by number when it is loaded
// (spec/formats/mmlang.md), else our own English labels for the same numbers.
#pragma once

#include <array>
#include <string>

#include "core/json_value.h"

namespace cc {
namespace fe {

std::string T(int id);                       // the player's text, else ours, else ""
std::string T(int id, const std::string& fallback);
std::string label(int id);                   // without a trailing colon or spaces
std::string labelled(int id, const std::string& v);  // "Label value"
bool playerWords();                          // an mmlang.dll is loaded

std::string modeName(int m);                 // results titles (5, 6, 7, 12; Cruise 80)
std::string listName(int m);                 // the race menu's list words (585 …, 12)
std::string skillName(int s);
std::string fmtTime(double t);               // M:SS:hh, "–:––:––" for none
std::string fmtThousands(long long v);       // 1,234
std::string pct(double v);                   // 50%
std::string fmtSecs(double t);               // M:SS, "none"
std::string fmt2(double v);                  // 0.00

// The customise-controls screen.
constexpr int kActions = 34;
extern const int kActionText[kActions];
std::string bindingName(const JValue& b);    // [kind, device, component]
std::string bindingName(int kind, int device, int component);
// Actions 5 – 10, 13 – 17 and 31 are held (keys and buttons kind 1, axes and the hat kind 2); the
// others are press actions (kind 3, keys and buttons only): controls.md.
bool heldAction(int action);

// Multiplayer words.
std::string netModeName(int m);
std::string subName(int k);
std::string teamName(int sub, int t);
bool isRaceMode(int m);

}  // namespace fe
}  // namespace cc
