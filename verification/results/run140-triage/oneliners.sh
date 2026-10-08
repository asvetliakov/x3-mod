# Run 140 A triage one-liners (run22 = flight 1, run23 = flight 2)
F=$(ls /tmp/x3-bottleX3-run22/session-*.log)
# Q4 capture readbacks: result/hr/bytes per kind
for k in hdr_readback motion_output_readback motion_output_depth_readback shadow_replay_map_readback; do echo "$k: $(grep "^$k " $F | grep -oE '(result|hr|bytes)=[^ ]*' | sort | uniq -c | tr '\n' ' ')"; done
grep -c capture=1 $F; grep -c '^capture_event' $F
# Q6 session_end and stderr errors
for r in 21 22 23; do grep -m1 '^session_end' /tmp/x3-bottleX3-run$r/session-*.log; grep -ciE 'exception|winedbg|unhandled|err:' /tmp/x3-bottleX3-run$r/launcher-stderr.log; done
# Q5 game_phase_window totals (slow vs retained frames)
grep '^game_phase_window ' $(ls /tmp/x3-bottleX3-run23/session-*.log) | python3 -c "import sys,re;t=[dict(re.findall(r'(\w+)=(\S+)',l)) for l in sys.stdin];print(sum(int(x['frames']) for x in t),sum(int(x['slow_frames']) for x in t),sum(int(x['retained_frames']) for x in t))"
