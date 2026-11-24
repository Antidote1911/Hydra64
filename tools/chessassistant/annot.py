# Checks variations and annotations against the PGN export; table (type, content) → Aquarium's rendering.
import re, collections, multiprocessing as mp, chess
import ca, truth
from calib import *
def flat(nodes,out,anns):
    for n in nodes:
        for a in n.pre: anns.append(a)
        if n.san: out.append(n.san)
        for a in n.ann: anns.append(a)
        for v in n.vars:
            out.append('('); flat(v,out,anns); out.append(')')
def pgn_tokens(body):
    toks=[];coms=[];i=0;n=len(body)
    while i<n:
        c=body[i]
        if c=='{':
            j=body.find('}',i); coms.append(body[i+1:j]); i=j+1; continue
        if c in '()': toks.append(c); i+=1; continue
        if c.isspace(): i+=1; continue
        j=i
        while j<n and not body[j].isspace() and body[j] not in '{}()': j+=1
        tok=body[i:j]; i=j
        if tok[0]=='$' or tok in('1-0','0-1','1/2-1/2','*'): continue
        tok=re.sub(r'^\d+\.+','',tok)
        if tok: toks.append(tok)
    return toks,coms
def canon(t):
    if t in '()' or t.startswith('O-') or t in('Z0','--'): return 'Z0' if t=='--' else t
    t=t.rstrip('+#!?').replace('x','')
    m=re.match(r'^([NBRQK]?)[a-h1-8]*?([a-h][1-8])(=[NBRQ])?$',t)
    return (m.group(1)+m.group(2)+(m.group(3) or '')) if m else t
def work(rng):
    a2,d2=load(); offs=truth.index(); res=collections.Counter(); table=collections.defaultdict(collections.Counter); ex=[]
    for gi in rng:
        r,n=record(a2,d2,gi)
        if not any(x>=0xe7 for x in r): continue
        tags,body=truth.split(truth.game(gi,offs))
        try: nodes=ca.parse(r)
        except ca.Bad: res['undecoded']+=1; continue
        mine=[];anns=[]; flat(nodes,mine,anns); toks,coms=pgn_tokens(body)
        if [canon(t) for t in mine]==[canon(t) for t in toks]: res['tree-ok']+=1
        else:
            res['tree-diff']+=1
            if len(ex)<3: ex.append((gi,mine[:40],toks[:40]))
        if len(anns)==len(coms):
            res['ann-count-ok']+=1
            for (typ,txt),c in zip(anns,coms):
                if typ in (4,5):
                    w=c.replace('\r\n',' ').encode('cp1251','replace'); g=txt.replace(b'\r\n',b' ').replace(b'\n',b' ')
                    res['text-ok' if re.sub(rb'\s+',b' ',w).strip()==re.sub(rb'\s+',b' ',g).strip() else 'text-diff']+=1
                    if res['text-diff']<=3 and re.sub(rb'\s+',b' ',w).strip()!=re.sub(rb'\s+',b' ',g).strip() and len(ex)<8: ex.append((gi,g[:80],w[:80]))
                else: table[(typ,txt if len(txt)<=3 else b'...')][c[:24]]+=1
        else:
            res['ann-count-diff']+=1
            if len(ex)<6: ex.append((gi,'anns',len(anns),len(coms),anns[:6],coms[:6]))
    return res,dict(table),ex
if __name__=='__main__':
    k=64; chunks=[range(i,N,k) for i in range(k)]; tot=collections.Counter(); T=collections.defaultdict(collections.Counter); exs=[]
    with mp.Pool(30) as p:
        for res,tab,ex in p.imap_unordered(work,chunks):
            tot.update(res); exs+=ex
            for kk,v in tab.items(): T[kk].update(v)
    print(sorted(tot.items(),key=lambda x:-x[1]))
    for kk in sorted(T,key=lambda k:(k[0],-sum(T[k].values()))): print(kk[0],kk[1].hex(' '),sum(T[kk].values()),T[kk].most_common(3))
    for e in exs[:14]: print(e)
