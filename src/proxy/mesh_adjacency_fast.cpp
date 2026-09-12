#include "mesh_adjacency_fast.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace x3m::mesh_adjacency_fast {
namespace {
struct Scratch { // malloc-backed; every allocation failure reports Status::Allocation.
    void* blocks[16]{};unsigned used=0;bool failed=false;
    ~Scratch(){for(unsigned i=0;i<used;++i)std::free(blocks[i]);}
    template<class T> T* array(size_t count) noexcept {
        if(failed||used>=16||count>SIZE_MAX/sizeof(T)){failed=true;return nullptr;}
        void* p=std::malloc(count?count*sizeof(T):1);if(!p){failed=true;return nullptr;}
        blocks[used++]=p;return static_cast<T*>(p);
    }
};
struct Key { uint32_t x,y,z; };
inline uint32_t normalize_zero(uint32_t bits) noexcept { return bits==0x80000000u?0u:bits; }
inline uint64_t mix(uint64_t h) noexcept { h^=h>>33;h*=0xff51afd7ed558ccdull;h^=h>>33;h*=0xc4ceb9fe1a85ec53ull;h^=h>>33;return h; }
inline uint64_t hash_key(const Key& k) noexcept { return mix((uint64_t(k.x)<<32|k.y)*0x9e3779b97f4a7c15ull^(uint64_t(k.z)*0xbf58476d1ce4e5b9ull)); }
inline uint64_t hash_cell(int32_t x,int32_t y,int32_t z) noexcept { return mix(uint64_t(uint32_t(x))*0x9e3779b97f4a7c15ull^mix(uint64_t(uint32_t(y)))^mix(uint64_t(uint32_t(z))*0x94d049bb133111ebull)); }
inline size_t table_size(uint32_t count) noexcept { size_t n=16;while(n<size_t(count)*2)n<<=1;return n; }
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
struct Edge { uint32_t v1,v2,other,face,point,next; };
}
const char* status_name(unsigned status) noexcept {
    static constexpr const char* names[]={"ok","input","index_range","non_finite","magnitude","epsilon_neighbour","allocation"};
    return status<status_count?names[status]:"unknown";
}
Report generate(const Input& in,uint32_t* adjacency,const Policy& policy) noexcept {
    Report report;
    const uint32_t V=in.vertex_count,F=in.face_count;
    if(!in.vertices||!in.indices||!adjacency||!V||!F||in.stride<12||in.position_offset>in.stride-12||
       F>unused/3||!(in.epsilon>=0.f)||std::isinf(in.epsilon))return report; // Status::Input
    // The 4x squared-distance margin of the gate needs a normal float epsilon^2.
    if(in.epsilon>0.f&&!std::isnormal(in.epsilon*in.epsilon))return report;
    Scratch scratch;
    auto* positions=scratch.array<Vec>(V);auto* keys=scratch.array<Key>(V);auto* rep=scratch.array<uint32_t>(V);
    if(scratch.failed){report.status=Status::Allocation;return report;}
    const auto* bytes=static_cast<const unsigned char*>(in.vertices);
    for(uint32_t v=0;v<V;++v){
        Key k;std::memcpy(&k,bytes+size_t(v)*in.stride+in.position_offset,sizeof k);
        for(uint32_t c:{k.x,k.y,k.z})if((c&0x7f800000u)==0x7f800000u){report.status=Status::NonFinite;return report;}
        k={normalize_zero(k.x),normalize_zero(k.y),normalize_zero(k.z)};keys[v]=k;
        std::memcpy(&positions[v],&k,sizeof k);
    }
    // Exact-equality representatives: the first vertex with the same three bit patterns.
    const size_t slots=table_size(V);auto* table=scratch.array<uint32_t>(slots);
    if(scratch.failed){report.status=Status::Allocation;return report;}
    std::memset(table,0xff,slots*sizeof(uint32_t));
    uint32_t representatives=0;
    for(uint32_t v=0;v<V;++v){
        size_t slot=size_t(hash_key(keys[v]))&(slots-1);
        for(;;){
            const uint32_t occupant=table[slot];
            if(occupant==unused){table[slot]=v;rep[v]=v;++representatives;break;}
            const Key& o=keys[occupant];
            if(o.x==keys[v].x&&o.y==keys[v].y&&o.z==keys[v].z){rep[v]=occupant;break;}
            slot=(slot+1)&(slots-1);
        }
    }
    report.representatives=representatives;report.welded=V-representatives;
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
            const size_t cells=table_size(representatives);
            auto* heads=scratch.array<uint32_t>(cells);auto* next=scratch.array<uint32_t>(V);auto* coords=scratch.array<int32_t>(size_t(V)*3);
            if(scratch.failed){report.status=Status::Allocation;return report;}
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
                    if(head==unused){heads[slot]=v;next[v]=unused;break;}
                    if(coords[size_t(head)*3]==coords[size_t(v)*3]&&coords[size_t(head)*3+1]==coords[size_t(v)*3+1]&&coords[size_t(head)*3+2]==coords[size_t(v)*3+2]){next[v]=heads[slot];heads[slot]=v;break;}
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
                            for(uint32_t o=head;o!=unused;o=next[o]){
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
        }
    }
    // Face corners as representatives; raw-index degeneracy is recorded first.
    auto* corners=scratch.array<uint32_t>(size_t(F)*3);auto* active=scratch.array<unsigned char>(F);auto* valid=scratch.array<unsigned char>(size_t(F)*3);
    if(scratch.failed){report.status=Status::Allocation;return report;}
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
    // (open addressing; a chain that empties keeps its key as a tombstone). The
    // relative order inside a chain is what D3DX's hash chains expose; keying by
    // the pair keeps a vertex shared by thousands of faces from a quadratic scan.
    const size_t edge_slots=table_size(F>unused/6?unused/2:F*3);
    auto* edges=scratch.array<Edge>(size_t(F)*3);auto* slot_key=scratch.array<uint64_t>(edge_slots);auto* slot_head=scratch.array<uint32_t>(edge_slots);
    if(scratch.failed){report.status=Status::Allocation;return report;}
    std::memset(slot_key,0xff,edge_slots*sizeof(uint64_t));std::memset(slot_head,0xff,edge_slots*sizeof(uint32_t));
    constexpr uint64_t no_key=~uint64_t(0);
    auto slot_of=[&](uint32_t a,uint32_t b) noexcept { // slot holding key (a,b), or its first free slot
        const uint64_t key=uint64_t(a)<<32|b;size_t s=size_t(mix(key*0x9e3779b97f4a7c15ull))&(edge_slots-1);
        for(;;){if(slot_key[s]==key||slot_key[s]==no_key)return s;s=(s+1)&(edge_slots-1);}
    };
    auto insert=[&](uint32_t id) noexcept {
        Edge& e=edges[id];const size_t s=slot_of(e.v1,e.v2);slot_key[s]=uint64_t(e.v1)<<32|e.v2;
        if(policy.head_insertion||slot_head[s]==unused){e.next=slot_head[s];slot_head[s]=id;}
        else{uint32_t tail=slot_head[s];while(edges[tail].next!=unused)tail=edges[tail].next;edges[tail].next=id;e.next=unused;}
    };
    auto head_of=[&](uint32_t a,uint32_t b) noexcept { const size_t s=slot_of(a,b);return slot_key[s]==no_key?unused:slot_head[s]; };
    auto remove=[&](uint32_t id) noexcept {
        const Edge& e=edges[id];const size_t s=slot_of(e.v1,e.v2);if(slot_key[s]==no_key)return;
        uint32_t prev=unused;
        for(uint32_t cur=slot_head[s];cur!=unused;prev=cur,cur=edges[cur].next){
            if(cur!=id)continue;
            if(prev==unused)slot_head[s]=edges[cur].next;else edges[prev].next=edges[cur].next;
            return;
        }
    };
    auto* dropped=scratch.array<unsigned char>(size_t(F)*3);
    if(scratch.failed){report.status=Status::Allocation;return report;}
    std::memset(dropped,0,size_t(F)*3);
    // A face with a repeated raw index contributes no edge and pairs with
    // nothing; both edges touching an invalid corner are skipped, so (0,1,4) with
    // 4 welded to 0 keeps 0->1 only, (0,4,1) keeps 1->0 only, (4,5,1) keeps 1->4.
    for(uint32_t f=0;f<F;++f){
        const uint32_t* c=corners+size_t(f)*3;const unsigned char* v=valid+size_t(f)*3;
        for(uint32_t k=0;k<3;++k){
        const uint32_t va=c[k],vb=c[(k+1)%3],other=c[(k+2)%3];
        Edge& e=edges[size_t(f)*3+k];e={va,vb,other,f,k,unused};
        if(!active[f])continue;
        if(!(v[k]&&v[(k+1)%3])){dropped[size_t(f)*3+k]=1;++report.dropped_edges;continue;}
        insert(f*3+k);
    }}
    for(size_t i=0;i<size_t(F)*3;++i)adjacency[i]=unused;
    for(uint32_t f=0;f<F;++f)for(uint32_t k=0;k<3;++k){
        if(adjacency[size_t(f)*3+k]!=unused)continue;
        if(!active[f]||dropped[size_t(f)*3+k]){++report.unmatched;continue;}
        const uint32_t vb=corners[size_t(f)*3+k],va=corners[size_t(f)*3+(k+1)%3],other=corners[size_t(f)*3+(k+2)%3];
        if(va==vb){++report.unmatched;continue;}
        uint32_t found=unused;unsigned candidates=0;float best=-2.f;Vec own{};bool own_ready=false;
        for(uint32_t cur=head_of(va,vb);cur!=unused;cur=edges[cur].next){
            const Edge& e=edges[cur];
            ++candidates;
            if(found==unused){found=cur;continue;}
            if(!policy.normal_selection)continue;
            if(!own_ready){own=face_normal(positions,vb,va,other);best=dot(face_normal(positions,edges[found].v1,edges[found].v2,edges[found].other),own);own_ready=true;}
            const float diff=dot(face_normal(positions,e.v1,e.v2,e.other),own);
            if(diff>best){best=diff;found=cur;++report.normal_selected;}
        }
        if(candidates>1)++report.multi_candidates;
        // The querying face's own entry is retired once its point is processed:
        // every later face's reverse edge was already in the table and searched.
        remove(f*3+k);
        if(found==unused){++report.unmatched;continue;}
        // D3DX evidence (fixture cases duplicate-faces, double-adjacency-order):
        // two faces never become adjacent across a second edge; the selected
        // candidate is refused after selection, and its entry stays for others.
        const uint32_t* row=adjacency+size_t(f)*3;const uint32_t g=edges[found].face;
        if(policy.single_adjacency&&(row[0]==g||row[1]==g||row[2]==g)){++report.repeated_neighbours;++report.unmatched;continue;}
        remove(found);
        adjacency[size_t(f)*3+k]=g;
        adjacency[size_t(g)*3+edges[found].point]=f;
    }
    report.status=Status::Ok;return report;
}
}
