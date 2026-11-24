//! Rules of chess: position, FEN, generation of legal moves, UCI notation.
//! Simple "8x8" representation: more than enough for an engine that does not calculate.

pub const EMPTY: i8 = 0;
pub const PAWN: i8 = 1;
pub const KNIGHT: i8 = 2;
pub const BISHOP: i8 = 3;
pub const ROOK: i8 = 4;
pub const QUEEN: i8 = 5;
pub const KING: i8 = 6;

/// Colour: +1 for White, -1 for Black (sign of the pieces).
pub type Color = i8;
pub const WHITE: Color = 1;
pub const BLACK: Color = -1;

const KNIGHT_D: [(i32, i32); 8] = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)];
const KING_D: [(i32, i32); 8] = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)];
const BISHOP_D: [(i32, i32); 4] = [(1, 1), (1, -1), (-1, 1), (-1, -1)];
const ROOK_D: [(i32, i32); 4] = [(1, 0), (-1, 0), (0, 1), (0, -1)];

const CASTLE_WK: u8 = 1;
const CASTLE_WQ: u8 = 2;
const CASTLE_BK: u8 = 4;
const CASTLE_BQ: u8 = 8;

pub const START_FEN: &str = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Move {
    pub from: u8,
    pub to: u8,
    pub promo: i8, // piece type (KNIGHT..QUEEN) or EMPTY
}

impl Move {
    pub fn uci(&self) -> String {
        let mut s = format!("{}{}", square_name(self.from), square_name(self.to));
        if self.promo != EMPTY {
            s.push(match self.promo {
                KNIGHT => 'n',
                BISHOP => 'b',
                ROOK => 'r',
                _ => 'q',
            });
        }
        s
    }
}

pub fn square_name(sq: u8) -> String {
    format!("{}{}", (b'a' + sq % 8) as char, (b'1' + sq / 8) as char)
}

fn on_board(f: i32, r: i32) -> bool {
    (0..8).contains(&f) && (0..8).contains(&r)
}

fn sq(f: i32, r: i32) -> usize {
    (r * 8 + f) as usize
}

#[derive(Clone, Debug)]
pub struct Position {
    pub board: [i8; 64], // index 0 = a1; signed piece (+ white, - black)
    pub side: Color,
    castling: u8,
    ep: Option<u8>,
    pub halfmove: u32,
    pub fullmove: u32,
}

impl Position {
    pub fn start() -> Position {
        Position::from_fen(START_FEN).expect("FEN initiale")
    }

    pub fn from_fen(fen: &str) -> Option<Position> {
        let mut parts = fen.split_whitespace();
        let placement = parts.next()?;
        let mut board = [EMPTY; 64];
        let (mut file, mut rank) = (0i32, 7i32);
        for c in placement.chars() {
            match c {
                '/' => {
                    rank -= 1;
                    file = 0;
                }
                '1'..='8' => file += c as i32 - '0' as i32,
                _ => {
                    let kind = match c.to_ascii_lowercase() {
                        'p' => PAWN,
                        'n' => KNIGHT,
                        'b' => BISHOP,
                        'r' => ROOK,
                        'q' => QUEEN,
                        'k' => KING,
                        _ => return None,
                    };
                    if !on_board(file, rank) {
                        return None;
                    }
                    board[sq(file, rank)] = if c.is_ascii_uppercase() { kind } else { -kind };
                    file += 1;
                }
            }
        }
        let side = match parts.next().unwrap_or("w") {
            "b" => BLACK,
            _ => WHITE,
        };
        let mut castling = 0;
        for c in parts.next().unwrap_or("-").chars() {
            castling |= match c {
                'K' => CASTLE_WK,
                'Q' => CASTLE_WQ,
                'k' => CASTLE_BK,
                'q' => CASTLE_BQ,
                _ => 0,
            };
        }
        let ep = parse_square(parts.next().unwrap_or("-"));
        let halfmove = parts.next().and_then(|s| s.parse().ok()).unwrap_or(0);
        let fullmove = parts.next().and_then(|s| s.parse().ok()).unwrap_or(1);
        let pos = Position { board, side, castling, ep, halfmove, fullmove };
        if pos.king_square(WHITE).is_none() || pos.king_square(BLACK).is_none() {
            return None;
        }
        Some(pos)
    }

    pub fn king_square(&self, c: Color) -> Option<usize> {
        self.board.iter().position(|&p| p == KING * c)
    }

