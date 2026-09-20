#include "fog_volume_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() { unsigned frame=0,n=0,accepted=0; double r[9],t[3],sun[3]={0,0,1},worst=0,detmin=2,detmax=-2;
  while (std::scanf("%u",&frame)==1) { for(double& x:r) assert(std::scanf("%lf",&x)==1); for(double& x:t) assert(std::scanf("%lf",&x)==1);
    x3m::renderer::FogWorldBasis basis; const bool ok=x3m::renderer::fog_world_basis(r,t,sun,basis); accepted+=ok; ++n;
    double det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]); detmin=fmin(detmin,det); detmax=fmax(detmax,det);
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) { double dot=0; for(unsigned k=0;k<3;++k) dot+=r[3*i+k]*r[3*j+k]; worst=fmax(worst,fabs(dot-(i==j?1.:0.))); }
  }
  std::printf("rows=%u accepted=%u gram_max=%.12g det_min=%.12g det_max=%.12g\n",n,accepted,worst,detmin,detmax);
  assert(n==32 && accepted==32);
}