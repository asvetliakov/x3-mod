// Included inside linear_material.cpp's anonymous namespace. This is a bounded
// extension for the 14 reviewed XT originals, not a general shader rewriter.
#include "linear_xt_profiles_inc.h"
constexpr std::uint64_t xt_default_vs = 0x494fe349b8bc12ecull;
constexpr std::uint64_t xt_bump_vs = 0x37c34a7478544c14ull;
const XtPixel* xt_pixel(std::uint64_t hash) noexcept {
    for (const auto& p:xt_pixels) if (p.hash==hash) return &p;
    return nullptr;
}
FamilyAbi xt_abi(bool bump) noexcept {
    return bump ? FamilyAbi{8,7,10,9,8,7,10,9,8} : FamilyAbi{10,8,10,6,6,4,7,7,7};
}
bool xt_instruction_at(const Structure& s, unsigned at) noexcept {
    return at<s.boundary.size() && s.boundary[at];
}
bool xt_vertex_sites(const Word* code, const Structure& s, bool bump) noexcept {
    const unsigned point=bump?593:428, emissive=bump?608:443, alpha=bump?683:500;
    if (!exact(code,s,point,mul,dst(temp,bump?1:5),{lane(temp,3,3),src(constant,1)|relative,src(3,0,255)}) ||
        !exact(code,s,emissive,add,dst(output_reg,1),{src(temp,0),src(constant,bump?41:40)}) ||
        !exact(code,s,alpha,mul,dst(output_reg,1,8),{lane(temp,bump?2:0,3),lane(constant,bump?40:39,0)}) ||
        !exact(code,s,alpha+5,mov,dst(output_reg,1,8),{lane(constant,bump?40:39,0)})) return false;
    if (bump && (!exact(code,s,733,14,dst(output_reg,8,2),{lane(temp,2,2)}) ||
        !exact(code,s,743,mad,dst(output_reg,8,1),{lane(temp,2,3),lane(temp,2,2),lane(constant,43,0)}))) return false;
    unsigned colors=0, scalar=0, view=0, normal=0;
    for (const auto& i:s.instructions) {
        if (i.opcode==dcl && kind(code[i.at+2])==output_reg) {
            const auto n=index(code[i.at+2]);
            if (bump && (n==3 || n==4 || n==8)) {
                const auto semantic=n==8?6u:n-2;
                if (code[i.at+1]!=(0x80000005u|(semantic<<16)) || code[i.at+2]!=dst(output_reg,n,n==8?3:xyz)) return false;
                if (n==3) ++view; else if (n==4) ++normal; else ++scalar;
            }
        } else if (i.opcode!=def && i.count && kind(code[i.at+1])==output_reg && index(code[i.at+1])==1) {
            if (i.at!=point && i.at!=emissive && i.at!=alpha && i.at!=alpha+5) return false;
            ++colors;
        }
    }
    return colors==3 && (!bump || (scalar==1 && view==1 && normal==1));
}
bool xt_pixel_sites(const Word* code, const Structure& s, const XtPixel& p) noexcept {
    if (!xt_instruction_at(s,p.base_at) || !xt_instruction_at(s,p.final_rgb) || !xt_instruction_at(s,p.alpha)) return false;
    unsigned textures=0, colors=0, lights=0, palette=0, clamps=0, scalar=0, view=0, normal=0, carrier=0;
    for (const auto& i:s.instructions) {
        const auto at=static_cast<unsigned>(i.at);
        if (i.opcode==dcl) {
            if (p.bump && kind(code[at+2])==input) {
                const auto number=index(code[at+2]);
                if (number==2 || number==3 || number==7) {
                    const auto semantic=number==7?6u:number-1;
                    if (code[at+1]!=(0x80000005u|(semantic<<16)) || code[at+2]!=(dst(input,number,number==7?3:xyz)|pp)) return false;
                    if (number==2) ++view; else if (number==3) ++normal; else ++carrier;
                }
            }
            continue;
        }
        if (i.opcode==def || !i.count) continue;
        if (i.opcode==texld) {
            const auto stage=index(code[at+3]);
            if (stage>=(p.bump?7u:5u) || p.texture[stage]!=at || kind(code[at+3])!=10 ||
                index(code[at+1])!=p.texture_reg[stage] || kind(code[at+1])!=temp || mask(code[at+1])!=xyzw) return false;
            ++textures;
        }
        if (kind(code[at+1])==color_output) {
            if ((at!=p.final_rgb && at!=p.alpha) || index(code[at+1])!=0 || mask(code[at+1])!=(at==p.final_rgb?xyz:8)) return false;
            ++colors;
        }
        if (std::find(p.rgb.begin(),p.rgb.end(),at)!=p.rgb.end() &&
            (mask(code[at+1])!=xyz || (i.opcode!=mov && i.opcode!=add && i.opcode!=mul && i.opcode!=mad))) return false;
        for (const auto clamp:p.clamps) if (clamp==at) {
            if (code[at]!=((2u<<24)|mov) || mask(code[at+1])!=xyz || (code[at+1]&(pp|sat))!=(pp|sat) ||
                code[at+2]!=src(input,0)) return false;
            ++clamps;
        }
        for (unsigned n=2;n<=i.count;++n) {
            const auto value=code[at+n];
            if (kind(value)==constant && (index(value)==6 || index(value)==8)) {
                const auto found=std::find_if(p.lights.begin(),p.lights.end(),[&](const XtUse& u){return u.operand==at+n && u.value==index(value);});
                if (found==p.lights.end() || value!=src(constant,index(value)) || mask(code[at+1])!=xyz) return false;
                ++lights;
            }
            const unsigned first=p.bump?17:14;
            if (kind(value)==constant && index(value)>=first && index(value)<first+5) {
                const auto found=std::find_if(p.palette.begin(),p.palette.end(),[&](const XtUse& u){return u.operand==at+n && u.value==index(value);});
                if (found==p.palette.end() || value!=src(constant,index(value)) || mask(code[at+1])!=xyz) return false;
                ++palette;
            }
            if (kind(value)==input && index(value)==(p.bump?7u:5u)) {
                const auto found=std::find_if(p.scalar.begin(),p.scalar.end(),[&](const XtUse& u){return u.operand==at+n;});
                if (found==p.scalar.end() || value!=lane(input,p.bump?7:5,found->value) || mask(code[at+1])!=xyz) return false;
                ++scalar;
            }
            // COLOR0 RGB may only enter through the two explicit clamp MOVs;
            // its sole retained read is the original output-alpha multiply.
            if (kind(value)==input && index(value)==0 && value!=lane(input,0,3) &&
                std::find(p.clamps.begin(),p.clamps.end(),at)==p.clamps.end()) return false;
        }
    }
    for (const auto at:p.rgb) if (at && !xt_instruction_at(s,at)) return false;
    return textures==(p.bump?7u:5u) && colors==2 && lights==4 && palette==5 && clamps==2 && scalar==2 &&
        (!p.bump || (view==1 && normal==1 && carrier==1));
}
// Constant fill for the XT law (docs/architecture/fill-light.md). The XT
// programs multiply the lobe sum by a branch-dependent albedo, so the single
// MAD goes in ahead of that two-armed branch, where one lobe sum is live for
// both arms. Every step is anchored on the reviewed COLOR0 clamp sites; an
// ambiguous branch, lobe sum or albedo multiply refuses (no fill).
bool xt_fill_site(const Word* code, const Structure& s, const XtPixel& p,
                  unsigned temporaries, unsigned& sum, unsigned& branch_at) noexcept {
    const unsigned first=p.clamps[0], second=p.clamps[1];
    if (!first || !second || first>=second) return false;
    std::array<unsigned,8> open{}; unsigned depth=0, opener=0, openers=0, alternative=0;
    for (const auto& i:s.instructions) {
        const auto at=static_cast<unsigned>(i.at);
        if (i.opcode==40 || i.opcode==41) { if (depth>=open.size()) return false; open[depth++]=at; }
        else if (i.opcode==43) { if (!depth) return false; --depth; }
        else if (i.opcode==42) {
            if (!depth) return false;
            if (at>first && at<second && open[depth-1]<first) { opener=open[depth-1]; alternative=at; ++openers; }
        }
    }
    if (depth || openers!=1) return false;
    unsigned resolved=0;
    for (unsigned arm=0; arm<2; ++arm) {
        const unsigned clamp=p.clamps[arm];
        if (kind(code[clamp+1])!=temp) return false;
        const unsigned carrier=index(code[clamp+1]);
        unsigned lobe=0, add_at=0, destination=0, found=0;
        for (const auto& i:s.instructions) {
            const auto at=static_cast<unsigned>(i.at);
            if (at<=clamp || i.opcode!=add || i.count!=3 || found) continue;
            if (kind(code[at+1])!=temp || mask(code[at+1])!=xyz) continue;
            const Word left=code[at+2], right=code[at+3];
            if (left!=src(temp,carrier) && right!=src(temp,carrier)) continue;
            const Word other=left==src(temp,carrier) ? right : left;
            if (kind(other)!=temp || other!=src(temp,index(other)) || index(other)==carrier) return false;
            lobe=index(other); add_at=at; destination=index(code[at+1]); found=1;
        }
        // The sum must survive from the branch (first arm) or from the ELSE
        // (second arm) to its use; the other arm never runs alongside it.
        if (!found || lobe>=temporaries || add_at<=(arm?alternative:opener) ||
            (!arm && add_at>=alternative) || !no_write(code,s,lobe,xyz,arm?alternative:opener,add_at)) return false;
        // The arm must finish with the albedo multiply of that sum.
        unsigned products=0;
        for (const auto& i:s.instructions) {
            const auto at=static_cast<unsigned>(i.at);
            if (at<=add_at || i.opcode!=mul || i.count!=3 || kind(code[at+1])!=temp || mask(code[at+1])!=xyz) continue;
            if (code[at+2]==src(temp,destination) || code[at+3]==src(temp,destination)) { ++products; break; }
        }
        if (!products) return false;
        if (arm && lobe!=sum) return false;
        sum=lobe; ++resolved;
    }
    if (resolved!=2 || !opener) return false;
    branch_at=opener;
    return true;
}
void xt_default_geometry(Words& out) {
    // These are owned vertex inputs/temporaries, never absent interstage values.
    // Preserve the native geometry and alpha; extra temporaries die at END.
    emit(out,mov,{dst(output_reg,2,12),src(input,1)});
    emit(out,36,{dst(temp,10),src(temp,0)});
    emit(out,36,{dst(temp,11),src(temp,2)});
    emit(out,8,{dst(temp,12,1),src(temp,10,identity,1),src(temp,11)});
    emit(out,add,{dst(temp,12,1),lane(temp,12,0),lane(temp,12,0)});
    emit(out,mad,{dst(temp,12),src(temp,11),src(temp,12,0,1),src(temp,10,identity,1)});
    emit(out,abs_op,{dst(output_reg,8),src(temp,12)});
    emit(out,8,{dst(temp,12,1)|sat,src(temp,10),src(temp,11)});
    emit(out,mul,{dst(temp,12,2),lane(temp,12,0),lane(temp,12,0)});
    emit(out,mul,{dst(temp,12,4),lane(temp,12,1),lane(temp,12,1)});
    emit(out,mul,{dst(temp,12,8),lane(temp,12,2),lane(temp,12,2)});
    emit(out,mul,{dst(output_reg,9,2),lane(temp,12,3),lane(temp,12,2)});
    emit(out,add,{dst(temp,12,1),lane(constant,244,0),src(temp,12,0,1)});
    emit(out,mul,{dst(output_reg,9,1),lane(temp,12,0),lane(temp,12,0)});
}
LinearMaterialResult xt_transform(const Word* original, std::size_t words, const LinearMaterialConfig& config,
    Words& output, bool current_depth, bool vertex, const XtPixel& p, bool linear, bool* fill_applied = nullptr) noexcept {
    if (fill_applied) *fill_applied=false;
    if (!original || words<2) return LinearMaterialResult::InvalidInput;
    if (!linear_material_config_valid(config)) return LinearMaterialResult::InvalidConfig;
    const auto expected=vertex?(p.bump?xt_bump_vs:xt_default_vs):p.hash;
    if (words!=(vertex?(p.bump?768u:526u):p.words) || material_motion_fingerprint(original,words)!=expected || (!linear && p.bump))
        return LinearMaterialResult::UnsupportedShader;
    const auto* row=material_motion_profile(p.bump?xt_bump_vs:xt_default_vs,p.hash);
    const auto abi=xt_abi(p.bump);
    if (!row || row->transformation_class!=(p.damage?MotionOutputClass::BoundedDamageBranches:MotionOutputClass::RelocatedRegistersWithBranches) ||
        row->vertex_output_register!=abi.vertex_motion || row->pixel_input_register!=abi.pixel_motion ||
        row->texcoord_index!=abi.motion_texcoord || row->vertex_depth_output_register!=abi.vertex_depth ||
        row->pixel_depth_input_register!=abi.pixel_depth || row->depth_texcoord_index!=abi.depth_texcoord ||
        row->pixel_temporary_base!=(p.terra?7u:6u) || row->vertex_constant_base!=252 || row->pixel_constant_base!=216 ||
        row->pixel_output_register!=1 || !row->depth_output) return LinearMaterialResult::ProfileMismatch;
    try {
        Structure s;
        if (!structure(original,words,vertex,s,true,abi,vertex?7u:p.terra?7u:6u,false,p.bump,true) ||
            !(vertex?xt_vertex_sites(original,s,p.bump):xt_pixel_sites(original,s,p))) return LinearMaterialResult::ProfileMismatch;
        // New policy DEFs have no native ownership; prohibit any original use.
        for (const auto& i:s.instructions) if (i.opcode!=dcl) {
            if (i.opcode==def) { if (index(original[i.at+1])==(vertex?244u:210u)) return LinearMaterialResult::ProfileMismatch; continue; }
            for (unsigned n=1;n<=i.count;++n) if (kind(original[i.at+n])==constant && index(original[i.at+n])==(vertex?244u:210u)) return LinearMaterialResult::ProfileMismatch;
        }
        unsigned fill_sum=0, fill_at=0; bool fill=false;
        if (!vertex && linear && config.fill>0.0f)
            fill=xt_fill_site(original,s,p,p.terra?7u:6u,fill_sum,fill_at) && fill_constant_free(original,s);
        Words motion;
        const auto mr=vertex?material_motion_vertex_variant_for(*row,original,words,motion,current_depth):material_motion_pixel_variant_for(*row,original,words,motion,current_depth);
        if (mr!=MaterialMotionResult::Applied) return mr==MaterialMotionResult::AllocationFailure?LinearMaterialResult::AllocationFailure:LinearMaterialResult::ProfileMismatch;
        const bool depth=vertex?material_motion_vertex_exports_depth(*row,current_depth):material_motion_pixel_writes_depth(*row,current_depth);
        std::vector<Insertion> insertions;
        if (!motion_insertions(original,words,motion,*row,vertex,depth,s,insertions)) return LinearMaterialResult::ProfileMismatch;
        Words combined; combined.reserve(motion.size()+1000); combined.push_back(original[0]);
        std::size_t inserted=0;
        for (std::size_t at=1;at<words;) {
            while (inserted<insertions.size() && insertions[inserted].at==at) {
                const auto& i=insertions[inserted++]; combined.insert(combined.end(),motion.begin()+i.begin,motion.begin()+i.end);
            }
            if (at==s.first_declaration) {
                if (linear) definitions(combined,vertex,config);
                if (fill) fill_definition(combined,config);
                if (!p.bump) emit(combined,def,{dst(constant,vertex?244:210,xyzw),bits(vertex?1.f:0.1f),bits(vertex?0.f:0.9f),0,0});
            }
            if (at==(vertex?row->vertex_declaration_insert_dword:row->pixel_declaration_insert_dword)) {
                if (vertex && !p.bump) {
                    emit(combined,dcl,{0x80050005u,dst(output_reg,8)});
                    emit(combined,dcl,{0x80060005u,dst(output_reg,9,3)});
                }
                if (linear) {
                    emit(combined,dcl,{0x8001000au,dst(vertex?output_reg:input,vertex?abi.vertex_rgb:abi.pixel_rgb)});
                    if (!vertex) {
                        transfer(combined,false,12,{src(constant,6)},false,10);gain(combined,false,12,0);
                        transfer(combined,false,13,{src(constant,8)},false,10);gain(combined,false,13,0);
                    }
                }
            }
            if (original[at]==end_token) {
                if (vertex && !p.bump) xt_default_geometry(combined);
                combined.push_back(end_token);++at;continue;
            }
            const auto op=original[at]&0xffff, n=length(original[at]);
            if (!vertex && linear && at==p.base_at) transfer(combined,false,p.base_reg,{src(temp,p.base_reg)},false,10);
            if (!vertex) {
                for (const auto& u:p.palette) if (linear && u.operand>at && u.operand<=at+n)
                    transfer(combined,false,14,{src(constant,u.value)},false,10);
                for (const auto& u:p.scalar) if (!p.bump && u.value==0 && u.operand>at && u.operand<=at+n) {
                    emit(combined,mul,{dst(temp,15,1),lane(input,5,0),lane(constant,11,0)});
                    emit(combined,mad,{dst(temp,15,1),lane(temp,15,0),lane(constant,210,1),lane(constant,210,0)});
                }
                if (linear && p.terra && at==p.final_rgb)
                    transfer(combined,false,14,{src(temp,p.texture_reg[p.bump?5:4])},false,10);
            }
            const unsigned point=p.bump?593:428, emissive=p.bump?608:443;
            if (vertex && linear && at==point) {
                transfer(combined,true,7,{original[at+3],original[at+4]});gain(combined,true,7,0);
            }
            if (vertex && linear && at==emissive) {
                // Native g_MatEmissiveColor includes strength; no power curve.
                sanitize(combined,true,7,{original[at+3]});
                emit(combined,abs_op,{dst(temp,7),src(temp,7)});gain(combined,true,7,1);
            }
            if (p.bump && op==dcl && kind(original[at+2])==(vertex?output_reg:input) && index(original[at+2])==(vertex?8u:7u)) {at+=n+1;continue;}
            if (fill && at==fill_at) fill_instruction(combined,fill_sum);
            const auto copy=combined.size();combined.insert(combined.end(),original+at,original+at+n+1);
            if (op==dcl && kind(original[at+2])==(vertex?output_reg:input)) {
                const auto number=index(original[at+2]);
                if (p.bump && (number==(vertex?3u:2u) || number==(vertex?4u:3u))) combined[copy+2]|=8u<<16;
                if (vertex && !p.bump && number==2) combined[copy+2]|=12u<<16;
            }
            if (vertex) {
                if (p.bump && at==733) combined[copy+1]=dst(output_reg,4,8);
                if (p.bump && at==743) combined[copy+1]=dst(output_reg,3,8);
                if (linear && at==point) {combined[copy+3]=src(temp,7);combined.erase(combined.begin()+copy+4);combined[copy]=(3u<<24)|mul;}
                if (linear && at==emissive) {combined[copy+1]=dst(output_reg,abi.vertex_rgb);combined[copy+3]=src(temp,7);}
            } else if (op!=0xfffe) {
                for (const auto& u:p.scalar) if (u.operand>at && u.operand<=at+n)
                    combined[copy+u.operand-at]=p.bump?lane(input,2+u.value,3):u.value==0?lane(temp,15,0):original[u.operand];
                if (linear) {
                    if (std::find(p.rgb.begin(),p.rgb.end(),at)!=p.rgb.end()) combined[copy+1]&=~pp;
                    if (std::find(p.clamps.begin(),p.clamps.end(),at)!=p.clamps.end()) {combined[copy+1]&=~sat;combined[copy+2]=src(input,abi.pixel_rgb);}
                    for (const auto& u:p.lights) if (u.operand>at && u.operand<=at+n) combined[copy+u.operand-at]=src(temp,u.value==6?12:13);
                    for (const auto& u:p.palette) if (u.operand>at && u.operand<=at+n) combined[copy+u.operand-at]=src(temp,14);
                    const auto cube=p.bump?4u:3u, lm=p.bump?3u:2u;
                    if (at==p.texture[cube]) transfer(combined,false,p.texture_reg[cube],{src(temp,p.texture_reg[cube])},false,10);
                    if (at==p.texture[lm]) {transfer(combined,false,p.texture_reg[lm],{src(temp,p.texture_reg[lm])},false,10);gain(combined,false,p.texture_reg[lm],2);}
                    if (at==p.final_rgb) {
                        if (p.terra) {
                            unsigned replaced=0;
                            for (unsigned q=2;q<=n;++q) if (original[at+q]==src(temp,p.texture_reg[p.bump?5:4])) {combined[copy+q]=src(temp,14);++replaced;}
                            if (replaced!=1) return LinearMaterialResult::ProfileMismatch;
                        }
                        combined[copy+1]=dst(temp,11);transfer(combined,false,11,{src(temp,11)},true,10);
                        emit(combined,mov,{dst(color_output,0),src(temp,11)});
                    }
                }
            }
            at+=n+1;
        }
        if (inserted!=insertions.size()) return LinearMaterialResult::ProfileMismatch;
        Structure final;
        if (!structure(combined.data(),combined.size(),vertex,final,false,abi,vertex?7u:p.terra?7u:6u,false,false,true)) return LinearMaterialResult::ResourceLimit;
        output.swap(combined);
        if (fill_applied) *fill_applied=fill;
        return LinearMaterialResult::Applied;
    } catch (...) {return LinearMaterialResult::AllocationFailure;}
}
