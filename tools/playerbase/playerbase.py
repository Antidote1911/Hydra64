#!/usr/bin/env python3
"""Reader of the ChessBase players encyclopedia ("Playbase") — see docs/playerbase-re.md.

    playerbase.py <Playbase folder> "Last, First"     profile, photos and Elo of a player
"""
import mmap, os, struct, sys

RATING_KEY = bytes.fromhex("f7fdff55c7ed4fc597dd9e3467cdefa537bd3f")
MEDIA_P = bytes.fromhex("ee7a5e4a4e1a3e6a2e3a9e0a8eda7e2a6efadecace9abeeaaeba1e8a0e5afeaa")
MEDIA_KEY = bytes(MEDIA_P[(j + 25) % 32] ^ 0xCC for j in range(89))
TITLES = {0: "", 1: "FM", 2: "IM", 3: "GM"}
WTITLES = {0: "", 1: "WFM", 2: "WIM", 3: "WGM"}

def date(v):
    return (v >> 9, (v >> 5) & 15, v & 31)

def find_file(folder, ext):
    for n in os.listdir(folder):
        if n.lower().endswith(ext): return os.path.join(folder, n)
    raise FileNotFoundError(ext)

class Playbase:
    def __init__(self, folder):
        self.folder = folder
        self.players = self._map(find_file(folder, ".player"))
        self.rating = self._map(find_file(folder, ".rating"))
        self.media = self._map(find_file(folder, ".media"))
        self.count = struct.unpack_from(">I", self.players, 0)[0]
        self._groups = None

    @staticmethod
    def _map(path):
        f = open(path, "rb")
        return mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

    def records(self):
        pos, m = 8, self.players
        while pos + 4 <= len(m):
            n = struct.unpack_from(">I", m, pos)[0]
            yield self.player(m[pos + 4:pos + 4 + n])
            pos += 4 + n

    @staticmethod
    def player(r):
        a = struct.unpack_from("<I", r, 0)[0]; last = r[4:4 + a].decode("cp1252", "replace")
        b = struct.unpack_from("<I", r, 4 + a)[0]; first = r[8 + a:8 + a + b].decode("cp1252", "replace")
        t = r[8 + a + b:]
        birth, homonym, pid, death, flags, nat, nat2, sex = struct.unpack_from("<IHIIHHHI", t, 0)
        media = struct.unpack_from("<i", t, 115)[0]
        return dict(last=last, first=first, id=pid, birth=date(birth), death=date(death), homonym=homonym, flags=flags,
                    title=TITLES[(flags >> 4) & 3], women_title=WTITLES[(flags >> 2) & 3], world_champion=bool(flags & 0x80),
                    women_world_champion=bool(flags & 0x40), reigning=bool(flags & 0x100), nation=nat, nation2=nat2, male=bool(sex & 1), media=media)

    def ratings(self, pid):
        if self._groups is None:  # (identifier → offsets); a player's ratings almost always follow one another
            self._groups = {}
            m, k = self.rating, RATING_KEY
            for i in range((len(m) - 4) // 19):
                o = 4 + i * 19
                p = (m[o] ^ k[0]) | (m[o + 1] ^ k[1]) << 8 | (m[o + 2] ^ k[2]) << 16
                self._groups.setdefault(p, []).append(o)
        out = []
        for o in self._groups.get(pid, []):
            r = bytes(a ^ b for a, b in zip(self.rating[o:o + 19], RATING_KEY))
            _, _, _, _, _, d, rank = struct.unpack("<IHHHBIi", r)
            elo = r[6] << 8 | r[7]  # most significant first
            out.append((date(d), elo, rank))
        return sorted(out)

    def media_record(self, i):
        r = bytes(a ^ b for a, b in zip(self.media[i * 89:(i + 1) * 89], MEDIA_KEY))
        s = lambda b: b.split(b"\0")[0].decode("cp1252", "replace")
        d, number, nxt = struct.unpack_from("<Iii", r, 74)
        return dict(prefix=s(r[0:4]), photographer=s(r[4:44]), place=s(r[44:74]), date=date(d), number=number, next=nxt, flags=r[86:89].hex())

    def photos(self, player):
        out, i, seen = [], player["media"], set()
        if i < 0: return out
        i = self.media_record(i)["next"]
        while i >= 0 and i not in seen and i * 89 < len(self.media):
            seen.add(i)
            r = self.media_record(i)
            r["file"] = os.path.join(self.folder, "PlayBase.MM" + r["prefix"][:1].upper(), "%s%05d.jpg" % (r["prefix"], r["number"]))
            out.append(r)
            i = r["next"]
        return out

if __name__ == "__main__":
    pb = Playbase(sys.argv[1])
    want = sys.argv[2].lower()
    for p in pb.records():
        if f"{p['last']}, {p['first']}".lower() != want: continue
        print(p)
        for ph in pb.photos(p): print("  photo", ph, "present" if os.path.exists(ph["file"]) else "MISSING")
        rs = pb.ratings(p["id"])
        print("  %d ratings; first %s; last %s; maximum %s" % (len(rs), rs[:2], rs[-2:], max(rs, key=lambda x: x[1]) if rs else None))
