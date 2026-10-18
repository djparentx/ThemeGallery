#pragma once
#include <atomic>
#include <string>
#include <thread>
#include "Screen.h"

class App;

// Renames the theme folder out of the way, deletes it on a worker thread, shows a spinner
class RemovingScreen : public Screen {
public:
    RemovingScreen(App& app, size_t theme) : app_(app), theme_(theme) {}
    ~RemovingScreen() override;
    void enter() override;
    void handle(Action) override {}
    void render(Uint32 now) override;

private:
    App& app_;
    size_t theme_;
    std::string trash_;
    std::string error_;  // rename failure; empty = renamed
    std::thread worker_;
    std::atomic<bool> done_{false};
    std::atomic<bool> ok_{true};
    bool finished_ = false;
    Uint32 startMs_ = 0;
};
