#include "capture.h"
#include "capture_state.h"
#include "../ownership/d3d9_ownership.h"
#include <algorithm>
#include <cstring>

namespace x3m {
namespace {
// Project-owned private-data namespace. No D3DSPD_IUNKNOWN: copying eight bytes
// adds no ownership cycle and resource destruction automatically removes the tag.
const GUID resource_guid = {0xaf21a9ad,0x728e,0x487c,{0xa2,0x37,0x06,0xb1,0x8d,0xec,0xa2,0x78}};
uint64_t next_resource_id = 1;
// Revision zero is not evidence of stability unless requested/known are true.
// Native-only capture explicitly reports unavailable tracking without reading bytes.
void capture_buffer_content(IDirect3DResource9* resource, uint64_t id, const char* kind) {
    ownership::BufferContentView view{};
    const HRESULT hr = ownership::get_buffer_content_view(resource, &view);
    log("buffer_content kind=%s identity=%llu result=%08lx status=%08lx requested=%u known=%u ambiguous=%u revision=%llu pending=%u flags=%08lx",
        kind,id,hr,view.status,view.requested,view.known,view.ambiguous,
        static_cast<unsigned long long>(view.revision),view.pending_locks,view.last_lock_flags);
}
}
uint64_t resource_id(IDirect3DResource9* resource) {
    if (!resource) return 0;
    uint64_t id = 0;
    DWORD size = sizeof id;
    const HRESULT hr = resource->GetPrivateData(resource_guid,&id,&size);
    if (SUCCEEDED(hr) && size == sizeof id && id) return id;
    // Unexpected metadata/API failures must not cause the same address to be
    // silently associated with a previous allocation. Zero means unavailable.
    if (hr != D3DERR_NOTFOUND) {
        log("resource_identity unavailable=%08lx bytes=%lu",hr,size);
        return 0;
    }
    id = next_resource_id++;
    const HRESULT set = resource->SetPrivateData(resource_guid,&id,sizeof id,0);
    if (FAILED(set)) {
        log("resource_identity unavailable=%08lx bytes=%u",set,unsigned(sizeof id));
        return 0;
    }
    log("resource identity=%llu ptr=%p type=%u",id,resource,resource->GetType());
    return id;
}
void capture_constants(IDirect3DDevice9* d, bool vertex, const D3DCAPS9& caps) {
    const char* kind = vertex ? "vs" : "ps";
    float values[256*4]{};
    const UINT count = vertex ? std::min<UINT>(caps.MaxVertexShaderConst,256)
        : (D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion)>=3 ? 224 : 32);
    const HRESULT floats = !count ? D3DERR_NOTAVAILABLE
        : vertex ? d->GetVertexShaderConstantF(0,values,count)
                 : d->GetPixelShaderConstantF(0,values,count);
    // Status makes an omitted sparse zero distinguishable from unavailable data.
    // Query independently: lack of F support must not erase I/B evidence.
    log("constants kind=%s type=f count=%u result=%08lx encoding=sparse_zero",kind,count,floats);
    if (SUCCEEDED(floats)) {
        for (UINT i=0;i<count;++i) {
            uint32_t bits[4]; std::memcpy(bits,values+i*4,sizeof bits);
            if (bits[0] || bits[1] || bits[2] || bits[3])
                log("constant kind=%s type=f reg=%u bits=%08x,%08x,%08x,%08x",kind,i,bits[0],bits[1],bits[2],bits[3]);
        }
    }
    int integers[16*4]{};
    const HRESULT ints = vertex ? d->GetVertexShaderConstantI(0,integers,16)
                                : d->GetPixelShaderConstantI(0,integers,16);
    log("constants kind=%s type=i count=16 result=%08lx encoding=full",kind,ints);
    if (SUCCEEDED(ints))
        for (UINT i=0;i<16;++i)
            log("constant kind=%s type=i reg=%u values=%d,%d,%d,%d",kind,i,integers[i*4],integers[i*4+1],integers[i*4+2],integers[i*4+3]);
    BOOL booleans[16]{};
    const HRESULT bools = vertex ? d->GetVertexShaderConstantB(0,booleans,16)
                                : d->GetPixelShaderConstantB(0,booleans,16);
    log("constants kind=%s type=b count=16 result=%08lx encoding=full",kind,bools);
    if (SUCCEEDED(bools))
        for (UINT i=0;i<16;++i)
            log("constant kind=%s type=b reg=%u values=%d",kind,i,booleans[i]);
}
void capture_geometry(IDirect3DDevice9* d, bool user_memory, const D3DCAPS9& caps) {
    log("geometry source=%s",user_memory ? "user_memory" : "buffers");
    if (user_memory) return; // Bound buffers are not the source of Draw*UP data.
    const UINT streams = std::min<UINT>(caps.MaxStreams,16);
    for (UINT i=0;i<streams;++i) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        UINT offset = 0, stride = 0, frequency = 0;
        HRESULT hr = d->GetStreamSource(i,&buffer,&offset,&stride);
        if (FAILED(hr)) { log("stream slot=%u result=%08lx",i,hr); continue; }
        if (!buffer) { log("stream slot=%u result=00000000 identity=0",i); continue; }
        const auto id = resource_id(buffer);
        HRESULT freq = d->GetStreamSourceFreq(i,&frequency);
        log("stream slot=%u result=%08lx identity=%llu offset=%u stride=%u frequency=%u frequency_result=%08lx",i,hr,id,offset,stride,frequency,freq);
        D3DVERTEXBUFFER_DESC desc{};
        hr = buffer->GetDesc(&desc);
        if (SUCCEEDED(hr))
            log("vertex_buffer identity=%llu bytes=%u usage=%lu pool=%u fvf=%lu",id,desc.Size,desc.Usage,desc.Pool,desc.FVF);
        capture_buffer_content(buffer,id,"vertex");
        buffer->Release(); // GetStreamSource adds a reference, including in pure devices.
    }
    IDirect3DIndexBuffer9* indices = nullptr;
    const HRESULT hr = d->GetIndices(&indices);
    if (FAILED(hr)) { log("indices result=%08lx",hr); return; }
    const auto id = resource_id(indices);
    log("indices result=%08lx identity=%llu",hr,id);
    if (indices) {
        D3DINDEXBUFFER_DESC desc{};
        if (SUCCEEDED(indices->GetDesc(&desc)))
            log("index_buffer identity=%llu bytes=%u usage=%lu pool=%u format=%u",id,desc.Size,desc.Usage,desc.Pool,desc.Format);
        capture_buffer_content(indices,id,"index");
        indices->Release();
    }
}
}
