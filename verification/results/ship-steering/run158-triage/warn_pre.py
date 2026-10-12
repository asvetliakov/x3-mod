import sys,re,collections as C
caps={"0x8f436ce8","0x8f43e458","0x8f86fca8","0x8f43c4a8","0x8f88cbf0","0xa4c06c10","0x3a2c2c30","0x8f893d48"}
R=[];E=[];W={}
for l in open(sys.argv[1]):
  if l.startswith('ship_avoid_engine_warn '):
    d=dict(re.findall(r"(\w+)=(\S+)",l)); W[(d['frame'],d['obj'])]=d
  elif l.startswith('ship_avoid_engine_warn_pre'):
    d=dict(re.findall(r"(\w+)=([^ \n]+)",l.split(' entries_')[0])); d['e']=[x.split(':') for x in l.rstrip().split('=')[-1].split('|')]
    R.append(d); E+=[(d,f) for f in d['e']]
print("pre rows",len(R),"entries",len(E),"subjects",len({d["obj"] for d in R}),"subject order",dict(C.Counter(d["order"] for d in R)))
m=[d for d in R if (d['frame'],d['obj']) in W]; print("pre rows matching a warn row (same frame,obj)",len(m),"of warn rows",len(W))
for tag,S in (("all",E),("matched to a 0x12 warn",[(d,f) for d,f in E if (d['frame'],d['obj']) in W])):
  print("==",tag,"entries",len(S))
  print(" other class/code",dict(C.Counter((f[1],f[2]) for d,f in S)))
  print(" other order",dict(C.Counter(f[3] for d,f in S)),"leader!=0",sum(f[4]!="0x00000000" for d,f in S),"member!=0",sum(f[5]!="0x00000000" for d,f in S),"other==subject tgt",sum(f[0]==d["tgt"] for d,f in S))
  print(" e_verdict",dict(C.Counter(f[8] for d,f in S)))
  print(" subj_order/other class/verdict",C.Counter((d["order"],f[1],f[8]) for d,f in S).most_common(12))
for d,f in E:
  if d["obj"] in caps: print("  cap",d["frame"],d["obj"],d["order"],"warn" if (d['frame'],d['obj']) in W else "-",":".join(f))
print("=== pre rows matched to a warn row one frame later")
S=[(d,f) for d,f in E if (str(int(d['frame'])+1),d['obj']) in W]
print("matched pre rows",len({(d['frame'],d['obj']) for d,f in S}),"entries",len(S))
print(" other class/code",dict(C.Counter((f[1],f[2]) for d,f in S)))
print(" other order",dict(C.Counter(f[3] for d,f in S)),"leader!=0",sum(f[4]!="0x00000000" for d,f in S),"member!=0",sum(f[5]!="0x00000000" for d,f in S))
print(" e_verdict",dict(C.Counter(f[8] for d,f in S)))
print(" class/code/order/verdict",C.Counter((f[1],f[2],f[3],f[8]) for d,f in S).most_common())
# what was the subject's previous order: gate target?
print(" other is a gate (class 18):",sum(f[1]=='18' for d,f in S))
