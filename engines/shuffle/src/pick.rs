//! Choice of moves: pure randomness, or "silly" randomness with a few naive preferences.

use crate::board::{Move, Position, EMPTY, KNIGHT, QUEEN};

/// xorshift64* pseudo-random generator (no external dependency).
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Rng {
        Rng(seed.max(1))
    }

    /// Seed drawn from the clock and the process number.
    pub fn from_entropy() -> Rng {
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos() as u64)
            .unwrap_or(0x9E37_79B9_7F4A_7C15);
        Rng::new(nanos ^ ((std::process::id() as u64) << 32) ^ 0x2545_F491_4F6C_DD1D)
    }

    pub fn next_u64(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }

    /// Uniform real in [0, 1).
    pub fn next_f64(&mut self) -> f64 {
        (self.next_u64() >> 11) as f64 / (1u64 << 53) as f64
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Style {
    /// All legal moves have the same chance.
    Random,
    /// At random, but attracted by captures, checks, promotions… and mate when it sees it.
    Silly,
}

impl Style {
    pub fn parse(s: &str) -> Option<Style> {
        match s.to_ascii_lowercase().as_str() {
            "random" => Some(Style::Random),
            "silly" => Some(Style::Silly),
            _ => None,
        }
    }
}

/// Weight of a move: the larger it is, the more likely the move is to be chosen.
fn weight(pos: &Position, m: Move, style: Style) -> f64 {
    if style == Style::Random {
        return 1.0;
    }
    let next = pos.after(m);
    let mut w = 1.0;
    if pos.board[m.to as usize] != EMPTY {
        w *= 3.0; // "a free piece!" (whether it is defended or not)
    }
    if next.in_check() {
        w *= 2.0;
        if next.legal_moves().is_empty() {
            w *= 40.0; // even Shuffle often notices a mate in one
        }
    }
    match m.promo {
        QUEEN => w *= 4.0,
        KNIGHT => w *= 0.6, // an underpromotion now and then, for style
        p if p != EMPTY => w *= 0.3,
        _ => {}
    }
    w
}

/// Orders the legal moves by weighted draw without replacement: the first is the move played,
/// the following ones serve as secondary lines (MultiPV).
pub fn ranked_moves(pos: &Position, style: Style, rng: &mut Rng) -> Vec<Move> {
    let mut pool: Vec<(Move, f64)> = pos.legal_moves().into_iter().map(|m| (m, weight(pos, m, style))).collect();
    let mut ranked = Vec::with_capacity(pool.len());
    while !pool.is_empty() {
        let total: f64 = pool.iter().map(|(_, w)| w).sum();
        let mut target = rng.next_f64() * total;
        let mut index = pool.len() - 1;
        for (i, (_, w)) in pool.iter().enumerate() {
            if target < *w {
                index = i;
                break;
            }
            target -= w;
        }
        ranked.push(pool.swap_remove(index).0);
    }
    ranked
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_seed_same_moves() {
        let p = Position::start();
        let a = ranked_moves(&p, Style::Silly, &mut Rng::new(42));
        let b = ranked_moves(&p, Style::Silly, &mut Rng::new(42));
        assert_eq!(a, b);
        assert_eq!(a.len(), 20);
    }

    #[test]
    fn silly_usually_finds_mate_in_one() {
        // Back-rank mate: Rd8#
        let p = Position::from_fen("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1").unwrap();
        let mut rng = Rng::new(7);
        let mates = (0..1000).filter(|_| ranked_moves(&p, Style::Silly, &mut rng)[0].uci() == "d1d8").count();
        assert!(mates > 700, "mat trouvé {mates} fois sur 1000");
    }

    #[test]
    fn random_is_roughly_uniform() {
        let p = Position::start();
        let mut rng = Rng::new(3);
        let mut counts = std::collections::HashMap::new();
        for _ in 0..20_000 {
            *counts.entry(ranked_moves(&p, Style::Random, &mut rng)[0].uci()).or_insert(0) += 1;
        }
        assert_eq!(counts.len(), 20);
        assert!(counts.values().all(|&c| (800..1200).contains(&c)), "{counts:?}");
    }
}
