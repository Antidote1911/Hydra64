# Texts of a ChessBase database (.cbh / .cbg)

Survey of 20 November 2026 on the Mega Database 2025 (1,740 texts) and a course database (19 texts,
in Cyrillic). The layout below was checked on the 1,759 texts: reading lands exactly on the end of
each record. Reader: `src/chessbase/cbtexts.*`.

## In the .cbh

46-byte header, then one 46-byte record per game or per text.

| Byte | Meaning |
|---|---|
| 0 | bit 0: record of the database; bit 1: text; bit 7: deleted |
| 1–4 | position of the text in the .cbg (big-endian) |

## In the .cbg

| Field | Size | Meaning |
|---|---|---|
| length | u32 big-endian | bit 31 set (it is a text); the rest: total size of the record |
| version | u16 little-endian | 1: old text; 3: HTML |
| number of titles | u16 | |
| titles | × n | language (u16), length (u16), bytes |
| ? | u8 | 0 or 1 — meaning unknown |
| number of texts | u16 | often 8 (one slot per language) |
| texts | × n | see below |

Text, version 3: language (u16), length (u32), HTML, then a block "length (u32) + bytes" (always
empty in the databases examined).

Text, version 1: language (u16), length (u16), text, then a block "length (u16) + bytes" that
carries the formatting (fonts) and the links to the games; it is not decoded. In the text: `0D`
line break, `09` tab, `04` place of a link.

Languages: 0 English, 1 German, 2 French, 3 Spanish, 4 Italian, 5 Dutch, 6 and 7 not identified.
Unused language slots contain an HTML page without text (199 or 221 bytes).

Character set: nothing indicates it in the record. The reader takes UTF-8 when the bytes are
valid, otherwise Windows-1251 if there are more bytes ≥ 0xC0 than Latin letters, otherwise
Windows-1252. It is a heuristic.

## What is not done

- `.2cbh` databases of ChessBase 18: texts not read.
- Links from the texts to the games, images and formatting of the old texts: ignored.
