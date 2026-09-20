// Included only under X3M_MOTION_OUTPUT_FIXTURE before Device is declared.
using FixtureDip=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
struct FixtureObserver {
    IDirect3DDevice9* device=nullptr;FixtureDip native=nullptr;
    bool active=false,queries=false,sampling=false;void* declaration_entry=nullptr;
    void** borrowed_original=nullptr;std::vector<void*> saved_dispatch;
    IDirect3DSurface9* held[3]{};
    LatticeGuardResult result{};
} fixture_observer;
bool fixture_observer_active(IDirect3DDevice9* d){return fixture_observer.active&&fixture_observer.device==d;}
bool fixture_observer_old(IDirect3DDevice9* d){return fixture_observer_active(d)&&fixture_observer.result.mode==1;}
void fixture_guard_cpu(LatticeGuardCpu& s){
    s.error=GetLastError();asm volatile("fnsave %0\n frstor %0\n stmxcsr %1":"=m"(s.x87),"=m"(s.mxcsr)::"memory");
}
