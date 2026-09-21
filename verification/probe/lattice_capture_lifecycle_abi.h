#pragma once
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/clone_upload_observer.h"
// Fixture-only CPU observations of actual capture/ownership paths; no fake counts.
struct LatticeCaptureLifecycleView {
    unsigned size=sizeof(LatticeCaptureLifecycleView),live=0,pin=0,closing=0,scope=0;
    unsigned gate=0,query_depth=0,retiring=0,motion_refs=0,bloom_refs=0;
    unsigned releases=0,retired=0,pin_queries=0,immediate=0,deferred=0;
    std::uint64_t capture_id=0,reset_generation=0,ownership_generation=0;
    x3m::ownership::CloneUploadArmToken arm{};
};
using LatticeCaptureCallback=void(*)(unsigned,IUnknown*);
