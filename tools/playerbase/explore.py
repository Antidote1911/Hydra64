import struct, sys, mmap
D = "/home/antidote/Games/playerbase/drive_c/users/steamuser/Documents/ChessBase/Playbase/"
def records(path=D + "Playbase.player"):
    f = open(path, "rb"); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    pos = 8
    while pos + 4 <= len(m):
        n = struct.unpack_from(">I", m, pos)[0]
        yield pos + 4, m[pos + 4:pos + 4 + n]
        pos += 4 + n
def names(r):
    a = struct.unpack_from("<I", r, 0)[0]; last = r[4:4 + a]
    b = struct.unpack_from("<I", r, 4 + a)[0]; first = r[8 + a:8 + a + b]
    return last.decode("latin-1"), first.decode("latin-1"), 8 + a + b
if __name__ == "__main__":
    want = [w.lower() for w in sys.argv[1:]]
    n = 0; sizes = {}
    for off, r in records():
        n += 1
        try: last, first, k = names(r)
        except Exception as e: print("ERR", off, r[:40].hex()); break
        sizes[len(r) - k] = sizes.get(len(r) - k, 0) + 1
        if f"{last}, {first}".lower() in want:
            print(off, last, first, len(r) - k); t = r[k:]
            for i in range(0, len(t), 32): print("   %3d  %s" % (i, t[i:i + 32].hex(" ")))
    print(n, "records; sizes after the names:", sorted(sizes.items(), key=lambda x: -x[1])[:12])
