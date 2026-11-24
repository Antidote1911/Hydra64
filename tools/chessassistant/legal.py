# Validation without ground truth: replays the games of a database (legality of each move).
#   python legal.py <database without extension> [step]
import sys, collections, multiprocessing as mp
import cabase, ca
def work(a):
    path,rng=a; b=cabase.Base(path,names=False); res=collections.Counter(); ex=[]
    for i in rng:
        try:
            nodes,info=b.game(i); res['ok']+=1
            if info['tail'] is not None: res['setup']+=1
        except ca.Bad as e:
            k=''.join(c for c in str(e).split(' in ')[0] if not c.isdigit())[:30]; res['bad:'+k]+=1
            if len(ex)<30: ex.append((i+1,str(e)))
        except Exception as e:
            res['exc:'+type(e).__name__]+=1
            if len(ex)<30: ex.append((i+1,repr(e)))
    return res,ex
if __name__=='__main__':
    path=sys.argv[1]; step=int(sys.argv[2]) if len(sys.argv)>2 else 1
    n=cabase.Base(path,names=False).count; k=120
    tot=collections.Counter(); exs=[]
    with mp.Pool(30) as p:
        for res,ex in p.imap_unordered(work,[(path,range(j*step,n,k*step)) for j in range(k)]): tot.update(res); exs+=ex
    print(n,'games;',sorted(tot.items(),key=lambda x:-x[1])); exs.sort(); seen=collections.Counter()
    for i,e in exs:
        key=''.join(c for c in e.split(' in ')[0] if not c.isdigit())[:30]; seen[key]+=1
        if seen[key]<=6: print(i,e[:200])
