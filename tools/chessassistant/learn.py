# Learning of the move codes by legality (python-chess).
import sys, collections, chess
from calib import *
KN=[(-1,-2),(-2,-1),(-2,1),(-1,2),(1,2),(2,1),(2,-1),(1,-2)]
class St:
    def __init__(s):
        s.b=chess.Board()
        # slots: [colour][type] -> list of squares
        s.sl={c:{'N':[chess.B1^(56 if not c else 0),chess.G1^(56 if not c else 0)],
                 'B':[chess.C1^(56 if not c else 0),chess.F1^(56 if not c else 0)],
                 'R':[chess.A1^(56 if not c else 0),chess.H1^(56 if not c else 0)],
                 'Q':[chess.D1^(56 if not c else 0)],'K':[chess.E1^(56 if not c else 0)]} for c in (True,False)}
    def push(s,m):
        b=s.b; us=b.turn
        # update of the slots
        for t,l in s.sl[us].items():
            for i,sq in enumerate(l):
                if sq==m.from_square: l[i]=m.to_square
        if b.is_castling(m):
            ks=chess.square_file(m.to_square)==6
            r_from=chess.square(7 if ks else 0,chess.square_rank(m.from_square)); r_to=chess.square(5 if ks else 3,chess.square_rank(m.from_square))
            l=s.sl[us]['R']
            for i,sq in enumerate(l):
                if sq==r_from: l[i]=r_to
        # capture: the captured piece disappears
        cap=m.to_square
        for t,l in s.sl[not us].items():
            for i,sq in enumerate(l):
                if sq==cap: l[i]=None
        b.push(m)
RANGES=[('N',0x20,8,2),('B',0x30,13,2),('R',0x4a,14,2),('Q',0x66,27,1),('K',0x81,8,1)]
def classify(c):
    if c<0x20: return ('P',c>>2,c&3)
    for t,base,n,k in RANGES:
        if base<=c<base+n*k: return (t,(c-base)//n,(c-base)%n)
    return None
def run(table, sample, verbose=False):
    a2,d2=load()
    votes=collections.defaultdict(collections.Counter); seen=collections.Counter()
    stats=collections.Counter(); stops=collections.Counter(); examples={}
    for gi in sample:
        r,n=record(a2,d2,gi)
        if any(x>=0xe6 for x in r): stats['annotated']+=1; continue
        st=St(); b=st.b; ok=True
        for pi,c in enumerate(r):
            cl=classify(c); m=None
            if cl is None:
                if c==0xd0 or c==0xcf:
                    k=b.king(b.turn); m=chess.Move(k,k+2 if c==0xd0 else k-2)
                else:
                    stops[('special',c)]+=1; examples.setdefault(('special',c),(gi,pi)); ok=False; break
            elif cl[0]=='P':
                f,k=cl[1],cl[2]; us=b.turn; d=1 if us else -1
                cands=[sq for sq in b.pieces(chess.PAWN,us) if chess.square_file(sq)==f]
                tgt=None
                for sq in cands:
                    rk=chess.square_rank(sq)
                    if k==0: to=(f,rk+d)
                    elif k==3: to=(f,rk+2*d)
                    elif k==1: to=(f-1,rk+d)
                    else: to=(f+1,rk+d)
                    if not(0<=to[0]<8 and 0<=to[1]<8): continue
                    mv=chess.Move(sq,chess.square(*to))
                    if to[1] in (0,7): mv.promotion=chess.QUEEN; 
                    if mv in b.legal_moves: 
                        if tgt is not None: stats['pawn-ambiguous']+=1
                        tgt=mv
                if tgt is not None and tgt.promotion: stats['promo']+=1; examples.setdefault('promo',(gi,pi))
                m=tgt
            else:
                t,slot,code=cl; sq=st.sl[b.turn][t][slot] if slot<len(st.sl[b.turn][t]) else None
                if sq is None: stops[('noslot',t,slot)]+=1; examples.setdefault(('noslot',t),(gi,pi)); ok=False; break
                f,rk=chess.square_file(sq),chess.square_rank(sq)
                if t=='N':
                    df,dr=KN[code]; to=(f+df,rk+dr)
                    m=chess.Move(sq,chess.square(*to)) if 0<=to[0]<8 and 0<=to[1]<8 else None
                elif (t,code) in table:
                    df,dr=table[(t,code)]; m=chess.Move(sq,chess.square((f+df)%8,(rk+dr)%8))
                else:
                    seen[(t,code)]+=1
                    for lm in b.legal_moves:
                        if lm.from_square==sq and not b.is_castling(lm):
                            votes[(t,code)][((chess.square_file(lm.to_square)-f)%8,(chess.square_rank(lm.to_square)-rk)%8)]+=1
                    stats['unknown-stop']+=1; ok=False; break
            if m is None or m not in b.legal_moves:
                stops[('illegal',cl if cl else c)]+=1; examples.setdefault(('illegal',cl[0] if cl else c),(gi,pi)); ok=False
                if verbose and stats['shown']<verbose:
                    stats['shown']+=1; print('ILLEGAL game',gi,'ply',pi,hex(c),cl,b.fen(),r[:pi+3].hex(' '))
                break
            st.push(m)
        if ok: stats['ok']+=1
    return votes,seen,stats,stops,examples
if __name__=='__main__':
    table={}
    sample=range(0,N,int(sys.argv[1]) if len(sys.argv)>1 else 97)
    for it in range(40):
        votes,seen,stats,stops,ex=run(table,sample)
        new={}
        for key,cnt in votes.items():
            best,nb=cnt.most_common(1)[0]
            full=[d for d,v in cnt.items() if v==seen[key]]
            if len(full)==1 and seen[key]>=3: new[key]=full[0]
        print('iter',it,dict(stats),'new',len(new),file=sys.stderr)
        if not new: break
        table.update(new)
    for t,base,n,k in RANGES[1:]:
        print(t,[table.get((t,c)) for c in range(n)])
    print(dict(stats)); print(stops.most_common(30)); print(ex)
    for key,cnt in votes.items(): print('unresolved',key,seen[key],cnt.most_common(4))
