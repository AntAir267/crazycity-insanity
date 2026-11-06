// The engine's C entry points (app/web_api.cpp, ui_api.cpp, net_api.cpp): exported to the page in
// the browser build; plain C functions natively, where the native program calls them directly.
#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif
