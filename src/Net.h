#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace Net {
bool checkInternet();  // HEAD https://github.com, about 3 s max; any HTTP response = online
// Blocking download to path; progress through the atomics; false on error or cancel (error filled)
bool download(const std::string& url, const std::string& path, const std::atomic<bool>& cancel,
              std::atomic<uint64_t>& received, std::atomic<uint64_t>& total, std::string& error);
// Tag of the latest GitHub release ("owner/repo"), read from the releases/latest redirect; empty on error
std::string latestTag(const std::string& repo);
}
