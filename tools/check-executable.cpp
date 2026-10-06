#include "executable_compatibility.hpp"
#include <windows.h>
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    if(argc!=2) {std::cerr<<"Usage: executable_check.exe <eu4.exe>\n";return 2;}
    const auto hash=eu4menu::executable_hash(argv[1]);
    if(hash.sha256.empty()) std::cout<<hash.error<<'\n';
    else std::cout<<"Executable SHA-256: "<<hash.sha256<<'\n';
    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module) {std::cerr<<"Cannot map executable: Windows error "<<GetLastError()<<'\n';return 2;}
    int status=1;
    try {
        const auto result=eu4menu::check_menu_image(module);
        if(result.compatible) {
            std::cout<<"Executable compatibility checks passed: "<<eu4menu::eu4_1375_profile().name
                <<"; "<<result.checked_sites<<" code/data sites. Entry point was not run.\n";status=0;
        } else std::cerr<<result.error<<'\n';
    } catch(const std::exception& error) {std::cerr<<"Compatibility check failed: "<<error.what()<<'\n';}
    FreeLibrary(module);return status;
}
