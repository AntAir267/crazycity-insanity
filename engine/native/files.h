// The native program's files: where its settings and the player's profiles live (XDG folders),
// the player's MM2 folder read straight from disk, and a small settings file.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cc {
namespace native {

using Bytes = std::vector<uint8_t>;

// ~/.config/crazycity-insanity/ ($XDG_CONFIG_HOME) and ~/.local/share/crazycity-insanity/
// ($XDG_DATA_HOME), each with a trailing slash.
std::string defaultConfigDir();
std::string defaultDataDir();

bool readFile(const std::string& path, Bytes& out);
// Writes through a temporary file and a rename, so a crash never leaves half a file.
bool writeFile(const std::string& path, const Bytes& data);
bool removeFile(const std::string& path);
bool makeDirs(const std::string& dir);
bool isDir(const std::string& path);
bool isFile(const std::string& path);
// The entries of a folder (names only), sorted.
std::vector<std::string> listDir(const std::string& dir, bool files, bool dirs);
// Every file below `dir`, as paths relative to it with '/' separators, sorted.
std::vector<std::string> listFilesRecursive(const std::string& dir);
// The entry of `dir` whose name equals `name` ignoring case ("" if none). MM2's files come from
// Windows, where the case of names does not matter.
std::string findNoCase(const std::string& dir, const std::string& name);
std::string joinPath(const std::string& dir, const std::string& name);
std::string absolutePath(const std::string& p);
std::string homeDir();

// A settings file of `key = value` lines (# comments), kept in order of first use.
class Settings {
public:
    bool load(const std::string& path);
    bool save() const;
    std::string get(const std::string& key, const std::string& def = std::string()) const;
    int getInt(const std::string& key, int def) const;
    bool getBool(const std::string& key, bool def) const;
    void set(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int v) { set(key, std::to_string(v)); }
    void setBool(const std::string& key, bool v) { set(key, v ? "1" : "0"); }
    const std::string& path() const { return path_; }
    bool dirty() const { return dirty_; }

private:
    std::string path_;
    std::vector<std::pair<std::string, std::string>> items_;
    mutable bool dirty_ = false;
};

}  // namespace native
}  // namespace cc
