import re,sys
src=open('/home/asvetl/x3-mod/verification/probe/engine_plumes_fixture.cpp').read()
src=src.replace('#include "../../src/','#include "/home/asvetl/x3-mod/src/')
# dump profiles + built phase/seed
old="""        const auto env = profile(&envelope, flow1), p1 = profile(&flow_look, flow1), p2 = profile(&flow_look, flow2);"""
new=old+"""
        {
            rr::EnginePlumesFrame fb = frame_for(t, true, &r, 1, ep::Preset::standard, 10.f, 0.f, 0.f, &flow_look);
            fb.flow = flow1; const Built b1 = build_cpu(fb); fb.flow = flow2; const Built b2 = build_cpu(fb);
            std::printf("FLOWBUILT w=%u V=%.0f Z=%.3f nozzles=%u phase1=%.9g phase2=%.9g params=%08x local_w=%.6g cx=%.4f x_near=%d x_far=%d\\n", t.w, double(V), double(Z), b1.nozzles,
                double(b1.v[0].shape[3]), double(b2.v[0].shape[3]), unsigned(b1.v[0].params), double(b1.v[0].local[3]), double(cx), x_near, x_far);
            for (int vi = 0; vi < 4; ++vi) std::printf("FLOWVERT w=%u V=%.0f v=%d pos=%.6g,%.6g,%.6g local=%.6g,%.6g,%.6g,%.6g\\n", t.w, double(V), vi,
                double(b1.v[vi].position[0]), double(b1.v[vi].position[1]), double(b1.v[vi].position[2]), double(b1.v[vi].local[0]), double(b1.v[vi].local[1]), double(b1.v[vi].local[2]), double(b1.v[vi].local[3]));
            for (std::size_t i = 0; i < env.size(); ++i) std::printf("FLOWDUMP w=%u V=%.0f i=%u env=%.6g p1=%.6g p2=%.6g\\n", t.w, double(V), unsigned(i), env[i], p1[i], p2[i]);
        }"""
assert old in src; src=src.replace(old,new)
open('engine_plumes_fixture.cpp','w').write(src)
