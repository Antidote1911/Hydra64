//! C bridge between Hydra64 and cbvault (reading of ChessBase databases). See `cbbridge.h`.
//!
//! Everything is read-only. Every exported function catches panics: an internal
//! error becomes a message, never a shutdown of the application.

use std::ffi::{c_char, c_int, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::Path;
use std::sync::atomic::{AtomicI32, Ordering};
use std::sync::{Mutex, OnceLock, RwLock};

use cbvault::bridge::{scan_range, Database, Filter, GameBuf, IdSet, Match};
use cbvault_format::archive::Archive;
use cbvault_format::cbh::Entity;
use cbvault_format::game::GameResult;
use std::collections::HashMap;
use cbvault_format::game::RecordKind;

// The search reads the database from a thread other than the display's.
const _: fn() = || {
    fn sync<T: Send + Sync>() {}
    sync::<Database>();
};

/// Open database: the database, its games (numbers), the displayed view and the header buffers.
/// Always handled through a shared reference: the search (other thread) only reads `db` and
/// `games`, the display goes through the locks of `view` and `strings`.
pub struct CbbDb {
    db: Database,
    games: Vec<u32>,
    view: RwLock<Option<Vec<u32>>>,
    strings: Mutex<[CString; 6]>,
    /// Database with Russian comments (Windows-1251)? Decided at the first rendering, on a sample.
    cyrillic: OnceLock<bool>,
    /// Path of the database without extension; its entity files (.cbt, .cbs, .cbe, .cbc) and its
    /// extended headers (.cbj: file, record size, count), opened on demand.
    stem: std::path::PathBuf,
    entity_files: [OnceLock<Option<EntityFile>>; 4],
    extended: OnceLock<Option<(std::fs::File, u64, u32)>>,
}

/// Entity file of a classic database (.cbt tournaments, .cbs sources, .cbe teams,
/// .cbc annotators), read into memory on demand: 28-byte header + those it announces,
/// then records of 9 tree bytes + the data.
struct EntityFile {
    bytes: Vec<u8>,
    header: usize,
    data: usize,
}

impl EntityFile {
    fn open(stem: &Path, extension: &str) -> Option<EntityFile> {
        let mut path = stem.as_os_str().to_owned();
        path.push(extension);
        let bytes = std::fs::read(path).ok()?;
        let word = |at: usize| bytes.get(at..at + 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as usize);
        let (data, header) = (word(12)?, 28 + word(24)?);
        Some(EntityFile { bytes, header, data })
    }

    /// The data of entity `id`, `None` outside the file or for a deleted record.
    fn record(&self, id: u32, least: usize) -> Option<&[u8]> {
        if self.data < least {
            return None;
        }
        let at = self.header + id as usize * (9 + self.data);
        let whole = self.bytes.get(at..at + 9 + self.data)?;
        if i32::from_le_bytes([whole[0], whole[1], whole[2], whole[3]]) == -999 {
            return None;
        }
        Some(&whole[9..])
    }
}

/// Text of a fixed-length field: up to the first zero, UTF-8 if valid (recent databases),
/// otherwise Windows-1252, then Cyrillic and figurines like the other names.
fn field_text(bytes: &[u8]) -> String {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    let bytes = &bytes[..end];
    let text = match std::str::from_utf8(bytes) {
        Ok(t) => t.to_owned(),
        Err(_) => cbvault_format::codepage::CodePage::WESTERN.decode(bytes),
    };
    // Without stripping trailing spaces: ChessBase writes them ("Sailauf  ", "Yermolinsky,Alex  ").
    fixed_name(&text, false)
}

/// ChessBase date (day on 5 bits, month on 4, year above) as "YYYY.MM.DD", unknown
/// parts as "??"; empty without a year.
fn date_text(raw: u32) -> String {
    let (year, month, day) = ((raw >> 9) & 0xfff, (raw >> 5) & 15, raw & 31);
    if year == 0 {
        return String::new();
    }
    let part = |v: u32| if v == 0 { "??".to_owned() } else { format!("{v:02}") };
    format!("{year:04}.{}.{}", part(month), part(day))
}

impl CbbDb {
    fn entities(&self, slot: usize) -> Option<&EntityFile> {
        const EXTENSIONS: [&str; 4] = [".cbt", ".cbs", ".cbe", ".cbc"];
        self.entity_files[slot].get_or_init(|| EntityFile::open(&self.stem, EXTENSIONS[slot])).as_ref()
    }

    /// Teams of game `id`, read from the extended headers file (.cbj: 32-byte
    /// header, one big-endian record per game, White's team then Black's).
    fn teams(&self, id: u32) -> Option<(i32, i32)> {
        // Read at a given offset without moving the file cursor: `read_exact_at` on Unix,
        // `seek_read` (repeated until the buffer is full) on Windows.
        #[cfg(unix)]
        fn read_at(file: &std::fs::File, buf: &mut [u8], offset: u64) -> std::io::Result<()> {
            use std::os::unix::fs::FileExt;
            file.read_exact_at(buf, offset)
        }
        #[cfg(windows)]
        fn read_at(file: &std::fs::File, buf: &mut [u8], offset: u64) -> std::io::Result<()> {
            use std::os::windows::fs::FileExt;
            let mut done = 0;
            while done < buf.len() {
                let n = file.seek_read(&mut buf[done..], offset + done as u64)?;
                if n == 0 {
                    return Err(std::io::ErrorKind::UnexpectedEof.into());
                }
                done += n;
            }
            Ok(())
        }
        let (file, record, count) = self
            .extended
            .get_or_init(|| {
                let mut path = self.stem.as_os_str().to_owned();
                path.push(".cbj");
                let file = std::fs::File::open(path).ok()?;
                let mut head = [0u8; 12];
                read_at(&file, &mut head, 0).ok()?;
                let record = u32::from_le_bytes([head[4], head[5], head[6], head[7]]) as u64;
                let count = u32::from_le_bytes([head[8], head[9], head[10], head[11]]);
                (record >= 8).then_some((file, record, count))
            })
            .as_ref()?;
        if id == 0 || id > *count {
            return None;
        }
        let mut b = [0u8; 8];
        read_at(file, &mut b, 32 + u64::from(id - 1) * record).ok()?;
        Some((i32::from_be_bytes([b[0], b[1], b[2], b[3]]), i32::from_be_bytes([b[4], b[5], b[6], b[7]])))
    }

    /// The tags that ChessBase's PGN export adds to the seven basic tags, in its order, one
    /// "Name<TAB>value" line per tag. A country is given by its ChessBase number preceded by
    /// "#" (the code table is on the C++ side, `PlayerBase::nationCode`). Layouts recorded
    /// on real databases and checked against ChessBase '26 exports (21 November 2026).
    fn extra_tags(&self, id: u32) -> String {
        let mut out = String::new();
        let Ok(h) = self.db.header_ref(id) else { return out };
        let mut tag = |name: &str, value: &str| {
            if !value.is_empty() {
                out.push_str(name);
                out.push('\t');
                out.push_str(value);
                out.push('\n');
            }
        };
        let entity = |slot: usize, id: i64, least: usize| u32::try_from(id).ok().and_then(|id| self.entities(slot)?.record(id, least));
        // Site and round: already in the PGN of a readable game; they serve a game whose
        // moves cannot be read, rebuilt from its header alone. "Hy64Moves 0": the header
        // says it has no moves (ChessBase exports it empty) — this is not a tag.
        if let cbvault::bridge::GameHeaderRef::Classic(c) = &h {
            if let Some(r) = entity(0, i64::from(c.tournament()), 70) {
                tag("Site", &field_text(&r[40..70]));
            }
            if c.round() != 0 {
                tag("Round", &if c.subround() != 0 { format!("{}.{}", c.round(), c.subround()) } else { c.round().to_string() });
            }
            if c.move_count() == 0 {
                tag("Hy64Moves", "0");
            }
        }
        // Annotator (.cbc): name on 45 bytes
        if let Some(r) = entity(3, h.annotator(), 45) {
            tag("Annotator", &field_text(&r[..45]));
        }
        // Teams (.cbj → .cbe): name on 45 bytes, country at byte 54
        if let Some((white, black)) = self.teams(id) {
            // Team number (bytes 45 to 48): from 2 up, ChessBase appends it to the name in
            // Roman numerals ("Regensburg Fortuna II"). The case of number 1 has not been seen.
            let team = |t: i32| {
                entity(2, i64::from(t), 55).map(|r| {
                    let mut name = field_text(&r[..45]);
                    let number = u32::from_le_bytes([r[45], r[46], r[47], r[48]]);
                    if (2..=39).contains(&number) && !name.is_empty() {
                        name.push(' ');
                        name.push_str(&"X".repeat((number / 10) as usize));
                        name.push_str(["", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"][(number % 10) as usize]);
                    }
                    (name, r[54])
                })
            };
            let (w, b) = (team(white), team(black));
            if let Some((name, _)) = &w {
                tag("WhiteTeam", name);
            }
            if let Some((name, _)) = &b {
                tag("BlackTeam", name);
            }
            if let Some((_, nation)) = w.filter(|t| t.1 != 0) {
                tag("WhiteTeamCountry", &format!("#{nation}"));
            }
            if let Some((_, nation)) = b.filter(|t| t.1 != 0) {
                tag("BlackTeamCountry", &format!("#{nation}"));
            }
        }
        // Tournament (.cbt): title 40, site 30, date 4, type 1, teams 1, country 1, ? 1, category 1, flags 1, rounds 1
        if let Some(r) = entity(0, h.tournament(), 81) {
            tag("EventDate", &date_text(u32::from_le_bytes([r[70], r[71], r[72], r[73]])));
            tag("EventType", &event_type(r[74], r[75] & 1 != 0));
            if r[80] != 0 {
                tag("EventRounds", &r[80].to_string());
            }
            if r[76] != 0 {
                tag("EventCountry", &format!("#{}", r[76]));
            }
            if r[78] != 0 {
                tag("EventCategory", &r[78].to_string());
            }
        }
        // Source (.cbs): title 25, publisher 16, date 4, version date 4, version 1, quality 1
        if let Some(r) = entity(1, h.source(), 51) {
            tag("SourceTitle", &field_text(&r[..25]));
            tag("Source", &field_text(&r[25..41]));
            tag("SourceDate", &date_text(u32::from_le_bytes([r[41], r[42], r[43], r[44]])));
            if r[49] != 0 {
                tag("SourceVersion", &r[49].to_string());
            }
            tag("SourceVersionDate", &date_text(u32::from_le_bytes([r[45], r[46], r[47], r[48]])));
            if r[50] != 0 {
                tag("SourceQuality", &r[50].to_string());
            }
        }
        out
    }
}

/// Reader of a .2cbh entity record: strings preceded by their length (u32), in UTF-8.
struct EntityCursor<'a>(&'a [u8], usize);

impl<'a> EntityCursor<'a> {
    fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        let v = self.0.get(self.1..self.1.checked_add(n)?)?;
        self.1 += n;
        Some(v)
    }
    fn u32(&mut self) -> Option<u32> {
        self.take(4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    }
    fn u16(&mut self) -> Option<u16> {
        self.take(2).map(|b| u16::from_le_bytes([b[0], b[1]]))
    }
    fn text(&mut self) -> Option<String> {
        let n = self.u32()? as usize;
        self.take(n).map(|b| String::from_utf8_lossy(b).into_owned())
    }
}

/// Tournament type as ChessBase writes it, from the type byte and the "team" flag.
fn event_type(style: u8, by_teams: bool) -> String {
    const KINDS: [&str; 9] = ["", "game", "match", "tourn", "swiss", "team", "k.o.", "simul", "schev"];
    let kind = (style & 15) as usize;
    // "team-" in front of the type when the "team" flag is set; type 5 alone
    // is written "team", with the flag "team-tourn"; the flag without type, "team".
    // Table checked on the 105,000 tournaments of the Mega Database 2025 (no conflict).
    let mut text = match (kind, by_teams) {
        (5, true) => "team-tourn".to_owned(),
        (0, true) => "team".to_owned(),
        (k, true) if k < KINDS.len() => format!("team-{}", KINDS[k]),
        (k, false) if k < KINDS.len() => KINDS[k].to_owned(),
        _ => String::new(),
    };
    // Time control: in parentheses after the type, alone without type ("rapid", "blitz")
    for (bit, label) in [(0x20, "blitz"), (0x40, "rapid"), (0x80, "corr")] {
        if style & bit != 0 {
            if text.is_empty() {
                text.push_str(label);
            } else {
                text.push_str(&format!(" ({label})"));
            }
            break;
        }
    }
    text
}

impl CbbDb {
    /// Like [`CbbDb::extra_tags`], for a .2cbh database: everything comes from the entity file
    /// (.2lid). Layouts recorded on the Mega Database 2026 and aligned with the PGN export of
    /// ChessBase '26 (21 November 2026):
    ///   player (type 0): last name, first name, 16 bytes, FIDE id (u32); the annotator of a
    ///     game is a player ("Reeh,Oliver"), the player with an empty name = no annotator;
    ///   tournament (type 1): site, title, date, then the same seven bytes as the .cbt (type,
    ///     teams, country, ?, category, flags, rounds);
    ///   source (type 2): title, publisher, date, version date, version (u16), quality (u16);
    ///   team (type 4): name, number (u8), season (u8), year (u16), country (u8).
    fn extra_tags_v2(&self, id: u32) -> String {
        use cbvault::bridge::GameHeaderRef;
        let mut out = String::new();
        let (Some(entities), Ok(GameHeaderRef::TwoCbh(h))) = (self.db.twocbh_entities(), self.db.header_ref(id)) else { return out };
        let analysis = matches!(h.kind(), RecordKind::Analysis);
        if !analysis && !matches!(h.kind(), RecordKind::Game) {
            return out;
        }
        let mut tag = |name: &str, value: &str| {
            if !value.is_empty() {
                out.push_str(name);
                out.push('\t');
                out.push_str(value);
                out.push('\n');
            }
        };
        let raw = |kind: usize, id: i64| if id < 0 { None } else { entities.raw(kind, id).ok().flatten() };
        let source = |tag: &mut dyn FnMut(&str, &str), id: i64| {
            let Some(r) = raw(2, id) else { return };
            let mut c = EntityCursor(&r, 0);
            if let (Some(title), Some(publisher), Some(date), Some(version_date)) = (c.text(), c.text(), c.u32(), c.u32()) {
                let (version, quality) = (c.u16().unwrap_or(0), c.u16().unwrap_or(0));
                tag("SourceTitle", &title);
                tag("Source", &publisher);
                tag("SourceDate", &date_text(date));
                if version != 0 {
                    tag("SourceVersion", &version.to_string());
                }
                tag("SourceVersionDate", &date_text(version_date));
                if quality != 0 {
                    tag("SourceQuality", &quality.to_string());
                }
            }
        };
        if analysis {
            // Analysis (opening survey): the author is the player stored in place of the tournament,
            // the source in place of Black's player; neither tournament nor teams.
            if let Some(r) = raw(0, h.tournament()) {
                let mut c = EntityCursor(&r, 0);
                if let (Some(last), Some(first)) = (c.text(), c.text()) {
                    tag("Annotator", &if first.is_empty() { last } else { format!("{last},{first}") });
                }
            }
            source(&mut tag, h.black());
            return out;
        }
        // Player: (last name, first name, FIDE id)
        let player = |id: i64| {
            let r = raw(0, id)?;
            let mut c = EntityCursor(&r, 0);
            let (last, first) = (c.text()?, c.text()?);
            c.take(16)?;
            Some((last, first, c.u32().unwrap_or(0)))
        };
        if let Some((last, first, _)) = player(h.annotator()) {
            tag("Annotator", &if first.is_empty() { last } else { format!("{last},{first}") });
        }
        if let Some((_, _, fide)) = player(h.white()).filter(|p| p.2 != 0) {
            tag("WhiteFideId", &fide.to_string());
        }
        if let Some((_, _, fide)) = player(h.black()).filter(|p| p.2 != 0) {
            tag("BlackFideId", &fide.to_string());
        }
        let team = |id: i64| {
            let r = raw(4, id)?;
            let mut c = EntityCursor(&r, 0);
            let mut name = c.text()?;
            let tail = c.take(5)?;
            let number = u32::from(tail[0]);
            if (2..=39).contains(&number) && !name.is_empty() {
                name.push(' ');
                name.push_str(&"X".repeat((number / 10) as usize));
                name.push_str(["", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"][(number % 10) as usize]);
            }
            Some((name, tail[4]))
        };
        let (w, b) = (team(h.white_team()), team(h.black_team()));
        if let Some((name, _)) = &w {
            tag("WhiteTeam", name);
        }
        if let Some((name, _)) = &b {
            tag("BlackTeam", name);
        }
        if let Some((_, nation)) = w.filter(|t| t.1 != 0) {
            tag("WhiteTeamCountry", &format!("#{nation}"));
        }
        if let Some((_, nation)) = b.filter(|t| t.1 != 0) {
            tag("BlackTeamCountry", &format!("#{nation}"));
        }
        if let Some(r) = raw(1, h.tournament()) {
            let mut c = EntityCursor(&r, 0);
            if let (Some(_place), Some(_title), Some(date), Some(t)) = (c.text(), c.text(), c.u32(), c.take(7)) {
                tag("EventDate", &date_text(date));
                tag("EventType", &event_type(t[0], t[1] & 1 != 0));
                if t[6] != 0 {
                    tag("EventRounds", &t[6].to_string());
                }
                if t[2] != 0 {
                    tag("EventCountry", &format!("#{}", t[2]));
                }
                if t[4] != 0 {
                    tag("EventCategory", &t[4].to_string());
                }
            }
        }
        source(&mut tag, h.source());
        out
    }
}

/// Extra tags of row `row` (annotator, teams, tournament, source), one
/// "Name<TAB>value" line per tag. To be freed with cbb_free_string.
#[no_mangle]
pub extern "C" fn cbb_extra_tags(db: *const CbbDb, row: u32) -> *mut c_char {
    let Some(d) = (unsafe { db.as_ref() }) else { return std::ptr::null_mut() };
    guarded(std::ptr::null_mut(), 0, std::ptr::null_mut(), || {
        let id = d.row_id(row).ok_or("ligne hors de la base")?;
        let tags = if d.db.twocbh_headers().is_some() { d.extra_tags_v2(id) } else { d.extra_tags(id) };
        Ok(cstring(&tags).into_raw())
    })
}

// ---------------------------------------------------------------- Cyrillic (Windows-1251)
//
// ChessBase does not record the encoding of its texts. cbvault recognises on its own a Russian comment that is long
// enough; a short comment ("или", "и") cannot be told from a Western text. Two
// contexts therefore decide in its place: the database (majority of Russian in a sample) and the
// game (it already has recognised Russian, and still has accented letters left).

/// Cyrillic letters and accented Latin letters (or badly decoded Russian) of the moves and comments.
fn script_counts(pgn: &str) -> (usize, usize) {
    let body = pgn.find("\n\n").map_or(pgn, |at| &pgn[at..]);
    let (mut cyr, mut lat) = (0usize, 0usize);
    for c in body.chars() {
        match c {
            '\u{410}'..='\u{44f}' | 'ё' | 'Ё' => cyr += 1,
            '\u{c0}'..='\u{ff}' => lat += 1,
            _ => {}
        }
    }
    (cyr, lat)
}

/// A game rendered out of context that would gain from being reread as Russian.
fn wants_cyrillic(pgn: &str) -> bool {
    let (cyr, lat) = script_counts(pgn);
    lat > 0 && cyr >= 8 && cyr >= lat
}

/// Runs `f` with cbvault's Russian context set for this thread, then removes it.
fn with_cyrillic<T>(on: bool, f: impl FnOnce() -> T) -> T {
    cbvault::pgn::set_cyrillic_context(on);
    let r = f();
    cbvault::pgn::set_cyrillic_context(false);
    r
}

/// A name (player, tournament, site) in Windows-1251 that cbvault read as Windows-1252: at least
/// four letters 0xC0…0xFF in a row and more of those letters than Latin letters — which no
/// Western name shows ("Ðàíäåðñ" → "Рандерс"). Returned in Cyrillic, otherwise as is.
fn cyrillic_name(name: &str) -> std::borrow::Cow<'_, str> {
    let high = |c: char| ('\u{c0}'..='\u{ff}').contains(&c);
    let count = name.chars().filter(|&c| high(c)).count();
    if count < 4 || count <= name.chars().filter(char::is_ascii_alphabetic).count() {
        return name.into();
    }
    let (mut run, mut word) = (0usize, false);
    for c in name.chars() {
        run = if high(c) { run + 1 } else { 0 };
        word |= run >= 4;
    }
    if !word {
        return name.into();
    }
    name.chars()
        .map(|c| match c {
            '\u{c0}'..='\u{ff}' => char::from_u32(c as u32 - 0xc0 + 0x410).unwrap_or(c),
            '\u{a8}' => 'Ё',
            '\u{b8}' => 'ё',
            _ => c,
        })
        .collect::<String>()
        .into()
}

/// Figurines of ChessBase's font in a title ("6.¤f3", "8...£a5"): ¢ king, £ queen,
/// ¤ knight, ¥ bishop, ¦ rook. ChessBase's PGN export writes them as English letters, wherever
/// they are (even at the end of a truncated title: "5.N").
fn figurines(name: &str) -> std::borrow::Cow<'_, str> {
    if !name.chars().any(|c| ('\u{a2}'..='\u{a6}').contains(&c)) {
        return name.into();
    }
    let chars: Vec<char> = name.chars().collect();
    let mut out = String::with_capacity(name.len());
    for &c in &chars {
        let piece = match c {
            '\u{a2}' => 'K',
            '\u{a3}' => 'Q',
            '\u{a4}' => 'N',
            '\u{a5}' => 'B',
            '\u{a6}' => 'R',
            _ => c,
        };
        out.push(piece);
    }
    out.into()
}

/// A name returned by cbvault, reread: Cyrillic, then figurines for a player name only
/// (ChessBase leaves them as they are in a tournament name: "Morphy Odds ¦a1").
fn fixed_name(name: &str, player: bool) -> String {
    let name = cyrillic_name(name);
    if player { figurines(&name).into_owned() } else { name.into_owned() }
}

/// The tag values of a PGN returned by cbvault, passed through [`fixed_name`].
fn cyrillic_tags(pgn: String) -> String {
    let head = pgn.find("\n\n").unwrap_or(pgn.len());
    if !pgn[..head].chars().any(|c| ('\u{a2}'..='\u{ff}').contains(&c)) {
        return pgn;
    }
    let mut out = String::with_capacity(pgn.len() + 16);
    for line in pgn[..head].split_inclusive('\n') {
        match (line.find('"'), line.rfind('"')) {
            (Some(a), Some(b)) if line.starts_with('[') && b > a => {
                out.push_str(&line[..=a]);
                out.push_str(&fixed_name(&line[a + 1..b], line.starts_with("[White ") || line.starts_with("[Black ")));
                out.push_str(&line[b..]);
            }
            _ => out.push_str(line),
        }
    }
    out.push_str(&pgn[head..]);
    out
}

impl CbbDb {
    /// Russian database? A sample of at most 400 games spread over the database, rendered out of
    /// context: clearly more recognised Cyrillic letters than accented letters.
    fn cyrillic_base(&self) -> bool {
        *self.cyrillic.get_or_init(|| {
            cbvault::pgn::set_cyrillic_context(false);
            let step = (self.games.len() / 400).max(1);
            let mut buf = GameBuf::new();
            let (mut cyr, mut lat) = (0usize, 0usize);
            for &id in self.games.iter().step_by(step) {
                if let Ok(pgn) = self.db.game_pgn_with(id, &mut buf, true) {
                    let (c, l) = script_counts(&pgn);
                    cyr += c;
                    lat += l;
                }
            }
            cyr >= 200 && cyr >= 3 * lat
        })
    }
}

pub struct CbbIds(Vec<u32>);

#[repr(C)]
pub struct CbbHeader {
    id: u32,
    white: *const c_char,
    black: *const c_char,
    event: *const c_char,
    date: *const c_char,
    result: *const c_char,
    eco: *const c_char,
    white_elo: i32,
    black_elo: i32,
}

fn write_err(err: *mut c_char, len: usize, msg: &str) {
    if err.is_null() || len == 0 {
        return;
    }
    let bytes = msg.as_bytes();
    let mut n = bytes.len().min(len - 1);
    while n > 0 && !msg.is_char_boundary(n) {
        n -= 1;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), err.cast::<u8>(), n);
        *err.add(n) = 0;
    }
}

