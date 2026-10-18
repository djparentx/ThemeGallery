#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Screen.h"

class App;

enum class ButtonStyle { Row, List };

struct DialogButton {
    std::string label;
    std::function<void()> action;  // empty = just close
};

struct DialogSpec {
    std::string title;
    std::string message;
    std::vector<DialogButton> buttons;
    ButtonStyle style = ButtonStyle::Row;
    int defaultButton = 0;
    bool showThumb = false;
    size_t theme = 0;
};

class DialogScreen : public Screen {
public:
    DialogScreen(App& app, DialogSpec spec) : app_(app), spec_(std::move(spec)), sel_(spec_.defaultButton) {}
    void handle(Action a) override;
    void render(Uint32 now) override;

private:
    App& app_;
    DialogSpec spec_;
    int sel_;
};