    /// Is square `s` attacked by side `by`?
    pub fn attacked(&self, s: usize, by: Color) -> bool {
        let (f, r) = ((s % 8) as i32, (s / 8) as i32);
        let pr = r - by as i32; // white pawns attack upwards
        for df in [-1, 1] {
            if on_board(f + df, pr) && self.board[sq(f + df, pr)] == PAWN * by {
                return true;
            }
        }
        for (df, dr) in KNIGHT_D {
            if on_board(f + df, r + dr) && self.board[sq(f + df, r + dr)] == KNIGHT * by {
                return true;
            }
        }
        for (df, dr) in KING_D {
            if on_board(f + df, r + dr) && self.board[sq(f + df, r + dr)] == KING * by {
                return true;
            }
        }
        let slide = |dirs: &[(i32, i32)], kind: i8| {
            for &(df, dr) in dirs {
                let (mut ff, mut rr) = (f + df, r + dr);
                while on_board(ff, rr) {
                    let p = self.board[sq(ff, rr)];
                    if p != EMPTY {
                        if p == kind * by || p == QUEEN * by {
                            return true;
                        }
                        break;
                    }
                    ff += df;
                    rr += dr;
                }
            }
            false
        };
        slide(&BISHOP_D, BISHOP) || slide(&ROOK_D, ROOK)
    }

    pub fn in_check(&self) -> bool {
        self.king_square(self.side).is_some_and(|k| self.attacked(k, -self.side))
    }

    fn push_pawn(&self, out: &mut Vec<Move>, from: usize, to: usize) {
        let last = if self.side == WHITE { 7 } else { 0 };
        if to / 8 == last {
            for p in [QUEEN, ROOK, BISHOP, KNIGHT] {
                out.push(Move { from: from as u8, to: to as u8, promo: p });
            }
        } else {
            out.push(Move { from: from as u8, to: to as u8, promo: EMPTY });
        }
    }

    fn pseudo_moves(&self) -> Vec<Move> {
        let us = self.side;
        let mut out = Vec::with_capacity(64);
        for s in 0..64usize {
            let p = self.board[s];
            if p == EMPTY || p.signum() != us {
                continue;
            }
            let (f, r) = ((s % 8) as i32, (s / 8) as i32);
            match p.abs() {
                PAWN => {
                    let dir = us as i32;
                    let start = if us == WHITE { 1 } else { 6 };
                    if on_board(f, r + dir) && self.board[sq(f, r + dir)] == EMPTY {
                        self.push_pawn(&mut out, s, sq(f, r + dir));
                        if r == start && self.board[sq(f, r + 2 * dir)] == EMPTY {
                            out.push(Move { from: s as u8, to: sq(f, r + 2 * dir) as u8, promo: EMPTY });
                        }
                    }
                    for df in [-1, 1] {
                        if !on_board(f + df, r + dir) {
                            continue;
                        }
                        let t = sq(f + df, r + dir);
                        let target = self.board[t];
                        if (target != EMPTY && target.signum() != us) || self.ep == Some(t as u8) {
                            self.push_pawn(&mut out, s, t);
                        }
                    }
                }
                KNIGHT | KING => {
                    let deltas: &[(i32, i32)] = if p.abs() == KNIGHT { &KNIGHT_D } else { &KING_D };
                    for &(df, dr) in deltas {
                        if on_board(f + df, r + dr) {
                            let t = sq(f + df, r + dr);
                            if self.board[t] == EMPTY || self.board[t].signum() != us {
                                out.push(Move { from: s as u8, to: t as u8, promo: EMPTY });
                            }
                        }
                    }
                }
                kind => {
                    let mut dirs: Vec<(i32, i32)> = Vec::new();
                    if kind != ROOK {
                        dirs.extend_from_slice(&BISHOP_D);
                    }
                    if kind != BISHOP {
                        dirs.extend_from_slice(&ROOK_D);
                    }
                    for (df, dr) in dirs {
                        let (mut ff, mut rr) = (f + df, r + dr);
                        while on_board(ff, rr) {
                            let t = sq(ff, rr);
                            if self.board[t] != EMPTY {
                                if self.board[t].signum() != us {
                                    out.push(Move { from: s as u8, to: t as u8, promo: EMPTY });
                                }
                                break;
                            }
                            out.push(Move { from: s as u8, to: t as u8, promo: EMPTY });
                            ff += df;
                            rr += dr;
                        }
                    }
                }
            }
        }
        // Castling
        let base = if us == WHITE { 0 } else { 56 };
        let (k_right, q_right) = if us == WHITE { (CASTLE_WK, CASTLE_WQ) } else { (CASTLE_BK, CASTLE_BQ) };
        let e = base + 4;
        if self.board[e] == KING * us && self.castling & (k_right | q_right) != 0 && !self.attacked(e, -us) {
            if self.castling & k_right != 0
                && self.board[base + 7] == ROOK * us
                && self.board[base + 5] == EMPTY
                && self.board[base + 6] == EMPTY
                && !self.attacked(base + 5, -us)
                && !self.attacked(base + 6, -us)
            {
                out.push(Move { from: e as u8, to: (base + 6) as u8, promo: EMPTY });
            }
            if self.castling & q_right != 0
                && self.board[base] == ROOK * us
                && self.board[base + 1] == EMPTY
                && self.board[base + 2] == EMPTY
                && self.board[base + 3] == EMPTY
                && !self.attacked(base + 3, -us)
                && !self.attacked(base + 2, -us)
            {
                out.push(Move { from: e as u8, to: (base + 2) as u8, promo: EMPTY });
            }
        }
        out
    }

