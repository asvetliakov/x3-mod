#pragma once
// Per-device state of the sun-shadow caster retention
// (docs/architecture/shadow-caster-retention.md; the store is
// shadow_retention_core.h, the owner motion_output_shadow_retention_inc.h).
// Allocated once at attach while the option is on; absent otherwise.
#include <cstdint>
#include <memory>
#include <d3d9.h>
#include "shadow_retention_core.h"
#include "../renderer/shadow_replay_pass.h"

// object_lifetime.h (windows.h) stays out of this header: motion_output.h is
// also compiled by host contracts against a mock d3d9.h. The owner includes it.
namespace x3m::object_lifetime { struct JournalCursor; struct JournalDrain; struct JournalEntry; struct Snapshot; }
namespace x3m {
// The lifetime observer as the store consumes it: the retirement journal and
// the full revalidation. Production binds object_lifetime; the motion-output
// fixture seam binds its synthetic observer (the scope is synthetic there too).
struct ShadowRetentionLifetime {
    object_lifetime::JournalCursor (*journal_register)() = nullptr;
    void (*journal_unregister)() = nullptr;
    object_lifetime::JournalDrain (*journal_drain)(object_lifetime::JournalCursor&, object_lifetime::JournalEntry*, std::uint32_t) = nullptr;
    bool (*current)(std::uintptr_t registry, std::uintptr_t node, std::uint32_t node_handle, std::uintptr_t camera, std::uint32_t camera_handle, object_lifetime::Snapshot*) = nullptr;
};
struct ShadowRetention {
    shadow_retention::Store store;
    shadow_retention::Mode mode = shadow_retention::Mode::Off;
    ShadowRetentionLifetime lifetime{};
    std::uint64_t cursor = ~std::uint64_t(0); // object_lifetime::JournalCursor::sequence (Invalid until registered)
    bool registered = false;
    // The cascade transaction's draw list with room for the retained records
    // behind the live ones (live mode only).
    std::unique_ptr<renderer::ShadowReplayDraw[]> draws;
    unsigned draw_capacity = 0;
    // The scope's registry and camera as the last recorded draw saw them (the revalidation's arguments).
    std::uintptr_t registry = 0, camera = 0; std::uint32_t camera_handle = 0;
    std::uint64_t mutation_revision = 0; bool mutation_known = false;
    bool available = false;     // the last drain found the observer available
    unsigned last_nodes_unseen = 0; // the last published frame's level (seam)
    bool sun_none = false;      // the previous frame had no validated sun (a fresh device starts without one: nothing to void yet)
    bool orphan_probe = false;  // the runtime reports reference counts (attach self-test)
    unsigned probe_cursor = 0;
    std::uint64_t eye_frame = ~std::uint64_t(0);
    // This frame's transaction: issues per cascade from live and retained records.
    unsigned replayed_live[renderer::shadow_cascade_max]{}, replayed_retained[renderer::shadow_cascade_max]{};
    unsigned retained_issues = 0;
    // Cost: the scene-end work of the frame; with X3M_SHADOW_RETENTION_TIMING=1 also the draw path (one counter pair per recorded draw).
    bool timing = false;
    std::int64_t draw_ticks = 0; std::uint32_t draw_calls = 0;
    std::int64_t gate_ticks = 0; std::uint32_t gate_calls = 0; // note_refused_sighting (timing on): the static gate's refused-draw sightings
    double us = 0, journal_us = 0, walk_us = 0;
    std::uint64_t published_frame = ~std::uint64_t(0);
    unsigned idle_frames = 0; // consecutive frame begins without a scene end since the last one (the idle watchdog)
};
} // namespace x3m
