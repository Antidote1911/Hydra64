//! A game's annotations as the reading form writes them: comments, NAGs and
//! `[%csl]` / `[%cal]` graphics, placed by position in stored order — the
//! classic format's numbering, which is what annotation positions count in.
//!
//! The reading form follows ChessBase's own export, which the gold
//! comparison holds it to: one comment around a move's medals, graphics,
//! texts and quotation; `[%evp …]` first in the game's comment; a text meant
//! to precede its move before the move's number; annotations past the game's
//! last move after the main line's last move; NAGs as `$n` between the SAN
//! and the comment. The texts of one language (the game's first stored, or
//! English when it has it) plus those meant for any language.
//!
//! Ported from `cbformat`'s `pgn/comments.rs` and the reading-form parts of
//! `pgn/commands.rs` (MIT, `oschess-cb-bridge` @ `ca9e8f8e`), re-based on the
//! borrowed annotation model: every lookup re-reads the record in place and
//! every comment part goes into the writer's reused buffers, so an annotated
//! game allocates nothing after the first. See `docs/provenance.md`.

use std::fmt::Write as _;

use cbvault_format::cbh::annotations::GameAnnotations;
use cbvault_format::codepage::CodePage;
use cbvault_format::error::{Error, Result};
use cbvault_format::game::annotations::timing::Evaluation;
use cbvault_format::game::annotations::{Annotation, GAME_POSITION, Quotation, arrows, language, squares, timing};

/// Either a classic `.cba` or 2CBH `.2cba` annotation record.
#[derive(Clone, Copy)]
pub(super) enum AnnotationsRef<'a> {
    /// Classic `.cbh`/`.cba` record.
    Classic(&'a GameAnnotations<'a>),
    /// 2CBH `.2cba` record.
    TwoCbh(&'a cbvault_format::twocbh::Annotations<'a>),
}

impl<'a> AnnotationsRef<'a> {
    /// Returns the annotation at byte offset within the record.
    #[inline]
    pub(super) fn annotation_at(&self, offset: u32) -> Result<Annotation<'a>> {
        match self {
            Self::Classic(a) => a.annotation_at(offset),
            Self::TwoCbh(a) => a.annotation_at(offset),
        }
    }

    /// Whether this is classic CBH.
    #[inline]
    pub(super) fn is_classic(&self) -> bool {
        matches!(self, Self::Classic(_))
    }
}

/// Where a move stands: its index in stored order and PGN emission order.
#[derive(Clone, Copy)]
pub(super) struct At {
    /// The move's stored index (classic CBH annotation indexing).
    pub(super) stored: u32,
    /// The move's PGN emission index (2CBH annotation indexing).
    pub(super) pgn: u32,
}

/// What the PGN writes around a move: the note before its number and the
/// notes after its SAN. Says whether a comment was written — a black move
/// after one repeats its number.
pub(super) trait Notes {
    /// Writes what precedes the move's number.
    fn before(&mut self, at: At, out: &mut String) -> Result<bool>;
    /// Writes what follows the move's SAN.
    fn after(&mut self, at: At, out: &mut String) -> Result<bool>;
}

/// No annotations.
pub(super) struct Bare;

impl Notes for Bare {
    #[inline]
    fn before(&mut self, _at: At, _out: &mut String) -> Result<bool> {
        Ok(false)
    }
    #[inline]
    fn after(&mut self, _at: At, _out: &mut String) -> Result<bool> {
        Ok(false)
    }
}

/// One game's annotations for the writer: the record, the writer's index over
/// it, the game's `[%evp …]`, and the parts buffer each comment is built in.
/// Built per game from the [`super::PgnWriter`]'s own buffers, so nothing is
/// allocated per game.
pub(super) struct Commentary<'a> {
    /// The game's parsed record.
    pub(super) anns: AnnotationsRef<'a>,
    /// (position, byte offset) of every item, ascending.
    pub(super) index: &'a [(i32, u32)],
    /// The game comment's `[%evp …]`, empty when the game has none.
    pub(super) evp: &'a str,
    /// The parts of the comment being written.
    pub(super) parts: &'a mut Parts,
    /// The game's moves, all lines counted, as the walk reported them.
    pub(super) moves: u32,
    /// The main line's last move in stored order, `None` for no moves.
    pub(super) last: Option<u32>,
}

/// How a comment part follows the one before it, as ChessBase's own export
/// writes the separators: its annotation tokens follow each other with nothing
/// between them (`[%csl Ge5][%cal Gh8h7][%mdl 32]`), a text follows a space,
/// and a quoted game follows the `;` (`[%csl Gd5];0-1 Alapin,S-Caro,H Berlin`,
/// 384 of the Mega's export). The part written first takes no separator.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(super) enum Sep {
    /// Nothing between the parts: the export's tokens (`[%csl]`, `[%cal]`,
    /// `[%mdl]`, `[%evp]`, and `[%eval]`).
    #[default]
    None,
    /// A space: a text, and the `[%emt]` after an `[%eval]`.
    Space,
    /// The `;` a quoted game follows.
    Semicolon,
}

/// One part of a comment: its separator from the part before it and its text.
#[derive(Debug, Default)]
pub(super) struct Part {
    /// The separator this part takes when it is not the comment's first.
    pub(super) sep: Sep,
    /// The part's text.
    pub(super) text: String,
    /// Keep the part's whitespace runs as they stand (a quoted game, whose
    /// fields can carry a trailing space that its own text needs).
    pub(super) raw: bool,
    /// Whether this part is a machine-generated token ([%cal], [%csl], [%mdl],
    /// [%eval], [%emt], [%evp]) rather than a human-written text annotation.
    pub(super) token: bool,
}

/// The parts of the comment being written, reused across comments and games:
/// the parts are written by index and the length is ours, so a part's buffer
/// is never dropped and re-allocated — a comment costs no allocation however
/// many comments and games have gone through before it.
#[derive(Debug, Default)]
pub(super) struct Parts {
    items: Vec<Part>,
    len: usize,
}

impl Parts {
    /// Starts a new comment: the parts are reused, the length is not.
    #[inline]
    pub(super) fn clear(&mut self) {
        self.len = 0;
    }

    /// The parts of the comment being written.
    #[inline]
    pub(super) fn as_slice(&self) -> &[Part] {
        &self.items[..self.len]
    }

    /// A new part with `sep` and an empty text, ready to be written into: the
    /// buffer of a part used earlier is reused.
    #[inline]
    pub(super) fn push(&mut self, sep: Sep) -> &mut Part {
        if self.len < self.items.len() {
            let part = &mut self.items[self.len];
            part.sep = sep;
            part.text.clear();
            part.raw = false;
            part.token = false;
        } else {
            self.items.push(Part { sep, text: String::new(), raw: false, token: false });
        }
        self.len += 1;
        let at = self.len - 1;
        &mut self.items[at]
    }

    /// Drops the part at `at`, moving the last one into its place so that no
    /// buffer is freed.
    #[inline]
    pub(super) fn remove(&mut self, at: usize) {
        let last = self.len - 1;
        if at != last {
            self.items.swap(at, last);
        }
        self.len = last;
    }

    /// The part at `at` of the comment being written.
    #[inline]
    pub(super) fn get_mut(&mut self, at: usize) -> &mut Part {
        &mut self.items[at]
    }

    /// The part before the one being written.
    #[inline]
    pub(super) fn last_mut(&mut self) -> &mut Part {
        &mut self.items[self.len - 1]
    }

    /// The bytes the parts keep for reuse (their high-water mark).
    pub(super) fn capacity(&self) -> usize {
        self.items.capacity() * size_of::<Part>() + self.items.iter().map(|p| p.text.capacity()).sum::<usize>()
    }
}

