// Browser glue: archive bytes kept on the JavaScript side, and messages to the page.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "vfs/source.h"

namespace cc {

// A byte source backed by a JavaScript ArrayBuffer registered by the page
// (window.CCI.archives[id]). Only the ranges read are copied into wasm memory.
std::shared_ptr<ByteSource> makeJsArchiveSource(int jsId, double size);

// Page callbacks. Natively they go to the sinks the native program sets (none: dropped).
void pageLog(int level, const std::string& kind, const std::string& msg);
void pageReadout(const std::string& text);
// An engine event: handed to the sink (the App's queue for its front end) and, in the browser, to
// the page when it subscribed to that name (cc_subscribe_event; scripted checks).
void pageEvent(const std::string& name, const std::string& json);
void setEventSink(std::function<void(const std::string& name, const std::string& json)> sink);

struct NativePageSinks {
    std::function<void(int level, const std::string& kind, const std::string& msg)> log;
    std::function<void(const std::string& text)> readout;
    std::function<void(const std::string& name, const std::string& json)> event;
};
void setNativePageSinks(NativePageSinks sinks);

}  // namespace cc
