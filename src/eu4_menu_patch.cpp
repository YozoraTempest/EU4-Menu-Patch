// Native engine menu transition patch for EU4 1.37.5.0, Windows x64.
// Author: VulonLok.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "executable_compatibility.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <exception>
#include "patch-version.h"
#include "menu_transition.hpp"

#pragma comment(lib, "bcrypt.lib")

static std::uint8_t* image;
static wchar_t log_path[MAX_PATH];
static void* volatile pending_frontend_app;
static volatile LONG installation_status;
// Accessed only by the engine's main thread during menu transitions.
static std::uint8_t* pending_menu_app;
static void* pending_menu_idler;
static std::uint32_t previous_multiplayer_reason;
static bool pending_session_logged;
static eu4menu::WorldResetGate world_reset_gate;
static constexpr wchar_t isolated_exe[] =
    L"D:\\Astra-Paradox\\repos\\EU4MenuPatch\\private\\runtime\\eu4.exe";

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

template <typename Function> static Function engine(std::uintptr_t rva) {
    return reinterpret_cast<Function>(image+rva);
}

class NativeBusyScope {
    std::uint8_t previous_=0;
public:
    NativeBusyScope() { engine<void*(*)(void*)>(0x470af0)(&previous_); }
    ~NativeBusyScope() { engine<void(*)(void*)>(0x470b30)(&previous_); }
    NativeBusyScope(const NativeBusyScope&)=delete;
    NativeBusyScope& operator=(const NativeBusyScope&)=delete;
};

static std::uint32_t multiplayer_reason() {
    auto* world = *reinterpret_cast<std::uint8_t**>(image+0x233fe78);
    return *reinterpret_cast<std::uint32_t*>(world+0x23b4);
}

static std::uint8_t* steam_lobby(std::uint8_t* app) {
    auto* services = *reinterpret_cast<std::uint8_t**>(app+0x348);
    auto* lobby = services ? *reinterpret_cast<std::uint8_t**>(services+0x10) : nullptr;
    return lobby && *reinterpret_cast<void**>(lobby) == image+0x1da6d38 ? lobby : nullptr;
}

static void leave_steam_session(std::uint8_t* app) {
    auto* lobby = steam_lobby(app);
    if (!lobby) return;
    const auto lobby_id = *reinterpret_cast<std::uint64_t*>(lobby+0x1c0);
    const auto create_call = *reinterpret_cast<std::uint64_t*>(lobby+0x2b0);
    const auto join_call = *reinterpret_cast<std::uint64_t*>(lobby+0x2e0);
    auto* search_call = reinterpret_cast<std::uint64_t*>(lobby+0x310);
    if (!lobby_id && !create_call && !join_call && !*search_call && !lobby[0x299] && !lobby[0x29b]) return;
    log("Steam session cleanup begin lobby=%llx create_pending=%u join_pending=%u search_pending=%u hosting=%u",
        lobby_id,create_call != 0,join_call != 0,*search_call != 0,lobby[0x299]);
    // Cancel the outstanding lobby search using the same import and callback
    // object as CSteamLobby's destructor, without destroying the reusable lobby.
    if (*search_call) {
        auto unregister = *reinterpret_cast<void(**)(void*,std::uint64_t)>(image+0x1b67730);
        unregister(lobby+0x300,*search_call);
        *search_call = 0;
        lobby[0x29c] = 0;
    }
    // Close disables hosting, leaves the Steam lobby, and unregisters the
    // engine's lobby event context. Disconnect clears the outstanding join
    // result. Initialize registers that context again for the next session.
    engine<void(*)(void*)>(0x1658d50)(lobby);
    engine<void(*)(void*)>(0x165b0c0)(lobby);
    engine<void(*)(void*)>(0x1658cb0)(lobby);
    log("Steam session cleanup complete lobby=%llx create_pending=%u join_pending=%u search_pending=%u hosting=%u",
        *reinterpret_cast<std::uint64_t*>(lobby+0x1c0),
        *reinterpret_cast<std::uint64_t*>(lobby+0x2b0) != 0,
        *reinterpret_cast<std::uint64_t*>(lobby+0x2e0) != 0,*search_call != 0,lobby[0x299]);
}