impl Commentary<'_> {
    /// The game's own comment, before the first move, as ChessBase's export
    /// writes it: the position −1 medals and graphics first, then the
    /// `[%evp …]` (joined to them), then the texts and quotations — all in the
    /// one comment (`{[%mdl 1][%evp 0,109,25,…] Conditions: …}`).
    pub(super) fn game_comment(&mut self, out: &mut String) -> Result<()> {
        let (anns, index) = (self.anns, self.index);
        census::count(anns, index);
        time_tags(out, anns, range(index, GAME_POSITION))?;
        self.parts.clear();
        let items = [range(index, GAME_POSITION), &[][..]];
        graphics(self.parts, anns, items)?;
        medals(self.parts, anns, items)?;
        if !self.evp.is_empty() {
            let part = self.parts.push(Sep::None);
            part.token = true;
            part.text.push_str(self.evp);
        }
        texts(self.parts, anns, items, false)?;
        comment(out, self.parts, "", " ");
        Ok(())
    }
}

impl Notes for Commentary<'_> {
    fn before(&mut self, at: At, out: &mut String) -> Result<bool> {
        let (anns, index) = (self.anns, self.index);
        let move_idx = if anns.is_classic() { at.stored } else { at.pgn } as i32;
        self.parts.clear();
        texts(self.parts, anns, [range(index, move_idx), &[][..]], true)?;
        // The comment trails a space: it lands before the move's number.
        let wrote = comment(out, self.parts, "", " ");
        // Hydra64 : la question d'entraînement du coup, dans son propre commentaire, après le
        // texte d'avant-coup et juste devant le coup à trouver, comme l'écrit ChessBase.
        let asked = training(out, anns, range(index, move_idx))?;
        Ok(wrote || asked)
    }

    fn after(&mut self, at: At, out: &mut String) -> Result<bool> {
        let (anns, index, moves, last) = (self.anns, self.index, self.moves, self.last);
        let move_idx = if anns.is_classic() { at.stored } else { at.pgn } as i32;
        let own = range(index, move_idx);
        // The annotations past the game's last move follow the main line's
        // last move, whatever their kind (asavis/oschess-cb-bridge#38).
        let past: &[(i32, u32)] = if last == Some(at.stored) { past_range(index, moves) } else { &[] };
        let items = [own, past];

        // The NAGs first, straight into the movetext: `6. Nxf7 $1 {comment}`.
        nags(out, anns, items)?;

        self.parts.clear();
        evaluations(self.parts, anns, items)?;
        times(self.parts, anns, items)?;
        graphics(self.parts, anns, items)?;
        medals(self.parts, anns, items)?;
        // The texts and the quotations share one pass: ChessBase's export
        // writes a quoted game where its record stands among the texts.
        texts(self.parts, anns, items, false)?;
        // Hydra64 : les textes rangés au-delà du dernier coup ne sont plus écrits — l'export de
        // ChessBase '26 ne les écrit pas (souvent un simple « - » : « 30. Qc6+ 1-0 » et non
        // « 30. Qc6+ {-} 1-0 »). `past_texts` reste pour mémoire.
        let _ = (past, past_texts);
        // Hydra64 : la pendule relevée sur ce coup, après le texte (« … Bb4 [%clk 0:28:22] »).
        move_clock(self.parts, anns, items)?;
        // The comment leads a space, after the SAN or the NAGs; the writer
        // trails the space that separates the moves.
        Ok(comment(out, self.parts, " ", ""))
    }
}

/// A number in decimal, without the formatting machinery: a token's own
/// value. Negative numbers carry their sign, as `{}` writes them.
#[inline]
fn push_number(out: &mut String, n: i64) {
    if n < 0 {
        out.push('-');
        push_u64(out, n.unsigned_abs());
    } else {
        push_u64(out, n as u64);
    }
}

