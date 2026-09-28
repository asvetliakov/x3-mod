"""Minimal SM1-3 token disassembler (opcode names, register types, CTAB names) used for
non-effect-materials.md section 3. It prints shader instructions derived from game bytes: write its
output only to an untracked local directory (standard_lighting_programs.py --disassemble DIR)."""
import struct, sys
import shader_constants as sc
OPS={0:'nop',1:'mov',2:'add',3:'sub',4:'mad',5:'mul',6:'rcp',7:'rsq',8:'dp3',9:'dp4',10:'min',11:'max',12:'slt',13:'sge',14:'exp',15:'log',16:'lit',17:'dst',18:'lrp',19:'frc',20:'m4x4',21:'m4x3',22:'m3x4',23:'m3x3',24:'m3x2',25:'call',26:'callnz',27:'loop',28:'ret',29:'endloop',30:'label',31:'dcl',32:'pow',33:'crs',34:'sgn',35:'abs',36:'nrm',37:'sincos',38:'rep',39:'endrep',40:'if',41:'ifc',42:'else',43:'endif',44:'break',45:'breakc',46:'mova',47:'defb',48:'defi',64:'texcoord',65:'texkill',66:'texld',78:'expp',79:'logp',80:'cnd',81:'def',88:'cmp',89:'bem',90:'dp2add',91:'dsx',92:'dsy',93:'texldd',94:'setp',95:'texldl',96:'breakp'}
NODEST={'if','ifc','else','endif','rep','endrep','loop','endloop','break','breakc','breakp','call','callnz','ret','label','nop'}
RT={0:'r',1:'v',2:'c',3:'a',4:'o',5:'oD',6:'o',7:'i',8:'oC',9:'oDepth',10:'s',14:'b',15:'aL',17:'vMisc',18:'l',19:'p'}
def rtype(t): return ((t>>28)&7)|((t>>8)&0x18)
def reg(t,names):
    ty=rtype(t); n=t&0x7ff
    nm=RT.get(ty,f'?{ty}')+str(n)
    key=({2:2,7:1,14:0,10:3}.get(ty),n)
    if key in names: nm+=f'<{names[key]}>'
    return nm
def dst(t,names):
    m=(t>>16)&0xf; mod=(t>>20)&0xf
    s=reg(t,names)+('' if m==0xf else '.'+''.join('xyzw'[i] for i in range(4) if m>>i&1))
    return s+('_sat' if mod&1 else '')+('_pp' if mod&2 else '')
def src(t,names):
    sw=(t>>16)&0xff; sws=''.join('xyzw'[(sw>>(2*i))&3] for i in range(4))
    if sws=='xyzw': sws=''
    elif len(set(sws))==1: sws=sws[0]
    mod=(t>>24)&0xf
    r=reg(t,names)+('.'+sws if sws else '')
    return {0:r,1:'-'+r,0xb:'abs('+r+')',0xc:'-abs('+r+')',0xd:'!'+r}.get(mod,f'mod{mod}({r})')
def names_of(code):
    out={}
    for c in sc.parse_ctab(code):
        for k in range(c['count']): out[(c['register_set'],c['register']+k)]=c['name']+(f'[{k}]' if c['count']>1 else '')
    return out
def dis(code):
    names=names_of(code)
    w=struct.unpack('<%dI'%(len(code)//4),code)
    i=1; out=[]
    while i<len(w):
        t=w[i]
        if t==0xffff: break
        op=t&0xffff
        if op==0xfffe: i+=1+((t>>16)&0x7fff); continue
        ln=(t>>24)&0xf; args=w[i+1:i+1+ln]; name=OPS.get(op,f'op{op}')
        ctl=(t>>16)&0xff
        if name=='def': s=f'def {reg(args[0],names)} = '+', '.join('%g'%struct.unpack('<f',struct.pack('<I',x))[0] for x in args[1:])
        elif name in('defi','defb'): s=f'{name} {reg(args[0],names)} = {list(args[1:])}'
        elif name=='dcl': s=f'dcl_{args[0]&0x1f}_{(args[0]>>16)&0xf} {dst(args[1],names)}'
        elif name in NODEST: s=name+(f'_{ctl}' if name in('ifc','breakc') else '')+' '+', '.join(src(a,names) for a in args)
        else: s=name+(f'_c{ctl}' if ctl and name not in ('texld',) else '')+' '+dst(args[0],names)+(', ' if len(args)>1 else '')+', '.join(src(a,names) for a in args[1:])
        out.append(s); i+=1+ln
    return out
