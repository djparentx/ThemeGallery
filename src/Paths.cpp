#include "Paths.h"
#include "Log.h"
#include <SDL.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace Paths {

std::string appDir() {
    char* base = SDL_GetBasePath();
    std::string dir = base ? base : "./";
    SDL_free(base);
    return dir;
}

bool isDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool writable(const std::string& dir) {
    std::string test = dir + "/.tg_write_test";
    int fd = open(test.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    bool ok = write(fd, "x", 1) == 1;
    ok = (close(fd) == 0) && ok;
    unlink(test.c_str());
    return ok;
}

std::string selectThemesDir() {
    const std::string roms2 = "/roms2/themes";
    const std::string roms  = "/roms/themes";

    if (isDir(roms2)) {
        if (writable(roms2)) {
            Log::info("paths", "themes dir " + roms2);
            return roms2;
        }
        Log::warn("paths", roms2 + " not writable, using " + roms);
    }
    if (!isDir(roms)) {
        if (mkdir(roms.c_str(), 0755) != 0)
            Log::error("paths", "cannot create " + roms + ": " + std::strerror(errno));
        else
            Log::info("paths", "created " + roms);
    }
    Log::info("paths", "themes dir " + roms);
    return roms;
}

uint64_t freeBytes(const std::string& path) {
    struct statvfs sv;
    if (statvfs(path.c_str(), &sv) != 0) {
        Log::warn("paths", "statvfs " + path + ": " + std::strerror(errno));
        return 0;
    }
    return (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
}

}  // namespace Paths