/// An unsigned number in decimal.
#[inline]
fn push_u64(out: &mut String, n: u64) {
    let mut digits = [0u8; 20];
    let mut i = digits.len();
    let mut n = n;
    loop {
        i -= 1;
        digits[i] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    for &b in &digits[i..] {
        out.push(char::from(b));
    }
}

/// A number below 100 as two digits, zero padded, as `{:02}` writes it.
#[inline]
fn push_two(out: &mut String, n: u8) {
    out.push(char::from(b'0' + n / 10));
    out.push(char::from(b'0' + n % 10));
}

/// The items at `position`: the index is ascending, so they are one run.
#[inline]
fn range(index: &[(i32, u32)], position: i32) -> &[(i32, u32)] {
    let start = index.partition_point(|&(p, _)| p < position);
    let len = index[start..].iter().take_while(|&&(p, _)| p == position).count();
    &index[start..start + len]
}

/// The items at or past `moves`: they lie beyond the game's last move.
#[inline]
fn past_range(index: &[(i32, u32)], moves: u32) -> &[(i32, u32)] {
    let moves = i32::try_from(moves).unwrap_or(i32::MAX);
    &index[index.partition_point(|&(p, _)| p < moves)..]
}

/// The move's NAGs, straight into the movetext in the stored slot order:
/// on the move, on the position, then the prefix.
fn nags(out: &mut String, anns: AnnotationsRef<'_>, items: [&[(i32, u32)]; 2]) -> Result<()> {
    for &(_, offset) in items.into_iter().flatten() {
        let Annotation::Symbols { on_move, on_position, prefix } = anns.annotation_at(offset)? else {
            continue;
        };
        // ChessBase's export writes the prefix NAG first, then the move's,
        // then the position's: `01 13 8e` → `$142 $1 $19`, `01 12` → `$1 $18`
        // (447+ games of the Mega's export confirm the order).
        for nag in [prefix, on_move, on_position] {
            if nag != 0 {
                out.push(' ');
                out.push('$');
                let _ = write!(out, "{nag}");
            }
        }
    }
    Ok(())
}

/// The medals (type `22`), as ChessBase writes them `[%mdl <bits>]`: the
/// `int` of medal bits, big-endian in the classic format, little-endian in 2CBH.
/// A medal of any other length is left out, as the ancestor's reader leaves
/// what it does not understand.
fn medals(parts: &mut Parts, anns: AnnotationsRef<'_>, items: [&[(i32, u32)]; 2]) -> Result<()> {
    for &(_, offset) in items.into_iter().flatten() {
        let Annotation::Other { code: 0x22, data } = anns.annotation_at(offset)? else { continue };
        let Ok(bytes) = <[u8; 4]>::try_from(data) else { continue };
        // `[%mdl …]` is one of the export's tokens: it follows the graphics
        // with nothing between them (`[%csl Ge5][%cal Ge5h8][%mdl 32]`). Its
        // part's buffer is reused, so the token costs no allocation.
        let part = parts.push(Sep::None);
        part.token = true;
        part.text.push_str("[%mdl ");
        let bits = if anns.is_classic() { u32::from_be_bytes(bytes) } else { u32::from_le_bytes(bytes) };
        push_number(&mut part.text, i64::from(bits));
        part.text.push(']');
    }
    Ok(())
}

/// The engine evaluations (type `21`), as ChessBase writes them
/// `[%eval score,depth]` — see [`timing::evaluation_pair`] for the reading.
fn evaluations(parts: &mut Parts, anns: AnnotationsRef<'_>, items: [&[(i32, u32)]; 2]) -> Result<()> {
    for &(_, offset) in items.into_iter().flatten() {
        let Annotation::Other { code: 0x21, data } = anns.annotation_at(offset)? else { continue };
        let Some((score, depth)) = timing::evaluation_pair(data) else { continue };
        let part = parts.push(Sep::None);
        part.token = true;
        part.text.push_str("[%eval ");
        push_number(&mut part.text, i64::from(score));
        part.text.push(',');
        push_number(&mut part.text, i64::from(depth));
        part.text.push(']');
    }
    Ok(())
}

/// The times spent on a move (type `07`), as ChessBase writes them
/// `[%emt h:mm:ss]`, whole seconds, a space after the `[%eval …]`.
fn times(parts: &mut Parts, anns: AnnotationsRef<'_>, items: [&[(i32, u32)]; 2]) -> Result<()> {
    for &(_, offset) in items.into_iter().flatten() {
        let Annotation::Other { code: 0x07, data } = anns.annotation_at(offset)? else { continue };
        let Some((h, m, s, hundredths)) = timing::time_spent(data, anns.is_classic()) else { continue };
        let part = parts.push(Sep::Space);
        part.token = true;
        part.text.push_str("[%emt ");
        // The hours are written as they stand (`1:25:00`), the minutes, seconds
        // and hundredths as two digits, exactly as `{h}:{m:02}:{s:02}` did.
        // Hydra64 : pas de centièmes, mais l'arrondi à la seconde la plus proche, comme l'export
        // de ChessBase '26 (« [%emt 0:04:55] » pour 4 min 54 s 80 d'une base .2cbh ; « 0:03:54 »
        // là où ce champ vaut 30 dans la Mega Database 2025).
        // Forme .2cbh de 7 octets : l'octet qui précède les secondes porte les centièmes.
        let hundredths = if !anns.is_classic() && data.len() == 7 { data[3] } else { hundredths };
        let total = u32::from(h) * 3600 + u32::from(m) * 60 + u32::from(s) + u32::from(hundredths >= 50);
        push_number(&mut part.text, i64::from(total / 3600));
        part.text.push(':');
        push_two(&mut part.text, (total / 60 % 60) as u8);
        part.text.push(':');
        push_two(&mut part.text, (total % 60) as u8);
        part.text.push(']');
    }
    Ok(())
}

/// `[%csl …]` and `[%cal …]`, each when there is at least one mark of a
/// known colour, joined into the one token ChessBase writes them in, with
/// `[%csl …]` and `[%cal …]` and a later `[%mdl …]` all following each other
/// with nothing between them. The colours 7, 8 and 9 are unknown and left out.
fn graphics(parts: &mut Parts, anns: AnnotationsRef<'_>, items: [&[(i32, u32)]; 2]) -> Result<()> {
    let csl_at = parts.as_slice().len();
    let part = parts.push(Sep::None);
    part.token = true;
    let mut marks = 0usize;
    for &(_, offset) in items.into_iter().flatten() {
        let Annotation::Squares(data) = anns.annotation_at(offset)? else { continue };
        for (c, square) in squares(data) {
            let Some(colour) = colour(c) else { continue };
            if marks == 0 {
                parts.get_mut(csl_at).text.push_str("[%csl ");
            } else {
                parts.get_mut(csl_at).text.push(',');
            }
            parts.get_mut(csl_at).text.push(colour);
            push_name(&mut parts.get_mut(csl_at).text, square);
            marks += 1;
        }
    }
    let squares_written = marks > 0;
    if squares_written {
        parts.get_mut(csl_at).text.push(']');
    } else {
        parts.remove(csl_at);
    }

    let cal_at = parts.as_slice().len();
    let part = parts.push(Sep::None);
    part.token = true;
    let mut marks = 0usize;
    for &(_, offset) in items.into_iter().flatten() {
        let Annotation::Arrows(data) = anns.annotation_at(offset)? else { continue };
        for (c, from, to) in arrows(data) {
            let Some(colour) = colour(c) else { continue };
            if marks == 0 {
                parts.get_mut(cal_at).text.push_str("[%cal ");
            } else {
                parts.get_mut(cal_at).text.push(',');
            }
            parts.get_mut(cal_at).text.push(colour);
            push_name(&mut parts.get_mut(cal_at).text, from);
            push_name(&mut parts.get_mut(cal_at).text, to);
            marks += 1;
        }
    }
    if marks == 0 {
        parts.remove(cal_at);
    } else {
        parts.get_mut(cal_at).text.push(']');
        if squares_written {
            // One token, as ChessBase and Lichess write them: `[%csl …][%cal …]`.
            // The arrow part is the last one, so it moves out and appends.
            let arrows = parts.last_mut().text.clone();
            parts.remove(cal_at);
            parts.get_mut(csl_at).text.push_str(&arrows);
        }
    }
    Ok(())
}

/// Whether two annotation texts are the same words once case and punctuation
/// are ignored: a duplicated translation, not a language of its own.
fn same_words(a: &[u8], b: &[u8]) -> bool {
    let mut a = a.iter().copied().filter(|c| c.is_ascii_alphanumeric() || !c.is_ascii());
    let mut b = b.iter().copied().filter(|c| c.is_ascii_alphanumeric() || !c.is_ascii());
    loop {
        match (a.next(), b.next()) {
            (Some(x), Some(y)) if x.eq_ignore_ascii_case(&y) => {}
            (None, None) => return true,
            _ => return false,
        }
    }
}

/// The texts of `items` into the comment parts: `before` selects the ones
/// meant to precede their move. Every text of every language is written, as
/// ChessBase's export writes them — in the order that export writes them.
/// A game whose comment carries an `any language` source note keeps the
/// record order of its own languages after that note, and so do move comments
/// whose languages the game comment knows; a comment with a language of its
/// own is sorted: any-language first, then ascending language number
/// (English, German, French …), record order breaking ties. The fast path,
/// one text, a record already in order, or nothing to reorder, costs no
/// allocation.
fn texts(parts: &mut Parts, anns: AnnotationsRef<'_>, items: [&[(i32, u32)]; 2], before: bool) -> Result<()> {
    let is_game = items.into_iter().flatten().any(|&(position, _)| position == GAME_POSITION);
    let mut count = 0usize;
    let mut ordered = true;
    let mut prev = i32::MIN;
    let mut has_any = false;
    let mut has_quote = false;
    let mut seen_languages = 0u128;
    let mut before_languages = 0u128;
    let mut after_languages = 0u128;
    let mut last_language = u16::MAX;
    let mut interleaved = false;
    let mut other = false;
    let mut any_text: Option<&[u8]> = None;
    let mut other_texts = [&[][..]; 8];
    let mut other_count = 0usize;
    let mut raw_comment = false;
    for item in items.into_iter().flatten() {
        let (_, offset) = *item;
        match anns.annotation_at(offset)? {
            Annotation::Text { before: b, language, text } => {
                raw_comment |= raw_mixed_text(text, language, anns.is_classic());
                // A language whose texts come back after another language's
                // is an interleaved import (61987), not a block ChessBase
                // writes itself; only that keeps the record order.
                let bit = 1u128 << (language & 127);
                if language != language::ANY {
                    if b == before {
                        before_languages |= bit;
                    } else {
                        after_languages |= bit;
                    }
                }
                if language != last_language {
                    interleaved |= seen_languages & bit != 0;
                    seen_languages |= bit;
                    last_language = language;
                }
                if b != before {
                    other = true;
                    continue;
                }
                let key = write_key(language);
                has_any |= key == -1;
                if language != language::ANY {
                    if other_count < other_texts.len() {
                        other_texts[other_count] = text;
                        other_count += 1;
                    }
                } else if any_text.is_none() {
                    any_text = Some(text);
                }
                ordered &= key >= prev;
                prev = key;
                count += 1;
            }
            Annotation::Other { code: 0x13, .. } if !before => has_quote = true,
            _ => {}
        }
    }
    // A duplicated `any language` text is not a source note but the same
    // words as a text of a language: it keeps the record order (122599's
    // `{'Or' Or}`), while a source note goes first (725, 6214, 50128).
    let duplicate = any_text.is_some_and(|any| other_texts[..other_count].iter().any(|&text| same_words(any, text)));
    // The game's own comment: an `any language` note comes first, then the
    // record order (725, 61987); without one it sorts like any other comment
    // (8730). A move comment keeps the record only when its languages came
    // interleaved (61987) or a duplicated `any` text precedes a move that has
    // texts on both sides (122599's `{'Or' Or}`); otherwise it sorts (51022,
    // 90019, 1332), the other `any` texts going first (6214, 50128, 34547).
    let record = if is_game {
        has_any
    } else {
        // Interleaved languages and a duplicated `any` text before a move
        // whose before side has a language the after side lacks (122599's
        // `{'Or' Or}`) keep the record; every other move comment sorts.
        interleaved || (other && duplicate && before_languages & !after_languages != 0)
    };
    let any_first = has_any && !(!is_game && record && duplicate);
    let write = |parts: &mut Parts, offset: u32| -> Result<()> {
        match anns.annotation_at(offset)? {
            Annotation::Text { language, text, .. } => {
                let part = parts.push(Sep::Space);
                let part = &mut part.text;
                // The part keeps its buffer for the next comment and the next
                // game. A text with a language of its own is the one ChessBase
                // stores in its own text form, where the control byte `04`
                // becomes the diagram `[#]`; an `any language` text keeps its
                // bytes as stored (probe games 4566, 12579 against 10474, 13058).
                let own_language = language != language::ANY || !anns.is_classic();
                clean_into(part, text, own_language, anns.is_classic(), raw_comment);
            }
            Annotation::Other { code: 0x13, data } => {
                let quotation = if anns.is_classic() {
                    Quotation::parse_classic(data).or_else(|| Quotation::parse(data))
                } else {
                    // A `[1, 1]` payload without player names is ordinary
                    // metadata, not a quoted game (games 773687, 831403).
                    Quotation::parse_v2(data)
                };
                if let Some(q) = quotation {
                    // A quoted game follows the part before it with the `;`
                    // ChessBase's export writes (`[%csl Gd5];0-1 Alapin…`). It
                    // renders into the part's own buffer, so a quotation costs
                    // no allocation either.
                    let part = parts.push(Sep::Semicolon);
                    q.chessbase_text_into(&mut part.text);
                    part.raw = true;
                } else if !anns.is_classic() && data.starts_with(&[1, 2]) {
                    // A `[1, 2]` payload is a quotation by construction; one
                    // our decoder cannot interpret must never vanish from a
                    // supposedly complete v2 PGN. `[1, 1]` metadata has no
                    // quotation to write (games 773687, 831403).
                    return Err(Error::corrupt(
                        "<.2cba>",
                        u64::from(offset),
                        "unhandled 2CBH quoted-game annotation payload",
                    ));
                }
            }
            _ => {}
        }
        Ok(())
    };
    // (Hydra64 : deux règles ont été essayées pour l'ordre des langues des textes d'avant-coup —
    // l'ordre inverse, puis l'ordre de l'enregistrement. Chacune créait plus d'écarts avec l'export
    // de ChessBase '26 qu'elle n'en corrigeait ; la règle d'origine est gardée : 54 parties de la
    // Mega Database 2025 sortent « anglais puis allemand » là où ChessBase écrit l'inverse.)
    let fast = count <= 1 || (record && !any_first) || (!record && ordered);
    if fast && !has_quote {
        // The fast path: the record's own order among the texts, which is
        // also the sorted order when `ordered` says so.
        for item in items.into_iter().flatten() {
            let (_, offset) = *item;
            if let Annotation::Text { before: b, .. } = anns.annotation_at(offset)?
                && b == before
            {
                write(parts, offset)?;
            }
        }
        return Ok(());
    }
    let mut found: Vec<(i32, u32)> = Vec::with_capacity(count);
    for item in items.into_iter().flatten() {
        let (_, offset) = *item;
        match anns.annotation_at(offset)? {
            Annotation::Text { before: b, language, .. } if b == before => {
                let key = write_key(language);
                found.push((if record { key.min(0) } else { key }, offset));
            }
            // A quoted game comes before every text, whatever its language
            // (the only mixed-language groups the Mega has hold no quotation,
            // so this is the one place the two could differ).
            Annotation::Other { code: 0x13, .. } if !before => found.push((QUOTATION_KEY, offset)),
            _ => {}
        }
    }
    found.sort_by_key(|&(key, _)| key);
    for &(_, offset) in &found {
        write(parts, offset)?;
    }
    Ok(())
}

/// The order key of a quoted game among the texts of one comment: before
/// every text, whatever its language.
const QUOTATION_KEY: i32 = i32::MIN;

/// The order key of a text's language: any language first, then the language
/// number itself (`language_of`'s: English 0, German 1 … unknown nations
/// above every known one).
#[inline]
fn write_key(language: u16) -> i32 {
    if language == language::ANY { -1 } else { i32::from(language) }
}

/// The annotations past the game's last move, written after it: per
/// position, the texts meant to precede a move first (there is no move to
/// precede them there), then the rest — the order the ancestor writes them
/// in, and the game served in full.
fn past_texts(parts: &mut Parts, anns: AnnotationsRef<'_>, past: &[(i32, u32)]) -> Result<()> {
    let mut at = 0;
    while at < past.len() {
        let position = past[at].0;
        let end = at + past[at..].iter().take_while(|&&(p, _)| p == position).count();
        let group = [&past[at..end], &[][..]];
        texts(parts, anns, group, true)?;
        texts(parts, anns, group, false)?;
        at = end;
    }
    Ok(())
}

/// Writes `{parts}` between `pre` and `post` when there is at least one
/// non-empty part: each part after the first follows with its own separator
/// ([`Sep`]), and empty parts are dropped — a text that cleans to nothing
/// writes no comment (ChessBase writes none). Nothing is written when every
/// part is empty. Whitespace runs are assembled across part boundaries and
/// halved, as ChessBase's export writes them: two stored spaces (or CR, LF,
/// CRLF) become one, three become two, and so on (`1932 \r\n 1.` stores four
/// and exports two; game 29508 stores five before a text separator and exports
/// three). Leading and trailing whitespace is dropped. A quoted game keeps its
/// own runs (its site field can end with the space before the year). Zero
/// allocation: the parts go straight into `out`.
fn comment(out: &mut String, parts: &Parts, pre: &str, post: &str) -> bool {
    if parts.as_slice().iter().all(|p| blank(&p.text)) {
        return false;
    }
    let start_len = out.len();
    out.push_str(pre);
    out.push('{');
    let mut pending = 0usize;
    let mut emitted = false;
    let mut first = true;
    let mut prev_token = false;
    for part in parts.as_slice() {
        if blank(&part.text) {
            continue;
        }
        if !first {
            match part.sep {
                Sep::None => {}
                Sep::Space => {
                    if prev_token {
                        pending = 1;
                    } else {
                        pending += 1;
                    }
                }
                Sep::Semicolon => {
                    flush_spaces(out, &mut pending);
                    out.push(';');
                }
            }
        }
        first = false;
        let was_token = prev_token;
        prev_token = part.token;
        if part.raw {
            flush_spaces(out, &mut pending);
            if emitted {
                out.push_str(&part.text);
            } else {
                let text = part.text.trim_start();
                out.push_str(text);
                emitted = !text.is_empty();
            }
            pending = 0;
            continue;
        }
        let mut chars = part.text.chars();
        if was_token {
            while let Some(' ') = chars.clone().next() {
                chars.next();
            }
        }
        for ch in chars {
            if ch == ' ' {
                pending += 1;
                continue;
            }
            if !emitted {
                pending = 0;
                emitted = true;
            }
            flush_spaces(out, &mut pending);
            out.push(ch);
        }
    }
    if !emitted {
        out.truncate(start_len);
        return false;
    }
    while out.ends_with(' ') {
        out.pop();
    }
    out.push('}');
    out.push_str(post);
    true
}

/// Writes `pending` stored whitespace bytes as ChessBase's export does: half
/// the run, rounded up.
#[inline]
fn flush_spaces(out: &mut String, pending: &mut usize) {
    let n = *pending;
    *pending = 0;
    for _ in 0..n.div_ceil(2) {
        out.push(' ');
    }
}

fn raw_mixed_text(bytes: &[u8], language: u16, is_classic: bool) -> bool {
    if is_classic || language != language::ANY {
        return false;
    }
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    let bytes = &bytes[..end];
    let Err(error) = std::str::from_utf8(bytes) else {
        return false;
    };
    bytes.iter().position(|&b| b >= 0x80).is_some_and(|at| error.valid_up_to() > at)
}

/// A text fit for a PGN comment, into `out` (which it empties first): UTF-8
/// when the bytes are valid UTF-8, else Windows-1252; the text ends at the
/// first NUL (damaged records trail garbage after one — the Mega's `Lehner,`
/// text; ChessBase reads the same); braces become parentheses, controls
/// become spaces, runs of spaces collapse and the ends trim. The
/// ChessBase export character table applies to the code-page bytes of every
/// text, and to the valid UTF-8 bytes of a text without a language of its own
/// (`own_language` false) — a text the record stores as UTF-8 with a language
/// of its own is written as it stands, its curly quotes and dashes included.
fn clean_into(out: &mut String, bytes: &[u8], own_language: bool, is_classic: bool, raw_mixed: bool) {
    out.clear();
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    let bytes = &bytes[..end];
    match std::str::from_utf8(bytes) {
        Ok(text) => push_clean_text(out, text, own_language, false, is_classic, raw_mixed),
        Err(_) if cyrillic(bytes) || (cyrillic_context() && cyrillic_in_context(bytes)) => {
            // Hydra64 : texte en Windows-1251 (bases russes). L'export de ChessBase le lirait
            // en Windows-1252 et y remplacerait des lettres par ses symboles ; ici il est lu
            // comme du cyrillique, sans la table de symboles. `04` reste le diagramme.
            // (l'octet 9E est le diagramme de ChessBase, comme 04)
            let text: String = bytes.iter().map(|&b| if b == 0x9e { '\u{4}' } else { CodePage::new(1251).char(b) }).collect();
            push_clean_text(out, &text, true, false, is_classic, raw_mixed);
        }
        Err(_) => {
            // A damaged or code-page text: its valid UTF-8 parts are kept as
            // UTF-8 and every byte UTF-8 cannot read is decoded as Windows-1252
            // — how ChessBase's own export reads the Mega's Czech, Hebrew and
            // Cyrillic texts, which hold valid UTF-8 beside stray bytes.
            let mut text = String::with_capacity(bytes.len());
            let mut rest = bytes;
            while !rest.is_empty() {
                match std::str::from_utf8(rest) {
                    Ok(tail) => {
                        text.push_str(tail);
                        break;
                    }
                    Err(e) => {
                        let good = e.valid_up_to();
                        if good > 0 {
                            text.push_str(std::str::from_utf8(&rest[..good]).expect("valid UTF-8"));
                        }
                        let bad = e.error_len().unwrap_or(rest.len() - good).min(rest.len() - good);
                        for &b in &rest[good..good + bad] {
                            // Hydra64 : l'octet de diagramme 9E devient le caractère privé du
                            // diagramme, toujours rendu « [#] » — y compris dans un texte mêlé
                            // (UTF-8 et octets de page de code), où il se perdait.
                            text.push(if b == 0x9e { '\u{e005}' } else { CodePage::WESTERN.char(b) });
                        }
                        rest = &rest[good + bad..];
                    }
                }
            }
            // A text whose first non-ASCII character is valid UTF-8 but which
            // also holds stray code-page bytes is written as its two decodings
            // stand (games 324683, 426791); one that turns to CP1252 at its
            // first non-ASCII byte still takes ChessBase's export table
            // (games 11204, 16995).
            let (own_language, code_page) = if raw_mixed { (false, false) } else { (own_language, true) };
            push_clean_text(out, &text, own_language, code_page, is_classic, raw_mixed);
        }
    }
}

thread_local! {
    // Hydra64 : vrai pendant le rendu d'une partie (ou d'une base) reconnue comme russe.
    static CYRILLIC_CONTEXT: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
}

/// Hydra64 : dans ce contexte (fixé par l'appelant pour le thread courant, le temps d'un
/// rendu), tout commentaire qui n'est pas de l'UTF-8 et qui porte une lettre 0xC0…0xFF est lu en
/// Windows-1251, même trop court pour être reconnu seul (« или », « но не », « и »).
pub fn set_cyrillic_context(on: bool) {
    CYRILLIC_CONTEXT.with(|c| c.set(on));
}

fn cyrillic_context() -> bool {
    CYRILLIC_CONTEXT.with(std::cell::Cell::get)
}

/// Hydra64 : texte à lire en Windows-1251 quand le contexte est russe — au moins une lettre
/// 0xC0…0xFF et presque aucune séquence UTF-8 valable (un texte UTF-8 abîmé garde l'ancien chemin).
fn cyrillic_in_context(bytes: &[u8]) -> bool {
    let high = bytes.iter().filter(|&&b| b >= 0xc0).count();
    high > 0 && utf8_sequences(bytes) * 4 < high
}

/// Hydra64 : un texte qui n'est pas de l'UTF-8 est-il du Windows-1251 ? Oui s'il a au moins
/// un mot (quatre octets de lettres 0xC0…0xFF de suite), plus de ces octets que de lettres latines, et presque aucune séquence
/// UTF-8 valable (un texte UTF-8 abîmé garde l'ancien chemin). Un texte occidental a toujours
/// bien plus de lettres ASCII que de lettres accentuées.
fn cyrillic(bytes: &[u8]) -> bool {
    let high = bytes.iter().filter(|&&b| b >= 0xc0).count();
    let latin = bytes.iter().filter(|&&b| b.is_ascii_alphabetic()).count();
    if high < 4 || high <= latin {
        return false;
    }
    // Un mot : quatre lettres de suite. Les symboles de ChessBase (× case faible, etc.) sont des
    // octets isolés entre des cases et des espaces (« ×e5,h8 ») et ne forment jamais de mot.
    let mut run = 0usize;
    let mut word = false;
    for &b in bytes {
        run = if b >= 0xc0 { run + 1 } else { 0 };
        word |= run >= 4;
    }
    if !word {
        return false;
    }
    utf8_sequences(bytes) * 4 < high
}

/// Hydra64 : nombre de caractères non ASCII lisibles comme de l'UTF-8 valable dans `bytes`.
fn utf8_sequences(bytes: &[u8]) -> usize {
    let mut sequences = 0usize;
    let mut rest = bytes;
    while !rest.is_empty() {
        match std::str::from_utf8(rest) {
            Ok(tail) => {
                sequences += tail.chars().filter(|c| !c.is_ascii()).count();
                break;
            }
            Err(e) => {
                let good = e.valid_up_to();
                sequences += std::str::from_utf8(&rest[..good]).map_or(0, |t| t.chars().filter(|c| !c.is_ascii()).count());
                let bad = e.error_len().unwrap_or(rest.len() - good).min(rest.len() - good).max(1);
                rest = &rest[(good + bad).min(rest.len())..];
            }
        }
    }
    sequences
}

/// Whether a text holds nothing the export writes: only the layout
/// whitespace it halves away. A tab is content (gold writes a lone tab as
/// `{\t}` in game 51383), and so is any other Unicode space.
#[inline]
fn blank(text: &str) -> bool {
    text.chars().all(|c| matches!(c, ' ' | '\r' | '\n'))
}

/// One comment's characters: the diagram first ([`DIAGRAM`]), then
/// [`export_char`] when `code_page` (the text was decoded as Windows-1252), then
/// its shape — braces as parentheses and every whitespace byte as one space,
/// with no runs collapsed here: [`comment`] halves the runs it assembles, as
/// ChessBase's export does. A tab is kept as a tab (the Mega's `A\tin` names
/// ship with it); other Unicode spaces are data, not layout. `own_language` is
/// whether the text carries a language of its own: the export's control-byte
/// table (the `04` diagram, mapped to `[#]` for such a text) applies to those;
/// an `any language` text is written as it stands, its `04` bytes, curly quotes
/// and dashes included (probe games 10474, 13058: `any` → raw; 4566, 12579:
/// English → `[#]`), while a code-page byte takes the private-use character the
/// export writes for it.
fn push_clean_text(
    out: &mut String,
    text: &str,
    own_language: bool,
    code_page: bool,
    is_classic: bool,
    _raw_mixed: bool,
) {
    if blank(text) {
        return;
    }
    // A text that is nothing but the control byte `04`: the export keeps it as
    // it stands, a language of its own or not (probe games 13058, 4259, 6774).
    let bare = text.trim() == "\u{4}";
    let bytes = text.as_bytes();
    let mut at = 0;
    while at < bytes.len() {
        // The bulk of a comment is plain ASCII: copy it whole, and take the
        // per-character path only for a brace, a space, a control byte, a
        // diagram byte, a private-use code or any byte of a multi-byte
        // character. Plain bytes are never remapped and never whitespace.
        let run = at;
        while at < bytes.len() && plain(bytes[at]) {
            at += 1;
        }
        if at > run {
            out.reserve(at - run);
            for &b in &bytes[run..at] {
                out.push(char::from(b));
            }
            continue;
        }
        let c = text[at..].chars().next().expect("a character where the index is");
        at += c.len_utf8();
        // The diagram. ChessBase stores it as the cp1252 byte 0x9E (`ž` once
        // decoded) or as the private-use `E005` in any text, and as the
        // control byte 0x04 in a text of the `any language` kind; its own
        // export writes `[#]` for each of those.
        // Hydra64 : tout repère de diagramme est rendu « [#] », quelle que soit sa forme dans
        // la base (octet 04, octet 9E, caractère privé E005) et qu'il soit seul ou dans un texte.
        // L'export PGN de ChessBase '26, lui, laisse l'octet 04 tel quel dans une partie des cas
        // (un diagramme seul, un texte dans une langue donnée) : c'est le même diagramme, que le
        // lecteur PGN de Hydra64 reconnaît aussi sous cette forme brute.
        let _ = (own_language, bare);
        if (code_page && c == DIAGRAM) || c == '\u{e005}' || c == '\u{4}' {
            out.push_str("[#]");
            continue;
        }
        let c = if code_page && (is_classic || own_language) { export_char(c) } else { c };
        match c {
            '{' => out.push('('),
            '}' => out.push(')'),
            '\t' => out.push('\t'),
            ' ' | '\r' | '\n' => out.push(' '),
            c => out.push(c),
        }
    }
}

/// A byte that goes into the comment exactly as it stands: printable ASCII
/// that is neither a brace (which becomes a parenthesis) nor a space (which
/// the whitespace rule takes, so that none is written leading).
#[inline]
const fn plain(b: u8) -> bool {
    b > b' ' && b < 0x7f && b != b'{' && b != b'}'
}

/// The character ChessBase stores for a diagram in text, as cp1252 decodes
/// it.
const DIAGRAM: char = '\u{17e}';

/// The character ChessBase's own PGN export writes instead of `c` in a
/// comment: a fixed table of private-use characters (Table B of
/// `docs/format-spec.md`), derived from the 419,385 games of Mega Database
/// 2025's own export — figurines (`¤` = knight …), the ellipsis and the
/// symbol characters. Everything else, accented Latin letters first, passes
/// unchanged. A char already private-use is not remapped: the record can
/// hold the export's own characters.
#[inline]
fn export_char(c: char) -> char {
    match c {
        '\u{a2}' => '\u{e024}',   // ¢  king
        '\u{a3}' => '\u{e025}',   // £  queen
        '\u{a4}' => '\u{e028}',   // ¤  knight
        '\u{a5}' => '\u{e027}',   // ¥  bishop
        '\u{a6}' => '\u{e026}',   // ¦  rook
        '\u{a7}' => '\u{e029}',   // §  pawn
        '\u{aa}' => '\u{e01b}',   // ª
        '\u{ab}' => '\u{e00e}',   // «
        '\u{ac}' => '\u{e01f}',   // ¬
        '\u{ad}' => '\u{e001}',   // ­ soft hyphen
        '\u{ae}' => '\u{e002}',   // ®
        '\u{af}' => '\u{e003}',   // ¯
        '\u{b0}' => '\u{e000}',   // °
        '\u{a9}' => '\u{e000}',   // © (the Mega's export writes E000 for it too)
        '\u{b1}' => '\u{e00a}',   // ±
        '\u{2022}' => '\u{e02a}', // • (gold games 96364, 309819: E02A)
        '\u{b2}' => '\u{e02f}',   // ²
        '\u{b3}' => '\u{e02e}',   // ³
        '\u{b5}' => '\u{e01a}',   // µ
        '\u{b9}' => '\u{e020}',   // ¹
        '\u{ba}' => '\u{e004}',   // º
        '\u{bb}' => '\u{e00f}',   // »
        '\u{de}' => '\u{e007}',   // Þ
        '\u{201a}' => '\u{e013}', // ‚
        '\u{201c}' => '\u{e01c}', // “
        '\u{201d}' => '\u{e01e}', // ”
        '\u{201e}' => '\u{e017}', // „
        '\u{2018}' => '\u{e018}', // ‘
        '\u{2019}' => '\u{e019}', // ’
        '\u{2020}' => '\u{e023}', // †
        '\u{2021}' => '\u{e01d}', // ‡
        '\u{2026}' => '\u{e00d}', // …
        '\u{2030}' => '\u{e02d}', // ‰
        '\u{2039}' => '\u{e00b}', // ‹
        '\u{203a}' => '\u{e00c}', // ›
        '\u{2122}' => '\u{e021}', // ™
        '\u{192}' => '\u{e012}',  // ƒ
        '\u{f7}' => '\u{e009}',   // ÷
        '\u{fe}' => '\u{e008}',   // þ
        c => c,
    }
}

/// A ChessBase colour letter: 2 green, 3 yellow, 4 red, 7 blue, 8 cyan, 9 orange —
/// verified empirically against Mega Database 2026 ChessBase 18 export (game 471420).
#[inline]
fn colour(c: u8) -> Option<char> {
    match c {
        2 => Some('G'),
        3 => Some('Y'),
        4 => Some('R'),
        7 => Some('B'),
        8 => Some('C'),
        9 => Some('O'),
        _ => None,
    }
}

/// A square's name, the squares numbered from 1 file by file: `a1` is 1 and
/// `b1` is 9. Out-of-range squares were rejected when the record was read;
/// a stray one adds nothing rather than panicking.
#[inline]
fn push_name(out: &mut String, n: u8) {
    if !(1..=64).contains(&n) {
        return;
    }
    let n = u32::from(n) - 1;
    out.push((b'a' + (n / 8) as u8) as char);
    out.push((b'1' + (n % 8) as u8) as char);
}

/// The game's evaluations as ChessBase's own PGN writes them: `[%evp
/// <first>,<last>,<values>]`, from the first entry that holds a value (a
/// game whose first twelve positions have none starts `12,<last>`), the
/// value of each entry as its profile holds it — centipawns as they are, a
/// mate in `n` plies as `30000 − n` (negated for Black), 32767 for no value.
pub(super) fn write_evp(out: &mut String, entries: &[Evaluation]) {
    let Some(last) = entries.len().checked_sub(1) else { return };
    let first = entries.iter().position(|e| e.flag != 0xff).unwrap_or(0);
    let _ = write!(out, "[%evp {first},{last}");
    for e in &entries[first..] {
        let _ = write!(out, ",{}", e.profile_value());
    }
    out.push(']');
}

// ---------------------------------------------------------------- Hydra64 : questions d'entraînement
//
// Annotation de type 09, que cbvault ne décodait pas. Disposition relevée sur la Mega Database 2025
// et calée sur l'export PGN de ChessBase '26 (21 novembre 2026) :
//
//   3 octets (01 00 01), longueur du reste (u16), 2 octets (2c 01), 2 octets (00 00), points (u16),
//   puis quatre groupes de textes — la question, un groupe toujours vide dans les exemples vus,
//   l'aide 1, l'aide 2 — chacun : nombre (u16), puis par texte la langue (u16), la longueur (u16)
//   et les octets ; une « langue » 7 ou 8 porte des flèches ou des cases, pas un texte ;
//   puis le nombre de réponses (u8) et, par réponse : case de départ, case d'arrivée (colonne × 8
//   + rangée), un octet, les points, et le groupe des textes de retour.
//   Bases .2cbh : même ordre, avec un octet d'en-tête de plus et des longueurs de texte en u32.
//
// ChessBase écrit `[%tqu "En","question","aide 1","aide 2","f6h7","retour",6,"De",…]` : un bloc
// de sept champs par langue de la question, dans l'ordre du groupe.

/// Les octets, la position, et la forme .2cbh (longueurs de texte sur 4 octets au lieu de 2).
struct TrainingReader<'a>(&'a [u8], usize, bool);

