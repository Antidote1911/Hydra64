# Raw reading of the move records (.d02) — common tools
import struct
D='/mnt/medias/save/Logiciels/Chess/corr2012/corr2012.'
N=595572
def load():
    a2=list(struct.unpack(f'<{N}I',open(D+'a02','rb').read()))
    d2=open(D+'d02','rb').read(); a2.append(len(d2))
    return a2,d2
def record(a2,d2,i):
    r=d2[a2[i]:a2[i+1]]
    # length: 7-bit varint
    n=0;sh=0;p=0
    while True:
        b=r[p];p+=1;n|=(b&0x7f)<<sh;sh+=7
        if not b&0x80:break
    return r[p:],n
