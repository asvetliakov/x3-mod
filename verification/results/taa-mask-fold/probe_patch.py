"""Probe of the ledger's "Mask fold" review items (C): patches a scratch copy of the temporal fixture (the scratch paths below)
so the camera-gate program can be replaced by the fold with its in-place 7x7 disabled (X3M_PROBE_NO_INPLACE=1) and the
half / full containment prints its output differences; probe_runs.sh runs it, probe_out.txt holds the rows. Not a fixture."""
from pathlib import Path
P=Path('/private/tmp/claude-501/-Users-asvetl-x3-mod/c01d92c1-4474-4383-946d-4fac988ae47e/scratchpad/probe_tree/verification/probe')
def edit(name,pairs):
    p=P/name;s=p.read_text()
    for old,new in pairs:
        assert s.count(old)==1,(name,old[:80])
        s=s.replace(old,new)
    p.write_text(s)
edit('temporal_thin_region_inc.h',[
("const DWORD* foldTestsProgram=nullptr;","const DWORD* foldTestsProgram=nullptr;const DWORD* probeCamera=nullptr; // PROBE: the camera program replaced"),
("""    if(on){check("thin configure",pass.configure_far());""","""    if(on){check("thin configure",pass.configure_far(nullptr,probeCamera));"""),
])
edit('temporal_box_half_inc.h',[
("""    const bool same=full.output==half.output&&full.age==half.age;print(scene,k,same);""","""    const bool same=full.output==half.output&&full.age==half.age;print(scene,k,same);
    {unsigned n=0;double worst=0,rel=0;for(std::size_t f=0;f<full.output.size();++f)for(std::size_t i=0;i<full.output[f].size();++i){const double a=full.output[f][i],b=half.output[f][i];if(a!=b){++n;worst=std::max(worst,std::fabs(a-b));rel=std::max(rel,std::fabs(a-b)/std::max(std::fabs(a),1e-6));}}
     std::printf("PROBE_OUTPUT_DIFF scene=%s differing=%u max_abs=%.8f max_rel=%.8f\\n",scene,n,worst,rel);}"""),
])
edit('temporal_pass_fixture.cpp',[
("""    const bool foldTimingOnly=argc==6&&std::strcmp(argv[5],"fold-timing")==0;""","""    const bool foldTimingOnly=argc==6&&std::strcmp(argv[5],"fold-timing")==0;const bool thinRegionOnly=argc==6&&std::strcmp(argv[5],"thin-region")==0;"""),
("""&&!thinSourceOnly&&!foldTimingOnly)||!window)""","""&&!thinSourceOnly&&!foldTimingOnly&&!thinRegionOnly)||!window)"""),
("""        else if(foldTimingOnly){""","""        else if(thinRegionOnly){thin_region_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS probe\\n");result=0;}
        else if(foldTimingOnly){"""),
("""foldTestsProgram=static_cast<DWORD*>(foldTests->GetBufferPointer());""","""foldTestsProgram=static_cast<DWORD*>(foldTests->GetBufferPointer());
        Com<ID3DXBuffer> probeCode;{char flag[4]{};if(GetEnvironmentVariableA("X3M_PROBE_NO_INPLACE",flag,4)==1){std::string v=resolveSource;const std::string from="else [branch] if (stabilise.b > stabilise.a) {";const auto at=v.find(from);if(at==std::string::npos)throw std::runtime_error("probe anchor");
            v.replace(at,from.size(),"else [branch] if (stabilise.b > stabilise.a && stabilise.b < -1) {");compile(compiler,"#define X3M_REGION_HOLD 1\\n"+v,"ps_3_0",&probeCode.p);probeCamera=static_cast<DWORD*>(probeCode->GetBufferPointer());std::puts("PROBE no in-place 7x7");}}"""),
])
print('ok')