impl<'a> TrainingReader<'a> {
    fn u8(&mut self) -> Option<u8> {
        let v = *self.0.get(self.1)?;
        self.1 += 1;
        Some(v)
    }
    fn u16(&mut self) -> Option<u16> {
        let b = self.0.get(self.1..self.1 + 2)?;
        self.1 += 2;
        Some(u16::from_le_bytes([b[0], b[1]]))
    }
    /// Un groupe : (langue, octets) de chaque texte.
    fn group(&mut self) -> Option<Vec<(u16, &'a [u8])>> {
        let count = self.u16()?;
        let mut texts = Vec::with_capacity(usize::from(count.min(16)));
        for _ in 0..count {
            let language = self.u16()?;
            let len = if self.2 { usize::from(self.u16()?) | usize::from(self.u16()?) << 16 } else { usize::from(self.u16()?) };
            let bytes = self.0.get(self.1..self.1 + len)?;
            self.1 += len;
            texts.push((language, bytes));
        }
        Some(texts)
    }
}

/// Un texte de question prêt pour une chaîne entre guillemets : UTF-8 s'il est valable, sinon
/// Windows-1252 ; guillemets doubles en simples, accolades en parenthèses, retours à la ligne et
/// caractères de contrôle en un espace, figurines de la police de ChessBase en caractères privés.
fn training_text(out: &mut String, bytes: &[u8]) {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    let bytes = &bytes[..end];
    let (decoded, code_page);
    let text = match std::str::from_utf8(bytes) {
        Ok(t) => {
            code_page = false;
            t
        }
        Err(_) => {
            code_page = true;
            decoded = CodePage::WESTERN.decode(bytes);
            decoded.as_str()
        }
    };
    let mut space = false;
    for c in text.chars() {
        let c = match c {
            '"' => '\'',
            '{' => '(',
            '}' => ')',
            // Figurines et signes de la police de ChessBase : gardés sous leur caractère privé,
            // comme dans les commentaires (« majorité des \u{e029} noirs »).
            c if code_page && export_char(c) != c => export_char(c),
            c if c.is_control() => ' ',
            c => c,
        };
        if c == ' ' {
            space = true;
            continue;
        }
        if space && !out.ends_with('"') {
            out.push(' ');
        }
        space = false;
        out.push(c);
    }
}

