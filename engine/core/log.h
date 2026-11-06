// Engine log. Every message goes to stdout and to an optional sink (the web page's log list).
// Problems with game files (missing, broken, refused) are reported through logFile() so the page
// can list them separately; each distinct message is reported once.
#pragma once

#include <functional>
#include <string>

namespace cc {

enum class LogLevel { Info = 0, Warn = 1, Error = 2 };

// kind: "file" for game-data problems, "" for everything else.
using LogSink = std::function<void(LogLevel level, const std::string& kind, const std::string& msg)>;

void setLogSink(LogSink sink);
void setLogEcho(bool echoToStdout);
void resetLogDedup();

void logInfo(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void logWarn(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void logError(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// A problem with a game file: path plus what is wrong. Reported once per distinct message.
void logFile(LogLevel level, const std::string& path, const std::string& problem);

// Number of distinct file problems reported since the last resetLogDedup().
int fileProblemCount();

}  // namespace cc
