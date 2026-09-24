#!/bin/sh
# run291-293: fog look override rows, march scale / jitter options, burst presence, sharpen route. usage: sh log_rows.sh
for r in 291 292 293; do f=$(ls /tmp/x3-bottleX3-run$r/session-*.log)
  echo "run$r look_rows=$(grep -c '^volumetric_fog_look_mode' $f) $(grep -m1 '^volumetric_fog_look_mode' $f | grep -o 'overrides=[0-9]*\|SHADOW_JITTER=[0-9]*' | tr '\n' ' ')" \
       "opts: $(grep -m1 '^proxy_options' $f | tr ' ' '\n' | grep -E 'FOG_MARCH_SCALE|FOG_LOOK_SHADOW_JITTER|TAA_DEBUG|TAA_SHARPEN' | tr '\n' ' ')" \
       "capture_armed=$(grep -c '^capture_armed' $f) capture_event=$(grep -c '^capture_event' $f) frame_end_capture1=$(grep -c '^frame_end.* capture=1 ' $f)" \
       "hdr_sharpened1=$(grep '^hdr_frame' $f | grep -c ' sharpened=1')/$(grep -c '^hdr_frame' $f) last_frame=$(grep '^frame_end' $f | tail -1 | grep -o 'frame=[0-9]*')"
done