fn training(out: &mut String, anns: AnnotationsRef<'_>, items: &[(i32, u32)]) -> Result<bool> {
    let mut wrote = false;
    for item in items {
        let Annotation::Other { code: 0x09, data } = anns.annotation_at(item.1)? else { continue };
        let before = out.len();
        if training_block(out, data, !anns.is_classic()).is_none() {
            if std::env::var_os("CBB_CENSUS").is_some() {
                eprintln!("TQU? {} {}", data.len(), data.iter().take(700).map(|b| format!("{b:02x}")).collect::<String>());
            }
            out.truncate(before); // enregistrement d'une autre forme : rien n'est écrit
        } else {
            wrote = true;
        }
    }
    Ok(wrote)
}

fn training_block(out: &mut String, data: &[u8], twocbh: bool) -> Option<()> {
    const LANGUAGES: [&str; 7] = ["En", "De", "Fr", "Es", "It", "Nl", "Pt"];
    // .2cbh : un octet d'en-tête de plus (01 01 01 00 00 00, puis 2c 01, 00 00, points)
    let mut r = TrainingReader(data, if twocbh { 12 } else { 11 }, twocbh);
    let question = r.group()?;
    let _unknown = r.group()?;
    let (help1, help2) = (r.group()?, r.group()?);
    let count = r.u8()?;
    if count == 0 {
        return None;
    }
    fn of<'a>(group: &[(u16, &'a [u8])], language: u16) -> &'a [u8] {
        group.iter().find(|t| t.0 == language).map_or(&[][..], |t| t.1)
    }
    // Troisième octet de l'en-tête : 1 = coup à trouver (`[%tqu]`), 2 = question à choix
    // multiples (`[%tmc]`). Pour celle-ci, par choix : les points (u8), le groupe des textes du
    // choix, le groupe des textes de retour. ChessBase écrit
    // `[%tmc "4","0","0","0","30","En","question","aide 1","aide 2","choix 1","retour 1",…,"De",…]` :
    // le nombre de choix, les points de chacun, puis par langue la question, les aides et les choix.
    if data.get(2) == Some(&2) {
        let mut choices = Vec::with_capacity(usize::from(count.min(16)));
        for _ in 0..count {
            let points = r.u8()?;
            choices.push((points, r.group()?, r.group()?));
        }
        let mut languages: Vec<u16> = question.iter().map(|t| t.0).filter(|&l| usize::from(l) < LANGUAGES.len()).collect();
        if languages.is_empty() {
            languages.push(0);
        }
        out.push_str("{[%tmc \"");
        push_number(out, i64::from(count));
        out.push('"');
        for (points, _, _) in &choices {
            out.push_str(",\"");
            push_number(out, i64::from(*points));
            out.push('"');
        }
        for language in languages {
            out.push_str(",\"");
            out.push_str(LANGUAGES[usize::from(language)]);
            for text in [of(&question, language), of(&help1, language), of(&help2, language)] {
                out.push_str("\",\"");
                training_text(out, text);
            }
            for (_, texts, feedback) in &choices {
                for text in [of(texts, language), of(feedback, language)] {
                    out.push_str("\",\"");
                    training_text(out, text);
                }
            }
            out.push('"');
        }
        out.push_str("]} ");
        return Some(());
    }
    // Les réponses : la bonne d'abord, puis d'autres coups prévus avec leur retour et leurs
    // points (souvent 0). ChessBase les écrit à la suite : `"f6f5","",10,"d8h8","",0`.
    let mut answers = Vec::with_capacity(usize::from(count.min(8)));
    for _ in 0..count {
        let (from, to, _, points) = (r.u8()?, r.u8()?, r.u8()?, r.u8()?);
        let feedback = r.group()?;
        if from > 63 || to > 63 {
            return None;
        }
        answers.push((from, to, points, feedback));
    }
    let mut texts: Vec<_> = question.iter().filter(|t| usize::from(t.0) < LANGUAGES.len()).collect();
    // Question sans énoncé : ChessBase écrit quand même le bloc, en « En » (`"En","","","","h5g6","",10`).
    let unasked = (0u16, &[][..]);
    if texts.is_empty() {
        texts.push(&unasked);
    }
    out.push_str("{[%tqu ");
    for (i, (language, text)) in texts.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        out.push('"');
        out.push_str(LANGUAGES[usize::from(*language)]);
        out.push_str("\",\"");
        training_text(out, text);
        out.push_str("\",\"");
        training_text(out, of(&help1, *language));
        out.push_str("\",\"");
        training_text(out, of(&help2, *language));
        out.push('"');
        for (from, to, points, feedback) in &answers {
            out.push_str(",\"");
            for square in [*from, *to] {
                out.push((b'a' + square / 8) as char);
                out.push((b'1' + square % 8) as char);
            }
            out.push_str("\",\"");
            training_text(out, of(feedback, *language));
            out.push_str("\",");
            push_number(out, i64::from(*points));
        }
    }
    out.push_str("]} ");
    Some(())
}

