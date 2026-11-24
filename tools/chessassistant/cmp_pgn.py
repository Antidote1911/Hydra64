# Compares a PGN produced by hydra64-convert with Aquarium's export (tree of moves and headers).
#   python cmp_pgn.py <Hydra64 pgn> <1-based numbers of the missing games, comma-separated>
import sys, re, collections, multiprocessing as mp
import truth
from annot import pgn_tokens, canon
def fx(s):
    try: return s.encode('cp1251').decode('cp1252')
    except Exception: return s
def load(path):
    data=open(path,'rb').read().decode('utf-8'); offs=[m.start() for m in re.finditer(r'(?:^|\n\n)\[Event ',data)]
    offs=[o+2 if data[o]=='\n' else o for o in offs]; return data,offs
def work(a):
    path,skip,rng=a; data,offs=load(path); to=truth.index(); res=collections.Counter(); ex=[]
    ids=[i for i in range(len(to)) if i not in skip]
    for k in rng:
        gi=ids[k]; mine=data[offs[k]:offs[k+1] if k+1<len(offs) else len(data)]
        h=mine.find('\n\n'); tags=dict(re.findall(r'\[(\w+) "(.*)"\]',mine[:h])); toks,coms=pgn_tokens(mine[h+2:])
        t,b=truth.split(truth.game(gi,to)); wt,wc=pgn_tokens(b)
        ok=[canon(x) for x in toks]==[canon('Z0' if x=='--' else x) for x in wt]
        res['tree-ok' if ok else 'tree-diff']+=1
        if not ok:
            k2=next((i for i,(x,y) in enumerate(zip(toks,wt)) if canon(x)!=canon(y)),min(len(toks),len(wt))); ex.append((gi+1,'TREE',k2,toks[max(0,k2-3):k2+5],wt[max(0,k2-3):k2+5]))
        for key in ('Event','Site','Date','White','Black','Result'):
            g=tags.get(key,'?'); w=t[key]; okh= g==w or g==fx(w) or g.replace('\\"','"')==fx(w) or g.replace('\\"','"')==w
            res[(key,okh)]+=1
            if not okh and (len(ex)<12 or key=='Date'): ex.append((gi+1,key,g,w))
    return res,ex
if __name__=='__main__':
    path=sys.argv[1]; skip={int(x)-1 for x in sys.argv[2].split(',')} if len(sys.argv)>2 else set()
    n=len(load(path)[1]); k=60; tot=collections.Counter(); exs=[]
    with mp.Pool(30) as p:
        for res,ex in p.imap_unordered(work,[(path,skip,range(j,n,k)) for j in range(k)]): tot.update(res); exs+=ex
    print(n,'games'); print(sorted(tot.items(),key=str)); [print(e) for e in [e for e in sorted(exs,key=str) if e[1] not in ('Event','Site')][:25]]
