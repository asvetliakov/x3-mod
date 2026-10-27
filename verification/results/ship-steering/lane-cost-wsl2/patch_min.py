# Applied 2026-10-27: the lane-cost measurement: each frame's best of the 3 deterministic reps, then the third-worst of those.
# Usage: patch_min.py <in.cpp> <out.cpp>
import sys
from pathlib import Path
s = Path(sys.argv[1]).read_text()

def sub(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)

sub('    double frame_us_max = 0, frame_us_sum = 0, frame_us_top[3] = {0, 0, 0}; // (the three worst frames, worst first)\n',
    '    double frame_us_max = 0, frame_us_sum = 0, frame_us_top[3] = {0, 0, 0}; // (the three worst frames, worst first)\n'
    '    std::vector<double> frame_us; // every frame\'s block in frame order (the bench\'s per-frame best of its reps)\n')
sub('            res.frame_us_sum += us;\n', '            res.frame_us_sum += us;\n            res.frame_us.push_back(us);\n')
sub('            double mean[2] = {0, 0}, worst[2] = {0, 0}, third[2] = {0, 0};\n',
    '            double mean[2] = {0, 0}, worst[2] = {0, 0}, third[2] = {0, 0}, best_third[2] = {0, 0};\n'
    '            bool same_frames[2] = {true, true};\n')
sub('            for (int lane = 1; lane >= 0; --lane)\n                for (int rep = 0; rep < 3; ++rep) {\n'
    '                    const lanec::CResult r = lanec::run_c(*cs.su, tf == 1 ? 60 : 30, tf, kSeeds[0], lane != 0);\n',
    '            for (int lane = 1; lane >= 0; --lane) {\n'
    '                std::vector<double> best; // (the reps fly identical frames: host noise hits a random frame per rep, a slow frame every rep)\n'
    '                for (int rep = 0; rep < 3; ++rep) {\n'
    '                    const lanec::CResult r = lanec::run_c(*cs.su, tf == 1 ? 60 : 30, tf, kSeeds[0], lane != 0);\n'
    '                    if (rep == 0) best = r.frame_us;\n'
    '                    else if (best.size() != r.frame_us.size()) same_frames[lane] = false;\n'
    '                    else for (size_t i = 0; i < best.size(); ++i) best[i] = std::fmin(best[i], r.frame_us[i]);\n')
sub('                    members = r.departures;\n                }\n',
    '                    members = r.departures;\n                }\n'
    '                if (best.size() >= 3) {\n'
    '                    std::nth_element(best.begin(), best.begin() + 2, best.end(), std::greater<double>());\n'
    '                    best_third[lane] = best[2];\n'
    '                }\n'
    '            }\n')
sub('"key off %.1f us mean, %.0f us worst, %.0f us third (%u decisions, %u launched); worst of 3 runs; ceiling 1000 us\\n", cs.name,\n'
    '                        tf, mean[1], worst[1], third[1], dec[1], launched[1], members, mean[0], worst[0], third[0], dec[0], launched[0]);\n',
    '"key off %.1f us mean, %.0f us worst, %.0f us third (%u decisions, %u launched); worst of 3 runs; third-worst of each frame\'s best of 3: "\n'
    '                        "lane %.0f us, key off %.0f us; ceiling 1000 us\\n", cs.name, tf, mean[1], worst[1], third[1], dec[1], launched[1], members,\n'
    '                        mean[0], worst[0], third[0], dec[0], launched[0], best_third[1], best_third[0]);\n')
sub('            // (the check reads each run\'s third-worst frame: a single frame of host scheduling noise reached 1.3-1.5 ms in repeated runs)\n'
    '            std::snprintf(why, sizeof why, "lane cost: the %s frame\'s third-worst controller block %.0f us under the 1 ms ceiling (tf %d)", cs.name,\n'
    '                          third[1], tf);\n'
    '            check(third[1] < 1000.0, why);\n',
    '            // (the check reads the third-worst of each frame\'s best of the 3 identical reps: single frames of host scheduling noise reached\n'
    '            // 1.3-4.6 ms on WSL2, alone and under load, landing on different frames in identical runs, while a real slow frame is slow in every run)\n'
    '            std::snprintf(why, sizeof why, "lane cost: the %s frame\'s third-worst controller block (per-frame best of 3) %.0f us under the 1 ms "\n'
    '                          "ceiling (tf %d)", cs.name, best_third[1], tf);\n'
    '            check(same_frames[1] && best_third[1] < 1000.0, why);\n')
Path(sys.argv[2]).write_text(s)
print('ok')