static bool steam_session_is_idle(std::uint8_t* app) {
    auto* lobby = steam_lobby(app);
    if (!lobby) {
        if (!pending_session_logged) {
            log("multiplayer restore deferred: Steam lobby service unavailable");
            pending_session_logged = true;
        }
        return false;
    }
    const auto lobby_id = *reinterpret_cast<std::uint64_t*>(lobby+0x1c0);
    const auto create_call = *reinterpret_cast<std::uint64_t*>(lobby+0x2b0);
    const auto join_call = *reinterpret_cast<std::uint64_t*>(lobby+0x2e0);
    if (lobby_id || create_call || join_call || lobby[0x299]) {
        if (!pending_session_logged) {
            log("multiplayer restore deferred: lobby=%llx create_pending=%u join_pending=%u hosting=%u",
                lobby_id,create_call != 0,join_call != 0,lobby[0x299]);
            pending_session_logged = true;
        }
        return false;
    }
    return true;
}

static void restore_multiplayer_access(void* opaque_idler) {
    if (opaque_idler != pending_menu_idler || !pending_menu_app) return;
    auto* idler = static_cast<std::uint8_t*>(opaque_idler);
    auto* app = pending_menu_app;
    // SetNextIdler defers destruction of the old frontend until the next engine
    // iteration. Restore eligibility only once the replacement main menu owns
    // the current slot, its first native update has completed, and the checksum
    // is ready. Never change the platform's own Steam/login eligibility checks.
    if (*reinterpret_cast<void**>(app+0x40) != idler ||
        *reinterpret_cast<void**>(app+0x70) != nullptr ||
        *reinterpret_cast<std::uint32_t*>(idler+0x900) != 0 || !app[0x328]) return;
    auto* world = *reinterpret_cast<std::uint8_t**>(image+0x233fe78);
    auto* reason = reinterpret_cast<std::uint32_t*>(world+0x23b4);
    const auto before = *reason;
    if (before == 1 && previous_multiplayer_reason == 1 && !steam_session_is_idle(app)) return;
    if (before == 1 && previous_multiplayer_reason != 0) {
        // ResetGame normalizes every nonzero reason to 1. Preserve restrictions
        // such as Nudge (2) rather than mistaking them for ordinary session use.
        *reason = previous_multiplayer_reason == 1 ? 0 : previous_multiplayer_reason;
    }
    log("main menu active; multiplayer reason=%u->%u previous=%u checksum_ready=%u",
        before,*reason,previous_multiplayer_reason,app[0x328]);
    pending_menu_app = nullptr;
    pending_menu_idler = nullptr;
}

static void construct_menu(std::uint8_t* app,std::uint32_t reason) {
    void* allocation = engine<void*(*)(std::size_t)>(0x1a332d4)(0xb48);
    void* menu = engine<void*(*)(void*,void*,void*,void*)>(0x10d23c0)(
        allocation,*reinterpret_cast<void**>(app+0x350),
        *reinterpret_cast<void**>(app+0x358),app);
    log("menu constructed object=%p",menu);
    engine<void(*)(void*,void**,bool)>(0x14c2530)(app,&menu,false);
    pending_menu_app = app;
    // SetNextIdler consumes and nulls the local owning pointer.
    pending_menu_idler = *reinterpret_cast<void**>(app+0x70);
    world_reset_gate.queue(app,pending_menu_idler);
    previous_multiplayer_reason = reason;
    pending_session_logged = false;
    log("menu switch queued next=%p exit=%u restart=%u",
        *reinterpret_cast<void**>(app+0x70),app[0x83],app[0x84]);
}

