// Host driver for the pure adjacency module (no Windows). Reads one mesh from
// stdin, prints the report and the adjacency; verification/analysis/
// test_mesh_adjacency_fast.py compares it with the Python reference port.
// Input: V F bits eps stride offset head normal skip_raw skip_rep drop_welded single_adjacency, then V lines "xbits ybits zbits"
// (hex float bit patterns), then F lines "i0 i1 i2".
#include "../../src/proxy/mesh_adjacency_fast.h"
#include <cstdio>
#include <cstring>
#include <vector>
int main(){
    unsigned V=0,F=0,bits=0,stride=0,offset=0,head=1,normal=1,skip_raw=1,skip_rep=0,drop=1,single=1;float eps=0.f;
    if(std::scanf("%u %u %u %g %u %u %u %u %u %u %u %u",&V,&F,&bits,&eps,&stride,&offset,&head,&normal,&skip_raw,&skip_rep,&drop,&single)!=12)return 2;
    std::vector<unsigned char> vertices(size_t(V)*stride);std::vector<unsigned char> indices(size_t(F)*3*(bits==32?4:2));
    for(unsigned v=0;v<V;++v){unsigned x,y,z;if(std::scanf("%x %x %x",&x,&y,&z)!=3)return 2;
        const unsigned c[3]={x,y,z};std::memcpy(&vertices[size_t(v)*stride+offset],c,12);}
    for(unsigned i=0;i<F*3;++i){unsigned index;if(std::scanf("%u",&index)!=1)return 2;
        if(bits==32)std::memcpy(&indices[size_t(i)*4],&index,4);else{unsigned short narrow=(unsigned short)index;std::memcpy(&indices[size_t(i)*2],&narrow,2);}}
    x3m::mesh_adjacency_fast::Input in;in.vertices=vertices.data();in.vertex_count=V;in.stride=stride;in.position_offset=offset;
    in.indices=indices.data();in.indices_32bit=bits==32;in.face_count=F;in.epsilon=eps;
    x3m::mesh_adjacency_fast::Policy policy;policy.head_insertion=head!=0;policy.normal_selection=normal!=0;policy.skip_raw_degenerate=skip_raw!=0;policy.skip_rep_degenerate=skip_rep!=0;policy.drop_welded_corners=drop!=0;policy.single_adjacency=single!=0;
    std::vector<uint32_t> adjacency(size_t(F)*3,0xabcdefu);
    const auto r=x3m::mesh_adjacency_fast::generate(in,adjacency.data(),policy);
    std::printf("%s %u %u %u %u %u %u %u %u %u %u\n",x3m::mesh_adjacency_fast::status_name(unsigned(r.status)),r.representatives,r.welded,unsigned(r.quantized),r.degenerate_faces,r.welded_degenerate_faces,r.dropped_edges,r.multi_candidates,r.normal_selected,r.repeated_neighbours,r.unmatched);
    if(r.status==x3m::mesh_adjacency_fast::Status::Ok){for(auto a:adjacency)std::printf("%d ",a==0xffffffffu?-1:int(a));std::printf("\n");}
    return 0;
}
