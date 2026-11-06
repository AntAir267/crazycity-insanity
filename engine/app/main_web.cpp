// Browser entry point: start the app and run one frame per animation frame.
#include <emscripten.h>

#include "app/app.h"
#include "app/web_host.h"
#include "core/log.h"
#include "platform/web_net_link.h"

namespace {
void frame() { cc::theApp().frame(); }
}  // namespace

EM_JS(void, cc_js_ready, (int ok), {
    if (window.CCI && window.CCI.onEngineReady) window.CCI.onEngineReady(ok !== 0);
});

// Testing in a page that gets no animation frames (a hidden browser pane): the main loop runs on
// timers at `fps` instead (?timer in the page's address).
extern "C" EMSCRIPTEN_KEEPALIVE void cc_debug_timer_loop(int fps) {
    emscripten_cancel_main_loop();
    emscripten_set_main_loop(frame, fps > 0 ? fps : 60, false);
}

int main() {
    static cc::WebHost host;
    cc::theApp().setHost(&host);
    cc::theApp().setNetLink(&cc::net::WebNetLink::get());
    bool ok = cc::theApp().init();
    if (!ok) cc::logError("engine failed to start (WebGL 2 needed)");
    cc_js_ready(ok ? 1 : 0);
    emscripten_set_main_loop(frame, 0, false);
    return 0;
}
