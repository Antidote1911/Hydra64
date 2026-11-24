// Builds a PGN database of endgame positions (3 to 6 pieces) to try the Syzygy tables:
// each "game" is a position without moves, whose exact result, distance and
// best move come from the tables themselves.
//
//   generate <tables folder> <output.pgn>
//
// Positions drawn at random with a fixed seed (the result is therefore reproducible), quiet
// (no check, no capture possible on the first move), plus a few classic positions.
#include "chess/pgn.h"
#include "chess/tablebase.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace chess;

namespace {

struct Entry {
    std::string fen, material, name;
    int pieces = 0;
};

std::string fenOf(const std::string& white, const std::string& black, std::mt19937& rng, bool whiteToMove)
{
    char board[64];
    for (char& c : board) c = 0;
    auto place = [&](char piece, bool isWhite) {
        for (int tries = 0; tries < 200; ++tries) {
            const int sq = int(rng() % 64), rank = sq / 8;
            if (board[sq]) continue;
            if (piece == 'P' && (rank == 0 || rank == 7)) continue;
            board[sq] = isWhite ? piece : char(piece + 32);
            return true;
        }
        return false;
    };
    for (char c : white)
        if (!place(c, true)) return {};
    for (char c : black)
        if (!place(c, false)) return {};
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const char c = board[rank * 8 + file];
            if (!c) {
                ++empty;
                continue;
            }
            if (empty) fen += char('0' + empty), empty = 0;
            fen += c;
        }
        if (empty) fen += char('0' + empty);
        if (rank) fen += '/';
    }
    return fen + (whiteToMove ? " w - - 0 1" : " b - - 0 1");
}

