# Decoding tools for Chess Assistant / Aquarium databases

Log and format: `docs/chess-assistant-re.md`.

Dependency: python-chess (`python -m venv venv && venv/bin/pip install chess`).

| File | Role |
|---|---|
| `cabase.py` | complete reader: `python cabase.py <database without extension> [no.…]` writes the PGN |
| `ca.py` | decoder of one game (moves, variations, annotations, starting position) |
| `volume.py` | extraction of the files embedded in an `.emb` / `.st0` |
| `calib.py` | raw access to the records of `.d02` (path of the study database hard-coded) |
| `truth.py` | reading of Aquarium's PGN export (ground truth, hard-coded path) |
| `check.py` | compares all the decoded main lines with the export (30 processes) |
| `annot.py` | compares variations and comments with the export, table of symbols |
| `td0.py` | `.td0` tree and `.ti0` order: `python td0.py <database>` checks them against the games (`td0_table.bin` = substitution table extracted from `ca70db.dll`) |
| `legal.py` | replays all the games of a database (legality), without ground truth |
| `cmp_pgn.py` | compares a PGN from `hydra64-convert` with Aquarium's export |
| `learn.py` | historical: learning of the move tables by legality |