fn path_of(p: *const c_char) -> Option<String> {
    if p.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(p) }.to_str().ok().map(str::to_owned)
}

/// Runs `f` catching a panic (message in `err`, value `fallback`).
fn guarded<T>(err: *mut c_char, len: usize, fallback: T, f: impl FnOnce() -> Result<T, String>) -> T {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(v)) => v,
        Ok(Err(msg)) => {
            write_err(err, len, &msg);
            fallback
        }
        Err(_) => {
            write_err(err, len, "erreur interne de lecture ChessBase");
            fallback
        }
    }
}

fn cstring(s: &str) -> CString {
    CString::new(s.replace('\0', " ")).unwrap_or_default()
}

impl CbbDb {
    fn row_count(&self) -> usize {
        self.view.read().map_or(0, |v| v.as_ref().map_or(self.games.len(), Vec::len))
    }
    fn row_id(&self, row: u32) -> Option<u32> {
        let v = self.view.read().ok()?;
        v.as_deref().unwrap_or(&self.games).get(row as usize).copied()
    }
}

#[no_mangle]
pub extern "C" fn cbb_open(path: *const c_char, err: *mut c_char, errlen: usize) -> *mut CbbDb {
    guarded(err, errlen, std::ptr::null_mut(), || {
        let path = path_of(path).ok_or("chemin invalide")?;
        let db = Database::open(Path::new(&path)).map_err(|e| e.to_string())?;
        // The rows of the list: the games, without the texts or the deleted games.
        let mut games = Vec::with_capacity(db.records() as usize);
        for id in 1..=db.records() {
            if let Ok(h) = db.header_ref(id) {
                // The "analyses" of .2cbh databases (opening surveys of ChessBase Magazine, 406
                // in the Mega Database 2026) are games like any other: moves, variations,
                // annotator. ChessBase exports them, with "?" for tournament and players.
                if matches!(h.kind(), RecordKind::Game | RecordKind::Analysis) && !h.is_deleted() {
                    games.push(id);
                }
            }
        }
        Ok(Box::into_raw(Box::new(CbbDb {
            db,
            games,
            view: RwLock::new(None),
            strings: Mutex::new(Default::default()),
            cyrillic: OnceLock::new(),
            stem: cbvault::stem_of(Path::new(&path)),
            entity_files: Default::default(),
            extended: OnceLock::new(),
        })))
    })
}

