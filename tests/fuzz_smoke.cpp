// Damaged files: the readers must refuse them or read what they can, never crash.
//
//   fuzz_smoke <source folder> [rounds] [seed]
//
// Each round takes a sound file of the repository (a PGN, a single-file Hydra64 database), changes a
// few bytes of it (or cuts it short), then reads it the way the program does. Nothing is compared:
// the test passes if the process is still there at the end. It is worth running in the ASan / UBSan
// build (./build.sh asan --test), where a read outside a buffer stops it. The draw is fixed by the
// seed: a failure is reproduced with the same numbers, printed at each step.
//
// Not covered, for lack of files that may be put in the repository: ChessBase databases (.cbh),
// ChessBase books (.ctg), Chess Assistant / Aquarium databases and trees.
#include "chess/epd.h"
#include "chess/game.h"
#include "chess/pgn.h"
#include "db/database.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A few bytes replaced, a run of bytes overwritten with one value, or the end cut off.
std::string damaged(const std::string& sound, std::mt19937_64& rng)
{
    std::string s = sound;
    if (s.empty()) return s;
    auto at = [&] { return size_t(rng() % s.size()); };
    switch (rng() % 4) {
    case 0: // single bytes
        for (int n = 1 + int(rng() % 8); n > 0; --n) s[at()] = char(rng());
        break;
    case 1: { // a run of the same value (0x00, 0xFF: the usual lengths and offsets gone wrong)
        const size_t from = at();
        const size_t len = std::min<size_t>(1 + rng() % 64, s.size() - from);
        const char v = (rng() & 1) ? char(0xFF) : char(0);
        std::fill_n(s.begin() + std::ptrdiff_t(from), len, v);
        break;
    }
    case 2: // cut short
        s.resize(at());
        break;
    default: { // one bit, in the first bytes (headers) half of the time
        const size_t i = (rng() & 1) ? size_t(rng() % std::min<size_t>(s.size(), 256)) : at();
        s[i] = char(uint8_t(s[i]) ^ uint8_t(1u << (rng() % 8)));
        break;
    }
    }
    return s;
}

void readPgn(const std::string& text)
{
    const auto entries = chess::pgn::indexDatabase(text);
    for (const auto& e : entries) {
        if (e.offset > text.size()) continue;
        chess::Game g;
        std::string err;
        if (chess::pgn::parseGame(std::string_view(text).substr(e.offset, e.length), g, &err)) (void)chess::pgn::writeGame(g);
    }
    std::istringstream lines(text);
    std::string line;
    for (int n = 0; n < 50 && std::getline(lines, line); ++n) {
        chess::epd::Record r;
        (void)chess::epd::parseLine(line, r);
    }
}

void readDatabase(const std::string& path)
{
    hy64db::Database db;
    std::string err;
    if (!db.open(path, &err)) return;
    const uint32_t count = std::min<uint32_t>(db.count(), 2000); // a damaged count must not make the test last
    for (uint32_t id = 0; id < count; ++id) {
        (void)db.record(id);
        chess::Game g;
        (void)db.game(id, g, &err);
    }
    if (db.count() <= 2000) {
        (void)db.checkIntegrity();
        (void)db.search("a");
        (void)db.allGames(true);
    }
    db.close();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: fuzz_smoke <source folder> [rounds] [seed]\n");
        return 2;
    }
    const std::string root = argv[1];
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 200;
    const uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 20261125;

    const std::string pgn = slurp(root + "/tests/data/classics.pgn");
    const std::vector<std::string> bases = {
        slurp(root + "/databases/engine-tests/Tactics_Marathon_210.hy64"),
        slurp(root + "/databases/training/mates/mate_in_1.hy64"),
    };
    if (pgn.empty() || bases[0].empty() || bases[1].empty()) {
        std::fprintf(stderr, "fuzz_smoke: sample files not found under %s\n", root.c_str());
        return 2;
    }
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("hydra64-fuzz-" + std::to_string(seed));
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "damaged.hy64").string();

    for (int round = 0; round < rounds; ++round) {
        std::mt19937_64 rng(seed + uint64_t(round)); // one draw per round: a round is replayed alone
        std::printf("round %d (seed %llu)\n", round, static_cast<unsigned long long>(seed));
        std::fflush(stdout);
        readPgn(damaged(pgn, rng));
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            const std::string bytes = damaged(bases[size_t(round) % bases.size()], rng);
            out.write(bytes.data(), std::streamsize(bytes.size()));
        }
        readDatabase(file);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::printf("[ OK ] %d damaged files read without a crash\n", rounds);
    return 0;
}
