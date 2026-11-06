#include "native/files.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace cc {
namespace native {

namespace fs = std::filesystem;

namespace {
std::string withSlash(std::string s) {
    if (!s.empty() && s.back() != '/') s += '/';
    return s;
}
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
}  // namespace

std::string homeDir() {
    const char* h = std::getenv("HOME");
#ifdef _WIN32
    if (!h || !*h) h = std::getenv("USERPROFILE");
#endif
    return h && *h ? std::string(h) : std::string(".");
}

std::string defaultConfigDir() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const std::string base = x && *x ? std::string(x) : homeDir() + "/.config";
    return withSlash(base) + "crazycity-insanity/";
}

std::string defaultDataDir() {
    const char* x = std::getenv("XDG_DATA_HOME");
    const std::string base = x && *x ? std::string(x) : homeDir() + "/.local/share";
    return withSlash(base) + "crazycity-insanity/";
}

bool readFile(const std::string& path, Bytes& out) {
    out.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

bool writeFile(const std::string& path, const Bytes& data) {
    const fs::path p(path);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        std::remove(tmp.c_str());
        return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool removeFile(const std::string& path) {
    std::error_code ec;
    return fs::remove(path, ec);
}

bool makeDirs(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return fs::is_directory(dir, ec);
}

bool isDir(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool isFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

std::vector<std::string> listDir(const std::string& dir, bool files, bool dirs) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const bool d = it->is_directory(e2);
        if ((d && dirs) || (!d && files && it->is_regular_file(e2))) out.push_back(it->path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> listFilesRecursive(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path base(dir);
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        out.push_back(fs::relative(it->path(), base, e2).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string findNoCase(const std::string& dir, const std::string& name) {
    const std::string want = lower(name);
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (lower(it->path().filename().string()) == want) return it->path().filename().string();
    return std::string();
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    return withSlash(dir) + name;
}

std::string absolutePath(const std::string& p) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    if (ec) return p;
    return a.lexically_normal().string();
}

bool Settings::load(const std::string& path) {
    path_ = path;
    items_.clear();
    Bytes b;
    if (!readFile(path, b)) return false;
    std::string line;
    auto take = [&]() {
        const std::string t = trimmed(line);
        line.clear();
        if (t.empty() || t[0] == '#') return;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) return;
        set(trimmed(t.substr(0, eq)), trimmed(t.substr(eq + 1)));
    };
    for (uint8_t c : b) {
        if (c == '\n') take();
        else if (c != '\r') line += char(c);
    }
    take();
    dirty_ = false;
    return true;
}

bool Settings::save() const {
    std::string s = "# CrazyCity Insanity (native) settings\n";
    for (const auto& kv : items_) s += kv.first + " = " + kv.second + "\n";
    const bool ok = writeFile(path_, Bytes(s.begin(), s.end()));
    if (ok) dirty_ = false;
    return ok;
}

std::string Settings::get(const std::string& key, const std::string& def) const {
    for (const auto& kv : items_)
        if (kv.first == key) return kv.second;
    return def;
}

int Settings::getInt(const std::string& key, int def) const {
    const std::string v = get(key);
    if (v.empty()) return def;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    return end && *end == 0 ? int(n) : def;
}

bool Settings::getBool(const std::string& key, bool def) const {
    const std::string v = lower(get(key));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return def;
}

void Settings::set(const std::string& key, const std::string& value) {
    for (auto& kv : items_)
        if (kv.first == key) {
            if (kv.second != value) {
                kv.second = value;
                dirty_ = true;
            }
            return;
        }
    items_.emplace_back(key, value);
    dirty_ = true;
}

}  // namespace native
}  // namespace cc
