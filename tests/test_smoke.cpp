#include <gbfr/core/log.hpp>

int main() {
    gbfr::Log::clear();
    gbfr::Log::write(gbfr::LogLevel::info, "smoke");
    return gbfr::Log::snapshot().size() == 1 ? 0 : 1;
}
