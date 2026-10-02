// Native engine menu transition patch for EU4 1.37.5.0, Windows x64.
// Author: VulonLok.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")

static std::uint8_t* image;
static wchar_t log_path[MAX_PATH];
static void* volatile pending_frontend_app;
static volatile LONG installation_status;
static constexpr wchar_t isolated_exe[] =
    L"D:\\Astra-Paradox\\repos\\EU4MenuPatch\\private\\runtime\\eu4.exe";
static constexpr unsigned char expected_hash[32] = {
    0x9a,0xd3,0xef,0xe1,0xaf,0x16,0x9f,0x40,0xee,0x57,0x7f,0x9d,0xae,0x5d,0xeb,0xbc,
    0x87,0xaf,0x6f,0xb8,0xb5,0x45,0x0f,0xb3,0x45,0xeb,0xf1,0x10,0xdc,0x4d,0x77,0x1a
};

static void log(const char* format, ...) {
    char buffer[1024];
    int prefix = sprintf_s(buffer, "[%lu:%llu] ", GetCurrentProcessId(), GetTickCount64());
    va_list args;
    va_start(args,format);
    int length = vsnprintf_s(buffer+prefix,sizeof(buffer)-prefix,_TRUNCATE,format,args);
    va_end(args);
    if (length < 0) length = static_cast<int>(strlen(buffer+prefix));
    buffer[prefix+length++] = '\n';
    HANDLE file = CreateFileW(log_path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file,buffer,prefix+length,&written,nullptr);
        CloseHandle(file);
    }
}

