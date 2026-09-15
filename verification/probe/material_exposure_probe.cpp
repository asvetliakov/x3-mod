// Host-only focused helper failure witnesses, without bypassing live admission.
#include "../../src/renderer/linear_material.cpp"
#include <iostream>
#include <stdexcept>
int main() {
    using namespace x3m::renderer;
    unsigned checks=0;
    const auto expect=[&](bool value) {++checks;if (!value) throw std::runtime_error("helper guard");};
    Words code{0xffff0300};emit(code,mad,{dst(temp,1),src(temp,2),lane(constant,3,0),src(temp,4)});code.push_back(end_token);
    Structure s;s.boundary.assign(code.size(),0);s.boundary[1]=1;s.instructions.push_back({1,mad,4});
    ExposureSeed seed{0,1,xyz,true};expect(exposure_seed_valid(code.data(),s,&seed));
    auto changed=code;changed[1]=(4u<<24)|add;expect(!exposure_seed_valid(changed.data(),s,&seed));
    changed=code;changed[2]|=sat;expect(!exposure_seed_valid(changed.data(),s,&seed));
    changed=code;changed[2]=dst(color_output,0);expect(!exposure_seed_valid(changed.data(),s,&seed));
    changed=code;changed[2]=dst(temp,1,1);expect(!exposure_seed_valid(changed.data(),s,&seed));
    for (unsigned n=3;n<=5;++n) {changed=code;changed[n]|=relative;expect(!exposure_seed_valid(changed.data(),s,&seed));}
    expect(!exposure_seed_valid(code.data(),s,nullptr));++seed.at;expect(!exposure_seed_valid(code.data(),s,&seed));
    for (bool vertex:{false,true}) {
        expect(exposure_reservations(code.data(),s,vertex));expect(exposure_motion_reservations(code,vertex));
        for (unsigned n=2;n<=5;++n) {
            changed=code;changed[n]=src(constant,vertex?250:222);
            expect(!exposure_reservations(changed.data(),s,vertex));expect(!exposure_motion_reservations(changed,vertex));
            changed[n]=src(temp,16);expect(!exposure_reservations(changed.data(),s,vertex));expect(!exposure_motion_reservations(changed,vertex));
        }
        Words definition{vertex?0xfffe0300u:0xffff0300u};emit(definition,def,{dst(constant,vertex?250:222,xyzw),0,0,0,0});definition.push_back(end_token);
        Structure ds;ds.instructions.push_back({1,def,5});
        expect(!exposure_reservations(definition.data(),ds,vertex));expect(!exposure_motion_reservations(definition,vertex));
        definition[2]=dst(constant,0,xyzw);definition[3]=src(constant,vertex?250:222);
        expect(exposure_reservations(definition.data(),ds,vertex));expect(exposure_motion_reservations(definition,vertex));
    }
    std::cout<<"{\"guard_checks\":"<<checks<<"}\n";
}
