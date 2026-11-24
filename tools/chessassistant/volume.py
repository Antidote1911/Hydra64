# "Data Pro Volume format 1.1" (.emb, .st0): embedded files.
# Directory entry (0x100 bytes): u32 0, u32 2, u32 size, u32 offset of the extent table,
# 12 bytes, name (C). Extent table: (u32 offset, u32 length) pairs until the size is covered.
import struct, re
def files(path):
    d=open(path,'rb').read(); out={}
    for m in re.finditer(rb'[A-Za-z0-9_ ]{2,20}\.[a-z0-9]{3}\x00',d):
        p=m.start()-0x1c
        if p%0x100: continue
        _,_,size,x=struct.unpack_from('<4I',d,p); buf=b''; q=x
        while len(buf)<size:
            off,ln=struct.unpack_from('<2I',d,q); q+=8
            if ln==0: raise ValueError('extent table too short for '+m.group().decode())
            buf+=d[off:off+ln]
        out[m.group()[:-1].decode()]=buf[:size]
    return out
if __name__=='__main__':
    import sys
    for n,b in files(sys.argv[1]).items(): print(f'{n:14} {len(b):9}')
