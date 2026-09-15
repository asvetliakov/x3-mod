// Create-time ordinary PS contribution propagation. Included only by
// linear_material.cpp; all instruction sites come from its hash-bound original
// profiles and the actual converted edit map. No raw game shader bytes here.
struct SunSite { std::size_t original, converted; };
struct SunEdit { std::size_t at; Words words; };
constexpr unsigned sun_constant=221, sun_base=16, sun_final=23;
using SunState=std::array<unsigned,7>;
struct SunBranch { SunState entry{}, first{}; bool alternative=false; };

bool sun_resources_free(const Words& code, const Structure& s) noexcept {
    for (const auto& i:s.instructions) {
        // DEF payloads are literals, not register operands. DCL's semantic is
        // likewise not a register. Check both declarations and definitions.
        const unsigned first=i.opcode==dcl?2:1, last=i.opcode==def?1:i.count;
        for (unsigned n=first;n<=last;++n) {
            const auto t=kind(code[i.at+n]), r=index(code[i.at+n]);
            if ((t==temp && r>=sun_base && r<=sun_final) ||
                (t==constant && r==sun_constant)) return false;
        }
    }
    return true;
}
// Does the original source read a tracked RGB channel? Source swizzles select
// lanes according to the original destination, including scalar/control reads.
bool sun_read(Word source, unsigned consumed, const SunState& state) noexcept {
    if (kind(source)!=temp || index(source)>=state.size()) return false;
    const unsigned sw=(source>>16)&255;
    for (unsigned c=0;c<4;++c) if ((consumed&(1u<<c)) &&
        (state[index(source)]&(1u<<((sw>>(2*c))&3)))) return true;
    return false;
}
void sun_reduction(Words& out) {
    // ps_3_0 has one distinct c# read port per instruction. Stage zero in
    // an otherwise unused lane before CMP also reads c221.
    emit(out,mov,{dst(temp,17,8),lane(constant,212,1)});
    const Word zero=lane(temp,17,3), epsilon=lane(constant,sun_constant,3);
    const Word negative_epsilon=epsilon|(1u<<24);
    // Saturating the existing 2.2 literal produces exact 1, then negate.
    // RCP(epsilon)*epsilon is not specified to be exact on native ps_3_0.
    emit(out,mov,{dst(temp,16,8)|sat,lane(constant,212,0)});
    emit(out,mov,{dst(temp,16,8),src(temp,16,255,1)});
    // Domain: 0 <= S <= L <= 65504 componentwise. Ordered CMP sends NaN to
    // the failure arm; subtraction catches infinities too. Never sanitize S/L.
    emit(out,cmp,{dst(temp,16),src(temp,23),zero,negative_epsilon});
    emit(out,add,{dst(temp,17),src(temp,11),src(temp,23,identity,1)});
    emit(out,cmp,{dst(temp,17),src(temp,17),src(temp,16),negative_epsilon});
    emit(out,add,{dst(temp,16),lane(constant,212,2),src(temp,11,identity,1)});
    emit(out,cmp,{dst(temp,16),src(temp,16),src(temp,17),negative_epsilon});
    emit(out,min_op,{dst(temp,16,1),lane(temp,16,0),lane(temp,16,1)});
    emit(out,min_op,{dst(temp,16,1),lane(temp,16,0),lane(temp,16,2)});
    emit(out,8,{dst(temp,17,1),src(temp,23),src(constant,sun_constant)});
    emit(out,8,{dst(temp,17,2),src(temp,11),src(constant,sun_constant)});
    emit(out,max_op,{dst(temp,17,4),lane(temp,17,1),epsilon});
    emit(out,6,{dst(temp,17,4),lane(temp,17,2)});
    emit(out,mul,{dst(temp,23,8)|sat,lane(temp,17,0),lane(temp,17,2)});
    emit(out,add,{dst(temp,17,4),lane(temp,17,1),negative_epsilon});
    emit(out,cmp,{dst(temp,23,8),lane(temp,17,2),lane(temp,23,3),lane(temp,16,3)});
    // Exact black is admitted; a nonzero sub-epsilon total is not. Check RGB
    // itself so DP3 underflow cannot misclassify a tiny nonzero total as black.
    emit(out,max_op,{dst(temp,17,4),lane(temp,11,0),lane(temp,11,1)});
    emit(out,max_op,{dst(temp,17,4),lane(temp,17,2),lane(temp,11,2)});
    emit(out,cmp,{dst(temp,23,8),src(temp,17,0xaa,1),zero,lane(temp,23,3)});
    emit(out,cmp,{dst(temp,23,8),lane(temp,16,0),lane(temp,23,3),lane(temp,16,3)});
}
bool sun_plan(const Word* original, const Structure& source, const Words& combined,
    const std::vector<SunSite>& sites, const std::array<unsigned,2>& seeds,
    unsigned final_rgb, std::vector<SunEdit>& edits) {
    SunState state{}; std::vector<SunBranch> branches;
    unsigned seeded=0, expected=0, finals=0; bool initialized=false;
    for (const auto seed:seeds) if (seed) ++expected;
    if (!expected) return false;
    std::size_t site_index=0;
    for (const auto& i:source.instructions) {
        if (i.opcode==dcl || i.opcode==def) continue;
        while (site_index<sites.size() && sites[site_index].original<i.at) ++site_index;
        if (site_index==sites.size() || sites[site_index].original!=i.at) return false;
        const auto at=sites[site_index].converted;
        if (at+i.count>=combined.size() || (combined[at]&0xffff)!=i.opcode || length(combined[at])!=i.count) return false;
        if (i.opcode==dcl || i.opcode==def) continue;
        SunEdit edit{at,{}};
        if (!initialized) {
            for (unsigned r=16;r<=23;++r) emit(edit.words,mov,{dst(temp,r),lane(constant,212,1)});
            initialized=true;
        }
        unsigned count=0,cost=0; bool destination=false;
        if (!body_shape(i.opcode,count,cost,destination)) return false;
        if (!destination) {
            for (unsigned q=1;q<=i.count;++q) if (sun_read(original[i.at+q],xyzw,state)) return false;
            if (i.opcode==40 || i.opcode==41) {
                if (branches.size()>=8) return false;
                branches.push_back({state,{},false});
            } else if (i.opcode==42) {
                if (branches.empty() || branches.back().alternative) return false;
                auto& b=branches.back(); b.first=state; b.alternative=true; state=b.entry;
            } else if (i.opcode==43) {
                if (branches.empty()) return false;
                const auto b=branches.back(); branches.pop_back();
                for (unsigned r=0;r<state.size();++r) state[r]|=(b.alternative?b.first:b.entry)[r];
            } else return false;
            if (!edit.words.empty()) edits.push_back(std::move(edit));
            continue;
        }
        const Word od=original[i.at+1], cd=combined[at+1];
        const unsigned lanes=mask(od), target=index(od);
        const bool final=i.at==final_rgb;
        // DP3/DP4/texture and nonlinear reads are deliberately conservative:
        // any tracked lane they could consume refuses extraction.
        const unsigned consumed=(i.opcode==mov || i.opcode==add || i.opcode==mul || i.opcode==mad)?lanes:xyzw;
        std::array<bool,3> dep{}; bool active=false;
        for (unsigned q=2;q<=i.count;++q) {
            const bool reads=sun_read(original[i.at+q],consumed,state);
            if (reads && q>=5) return false;
            if (q<5) dep[q-2]=reads;
            active|=reads;
        }
        unsigned seed_operand=0;
        for (const auto seed:seeds) if (seed>i.at && seed<=i.at+i.count) {
            if (seed_operand) return false;
            seed_operand=static_cast<unsigned>(seed-i.at); ++seeded;
        }
        active|=seed_operand!=0;
        if (active) {
            if (lanes!=xyz || (cd&(pp|sat)) ||
                (!final && (kind(od)!=temp || target>=state.size() || cd!=dst(temp,target))) ||
                (final && (cd!=dst(temp,11) || !branches.empty())) ||
                (i.opcode!=mov && i.opcode!=add && i.opcode!=mul && i.opcode!=mad)) return false;
            std::array<Word,3> parallel{lane(constant,212,1),lane(constant,212,1),lane(constant,212,1)};
            for (unsigned q=2;q<=i.count;++q) if (dep[q-2]) {
                const Word operand=original[i.at+q];
                if (operand!=src(temp,index(operand)) || combined[at+q]!=operand || state[index(operand)]!=xyz) return false;
                parallel[q-2]=src(temp,sun_base+index(operand));
            }
            const Word output=dst(temp,final?sun_final:sun_base+target);
            if (seed_operand) {
                if (i.opcode!=mad || (seed_operand!=2 && seed_operand!=3) || dep[0] || dep[1] ||
                    combined[at+seed_operand]!=src(temp,12)) return false;
                if (dep[2]) emit(edit.words,mad,{output,combined[at+2],combined[at+3],parallel[2]});
                else emit(edit.words,mul,{output,combined[at+2],combined[at+3]});
            } else if (i.opcode==mov) emit(edit.words,mov,{output,parallel[0]});
            else if (i.opcode==add) {
                if (dep[0] && dep[1]) emit(edit.words,add,{output,parallel[0],parallel[1]});
                else emit(edit.words,mov,{output,parallel[dep[0]?0:1]});
            } else {
                if (dep[0] && dep[1]) return false; // nonlinear in sun
                if (dep[0] || dep[1]) {
                    const Word a=dep[0]?parallel[0]:combined[at+2], b=dep[1]?parallel[1]:combined[at+3];
                    if (i.opcode==mad && dep[2]) emit(edit.words,mad,{output,a,b,parallel[2]});
                    else emit(edit.words,mul,{output,a,b});
                } else emit(edit.words,mov,{output,parallel[2]});
            }
            if (!final) state[target]=xyz;
        } else if (kind(od)==temp && target<state.size()) {
            // Keep the zero invariant on overwritten channels. This also makes
            // an OR merge at ENDIF valid when only one arm carries sun.
            const unsigned killed=state[target]&lanes;
            if (killed) emit(edit.words,mov,{dst(temp,sun_base+target,killed),lane(constant,212,1)});
            state[target]&=~lanes;
        }
        if (!edit.words.empty()) edits.push_back(std::move(edit));
        if (final) {
            if (!active || ++finals!=1) return false;
            SunEdit reduce{at+i.count+1,{}}; sun_reduction(reduce.words); edits.push_back(std::move(reduce));
        }
    }
    return initialized && branches.empty() && seeded==expected && finals==1;
}
bool sun_share_variant(const Word* original, const Structure& source, Words& combined,
    const Structure& converted, const std::vector<SunSite>& sites,
    const std::array<unsigned,2>& seeds, unsigned final_rgb, bool& extracted) {
    extracted=false;
    if (!sun_resources_free(combined,converted)) return false;
    std::vector<SunEdit> edits;
    const bool proved=sun_plan(original,source,combined,sites,seeds,final_rgb,edits);
    if (!proved) edits.clear();
    Words result; result.reserve(combined.size()+512);
    result.push_back(combined[0]);
    emit(result,def,{dst(constant,sun_constant,xyzw),bits(0.2126f),bits(0.7152f),bits(0.0722f),bits(0x1p-20f)});
    std::size_t copied=1;
    for (const auto& edit:edits) {
        if (edit.at<copied || edit.at>=combined.size()) return false;
        result.insert(result.end(),combined.begin()+copied,combined.begin()+edit.at);
        result.insert(result.end(),edit.words.begin(),edit.words.end()); copied=edit.at;
    }
    result.insert(result.end(),combined.begin()+copied,combined.end()-1);
    if (proved) emit(result,mov,{dst(color_output,2,2),lane(temp,sun_final,3)});
    else {
        emit(result,mov,{dst(temp,sun_final,8)|sat,lane(constant,212,0)});
        emit(result,mov,{dst(color_output,2,2),src(temp,sun_final,255,1)});
    }
    result.push_back(end_token); combined.swap(result); extracted=proved;
    return true;
}
