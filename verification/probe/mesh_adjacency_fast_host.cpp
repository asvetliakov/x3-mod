// Host driver for the pure adjacency module (no Windows). Reads one mesh from
// stdin, prints the report and the adjacency; verification/analysis/
// test_mesh_adjacency_fast.py compares it with the Python reference port.
// Input: V F bits eps stride offset head normal weld_refusal heap_order retire_own unlink_refused later_slot normalize,
// (normalize: 0 the SSE2 table's rsqrtss, 1 the generic table's interpolation),
// then V lines "xbits ybits zbits hx hy hz" (hex float bit patterns; the head
// triple is written to byte 0 of the vertex when offset > 0, else byte 0 holds the position),
// then F lines "i0 i1 i2".
#include "../../src/proxy/mesh_adjacency_fast.h"
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc,char** argv){
    if(argc==5&&!std::strcmp(argv[1],"fp-domain")){unsigned cw=0,tag=0,mx=0;if(std::sscanf(argv[2],"%x",&cw)!=1||std::sscanf(argv[3],"%x",&tag)!=1||std::sscanf(argv[4],"%x",&mx)!=1)return 2;std::printf("%u\n",unsigned(x3m::mesh_adjacency_fast::supported_fp_domain(cw,tag,mx)));return 0;}
    unsigned V=0,F=0,bits=0,stride=0,offset=0,head=1,normal=1,refusal=1,heap=1,retire=0,unlink=1,later=0,normalize=0;float eps=0.f;
    if(std::scanf("%u %u %u %g %u %u %u %u %u %u %u %u %u %u",&V,&F,&bits,&eps,&stride,&offset,&head,&normal,&refusal,&heap,&retire,&unlink,&later,&normalize)!=14)return 2;
    std::vector<unsigned char> vertices(size_t(V)*stride);std::vector<unsigned char> indices(size_t(F)*3*(bits==32?4:2));
    for(unsigned v=0;v<V;++v){unsigned x,y,z,hx,hy,hz;if(std::scanf("%x %x %x %x %x %x",&x,&y,&z,&hx,&hy,&hz)!=6)return 2;
        const unsigned c[3]={x,y,z},h[3]={hx,hy,hz};
        if(offset)std::memcpy(&vertices[size_t(v)*stride],h,12); // the position overwrites the overlap when offset < 12
        std::memcpy(&vertices[size_t(v)*stride+offset],c,12);}
    for(unsigned i=0;i<F*3;++i){unsigned index;if(std::scanf("%u",&index)!=1)return 2;
        if(bits==32)std::memcpy(&indices[size_t(i)*4],&index,4);else{unsigned short narrow=(unsigned short)index;std::memcpy(&indices[size_t(i)*2],&narrow,2);}}
    x3m::mesh_adjacency_fast::Input in;in.vertices=vertices.data();in.vertex_count=V;in.stride=stride;in.position_offset=offset;
    in.indices=indices.data();in.indices_32bit=bits==32;in.face_count=F;in.epsilon=eps;
    x3m::mesh_adjacency_fast::Policy policy;policy.head_insertion=head!=0;policy.normal_selection=normal!=0;policy.weld_refusal=refusal!=0;policy.heap_order=heap!=0;policy.retire_own_entry=retire!=0;policy.unlink_refused=unlink!=0;policy.later_slot_check=later!=0;
    policy.refuse_competing_normals=argc==2&&!std::strcmp(argv[1],"refuse-competing");
    policy.normalize=normalize?x3m::mesh_adjacency_fast::Normalize::Generic:x3m::mesh_adjacency_fast::Normalize::Sse2;
    std::vector<uint32_t> adjacency(size_t(F)*3,0xabcdefu);
    const auto r=x3m::mesh_adjacency_fast::generate(in,adjacency.data(),policy);
    std::printf("%s %u %u %u %u %u %u %u %u %u %u %s %s\n",x3m::mesh_adjacency_fast::status_name(unsigned(r.status)),r.representatives,r.welded,unsigned(r.quantized),r.degenerate_faces,r.welded_degenerate_faces,r.refused_welds,r.multi_candidates,r.normal_selected,r.repeated_neighbours,r.unmatched,x3m::mesh_adjacency_fast::rsqrt_implementation(),x3m::mesh_adjacency_fast::normalize_name(policy.normalize));
    if(r.status==x3m::mesh_adjacency_fast::Status::Ok){for(auto a:adjacency)std::printf("%d ",a==0xffffffffu?-1:int(a));std::printf("\n");}
    if(r.status!=x3m::mesh_adjacency_fast::Status::Ok)for(auto a:adjacency)if(a!=0xabcdefu)return 3;
    return 0;
}