static bool verify_hash() {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0) < 0)
        return false;
    if (BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm,0);
        return false;
    }
    wchar_t executable[MAX_PATH];
    if (!GetModuleFileNameW(nullptr,executable,MAX_PATH)) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm,0);
        return false;
    }
    HANDLE file = CreateFileW(executable,GENERIC_READ,FILE_SHARE_READ,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    bool success = file != INVALID_HANDLE_VALUE;
    unsigned char block[65536];
    DWORD read;
    while (success) {
        if (!ReadFile(file,block,sizeof(block),&read,nullptr)) { success=false;break; }
        if (!read) break;
        success = BCryptHashData(hash,block,read,0) >= 0;
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    unsigned char digest[32];
    success = success && BCryptFinishHash(hash,digest,sizeof(digest),0) >= 0
        && memcmp(digest,expected_hash,sizeof(digest)) == 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    return success;
}

template <typename Function> static Function engine(std::uintptr_t rva) {
    return reinterpret_cast<Function>(image+rva);
}

static void construct_menu(std::uint8_t* app) {
    void* allocation = engine<void*(*)(std::size_t)>(0x1a332d4)(0xb48);
    void* menu = engine<void*(*)(void*,void*,void*,void*)>(0x10d23c0)(
        allocation,*reinterpret_cast<void**>(app+0x350),
        *reinterpret_cast<void**>(app+0x358),app);
    log("menu constructed object=%p",menu);
    engine<void(*)(void*,void**,bool)>(0x14c2530)(app,&menu,false);
    log("menu switch queued next=%p exit=%u restart=%u",
        *reinterpret_cast<void**>(app+0x70),app[0x83],app[0x84]);
}

static void return_to_menu(void* opaque_idler) {
    auto* idler = static_cast<std::uint8_t*>(opaque_idler);
    auto* app = *reinterpret_cast<std::uint8_t**>(idler+0x330);
    void* before = *reinterpret_cast<void**>(image+0x233fe78);
    log("menu request idler=%p app=%p world=%p",idler,app,before);
    idler[0x12e9] = 0;
    // The stock in-game load path uses this wrapper to release GUI state before
    // CEU4Application::ResetGame and rebuild the GUI references afterwards.
    engine<void(*)(void*)>(0x80fee0)(idler);
    engine<void(*)()>(0x5cc160)();
    engine<void(*)(void*,bool)>(0x826c20)(idler,false);
    log("reset complete world=%p",*reinterpret_cast<void**>(image+0x233fe78));
    construct_menu(app);
}

static void queue_return_to_menu(void* opaque_app) {
    auto* app = static_cast<std::uint8_t*>(opaque_app);
    auto* idler = *reinterpret_cast<std::uint8_t**>(app+0x40);
    log("pregame return queued app=%p idler=%p vtable=%p",app,idler,
        *reinterpret_cast<void**>(idler));
    InterlockedExchangePointer(&pending_frontend_app,app);
}

static void frontend_tick(void* idler,bool update) {
    auto* app = static_cast<std::uint8_t*>(InterlockedExchangePointer(&pending_frontend_app,nullptr));
    if (app) {
        log("frontend reset begin world=%p",*reinterpret_cast<void**>(image+0x233fe78));
        engine<void(*)(void*,bool)>(0x210000)(app,false);
        // ResetGame only post-validates reloaded history when the current idler
        // reports that it is in-game. Frontend resets need the same native pass
        // to bind the new history effects to definition objects (including CBs).
        void* history = *reinterpret_cast<void**>(image+0x233fea0);
        log("frontend history postvalidate begin history=%p",history);
        engine<void(*)(void*)>(0x7e9b70)(history);
        log("frontend history postvalidate complete");
        log("frontend reset complete world=%p",*reinterpret_cast<void**>(image+0x233fe78));
        construct_menu(app);
        return;
    }
    engine<void(*)(void*,bool)>(0x10d2e80)(idler,update);
}

static LONG CALLBACK observe_exception(EXCEPTION_POINTERS* exception) {
    auto* record = exception->ExceptionRecord;
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        log("exception code=%08lx RIP=%llx RVA=%llx RSP=%llx fault=%llx",
            record->ExceptionCode,exception->ContextRecord->Rip,
            exception->ContextRecord->Rip-reinterpret_cast<std::uintptr_t>(image),
            exception->ContextRecord->Rsp,record->ExceptionInformation[1]);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void* allocate_near(std::uintptr_t target) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const std::uintptr_t step = info.dwAllocationGranularity;
    const std::uintptr_t origin = target & ~(step-1);
    for (std::uintptr_t delta = step; delta < 0x70000000; delta += step) {
        const int directions[] = {-1,1};
        for (int direction : directions) {
            std::uintptr_t candidate = direction < 0 ? origin-delta : origin+delta;
            void* result = VirtualAlloc(reinterpret_cast<void*>(candidate),4096,
                MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if (result) return result;
        }
    }
    return nullptr;
}

struct PreparedHook {
    std::uint8_t* site = nullptr;
    std::uint8_t* stub = nullptr;
    std::size_t length = 0;
    unsigned char patch[16]{};
    DWORD protection = 0;
};

static bool prepare_hook(PreparedHook& prepared,std::uintptr_t rva,
    const unsigned char* original,std::size_t length,
    std::uintptr_t callback,std::uintptr_t destination,bool from_r13) {
    auto* site = image+rva;
    if (memcmp(site,original,length) != 0) {
        log("REFUSED: hook bytes mismatch RVA=%zx",rva); return false;
    }
    auto* stub = static_cast<std::uint8_t*>(allocate_near(reinterpret_cast<std::uintptr_t>(site)));
    if (!stub) { log("REFUSED: no nearby executable allocation"); return false; }
    // This block runs at a normal call boundary in CInGameIdler::Idle. Its
    // existing stack frame already provides Windows x64 shadow space.
    unsigned char code[] = {
        0x4c,0x89,0xe9,                   // mov rcx,r13 (current idler)
        0x48,0xb8,0,0,0,0,0,0,0,0,     // mov rax,return_to_menu
        0xff,0xd0,                      // call rax
        0x48,0xb8,0,0,0,0,0,0,0,0,     // mov rax,stock epilogue
        0xff,0xe0                       // jmp rax
    };
    if (!from_r13) { code[0]=0x48;code[1]=0x89;code[2]=0xd9; }
    auto continuation = reinterpret_cast<std::uintptr_t>(image+destination);
    memcpy(code+5,&callback,8);
    memcpy(code+17,&continuation,8);
    memcpy(stub,code,sizeof(code));
    DWORD old;
    if (!VirtualProtect(stub,4096,PAGE_EXECUTE_READ,&old)) {
        VirtualFree(stub,0,MEM_RELEASE);return false;
    }
    FlushInstructionCache(GetCurrentProcess(),stub,sizeof(code));
    memset(prepared.patch,0x90,sizeof(prepared.patch));
    prepared.patch[0]=0xe9;
    std::int64_t displacement = stub-(site+5);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        VirtualFree(stub,0,MEM_RELEASE);return false;
    }
    std::int32_t relative = static_cast<std::int32_t>(displacement);
    memcpy(prepared.patch+1,&relative,4);
    prepared.site=site;
    prepared.stub=stub;
    prepared.length=length;
    return true;
}

static bool install_hooks(void** frontend_slot,PreparedHook (&hooks)[2]) {
    DWORD frontend_protection;
    if (!VirtualProtect(frontend_slot,sizeof(void*),PAGE_READWRITE,&frontend_protection))
        return false;
    std::size_t writable=0;
    for (auto& prepared : hooks) {
        if (!VirtualProtect(prepared.site,prepared.length,PAGE_EXECUTE_READWRITE,
            &prepared.protection)) break;
        ++writable;
    }
    // Acquire all write permissions before changing any instruction or pointer.
    // A failed installation leaves the original engine behavior intact.
    if (writable == 2) {
        for (auto& prepared : hooks) {
            memcpy(prepared.site,prepared.patch,prepared.length);
            FlushInstructionCache(GetCurrentProcess(),prepared.site,prepared.length);
            log("installed menu hook RVA=%zx stub=%p",prepared.site-image,prepared.stub);
        }
        InterlockedExchangePointer(frontend_slot,reinterpret_cast<void*>(&frontend_tick));
    }
    DWORD unused;
    for (std::size_t index=0;index<writable;++index) {
        auto& prepared=hooks[index];
        if (!VirtualProtect(prepared.site,prepared.length,prepared.protection,&unused))
            log("page protection restore failed RVA=%zx error=%lu",prepared.site-image,GetLastError());
    }
    if (!VirtualProtect(frontend_slot,sizeof(void*),frontend_protection,&unused))
        log("frontend protection restore failed error=%lu",GetLastError());
    return writable == 2;
}

static DWORD WINAPI install(void* module) {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr,path,MAX_PATH)) {
        InterlockedExchange(&installation_status,-1);return 1;
    }
