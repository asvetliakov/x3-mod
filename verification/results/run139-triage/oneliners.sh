# Run 139 A triage (run20 = occlusion_cull engine, run21 = off), DXVK, build d48d2bab (main 8f03c306).
# python3 run139.py /tmp/x3-bottleX3-run2{0,1} > run139_out.txt          # Q1 scene-matched dt (scene = draws + engine_skipped_draws), per-window view_submit; Q2 engine fields; Q3 hold; Q4 >100 ms
# python3 ab_cull.py /tmp/x3-bottleX3-run2{0,1} > ab_cull_out.txt         # Run138 script reused: dt, hitches, batched cull fields (draws = post-skip app draws in run20)
# python3 windows_by_draws.py /tmp/x3-bottleX3-run2{0,1} > windows_by_draws_out.txt; python3 tail_by_draws.py ... > tail_by_draws_out.txt
# Q5: grep -aE '^[a-z_]*(_failed|_refused|_error|crash|abort)[ _]|failed=[1-9]|errors=[1-9]' $S | awk '{print $1}' | sort | uniq -c   -> same kinds/counts class in both runs; winedbg/unhandled 0; session_end exception=0 resets=0 both
