#pragma once
#include <cstdint>
#include <string>

namespace Paths {
std::string appDir();                          // with trailing slash
std::string selectThemesDir();                 // /roms2/themes if writable, else /roms/themes (created if missing)
bool isDir(const std::string& path);
bool writable(const std::string& dir);         // create + delete a test file
uint64_t freeBytes(const std::string& path);   // 0 on error
bool removeTree(const std::string& path);      // recursive delete, never follows symlinks; true if all removed
}
