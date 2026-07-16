#include <gbfr/core/log.hpp>

#include <mutex>

namespace gbfr {
namespace {
std::mutex g_log_mutex;
std::vector<LogEntry> g_entries;
}

void Log::write(LogLevel level, std::string message) {
    std::scoped_lock lock(g_log_mutex);
    g_entries.push_back({level, std::move(message)});
}

std::vector<LogEntry> Log::snapshot() {
    std::scoped_lock lock(g_log_mutex);
    return g_entries;
}

void Log::clear() {
    std::scoped_lock lock(g_log_mutex);
    g_entries.clear();
}
}
