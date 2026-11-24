# "Data Pro Tree format 1.1" tree (.td0) and sorted order (.ti0).
#   .ti0: by rank, u24 game number + u8 result — games sorted by their moves.
#   .td0: 0x80 header, u32 (maximum depth), then a stream of scrambled bytes:
#          clear byte = INVERSE_TABLE[byte] − offset in the file (mod 256).
#          Stream: E7 <u32 end of the branch> <u32 number of games> … E8 = branch; ED = a game
#          ends here; the other bytes are moves (same codes as .d02, D1/D2/D4–DB included).
#          The two u32 after E7 are in clear.
import struct, sys, os
P=open(os.path.join(os.path.dirname(__file__),'td0_table.bin'),'rb').read()
T=bytearray(256)
for i,b in enumerate(P): T[b]=i
def games(path):
    """Move byte sequences of the games, in tree order (= order of .ti0)."""
    td=open(path,'rb').read(); p=0x84; stack=[]; cur=[]; out=[]; checks=[0,0]
    while p<len(td):
        b=(T[td[p]]-p)&0xff; p+=1
        if b==0xe7:
            end,n=struct.unpack_from('<II',td,p); p+=8; stack.append((len(cur),end,n,len(out)))
        elif b==0xe8:
            depth,end,n,first=stack.pop(); checks[0]+= (p==end) ; checks[1]+= (len(out)-first==n); del cur[depth:]
        elif b==0xed: out.append(bytes(cur))
        else:
            cur.append(b)
            if b==0xd1:
                cur.append((T[td[p]]-p)&0xff); cur.append((T[td[p+1]]-(p+1))&0xff); p+=2
            elif 0xd4<=b<=0xe5:
                cur.append((T[td[p]]-p)&0xff); p+=1
    return out,checks,struct.unpack_from('<I',td,0x80)[0]
def mainline_bytes(r):
    """Move bytes of the main line of a .d02 record (without annotations or variations)."""
    out=bytearray(); p=0; depth=0
    if r[:1]==b'\xe6': return None
    while p<len(r):
        c=r[p]; n=1
        if c==0xe7: depth+=1
        elif c==0xe8: depth-=1
        elif c==0xe9: n=2
        elif c==0xea: n=3
        elif c==0xeb: n=2+r[p+1]
        elif c==0xec: n=5+int.from_bytes(r[p+1:p+5],'little')
        elif c==0xd1: n=3
        elif 0xd4<=c<=0xe5: n=2
        if depth==0 and c<0xe7: out+=r[p:p+n]
        p+=n
    return bytes(out)
if __name__=='__main__':
    import cabase
    base=sys.argv[1]; b=cabase.Base(base,names=False)
    g,checks,depth=games(base+'.td0'); ti=open(base+'.ti0','rb').read()
    print(len(g),'games in the tree /',b.count,'; branches closed in the right place',checks[0],'; exact game counts',checks[1],'; profondeur',depth,'; plus longue',max(map(len,g)))
    ok=bad=skip=0; ex=[]
    for k,seq in enumerate(g):
        gi=int.from_bytes(ti[k*4:k*4+3],'little'); want=mainline_bytes(b.record(gi))
        if want is None: skip+=1
        elif want==seq: ok+=1
        else:
            bad+=1
            if len(ex)<5: ex.append((k,gi+1,seq[:20].hex(' '),want[:20].hex(' '),len(seq),len(want)))
    print('identical',ok,'different',bad,'starting positions',skip); [print(e) for e in ex]
