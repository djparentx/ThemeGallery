#include "EsSettings.h"
#include "Log.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Next "<...>" tag at or after pos
bool nextTag(const std::string& text, size_t& pos, std::string& tag) {
    size_t s = text.find('<', pos);
    if (s == std::string::npos) return false;
    size_t e = text.find('>', s);
    if (e == std::string::npos) return false;
    tag = text.substr(s, e - s + 1);
    pos = e + 1;
    return true;
}

std::string tagName(const std::string& tag) {
    size_t i = 1;
    while (i < tag.size() && !std::isspace((unsigned char)tag[i]) && tag[i] != '>' && tag[i] != '/') i++;
    return tag.substr(1, i - 1);
}

std::string decodeEntities(const std::string& s) {
    static const std::pair<const char*, char> ents[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        bool done = false;
        if (s[i] == '&') {
            for (const auto& en : ents) {
                size_t len = std::strlen(en.first);
                if (s.compare(i, len, en.first) == 0) {
                    out += en.second;
                    i += len - 1;
                    done = true;
                    break;
                }
            }
        }
        if (!done) out += s[i];
    }
    return out;
}

// Exact attribute-name match, so "name" never matches "deviceName"
std::string getAttr(const std::string& tag, const std::string& key) {
    size_t i = 1;
    while (i < tag.size() && !std::isspace((unsigned char)tag[i])) i++;  // skip tag name
    while (i < tag.size()) {
        while (i < tag.size() && std::isspace((unsigned char)tag[i])) i++;
        size_t ns = i;
        while (i < tag.size() && tag[i] != '=' && tag[i] != '>' && tag[i] != '/' &&
               !std::isspace((unsigned char)tag[i])) i++;
        std::string name = tag.substr(ns, i - ns);
        while (i < tag.size() && std::isspace((unsigned char)tag[i])) i++;
        if (i >= tag.size() || tag[i] != '=') {
            if (name.empty()) i++;
            continue;
        }
        i++;  // '='
        while (i < tag.size() && std::isspace((unsigned char)tag[i])) i++;
        if (i >= tag.size()) break;
        char q = tag[i];
        if (q != '"' && q != '\'') { i++; continue; }
        size_t vs = ++i;
        size_t ve = tag.find(q, vs);
        if (ve == std::string::npos) break;
        if (name == key) return decodeEntities(tag.substr(vs, ve - vs));
        i = ve + 1;
    }
    return "";
}

}  // namespace

bool EsSettings::load(const std::string& path) {
    std::string text;
    if (!readFile(path, text)) {
        Log::warn("settings", "cannot read " + path);
        return false;
    }
    size_t pos = 0;
    std::string tag;
    while (nextTag(text, pos, tag)) {
        std::string n = getAttr(tag, "name");
        if (n == "InvertButtons")  invertButtons = (getAttr(tag, "value") == "true");
        else if (n == "ThemeSet")  themeSet = getAttr(tag, "value");
    }
    return true;
}

bool EsInputFile::load(const std::string& path) {
    std::string text;
    if (!readFile(path, text)) {
        Log::error("input", "cannot read " + path);
        return false;
    }
    joysticks.clear();
    EsInputConfig* cur = nullptr;
    size_t pos = 0;
    std::string tag;
    while (nextTag(text, pos, tag)) {
        if (tag.size() > 1 && tag[1] == '/') {
            if (tag.find("inputConfig") != std::string::npos) cur = nullptr;
            continue;
        }
        std::string tn = tagName(tag);
        if (tn == "inputConfig") {
            if (getAttr(tag, "type") == "joystick") {
                joysticks.push_back({getAttr(tag, "deviceName"), {}});
                cur = &joysticks.back();
            } else {
                cur = nullptr;
            }
        } else if (tn == "input" && cur) {
            std::string type = getAttr(tag, "type");
            EsInputEntry e;
            if (type == "button")    e.type = EsInputType::Button;
            else if (type == "hat")  e.type = EsInputType::Hat;
            else if (type == "axis") e.type = EsInputType::Axis;
            else continue;
            e.name  = getAttr(tag, "name");
            e.id    = std::atoi(getAttr(tag, "id").c_str());
            e.value = std::atoi(getAttr(tag, "value").c_str());
            cur->inputs.push_back(e);
        }
    }
    Log::info("input", "es_input.cfg: " + std::to_string(joysticks.size()) + " joystick config(s)");
    return !joysticks.empty();
}

const EsInputConfig* EsInputFile::find(const std::string& deviceName) const {
    for (const auto& j : joysticks)
        if (j.deviceName == deviceName) return &j;
    return nullptr;
}

const EsInputConfig* EsInputFile::fallback() const {
    return joysticks.empty() ? nullptr : &joysticks.front();
}