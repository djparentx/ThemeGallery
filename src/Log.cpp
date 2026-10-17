#include "Log.h"
#include <cstdio>
#include <ctime>

namespace {
void write(const char* level, const char* module, const std::string& msg) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[20];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    std::fprintf(stderr, "%-19s  %-5s  %-10s  %s\n", ts, level, module, msg.c_str());
    std::fflush(stderr);
}
}

namespace Log {
void header() {
    std::fprintf(stderr, "%-19s  %-5s  %-10s  %s\n", "TIME", "LEVEL", "MODULE", "MESSAGE");
    std::fflush(stderr);
}
void info(const char* module, const std::string& msg)  { write("INFO",  module, msg); }
void warn(const char* module, const std::string& msg)  { write("WARN",  module, msg); }
void error(const char* module, const std::string& msg) { write("ERROR", module, msg); }
}