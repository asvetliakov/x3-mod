#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// Staged prerequisite only: no production caller or automatic installation.
// Engine binding/table exclusion, all Reset entry coverage, device thread and
// exception/nonlocal lease cleanup must be qualified BEFORE admitting copies.
namespace x3m::media_presentation_gate {
constexpr unsigned gate_size=27;
struct Site {
    std::uint32_t entry,renderer_word,forward,defer,unavailable_word;
    unsigned char entry_bytes[8];
    unsigned char defer_bytes[11];
};
Site game_site();
// All addresses are x86 virtual addresses. Output unchanged on invalid input.
// PUSHFD; CMP [depth],0; JNE defer; POPFD; original MOV; JMP forward;
// defer: POPFD; JMP existing unavailable-store/RET. No helper or FP operation.
bool encode(unsigned char* out,unsigned capacity,std::uint32_t base,
            std::uint32_t depth,const Site& site);

// Single designated engine/device thread. Foreign callers may only disable;
// they never read/write the owner-only reset state. depth is an aligned,
// lock-free x86 word read directly by emitted code (not a portable ISA ABI).
struct Platform;
struct Transaction;
class Admission {
public:
    bool qualify_owner(std::uint32_t thread); // once, before publication
    bool enable(std::uint32_t thread); // caller owns ALL external qualification
    void disable() { enabled_.store(false); }
    bool enabled() const { return enabled_.load(); }
    std::uint32_t depth() const { return depth_.load(); }
    const void* depth_address() const { return &depth_; }
    enum class Reset { engine, native };
    // Refusal requires caller to take a qualified pre-Reset deferral route.
    // This is NOT permission to invent a public Reset HRESULT or forward it.
    bool begin_reset(std::uint32_t thread,Reset kind);
    bool end_reset(std::uint32_t thread,Reset kind,bool succeeded);
    bool observe_recovery(std::uint32_t thread); // actual device recovery only
private:
    friend class CopyScope;
    friend bool install(Platform&,Transaction&,Admission&);
    friend bool restore(Platform&,Transaction&,Admission&,bool);
    bool owner(std::uint32_t thread) const { return thread&&thread==owner_.load(); }
    bool begin_copy(std::uint32_t thread);
    void finish_copy();
    alignas(4) std::atomic<std::uint32_t> depth_{0};
    std::atomic<std::uint32_t> owner_{0};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> gate_ready_{false};
    bool engine_reset_=false,native_reset_=false,failed_reset_=false;
};
// Cleanup is installed BEFORE admission, must be noexcept and release every
// lock/temporary surface/reference before returning, including partial acquire.
// Scope must die on its creating thread. No longjmp/SEH/process-unwind guarantee:
// unsupported nonlocal exits must stay admission-disabled until qualified.
class CopyScope {
public:
    using Cleanup=void(*)(void*) noexcept;
    CopyScope(Admission& state,std::uint32_t thread,Cleanup cleanup,void* context);
    ~CopyScope() { close(); }
    CopyScope(const CopyScope&)=delete;
    CopyScope& operator=(const CopyScope&)=delete;
    explicit operator bool() const { return state_!=nullptr; }
    void close(); // idempotent; cleanup (may reenter gate), THEN depth zero
private:
    Admission* state_=nullptr; Cleanup cleanup_=nullptr; void* context_=nullptr;
    bool closing_=false;
};

// Slow-path, serialized startup/shutdown interface. Implementations must have
// non-throwing operations. compare8 is one aligned atomic compare-exchange;
// failure cannot modify bytes. No unbounded retry over a foreign patch.
// reserve returns process-lifetime RW storage; never reuse it even on failure.
struct Platform {
    virtual ~Platform()=default;
    virtual bool qualified()=0;
    virtual bool install_window()=0;
    virtual std::uint32_t reserve()=0;
    virtual std::uint32_t counter_address(const Admission&)=0;
    virtual bool read(std::uint32_t address,void* out,unsigned size)=0;
    virtual bool write_reserved(std::uint32_t address,const void* bytes,unsigned size)=0;
    virtual bool executable(std::uint32_t address,unsigned size)=0;
    virtual bool protect(std::uint32_t address,unsigned size,std::uint32_t protection,std::uint32_t& previous)=0;
    virtual bool compare8(std::uint32_t address,const unsigned char* expected,const unsigned char* desired)=0;
    virtual bool flush(std::uint32_t address,unsigned size)=0;
};
constexpr std::uint32_t read_execute=0x20,read_write_execute=0x40;
struct Transaction {
    Transaction()=default;
    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;
    Site site{};
    const Admission* admission=nullptr; // exact counter identity bound at stage
    std::uint32_t code=0,protection=0;
    unsigned char original[8]{},patched[8]{};
    bool ready=false,installed=false;
    bool emission_flush_debt=false,emission_protection_debt=false;
    bool may_redirect=false,flush_debt=false,protection_debt=false;
    bool ever_published=false; // code AND counter must survive process lifetime
    const char* status="empty";
};
// stage emits/seals only; install separately requires startup quiescence.
// State/counter storage must be process-lifetime before calling either API.
bool stage(Platform&,Transaction&,const Site&,const Admission&);
bool install(Platform&,Transaction&,Admission&);
// Caller proves no thread executing overwritten span; startup rollback shares
// that window. For later removal caller must supply separately proved quiescence.
// disable admission and finish all CopyScopes BEFORE removal. Code stays live.
bool restore(Platform&,Transaction&,Admission&,bool quiescent);
}
