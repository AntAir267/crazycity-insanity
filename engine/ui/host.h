// What the UI needs from the platform (the browser page or the native program). Every call has a
// harmless default, so a build without a hook still runs.
//
// The web build implements it in app/web_host.cpp (localStorage, the page's hidden file inputs,
// the clipboard events, the address parameters); the native build implements it with SDL3 and
// files (the native-platform builder's part).
#pragma once

#include <string>
#include <vector>

#include "ui/draw.h"

namespace cc {
namespace ui {

enum class Cursor { Arrow, Hand, Text };

// File choices the UI asks for. The answers arrive later through the app (App::mount() after
// new archives, FrontEnd::importFinish() after a players folder, App::setMusicGmBank()).
enum class Pick {
    GameFolder,      // the MM2 folder: archives, mmlang.dll, a players folder, gm.dls
    ModArchives,     // extra .ar files
    PlayersFolder,   // an MM2 folder or its players folder, for the profile import
    DevPlayers,      // (web, dev server) the server's own players folder
    HeldPlayers,     // the players folder of the game folder chosen before (held by the host)
    GmBank,          // a gm.dls or SoundFont 2
};

class Host {
public:
    virtual ~Host() = default;
    // Clipboard: copy and cut hand the text here. Pasting: the browser sends its text as an
    // InputEvent::Paste; natively Ctrl+V reads clipboardText().
    virtual void setClipboard(const std::string& text) { (void)text; }
    virtual std::string clipboardText() { return std::string(); }
    // A text field has the keyboard (on: start text input / show an on-screen keyboard near `r`,
    // device px).
    virtual void setTextInput(bool on, const Rect& r) {
        (void)on;
        (void)r;
    }
    virtual void setCursor(Cursor c) { (void)c; }
    // Per-user storage (the browser's localStorage; files natively). Values are strings.
    virtual std::string storageGet(const std::string& key) {
        (void)key;
        return std::string();
    }
    virtual bool storageSet(const std::string& key, const std::string& value) {
        (void)key;
        (void)value;
        return false;
    }
    virtual void storageDel(const std::string& key) { (void)key; }
    virtual std::vector<std::string> storageKeys(const std::string& prefix) {
        (void)prefix;
        return {};
    }
    // Opens a file or folder choice (asynchronous); false when this build has none.
    virtual bool pick(Pick what) {
        (void)what;
        return false;
    }
    virtual bool canPick(Pick what) const {
        (void)what;
        return false;
    }
    // A parameter of the page's address (?play, ?join=CODE, ?net=…); "" when absent.
    virtual bool hasParam(const std::string& name) const {
        (void)name;
        return false;
    }
    virtual std::string param(const std::string& name) const {
        (void)name;
        return std::string();
    }
    // Words for the footer ("Your files stay in this browser.").
    virtual bool isBrowser() const { return false; }
    // The sound output runs (a browser starts it at the first click or key).
    virtual bool audioRunning() const { return true; }
    // The program can close itself (native); the browser cannot.
    virtual bool canQuit() const { return false; }
    virtual void quit() {}
};

}  // namespace ui
}  // namespace cc
