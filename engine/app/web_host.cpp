#include "app/web_host.h"

#include <cstdlib>

#include "core/json_value.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, cc_js_host_clipboard, (const char* text), {
    var t = UTF8ToString(text);
    if (window.CCIShell) window.CCIShell.setClipboard(t);
});
EM_JS(void, cc_js_host_text_input, (int on, float x, float y, float w, float h), {
    if (window.CCIShell) window.CCIShell.textInput(on !== 0, x, y, w, h);
});
EM_JS(void, cc_js_host_cursor, (int c), {
    var cv = Module['canvas'];
    if (cv) cv.style.cursor = c === 1 ? 'pointer' : c === 2 ? 'text' : "";
});
EM_JS(char*, cc_js_storage_get, (const char* key), {
    var v = null;
    try { v = localStorage.getItem(UTF8ToString(key)); } catch (e) { v = null; }
    if (v === null) return 0;
    return stringToNewUTF8(v);
});
EM_JS(int, cc_js_storage_set, (const char* key, const char* value), {
    try { localStorage.setItem(UTF8ToString(key), UTF8ToString(value)); return 1; } catch (e) { return 0; }
});
EM_JS(void, cc_js_storage_del, (const char* key), {
    try { localStorage.removeItem(UTF8ToString(key)); } catch (e) { }
});
EM_JS(char*, cc_js_storage_keys, (const char* prefix), {
    var p = UTF8ToString(prefix), out = [];
    try {
        for (var i = 0; i < localStorage.length; i++) {
            var k = localStorage.key(i);
            if (k && k.indexOf(p) === 0) out.push(k);
        }
    } catch (e) { }
    return stringToNewUTF8(JSON.stringify(out));
});
EM_JS(int, cc_js_host_pick, (int kind), {
    return window.CCIShell && window.CCIShell.pick(kind) ? 1 : 0;
});
EM_JS(int, cc_js_host_can_pick, (int kind), {
    return window.CCIShell && window.CCIShell.canPick(kind) ? 1 : 0;
});
EM_JS(char*, cc_js_host_param, (const char* name), {
    var p = new URLSearchParams(location.search);
    var n = UTF8ToString(name);
    if (!p.has(n)) return 0;
    return stringToNewUTF8(p.get(n) || "");
});
EM_JS(int, cc_js_audio_running, (), {
    return window.CCIAudio && window.CCIAudio.running() ? 1 : 0;
});
#endif

namespace cc {

#ifdef __EMSCRIPTEN__
namespace {
std::string take(char* p) {
    if (!p) return std::string();
    std::string s(p);
    std::free(p);
    return s;
}
}  // namespace
#endif

void WebHost::setClipboard(const std::string& text) {
#ifdef __EMSCRIPTEN__
    cc_js_host_clipboard(text.c_str());
#else
    (void)text;
#endif
}

void WebHost::setTextInput(bool on, const ui::Rect& r) {
#ifdef __EMSCRIPTEN__
    cc_js_host_text_input(on ? 1 : 0, r.x, r.y, r.w, r.h);
#else
    (void)on;
    (void)r;
#endif
}

void WebHost::setCursor(ui::Cursor c) {
#ifdef __EMSCRIPTEN__
    cc_js_host_cursor(c == ui::Cursor::Hand ? 1 : c == ui::Cursor::Text ? 2 : 0);
#else
    (void)c;
#endif
}

std::string WebHost::storageGet(const std::string& key) {
#ifdef __EMSCRIPTEN__
    return take(cc_js_storage_get(key.c_str()));
#else
    (void)key;
    return std::string();
#endif
}

bool WebHost::storageSet(const std::string& key, const std::string& value) {
#ifdef __EMSCRIPTEN__
    return cc_js_storage_set(key.c_str(), value.c_str()) != 0;
#else
    (void)key;
    (void)value;
    return false;
#endif
}

void WebHost::storageDel(const std::string& key) {
#ifdef __EMSCRIPTEN__
    cc_js_storage_del(key.c_str());
#else
    (void)key;
#endif
}

std::vector<std::string> WebHost::storageKeys(const std::string& prefix) {
    std::vector<std::string> out;
#ifdef __EMSCRIPTEN__
    const JValue v = JValue::parse(take(cc_js_storage_keys(prefix.c_str())));
    for (const JValue& k : v.items()) out.push_back(k.str());
#else
    (void)prefix;
#endif
    return out;
}

bool WebHost::pick(ui::Pick what) {
#ifdef __EMSCRIPTEN__
    return cc_js_host_pick(int(what)) != 0;
#else
    (void)what;
    return false;
#endif
}

bool WebHost::canPick(ui::Pick what) const {
#ifdef __EMSCRIPTEN__
    return cc_js_host_can_pick(int(what)) != 0;
#else
    (void)what;
    return false;
#endif
}

bool WebHost::hasParam(const std::string& name) const {
#ifdef __EMSCRIPTEN__
    char* p = cc_js_host_param(name.c_str());
    if (!p) return false;
    std::free(p);
    return true;
#else
    (void)name;
    return false;
#endif
}

std::string WebHost::param(const std::string& name) const {
#ifdef __EMSCRIPTEN__
    return take(cc_js_host_param(name.c_str()));
#else
    (void)name;
    return std::string();
#endif
}

bool WebHost::audioRunning() const {
#ifdef __EMSCRIPTEN__
    return cc_js_audio_running() != 0;
#else
    return true;
#endif
}

}  // namespace cc
