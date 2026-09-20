#include "../../src/renderer/fog_volume_math.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstring>
using namespace x3m::renderer;
int main(){
    const double identity[9]={1,0,0,0,1,0,0,0,1};
    const double t[3]={-65539,32770,-.25},sun[3]={0,0,2};FogWorldBasis b;
    assert(fog_world_basis(identity,t,sun,b));
    assert(b.valid&&b.origin_mod[0]==3&&b.origin_mod[1]==32766&&b.origin_mod[2]==.25f&&b.sun_world[2]==1);
    // Nontrivial row-vector rotation and a slightly rounded captured basis.
    const double angle=.41,c=std::cos(angle),s=std::sin(angle);
    const double rotation[9]={c,0,s,0,1,0,-s,0,c};
    assert(fog_world_basis(rotation,t,sun,b));
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){
        double v=0;for(unsigned k=0;k<3;++k)v+=rotation[3*row+k]*b.inverse_columns[3*col+k];
        assert(std::abs(v-(row==col?1:0))<1e-7);
    }
    assert(std::abs(b.sun_world[0]-s)<1e-7&&std::abs(b.sun_world[2]-c)<1e-7);
    double rounded[9]={1.000002,0,0,0,.999998,0,0,0,1};
    assert(fog_world_basis(rounded,t,sun,b));assert(b.inverse_columns[0]<1); // true inverse, not transpose
    rounded[0]=2;assert(!fog_world_basis(rounded,t,sun,b)&&!b.valid);
    rounded[0]=std::numeric_limits<double>::quiet_NaN();assert(!fog_world_basis(rounded,t,sun,b));
    double bad_t[3]={std::numeric_limits<double>::infinity(),0,0};assert(!fog_world_basis(identity,bad_t,sun,b));
    const double zero[3]{};assert(!fog_world_basis(identity,t,zero,b));
    const double reflection[9]={-1,0,0,0,1,0,0,0,1};assert(!fog_world_basis(reflection,t,sun,b));
    float phase[4],light[4],E[3]={fog_volume_pi,fog_volume_pi,fog_volume_pi};
    fog_phase_constants(.3f,2.2f,E,phase,light);
    assert(phase[0]==1.09f&&phase[1]==.6f&&phase[2]==.91f&&light[0]==1&&light[3]==0.4545454680919647f);
    std::uint32_t encode_bits=0;std::memcpy(&encode_bits,&light[3],sizeof encode_bits);assert(encode_bits==0x3ee8ba2f);
    fog_phase_constants(0,1,E,phase,light);assert(phase[0]==1&&phase[1]==0&&phase[2]==1&&light[3]==1);
    E[0]=0;E[1]=2*fog_volume_pi;fog_phase_constants(.9f,1,E,phase,light);
    assert(light[0]==0&&light[1]==2&&phase[0]>1.8f&&phase[2]>.18f);
}
