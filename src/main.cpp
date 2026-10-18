#include "App.h"
#include "Log.h"

int main(int, char**) {
    Log::header();
    App app;
    return app.run();
}