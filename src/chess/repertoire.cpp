#include "repertoire.h"

#include <algorithm>

namespace chess::repertoire {

int addLine(Game& repertoire, const Game& game, const Node* upTo)
{
    if (!upTo) return repertoire.merge(game);
    // Only the line leading to `upTo`: a copy of the game reduced to that line
    std::vector<const Node*> path;
    for (const Node* n = upTo; n && n->parent; n = n->parent) path.push_back(n);
    std::reverse(path.begin(), path.end());
    Game line(game.startPosition());
    Node* at = line.root();
    for (const Node* n : path) {
        at = line.addMove(at, n->move);
        at->comment = n->comment;
        at->preComment = n->preComment;
        at->nags = n->nags;
        at->graphics = n->graphics;
    }
    return repertoire.merge(line);
}

namespace {
// "1. e4 c5 2. Nf3": moves from the root to `node`.
std::string lineTo(const Node* node)
{
    std::vector<const Node*> path;
    for (const Node* n = node; n && n->parent; n = n->parent) path.push_back(n);
    std::string out;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const Position& before = (*it)->parent->pos;
        if (!out.empty()) out += ' ';
        if (before.sideToMove() == White) out += std::to_string(before.fullmoveNumber()) + ". ";
        else if (it == path.rbegin()) out += std::to_string(before.fullmoveNumber()) + "... ";
        out += (*it)->san;
    }
    return out;
}
} // namespace

Report::Report(const Game& repertoire, Color me)
    : m_rep(repertoire)
    , m_me(me)
{
}

bool Report::add(const Game& game, int id)
{
    if (game.startPosition().fen() != m_rep.startPosition().fen()) return false;
    ++m_seen;
    const Node* rep = m_rep.root();
    for (const Node* n : game.mainline()) {
        const Position& before = n->parent->pos;
        const Node* next = nullptr;
        for (const auto& c : rep->children)
            if (c->move == n->move) next = c.get();
        if (next) {
            rep = next;
            continue;
        }
        // The game leaves the repertoire here.
        const bool mine = before.sideToMove() == m_me;
        if (rep == m_rep.root() && rep->children.empty()) return false; // empty repertoire
        Gap::Kind kind;
        if (rep->children.empty()) kind = Gap::LineEnds;
        else if (mine) kind = Gap::Deviation;
        else kind = Gap::Uncovered;
        const std::string move = std::to_string(before.fullmoveNumber()) + (before.sideToMove() == White ? ". " : "... ") + before.toSan(n->move);
        Gap& gap = m_gaps[{rep->id, move}];
        if (gap.games == 0) {
            gap.kind = kind;
            gap.line = lineTo(rep);
            gap.move = move;
            for (const auto& c : rep->children) gap.known += (gap.known.empty() ? "" : ", ") + c->san;
        }
        ++gap.games;
        const std::string result = game.header("Result");
        if (result == "1-0") ++gap.whiteWins;
        else if (result == "0-1") ++gap.blackWins;
        else if (result == "1/2-1/2") ++gap.draws;
        if (id >= 0 && gap.ids.size() < 50) gap.ids.push_back(id);
        return true;
    }
    ++m_inside; // the whole game is in the repertoire
    return true;
}

std::vector<Gap> Report::gaps() const
{
    std::vector<Gap> out;
    for (const auto& [key, gap] : m_gaps) out.push_back(gap);
    std::stable_sort(out.begin(), out.end(), [](const Gap& a, const Gap& b) { return a.games > b.games; });
    return out;
}

} // namespace chess::repertoire