static void release_minimap(std::uint8_t* idler) {
    auto** slot = reinterpret_cast<void**>(idler+0x10e0);
    void* panel = *slot;
    if (!panel) return;
    if (*reinterpret_cast<void**>(panel) != image+0x1d6c620) {
        log("minimap cleanup refused: unexpected controller vtable");
        return;
    }
    // CInGameIdler's stock exit and destructor do not release this controller.
    // Its native destructor detaches the window, destroys its GUI children and
    // event handlers, and frees the controller. Release while the outgoing
    // world is still alive; the menu transition must not recreate in-game GUI.
    *slot = nullptr;
    engine<void(*)(void*,unsigned int)>(0x1248740)(panel,1);
    log("in-game minimap controller released");
}

static void return_to_menu(void* opaque_idler) {
    auto* idler = static_cast<std::uint8_t*>(opaque_idler);
    auto* app = *reinterpret_cast<std::uint8_t**>(idler+0x330);
    void* before = *reinterpret_cast<void**>(image+0x233fe78);
    const auto reason = multiplayer_reason();
    log("menu request idler=%p app=%p world=%p multiplayer_reason=%u",idler,app,before,reason);
    idler[0x12e9] = 0;
    NativeBusyScope busy;
    engine<void(*)(void*)>(0x80fee0)(idler);
    leave_steam_session(app);
    engine<void(*)()>(0x5cc160)();
    release_minimap(idler);
    log("outgoing GUI released; world reset deferred until menu activation");
    construct_menu(app,reason);
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
        const auto reason = multiplayer_reason();
        NativeBusyScope busy;
        leave_steam_session(app);
        construct_menu(app,reason);
        return;
    }
    app=pending_menu_app;
    if(app) world_reset_gate.run(app,idler,*reinterpret_cast<void**>(app+0x40),
        *reinterpret_cast<void**>(app+0x70),[&] {
        NativeBusyScope busy;
        log("active menu reset begin idler=%p world=%p",idler,
            *reinterpret_cast<void**>(image+0x233fe78));
        engine<void(*)(void*,bool)>(0x210000)(app,false);
        // A frontend owns this reset, so history needs the native post-load
        // validation that ResetGame performs automatically for an in-game idler.
        void* history = *reinterpret_cast<void**>(image+0x233fea0);
        log("frontend history postvalidate begin history=%p",history);
        engine<void(*)(void*)>(0x7e9b70)(history);
        log("frontend history postvalidate complete");
        log("active menu reset complete world=%p",*reinterpret_cast<void**>(image+0x233fe78));
    });
    engine<void(*)(void*,bool)>(0x10d2e80)(idler,update);
    restore_multiplayer_access(idler);
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
    const auto ready=eu4menu::check_menu_image(image);
    if(!ready.compatible) { log("%s",ready.error.c_str());return false; }
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

static DWORD install_checked(void* module) {
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
    try {
        const auto hash=eu4menu::executable_hash(path);
        if(hash.sha256.empty()) log("%s",hash.error.c_str());
        else log("Executable SHA-256: %s",hash.sha256.c_str());
    } catch(const std::exception& error) {
        log("Executable SHA-256 unavailable: %s",error.what());
    }
    const auto compatibility=eu4menu::check_menu_image(image);
    if (!compatibility.compatible) {
        log("%s",compatibility.error.c_str());
        InterlockedExchange(&installation_status,-2);return 2;
    }
    log("Executable compatibility checks passed: %s; %zu code/data sites.",
        eu4menu::eu4_1375_profile().name,compatibility.checked_sites);
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
    log("menu transition patch initialized; author=VulonLok; version=%s", EU4_MENU_PATCH_VERSION);
    return 0;
}

static DWORD WINAPI install(void* module) {
    try { return install_checked(module); }
    catch(const std::exception& error) {
        log("REFUSED: executable compatibility check failed: %s",error.what());
        InterlockedExchange(&installation_status,-2);return 2;
    }
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
