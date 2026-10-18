#pragma once
#include <string>
#include <vector>

struct EsSettings {
    bool invertButtons = false;
    std::string themeSet;
    std::string path;  // set by load()
    bool load(const std::string& path);
    bool saveThemeSet(const std::string& value);  // rewrites ThemeSet in the file; updates themeSet on success
};

enum class EsInputType { Button, Hat, Axis };

struct EsInputEntry {
    std::string name;   // "a", "up", "leftanalogup", "joystick1up", ...
    EsInputType type;
    int id;
    int value;
};

struct EsInputConfig {
    std::string deviceName;
    std::vector<EsInputEntry> inputs;
};

struct EsInputFile {
    std::vector<EsInputConfig> joysticks;
    bool load(const std::string& path);
    const EsInputConfig* find(const std::string& deviceName) const;  // exact match or nullptr
    const EsInputConfig* fallback() const;                           // first joystick or nullptr
};