// The browser's UI hooks (ui/host.h): localStorage, the page's hidden file inputs
// (window.CCIShell, web/app.js), the clipboard, the address parameters, the mouse cursor.
#pragma once

#include "ui/host.h"

namespace cc {

class WebHost : public ui::Host {
public:
    void setClipboard(const std::string& text) override;
    void setTextInput(bool on, const ui::Rect& r) override;
    void setCursor(ui::Cursor c) override;
    std::string storageGet(const std::string& key) override;
    bool storageSet(const std::string& key, const std::string& value) override;
    void storageDel(const std::string& key) override;
    std::vector<std::string> storageKeys(const std::string& prefix) override;
    bool pick(ui::Pick what) override;
    bool canPick(ui::Pick what) const override;
    bool hasParam(const std::string& name) const override;
    std::string param(const std::string& name) const override;
    bool isBrowser() const override { return true; }
    bool audioRunning() const override;
};

}  // namespace cc