// ---------------------------------------------------------------- Hydra64 : cadence et pendules
//
// Trois annotations de la partie (position −1) que l'export PGN de ChessBase écrit en tags et que
// cbvault laissait de côté. Dispositions relevées sur les Mega Database 2025 et 2026 et calées sur
// l'export de ChessBase '26 :
//
//   24, cadence : trois étapes de 11 octets — temps initial et incrément en centièmes de seconde
//     (i32), nombre de coups (u16), genre (1 : étape de n coups ; autre : le reste de la partie).
//     Base .cbh : 33 octets, gros-boutiste. Base .2cbh : 38 octets, un octet 01 devant, petit-
//     boutiste (`timing::time_control`). Écrite `[TimeControl "40/5400+30:1800+30"]` : par étape
//     « coups/secondes », « +incrément » s'il y en a un, jusqu'à la première étape sans nombre de
//     coups, écrite « secondes » (« 0 » : plus de temps ajouté).
//   16 et 17, pendules des Blancs et des Noirs : un temps en centièmes de seconde (u32, gros-
//     boutiste en .cbh, petit-boutiste en .2cbh), écrit `[WhiteClock "1:30:00"]`.

fn time_tags(out: &mut String, anns: AnnotationsRef<'_>, items: &[(i32, u32)]) -> Result<()> {
    let classic = anns.is_classic();
    let mut tags = String::new();
    for item in items {
        let Annotation::Other { code, data } = anns.annotation_at(item.1)? else { continue };
        match code {
            0x24 => {
                let stages: Option<Vec<(i32, i32, u16, u8)>> = if classic {
                    (data.len() == 33).then(|| {
                        data.chunks_exact(11)
                            .map(|s| {
                                (
                                    i32::from_be_bytes([s[0], s[1], s[2], s[3]]),
                                    i32::from_be_bytes([s[4], s[5], s[6], s[7]]),
                                    u16::from_be_bytes([s[8], s[9]]),
                                    s[10],
                                )
                            })
                            .collect()
                    })
                } else {
                    timing::time_control(data, false).map(|s| s.iter().map(|s| (s.initial, s.increment, s.moves, s.kind)).collect())
                };
                let Some(stages) = stages.filter(|s| s.iter().all(|s| s.0 >= 0 && s.1 >= 0)) else { continue };
                tags.push_str("[TimeControl \"");
                for (i, &(initial, increment, moves, kind)) in stages.iter().enumerate() {
                    if i > 0 {
                        tags.push(':');
                    }
                    if kind == 1 {
                        push_number(&mut tags, i64::from(moves));
                        tags.push('/');
                    }
                    push_number(&mut tags, i64::from(initial / 100));
                    if increment > 0 {
                        tags.push('+');
                        push_number(&mut tags, i64::from(increment / 100));
                    }
                    if kind != 1 {
                        break;
                    }
                }
                tags.push_str("\"]\n");
            }
            0x16 | 0x17 => {
                let Ok(d) = <[u8; 4]>::try_from(data) else { continue };
                let hundredths = if classic { u32::from_be_bytes(d) } else { u32::from_le_bytes(d) };
                let seconds = (hundredths.saturating_add(50)) / 100; // arrondi à la seconde, comme ChessBase
                tags.push_str(if code == 0x16 { "[WhiteClock \"" } else { "[BlackClock \"" });
                push_number(&mut tags, i64::from(seconds / 3600));
                tags.push(':');
                push_two(&mut tags, (seconds / 60 % 60) as u8);
                tags.push(':');
                push_two(&mut tags, (seconds % 60) as u8);
                tags.push_str("\"]\n");
            }
            _ => {}
        }
    }
    if tags.is_empty() {
        return Ok(());
    }
    // Les tags sont déjà écrits, suivis de leur ligne vide : ceux-ci s'insèrent juste avant elle.
    let end = out.trim_end_matches('\n').len();
    let blank = out.len() - end;
    out.truncate(end);
    out.push('\n');
    out.push_str(&tags);
    for _ in 1..blank {
        out.push('\n');
    }
    Ok(())
}

