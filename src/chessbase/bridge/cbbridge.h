/* Hydra64 — reading of ChessBase databases, C interface of the Rust bridge (src/chessbase/bridge).
 * All strings are UTF-8. Functions that fail write a message into
 * `err` (truncated to `errlen`, always zero-terminated). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CbbDb CbbDb;   /* open database (with its view: all the games or a search result) */
typedef struct CbbIds CbbIds; /* search result, to install with cbb_set_view or to free */

typedef struct CbbHeader {
    uint32_t id; /* number of the game in the ChessBase database (1, 2, …) */
    const char* white;
    const char* black;
    const char* event;
    const char* date;   /* "YYYY.MM.DD", "?" for unknown */
    const char* result; /* « 1-0 », « 0-1 », « 1/2-1/2 », « * » */
    const char* eco;    /* "B90" or empty */
    int32_t white_elo;  /* 0: unknown */
    int32_t black_elo;
} CbbHeader;

/* Opens a database (.cbh, .2cbh or database name). NULL on failure. */
CbbDb* cbb_open(const char* path, char* err, size_t errlen);
void cbb_close(CbbDb* db);

uint32_t cbb_total(const CbbDb* db); /* games of the database (texts and deleted games excluded) */
uint32_t cbb_count(const CbbDb* db); /* rows of the current view */
int cbb_is_twocbh(const CbbDb* db);  /* 1: ChessBase 18 format (.2cbh) */

/* Header of row `row` (0…count-1) of the view. The strings stay valid until the
 * next call of cbb_header on the same database. 0 on failure. */
int cbb_header(CbbDb* db, uint32_t row, CbbHeader* out);


/* Tags that ChessBase's PGN export adds to the seven basic tags, for row `row`: annotator,
 * teams, tournament (date, type, rounds, country, category), source. One "Name<TAB>value" line per
 * tag; a country is given by its ChessBase number preceded by "#" (see PlayerBase::nationCode).
 * .cbh and .2cbh databases (the latter also gives WhiteFideId and BlackFideId). To be freed with
 * cbb_free_string; NULL on failure. */
char* cbb_extra_tags(const CbbDb* db, uint32_t row);

/* Full game (headers, moves, variations, comments) as PGN; to be freed with
 * cbb_free_string. NULL on failure. */
char* cbb_game_pgn(const CbbDb* db, uint32_t row, char* err, size_t errlen);
void cbb_free_string(char* s);
/* Like cbb_game_pgn, for conversion: if the game has neither annotation nor variation, the text
 * returned only contains the tags, and its main line is written into `moves` (at most
 * `capacity` 16-bit words: from | to << 6 | promotion << 12; 0xFFFF = null move;
 * castling = the king onto its rook's square), `*count` receiving their number. Otherwise (or if the line
 * exceeds `capacity`) `*count` is UINT32_MAX and the text is the full PGN. */
char* cbb_game_pgn_moves(const CbbDb* db, uint32_t row, uint16_t* moves, uint32_t capacity, uint32_t* count, char* err, size_t errlen);

/* Search: games in which a player or the tournament starts with `text` (family name or
 * title, case-insensitive). Can run in a thread other than the display's
 * (read-only access to the database); a non-null `cancel` interrupts it (returns NULL, empty err).
 * NULL and `err` filled on failure. */
CbbIds* cbb_search(const CbbDb* db, const char* text, const volatile int32_t* cancel, char* err, size_t errlen);
uint32_t cbb_ids_count(const CbbIds* ids);
void cbb_free_ids(CbbIds* ids);
/* Installs the result as the view (the database takes ownership); NULL: the whole database. */
void cbb_set_view(CbbDb* db, CbbIds* ids);

/* Copy of the games of the current view (all = 0) or of the whole database (all = 1), to be sorted. */
CbbIds* cbb_view_ids(const CbbDb* db, int all);
/* Sorts `ids` by a column: 0 number, 1 white, 2 white Elo, 3 black, 4 black Elo,
 * 5 result, 6 date, 7 tournament, 8 ECO. Unknown values (empty Elo, date, ECO, name)
 * stay at the end of the list in both directions; on ties, database order. Like
 * cbb_search: read-only, usable from another thread, `cancel` interrupts it.
 * 1 if sorted, 0 if cancelled or failed (`err`). */
int cbb_sort_ids(const CbbDb* db, CbbIds* ids, int32_t column, int32_t ascending, const volatile int32_t* cancel, char* err, size_t errlen);

/* Extracts a .cbv archive into `dir` (created if needed). 1 on success. */
int cbb_extract_archive(const char* archive, const char* dir, char* err, size_t errlen);

#ifdef __cplusplus
}
#endif
