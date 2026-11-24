#include "deeptree.h"

#include "chess/polyglot.h"

#include <algorithm>
#include <deque>
#include <functional>

using namespace chess;

namespace deep {

int toCp(const Eval& e)
{
    if (!e.valid) return Unknown;
    if (!e.mate) return std::clamp(e.score, -MateValue / 2, MateValue / 2);
    if (e.score == 0) return -MateValue; // the side to move is mated
    // mate in N moves = 2N−1 half-moves for the mating side
    return e.score > 0 ? MateValue - (2 * e.score - 1) : -MateValue + 2 * (-e.score);
}

uint16_t packMove(Move m)
{
    if (m.isNull()) return 0xFFFF;
    return uint16_t(uint16_t(m.from) | uint16_t(m.to) << 6 | uint16_t(m.promo) << 12);
}

Move unpackMove(uint16_t v)
{
    if (v == 0xFFFF) return {};
    Move m;
    m.from = Square(v & 63);
    m.to = Square((v >> 6) & 63);
    m.promo = PieceType((v >> 12) & 7);
    return m;
}

uint32_t Tree::find(uint64_t key) const
{
    const auto it = m_index.find(key);
    return it == m_index.end() ? NoNode : it->second;
}

uint32_t Tree::find(const Position& pos) const { return find(polyglot::key(pos)); }

uint32_t Tree::ensure(const Position& pos)
{
    const uint64_t key = polyglot::key(pos);
    if (const uint32_t i = find(key); i != NoNode) return i;
    const uint32_t i = uint32_t(m_nodes.size());
    m_nodes.push_back({});
    m_nodes.back().key = key;
    m_index.emplace(key, i);
    return i;
}

Edge* Tree::edge(uint32_t parent, Move move)
{
    for (Edge& e : m_nodes[parent].edges)
        if (e.move == move) return &e;
    return nullptr;
}

const Edge* Tree::edge(uint32_t parent, Move move) const
{
    for (const Edge& e : m_nodes[parent].edges)
        if (e.move == move) return &e;
    return nullptr;
}

uint32_t Tree::addEdge(uint32_t parent, const Position& parentPos, Move move)
{
    if (const Edge* e = edge(parent, move)) return e->child;
    const uint32_t child = ensure(parentPos.after(move));
    m_nodes[parent].edges.push_back({move, child, Mark::None});
    auto& ps = m_nodes[child].parents;
    if (std::find(ps.begin(), ps.end(), parent) == ps.end()) ps.push_back(parent);
    return child;
}

uint32_t Tree::ensurePath(const Position& start, const std::vector<Move>& path, Position* end)
{
    Position pos = start;
    uint32_t n = ensure(pos);
    for (const Move& m : path) {
        if (!pos.isLegal(m)) break;
        n = addEdge(n, pos, m);
        pos.makeMove(m);
    }
    if (end) *end = pos;
    return n;
}

int Tree::addRoot(const Position& pos, const std::string& comment)
{
    const uint32_t n = ensure(pos);
    for (size_t i = 0; i < m_roots.size(); ++i)
        if (m_roots[i].node == n) return int(i);
    m_roots.push_back({pos.fen(), n, true, comment});
    return int(m_roots.size()) - 1;
}

int Tree::moveScore(const Edge& e) const
{
    if (e.child == NoNode) return Unknown;
    const int s = m_nodes[e.child].score;
    if (s == Unknown) return Unknown;
    int v = -s;
    if (v > MateValue / 2) --v; // one more half-move before the mate
    else if (v < -MateValue / 2) ++v;
    return v;
}

const Edge* Tree::bestEdge(uint32_t node) const
{
    const Edge* best = nullptr;
    int bestScore = Unknown;
    for (const Edge& e : m_nodes[node].edges) {
        if (e.mark == Mark::Exclude) continue;
        const int s = moveScore(e);
        if (s == Unknown) continue;
        if (!best || s > bestScore) {
            best = &e;
            bestScore = s;
        }
    }
    return best;
}

int Tree::localScore(uint32_t i) const
{
    int best = Unknown;
    for (const Edge& e : m_nodes[i].edges) {
        if (e.mark == Mark::Exclude) continue;
        const int s = moveScore(e);
        if (s != Unknown && (best == Unknown || s > best)) best = s;
    }
    return best != Unknown ? best : toCp(m_nodes[i].eval);
}

void Tree::setEval(uint32_t node, const Eval& e)
{
    Node& n = m_nodes[node];
    // A shallower evaluation does not replace the old one (unless that one had no PV).
    if (!n.eval.valid || e.depth >= n.eval.depth || (n.eval.pv.empty() && !e.pv.empty())) n.eval = e;
    propagate(node);
}

void Tree::propagate(uint32_t node)
{
    std::deque<uint32_t> queue{node};
    size_t steps = 0;
    while (!queue.empty() && ++steps < 2000000) {
        const uint32_t i = queue.front();
        queue.pop_front();
        const int s = localScore(i);
        if (s == m_nodes[i].score && i != node) continue;
        const bool changed = s != m_nodes[i].score;
        m_nodes[i].score = s;
        if (changed || i == node)
            for (uint32_t p : m_nodes[i].parents) queue.push_back(p);
    }
}

void Tree::minimaxAll()
{
    // 0: not seen, 1: on the stack (repetition), 2: done
    std::vector<uint8_t> state(m_nodes.size(), 0);
    std::function<void(uint32_t)> visit = [&](uint32_t i) {
        state[i] = 1;
        Node& n = m_nodes[i];
        int best = Unknown;
        uint32_t positions = 1;
        for (const Edge& e : n.edges) {
            if (e.child == NoNode) continue;
            if (state[e.child] == 0) visit(e.child);
            if (state[e.child] == 2) positions += m_nodes[e.child].positions;
            if (e.mark == Mark::Exclude) continue;
            int s = state[e.child] == 1 ? 0 : moveScore(e); // position already on the line: draw
            if (s != Unknown && (best == Unknown || s > best)) best = s;
        }
        n.score = best != Unknown ? best : toCp(n.eval);
        n.positions = positions;
        state[i] = 2;
    };
    for (uint32_t i = 0; i < m_nodes.size(); ++i)
        if (state[i] == 0 && m_nodes[i].parents.empty()) visit(i);
    for (uint32_t i = 0; i < m_nodes.size(); ++i)
        if (state[i] == 0) visit(i); // components with no starting node (cycles)
}

std::vector<Move> Tree::bestLine(uint32_t node, int maxPlies) const
{
    std::vector<Move> line;
    std::vector<uint32_t> seen{node};
    while (node != NoNode && int(line.size()) < maxPlies) {
        const Edge* e = bestEdge(node);
        if (!e || std::find(seen.begin(), seen.end(), e->child) != seen.end()) break;
        line.push_back(e->move);
        node = e->child;
        seen.push_back(node);
    }
    return line;
}

void Tree::merge(const Tree& other)
{
    // Matching of the nodes by key, then evaluations and edges.
    std::vector<uint32_t> map(other.m_nodes.size(), NoNode);
    for (uint32_t i = 0; i < other.m_nodes.size(); ++i) {
        const Node& o = other.m_nodes[i];
        uint32_t j = find(o.key);
        if (j == NoNode) {
            j = uint32_t(m_nodes.size());
            m_nodes.push_back({});
            m_nodes.back().key = o.key;
            m_index.emplace(o.key, j);
        }
        map[i] = j;
        Node& n = m_nodes[j];
        if (o.eval.valid && (!n.eval.valid || o.eval.depth > n.eval.depth)) n.eval = o.eval;
        n.alternatives = std::max(n.alternatives, o.alternatives);
        n.allMovesFound = n.allMovesFound || o.allMovesFound;
        if (n.comment.empty()) n.comment = o.comment;
        if (n.note == NoteList::None) {
            n.note = o.note;
            n.noteDate = o.noteDate;
            n.noteInitialScore = o.noteInitialScore;
        }
    }
    for (uint32_t i = 0; i < other.m_nodes.size(); ++i) {
        const uint32_t j = map[i];
        for (const Edge& oe : other.m_nodes[i].edges) {
            if (oe.child == NoNode) continue;
            const uint32_t child = map[oe.child];
            Edge* e = edge(j, oe.move);
            if (!e) {
                m_nodes[j].edges.push_back({oe.move, child, oe.mark});
                auto& ps = m_nodes[child].parents;
                if (std::find(ps.begin(), ps.end(), j) == ps.end()) ps.push_back(j);
            } else if (e->mark == Mark::None) {
                e->mark = oe.mark;
            }
        }
    }
    minimaxAll();
}

void Tree::restore(std::vector<Node> nodes, std::vector<Root> roots)
{
    m_nodes = std::move(nodes);
    m_roots = std::move(roots);
    m_index.clear();
    for (uint32_t i = 0; i < m_nodes.size(); ++i) {
        m_index.emplace(m_nodes[i].key, i);
        m_nodes[i].parents.clear();
    }
    for (uint32_t i = 0; i < m_nodes.size(); ++i)
        for (const Edge& e : m_nodes[i].edges) {
            auto& ps = m_nodes[e.child].parents;
            if (std::find(ps.begin(), ps.end(), i) == ps.end()) ps.push_back(i);
        }
    for (Root& r : m_roots)
        if (r.node >= m_nodes.size()) r.node = NoNode;
    std::erase_if(m_roots, [](const Root& r) { return r.node == NoNode; });
    minimaxAll();
}

void Tree::clear()
{
    m_nodes.clear();
    m_index.clear();
    m_roots.clear();
}

} // namespace deep
