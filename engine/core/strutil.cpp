#include "core/strutil.h"

#include <cstdarg>
#include <cstdio>

namespace cc {

static inline char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

std::string toLower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = lowerAscii(c);
    return r;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    return true;
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool iendsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
}

bool istartsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string pathKey(std::string_view path) {
    std::string r(path);
    for (char& c : r) c = (c == '\\') ? '/' : lowerAscii(c);
    return r;
}

std::string baseName(std::string_view path) {
    size_t p = path.find_last_of("/\\");
    return std::string(p == std::string_view::npos ? path : path.substr(p + 1));
}

std::string stem(std::string_view path) {
    std::string b = baseName(path);
    size_t d = b.find_last_of('.');
    if (d != std::string::npos && d > 0) b.resize(d);
    return b;
}

std::string latin1ToUtf8(std::string_view s) {
    std::string r;
    r.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 0x80) {
            r.push_back(char(c));
        } else {
            r.push_back(char(0xC0 | (c >> 6)));
            r.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return r;
}

std::string strf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return std::string();
    if (size_t(n) < sizeof buf) return std::string(buf, size_t(n));
    std::string big(size_t(n) + 1, '\0');
    va_start(ap, fmt);
    std::vsnprintf(&big[0], big.size(), fmt, ap);
    va_end(ap);
    big.resize(size_t(n));
    return big;
}

}  // namespace cc