// ---------------------------------------------------------------- Hydra64 : recensement des annotations
//
// Outil de contrôle : avec la variable d'environnement CBB_CENSUS, compte chaque annotation
// rencontrée par genre (et, pour les genres que rien n'écrit dans le PGN, garde un exemple de ses
// octets). `hydra64-convert` l'affiche à la fermeture de la base. Sert à vérifier qu'aucun
// genre d'annotation ne passe inaperçu.
#[allow(missing_docs)]
pub mod census {
    use super::{Annotation, AnnotationsRef};
    use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
    use std::sync::{Mutex, OnceLock};

    static COUNTS: [AtomicU64; 256] = [const { AtomicU64::new(0) }; 256];
    static SAMPLES: Mutex<Vec<(u16, i32, Vec<u8>)>> = Mutex::new(Vec::new());
    static HAS: [AtomicBool; 256] = [const { AtomicBool::new(false) }; 256];

    fn enabled() -> bool {
        static ON: OnceLock<bool> = OnceLock::new();
        *ON.get_or_init(|| std::env::var_os("CBB_CENSUS").is_some())
    }

    pub(super) fn count(anns: AnnotationsRef<'_>, index: &[(i32, u32)]) {
        if !enabled() {
            return;
        }
        for &(position, offset) in index {
            let Ok(annotation) = anns.annotation_at(offset) else { continue };
            let (code, data): (u16, &[u8]) = match annotation {
                Annotation::Text { before, text, .. } => (if before { 0x82 } else { 0x02 }, text),
                Annotation::Symbols { .. } => (0x03, &[]),
                Annotation::Squares(d) => (0x04, d),
                Annotation::Arrows(d) => (0x05, d),
                Annotation::Other { code, data } => (code, data),
            };
            let slot = usize::from(code.min(255));
            COUNTS[slot].fetch_add(1, Ordering::Relaxed);
            if !HAS[slot].swap(true, Ordering::Relaxed) {
                if let Ok(mut samples) = SAMPLES.lock() {
                    samples.push((code, position, data[..data.len().min(48)].to_vec()));
                }
            }
        }
    }

