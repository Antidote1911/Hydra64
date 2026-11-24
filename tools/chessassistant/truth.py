# Ground truth: Aquarium's PGN export (595572.pgn), games in database order.
import re, os, pickle
PGN='/home/antidote/Documents/export corr database/595572.pgn'
CACHE=os.environ.get('CA_CACHE','/tmp')+'/ca_pgn_index.pkl'  # index of the export, recomputable
def index():
    if os.path.exists(CACHE): return pickle.load(open(CACHE,'rb'))
    data=open(PGN,'rb').read(); offs=[m.start() for m in re.finditer(rb'(?:^|\r\n\r\n)\[Event ',data)]
    offs=[o+4 if data[o:o+1]==b'\r' else o for o in offs]
    pickle.dump(offs,open(CACHE,'wb')); return offs
_data=None
def game(i,offs):
    global _data
    if _data is None: _data=open(PGN,'rb').read()
    return _data[offs[i]:offs[i+1] if i+1<len(offs) else len(_data)].decode('utf-8')
def split(text):
    h=text.find('\r\n\r\n'); head=text[:h]; body=text[h+4:]
    tags=dict(re.findall(r'\[(\w+) "(.*)"\]',head)); return tags,body
def mainline(body):
    """SAN moves of the main line (without comments, variations, NAGs, numbers, result)."""
    out=[];depth=0;i=0;n=len(body)
    while i<n:
        c=body[i]
        if c=='{':
            i=body.find('}',i)+1 or n; continue
        if c=='(': depth+=1;i+=1;continue
        if c==')': depth-=1;i+=1;continue
        if c.isspace(): i+=1;continue
        j=i
        while j<n and not body[j].isspace() and body[j] not in '{}()': j+=1
        tok=body[i:j]; i=j
        if depth: continue
        if tok[0]=='$' or tok in('1-0','0-1','1/2-1/2','*'): continue
        tok=re.sub(r'^\d+\.+','',tok)
        tok=tok.rstrip('!?')
        if tok: out.append(tok)
    return out
