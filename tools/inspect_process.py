"""Read selected pointers and counters from the isolated EU4 process."""

import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import json
import struct
import psutil

ROOT = Path(__file__).resolve().parents[1]


def inspect(pid):
    process = psutil.Process(pid)
    if Path(process.exe()).resolve() != (ROOT / "private/runtime/eu4.exe").resolve():
        raise ValueError("Only the isolated research executable may be inspected")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE,ctypes.c_void_p,ctypes.c_void_p,
        ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi = ctypes.WinDLL("psapi",use_last_error=True)
    psapi.EnumProcessModules.argtypes = [wintypes.HANDLE,ctypes.POINTER(wintypes.HMODULE),
        wintypes.DWORD,ctypes.POINTER(wintypes.DWORD)]
    handle = kernel.OpenProcess(0x1010,False,pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        modules = (wintypes.HMODULE * 1024)()
        needed = wintypes.DWORD()
        if not psapi.EnumProcessModules(handle,modules,ctypes.sizeof(modules),ctypes.byref(needed)):
            raise ctypes.WinError(ctypes.get_last_error())
        base = modules[0]
        def read(address,size):
            buffer = ctypes.create_string_buffer(size)
            count = ctypes.c_size_t()
            if not kernel.ReadProcessMemory(handle,address,buffer,size,ctypes.byref(count)):
                raise ctypes.WinError(ctypes.get_last_error())
            if count.value != size:
                raise ValueError("Short process-memory read")
            return buffer.raw
        def pointer(address):
            return struct.unpack("<Q",read(address,8))[0]
        app = pointer(base+0x242bf50)
        world = pointer(base+0x233fe78)
        idler = pointer(app+0x40)
        effects = pointer(base+0x233fe48)
        cb_database = pointer(base+0x242ba90)
        history = pointer(base+0x233fea0)
        idler_vtable=pointer(idler)-base
        services=pointer(app+0x348)
        lobby=pointer(services+0x10) if services else 0
        steam_lobby=lobby if lobby and pointer(lobby)-base == 0x1da6d38 else 0
        return {"pid":pid,"base":hex(base),"app":hex(app),"world":hex(world),
            "idler":hex(idler),"idler_vtable_rva":hex(pointer(idler)-base),
            "pending_idler":hex(pointer(app+0x70)),"exit_restart":read(app+0x83,2).hex(),
            "multiplayer_reason":struct.unpack("<I",read(world+0x23b4,4))[0],
            "checksum_ready":bool(read(app+0x328,1)[0]),
            "frontend_page":struct.unpack("<I",read(idler+0x900,4))[0]
                if idler_vtable == 0x1d5a8c8 else None,
            "steam_lobby":hex(steam_lobby),
            "steam_lobby_id":hex(pointer(steam_lobby+0x1c0)) if steam_lobby else None,
            "steam_create_pending":bool(pointer(steam_lobby+0x2b0)) if steam_lobby else None,
            "steam_join_pending":bool(pointer(steam_lobby+0x2e0)) if steam_lobby else None,
            "steam_search_pending":bool(pointer(steam_lobby+0x310)) if steam_lobby else None,
            "steam_hosting":bool(read(steam_lobby+0x299,1)[0]) if steam_lobby else None,
            "history":hex(history),"cb_database":hex(cb_database),
            "cb_count":(pointer(cb_database+0xd8)-pointer(cb_database+0xd0))//8 if cb_database else 0,
            "pending_effects":(pointer(effects+8)-pointer(effects))//8 if effects else 0,
            "postvalidate_policy_rva":hex(pointer(pointer(idler)+0x140)-base)
                if idler_vtable in (0x1d5a8c8,0x1cb8460) else None,
            "in_game_postvalidate":read(idler+0x1158,1).hex() if idler_vtable == 0x1cb8460 else None,
            "world_date_raw":read(world+0x1dd0,8).hex(),
            "working_set":process.memory_info().rss}
    finally:
        kernel.CloseHandle(handle)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("pid",type=int)
    args = parser.parse_args()
    print(json.dumps(inspect(args.pid),indent=2))
