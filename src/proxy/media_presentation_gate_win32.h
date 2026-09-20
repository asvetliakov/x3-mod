#pragma once
#include "media_presentation_gate.h"
namespace x3m::media_presentation_gate {
// Native documented APIs only. The optional runtime is deliberately NOT wired
// into proxy startup or any media upload. One serialized owner per process.
class Win32Platform final : public Platform {
public:
    bool qualified() override;
    bool install_window() override;
    std::uint32_t reserve() override;
    std::uint32_t counter_address(const Admission&) override;
    bool read(std::uint32_t,void*,unsigned) override;
    bool write_reserved(std::uint32_t,const void*,unsigned) override;
    bool executable(std::uint32_t,unsigned) override;
    bool protect(std::uint32_t,unsigned,std::uint32_t,std::uint32_t&) override;
    bool compare8(std::uint32_t,const unsigned char*,const unsigned char*) override;
    bool flush(std::uint32_t,unsigned) override;
};
struct Runtime {
    Admission admission;
    Transaction transaction;
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
private:
    Runtime()=default;
    ~Runtime()=default;
    friend Runtime* process_runtime();
};
// Process-lifetime VirtualAlloc storage, independent of DLL data. The gate
// references only its own allocation, this counter, and the EXE. Never free or
// reuse emitted memory, even after restore. First call must be serialized.
// Allocation failure returns null; does not install, stage or enable anything.
Runtime* process_runtime();
}
