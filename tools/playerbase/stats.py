import struct, numpy as np
from explore import records, names, D
rows = []
for off, r in records():
    l, f, k = names(r); t = r[k:]
    rows.append(struct.unpack_from("<IHIIHHHI", t, 0) + struct.unpack_from("<iiiIIHII", t, 115))
a = np.array(rows, dtype=np.int64)
lab = "birth u4 id death flags nat nat2 sex | m115 m119 m123 z127 f131 f135 d137 d141".replace("|", "").split()
for i, n in enumerate(lab):
    c = a[:, i]; u, cnt = np.unique(c, return_counts=True); top = np.argsort(-cnt)[:6]
    print("%-6s min %d max %d distinct %d  top %s" % (n, c.min(), c.max(), len(u), [(int(u[j]), int(cnt[j])) for j in top]))
ids = np.sort(a[:, 2]); print("ids uniques", len(np.unique(ids)), ids[:5], ids[-5:])
# bytes 24..114 non-zero?
nz = np.zeros(145, dtype=np.int64)
for off, r in records():
    l, f, k = names(r); t = np.frombuffer(r[k:], dtype=np.uint8); nz += t != 0
print("positions non nulles 22..115 :", [(i, int(nz[i])) for i in range(22, 115) if nz[i]])
r = np.fromfile(D + "PlayBase.rating", dtype=np.uint8)[4:].reshape(-1, 19)
print(len(r), "notes")
for p in range(19):
    u, cnt = np.unique(r[:, p], return_counts=True); top = np.argsort(-cnt)[:5]
    print(p, len(u), [(hex(int(u[j])), int(cnt[j])) for j in top])
