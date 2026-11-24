#include "chess/position.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace chess;
// Build: g++ -std=c++20 -O2 -Isrc tools/lichess-puzzles/convert.cpp build/libhydra64_core.a -o convert
// Usage: zstdcat lichess_db_puzzle.csv.zst | python3 select.py ; ./convert sel_mate2.txt 2 > mats-en-2.epd ;
// then to_pgn.py (PGN games) and hydra64-convert (databases of databases/training)
// input: id \t rating \t fen \t UCI moves; EPD output (bm, id, dm, pv)
int main(int argc, char** argv)
{
    std::ifstream in(argv[1]);
    const int dm = argc > 2 ? std::stoi(argv[2]) : 0;
    std::string line;
    int n = 0, bad = 0;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string id, rating, fen, moves;
        std::getline(ss, id, '\t'); std::getline(ss, rating, '\t'); std::getline(ss, fen, '\t'); std::getline(ss, moves, '\t');
        Position p;
        if (!p.setFen(fen)) { ++bad; continue; }
        std::istringstream ms(moves);
        std::string u;
        std::vector<std::string> uci;
        while (ms >> u) uci.push_back(u);
        if (uci.size() < 2) { ++bad; continue; }
        Move m0 = p.parseUci(uci[0]);
        if (m0.isNull() || !p.isLegal(m0)) { ++bad; continue; }
        p.makeMove(m0);
        std::string f = p.fen();
        std::istringstream fs(f); std::string a, b, c, d; fs >> a >> b >> c >> d;
        Position q = p;
        std::string pv, bm;
        bool ok = true;
        for (size_t i = 1; i < uci.size(); ++i) {
            Move m = q.parseUci(uci[i]);
            if (m.isNull() || !q.isLegal(m)) { ok = false; break; }
            std::string san = q.toSan(m);
            if (i == 1) bm = san;
            pv += (i > 1 ? " " : "") + san;
            q.makeMove(m);
        }
        if (!ok || (dm && !q.isCheckmate())) { ++bad; continue; }
        std::cout << a << ' ' << b << ' ' << c << ' ' << d << " bm " << bm << "; id \"Lichess " << id << " (" << rating << ")\";";
        if (dm) std::cout << " dm " << dm << ';';
        std::cout << " pv \"" << pv << "\";\n";
        ++n;
    }
    std::cerr << argv[1] << ": " << n << " ok, " << bad << " rejetées\n";
}
