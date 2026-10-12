#include "ship_steering_core.h"
#include <cstdio>
#include <cmath>
namespace core = x3m::ship_steering::core;
int main(){
  const float r=1105, G[3]={0,0,0}, row1[3]={0,1,0}, row2[3]={0,0,1};
  const float A[3]={0,-0.8f*r,-1.0f*r};
  core::GateGeometry g; if(!core::gate_geometry(A,G,row1,row2,r,&g)){puts("geo fail");return 1;}
  struct St{const char*n; float x[3];} starts[]={{"front_axis_3km",{0,-0.8f*r,-1.0f*r-3000}},{"behind_gate_3r",{0,0,3*r}},{"side_4r",{4*r,-0.8f*r,-1.0f*r}},{"above_gate_2r",{0,2*r,0}},{"front_far_10r",{0,-0.8f*r,-11*r}},{"front_2r_side_3r",{3*r,-0.8f*r,-3*r}},{"front_1r_side_2r",{2*r,-0.8f*r,-2*r}},{"below_A_2r",{0,-2.8f*r,-1*r}},{"front_0r_side_1r_v500",{1*r,-0.8f*r,-1.9f*r}}};
  for(auto&st:starts){ float x[3]={st.x[0],st.x[1],st.x[2]}; float v=100, dt=0.02f; float mind=1e30f; int t; float s=0; int lead_calls=0;
    for(t=0;t<60000;++t){ float P[3]; if(core::gate_lead_in(g,x,v,P,&s)) ++lead_calls; else {P[0]=A[0];P[1]=A[1];P[2]=A[2];}
      float d[3]={P[0]-x[0],P[1]-x[1],P[2]-x[2]}; float n=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(n<1e-3f)break;
      for(int i=0;i<3;++i) x[i]+=d[i]/n*v*dt;
      float da[3]={A[0]-x[0],A[1]-x[1],A[2]-x[2]}; float dA=std::sqrt(da[0]*da[0]+da[1]*da[1]+da[2]*da[2]); if(dA<mind)mind=dA; if(dA<=0.75f*r)break; }
    printf("%-16s arrived=%d t=%.0fs min_dA=%.0f m (0.75r=%.0f) s_end=%.0f lead_calls=%d\n",st.n,mind<=0.75f*r,t*dt,mind,0.75f*r,s,lead_calls);}
}
