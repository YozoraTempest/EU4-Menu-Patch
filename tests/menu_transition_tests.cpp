#include "menu_transition.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void check(bool condition) { if(!condition) std::exit(1); }
int main() {
    eu4menu::WorldResetGate gate;
    int app=0,old=0,menu=0,other=0,resets=0;
    for(int cycle=0;cycle<1000;++cycle) {
        bool outgoing_alive=true;
        gate.queue(&app,&menu);
        auto reset=[&] {
            check(!outgoing_alive);++resets;
            // A native reset can pump messages. It must never reset twice.
            check(!gate.run(&app,&menu,&menu,nullptr,[] { std::exit(1); }));
        };
        check(!gate.run(&app,&old,&old,&menu,reset));
        check(!gate.run(&app,&menu,&old,&menu,reset));
        check(!gate.run(&other,&menu,&menu,nullptr,reset));
        check(!gate.run(&app,&menu,&menu,&other,reset));
        outgoing_alive=false;
        check(gate.run(&app,&menu,&menu,nullptr,reset));
        check(!gate.run(&app,&menu,&menu,nullptr,reset));
        check(resets==cycle+1);
    }
    gate.queue(&app,&menu);
    try { gate.run(&app,&menu,&menu,nullptr,[] { throw std::runtime_error("reset failed"); }); }
    catch(const std::runtime_error&) {}
    check(!gate.run(&app,&menu,&menu,nullptr,[] { std::exit(1); }));
    std::cout<<"Menu transition lifecycle tests passed\n";
}
