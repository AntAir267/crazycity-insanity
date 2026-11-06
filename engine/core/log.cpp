#include "core/log.h"

#include <cstdarg>
#include <cstdio>
#include <unordered_set>

namespace cc {

namespace {
LogSink g_sink;
bool g_echo = true;
std::unordered_set<std::string> g_seen;
int g_fileProblems = 0;

const char* levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "?";
}

void emit(LogLevel level, const std::string& kind, const std::string& msg) {
    if (g_echo) {
        std::FILE* f = level == LogLevel::Info ? stdout : stderr;
        std::fprintf(f, "[%s]%s%s %s\n", levelName(level), kind.empty() ? "" : " ", kind.c_str(),
                     msg.c_str());
    }
    if (g_sink) g_sink(level, kind, msg);
}

void vlog(LogLevel level, const char* fmt, va_list ap) {
    char buf[2048];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    emit(level, "", buf);
}
}  // namespace

void setLogSink(LogSink sink) { g_sink = std::move(sink); }
void setLogEcho(bool echoToStdout) { g_echo = echoToStdout; }

void resetLogDedup() {
    g_seen.clear();
    g_fileProblems = 0;
}

void logInfo(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(LogLevel::Info, fmt, ap);
    va_end(ap);
}

void logWarn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(LogLevel::Warn, fmt, ap);
    va_end(ap);
}

void logError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(LogLevel::Error, fmt, ap);
    va_end(ap);
}

void logFile(LogLevel level, const std::string& path, const std::string& problem) {
    std::string msg = path + ": " + problem;
    if (!g_seen.insert(msg).second) return;
    ++g_fileProblems;
    emit(level, "file", msg);
}

int fileProblemCount() { return g_fileProblems; }

}  // namespace cc
