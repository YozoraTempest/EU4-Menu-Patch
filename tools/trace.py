"""Observe native engine calls only in the isolated research executable."""

import argparse
from pathlib import Path
import json
import sys
import time
import frida
import ctypes
from ctypes import wintypes

ROOT = Path(__file__).resolve().parents[1]

SCRIPT = r"""
const module = Process.mainModule;
const base = module.base;
function rva(p) { return p.sub(base).toString(); }
function trace(name, address) {
  Interceptor.attach(base.add(address), {
    onEnter(args) {
      send({event:name,phase:'enter',thread:Process.getCurrentThreadId(),
        rcx:args[0].toString(),rdx:args[1].toString(),
        stack:Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0,16).map(rva)});
    },
    onLeave(ret) { send({event:name,phase:'leave',value:ret.toString()}); }
  });
}
trace('request-restart',0x14c2500);
trace('shutdown',0x14c2d00);
trace('spawn-restart',0x14c3a10);
trace('reset-game',0x210000);
trace('reset-idler',0x826c20);
trace('confirm-resign',0x838920);
trace('history-postvalidate',0x7e9b70);
Interceptor.attach(base.add(0x6241a0), {
  onEnter(args) {
    this.effect=args[0];
    const name=this.effect.add(0x38);
    this.name=(name.add(24).readU64().compare(16)<0 ? name : name.readPointer()).readUtf8String();
    this.before=this.effect.add(0x60).readPointer().toString();
  },
  onLeave(ret) {
    if (this.name==='cb_restore_personal_union') {
      send({event:'bind-history-cb',effect:this.effect.toString(),name:this.name,
        before:this.before,after:this.effect.add(0x60).readPointer().toString(),result:ret.toString()});
    }
  }
});
send({event:'ready',pid:Process.id,path:module.path,base:base.toString(),
  app:base.add(0x242bf50).readPointer().toString()});
rpc.exports={
  read(address,size) { return Array.from(new Uint8Array(ptr(address).readByteArray(size))); },
  state() {
    const app=base.add(0x242bf50).readPointer();
    const game=base.add(0x233fe78).readPointer();
    return {pid:Process.id,base:base.toString(),app:app.toString(),game:game.toString(),
      appHeader:hexdump(app,{length:0xb0,header:true,ansi:false})};
  }
};
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pid", type=int)
    parser.add_argument("--duration", type=int, default=3600)
    args = parser.parse_args()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE,wintypes.DWORD,wintypes.LPWSTR,ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000,False,args.pid)
    buffer = ctypes.create_unicode_buffer(32768)
    size = wintypes.DWORD(len(buffer))
    if not handle or not kernel.QueryFullProcessImageNameW(handle,0,buffer,ctypes.byref(size)):
        raise ctypes.WinError(ctypes.get_last_error())
    kernel.CloseHandle(handle)
    if Path(buffer.value).resolve() != (ROOT / "private/runtime/eu4.exe").resolve():
        raise SystemExit("Refusing to instrument an executable outside the isolated runtime")
    session = frida.attach(args.pid)
    trace_path = ROOT / f"private/trace-{args.pid}.jsonl"
    with trace_path.open("a", encoding="utf-8") as output:
        def message(value, data):
            record = {"time":time.time(),**value}
            line = json.dumps(record, ensure_ascii=False)
            output.write(line+"\n")
            output.flush()
            print(line, flush=True)
        script = session.create_script(SCRIPT)
        script.on("message", message)
        script.load()
        (ROOT / f"private/state-{args.pid}.json").write_text(
            json.dumps(script.exports_sync.state(),indent=2),encoding="utf-8")
        detached = False
        def on_detached(*args):
            nonlocal detached
            detached = True
            print("DETACHED",args,flush=True)
        session.on("detached",on_detached)
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline and not detached:
            time.sleep(1)
    if not detached:
        session.detach()


if __name__ == "__main__":
    main()
