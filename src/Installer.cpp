#include "Installer.h"
#include "Log.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace Installer {

bool extract(const std::string& zip, const std::string& outDir, const std::atomic<bool>& cancel, std::string& error) {
    // the system unzip must not load our bundled libraries
    std::vector<std::string> envStore;
    for (char** e = environ; *e; e++)
        if (std::strncmp(*e, "LD_LIBRARY_PATH=", 16) != 0) envStore.emplace_back(*e);
    std::vector<char*> envp;
    for (auto& s : envStore) envp.push_back(&s[0]);
    envp.push_back(nullptr);

    std::vector<std::string> argStore = {"unzip", "-q", "-o", zip, "-d", outDir};
    std::vector<char*> argv;
    for (auto& s : argStore) argv.push_back(&s[0]);
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);  // stderr still goes to the log
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, "unzip", &fa, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        error = std::string("cannot run unzip: ") + std::strerror(rc);
        return false;
    }

    int status = 0;
    for (;;) {
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (r < 0 && errno != EINTR) {
            error = std::string("waitpid: ") + std::strerror(errno);
            return false;
        }
        if (cancel) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            error = "cancelled";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!WIFEXITED(status)) {
        error = "unzip was terminated";
        return false;
    }
    const int code = WEXITSTATUS(status);
    if (code == 1) {
        Log::warn("install", "unzip reported warnings");  // 1 = completed with warnings
    } else if (code != 0) {
        error = "unzip failed (exit " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

bool findRoot(const std::string& dir, std::string& root, std::string& error) {
    DIR* d = opendir(dir.c_str());
    if (!d) {
        error = dir + ": " + std::strerror(errno);
        return false;
    }
    std::vector<std::string> dirs;
    int others = 0;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        const std::string full = dir + "/" + n;
        struct stat st;
        if (lstat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) dirs.push_back(full);
        else others++;
    }
    closedir(d);
    if (dirs.size() != 1 || others != 0) {
        error = "unexpected zip layout (expected one top-level folder)";
        return false;
    }
    root = dirs[0];
    return true;
}

bool swapIn(const std::string& newDir, const std::string& dest, std::string& trash, std::string& error) {
    trash.clear();
    struct stat st;
    if (lstat(dest.c_str(), &st) == 0) {
        const size_t slash = dest.rfind('/');
        trash = dest.substr(0, slash + 1) + ".tg_old_" + dest.substr(slash + 1) + "_" +
                std::to_string((long long)std::time(nullptr));
        if (std::rename(dest.c_str(), trash.c_str()) != 0) {
            error = std::string("cannot move the old copy aside: ") + std::strerror(errno);
            trash.clear();
            return false;
        }
    }
    if (std::rename(newDir.c_str(), dest.c_str()) != 0) {
        error = std::string("cannot move the new copy into place: ") + std::strerror(errno);
        if (!trash.empty() && std::rename(trash.c_str(), dest.c_str()) == 0) trash.clear();  // old copy restored
        return false;
    }
    return true;
}

}  // namespace Installer
