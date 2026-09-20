#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
// Private fixture ABI; never exported by production builds.
struct LatticeGuardCpu {unsigned char x87[108];DWORD mxcsr,error;};
struct LatticeGuardSample {HRESULT hr[3];std::uint32_t pointer[3];};
struct LatticeGuardResult {
    unsigned size=sizeof(LatticeGuardResult),mode=0,action=0;
    unsigned query_releases=0,query_restores=0,sample_releases=0,native_calls=0,native_query_depth=0;
    unsigned pin_acquires=0,pin_releases=0,retired=0,getter_failures=0;
    unsigned args[6]{};HRESULT native_result=D3DERR_NOTAVAILABLE,nested_result=D3DERR_NOTAVAILABLE;
    std::uint64_t frame_before=0,frame_after=0,reset_before=0,reset_after=0;
    LatticeGuardSample samples[3]{}; // after route, after observer, native entry
    LatticeGuardCpu incoming{},outgoing{};
};
using LatticeGuardArm=HRESULT(*)(IDirect3DDevice9*,unsigned,unsigned);
using LatticeGuardRead=HRESULT(*)(IDirect3DDevice9*,LatticeGuardResult*);