    /// Le recensement : (genre, nombre, position et premiers octets d'un exemple), par genre croissant.
    pub fn report() -> Vec<(u16, u64, i32, Vec<u8>)> {
        let samples = SAMPLES.lock().map(|s| s.clone()).unwrap_or_default();
        let mut out = Vec::new();
        for (code, counter) in COUNTS.iter().enumerate() {
            let n = counter.load(Ordering::Relaxed);
            if n > 0 {
                let sample = samples.iter().find(|s| usize::from(s.0.min(255)) == code);
                out.push((code as u16, n, sample.map_or(0, |s| s.1), sample.map_or_else(Vec::new, |s| s.2.clone())));
            }
        }
        out
    }
}

/// Hydra64 : pendule relevée sur un coup (annotations 16 et 17 posées sur un coup et non sur la
/// partie), que l'export de ChessBase écrit `[%clk h:mm:ss]` à la fin du commentaire du coup.
fn move_clock(parts: &mut Parts, anns: AnnotationsRef<'_>, items: [&[(i32, u32)]; 2]) -> Result<()> {
    for item in items.into_iter().flatten() {
        if item.0 == GAME_POSITION {
            continue;
        }
        let Annotation::Other { code: 0x16 | 0x17, data } = anns.annotation_at(item.1)? else { continue };
        let Ok(d) = <[u8; 4]>::try_from(data) else { continue };
        let hundredths = if anns.is_classic() { u32::from_be_bytes(d) } else { u32::from_le_bytes(d) };
        let seconds = hundredths.saturating_add(50) / 100;
        let part = parts.push(Sep::Space);
        part.token = true;
        part.text.push_str("[%clk ");
        push_number(&mut part.text, i64::from(seconds / 3600));
        part.text.push(':');
        push_two(&mut part.text, (seconds / 60 % 60) as u8);
        part.text.push(':');
        push_two(&mut part.text, (seconds % 60) as u8);
        part.text.push(']');
    }
    Ok(())
}
