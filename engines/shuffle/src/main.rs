//! Shuffle — UCI chess engine that plays silly moves, almost at random.
//!
//! UCI protocol: uci, isready, ucinewgame, setoption, position, go, stop, quit.
//! Convenience commands: `d` (displays the board), `go perft N`.

mod board;
mod pick;

use board::{Move, Position};
use pick::{ranked_moves, Rng, Style};
use std::io::{self, BufRead, Write};
use std::sync::mpsc;
use std::time::{Duration, Instant};

const NAME: &str = "Shuffle 0.1";
const AUTHOR: &str = "Hydra64";

struct Options {
    style: Style,
    seed: u64,      // 0: new random seed for each game
    delay_ms: u64,  // fake thinking, so as not to play instantly
    multipv: usize,
}

/// Search in progress: the move is already chosen, we just wait for the deadline (or "stop").
struct Pending {
    best: Option<Move>,
    deadline: Option<Instant>, // None: "go infinite", we wait for "stop"
}

fn send(out: &mut impl Write, line: &str) {
    let _ = writeln!(out, "{line}");
    let _ = out.flush();
}

fn bestmove(out: &mut impl Write, m: Option<Move>) {
    send(out, &format!("bestmove {}", m.map_or_else(|| "0000".to_string(), |m| m.uci())));
}

/// "position startpos moves e2e4 …" or "position fen <fen> moves …"
fn parse_position(args: &[&str]) -> Option<Position> {
    let moves_at = args.iter().position(|&t| t == "moves");
    let (setup, moves) = match moves_at {
        Some(i) => (&args[..i], &args[i + 1..]),
        None => (args, &[][..]),
    };
    let mut pos = match setup.first() {
        Some(&"startpos") => Position::start(),
        Some(&"fen") => Position::from_fen(&setup[1..].join(" "))?,
        _ => return None,
    };
    for text in moves {
        let m = pos.parse_uci(text)?;
        pos.make(m);
    }
    Some(pos)
}

fn arg_value(args: &[&str], key: &str) -> Option<u64> {
    args.iter().position(|&t| t == key).and_then(|i| args.get(i + 1)).and_then(|v| v.parse::<i64>().ok()).map(|v| v.max(0) as u64)
}

fn perft(p: &Position, depth: u32) -> u64 {
    if depth == 0 {
        return 1;
    }
    p.legal_moves().iter().map(|&m| perft(&p.after(m), depth - 1)).sum()
}

fn print_board(out: &mut impl Write, p: &Position) {
    const LETTERS: &[u8] = b".pnbrqk";
    for r in (0..8).rev() {
        let row: String = (0..8)
            .map(|f| {
                let v = p.board[r * 8 + f];
                let c = LETTERS[v.unsigned_abs() as usize] as char;
                if v > 0 { c.to_ascii_uppercase() } else { c }
            })
            .map(|c| format!(" {c}"))
            .collect();
        send(out, &format!("{}{row}", r + 1));
    }
    send(out, "  a b c d e f g h");
    send(out, &format!("Trait : {}", if p.side == board::WHITE { "Blancs" } else { "Noirs" }));
}

