"""OLS of shadow_replay_depth us on summed per-cascade draws (draws0..4) and on issues, per run."""
import re, statistics as s
LOGS = {"run253": "/tmp/x3-bottleX3-run253/session-20260922-232511-212.log",
        "run251": "/tmp/x3-bottleX3-run251/session-20260922-231055-216.log",
        "run250": "/tmp/x3-bottleX3-run250/session-20260922-230123-216.log"}
kv = re.compile(r"(\w+)=(\S+)")
def fit(p):
    xs=[a for a,b in p]; ys=[b for a,b in p]; mx,my=s.mean(xs),s.mean(ys)
    sl=sum((a-mx)*(b-my) for a,b in p)/sum((a-mx)**2 for a in xs)
    r=sum((a-mx)*(b-my) for a,b in p)/(sum((a-mx)**2 for a in xs)*sum((b-my)**2 for b in ys))**.5
    return "slope %.3f us  intercept %.1f  r %.2f  mean_x %.1f" % (sl, my-sl*mx, r, mx)
for name, path in LOGS.items():
    A=[];B=[]
    for line in open(path, errors="replace"):
        if not line.startswith("shadow_replay_depth "): continue
        d=dict(kv.findall(line)); u=float(d["us"])
        if u<=0: continue
        A.append((sum(int(d["draws%d"%i]) for i in range(5)),u)); B.append((int(d["issues"]),u))
    print(name, "n", len(A), "| us~sum(draws_c):", fit(A), "| us~issues:", fit(B))
