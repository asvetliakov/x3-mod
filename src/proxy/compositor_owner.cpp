#include "compositor_owner.h"
#include <windows.h>

static_assert(sizeof(void*) == 4, "The compositor owner layout is x86 only");
namespace x3m::compositor_owner {
Result read(std::uintptr_t base, Snapshot& out) noexcept {
    const DWORD error = GetLastError();
    const Result result = detail::collect(
        base, out, [](std::uint32_t address, void* destination, std::size_t size) noexcept {
            SIZE_T copied = 0;
            return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination, size,
                                     &copied) &&
                   copied == size;
        });
    SetLastError(error);
    return result;
}
} // namespace x3m::compositor_owner