#ifdef EU4_MENU_PATCH_RESEARCH
    if (_wcsicmp(path,isolated_exe) != 0) {
        InterlockedExchange(&installation_status,-1);return 1;
    }
#else
    const wchar_t* name = wcsrchr(path,L'\\');
    if (!name || _wcsicmp(name+1,L"eu4.exe") != 0) {
        InterlockedExchange(&installation_status,-1);return 1;
    }
#endif
    if (!GetModuleFileNameW(static_cast<HMODULE>(module),log_path,MAX_PATH)) return 1;
    wchar_t* slash = wcsrchr(log_path,L'\\');
    if (!slash) return 1;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-log_path),L"eu4_menu_patch.log");
    image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!verify_hash()) {
        log("REFUSED: executable SHA-256 mismatch");
        InterlockedExchange(&installation_status,-2);return 2;
    }
    auto** frontend_slot = reinterpret_cast<void**>(image+0x1d5a8c8+0x20);
    constexpr unsigned char in_game[] = {0x48,0x8d,0x8d,0x40,0x02,0x00,0x00};
    constexpr unsigned char pregame[] = {0x66,0xc7,0x83,0x83,0x00,0x00,0x00,0x01,0x01};
    if (*frontend_slot != image+0x10d2e80 ||
        memcmp(image+0x815cae,in_game,sizeof(in_game)) != 0 ||
        memcmp(image+0x111dfd7,pregame,sizeof(pregame)) != 0) {
        log("REFUSED: target memory has already been modified");
        InterlockedExchange(&installation_status,-3);return 3;
    }
    PreparedHook hooks[2];
    if (!prepare_hook(hooks[0],0x815cae,in_game,sizeof(in_game),
            reinterpret_cast<std::uintptr_t>(&return_to_menu),0x8165f2,true) ||
        !prepare_hook(hooks[1],0x111dfd7,pregame,sizeof(pregame),
            reinterpret_cast<std::uintptr_t>(&queue_return_to_menu),0x111e000,false) ||
        !install_hooks(frontend_slot,hooks)) {
        for (auto& prepared : hooks) {
            if (prepared.stub) VirtualFree(prepared.stub,0,MEM_RELEASE);
        }
        log("REFUSED: hook preparation or write permissions failed");
        InterlockedExchange(&installation_status,-4);return 4;
    }
    AddVectoredExceptionHandler(0,observe_exception);
    InterlockedExchange(&installation_status,1);
    log("menu transition patch initialized; author=VulonLok");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE thread = CreateThread(nullptr,0,install,module,0,nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) LONG EU4MenuPatchStatus() {
    return InterlockedCompareExchange(&installation_status,0,0);
}