    pub fn legal_moves(&self) -> Vec<Move> {
        self.pseudo_moves()
            .into_iter()
            .filter(|&m| {
                let next = self.after(m);
                next.king_square(self.side).is_some_and(|k| !next.attacked(k, -self.side))
            })
            .collect()
    }

    pub fn after(&self, m: Move) -> Position {
        let mut p = self.clone();
        p.make(m);
        p
    }

    pub fn make(&mut self, m: Move) {
        let (from, to) = (m.from as usize, m.to as usize);
        let piece = self.board[from];
        let capture = self.board[to] != EMPTY;
        self.halfmove += 1;
        if piece.abs() == PAWN || capture {
            self.halfmove = 0;
        }
        if piece.abs() == PAWN && self.ep == Some(m.to) {
            // en passant: the captured pawn is alongside
            self.board[(from / 8) * 8 + to % 8] = EMPTY;
        }
        self.ep = None;
        if piece.abs() == PAWN && (to as i32 - from as i32).abs() == 16 {
            self.ep = Some(((from + to) / 2) as u8);
        }
        if piece.abs() == KING && (to as i32 % 8 - from as i32 % 8).abs() == 2 {
            let base = (from / 8) * 8;
            if to % 8 == 6 {
                self.board[base + 5] = self.board[base + 7];
                self.board[base + 7] = EMPTY;
            } else {
                self.board[base + 3] = self.board[base];
                self.board[base] = EMPTY;
            }
        }
        self.board[to] = if m.promo != EMPTY { m.promo * piece.signum() } else { piece };
        self.board[from] = EMPTY;
        for s in [from, to] {
            self.castling &= match s {
                0 => !CASTLE_WQ,
                7 => !CASTLE_WK,
                4 => !(CASTLE_WK | CASTLE_WQ),
                56 => !CASTLE_BQ,
                63 => !CASTLE_BK,
                60 => !(CASTLE_BK | CASTLE_BQ),
                _ => 0xFF,
            };
        }
        if self.side == BLACK {
            self.fullmove += 1;
        }
        self.side = -self.side;
    }

    /// Move in UCI notation ("e2e4", "e7e8q"), if it is legal.
    pub fn parse_uci(&self, text: &str) -> Option<Move> {
        let t = text.as_bytes();
        if t.len() < 4 {
            return None;
        }
        let from = parse_square(&text[0..2])?;
        let to = parse_square(&text[2..4])?;
        let promo = match t.get(4).map(|c| c.to_ascii_lowercase()) {
            Some(b'q') => QUEEN,
            Some(b'r') => ROOK,
            Some(b'b') => BISHOP,
            Some(b'n') => KNIGHT,
            _ => EMPTY,
        };
        self.legal_moves().into_iter().find(|m| m.from == from && m.to == to && m.promo == promo)
    }

    /// Material from the point of view of the side to move, in centipawns.
    pub fn material(&self) -> i32 {
        const VALUES: [i32; 7] = [0, 100, 300, 320, 500, 900, 0];
        let total: i32 = self.board.iter().map(|&p| VALUES[p.unsigned_abs() as usize] * p.signum() as i32).sum();
        total * self.side as i32
    }
}

pub fn parse_square(s: &str) -> Option<u8> {
    let b = s.as_bytes();
    if b.len() < 2 || !(b'a'..=b'h').contains(&b[0]) || !(b'1'..=b'8').contains(&b[1]) {
        return None;
    }
    Some((b[1] - b'1') * 8 + (b[0] - b'a'))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn perft(p: &Position, depth: u32) -> u64 {
        if depth == 0 {
            return 1;
        }
        let moves = p.legal_moves();
        if depth == 1 {
            return moves.len() as u64;
        }
        moves.iter().map(|&m| perft(&p.after(m), depth - 1)).sum()
    }

    #[test]
    fn perft_reference_positions() {
        let cases = [
            (START_FEN, 4, 197_281),
            ("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97_862),
            ("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43_238),
            ("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9_467),
            ("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62_379),
        ];
        for (fen, depth, nodes) in cases {
            let p = Position::from_fen(fen).unwrap();
            assert_eq!(perft(&p, depth), nodes, "{fen}");
        }
    }

    #[test]
    fn uci_round_trip() {
        let p = Position::start();
        let m = p.parse_uci("e2e4").unwrap();
        assert_eq!(m.uci(), "e2e4");
        assert!(p.parse_uci("e2e5").is_none());
        let promo = Position::from_fen("8/4P3/8/8/8/8/8/k6K w - - 0 1").unwrap();
        assert_eq!(promo.parse_uci("e7e8n").unwrap().promo, KNIGHT);
    }
}
