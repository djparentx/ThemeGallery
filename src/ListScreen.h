#pragma once
#include <SDL.h>
#include "Screen.h"

class App;

class ListScreen : public Screen {
public:
    explicit ListScreen(App& app) : app_(app) {}
    void enter() override;
    void handle(Action a) override;
    void render(Uint32 now) override;

private:
    App& app_;
    size_t top_ = 0;  // first visible row

    SDL_Rect area() const;
    int rows() const;
    void keepVisible();
    void requestThumbs();
};
