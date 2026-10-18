#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct Theme {
    std::string name;                 // ini section name, shown as the title
    std::string download;             // GitHub archive zip URL
    std::string imagesDir;            // subfolder of Images/
    std::string author;
    std::string folder;               // install folder name derived from the URL
    uint64_t size = 0;                // bytes, 0 = unknown
    std::vector<std::string> images;  // full paths, sorted alphabetically
};

struct Gallery {
    std::vector<Theme> themes;
    bool load(const std::string& iniPath, const std::string& imagesRoot);
};

enum class ViewMode { Carousel, List };

struct UserState {
    ViewMode view = ViewMode::Carousel;
    std::set<std::string> favorites;  // theme folder names
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool isFavorite(const std::string& folder) const { return favorites.count(folder) != 0; }
    void toggleFavorite(const std::string& folder);
};
