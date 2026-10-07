#pragma once

namespace eu4menu {
// SetNextIdler transfers ownership on a later application iteration.
// Reset only after that transfer has destroyed the outgoing idler.
class WorldResetGate {
    void* app_=nullptr;
    void* menu_=nullptr;
public:
    void queue(void* app,void* menu) noexcept { app_=app;menu_=menu; }
    template<class Reset>
    bool run(void* app,void* updating,void* current,void* next,Reset&& reset) {
        if(!app_||app!=app_||updating!=menu_||current!=menu_||next) return false;
        app_=nullptr;menu_=nullptr;
        reset();
        return true;
    }
};
}
