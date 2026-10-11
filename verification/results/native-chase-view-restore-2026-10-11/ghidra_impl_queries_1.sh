#!/bin/sh
G=/tmp/claude-1000/-home-asvetl-x3-mod/e54ef5fd-8036-45b2-b638-60c6a2d87dd4/scratchpad/g2
cd /home/asvetl/x3-mod/.claude/worktrees/agent-a5ec96618d1caed68
H="JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 $HOME/ghidra_12.1.4_PUBLIC/support/analyzeHeadless $HOME/x3-ghidra-research X3Render -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis"
eval $H -postScript X3ListRange.java "$G/ranges.txt" 00410100:00410140 0049f570:0049f5a0 00404280:004042c0 004121e0:00412200 0049ea80:0049eab4 004a0880:004a08ab 0052f298:0052f2a2 0049f660:0049f680 > $G/log1.txt 2>&1
eval $H -postScript X3DecompileFunctions.java "$G/decomp.c" 0049f570 00404280 > $G/log2.txt 2>&1
echo DONE > $G/done
