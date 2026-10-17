#pragma once
#include <string>

namespace Log {
void header();
void info(const char* module, const std::string& msg);
void warn(const char* module, const std::string& msg);
void error(const char* module, const std::string& msg);
}