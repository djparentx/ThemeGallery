#pragma once
#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>
#include "EsSettings.h"

enum class Action { Up, Down, Left, Right, Ok, Back, X, Y, Start, Select, Exit, Cancel, PageUp, PageDown };

const char* actionName(Action a);

class Input {
public:
    bool open(const EsInputFile& esInput, bool invertButtons);
    void close();
    void handleEvent(const SDL_Event& e, std::vector<Action>& out);
    void update(std::vector<Action>& out);  // key-repeat for directions
    const std::string& deviceName() const { return name_; }
    bool mapped() const { return mapped_; }

private:
    enum Btn { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_OK, B_BACK, B_X, B_Y, B_START, B_SELECT, B_L1, B_R1, B_COUNT };
    struct Binding { EsInputType type; int id; int value; };

    std::vector<Binding> binds_[B_COUNT];
    bool held_[B_COUNT] = {};
    Uint32 repeatAt_[B_COUNT] = {};

    std::vector<uint8_t> buttons_;
    std::vector<uint8_t> hats_;
    std::vector<int16_t> axes_;

    SDL_Joystick* joy_ = nullptr;
    std::string name_;
    bool mapped_ = false;

    bool active(const Binding& b) const;
    void recompute(std::vector<Action>& out);
    void press(int b, std::vector<Action>& out);
};