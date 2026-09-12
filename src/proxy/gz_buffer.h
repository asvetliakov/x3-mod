#pragma once
#include <windows.h>
#include <cstdint>

// Read-ahead buffer in front of the game's zlib1.dll gz imports (X3M_GZ_BUFFER=1,
// X3M_GZ_BUFFER_KB, default 256). The savegame decoder issues ~14 M gzread calls
// of ~3 bytes through one dispatcher (docs/reverse-engineering/savegame-gz-stream.md);
// each is a DLL call plus zlib's per-call bookkeeping. The buffer answers them from
// one chunk per real read and keeps zlib 1.2.3 gzio semantics observable through the
// hooked imports (docs/verification/gz-buffer.md, semantics table):
//   gzgetc  = 1-byte gzread (byte or -1);
//   gztell  = logical position (base + pos), -1 while the stream's error is visible;
//   gzseek  SEEK_SET/SEEK_CUR inside the buffered range moves the cursor, otherwise
//           the buffer is dropped and the real seek runs (forward = read-and-discard,
//           backward = rewind and re-inflate, as zlib does); other whence values
//           realign the real stream to the logical position and pass through;
//   gzread  returns the bytes copied in this call when the real stream reports an
//           error after some bytes, -1 only when nothing was copied (sticky), 0 after
//           the end of the stream.
// Only handles opened with a read mode are buffered (the last of r/w/a in the mode
// string, as zlib parses it); write handles and every call on an unknown handle pass
// through. The slot table is fixed (no per-call allocation, no lock on the read
// path); one summary log line per buffered file at gzclose. The functions below are
// called by the loading-trace import hooks, never directly by the game.
namespace x3m::gz_buffer {
using OpenFn=void* (__cdecl*)(const char*,const char*);
using ReadFn=int (__cdecl*)(void*,void*,unsigned);
using WriteFn=int (__cdecl*)(void*,const void*,unsigned);
using SeekFn=LONG (__cdecl*)(void*,LONG,int);
using TellFn=LONG (__cdecl*)(void*);
using GetcFn=int (__cdecl*)(void*);
using CloseFn=int (__cdecl*)(void*);
using RewindFn=int (__cdecl*)(void*);
struct Originals {
    OpenFn open=nullptr; ReadFn read=nullptr; SeekFn seek=nullptr; TellFn tell=nullptr;
    GetcFn getc=nullptr; CloseFn close=nullptr;
    RewindFn rewind=nullptr; // optional (GetProcAddress): clears a sticky real error that the caller has not reached yet
};
constexpr unsigned slot_count=32;
constexpr unsigned default_capacity_kb=256, minimum_capacity_kb=1, maximum_capacity_kb=65536;
bool requested();          // X3M_GZ_BUFFER=1
unsigned requested_capacity(); // bytes from X3M_GZ_BUFFER_KB, clamped; default 256 KB
// Binds the real functions and the chunk size. read/seek/tell/close/getc/open are
// required. Refused while any handle is registered (returns false). No hook is
// patched here; the caller routes its hooks through the functions below.
bool initialize(const Originals& originals,unsigned capacity_bytes);
bool enabled();
void* open(const char* path,const char* mode);
int read(void* file,void* data,unsigned size);
int getc(void* file);
LONG tell(void* file);
LONG seek(void* file,LONG offset,int whence);
int close(void* file);
bool buffered(const void* file); // registered read handle (diagnostics/fixture)
struct Statistics {
    uint64_t opens=0,buffered_opens=0,passthrough_opens=0,closes=0;
    uint64_t calls=0,small_calls=0,served_bytes=0,real_reads=0,real_bytes=0,direct_reads=0;
    uint64_t getcs=0,tells=0,seeks=0,seeks_served=0,seeks_real=0,errors=0;
};
Statistics statistics(); // process totals (relaxed reads; per-file lines are logged at close)
}
