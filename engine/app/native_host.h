// The native program's UI hooks (ui/host.h): the profiles and settings as real files (the
// players folder under the data folder, the rest in the program's settings file), the system's
// clipboard, folder and file dialogs (answered through the native program's callbacks), and
// quitting.
#pragma once

#include <functional>
#include <string>

#include "ui/host.h"

namespace cc {

class Platform;
namespace native {
class Settings;
}

class NativeHost : public ui::Host {
public:
    Platform* platform = nullptr;
    native::Settings* settings = nullptr;  // the program's settings file (the page's localStorage keys)
    std::string playersDir;                // "cci.players/<path>" keys are files here
    // A file choice: the native program shows the dialog and loads what was chosen.
    std::function<bool(ui::Pick)> onPick;
    std::function<bool(ui::Pick)> canPickFn;
    // Address parameters stand-ins ("play": open the front end at start).
    std::function<bool(const std::string&)> hasParamFn;
    bool quitAsked = false;

    void setClipboard(const std::string& text) override;
    std::string clipboardText() override;
    std::string storageGet(const std::string& key) override;
    bool storageSet(const std::string& key, const std::string& value) override;
    void storageDel(const std::string& key) override;
    std::vector<std::string> storageKeys(const std::string& prefix) override;
    bool pick(ui::Pick what) override { return onPick ? onPick(what) : false; }
    bool canPick(ui::Pick what) const override { return canPickFn ? canPickFn(what) : false; }
    bool hasParam(const std::string& name) const override { return hasParamFn ? hasParamFn(name) : false; }
    bool canQuit() const override { return true; }
    void quit() override { quitAsked = true; }
};

}  // namespace cc
