# Compares the decoded main line with Aquarium's PGN export, over the whole database (multiprocess).
import sys, collections, multiprocessing as mp, chess
import ca, truth
from calib import *
def work(rng):
    a2,d2=load(); offs=truth.index(); res=collections.Counter(); ex=[]
    for gi in rng:
        r,n=record(a2,d2,gi); tags,body=truth.split(truth.game(gi,offs))
        start=chess.Board(tags['FEN'],chess960=True) if 'FEN' in tags else None
        want=truth.mainline(body)
        try:
            info={}; nodes=ca.parse(r,start,info); got=[n.san for n in nodes if n.san]
            if (start is not None)!=(info['tail'] is not None): res['setup-presence-mismatch']+=1
            if start is not None and info['tail'] is not None:
                a,b_=info['start'],start
                if a.board_fen()!=b_.board_fen() or a.turn!=b_.turn: res['setup-board-mismatch']+=1; ex.append((gi,'SETUPBOARD %s vs %s'%(a.fen(),b_.fen())))
                elif a.fullmove_number!=b_.fullmove_number: res['setup-moveno-mismatch']+=1
                tail=info['tail'].hex()+' castling='+b_.fen().split()[2]+' ep='+b_.fen().split()[3]
                res['tail:'+tail[4:]]+=1
            want=[('Z0' if w in('Z0','--') else w) for w in want]
            if got!=want:  # Aquarium disambiguates more than necessary: comparison of the moves themselves
                b=info['start'].copy(); w2=[]
                try:
                    for s_ in want:
                        if s_=='Z0': b.push(chess.Move.null()); w2.append('Z0'); continue
                        m=b.parse_san(s_); w2.append(b.san(m)); b.push(m)
                    want=w2
                except Exception as e: res['truth-unparsable']+=1
            if got==want: res['ok']+=1
            else:
                res['diff']+=1
                k=next((i for i,(a,b) in enumerate(zip(got,want)) if a!=b),min(len(got),len(want)))
                if len(ex)<400: ex.append((gi,'diff at %d: got %s want %s (len %d/%d)'%(k,got[k:k+3],want[k:k+3],len(got),len(want))))
        except ca.Bad as e:
            key=str(e).split(' in ')[0]; key=''.join(ch for ch in key if not ch.isdigit())[:28]
            res['bad:'+('FEN ' if start else '')+key]+=1
            if len(ex)<400: ex.append((gi,('FEN ' if start else '')+str(e)))
        except Exception as e:
            res['exc:'+type(e).__name__]+=1
            if len(ex)<400: ex.append((gi,repr(e)))
    return res,ex
if __name__=='__main__':
    step=int(sys.argv[1]) if len(sys.argv)>1 else 1
    ids=list(range(0,N,step)); k=64; chunks=[ids[i::k] for i in range(k)]
    tot=collections.Counter(); exs=[]
    with mp.Pool(30) as p:
        for res,ex in p.imap_unordered(work,chunks): tot.update(res); exs+=ex
    print(sorted(tot.items(),key=lambda x:-x[1]))
    exs.sort(); seen=collections.Counter()
    for gi,e in exs:
        key=''.join(ch for ch in e.split(' in ')[0] if not ch.isdigit())[:24]
        seen[key]+=1
        if seen[key]<=int(sys.argv[2]) if len(sys.argv)>2 else 5: print(gi,e)
