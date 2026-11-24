import csv, sys, random, io
random.seed(1911)
groups = {f"mate{n}": [] for n in range(1,6)}
groups.update({"best1": [], "best2": [], "best3": []})
r = csv.reader(io.TextIOWrapper(sys.stdin.buffer, encoding="utf-8"))
next(r)
for row in r:
    pid, fen, moves, rating, dev, pop, plays, themes = row[:8]
    rating, dev, pop, plays = int(rating), int(dev), int(pop), int(plays)
    if dev > 80 or pop < 85 or plays < 1000: continue
    th = themes.split()
    mates = [t for t in th if t.startswith("mateIn")]
    if mates:
        n = int(mates[0][6:])
        if 1 <= n <= 5: groups[f"mate{n}"].append((rating, pid, fen, moves))
        continue
    if "mate" in th: continue
    if rating < 1500: g = "best1"
    elif rating < 2000: g = "best2"
    else: g = "best3"
    groups[g].append((rating, pid, fen, moves))
want = {"mate1":300,"mate2":500,"mate3":500,"mate4":300,"mate5":200,"best1":600,"best2":600,"best3":600}
for g, rows in groups.items():
    random.shuffle(rows)
    sel = sorted(rows[:want[g]])
    with open(f"sel_{g}.txt","w") as f:
        for rating,pid,fen,moves in sel: f.write(f"{pid}\t{rating}\t{fen}\t{moves}\n")
    print(g, len(rows), len(sel), file=sys.stderr)
