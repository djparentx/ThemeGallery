#pragma once
#include <atomic>
#include <string>

namespace Installer {
// Runs the system unzip into outDir; cancel kills it
bool extract(const std::string& zip, const std::string& outDir, const std::atomic<bool>& cancel, std::string& error);
// The single top-level folder inside dir (GitHub archives always have exactly one)
bool findRoot(const std::string& dir, std::string& root, std::string& error);
// Applies a -p1 unified diff inside root with /usr/bin/patch; dry-run first so nothing changes unless every hunk fits
bool applyPatch(const std::string& root, const std::string& patchFile, std::string& error);
// Puts newDir at dest; an existing dest is renamed to trash (empty if none) and restored on failure
bool swapIn(const std::string& newDir, const std::string& dest, std::string& trash, std::string& error);
}