fn main() {
    // Input read in a separate thread: "stop" and "quit" are still taken into account
    // during the (fake) thinking.
    let (tx, rx) = mpsc::channel::<String>();
    std::thread::spawn(move || {
        for line in io::stdin().lock().lines() {
            match line {
                Ok(l) => {
                    if tx.send(l).is_err() {
                        break;
                    }
                }
                Err(_) => break,
            }
        }
    });

    let mut out = io::stdout();
    let mut pos = Position::start();
    let mut opts = Options { style: Style::Silly, seed: 0, delay_ms: 300, multipv: 1 };
    let mut rng = Rng::from_entropy();
    let mut pending: Option<Pending> = None;

    loop {
        // Waiting for a command, or for the deadline of the search in progress
        let line = match pending.as_ref().and_then(|p| p.deadline) {
            Some(deadline) => match rx.recv_timeout(deadline.saturating_duration_since(Instant::now())) {
                Ok(l) => l,
                Err(mpsc::RecvTimeoutError::Timeout) => {
                    let p = pending.take().unwrap();
                    bestmove(&mut out, p.best);
                    continue;
                }
                Err(mpsc::RecvTimeoutError::Disconnected) => break,
            },
            None => match rx.recv() {
                Ok(l) => l,
                Err(_) => break,
            },
        };

        let tokens: Vec<&str> = line.split_whitespace().collect();
        let Some(&cmd) = tokens.first() else { continue };
        let args = &tokens[1..];
        match cmd {
            "uci" => {
                send(&mut out, &format!("id name {NAME}"));
                send(&mut out, &format!("id author {AUTHOR}"));
                send(&mut out, "option name Style type combo default Silly var Silly var Random");
                send(&mut out, "option name Seed type spin default 0 min 0 max 2147483647");
                send(&mut out, "option name Move Delay type spin default 300 min 0 max 10000");
                send(&mut out, "option name MultiPV type spin default 1 min 1 max 256");
                send(&mut out, "uciok");
            }
            "isready" => send(&mut out, "readyok"),
            "ucinewgame" => {
                pos = Position::start();
                rng = if opts.seed != 0 { Rng::new(opts.seed) } else { Rng::from_entropy() };
            }
            "setoption" => {
                // setoption name <name…> value <value…>
                let value_at = args.iter().position(|&t| t == "value");
                let name = args.get(1..value_at.unwrap_or(args.len())).map(|v| v.join(" ")).unwrap_or_default();
                let value = value_at.map(|i| args[i + 1..].join(" ")).unwrap_or_default();
                match name.to_ascii_lowercase().as_str() {
                    "style" => {
                        if let Some(s) = Style::parse(&value) {
                            opts.style = s;
                        }
                    }
                    "seed" => {
                        opts.seed = value.parse().unwrap_or(0);
                        rng = if opts.seed != 0 { Rng::new(opts.seed) } else { Rng::from_entropy() };
                    }
                    "move delay" => opts.delay_ms = value.parse().unwrap_or(opts.delay_ms).min(10_000),
                    "multipv" => opts.multipv = value.parse().unwrap_or(1usize).clamp(1, 256),
                    _ => {} // unknown options ignored (Threads, Hash… sent by some interfaces)
                }
            }
            "position" => {
                if let Some(p) = parse_position(args) {
                    pos = p;
                } else {
                    send(&mut out, "info string position invalide ignorée");
                }
            }
            "go" => {
                if args.first() == Some(&"perft") {
                    let depth = args.get(1).and_then(|d| d.parse().ok()).unwrap_or(1);
                    let start = Instant::now();
                    let n = perft(&pos, depth);
                    send(&mut out, &format!("info string perft {depth} : {n} nœuds en {} ms", start.elapsed().as_millis()));
                    continue;
                }
                let start = Instant::now();
                let ranked = ranked_moves(&pos, opts.style, &mut rng);
                // "Analysis" lines: the score is simply the material after the move
                for (i, m) in ranked.iter().take(opts.multipv).enumerate() {
                    let next = pos.after(*m);
                    let score = if next.in_check() && next.legal_moves().is_empty() {
                        "mate 1".to_string()
                    } else {
                        format!("cp {}", -next.material())
                    };
                    send(&mut out, &format!("info depth 1 seldepth 1 multipv {} score {score} nodes {} nps 0 time 0 pv {}", i + 1, ranked.len(), m.uci()));
                }
                if ranked.is_empty() {
                    let score = if pos.in_check() { "mate 0" } else { "cp 0" };
                    send(&mut out, &format!("info depth 0 score {score}"));
                }
                // "Thinking" time: the chosen delay, without exceeding the available time
                let mut think = opts.delay_ms;
                if let Some(mt) = arg_value(args, "movetime") {
                    think = think.min(mt);
                }
                let my_time = if pos.side == board::WHITE { arg_value(args, "wtime") } else { arg_value(args, "btime") };
                if let Some(t) = my_time {
                    think = think.min(t / 40);
                }
                if arg_value(args, "depth").is_some() || arg_value(args, "nodes").is_some() {
                    think = 0;
                }
                let infinite = args.contains(&"infinite") || args.contains(&"ponder");
                pending = Some(Pending {
                    best: ranked.first().copied(),
                    deadline: if infinite { None } else { Some(start + Duration::from_millis(think)) },
                });
            }
            "stop" => {
                if let Some(p) = pending.take() {
                    bestmove(&mut out, p.best);
                }
            }
            "ponderhit" => {
                if let Some(p) = pending.as_mut() {
                    p.deadline = Some(Instant::now());
                }
            }
            "d" => print_board(&mut out, &pos),
            "quit" => break,
            _ => send(&mut out, &format!("info string commande inconnue : {cmd}")),
        }
    }
}
