#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>

int wmain(int argc,wchar_t** argv) {
    if (argc != 3) return 2;
    HMODULE module=LoadLibraryW(argv[1]);
    if (!module) return 3;
    auto status=reinterpret_cast<LONG(*)()>(GetProcAddress(module,"EU4MenuPatchStatus"));
    if (!status) return 4;
    LONG result=0;
    for (unsigned attempt=0;attempt<1000 && !result;++attempt) {
        result=status();
        if (!result) Sleep(10);
    }
    std::printf("Installation status: %ld\n",result);
    return result == _wtoi(argv[2]) ? 0 : 5;
}
