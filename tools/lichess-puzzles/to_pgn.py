#!/usr/bin/env python3
"""EPD collections (output of convert.cpp) -> PGN of Hydra64's training databases.

Usage: python3 to_pgn.py <folder of the .epd files> <output folder>
Produces one PGN database per category (best_moves/best_move_easy.pgn…, mates/mate_in_2.pgn…), to
be converted afterwards, for example:
  build/hydra64-convert mates/mate_in_2.pgn databases/training/mates/mate_in_2.hy64

Each exercise is a game: FEN header (position to solve), the solution as first move and the
continuation as main line; Event = category ("Meilleur coup, facile", "Mat en 2"…, used by the
Training), White = "Lichess <id>", WhiteElo = rating of the exercise, Site = link to the
exercise; "dm N" as starting comment for a mate.
"""
import os
import re
import sys

# (EPD file from convert.cpp, subfolder and name of the database, category = Event field)
SETS = [
    ("meilleur-coup-facile.epd", "best_moves/best_move_easy", "Meilleur coup, facile"),
    ("meilleur-coup-moyen.epd", "best_moves/best_move_medium", "Meilleur coup, moyen"),
    ("meilleur-coup-difficile.epd", "best_moves/best_move_hard", "Meilleur coup, difficile"),
] + [(f"mats-en-{n}.epd", f"mates/mate_in_{n}", f"Mat en {n}") for n in range(1, 6)]

OP = re.compile(r'\s*([a-z][a-z0-9_]*)\s+("([^"]*)"|[^;]*);')


def parse(line):
    fields = line.split()
    fen = " ".join(fields[:4])
    rest = line.split(None, 4)[4] if len(fields) > 4 else ""
    ops = {}
    for m in OP.finditer(rest):
        ops[m.group(1)] = m.group(3) if m.group(3) is not None else m.group(2).strip()
    return fen, ops


def numbered(fen, sans):
    white = fen.split()[1] == "w"
    number = 1
    out = []
    for i, san in enumerate(sans):
        if white:
            out.append(f"{number}. {san}")
        else:
            out.append(f"{number}... {san}" if i == 0 else san)
            number += 1
        white = not white
    return " ".join(out)


def main(src, dst):
    for epd_name, out_name, category in SETS:
        games = []
        skipped = 0
        with open(os.path.join(src, epd_name), encoding="utf-8") as f:
            rows = [l.strip() for l in f if l.strip() and not l.startswith("#")]
        n = 0
        for line in rows:
            fen, ops = parse(line)
            m = re.match(r"Lichess (\S+) \((\d+)\)", ops.get("id", ""))
            pid, rating = (m.group(1), m.group(2)) if m else ("?", "")
            sans = (ops.get("pv") or ops.get("bm", "")).split()
            # Lichess's "mateIn5" means "mate in 5 or more": we only keep the mates
            # whose solution mates exactly on the announced move.
            if "dm" in ops and len(sans) != 2 * int(ops["dm"]) - 1:
                skipped += 1
                continue
            comment = f"{{dm {ops['dm']}}} " if "dm" in ops else ""
            n += 1
            headers = [("Event", category), ("Site", f"https://lichess.org/training/{pid}"), ("Date", "????.??.??"),
                       ("Round", str(n)), ("White", f"Lichess {pid}"), ("Black", "?"), ("Result", "*")]
            if rating:
                headers.append(("WhiteElo", rating))
            headers += [("Annotator", "Lichess (CC0)"), ("SetUp", "1"), ("FEN", fen + " 0 1")]
            text = "\n".join(f'[{k} "{v}"]' for k, v in headers)
            games.append(f"{text}\n\n{comment}{numbered(fen, sans)} *\n")
        path = os.path.join(dst, out_name + ".pgn")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(games))
        print(out_name, len(games), "games", f"({skipped} mates longer than announced discarded)" if skipped else "")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
