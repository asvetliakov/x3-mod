#pragma once
#include <windows.h>
#include <cstdio>
#include "../../src/ownership/application_admission_abi.h"

// The loading-trace fixtures link loading_probes.cpp and resource_reader.cpp
// (loading_trace.cpp reports through them) but never object_trace.cpp: the
// exact-executable gate answers false here, so no game address is ever patched
// by a fixture (the probe/reader machinery is exercised on fixture sites by
// resource_reader_fixture.cpp instead).
namespace x3m::object_trace {
bool executable_verified() {
    return false;
}
} // one definition per fixture executable (each includes this header once)

// End-of-fixture observation, separate from the native operation assertions.
// No scope is created here: enabled hooks must have produced real root counts.
inline bool loading_admission_witness() {
    char value[8]{};
    const bool requested = GetEnvironmentVariableA("X3M_ADMISSION", value, sizeof value) == 1 && value[0] == '1';
    auto* monitor = x3m::ownership::process_admission_monitor();
    const auto state = x3m::ownership::admission_snapshot(monitor);
    const bool valid = bool(monitor) == requested && state.active_roots == 0 && state.waiting_roots == 0 &&
                       state.promotions == 0 && (!requested || state.admitted_roots > 0);
    std::printf(
        "ADMISSION_WITNESS requested=%u enabled=%u active_roots=%llu waiting_roots=%llu admitted_roots=%llu promotions=%llu vetoes=%u valid=%u\n",
        requested, monitor != nullptr, state.active_roots, state.waiting_roots, state.admitted_roots, state.promotions,
        unsigned(state.vetoes), valid);
    return valid;
}
