#pragma once
#include <SDL.h>
#include "Input.h"

class Screen {
public:
    virtual ~Screen() = default;
    virtual void enter() {}
    virtual void handle(Action a) = 0;
    virtual void render(Uint32 now) = 0;
};
