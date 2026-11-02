#!/bin/sh
# FL run 21 baseline of the proxy culls before the edition gate (log local, never tracked; read with grep only).
# Measured 2026-11-02 on /home/asvetl/x3m-logs/fl-run21-overlay.log:
#   rows 133 rows_with_draws 0 rows_with_culled 0 no_scope_total 2463523
#   occlusion_cull_device/occlusion_cull_session rows: 0
log=${1:-/home/asvetl/x3m-logs/fl-run21-overlay.log}
grep "cull_small_props_frame" "$log" | awk '{n++; for(i=1;i<=NF;i++){split($i,a,"="); if(a[1]=="draws"&&a[2]!=0)d++; if(a[1]=="culled"&&a[2]!=0)c++; if(a[1]=="no_scope")s+=a[2]}} END{print "rows",n,"rows_with_draws",d+0,"rows_with_culled",c+0,"no_scope_total",s}'
echo "occlusion_cull_device/occlusion_cull_session rows: $(grep -c 'occlusion_cull_device\|occlusion_cull_session' "$log")"