#[no_mangle]
pub extern "C" fn cbb_close(db: *mut CbbDb) {
    if !db.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(db) })));
    }
    // Check: census of the annotations encountered (CBB_CENSUS environment variable)
    if std::env::var_os("CBB_CENSUS").is_some() {
        for (code, count, position, sample) in cbvault::pgn::census::report() {
            eprintln!("CENSUS {code:02x} {count} pos={position} {}", sample.iter().map(|b| format!("{b:02x}")).collect::<String>());
        }
    }
}

#[no_mangle]
pub extern "C" fn cbb_total(db: *const CbbDb) -> u32 {
    unsafe { db.as_ref() }.map_or(0, |d| d.games.len() as u32)
}

#[no_mangle]
pub extern "C" fn cbb_count(db: *const CbbDb) -> u32 {
    unsafe { db.as_ref() }.map_or(0, |d| d.row_count() as u32)
}

#[no_mangle]
pub extern "C" fn cbb_is_twocbh(db: *const CbbDb) -> c_int {
    unsafe { db.as_ref() }.map_or(0, |d| c_int::from(d.db.twocbh_headers().is_some()))
}

#[no_mangle]
pub extern "C" fn cbb_header(db: *mut CbbDb, row: u32, out: *mut CbbHeader) -> c_int {
    let (Some(d), Some(out)) = (unsafe { db.as_ref() }, unsafe { out.as_mut() }) else { return 0 };
    guarded(std::ptr::null_mut(), 0, 0, || {
        let id = d.row_id(row).ok_or("ligne hors de la base")?;
        // A range of a single game: header and names, whatever the format.
        let scan = scan_range(&d.db, id, id, &Filter::All, 1).map_err(|e| e.to_string())?;
        let m = scan.matches().first().ok_or("partie illisible")?;
        let name = |n: &dyn Fn(&mut String) -> bool| {
            let mut s = String::new();
            n(&mut s);
            s
        };
        let white = fixed_name(&name(&|s| Match::player(&m.white, s)), true);
        let black = fixed_name(&name(&|s| Match::player(&m.black, s)), true);
        let event = fixed_name(&name(&|s| Match::player(&m.event, s)), false);
        let h = &m.header;
        let date = h.played_date().pgn();
        let result = h.result().pgn().to_owned();
        let eco = h.eco().pgn().unwrap_or_default();
        let mut strings = d.strings.lock().map_err(|_| "verrou des en-têtes")?;
        *strings = [cstring(&white), cstring(&black), cstring(&event), cstring(&date), cstring(&result), cstring(&eco)];
        *out = CbbHeader {
            id,
            white: strings[0].as_ptr(),
            black: strings[1].as_ptr(),
            event: strings[2].as_ptr(),
            date: strings[3].as_ptr(),
            result: strings[4].as_ptr(),
            eco: strings[5].as_ptr(),
            white_elo: i32::from(h.white_elo()),
            black_elo: i32::from(h.black_elo()),
        };
        Ok(1)
    })
}

