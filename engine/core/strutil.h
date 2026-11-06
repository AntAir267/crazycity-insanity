// Small string helpers. Game data paths are compared without regard to ASCII case.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cc {

std::string toLower(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);
bool iendsWith(std::string_view s, std::string_view suffix);
bool istartsWith(std::string_view s, std::string_view prefix);
std::string trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep);

// Normalises a game path: backslashes become '/', and the result is lower-case (the key used
// for case-insensitive lookups).
std::string pathKey(std::string_view path);

// Last path component ("geometry/vpbug.pkg" -> "vpbug.pkg").
std::string baseName(std::string_view path);
// File name without its extension ("geometry/vpbug.pkg" -> "vpbug").
std::string stem(std::string_view path);

// Latin-1 bytes to UTF-8 (archive names and text files are Latin-1).
std::string latin1ToUtf8(std::string_view s);

// printf-style formatting into a std::string.
std::string strf(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

}  // namespace cc
