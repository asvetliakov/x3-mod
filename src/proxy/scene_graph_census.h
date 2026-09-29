#pragma once
#include <cstdint>

// Scene-graph census row (--perf / --debug; docs/architecture/logging-tiers.md,
// docs/reverse-engineering/object-lifetimes.md "Run382"): read-only, no engine
// patch. Once per 300 frames at Present on the main-loop thread (under the
// Present hook's CpuCallBoundary), through engine_memory::read only:
//   scene_graph_census device= frame= engine_nodes= scenes= cuts= cut_buckets= tasks=
//     unattached= truncated= cycle= bounded= capped= sampled= sampled_nodes= distinct= other=
//     b0=<body>:<n> .. b7= registry_live= inserts= insert_pairs= insert_dropped= i0=<site>/<caller>:<n> .. i7=
//     walk_us= reads= queries=
// engine_nodes/scenes/cuts/tasks are the engine's hash-table entry counts
// ([[R+0xc]+0xc], [[R+0x10]+0xc], [[R+0x84]+0xc], [[VM]+0xc]; `-` when refused);
// unattached is the length of the list at R+0x28 (bounded, cycle-guarded, time
// budgeted); b0.. are the top model ids of every `sampled`-th unattached node by
// body name (counts are sample counts); registry_live is the object-lifetime
// observer's live key count; i0.. are the registry inserts since the previous row
// by (return site, allocator's return address) from object_lifetime's Insert hook.
// Fixed static tables, no allocation; walk_us is this row's own cost.
namespace x3m::scene_graph_census {
// Arms the insert-caller capture when a tier is on (initialize_log, after log_tier::init()).
void initialize(bool enabled);
void report(unsigned long long device, unsigned long long frame);
#ifdef X3M_SCENE_GRAPH_CENSUS_FIXTURE
// Fixture build only: the image globals holding R and VM (production reads 0x00608518 / 0x006085e4).
void fixture_globals(std::uintptr_t manager_va, std::uintptr_t vm_va);
#endif
}