#[no_mangle]
pub extern "C" fn cbb_game_pgn(db: *const CbbDb, row: u32, err: *mut c_char, errlen: usize) -> *mut c_char {
    let Some(d) = (unsafe { db.as_ref() }) else { return std::ptr::null_mut() };
    guarded(err, errlen, std::ptr::null_mut(), || {
        let id = d.row_id(row).ok_or("ligne hors de la base")?;
        let mut buf = GameBuf::new();
        let pgn = game_pgn_in_context(d, id, &mut buf)?;
        Ok(cstring(&pgn).into_raw())
    })
}

/// PGN of a game. If its annotations are unreadable (damaged annotation file, or a kind of
/// annotation the library does not know), the game is not lost for all that: its
/// headers and moves are returned without annotations, with a leading comment saying so.
/// The original error is returned only if the moves themselves are unreadable.
fn game_pgn_tolerant(db: &Database, id: u32, buf: &mut GameBuf) -> Result<String, String> {
    let first = match db.game_pgn_with(id, buf, true) {
        Ok(pgn) => return Ok(pgn),
        Err(e) => e.to_string(),
    };
    let Ok(plain) = db.game_pgn_with(id, buf, false) else { return Err(first) };
    // Short reason: the end of the message, without the file paths
    let why = first.rsplit(": ").next().unwrap_or("").replace(['{', '}'], "");
    let note = format!("{{Annotations illisibles dans la base ChessBase ({why}) : coups conservés, commentaires et variantes annotées perdus.}} ");
    Ok(match plain.find("\n\n") {
        Some(at) => format!("{}{}{}", &plain[..at + 2], note, &plain[at + 2..]),
        None => format!("{note}{plain}"),
    })
}

