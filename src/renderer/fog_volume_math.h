#pragma once
// Public, D3D-free values copied from the current camera. No engine pointer is retained.
#include <cmath>
namespace x3m::renderer {
constexpr double fog_volume_period = 32768.0;
constexpr float fog_volume_horizon = 12000.f;
constexpr float fog_volume_pi = 3.14159265358979323846f;
struct FogWorldBasis {
    float origin_mod[3]{};
    // Columns of inverse(row-vector world->view R), uploaded as shader dot rows.
    float inverse_columns[9]{};
    float sun_world[3]{};
    bool valid = false;
    // Unwrapped camera position in world render units (the stored-density path's camera_world).
    double origin[3]{};
};
inline bool fog_world_inverse(const double rotation[9], double inv[9]) noexcept {
    if (!rotation) return false;
    for (unsigned i=0;i<9;++i) if (!std::isfinite(rotation[i])) return false;
    // Match camera_state_from_matrices' 1e-3 near-rigid Gram tolerance: the
    // engine's fixed-point basis can retain small normalization drift (run197
    // reaches 1.53e-4). Reject scaling/malformed matrices, but do not reject a
    // valid camera merely for exceeding the old 1e-4 threshold. Keep the true
    // inverse, not a transpose or a reorthogonalized view.
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) {
        double dot=0; for(unsigned k=0;k<3;++k) dot+=rotation[3*i+k]*rotation[3*j+k];
        if (std::abs(dot-(i==j?1.0:0.0))>1e-3) return false;
    }
    const double* r=rotation;
    const double det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
    if (!std::isfinite(det) || det<0.999 || det>1.001) return false;
    const double value[9]={
        (r[4]*r[8]-r[5]*r[7])/det,(r[2]*r[7]-r[1]*r[8])/det,(r[1]*r[5]-r[2]*r[4])/det,
        (r[5]*r[6]-r[3]*r[8])/det,(r[0]*r[8]-r[2]*r[6])/det,(r[2]*r[3]-r[0]*r[5])/det,
        (r[3]*r[7]-r[4]*r[6])/det,(r[1]*r[6]-r[0]*r[7])/det,(r[0]*r[4]-r[1]*r[3])/det};
    for (unsigned i=0;i<9;++i) inv[i]=value[i];
    return true;
}
// Unwrapped world camera of a row-vector world->view (R,t): -t*inverse(R), the same sums as
// FogWorldBasis::origin. No sun needed: the stored-density cache follows it on refused frames too.
inline bool fog_world_camera(const double rotation[9], const double translation[3], double camera[3]) noexcept {
    double inv[9];
    if (!translation || !camera || !fog_world_inverse(rotation, inv)) return false;
    for (unsigned i=0;i<3;++i) if (!std::isfinite(translation[i])) return false;
    for (unsigned col=0;col<3;++col) {
        double origin=0;
        for (unsigned row=0;row<3;++row) origin-=translation[row]*inv[3*row+col];
        if (!std::isfinite(origin)) return false;
        camera[col]=origin;
    }
    return true;
}
inline bool fog_world_basis(const double rotation[9], const double translation[3],
                            const double sun_view[3], FogWorldBasis& output) noexcept {
    output = {};
    if (!rotation || !translation || !sun_view) return false;
    for (unsigned i=0;i<3;++i) if (!std::isfinite(translation[i]) || !std::isfinite(sun_view[i])) return false;
    double inv[9];
    if (!fog_world_inverse(rotation, inv)) return false;
    FogWorldBasis result{}; double sun[3]{},length2=0;
    for(unsigned col=0;col<3;++col) {
        double origin=0;
        for(unsigned row=0;row<3;++row) {
            origin-=translation[row]*inv[3*row+col];
            sun[col]+=sun_view[row]*inv[3*row+col];
            result.inverse_columns[3*col+row]=static_cast<float>(inv[3*row+col]);
        }
        if (!std::isfinite(origin) || !std::isfinite(sun[col])) return false;
        result.origin[col]=origin;
        double mod=std::fmod(origin,fog_volume_period); if(mod<0) mod+=fog_volume_period;
        result.origin_mod[col]=static_cast<float>(mod);
        // A value immediately below the period can round up to it in float32.
        if(result.origin_mod[col]>=fog_volume_period) result.origin_mod[col]=0;
        length2+=sun[col]*sun[col];
    }
    if (!std::isfinite(length2) || length2<1e-12) return false;
    const double length=std::sqrt(length2);
    for(unsigned i=0;i<3;++i) result.sun_world[i]=static_cast<float>(sun[i]/length);
    result.valid=true; output=result; return true;
}
inline void fog_phase_constants(float g,float gamma,const float radiance[3],float phase[4],float light[4]) noexcept {
    // Preserve the qualified default coefficients' rounding, while other g uses HG.
    phase[0]=g==.3f?1.09f:1.f+g*g; phase[1]=g==.3f?.6f:2.f*g;
    phase[2]=g==.3f?.91f:1.f-g*g; phase[3]=gamma;
    for(unsigned i=0;i<3;++i) light[i]=radiance[i]/fog_volume_pi;
    // Qualified HLSL folds 1.0/2.2 before float32 conversion (0x3ee8ba2f).
    light[3]=gamma==2.2f?0.4545454680919647f:1.f/gamma;
}
} // namespace x3m::renderer
