#pragma once

#include <string>
#include <vector>

namespace gbfr {
enum class LogLevel { info, warning, error };

struct LogEntry {
    LogLevel level;
    std::string message;
};

class Log {
public:
    static void write(LogLevel level, std::string message);
    static std::vector<LogEntry> snapshot();
    static void clear();
};
}