/// `game_pgn_tolerant` in the right context: Russian for a Russian database, or for a game that
/// already has recognised Russian and still has accented letters; tag names reread as Cyrillic.
fn game_pgn_in_context(d: &CbbDb, id: u32, buf: &mut GameBuf) -> Result<String, String> {
    let base = d.cyrillic_base();
    let mut pgn = with_cyrillic(base, || game_pgn_tolerant(&d.db, id, buf))?;
    if !base && wants_cyrillic(&pgn) {
        pgn = with_cyrillic(true, || game_pgn_tolerant(&d.db, id, buf))?;
    }
    Ok(cyrillic_tags(pgn))
}

/// Read buffers of a game, kept per thread (conversion reads in parallel).
#[derive(Default)]
struct Scratch {
    buf: GameBuf,
    writer: cbvault::pgn::PgnWriter,
    out: Vec<u8>,
    moves: Vec<u16>,
}

thread_local! {
    static SCRATCH: std::cell::RefCell<Option<Scratch>> = const { std::cell::RefCell::new(None) };
}

/// Like `cbb_game_pgn`, for conversion: a game with neither annotation nor variation comes back
/// with its tags alone and its main line in binary (no notation is computed).
#[no_mangle]
pub extern "C" fn cbb_game_pgn_moves(
    db: *const CbbDb,
    row: u32,
    moves: *mut u16,
    capacity: u32,
    count: *mut u32,
    err: *mut c_char,
    errlen: usize,
) -> *mut c_char {
    let Some(d) = (unsafe { db.as_ref() }) else { return std::ptr::null_mut() };
    if let Some(c) = unsafe { count.as_mut() } {
        *c = u32::MAX;
    }
    guarded(err, errlen, std::ptr::null_mut(), || {
        let id = d.row_id(row).ok_or("ligne hors de la base")?;
        SCRATCH.with(|cell| {
            let mut slot = cell.borrow_mut();
            let s = slot.get_or_insert_with(|| Scratch { writer: cbvault::pgn::PgnWriter::new(), ..Default::default() });
            if !moves.is_null() && !count.is_null() {
                // Unreadable annotations: the lenient reading, further down, keeps the moves
                let base = d.cyrillic_base();
                let mut rendered = with_cyrillic(base, || d.db.game_for_encoding(id, &mut s.buf, &mut s.writer, &mut s.out, &mut s.moves));
                // Annotated game, rendered out of context, that has Russian and still has accented letters
                if !base && matches!(rendered, Ok(false)) && std::str::from_utf8(&s.out).is_ok_and(wants_cyrillic) {
                    rendered = with_cyrillic(true, || d.db.game_for_encoding(id, &mut s.buf, &mut s.writer, &mut s.out, &mut s.moves));
                }
                let plain = match rendered {
                    Ok(plain) => plain,
                    Err(_) => {
                        let pgn = game_pgn_in_context(d, id, &mut s.buf)?;
                        return Ok(cstring(&pgn).into_raw());
                    }
                };
                if !plain || s.moves.len() <= capacity as usize {
                    if plain {
                        unsafe {
                            std::ptr::copy_nonoverlapping(s.moves.as_ptr(), moves, s.moves.len());
                            *count = s.moves.len() as u32;
                        }
                    }
                    let text = std::str::from_utf8(&s.out).map_err(|e| e.to_string())?;
                    if text.is_ascii() {
                        return Ok(cstring(text).into_raw()); // common case: nothing to reread, no copy
                    }
                    return Ok(cstring(&cyrillic_tags(text.to_owned())).into_raw());
                }
                // Main line longer than the caller's buffer: full PGN.
            }
            let pgn = game_pgn_in_context(d, id, &mut s.buf)?;
            Ok(cstring(&pgn).into_raw())
        })
    })
}

