#include "Input.h"
#include "Log.h"
#include <algorithm>
#include <cctype>

namespace {
constexpr int    AXIS_DEADZONE = 16000;
constexpr Uint32 REPEAT_DELAY  = 400;
constexpr Uint32 REPEAT_RATE   = 120;

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
}

const char* actionName(Action a) {
    switch (a) {
    case Action::Up:     return "Up";
    case Action::Down:   return "Down";
    case Action::Left:   return "Left";
    case Action::Right:  return "Right";
    case Action::Ok:     return "Ok";
    case Action::Back:   return "Back";
    case Action::X:      return "X";
    case Action::Y:      return "Y";
    case Action::Start:  return "Start";
    case Action::Select: return "Select";
    case Action::Exit:   return "Exit";
    case Action::Cancel: return "Cancel";
    }
    return "?";
}

bool Input::open(const EsInputFile& esInput, bool invertButtons) {
    int count = SDL_NumJoysticks();
    if (count < 1) {
        Log::error("input", "no joystick found");
        return false;
    }

    // Prefer the joystick ES has a config for
    int index = 0;
    for (int i = 0; i < count; i++) {
        const char* n = SDL_JoystickNameForIndex(i);
        if (n && esInput.find(n)) { index = i; break; }
    }

    joy_ = SDL_JoystickOpen(index);
    if (!joy_) {
        Log::error("input", std::string("open failed: ") + SDL_GetError());
        return false;
    }
    const char* jn = SDL_JoystickName(joy_);
    name_ = jn ? jn : "";
    buttons_.assign(SDL_JoystickNumButtons(joy_), 0);
    hats_.assign(SDL_JoystickNumHats(joy_), 0);
    axes_.assign(SDL_JoystickNumAxes(joy_), 0);
    Log::info("input", "device '" + name_ + "' buttons=" + std::to_string(buttons_.size()) +
                       " hats=" + std::to_string(hats_.size()) + " axes=" + std::to_string(axes_.size()));

    const EsInputConfig* cfg = esInput.find(name_);
    if (!cfg) {
        cfg = esInput.fallback();
        if (!cfg) {
            Log::error("input", "es_input.cfg has no joystick config");
            return false;
        }
        Log::warn("input", "no entry for '" + name_ + "', using '" + cfg->deviceName + "'");
    }

    const std::string okName   = invertButtons ? "b" : "a";
    const std::string backName = invertButtons ? "a" : "b";

    for (const auto& e : cfg->inputs) {
        std::string n = lower(e.name);
        Binding bnd{e.type, e.id, e.value};
        Binding inv{e.type, e.id, -e.value};
        if      (n == "up"    || n == "leftanalogup")    binds_[B_UP].push_back(bnd);
        else if (n == "down"  || n == "leftanalogdown")  binds_[B_DOWN].push_back(bnd);
        else if (n == "left"  || n == "leftanalogleft")  binds_[B_LEFT].push_back(bnd);
        else if (n == "right" || n == "leftanalogright") binds_[B_RIGHT].push_back(bnd);
        else if (n == "joystick1up") {                   // RetroPie style: one axis entry per pair
            binds_[B_UP].push_back(bnd);
            if (e.type == EsInputType::Axis) binds_[B_DOWN].push_back(inv);
        }
        else if (n == "joystick1left") {
            binds_[B_LEFT].push_back(bnd);
            if (e.type == EsInputType::Axis) binds_[B_RIGHT].push_back(inv);
        }
        else if (n == okName)   binds_[B_OK].push_back(bnd);
        else if (n == backName) binds_[B_BACK].push_back(bnd);
        else if (n == "x")      binds_[B_X].push_back(bnd);
        else if (n == "y")      binds_[B_Y].push_back(bnd);
        else if (n == "start")  binds_[B_START].push_back(bnd);
        else if (n == "select") binds_[B_SELECT].push_back(bnd);
    }

    static const char* btnNames[B_COUNT] = {"up", "down", "left", "right", "ok", "back", "x", "y", "start", "select"};
    mapped_ = true;
    for (int b = 0; b < B_COUNT; b++) {
        if (binds_[b].empty()) {
            Log::warn("input", std::string("no binding for ") + btnNames[b]);
            mapped_ = false;
        }
    }
    return true;
}

void Input::close() {
    if (joy_) SDL_JoystickClose(joy_);
    joy_ = nullptr;
}

void Input::handleEvent(const SDL_Event& e, std::vector<Action>& out) {
    if (!joy_) return;
    SDL_JoystickID id = SDL_JoystickInstanceID(joy_);
    switch (e.type) {
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        if (e.jbutton.which != id || e.jbutton.button >= buttons_.size()) return;
        buttons_[e.jbutton.button] = (e.type == SDL_JOYBUTTONDOWN);
        break;
    case SDL_JOYHATMOTION:
        if (e.jhat.which != id || e.jhat.hat >= hats_.size()) return;
        hats_[e.jhat.hat] = e.jhat.value;
        break;
    case SDL_JOYAXISMOTION:
        if (e.jaxis.which != id || e.jaxis.axis >= axes_.size()) return;
        axes_[e.jaxis.axis] = e.jaxis.value;
        break;
    default:
        return;
    }
    recompute(out);
}

bool Input::active(const Binding& b) const {
    if (b.id < 0) return false;
    size_t i = (size_t)b.id;
    switch (b.type) {
    case EsInputType::Button: return i < buttons_.size() && buttons_[i];
    case EsInputType::Hat:    return i < hats_.size() && (hats_[i] & b.value) != 0;
    case EsInputType::Axis:
        if (i >= axes_.size()) return false;
        return b.value < 0 ? axes_[i] < -AXIS_DEADZONE : axes_[i] > AXIS_DEADZONE;
    }
    return false;
}

void Input::recompute(std::vector<Action>& out) {
    Uint32 now = SDL_GetTicks();
    for (int b = 0; b < B_COUNT; b++) {
        bool on = std::any_of(binds_[b].begin(), binds_[b].end(),
                              [this](const Binding& x) { return active(x); });
        if (on && !held_[b]) {
            held_[b] = true;
            repeatAt_[b] = now + REPEAT_DELAY;
            press(b, out);
        } else if (!on && held_[b]) {
            held_[b] = false;
        }
    }
}

void Input::press(int b, std::vector<Action>& out) {
    switch (b) {
    case B_UP:     out.push_back(Action::Up); break;
    case B_DOWN:   out.push_back(Action::Down); break;
    case B_LEFT:   out.push_back(Action::Left); break;
    case B_RIGHT:  out.push_back(Action::Right); break;
    case B_OK:     out.push_back(Action::Ok); break;
    case B_BACK:   out.push_back(Action::Back); break;
    case B_Y:      out.push_back(Action::Y); break;
    case B_SELECT: out.push_back(Action::Select); break;
    case B_START:  out.push_back(held_[B_SELECT] ? Action::Exit : Action::Start); break;
    case B_X:      out.push_back(held_[B_SELECT] ? Action::Cancel : Action::X); break;
    }
}

void Input::update(std::vector<Action>& out) {
    Uint32 now = SDL_GetTicks();
    for (int b : {B_UP, B_DOWN, B_LEFT, B_RIGHT}) {
        if (held_[b] && (Sint32)(now - repeatAt_[b]) >= 0) {
            repeatAt_[b] = now + REPEAT_RATE;
            press(b, out);
        }
    }
}