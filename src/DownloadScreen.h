#pragma once
#include <SDL.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include "Screen.h"

class App;
struct InstallLoc;

// Install / reinstall: optional connection check, download, extract, swap into place
class DownloadScreen : public Screen {
public:
    DownloadScreen(App& app, size_t theme, const InstallLoc& target, bool needCheck);
    ~DownloadScreen() override;
    void enter() override;
    void handle(Action a) override;
    void render(Uint32 now) override;

private:
    enum Phase { CHECKING, DOWNLOADING, INSTALLING };

    App& app_;
    const size_t theme_;
    // copied up front so the worker never touches App data
    const std::string target_, url_, folder_, work_, zip_, extractDir_, dest_, patch_;
    const uint64_t cfgSize_;
    const bool needCheck_;

    std::thread worker_;
    std::atomic<int> phase_;
    std::atomic<bool> cancel_{false}, canCancel_{true}, done_{false};
    std::atomic<bool> ok_{false}, offline_{false}, cancelled_{false}, unpatched_{false};
    std::atomic<uint64_t> received_{0}, total_{0};
    std::string error_;  // valid once done_

    // main thread only
    bool finished_ = false;
    Uint32 sampleMs_ = 0, etaMs_ = 0;
    uint64_t sampleBytes_ = 0;
    double speed_ = 0.0;
    std::string etaText_ = "--:--";

    void run();
    void finish();
};