#[no_mangle]
pub extern "C" fn cbb_free_string(s: *mut c_char) {
    if !s.is_null() {
        drop(unsafe { CString::from_raw(s) });
    }
}

fn lower(s: &str) -> String {
    s.to_lowercase()
}

#[no_mangle]
pub extern "C" fn cbb_search(
    db: *const CbbDb,
    text: *const c_char,
    cancel: *const i32,
    err: *mut c_char,
    errlen: usize,
) -> *mut CbbIds {
    let Some(d) = (unsafe { db.as_ref() }) else { return std::ptr::null_mut() };
    let cancelled = || !cancel.is_null() && unsafe { (*(cancel as *const AtomicI32)).load(Ordering::Relaxed) } != 0;
    guarded(err, errlen, std::ptr::null_mut(), || {
        let text = path_of(text).unwrap_or_default();
        let text = text.trim();
        if text.is_empty() {
            return Ok(Box::into_raw(Box::new(CbbIds(d.games.clone()))));
        }
        let wanted = lower(text);
        let mut found = Vec::new();
        let last = d.db.records();
        const CHUNK: u32 = 65_536;

        if let Ok(entities) = d.db.try_classic_entities() {
            // Classic format: names → numbers in the name indexes, then scan of the
            // headers (integer comparison, several threads).
            let players = entities.find_players_prefix(text).map_err(|e| e.to_string())?;
            let tournaments = entities.find_tournaments_prefix(text).map_err(|e| e.to_string())?;
            if players.is_empty() && tournaments.is_empty() {
                return Ok(Box::into_raw(Box::new(CbbIds(Vec::new()))));
            }
            let mut filter = if players.is_empty() { None } else { Some(Filter::PlayerSet(IdSet::from_ids(players))) };
            for t in tournaments.into_iter().take(4096) {
                let f = Filter::Tournament(t);
                filter = Some(match filter {
                    Some(prev) => prev.or(f),
                    None => f,
                });
            }
            let filter = filter.unwrap_or(Filter::All);
            let threads = std::thread::available_parallelism().map_or(4, |n| n.get().min(8));
            let mut first = 1;
            while first <= last {
                if cancelled() {
                    return Ok(std::ptr::null_mut());
                }
                let end = first.saturating_add(CHUNK - 1).min(last);
                let scan = scan_range(&d.db, first, end, &filter, threads).map_err(|e| e.to_string())?;
                found.extend(scan.matches().iter().map(|m| m.id));
                first = end + 1;
            }
        } else {
            // ChessBase 18 format: no usable name index, we compare the names
            // of each game (slower, but complete).
            let starts = |name: &str| {
                let n = lower(name);
                n.starts_with(&wanted) || n.split([',', ' ']).any(|part| part.starts_with(&wanted))
            };
            let mut first = 1;
            let mut s = String::new();
            while first <= last {
                if cancelled() {
                    return Ok(std::ptr::null_mut());
                }
                let end = first.saturating_add(CHUNK - 1).min(last);
                let scan = scan_range(&d.db, first, end, &Filter::All, 4).map_err(|e| e.to_string())?;
                for m in scan.matches() {
                    let mut hit = false;
                    for n in [&m.white, &m.black, &m.event] {
                        s.clear();
                        Match::player(n, &mut s);
                        if starts(&s) {
                            hit = true;
                            break;
                        }
                    }
                    if hit {
                        found.push(m.id);
                    }
                }
                first = end + 1;
            }
        }
        Ok(Box::into_raw(Box::new(CbbIds(found))))
    })
}

