#pragma once
#include "Screen.h"

class App;

class MenuScreen : public Screen {
public:
    explicit MenuScreen(App& app) : app_(app) {}
    void handle(Action a) override;
    void render(Uint32 now) override;

private:
    App& app_;
    int sel_ = 0;  // 0 View Mode, 1 Filter, 2 Exit

    void change(int row);
};
