# Reader of a Chess Assistant "Chess Data Pro 1.3" database: headers, names, moves.
#   python cabase.py <database without extension> [game no.…]     → PGN on standard output
import struct, sys, os
import volume, ca
RESULT={0:'*',1:'1/2-1/2',2:'1-0',3:'0-1'}
SYMBOLS={0x82:'→',0x83:'↑',0x84:'⇄',0x87:'⨀',0x93:'⊕',0x99:'□',0x9b:'∞',0x9d:'+−',0xb0:'−+',0xb1:'±',0xb2:'⩲',0xb3:'⩱',0xb5:'∓'}
KINDS='Ctrl bCtr Shrt bShr Long bLon Val Temp Dgrm bDgr Time bTim Tab Tabs Mrkr bMrk Tutr bTut 1Anl b1An Summ Fldr 3Anl b2An Medi b4An Obst Numb Refr CVal Clas NTut'.split()
def fixtext(b):
    """Texts: UTF-8; the original accented characters (Latin-1) were reread as cp1251 when the
    database was created ("é" → "й"): they are restored when that is lossless."""
    s=b.decode('utf-8','replace')
    try: return s.encode('cp1251').decode('cp1252')
    except Exception: return s
class Base:
    def __init__(s,base,names=True):
        rd=lambda ext: open(base+'.'+ext,'rb').read()
        import mmap
        s._fh=[]
        def mp(ext):
            f=open(base+'.'+ext,'rb'); s._fh.append(f); return mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
        cdp=rd('cdp'); s.count=struct.unpack_from('<I',cdp,0x68)[0]
        s.d0=mp('d00'); s.d1=mp('d01'); s.d2=mp('d02'); s.a1=mp('a01'); s.a2=mp('a02')
        s.deleted=rd('dxx') if os.path.exists(base+'.dxx') else b''
        s.dict={}
        if not names: return
        F=volume.files(base+'.emb')
        for n in ('Players','Events','Places','Sources'):
            a=struct.unpack('<%dI'%(len(F[n+'.a01'])//4),F[n+'.a01'])+(len(F[n+'.d01']),); d=F[n+'.d01']
            s.dict[n]=[d[a[i]:a[i+1]] for i in range(len(a)-1)]
    def name(s,dic,idx):
        t=s.dict[dic]
        if idx>=len(t): return ''
        r=t[idx]
        if dic=='Players': return fixtext(r[2:2+r[0]])   # 2 lengths (name, title) then the texts
        return fixtext(r[1:1+r[0]])
    def title(s,idx):
        t=s.dict['Players']
        if idx>=len(t): return ''
        r=t[idx]; return r[2+r[0]:2+r[0]+r[1]].decode('latin1')
    def is_deleted(s,i):
        return bool(s.deleted) and bool(s.deleted[i>>3]>>(7-(i&7))&1)   # most significant bits first
    def header(s,i):
        r=s.d0[i*38:i*38+38]; u=lambda a,n:int.from_bytes(r[a:a+n],'little')
        d=u(11,3); y,md=divmod(d,416); m,dd=divmod(md,32)
        date='%s.%s.%s'%('%04d'%y if y else '????','%02d'%m if m else '??','%02d'%dd if dd else '??')
        eco=u(30,2); rnd=u(28,2)
        h={'Event':s.name('Events',u(14,3)) or '?','Site':s.name('Places',u(17,3)) or '?','Date':date,
           'Round':(str(rnd>>9)+('.%d'%(rnd&511) if rnd&511 else '')) if rnd else '?',
           'White':s.name('Players',u(0,3)) or '?','Black':s.name('Players',u(3,3)) or '?','Result':RESULT.get(r[10],'*')}
        if u(6,2): h['WhiteElo']=str(u(6,2))
        if u(8,2): h['BlackElo']=str(u(8,2))
        if eco!=0xFFFF: h['ECO']='%s%02d'%('ABCDE'[eco//100],eco%100)
        if u(32,3)!=0xFFFFFF: h['Annotator']=s.name('Players',u(32,3))
        if u(35,3)!=0xFFFFFF: h['Source']=s.name('Sources',u(35,3))
        for k,idx in (('WhiteTitle',u(0,3)),('BlackTitle',u(3,3))):
            if s.title(idx): h[k]=s.title(idx)
        h['_classes']=u(20,4); h['_gameid']=u(24,4)
        return h
    def record(s,i):
        a=struct.unpack_from('<I',s.a2,i*4)[0]; r=s.d2[a:a+6]; n=0;sh=0;p=0
        while True:
            b=r[p];p+=1;n|=(b&0x7f)<<sh;sh+=7
            if not b&0x80: break
        return s.d2[a+p:a+p+n]
    def game(s,i):
        info={}; nodes=ca.parse(s.record(i),None,info); return nodes,info
    def pgn(s,i):
        h=s.header(i); nodes,info=s.game(i); out=[]
        if info['tail'] is not None: h['SetUp']='1'; h['FEN']=info['start'].fen()
        for k,v in h.items():
            if not k.startswith('_'): out.append('[%s "%s"]'%(k,v.replace('\\','\\\\').replace('"','\\"')))
        def ann(a):
            typ,txt=a
            if typ in (2,3): return '{'+''.join(SYMBOLS.get(c,chr(c)) for c in txt)+'}'
            if typ==29 and len(txt)>=2:   # CVal: centipawns (White), depth, flags, engine
                sc=int.from_bytes(txt[:2],'little',signed=True)
                return '{[%%eval %s]}'%(('#%s%d'%('-' if sc<0 else '',30000-abs(sc))) if abs(sc)>=29000 else '%.2f'%(sc/100))
            if typ not in (4,5): return '{[%%t %s] %s}'%(KINDS[typ] if typ<len(KINDS) else typ, txt.hex())
            return '{'+fixtext(txt).replace('}',')').replace('\r\n',' ')+'}'
        def line(nodes,board,first=True):
            toks=[]; b=board.copy(); force=first
            for n in nodes:
                toks+=[ann(a) for a in n.pre]
                if n.san is None: continue
                before=b.copy()
                if b.turn: toks.append('%d.'%b.fullmove_number)
                elif force or n.pre: toks.append('%d...'%b.fullmove_number)
                toks.append('--' if n.san=='Z0' else n.san); force=False
                b.push(n.move)
                for a in n.ann: toks.append(ann(a)); force=True
                for v in n.vars: toks.append('('+line(v,before)+')'); force=True
            return ' '.join(toks)
        body=line(nodes,info['start'])+' '+h['Result']
        import textwrap
        return '\n'.join(out)+'\n\n'+'\n'.join(textwrap.wrap(body,79,break_long_words=False,break_on_hyphens=False))+'\n'
if __name__=='__main__':
    b=Base(sys.argv[1]); ids=[int(x)-1 for x in sys.argv[2:]] or range(b.count)
    for i in ids:
        try: print(b.pgn(i))
        except ca.Bad as e: sys.stderr.write('partie %d : %s\n'%(i+1,e))