#[no_mangle]
pub extern "C" fn cbb_ids_count(ids: *const CbbIds) -> u32 {
    unsafe { ids.as_ref() }.map_or(0, |i| i.0.len() as u32)
}

#[no_mangle]
pub extern "C" fn cbb_free_ids(ids: *mut CbbIds) {
    if !ids.is_null() {
        drop(unsafe { Box::from_raw(ids) });
    }
}

#[no_mangle]
pub extern "C" fn cbb_set_view(db: *mut CbbDb, ids: *mut CbbIds) {
    let Some(d) = (unsafe { db.as_ref() }) else {
        cbb_free_ids(ids);
        return;
    };
    let view = if ids.is_null() { None } else { Some(unsafe { Box::from_raw(ids) }.0) };
    if let Ok(mut v) = d.view.write() {
        *v = view;
    }
}

#[no_mangle]
pub extern "C" fn cbb_view_ids(db: *const CbbDb, all: c_int) -> *mut CbbIds {
    let Some(d) = (unsafe { db.as_ref() }) else { return std::ptr::null_mut() };
    let ids = if all != 0 {
        d.games.clone()
    } else {
        d.view.read().map(|v| v.as_ref().unwrap_or(&d.games).clone()).unwrap_or_default()
    };
    Box::into_raw(Box::new(CbbIds(ids)))
}

/// Name of a player ("Last, First") or of a tournament, whatever the format.
fn entity_name(db: &Database, tournament: bool, id: i64) -> String {
    if id < 0 {
        return String::new();
    }
    if let Ok(entities) = db.try_classic_entities() {
        let kind = if tournament { Entity::Tournament } else { Entity::Player };
        return match entities.name(kind, id as u32) {
            Ok(Some(n)) if tournament => String::from_utf8_lossy(n.last()).into_owned(),
            Ok(Some(n)) => {
                let mut s = String::new();
                n.push_text(&mut s);
                s
            }
            _ => String::new(),
        };
    }
    let Some(entities) = db.twocbh_entities() else { return String::new() };
    if tournament {
        entities.tournament(id).ok().flatten().map(|t| t.title).unwrap_or_default()
    } else {
        entities.player(id).ok().flatten().map(|p| p.pgn()).unwrap_or_default()
    }
}

