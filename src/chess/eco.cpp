#include "eco.h"

#include "polyglot.h"

#include <sstream>

namespace chess::eco {

int Book::load(std::string_view tsv)
{
    int count = 0;
    size_t pos = 0;
    while (pos < tsv.size()) {
        size_t end = tsv.find('\n', pos);
        if (end == std::string_view::npos) end = tsv.size();
        std::string_view line = tsv.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const size_t t1 = line.find('\t');
        const size_t t2 = t1 == std::string_view::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string_view::npos) continue;
        const std::string code(line.substr(0, t1));
        if (code.size() != 3 || code[0] < 'A' || code[0] > 'E') continue; // header
        Opening o;
        o.eco = code;
        o.name = std::string(line.substr(t1 + 1, t2 - t1 - 1));
        // SAN moves (move numbers ignored)
        Position p = Position::startPos();
        std::istringstream in{std::string(line.substr(t2 + 1))};
        bool ok = true;
        for (std::string tok; in >> tok;) {
            if (tok.back() == '.' || (tok.find('.') != std::string::npos && std::isdigit(static_cast<unsigned char>(tok[0])))) {
                const size_t dot = tok.find_last_of('.');
                tok = tok.substr(dot + 1);
                if (tok.empty()) continue;
            }
            const Move m = p.parseSan(tok);
            if (m.isNull()) {
                ok = false;
                break;
            }
            p.makeMove(m);
            ++o.plies;
        }
        if (!ok) continue;
        const uint64_t key = polyglot::key(p);
        auto it = m_byKey.find(key);
        // Same position through several lines: the shortest (most general name) wins
        if (it == m_byKey.end() || o.plies < it->second.plies) m_byKey[key] = o;
        auto c = m_byCode.find(code);
        if (c == m_byCode.end() || o.plies < c->second.plies) m_byCode[code] = o;
        m_lines.push_back({code, o.name, std::string(line.substr(t2 + 1))});
        ++count;
    }
    return count;
}

const Opening* Book::lookup(const Position& pos) const
{
    const auto it = m_byKey.find(polyglot::key(pos));
    return it == m_byKey.end() ? nullptr : &it->second;
}

const Opening* Book::classify(const Game& game, int* ply) const
{
    const Opening* best = nullptr;
    int at = 0, k = 0;
    for (const Node* n : game.mainline()) {
        ++k;
        if (k > 60) break; // beyond this point, no listed opening
        if (const Opening* o = lookup(n->pos)) {
            best = o;
            at = k;
        }
    }
    if (ply) *ply = at;
    return best;
}

std::string Book::nameOf(const std::string& eco) const
{
    const auto it = m_byCode.find(eco);
    return it == m_byCode.end() ? std::string() : it->second.name;
}

} // namespace chess::eco
