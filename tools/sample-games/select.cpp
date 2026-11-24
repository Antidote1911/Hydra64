// Chooses the 1000 games of databases/games/Masters_1000.hy64 from a large reference database.
//
//   g++ -std=c++20 -O2 -Isrc tools/sample-games/select.cpp build/libhydra64_core.a -lz -o select-games
//   ./select-games <large .hy64i or .hy64 database> <output.pgn>
//   build/hydra64-convert <output.pgn> databases/games/Masters_1000.hy64
//
// The choice is deterministic (same games on every run over the same database):
//  - complete classical games, of 20 to 120 moves, from the initial position, with a result;
//    no blitz, rapid or simultaneous games, no correspondence games or games against computers;
//  - before 1970: both players are leading masters of their time (list below);
//    from 1970: both players have a high Elo rating for the period;
//  - a number of games fixed per period, world championships first, decisive games
//    slightly favoured, without two players, one opening or a single champion dominating.
// Only the moves and the header of the game are taken: no comments, variations or symbols.
#include "db/database.h"
#include "db/format.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace hy64db;

namespace {

const char* const Legends[] = {
    "McDonnell", "De Labourdonnais", "Staunton", "Saint Amant", "Anderssen", "Morphy", "Paulsen", "Kolisch", "Steinitz", "Zukertort",
    "Blackburne", "Chigorin", "Tarrasch", "Gunsberg", "Pillsbury", "Lasker", "Schlechter", "Maroczy", "Marshall", "Janowski",
    "Rubinstein", "Capablanca", "Nimzowitsch", "Reti", "Spielmann", "Tartakower", "Bogoljubow", "Alekhine", "Euwe", "Flohr",
    "Botvinnik", "Keres", "Fine", "Reshevsky", "Smyslov", "Bronstein", "Boleslavsky", "Najdorf", "Geller", "Petrosian",
    "Tal", "Spassky", "Fischer", "Korchnoi", "Larsen", "Portisch", "Polugaevsky", "Stein", "Taimanov", "Gligoric",
    "Kotov", "Szabo", "Averbakh", "Vidmar", "Teichmann", "Duras", "Bernstein", "Winawer", "Mackenzie", "Bird",
};

const char* const Excluded[] = {"blitz", "rapid", "bullet", "simul", "blind", "armageddon", "online", "chess.com", "lichess", "titled",
                                "freestyle", "960", "exhibition", "consult", "odds", "corr", "computer", "internet", "icc ", "playchess",
                                "speed", "lightning", "handicap", "thematic", "casual", "training", "hyper", "fischer random",
                                // online rapid tournaments of the 2020s
                                "carlsen inv", "prelim", "meltwater", "champions chess tour", "cct ", "chessable", "skilling", "airthings",
                                "aimchess", "ftx ", "opera euro", "magnus", "legends of chess", "lindores", "goldmoney", "new in chess classic",
                                "julius baer", "chess tour", " ko ", "tiebreak", "tie-break", "playoff", "play-off", "wcht", "world cup", "clutch",
                                "tour final"};

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "Capablanca, Jose Raul" → legend if the family name is exactly in the list.
bool legend(const std::string& name)
{
    const std::string family = name.substr(0, name.find(','));
    for (const char* l : Legends)
        if (family == l) return true;
    return false;
}

struct Period {
    int from, to, quota, minElo; // minElo 0: period without ratings (legends)
};
const Period Periods[] = {{1834, 1899, 110, 0},    {1900, 1929, 120, 0},    {1930, 1949, 90, 0},     {1950, 1969, 130, 0},
                          {1970, 1984, 120, 2580}, {1985, 1999, 140, 2640}, {2000, 2012, 140, 2700}, {2013, 2100, 150, 2740}};

struct Candidate {
    uint32_t id;
    int score;
    int year;
    std::string white, black;
    uint16_t eco;
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage : select-games <base> <sortie.pgn>\n");
        return 2;
    }
    Database db;
    std::string err;
    if (!db.open(argv[1], &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const Names& names = db.names();
    // Excluded tournaments, player names: computed once per number.
    std::vector<int8_t> eventKind(names.count(Event), -1); // 0 normal, 1 excluded, 2 world championship
    std::vector<int8_t> isLegend(names.count(Player), -1);
    auto kindOf = [&](uint32_t e) -> int {
        if (e >= eventKind.size()) return 0;
        if (eventKind[e] < 0) {
            const std::string name = lower(names.name(Event, e));
            int kind = 0;
            for (const char* x : Excluded)
                if (name.find(x) != std::string::npos) kind = 1;
            if (!kind && (name.find("world-ch") != std::string::npos || name.find("world ch") != std::string::npos || name.rfind("wch", 0) == 0)
                && name.find("jun") == std::string::npos && name.find("u20") == std::string::npos && name.find("girls") == std::string::npos
                && name.find("senior") == std::string::npos && name.find("team") == std::string::npos && name.find("women") == std::string::npos
                && name.find("u1") == std::string::npos && name.find("u0") == std::string::npos && name.find("stud") == std::string::npos
                && name.find("open") == std::string::npos && name.find("qual") == std::string::npos)
                kind = 2;
            eventKind[e] = int8_t(kind);
        }
        return eventKind[e];
    };
    auto legendId = [&](uint32_t p) {
        if (p >= isLegend.size()) return false;
        if (isLegend[p] < 0) isLegend[p] = legend(names.name(Player, p));
        return isLegend[p] == 1;
    };

    std::vector<std::vector<Candidate>> pools(std::size(Periods));
    for (uint32_t id : db.allGames()) {
        const Record r = db.record(id);
        if ((r.flags & FlagStartFen) || r.variant != 0 || r.result == ResultNone || r.plies < 40 || r.plies > 240) continue;
        const int year = dateYear(r.date);
        size_t p = 0;
        while (p < std::size(Periods) && !(year >= Periods[p].from && year <= Periods[p].to)) ++p;
        if (p == std::size(Periods) || r.white == NoName || r.black == NoName || r.white == r.black) continue;
        const int kind = kindOf(r.event);
        if (kind == 1) continue;
        int score;
        if (Periods[p].minElo == 0) {
            if (!legendId(r.white) || !legendId(r.black)) continue;
            score = 5000;
        } else {
            if (r.whiteElo < Periods[p].minElo || r.blackElo < Periods[p].minElo) continue;
            score = r.whiteElo + r.blackElo;
        }
        // A little reproducible randomness (from the game's number), so as not to keep only
        // the decisive games or only the very top ratings.
        score += int((id * 2654435761u) >> 16) % 160;
        if (kind == 2) score += 600;
        if (r.result != Draw) score += Periods[p].minElo ? 60 : 15;
        pools[p].push_back({id, score, year, names.name(Player, r.white), names.name(Player, r.black), r.eco});
    }

    std::vector<Candidate> chosen;
    for (size_t p = 0; p < std::size(Periods); ++p) {
        std::vector<Candidate>& pool = pools[p];
        std::stable_sort(pool.begin(), pool.end(), [](const Candidate& a, const Candidate& b) { return a.score != b.score ? a.score > b.score : a.id < b.id; });
        // Variety: at most 4 games per pair of players, 6 per ECO code, 40 per player, 12 per year.
        std::map<std::string, int> pairs, players;
        std::map<int, int> ecos, years;
        const int perYear = std::max(12, Periods[p].quota * 2 / (std::min(Periods[p].to, 2025) - Periods[p].from + 1));
        int taken = 0;
        for (int pass = 0; pass < 2 && taken < Periods[p].quota; ++pass) { // second pass: caps lifted if games are missing
            for (Candidate& c : pool) {
                if (taken >= Periods[p].quota) break;
                if (c.id == UINT32_MAX) continue;
                const std::string pair = std::min(c.white, c.black) + "|" + std::max(c.white, c.black);
                if (pass == 0 && (pairs[pair] >= 4 || ecos[c.eco] >= 6 || players[c.white] >= 40 || players[c.black] >= 40 || years[c.year] >= perYear)) continue;
                ++pairs[pair], ++ecos[c.eco], ++players[c.white], ++players[c.black], ++years[c.year];
                chosen.push_back(c);
                c.id = UINT32_MAX;
                ++taken;
            }
        }
        std::printf("%d-%d : %zu candidates, %d retenues\n", Periods[p].from, std::min(Periods[p].to, 2025), pool.size(), taken);
    }

    // In chronological order (that of the reference database on equal dates).
    std::vector<std::pair<uint32_t, uint32_t>> order; // (date, id)
    for (const Candidate& c : chosen) order.push_back({db.record(c.id).date, c.id});
    std::sort(order.begin(), order.end());
    std::ofstream out(argv[2], std::ios::binary);
    size_t written = 0;
    for (const auto& [date, id] : order) {
        chess::Game g;
        if (!db.game(id, g)) continue;
        const Record r = db.record(id);
        auto tag = [&](const char* key, std::string value) {
            for (char& ch : value)
                if (ch == '"' || ch == '\\') ch = '\'';
            out << '[' << key << " \"" << value << "\"]\n";
        };
        const std::string round = names.name(Round, r.round);
        tag("Event", names.name(Event, r.event).empty() ? "?" : names.name(Event, r.event));
        tag("Site", names.name(Site, r.site).empty() ? "?" : names.name(Site, r.site));
        tag("Date", formatDate(r.date));
        tag("Round", round.empty() ? "?" : round);
        tag("White", names.name(Player, r.white));
        tag("Black", names.name(Player, r.black));
        tag("Result", formatResult(r.result));
        if (r.eco) tag("ECO", formatEco(r.eco));
        if (r.whiteElo) tag("WhiteElo", std::to_string(r.whiteElo));
        if (r.blackElo) tag("BlackElo", std::to_string(r.blackElo));
        out << '\n';
        // Main line only: no comments, variations or annotation symbols.
        std::string line;
        int ply = 0;
        for (const chess::Node* n : g.mainline()) {
            std::string word = (ply % 2 == 0 ? std::to_string(ply / 2 + 1) + ". " : std::string()) + n->san;
            ++ply;
            if (line.size() + word.size() + 1 > 79) {
                out << line << '\n';
                line.clear();
            }
            line += (line.empty() ? "" : " ") + word;
        }
        const std::string result = formatResult(r.result);
        if (line.size() + result.size() + 1 > 79) {
            out << line << '\n';
            line.clear();
        }
        out << line << (line.empty() ? "" : " ") << result << "\n\n";
        ++written;
    }
    std::printf("%zu parties écrites dans %s\n", written, argv[2]);
    return written == 1000 ? 0 : 3;
}