#[no_mangle]
pub extern "C" fn cbb_sort_ids(
    db: *const CbbDb,
    ids: *mut CbbIds,
    column: i32,
    ascending: i32,
    cancel: *const i32,
    err: *mut c_char,
    errlen: usize,
) -> c_int {
    let (Some(d), Some(ids)) = (unsafe { db.as_ref() }, unsafe { ids.as_mut() }) else { return 0 };
    let cancelled = || !cancel.is_null() && unsafe { (*(cancel as *const AtomicI32)).load(Ordering::Relaxed) } != 0;
    guarded(err, errlen, 0, || {
        let ascending = ascending != 0;
        // Key of each game: (unknown, value). The unknown ones end the list.
        let mut keyed: Vec<(bool, u64, u32)> = Vec::with_capacity(ids.0.len());
        let names = matches!(column, 1 | 3 | 7);
        if names {
            // Alphabetical rank of each player or tournament present, then sort on that rank.
            let tournament = column == 7;
            let mut entity_of = Vec::with_capacity(ids.0.len());
            let mut distinct: HashMap<i64, u32> = HashMap::new();
            for (n, &id) in ids.0.iter().enumerate() {
                if n % (1 << 20) == 0 && cancelled() {
                    return Ok(0);
                }
                let h = d.db.header_ref(id).map_err(|e| e.to_string())?;
                let e = match column {
                    1 => h.white(),
                    3 => h.black(),
                    _ => h.tournament(),
                };
                entity_of.push(e);
                distinct.entry(e).or_insert(0);
            }
            let mut named: Vec<(String, i64)> =
                distinct.keys().map(|&e| (entity_name(&d.db, tournament, e).to_lowercase(), e)).collect();
            if cancelled() {
                return Ok(0);
            }
            named.sort_unstable();
            let mut blank = std::collections::HashSet::new();
            for (rank, (name, e)) in named.iter().enumerate() {
                distinct.insert(*e, rank as u32);
                if name.trim().is_empty() || name == "?" {
                    blank.insert(*e);
                }
            }
            for (&id, e) in ids.0.iter().zip(entity_of) {
                keyed.push((blank.contains(&e), u64::from(distinct[&e]), id));
            }
        } else {
            for (n, &id) in ids.0.iter().enumerate() {
                if n % (1 << 20) == 0 && cancelled() {
                    return Ok(0);
                }
                let h = d.db.header_ref(id).map_err(|e| e.to_string())?;
                let (unknown, key) = match column {
                    2 => (h.white_elo() == 0, u64::from(h.white_elo())),
                    4 => (h.black_elo() == 0, u64::from(h.black_elo())),
                    5 => match h.result() {
                        GameResult::WhiteWins | GameResult::WhiteWinsForfeit => (false, 0),
                        GameResult::Draw | GameResult::DrawForfeit => (false, 1),
                        GameResult::BlackWins | GameResult::BlackWinsForfeit => (false, 2),
                        _ => (true, 3),
                    },
                    6 => {
                        let date = h.played_date();
                        (date.year() == 0, date.0 as u32 as u64)
                    }
                    8 => {
                        // Without an A00–E99 code (missing, Chess960, invalid): unknown.
                        let eco = h.eco();
                        (eco.code_text().is_none(), u64::from(eco.field()))
                    }
                    _ => (false, u64::from(id)),
                };
                keyed.push((unknown, key, id));
            }
        }
        if cancelled() {
            return Ok(0);
        }
        keyed.sort_unstable_by(|a, b| {
            a.0.cmp(&b.0)
                .then_with(|| if ascending { a.1.cmp(&b.1) } else { b.1.cmp(&a.1) })
                .then_with(|| a.2.cmp(&b.2))
        });
        ids.0 = keyed.into_iter().map(|k| k.2).collect();
        Ok(1)
    })
}

#[no_mangle]
pub extern "C" fn cbb_extract_archive(archive: *const c_char, dir: *const c_char, err: *mut c_char, errlen: usize) -> c_int {
    guarded(err, errlen, 0, || {
        let archive = path_of(archive).ok_or("chemin d'archive invalide")?;
        let dir = path_of(dir).ok_or("dossier invalide")?;
        std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
        let a = Archive::open(&archive).map_err(|e| e.to_string())?;
        a.extract_parallel(&dir, 4).map_err(|e| e.to_string())?;
        Ok(1)
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn err_text(buf: &[u8]) -> String {
        let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
        String::from_utf8_lossy(&buf[..end]).into_owned()
    }

    #[test]
    fn missing_database_is_an_error_not_a_crash() {
        let mut err = [0u8; 256];
        let path = CString::new("/nonexistent/none.cbh").unwrap();
        let db = cbb_open(path.as_ptr(), err.as_mut_ptr().cast(), err.len());
        assert!(db.is_null());
        assert!(!err_text(&err).is_empty());
    }

    #[test]
    fn null_handles_are_harmless() {
        assert_eq!(cbb_total(std::ptr::null()), 0);
        assert_eq!(cbb_count(std::ptr::null()), 0);
        cbb_close(std::ptr::null_mut());
        cbb_free_string(std::ptr::null_mut());
        cbb_free_ids(std::ptr::null_mut());
        assert!(cbb_game_pgn(std::ptr::null(), 0, std::ptr::null_mut(), 0).is_null());
    }

    #[test]
    fn error_messages_are_truncated_on_char_boundaries() {
        let mut err = [0xffu8; 5];
        write_err(err.as_mut_ptr().cast(), err.len(), "éléphant");
        assert_eq!(err_text(&err), "él");
    }

    #[test]
    fn missing_archive_is_an_error() {
        let mut err = [0u8; 256];
        let a = CString::new("/nonexistent/x.cbv").unwrap();
        let d = CString::new(std::env::temp_dir().join("cbb-none").to_string_lossy().as_ref()).unwrap();
        assert_eq!(cbb_extract_archive(a.as_ptr(), d.as_ptr(), err.as_mut_ptr().cast(), err.len()), 0);
        assert!(!err_text(&err).is_empty());
    }
}
