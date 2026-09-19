#pragma once
#include <cstdint>
#include "collide_query_phases_core.h"
namespace x3m::collide_query_phases {
extern bool enabled; // immutable after startup until detach
bool initialize(bool memo_ready,bool verify_mode=false);
bool shutdown();
namespace detail { void begin(core::Kind kind); void end(); void invalidate(); void foreign(); }
// Off: one predictable flag test, no function call and no clock read.
inline void begin(core::Kind kind=core::Miss){if(enabled)detail::begin(kind);}
inline void end(){if(enabled)detail::end();}
inline void invalidate(){if(enabled)detail::invalidate();}
inline void foreign(){if(enabled)detail::foreign();}
void device_reset(); // any thread; pending samples rejected by generation
void present(unsigned long long device,unsigned long long frame);
}
extern "C" {
void x3m_collide_query_memo_thunk();
void x3m_collide_query_descent_thunk();
}

#ifdef X3M_COLLIDE_QUERY_FIXTURE
#include "collide_query_phases_core.h"
namespace x3m::collide_query_phases {
void fixture_clock(std::uint64_t (*clock)());
core::Sample fixture_sample();
unsigned fixture_window_size();
void fixture_clear();
void fixture_fail_restore(bool fail);
}
#endif
