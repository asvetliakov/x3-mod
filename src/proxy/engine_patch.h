#pragma once
#include <windows.h>
#include <cstdint>

// Byte-verified entry patches on engine functions with a chain of generated
// stubs (docs/reverse-engineering/loading-probes.md, "Patch mechanics").
//
// A site is claimed with the expected first bytes of the function (whole
// instructions, >= 5 bytes, no relative branch inside except one declared
// through SiteSpec::rel32_offset, which the tail copy re-bases): the bytes are compared
// (fail closed), the displaced instructions are copied into a "tail" block in
// an executable arena followed by a jump back to the instruction after them,
// a dispatcher block `jmp [entry]` is emitted, and the site's first five bytes
// become `jmp dispatcher`. `entry` initially points at the tail, so the
// patched function behaves as before. Hooks then push generated stubs in front
// (push_front): the previous target becomes the stub's continuation, so a
// probe stub can precede the resource reader's stub on the same function and
// both fall through to the tail. The arena is never freed (a thread may sit in
// a stub at shutdown); restore() puts the original bytes back and leaves the
// tails callable.
//
// Install window. Writing a jump over live code is safe only while no other
// thread can be executing the patched bytes. Every production claim happens on
// the backend-load path (loader.cpp load_backend -> capture initialize_log, the
// thread that calls Direct3DCreate9, before the device and the loading it
// drives exist); capture closes the window at the first Present and every later
// claim()/claim_call() is refused with status `late_claim` (the caller logs it).
// As defence in depth the five patch bytes (and any restore of up to eight
// bytes) are written with one `lock cmpxchg8b` when the span lies inside one
// 8-byte-aligned word, so a reader on another core sees either the old or the
// new instruction bytes, never a mix (Site::atomic_write records which path was
// taken; only a site whose first five bytes straddle a qword boundary falls
// back to a plain copy).
namespace x3m::engine_patch {
constexpr unsigned max_prologue=16;
struct SiteSpec {
    const char* name;
    uintptr_t address;              // function entry (the game's preferred VA, or a fixture function)
    unsigned char expected[max_prologue];
    unsigned length;                // bytes displaced (>= 5, whole instructions)
    unsigned ret_pop;               // bytes the function's `ret n` pops (0 for `ret`)
    // Offset of the 4-byte rel32 field of the one relative branch the displaced
    // span may contain (a `0F 8x rel32` Jcc or an `E8/E9 rel32`), 0 = none.
    // The tail copy re-bases that rel32 so the branch keeps its absolute
    // target; the expected bytes still carry the original rel32 (exact match).
    // Mid-function sites use it (the chase camera's cockpit-update site is
    // `cmp [ebx+0x54],0; jz rel32`); entry sites leave it 0.
    unsigned rel32_offset;
};
struct Site {
    SiteSpec spec{};
    void* tail=nullptr;             // displaced prologue + jmp back
    void* dispatcher=nullptr;       // jmp [entry]
    void** entry=nullptr;           // the chain head (arena data)
    unsigned char original[max_prologue]{},patched[5]{};
    DWORD protection=0;
    bool claimed=false,patched_in=false,atomic_write=false;
    const char* status="unclaimed";
};
// Small x86 byte emitter into the arena (executable memory; writable only while emitting).
class Emitter {
public:
    explicit Emitter(unsigned reserve);
    ~Emitter();
    bool ok() const { return cursor_!=nullptr; }
    void* here() const { return cursor_; }
    void byte(unsigned char b);
    void bytes(const void* p,unsigned n);
    void dword(uint32_t v);
    void rel32(const void* target); // for E8/E9 already emitted: displacement from the next byte
    void* finish();                 // makes the block executable again; returns its start
private:
    unsigned char* start_=nullptr; unsigned char* cursor_=nullptr; unsigned reserve_=0;
};
// Closes the install window (idempotent; the first reason is kept). After this
// claim() and claim_call() fail with status late_claim.
void close_install_window(const char* reason);
bool install_window_open();
const char* install_window_reason(); // nullptr while open
// Writes n code bytes at address: one lock cmpxchg8b when [address, address+n)
// lies inside an aligned 8-byte word (n <= 8), else a plain copy. The caller
// holds the page writable. Returns whether the atomic path was used.
bool write_code(uintptr_t address,const unsigned char* bytes,unsigned n);
// Claims and patches one site; false with site.status set on any mismatch.
// A failed post-write step leaves the original bytes in place (patch_rolled_back)
// or, when even that fails, keeps the site registered as patched
// (rollback_failed, patched_in=true) so restore() still tries at shutdown.
bool claim(Site& site,const SiteSpec& spec);
// Installs stub in front of the chain; returns the previous head (the stub's continuation).
void* push_front(Site& site,void* stub);
// Writes a pointer word that lives in the arena (a stub's continuation slot).
bool store_pointer(void** slot,void* value);
// Restores the original bytes (the chain stays callable for threads already inside it).
bool restore(Site& site);
// Whether the expected bytes are what the site holds now (before any patch).
bool verify_bytes(uintptr_t address,const unsigned char* expected,unsigned length);
// Reads count bytes from a code address into out (false when unreadable).
bool read_code(uintptr_t address,unsigned char* out,unsigned count);
// Arena accounting for reports.
unsigned arena_used();
unsigned arena_capacity();
// A `call rel32` site redirected to a replacement with the same calling
// convention (the pool's _fopen/_fclose call sites). Verified against the
// expected callee; fails closed.
struct CallSite {
    uintptr_t address=0,expected_target=0; void* replacement=nullptr;
    unsigned char original[5]{},patched[5]{}; DWORD protection=0; bool patched_in=false,atomic_write=false; const char* status="unclaimed";
};
bool claim_call(CallSite& site,uintptr_t address,uintptr_t expected_target,void* replacement);
bool restore_call(CallSite& site);
}
