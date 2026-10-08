# Run 137 A triage (run16 = Run137 cull on, run15 = Run136 dxvk.conf async ON, run14 = Run135 sync), DXVK. Outputs beside each script.
# python3 ab_cull.py /tmp/x3-bottleX3-run16 /tmp/x3-bottleX3-run15 /tmp/x3-bottleX3-run14 > ab_cull_out.txt          # (1)(2) totals, cull fields, phase medians, draw/tested bins
# python3 cost_model.py /tmp/x3-bottleX3-run16 /tmp/x3-bottleX3-run15 /tmp/x3-bottleX3-run14 > cost_model_out.txt    # (2)(3)(4) matched-draws, fits, window view_submit vs tested
# python3 midhitch_and_within_bin.py /tmp/x3-bottleX3-run16 /tmp/x3-bottleX3-run15 > midhitch_and_within_bin_out.txt  # 33-50 ms clusters, within-bin conditioning
# python3 windows_by_draws.py /tmp/x3-bottleX3-run1{6,5,4} > windows_by_draws_out.txt                                  # close-view view_submit at matched draws
# python3 tail_by_draws.py /tmp/x3-bottleX3-run1{6,5,4} > tail_by_draws_out.txt                                        # 20-50 ms share per draw / tested bin
# python3 ../run133-dxvk-triage/us_fields.py <log> (run16, run15) > us_fields_run16_15.txt
# python3 ../run135-dxvk-triage/hitches.py <log16> 33 > hitches33_run16.txt ; hitches.py|head -1 + slow_phase.py (16,15) > hitches_slow_run16_15.txt
# failure rows: grep -aE '^[a-z_]*(_failed|_refused)[ _]|failed=[1-9]|unavailable=' $S | awk '{print $1}' | sort | uniq -c > failed_kinds_run1{5,6}.txt
# stderr: grep -aiE 'err|warn|mvk|GStreamer|DXVK' launcher-stderr.log | strip [ts] | digits/hex->N | sort | uniq -c > stderr_kinds_run1{5,6}.txt
# dxvk config: grep -a -i dxvk launcher-stderr.log | grep -v QueryInterface | sort | uniq -c  -> run15 only has 'Found config file: dxvk.conf', 'dxvk.enableAsync = True', 'Using 12 async compiler threads'
