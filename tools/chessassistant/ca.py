# Decoder of a Chess Assistant game (.d02 record) → tree of moves.
import chess
KN=[(-1,-2),(-2,-1),(-2,1),(-1,2),(1,2),(2,1),(2,-1),(1,-2)]
DA=[(k,k) for k in range(1,8)]; DB=[(7,1),(6,2),(5,3),(3,5),(2,6),(1,7)]
RK=[(k,0) for k in range(7,0,-1)]+[(0,k) for k in range(7,0,-1)]
DELTA={'B':DA+DB,'R':RK,'Q':DA+DB+RK,'K':[(1,1),(7,7),(7,1),(1,7),(7,0),(1,0),(0,7),(0,1)]}
RANGES=[('N',0x20,8,2),('B',0x30,13,2),('R',0x4a,14,2),('Q',0x66,27,1),('K',0x81,8,1)]
PROMO={0:chess.KNIGHT,1:chess.BISHOP,2:chess.ROOK,3:chess.QUEEN}
IDX={8:('N',0),9:('N',1),10:('B',0),11:('B',1),12:('R',0),13:('R',1),14:('Q',0)}
class Bad(Exception): pass
XSTEP=34
def classify(c):
    if c<0x20: return ('P',c>>2,c&3)
    for t,base,n,k in RANGES:
        if base<=c<base+n*k: return (t,(c-base)//n,(c-base)%n)
    return None
class St:
    def __init__(s,board=None):
        s.b=board or chess.Board(chess960=True); s.sl={}; s.pt={True:{},False:{}}; s.xt={True:[None]*2,False:[None]*2}
        for c in (True,False):
            x=0 if c else 56
            s.sl[c]={'P':[chess.square(f,1 if c else 6) for f in range(8)],'N':[chess.B1^x,chess.G1^x],'B':[chess.C1^x,chess.F1^x],
                     'R':[chess.A1^x,chess.H1^x],'Q':[chess.D1^x],'K':[chess.E1^x],'X':[None]*2}
    def copy(s):
        n=St.__new__(St); n.b=s.b.copy(stack=False)
        n.sl={c:{t:l[:] for t,l in d.items()} for c,d in s.sl.items()}; n.pt={c:dict(d) for c,d in s.pt.items()}; n.xt={c:l[:] for c,l in s.xt.items()}; return n
    def push(s,m,promo=None):
        b=s.b; us=b.turn; ep=b.is_en_passant(m)
        if not m: b.push(m); return
        if b.is_castling(m):
            ks=b.is_kingside_castling(m); rr=chess.square_rank(m.from_square); l=s.sl[us]['R']
            for i,sq in enumerate(l):
                if sq==m.to_square: l[i]=chess.square(5 if ks else 3,rr)
            s.sl[us]['K'][0]=chess.square(6 if ks else 2,rr); b.push(m); return
        cap=m.to_square if not ep else chess.square(chess.square_file(m.to_square),chess.square_rank(m.from_square))
        for t,l in s.sl[not us].items():
            for i,sq in enumerate(l):
                if sq==cap:
                    l[i]=None
                    if t=='P': s.pt[not us].pop(i,None)
        for t,l in s.sl[us].items():
            for i,sq in enumerate(l):
                if sq==m.from_square: l[i]=m.to_square
        if m.promotion:
            l=s.sl[us]['P']; pawn_slot=None
            for i,sq in enumerate(l):
                if sq==m.to_square: l[i]=None; pawn_slot=i
            idx=promo&0x1f
            if idx<8 and idx!=pawn_slot: raise Bad('promo keeps another pawn slot %#x'%promo)
            t=chess.piece_symbol(m.promotion).upper()
            if idx in IDX:
                tt,k=IDX[idx]
                if tt!=t: raise Bad('promo type/slot %#x'%promo)
                if s.sl[us][tt][k] is not None: raise Bad('promo slot busy %#x'%promo)
                s.sl[us][tt][k]=m.to_square
            elif 16<=idx<18:
                if s.sl[us]['X'][idx-16] is not None: raise Bad('promo xslot busy %#x'%promo)
                s.sl[us]['X'][idx-16]=m.to_square; s.xt[us][idx-16]=t
            elif idx<8:   # no free slot left: the piece keeps the pawn's number
                s.sl[us]['P'][idx]=m.to_square; s.pt[us][idx]=t
            else: raise Bad('promo idx %#x'%promo)
        b.push(m)
    def move(s,c):
        b=s.b; cl=classify(c)
        if cl is None:
            if c in (0xd0,0xcf):
                for m in b.generate_castling_moves():
                    if b.is_kingside_castling(m)==(c==0xd0): return m
                raise Bad('castling impossible %#x'%c)
            if c==0xd3: return chess.Move.null()
            if 0x89<=c<0xcf:
                e,code=divmod(c-0x89,35); sq=s.sl[b.turn]['X'][e]   # 27 queen codes then 8 knight codes
                if sq is None: raise Bad('no extra piece %#x'%c)
                f,rk=chess.square_file(sq),chess.square_rank(sq)
                if code>=27:
                    df,dr=KN[code-27]; to=(f+df,rk+dr)
                    if not(0<=to[0]<8 and 0<=to[1]<8): raise Bad('off board')
                    return chess.Move(sq,chess.square(*to))
                df,dr=DELTA['Q'][code]; return chess.Move(sq,chess.square((f+df)%8,(rk+dr)%8))
            raise Bad('unknown byte %#x'%c)
        t,slot,code=cl; l=s.sl[b.turn][t]; sq=l[slot]
        if sq is not None and t=='P' and slot in s.pt[b.turn]:   # promoted piece left in the pawn's slot: same 4 codes, modulo 8
            d=1 if b.turn else -1; df,dr={0:(0,d),3:(0,2*d),1:(-1,d),2:(1,d)}[code]
            return chess.Move(sq,chess.square((chess.square_file(sq)+df)%8,(chess.square_rank(sq)+dr)%8))
        if sq is None: raise Bad('empty slot %s%d (%#x)'%(t,slot,c))
        f,rk=chess.square_file(sq),chess.square_rank(sq)
        if t=='P':
            d=1 if b.turn else -1
            to={0:(f,rk+d),3:(f,rk+2*d),1:(f-1,rk+d),2:(f+1,rk+d)}[code]
        elif t=='N': to=(f+KN[code][0],rk+KN[code][1])
        else:
            df,dr=DELTA[t][code]; to=((f+df)%8,(rk+dr)%8)
        if not(0<=to[0]<8 and 0<=to[1]<8): raise Bad('off board %#x'%c)
        return chess.Move(sq,chess.square(*to))
TYPES={1:chess.PAWN,2:chess.KNIGHT,3:chess.BISHOP,4:chess.ROOK,5:chess.QUEEN,6:chess.KING}
NAMES=['P']*8+['N','N','B','B','R','R','Q','K']
def setup(d):
    """E6 block: 32 × (square, type) — 16 white pieces then 16 black, in index order —
    then u16 half-move, castling mask, byte (en passant?)."""
    b=chess.Board(None,chess960=True); st=St.__new__(St); st.b=b; st.sl={}; st.pt={True:{},False:{}}; st.xt={True:[None]*2,False:[None]*2}
    for ci,c in enumerate((True,False)):
        sl={'P':[None]*8,'N':[None]*2,'B':[None]*2,'R':[None]*2,'Q':[None],'K':[None],'X':[None]*2}
        for i in range(18):
            sqb,t=d[(ci*18+i)*2],d[(ci*18+i)*2+1]
            if t==0: continue
            sq=chess.square(sqb&7,sqb>>4); b.set_piece_at(sq,chess.Piece(TYPES[t],c)); sym=chess.piece_symbol(TYPES[t]).upper()
            if i>=16: sl['X'][i-16]=sq; st.xt[c][i-16]=sym; continue
            n=NAMES[i]; k=i if i<8 else (i-8)%2 if i<14 else 0
            if sym!=n: raise Bad('setup: slot %d holds type %d'%(i,t))
            sl[n][k]=sq
        st.sl[c]=sl
    ply=d[72]|d[73]<<8; b.turn=ply%2==0; b.fullmove_number=ply//2+1
    cm=d[74]
    st.setup_tail=d[72:76]
    rights=0
    for bit,c,ks in ((1,True,True),(2,True,False),(4,False,True),(8,False,False)):
        if cm&bit:
            k=b.king(c); rooks=[q for q in b.pieces(chess.ROOK,c) if chess.square_rank(q)==chess.square_rank(k) and ((chess.square_file(q)>chess.square_file(k))==ks)]
            if rooks: rights|=chess.BB_SQUARES[max(rooks) if ks else min(rooks)]
    b.castling_rights=rights
    return st
class Node:
    __slots__=('san','move','ann','vars','pre')
    def __init__(s): s.san=None; s.move=None; s.ann=[]; s.vars=[]; s.pre=[]
def parse(r,start=None,info=None):
    """r : octets de la partie (sans la longueur). Renvoie (annotations initiales, ligne principale)."""
    pos=[0]
    def line(st):
        nodes=[]; pending_vars=[]; pre=[]
        while pos[0]<len(r):
            c=r[pos[0]]
            if c==0xe8: break
            pos[0]+=1
            if c==0xe7:
                sub=line(st.copy())
                if pos[0]>=len(r) or r[pos[0]]!=0xe8: raise Bad('variation not closed')
                pos[0]+=1; pending_vars.append(sub); continue
            if c in (0xe9,0xea,0xeb,0xec):
                if c==0xe9: typ=r[pos[0]]; txt=b''; pos[0]+=1
                elif c==0xea: typ=r[pos[0]]; txt=r[pos[0]+1:pos[0]+2]; pos[0]+=2
                else:
                    if c==0xeb: n=r[pos[0]]; pos[0]+=1
                    else: n=int.from_bytes(r[pos[0]:pos[0]+4],'little'); pos[0]+=4
                    typ=r[pos[0]]; txt=r[pos[0]+1:pos[0]+n]; pos[0]+=n
                (nodes[-1].ann if nodes and not pending_vars else pre).append((typ,txt)); continue
            promo=None; ep=False
            if c==0xd2: ep=True; c=r[pos[0]]; pos[0]+=1
            if c==0xd1: promo=r[pos[0]+1]; c=r[pos[0]]; pos[0]+=2
            if 0xd4<=c<=0xe5 and promo is None:
                # long move of piece no. (c − D4): pawns 0–7, N 8–9, B 10–11, R 12–13, Q 14, K 15, extras 16–17
                k=c-0xd4; sl=st.sl[st.b.turn]
                sq=sl['P'][k] if k<8 else sl['N'][k-8] if k<10 else sl['B'][k-10] if k<12 else sl['R'][k-12] if k<14 else sl['Q'][0] if k==14 else sl['K'][0] if k==15 else sl['X'][k-16]
                t=r[pos[0]]; pos[0]+=1
                if sq is None or (k<8 and k not in st.pt[st.b.turn]): raise Bad('long move without piece %#x'%c)
                m=chess.Move(sq,chess.square((chess.square_file(sq)+(t&15))%8,(chess.square_rank(sq)+(t>>4))%8))   # Δrank<<4 | Δfile, modulo 8
                if k==15 and m not in st.b.legal_moves:   # Chess960: the king jumps to its castling square
                    for cm in st.b.generate_castling_moves():
                        if chess.square(6 if st.b.is_kingside_castling(cm) else 2,chess.square_rank(sq))==m.to_square: m=cm
            else: m=st.move(c)
            if promo is not None: m.promotion=PROMO.get((promo-0x40)>>5)
            if m and m not in st.b.legal_moves: raise Bad('illegal %s (%#x) in %s'%(m.uci(),c,st.b.fen()))
            if m and ep!=st.b.is_en_passant(m): raise Bad('ep flag')
            nd=Node(); nd.move=m; nd.san=st.b.san(m) if m else 'Z0'; nd.vars=pending_vars; nd.pre=pre; pending_vars=[]; pre=[]
            st.push(m,promo); nodes.append(nd)
        if pending_vars or pre:
            nd=Node(); nd.vars=pending_vars; nd.pre=pre; nodes.append(nd)  # annotations with no move after them
        return nodes
    if r[:1]==b'\xe6':
        st=setup(r[1:77]); pos[0]=77
    else: st=St()
    if info is not None: info['start']=st.b.copy(); info['tail']=getattr(st,'setup_tail',None)
    nodes=line(st)
    if pos[0]!=len(r): raise Bad('trailing bytes at %d'%pos[0])
    return nodes
