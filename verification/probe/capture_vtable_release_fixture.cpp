// Host double for capture.cpp's Hooks::release_original (test_capture_vtable_release.py).
// The production `struct Hooks` is extracted at test time into the include below. The
// object double models a C++ COM implementation (DXVK, native d3d9): its Release reads
// the deleting destructor through the object's *current* vtable pointer at a slot past
// the documented interface, which the proxy's copy does not hold. The destructor records
// whether it found the backend's own table there instead of reading past the copy.
#include <cstddef>
#include <cstdio>
#include <vector>
#define WINAPI
typedef unsigned long ULONG;
#include "capture_vtable_release_under_test_inc.h"

namespace {
struct Object {
    void** vtbl;
    ULONG refs;
    bool destroyed = false, revive = false;
    Object* child = nullptr; // released from inside the destructor (a nested hooked Release)
    Hooks* hooks = nullptr;
};
unsigned checks = 0, failures = 0, scenarios = 0, wrong_table = 0, hook_calls = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}
ULONG backend_query(Object*) { return 0; }
ULONG backend_add_ref(Object* o) { return ++o->refs; }
ULONG backend_release(Object* o);
void backend_destroy(Object* o);
void* backend_table[4] = {reinterpret_cast<void*>(&backend_query), reinterpret_cast<void*>(&backend_add_ref),
                          reinterpret_cast<void*>(&backend_release), reinterpret_cast<void*>(&backend_destroy)};
ULONG backend_release(Object* o) {
    const ULONG refs = --o->refs;
    if (refs) return refs;
    if (o->revive) { // a backend-internal reference taken during teardown keeps the object alive
        o->revive = false;
        return ++o->refs;
    }
    if (o->vtbl != backend_table) { // the read past a shorter copy: garbage on the real backend
        ++wrong_table;
        return 0;
    }
    reinterpret_cast<void (*)(Object*)>(o->vtbl[3])(o);
    return 0;
}
ULONG hook_release(Object* o) {
    ++hook_calls;
    return o->hooks->release_original(o);
}
void backend_destroy(Object* o) {
    o->destroyed = true;
    if (o->child) reinterpret_cast<ULONG (*)(Object*)>(o->child->vtbl[2])(o->child);
}
struct Hooked {
    Object object;
    Hooks hooks;
    explicit Hooked(ULONG refs)
        : object{backend_table, refs}
        , hooks(&object, 3) { // IUnknown only: the destructor slot 3 is not copied
        hooks.set(2, &hook_release);
        hooks.install(&object);
        object.hooks = &hooks;
    }
    ULONG release() { return reinterpret_cast<ULONG (*)(Object*)>(object.vtbl[2])(&object); }
};
} // namespace

int main() {
    {
        ++scenarios;
        Hooked a(2);
        check(a.hooks.table.size() == 3, "the copy holds only the known slots");
        const ULONG refs = a.release();
        check(refs == 1 && !a.object.destroyed, "non-final Release returns the backend count");
        check(a.object.vtbl == a.hooks.table.data(), "non-final Release keeps the proxy table installed");
        check(a.release() == 0 && a.object.destroyed, "the following Release is final");
    }
    {
        ++scenarios;
        Hooked a(1);
        check(a.release() == 0 && a.object.destroyed, "final Release destroys");
        check(a.object.vtbl == backend_table, "final Release ran with the backend table");
    }
    {
        ++scenarios;
        Hooked parent(1), child(1);
        parent.object.child = &child.object;
        const unsigned calls = hook_calls;
        check(parent.release() == 0, "parent final Release");
        check(parent.object.destroyed && child.object.destroyed, "nested final Release from the destructor");
        check(hook_calls == calls + 2, "the nested Release went through its own hook");
    }
    {
        ++scenarios;
        Hooked parent(1), child(2);
        parent.object.child = &child.object;
        check(parent.release() == 0 && parent.object.destroyed, "parent final Release");
        check(child.object.refs == 1 && !child.object.destroyed && child.object.vtbl == child.hooks.table.data(),
              "a nested non-final Release keeps the child's hooks");
    }
    {
        ++scenarios;
        Hooked a(1);
        a.object.revive = true;
        check(a.release() == 1 && !a.object.destroyed, "a revived object reports its count");
        check(a.object.vtbl == a.hooks.table.data(), "a revived object gets the proxy table back");
    }
    check(wrong_table == 0, "no destructor was read through the proxy copy");
    std::printf("capture_vtable_release scenarios=%u checks=%u failures=%u\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
