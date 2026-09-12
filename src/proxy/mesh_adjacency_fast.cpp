#include "mesh_adjacency_fast.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace x3m::mesh_adjacency_fast {
namespace {
// One malloc per call: every array is carved from a single arena whose size is
// computed up front (phase scratch is rewound and reused). The arena is kept
// per thread between calls up to retained_scratch_limit, so a burst of meshes
// on the loading thread pays the allocation and the first-touch page faults
// once; release_scratch() or the thread's exit frees it.
struct Arena {
    unsigned char* base=nullptr;size_t capacity=0;
    ~Arena(){std::free(base);}
};
thread_local Arena arena;
constexpr size_t retained_scratch_limit=size_t(16)<<20;
constexpr size_t align=8;
inline size_t padded(uint64_t bytes) noexcept { return size_t((bytes+(align-1))&~uint64_t(align-1)); }
struct Bump { // bump allocator over the arena; mark()/rewind() reuse phase scratch
    unsigned char* base;size_t capacity,used=0;
    template<class T> T* array(size_t count) noexcept { T* p=reinterpret_cast<T*>(base+used);used+=padded(uint64_t(count)*sizeof(T));return p; }
    bool overflow() const noexcept { return used>capacity; } // the layout computed the size; a mismatch is a bug, reported as Allocation
    size_t mark() const noexcept { return used; }
    void rewind(size_t m) noexcept { used=m; }
};
struct Key { uint32_t x,y,z; };
inline uint32_t normalize_zero(uint32_t bits) noexcept { return bits==0x80000000u?0u:bits; }
inline uint64_t mix(uint64_t h) noexcept { h^=h>>33;h*=0xff51afd7ed558ccdull;h^=h>>33;h*=0xc4ceb9fe1a85ec53ull;h^=h>>33;return h; }
// Position key: the three bit patterns of the 2^-14 grid share their low mantissa
// bits (zero) and differ in a few high bits; each component is spread by its own
// odd multiplier before the 64-bit finalizer (fmix64), whose low bits select the slot.
inline uint64_t hash_key(const Key& k) noexcept { return mix((uint64_t(k.x)<<32|k.y)*0x9e3779b97f4a7c15ull^(uint64_t(k.z)*0xbf58476d1ce4e5b9ull)); }
inline uint64_t hash_cell(int32_t x,int32_t y,int32_t z) noexcept { return mix(uint64_t(uint32_t(x))*0x9e3779b97f4a7c15ull^mix(uint64_t(uint32_t(y)))^mix(uint64_t(uint32_t(z))*0x94d049bb133111ebull)); }
inline uint64_t table_size(uint64_t count) noexcept { uint64_t n=16;while(n<count*2)n<<=1;return n; } // load factor <= 1/2
// Bit-level helpers keep the module free of CRT floor/frexp/ldexp and 64-bit
// integer conversions, which the i386 compiler would route through x87.
inline bool is_integer(double t) noexcept {
    uint64_t bits;std::memcpy(&bits,&t,sizeof bits);
    const int exponent=int((bits>>52)&0x7ff)-1023;
    if(exponent<0)return (bits&0x7fffffffffffffffull)==0;
    if(exponent>=52)return true;
    return (bits&((uint64_t(1)<<(52-exponent))-1))==0;
}
inline double power_of_two(int exponent) noexcept { // |exponent| < 1023
    const uint64_t bits=uint64_t(1023+exponent)<<52;double v;std::memcpy(&v,&bits,sizeof v);return v;
}
inline int32_t floor_to_int(double q) noexcept { // |q| < 2^30 guaranteed by the caller
    int32_t i=int32_t(q);if(double(i)>q)--i;return i;
}
struct Vec { float x,y,z; };
inline Vec sub(const Vec& a,const Vec& b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec cross(const Vec& a,const Vec& b) noexcept { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float dot(const Vec& a,const Vec& b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec normalize(const Vec& v) noexcept {
    const float length=std::sqrt(dot(v,v));
    if(length>0.f)return {v.x/length,v.y/length,v.z/length};
    return {0.f,0.f,0.f};
}
inline Vec face_normal(const Vec* p,uint32_t v1,uint32_t v2,uint32_t v3) noexcept {
    return normalize(cross(sub(p[v1],p[v2]),sub(p[v1],p[v3])));
}
// A directed edge is identified by its id = face * 3 + point; its corners are
// corners[face*3 + point], corners[face*3 + (point+1)%3] and the third corner,
// so the edge table stores only the chain links and each slot only an anchor
// edge (whose corners are the slot's key; it stays valid after its chain
// empties, as a tombstone) and the chain head.
struct Slot { uint32_t anchor,head; };
struct NormalCache { // lazily malloc'd only when a chain offers several candidates
    Vec* normals=nullptr;unsigned char* ready=nullptr;
    ~NormalCache(){std::free(normals);std::free(ready);}
};
}
const char* status_name(unsigned status) noexcept {
    static constexpr const char* names[]={"ok","input","index_range","non_finite","magnitude","epsilon_neighbour","allocation"};
    return status<status_count?names[status]:"unknown";
}
void release_scratch() noexcept { std::free(arena.base);arena.base=nullptr;arena.capacity=0; }
Report generate(const Input& in,uint32_t* adjacency,const Policy& policy) noexcept {
    Report report;
    const uint32_t V=in.vertex_count,F=in.face_count;
    if(!in.vertices||!in.indices||!adjacency||!V||!F||in.stride<12||in.position_offset>in.stride-12||
       F>unused/3||!(in.epsilon>=0.f)||std::isinf(in.epsilon))return report; // Status::Input
    // The 4x squared-distance margin of the gate needs a normal float epsilon^2.
    if(in.epsilon>0.f&&!std::isnormal(in.epsilon*in.epsilon))return report;
    // Arena layout (bytes): persistent arrays, then the larger of the two phase
    // scratches (representative table; unquantized-gate cell hash).
    const uint64_t E=uint64_t(F)*3,vertex_slots=table_size(V),edge_slots=table_size(E);
    const uint64_t persistent=padded(uint64_t(V)*sizeof(Vec))+padded(uint64_t(V)*4)+padded(E*4)+padded(F)+padded(E)+padded(E)+padded(E*4)+padded(edge_slots*sizeof(Slot));
    const uint64_t phase_rep=padded(vertex_slots*4);
    const uint64_t phase_gate=padded(table_size(V)*4)+padded(uint64_t(V)*4)+padded(uint64_t(V)*12);
    const uint64_t total=persistent+(phase_rep>phase_gate?phase_rep:phase_gate);
    if(total>uint64_t(SIZE_MAX/2)){report.status=Status::Allocation;return report;}
    if(arena.capacity<size_t(total)){
        std::free(arena.base);arena.base=static_cast<unsigned char*>(std::malloc(size_t(total)));arena.capacity=arena.base?size_t(total):0;
        if(!arena.base){report.status=Status::Allocation;return report;}
    }
    struct Retain { ~Retain(){if(arena.capacity>retained_scratch_limit)release_scratch();} } retain;
    Bump scratch{arena.base,arena.capacity};
    auto* positions=scratch.array<Vec>(V);auto* rep=scratch.array<uint32_t>(V);
    auto* corners=scratch.array<uint32_t>(size_t(E));auto* active=scratch.array<unsigned char>(F);auto* valid=scratch.array<unsigned char>(size_t(E));
    auto* retired=scratch.array<unsigned char>(size_t(E));auto* next=scratch.array<uint32_t>(size_t(E));auto* slots=scratch.array<Slot>(size_t(edge_slots));
    const size_t persistent_mark=scratch.mark();
    if(scratch.overflow()){report.status=Status::Allocation;return report;}
    const auto* bytes=static_cast<const unsigned char*>(in.vertices);
    for(uint32_t v=0;v<V;++v){
        Key k;std::memcpy(&k,bytes+size_t(v)*in.stride+in.position_offset,sizeof k);
        for(uint32_t c:{k.x,k.y,k.z})if((c&0x7f800000u)==0x7f800000u){report.status=Status::NonFinite;return report;}
        k={normalize_zero(k.x),normalize_zero(k.y),normalize_zero(k.z)};
        std::memcpy(&positions[v],&k,sizeof k); // the normalised bit patterns double as the hash key
    }
    auto key_of=[&](uint32_t v) noexcept { Key k;std::memcpy(&k,&positions[v],sizeof k);return k; };
    // Exact-equality representatives: the first vertex with the same three bit patterns.
    {
        auto* table=scratch.array<uint32_t>(size_t(vertex_slots));
        if(scratch.overflow()){report.status=Status::Allocation;return report;}
        std::memset(table,0xff,size_t(vertex_slots)*sizeof(uint32_t));
        uint32_t representatives=0;
        for(uint32_t v=0;v<V;++v){
            const Key k=key_of(v);size_t slot=size_t(hash_key(k))&size_t(vertex_slots-1);
            for(;;){
                const uint32_t occupant=table[slot];
                if(occupant==unused){table[slot]=v;rep[v]=v;++representatives;break;}
                const Key o=key_of(occupant);
                if(o.x==k.x&&o.y==k.y&&o.z==k.z){rep[v]=occupant;break;}
                slot=(slot+1)&size_t(vertex_slots-1);
            }
        }
        report.representatives=representatives;report.welded=V-representatives;
        scratch.rewind(persistent_mark);
    }
    // Equivalence gate: distinct positions must be further apart than 2*epsilon.
    if(in.epsilon>0.f){
        // eps = 1.m * 2^E (normal): 2*eps < 2^(E+2), so the grid 2^(E+2) exceeds 2*eps.
        uint32_t epsilon_bits;std::memcpy(&epsilon_bits,&in.epsilon,sizeof epsilon_bits);
        const int exponent=int((epsilon_bits>>23)&0xff)-127+2;
        const double grid_inverse=power_of_two(-exponent); // exact scaling
        bool quantized=true;
        for(uint32_t v=0;v<V&&quantized;++v){
            if(rep[v]!=v)continue;
            for(float c:{positions[v].x,positions[v].y,positions[v].z})if(!is_integer(double(c)*grid_inverse)){quantized=false;break;}
        }
        report.quantized=quantized;
        if(!quantized){
            const double cell=4.0*double(in.epsilon),threshold=4.0*double(in.epsilon)*double(in.epsilon);
            const size_t cells=size_t(table_size(report.representatives));
            auto* heads=scratch.array<uint32_t>(cells);auto* chain=scratch.array<uint32_t>(V);auto* coords=scratch.array<int32_t>(size_t(V)*3);
            if(scratch.overflow()){report.status=Status::Allocation;return report;}
            std::memset(heads,0xff,cells*sizeof(uint32_t));
            constexpr double limit=1073741824.0; // 2^30: int32 cell coordinates with neighbour headroom
            for(uint32_t v=0;v<V;++v){
                if(rep[v]!=v)continue;
                const double q[3]={double(positions[v].x)/cell,double(positions[v].y)/cell,double(positions[v].z)/cell};
                for(double value:q)if(!(value>-limit&&value<limit)){report.status=Status::Magnitude;return report;}
                for(unsigned i=0;i<3;++i)coords[size_t(v)*3+i]=floor_to_int(q[i]);
                size_t slot=size_t(hash_cell(coords[size_t(v)*3],coords[size_t(v)*3+1],coords[size_t(v)*3+2]))&(cells-1);
                // Chains share a slot on equal cell; probing keeps distinct cells apart.
                for(;;){
                    const uint32_t head=heads[slot];
                    if(head==unused){heads[slot]=v;chain[v]=unused;break;}
                    if(coords[size_t(head)*3]==coords[size_t(v)*3]&&coords[size_t(head)*3+1]==coords[size_t(v)*3+1]&&coords[size_t(head)*3+2]==coords[size_t(v)*3+2]){chain[v]=heads[slot];heads[slot]=v;break;}
                    slot=(slot+1)&(cells-1);
                }
            }
            for(uint32_t v=0;v<V;++v){
                if(rep[v]!=v)continue;
                const int32_t cx=coords[size_t(v)*3],cy=coords[size_t(v)*3+1],cz=coords[size_t(v)*3+2];
                for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)for(int dz=-1;dz<=1;++dz){
                    const int32_t qx=cx+dx,qy=cy+dy,qz=cz+dz;
                    size_t slot=size_t(hash_cell(qx,qy,qz))&(cells-1);
                    for(;;){
                        const uint32_t head=heads[slot];if(head==unused)break;
                        if(coords[size_t(head)*3]==qx&&coords[size_t(head)*3+1]==qy&&coords[size_t(head)*3+2]==qz){
                            for(uint32_t o=head;o!=unused;o=chain[o]){
                                if(o==v)continue;
                                const double ex=double(positions[o].x)-double(positions[v].x),ey=double(positions[o].y)-double(positions[v].y),ez=double(positions[o].z)-double(positions[v].z);
                                if(ex*ex+ey*ey+ez*ez<=threshold){report.status=Status::EpsilonNeighbour;return report;}
                            }
                            break;
                        }
                        slot=(slot+1)&(cells-1);
                    }
                }
            }
            scratch.rewind(persistent_mark);
        }
    }
    // Face corners as representatives; raw-index degeneracy is recorded first.
    for(uint32_t f=0;f<F;++f){
        uint32_t raw[3];
        for(unsigned k=0;k<3;++k){
            const size_t i=size_t(f)*3+k;uint32_t index;
            if(in.indices_32bit)std::memcpy(&index,static_cast<const unsigned char*>(in.indices)+i*4,4);
            else{uint16_t narrow;std::memcpy(&narrow,static_cast<const unsigned char*>(in.indices)+i*2,2);index=narrow;}
            if(index>=V){report.status=Status::IndexRange;return report;}
            raw[k]=index;corners[i]=rep[index];
        }
        const bool raw_degenerate=raw[0]==raw[1]||raw[1]==raw[2]||raw[0]==raw[2];
        const uint32_t* c=corners+size_t(f)*3;
        const bool rep_degenerate=!raw_degenerate&&(c[0]==c[1]||c[1]==c[2]||c[0]==c[2]);
        report.degenerate_faces+=raw_degenerate;report.welded_degenerate_faces+=rep_degenerate;
        active[f]=!(policy.skip_raw_degenerate&&raw_degenerate)&&!(policy.skip_rep_degenerate&&rep_degenerate);
        // D3DX evidence (fixture cases degenerate-welded-*): of two corners
        // sharing a representative, the one with the larger raw index is invalid
        // (the representative itself, the smallest index of its class, stays valid).
        for(unsigned k=0;k<3;++k){bool ok=true;
            for(unsigned j=0;j<3;++j)if(j!=k&&c[j]==c[k]&&raw[j]<raw[k])ok=false;
            valid[size_t(f)*3+k]=!policy.drop_welded_corners||ok;}
    }
    // Directed-edge table keyed by the exact (v1,v2) pair, one chain per key
    // (open addressing; a chain that empties keeps its anchor as a tombstone).
    // The relative order inside a chain is what D3DX's hash chains expose; keying
    // by the pair keeps a vertex shared by thousands of faces from a quadratic
    // scan. Entries are never unlinked: a retired flag hides them from scans.
    std::memset(slots,0xff,size_t(edge_slots)*sizeof(Slot));
    const size_t edge_mask=size_t(edge_slots-1);
    auto edge_v1=[&](uint32_t id) noexcept { return corners[id]; };
    auto edge_v2=[&](uint32_t id) noexcept { const uint32_t f=id/3,k=id-f*3;return corners[size_t(f)*3+(k+1)%3]; };
    auto edge_other=[&](uint32_t id) noexcept { const uint32_t f=id/3,k=id-f*3;return corners[size_t(f)*3+(k+2)%3]; };
    auto slot_of=[&](uint32_t a,uint32_t b) noexcept { // slot holding key (a,b), or its first free slot
        size_t s=size_t(mix((uint64_t(a)<<32|b)*0x9e3779b97f4a7c15ull))&edge_mask;
        for(;;){const uint32_t anchor=slots[s].anchor;if(anchor==unused||(edge_v1(anchor)==a&&edge_v2(anchor)==b))return s;s=(s+1)&edge_mask;}
    };
    auto insert=[&](uint32_t id) noexcept {
        const size_t s=slot_of(edge_v1(id),edge_v2(id));if(slots[s].anchor==unused)slots[s].anchor=id;
        if(policy.head_insertion||slots[s].head==unused){next[id]=slots[s].head;slots[s].head=id;}
        else{uint32_t tail=slots[s].head;while(next[tail]!=unused)tail=next[tail];next[tail]=id;next[id]=unused;}
    };
    auto head_of=[&](uint32_t a,uint32_t b) noexcept { const size_t s=slot_of(a,b);return slots[s].anchor==unused?unused:slots[s].head; };
    std::memset(retired,0,size_t(E));
    // A face with a repeated raw index contributes no edge and pairs with
    // nothing; both edges touching an invalid corner are skipped, so (0,1,4) with
    // 4 welded to 0 keeps 0->1 only, (0,4,1) keeps 1->0 only, (4,5,1) keeps 1->4.
    for(uint32_t f=0;f<F;++f){
        if(!active[f])continue;
        const unsigned char* v=valid+size_t(f)*3;
        for(uint32_t k=0;k<3;++k){
            if(!(v[k]&&v[(k+1)%3])){retired[size_t(f)*3+k]=1;++report.dropped_edges;continue;}
            insert(f*3+k);
        }
    }
    for(size_t i=0;i<size_t(E);++i)adjacency[i]=unused;
    // Face normals are needed only where a chain offers several candidates; they
    // are then computed once per edge (from the edge's own corner order, as D3DX).
    NormalCache cache;
    auto normal_of=[&](uint32_t id) noexcept -> const Vec& {
        if(!cache.ready[id]){cache.normals[id]=face_normal(positions,edge_v1(id),edge_v2(id),edge_other(id));cache.ready[id]=1;}
        return cache.normals[id];
    };
    for(uint32_t f=0;f<F;++f)for(uint32_t k=0;k<3;++k){
        const uint32_t own=f*3+k;
        if(adjacency[own]!=unused)continue;
        if(!active[f]||retired[own]){++report.unmatched;continue;}
        const uint32_t vb=corners[own],va=edge_v2(own);
        if(va==vb){++report.unmatched;continue;}
        uint32_t found=unused;unsigned candidates=0;float best=-2.f;
        for(uint32_t cur=head_of(va,vb);cur!=unused;cur=next[cur]){
            if(retired[cur])continue;
            ++candidates;
            if(found==unused){found=cur;continue;}
            if(!policy.normal_selection)continue;
            if(!cache.normals){
                cache.normals=static_cast<Vec*>(std::malloc(size_t(E)*sizeof(Vec)));cache.ready=static_cast<unsigned char*>(std::malloc(size_t(E)));
                if(!cache.normals||!cache.ready){report.status=Status::Allocation;return report;}
                std::memset(cache.ready,0,size_t(E));
            }
            if(candidates==2)best=dot(normal_of(found),normal_of(own));
            const float diff=dot(normal_of(cur),normal_of(own));
            if(diff>best){best=diff;found=cur;++report.normal_selected;}
        }
        if(candidates>1)++report.multi_candidates;
        // The querying face's own entry is retired once its point is processed:
        // every later face's reverse edge was already in the table and searched.
        retired[own]=1;
        if(found==unused){++report.unmatched;continue;}
        // D3DX evidence (fixture cases duplicate-faces, double-adjacency-order):
        // two faces never become adjacent across a second edge; the selected
        // candidate is refused after selection, and its entry stays for others.
        const uint32_t* row=adjacency+size_t(f)*3;const uint32_t g=found/3;
        if(policy.single_adjacency&&(row[0]==g||row[1]==g||row[2]==g)){++report.repeated_neighbours;++report.unmatched;continue;}
        retired[found]=1;
        adjacency[own]=g;
        adjacency[found]=f;
    }
    report.status=Status::Ok;return report;
}
}