// Playable and quiet position: legal, no check, no capture on the first move, moves to play.
bool quiet(const Position& pos)
{
    const Color us = pos.sideToMove(), them = us == White ? Black : White;
    Square kings[2] = {NoSquare, NoSquare};
    for (int s = 0; s < 64; ++s) {
        const Piece p = pos.at(Square(s));
        if (p != NoPiece && typeOf(p) == King) kings[colorOf(p) == White ? 0 : 1] = Square(s);
    }
    if (kings[0] == NoSquare || kings[1] == NoSquare) return false;
    if (pos.isAttacked(kings[them == White ? 0 : 1], us)) return false; // the side that just moved would be in check
    if (pos.inCheck()) return false;
    const auto moves = pos.legalMoves();
    if (moves.empty()) return false;
    for (const Move& m : moves)
        if (pos.at(m.to) != NoPiece) return false;
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "Usage : %s <dossier des tables> <sortie.pgn>\n", argv[0]);
        return 2;
    }
    const int largest = tb::init(argv[1]);
    if (largest < 6) {
        std::fprintf(stderr, "Les tables de %s ne couvrent que %d pièces : il en faut 6.\n", argv[1], largest);
        return 1;
    }
    std::vector<Entry> entries;
    // Classic positions (the name is indicative; the result comes from the tables as for the others)
    const std::vector<Entry> classics = {
        {"8/8/8/4k3/8/4K3/4P3/8 w - - 0 1", "KPvK", "Opposition : trait aux Blancs", 3},
        {"8/8/8/4k3/8/4K3/4P3/8 b - - 0 1", "KPvK", "Opposition : trait aux Noirs", 3},
        {"8/8/1KP5/3r4/8/8/8/k7 w - - 0 1", "KPvKR", "Étude de Saavedra", 4},
        {"7K/8/k1P5/7p/8/8/8/8 w - - 0 1", "KPvKP", "Étude de Réti", 4},
        {"k7/8/8/8/8/8/P7/K1B5 w - - 0 1", "KBPvK", "Pion tour et mauvais fou", 4},
        {"7k/8/8/8/8/8/8/KBN5 w - - 0 1", "KBNvK", "Mat fou et cavalier", 4},
        {"1K1k4/1P6/8/8/8/8/r7/2R5 w - - 0 1", "KRPvKR", "Position de Lucena", 5},
        {"4k3/R7/7r/3KP3/8/8/8/8 w - - 0 1", "KRPvKR", "Défense de Philidor (troisième rangée)", 5},
        {"8/8/8/8/3p4/8/8/KNN4k w - - 0 1", "KNNvKP", "Deux cavaliers contre pion", 5},
    };
    for (const Entry& e : classics) entries.push_back(e);

    struct Plan {
        const char *white, *black;
        int count;
    };
    const std::vector<Plan> plans = {
        // 3 pieces
        {"KQ", "K", 1}, {"KR", "K", 1}, {"KP", "K", 2},
        // 4 pieces
        {"KQ", "KR", 2}, {"KR", "KB", 1}, {"KR", "KN", 1}, {"KQ", "KP", 2}, {"KR", "KP", 2}, {"KP", "KP", 1}, {"KBB", "K", 1},
        // 5 pieces
        {"KRP", "KR", 4}, {"KQP", "KQ", 3}, {"KBP", "KB", 2}, {"KNP", "KN", 2}, {"KPP", "KP", 3}, {"KRB", "KR", 2}, {"KRN", "KR", 1}, {"KQ", "KRP", 2},
        {"KR", "KPP", 2}, {"KBN", "KP", 1}, {"KRR", "KQ", 1},
        // 6 pieces: the 27 endgames of the "Endgame Turbo 4" DVDs
        {"KRP", "KRP", 2}, {"KRPP", "KR", 2}, {"KQP", "KQP", 2}, {"KQPP", "KQ", 2}, {"KPP", "KPP", 2}, {"KPPP", "KP", 2}, {"KRP", "KBP", 2}, {"KRP", "KNP", 2},
        {"KRP", "KPP", 2}, {"KBP", "KBP", 2}, {"KBP", "KNP", 2}, {"KNP", "KNP", 2}, {"KBPP", "KB", 2}, {"KBPP", "KN", 2}, {"KNPP", "KB", 2}, {"KNPP", "KN", 2},
        {"KBP", "KPP", 2}, {"KNP", "KPP", 2}, {"KBPP", "KR", 2}, {"KNPP", "KR", 2}, {"KPPP", "KB", 2}, {"KPPP", "KN", 2}, {"KPPP", "KR", 2}, {"KPPP", "KQ", 2},
        {"KRPP", "KQ", 2}, {"KQPP", "KP", 2}, {"KQP", "KPP", 2},
    };
    std::mt19937 rng(64); // fixed seed
    std::set<std::string> seen;
    for (const Plan& plan : plans) {
        const std::string material = std::string(plan.white) + "v" + plan.black;
        const int pieces = int(material.size()) - 1;
        // Varied results for the same endgame: first a win that takes work, then something else
        std::vector<int> wanted; // wdl from the point of view of the side to move, 99: does not matter
        for (int k = 0; k < plan.count; ++k) wanted.push_back(k % 3 == 0 ? 2 : k % 3 == 1 ? 0 : -2);
        for (int want : wanted) {
            bool found = false;
            for (int tries = 0; tries < 60000 && !found; ++tries) {
                if (tries == 40000) want = 99; // this result is too rare in this endgame
                const std::string fen = fenOf(plan.white, plan.black, rng, rng() % 2 == 0);
                Position pos;
                if (fen.empty() || seen.count(fen) || !pos.setFen(fen) || !quiet(pos)) continue;
                const tb::Probe p = tb::probe(pos);
                if (!p.ok || p.moves.empty()) continue;
                if (want != 99 && p.wdl != want) continue;
                if (p.wdl != 0 && std::abs(p.dtz) < 8 && tries < 30000) continue; // immediate win or loss: not very instructive
                seen.insert(fen);
                entries.push_back({fen, material, "", pieces});
                found = true;
            }
            if (!found) std::fprintf(stderr, "%s : aucune position trouvée\n", material.c_str());
        }
    }

    std::ofstream out(argv[2]);
    int written = 0, byPieces[8] = {};
    for (const Entry& e : entries) {
        Position pos;
        if (!pos.setFen(e.fen)) continue;
        const tb::Probe p = tb::probe(pos);
        if (!p.ok || p.moves.empty()) {
            std::fprintf(stderr, "%s : non couverte par les tables (%s)\n", e.fen.c_str(), e.material.c_str());
            continue;
        }
        const bool whiteToMove = pos.sideToMove() == White;
        const int forWhite = whiteToMove ? p.wdl : -p.wdl;
        const char* result = forWhite == 2 ? "1-0" : forWhite == -2 ? "0-1" : "1/2-1/2";
        std::string verdict = forWhite == 2 ? "gain des Blancs" : forWhite == -2 ? "gain des Noirs" : forWhite == 1 ? "gain des Blancs annulé par la règle des 50 coups (nulle)"
                                                                                  : forWhite == -1 ? "gain des Noirs annulé par la règle des 50 coups (nulle)" : "nulle";
        const std::string best = pos.toSan(p.moves.front().move);
        std::string comment = "Tables : " + verdict;
        if (p.wdl != 0) comment += ", DTZ " + std::to_string(std::abs(p.dtz));
        comment += ". Trait aux " + std::string(whiteToMove ? "Blancs" : "Noirs") + ". Meilleur coup : " + best + ".";
        int winning = 0, drawing = 0, losing = 0;
        for (const auto& m : p.moves) (m.wdl > 0 ? winning : m.wdl == 0 ? drawing : losing) += 1;
        comment += " Coups du camp au trait : " + std::to_string(winning) + " gagnent, " + std::to_string(drawing) + " annulent, " + std::to_string(losing) + " perdent.";
        Game g(pos);
        g.setHeader("Event", "Tables de finales, " + std::to_string(e.pieces) + " pièces");
        g.setHeader("Site", e.name.empty() ? "Position tirée au hasard" : e.name);
        g.setHeader("Round", std::to_string(++written));
        g.setHeader("White", e.material + (whiteToMove ? " (trait aux Blancs)" : " (trait aux Noirs)"));
        g.setHeader("Black", verdict + (p.wdl != 0 ? ", DTZ " + std::to_string(std::abs(p.dtz)) : ""));
        g.setHeader("Result", result);
        g.setHeader("SetUp", "1");
        g.setHeader("FEN", e.fen);
        g.root()->comment = comment;
        out << pgn::writeGame(g) << "\n";
        ++byPieces[e.pieces];
    }
    std::printf("%d positions : %d à 3 pièces, %d à 4, %d à 5, %d à 6\n", written, byPieces[3], byPieces[4], byPieces[5], byPieces[6]);
    return 0;
}
