#include "Config.h"
#include "Log.h"
#include "Util.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// https://github.com/<owner>/<repo>/archive/refs/heads/<branch>.zip -> <repo>
// https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.zip     -> <repo>-<tag, leading v dropped before a digit>
bool folderFromUrl(const std::string& url, std::string& folder) {
    const std::string prefix = "https://github.com/";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    if (!Util::endsWithNoCase(url, ".zip")) return false;
    std::string path = url.substr(prefix.size(), url.size() - prefix.size() - 4);

    size_t p1 = path.find('/');
    if (p1 == std::string::npos) return false;
    size_t p2 = path.find('/', p1 + 1);
    if (p2 == std::string::npos) return false;
    std::string repo = path.substr(p1 + 1, p2 - p1 - 1);
    std::string rest = path.substr(p2 + 1);

    const std::string heads = "archive/refs/heads/";
    const std::string tags  = "archive/refs/tags/";
    std::string ref;
    bool isTag = false;
    if (rest.compare(0, heads.size(), heads) == 0) {
        ref = rest.substr(heads.size());
    } else if (rest.compare(0, tags.size(), tags) == 0) {
        ref = rest.substr(tags.size());
        isTag = true;
    } else {
        return false;
    }
    if (repo.empty() || ref.empty()) return false;
    if (!isTag) {  // branch archives install under the bare repo name
        folder = repo;
        return true;
    }
    if (ref.size() > 1 && ref[0] == 'v' && std::isdigit((unsigned char)ref[1])) ref.erase(0, 1);
    std::replace(ref.begin(), ref.end(), '/', '-');
    folder = repo + "-" + ref;
    return true;
}

// "123", "29.8M", "512K", "1.2GB" (1024-based)
bool parseSize(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || v < 0) return false;
    std::string suf = Util::lower(Util::trim(end));
    double mult;
    if (suf.empty() || suf == "b")                         mult = 1.0;
    else if (suf == "k" || suf == "kb" || suf == "kib")   mult = 1024.0;
    else if (suf == "m" || suf == "mb" || suf == "mib")   mult = 1024.0 * 1024.0;
    else if (suf == "g" || suf == "gb" || suf == "gib")   mult = 1024.0 * 1024.0 * 1024.0;
    else return false;
    out = (uint64_t)(v * mult + 0.5);
    return true;
}

// Only images named by number (1.png, 2.jpg, ...) are used, ordered by that number
std::vector<std::string> listImages(const std::string& dir) {
    std::vector<std::pair<unsigned long, std::string>> found;
    DIR* d = opendir(dir.c_str());
    if (!d) {
        Log::warn("config", "no image folder " + dir);
        return {};
    }
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.empty() || n[0] == '.') continue;
        size_t dot = n.rfind('.');
        if (dot == std::string::npos) continue;
        std::string ext = Util::lower(n.substr(dot));
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".webp") continue;
        std::string stem = n.substr(0, dot);
        if (stem.empty() ||
            !std::all_of(stem.begin(), stem.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; })) {
            Log::warn("config", "ignored image " + dir + "/" + n + " (name must be a number)");
            continue;
        }
        std::string full = dir + "/" + n;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            found.emplace_back(std::strtoul(stem.c_str(), nullptr, 10), full);
    }
    closedir(d);
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (size_t i = 0; i < found.size(); i++) {
        if (i > 0 && found[i].first == found[i - 1].first) {
            Log::warn("config", "duplicate image number, ignored " + found[i].second);
            continue;
        }
        out.push_back(found[i].second);
    }
    return out;
}

}  // namespace

bool Gallery::load(const std::string& iniPath, const std::string& imagesRoot) {
    std::ifstream f(iniPath);
    if (!f) {
        Log::error("config", "cannot read " + iniPath);
        return false;
    }

    std::vector<Theme> raw;
    Theme* cur = nullptr;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        lineNo++;
        std::string t = Util::trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            raw.push_back(Theme{});
            cur = &raw.back();
            cur->name = Util::trim(t.substr(1, t.size() - 2));
            continue;
        }
        size_t eq = t.find('=');
        if (!cur || eq == std::string::npos) {
            Log::warn("config", "gallery.ini line " + std::to_string(lineNo) + " ignored");
            continue;
        }
        std::string key = Util::lower(Util::trim(t.substr(0, eq)));
        std::string val = Util::trim(t.substr(eq + 1));
        if (key == "download")         cur->download = val;
        else if (key == "images")      cur->imagesDir = val;
        else if (key == "author")      cur->author = val;
        else if (key == "category") {
            const std::string c = Util::lower(val);
            if (c == "batocera") cur->category = Category::Batocera;
            else if (c.empty() || c == "fcamod") cur->category = Category::Fcamod;
            else Log::warn("config", "[" + cur->name + "] unknown category '" + val + "', using fcamod");
        }
        else if (key == "size") {
            if (!parseSize(val, cur->size))
                Log::warn("config", "[" + cur->name + "] bad size '" + val + "', treated as unknown");
        } else {
            Log::warn("config", "[" + cur->name + "] unknown key '" + key + "'");
        }
    }

    themes.clear();
    std::set<std::string> names, folders;
    for (auto& th : raw) {
        if (th.name.empty()) {
            Log::warn("config", "section with empty name skipped");
            continue;
        }
        if (names.count(th.name)) {
            Log::warn("config", "[" + th.name + "] duplicate name, skipped");
            continue;
        }
        if (th.download.empty()) {
            Log::warn("config", "[" + th.name + "] no download, skipped");
            continue;
        }
        if (!folderFromUrl(th.download, th.folder)) {
            Log::warn("config", "[" + th.name + "] unsupported download URL, skipped");
            continue;
        }
        if (folders.count(th.folder)) {
            Log::warn("config", "[" + th.name + "] installs to the same folder as another entry, skipped");
            continue;
        }
        if (!th.imagesDir.empty()) th.images = listImages(imagesRoot + th.imagesDir);
        names.insert(th.name);
        folders.insert(th.folder);
        Log::info("config", "theme " + th.folder + "  images=" + std::to_string(th.images.size()));
        themes.push_back(std::move(th));
    }
    Log::info("config", std::to_string(themes.size()) + " theme(s) loaded");
    return true;
}

bool UserState::load(const std::string& path) {
    view = ViewMode::Carousel;
    favorites.clear();
    std::ifstream f(path);
    if (!f) {
        Log::info("config", path + " not found, using defaults");
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        std::string t = Util::trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Util::trim(t.substr(0, eq));
        std::string val = Util::trim(t.substr(eq + 1));
        if (key == "view")                         view = (val == "list") ? ViewMode::List : ViewMode::Carousel;
        else if (key == "favorite" && !val.empty()) favorites.insert(val);
    }
    return true;
}

bool UserState::save(const std::string& path) const {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) {
        Log::error("config", tmp + ": " + std::strerror(errno));
        return false;
    }
    std::fprintf(f, "view=%s\n", view == ViewMode::List ? "list" : "carousel");
    for (const auto& fav : favorites) std::fprintf(f, "favorite=%s\n", fav.c_str());
    bool ok = std::fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        Log::error("config", path + ": save failed: " + std::strerror(errno));
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

void UserState::toggleFavorite(const std::string& folder) {
    if (!favorites.erase(folder)) favorites.insert(folder);
}
