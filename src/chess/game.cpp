#include "game.h"

#include <algorithm>
#include <functional>

namespace chess {

bool Node::isMainline() const
{
    for (const Node* n = this; n->parent; n = n->parent)
        if (n->parent->children.front().get() != n) return false;
    return true;
}

int Node::ply() const
{
    int p = 0;
    for (const Node* n = this; n->parent; n = n->parent) ++p;
    return p;
}

namespace {

// Repetition key: placement, side to move, castling rights, and the en passant square only if
// an en passant capture is actually possible.
std::string repetitionKey(const Position& p)
{
    const std::string fen = p.fen();
    size_t cut = fen.find(' ');
    cut = fen.find(' ', cut + 1);
    cut = fen.find(' ', cut + 1); // after the castling rights
    std::string key = fen.substr(0, cut);
    if (p.epSquare() != NoSquare) {
        for (const Move& m : p.legalMoves())
            if (m.to == p.epSquare() && typeOf(p.at(m.from)) == Pawn) {
                key += ' ' + squareName(p.epSquare());
                break;
            }
    }
    return key;
}

} // namespace

bool detectGameEnd(const Node* n, GameEnd& out)
{
    const Position& p = n->pos;
    if (p.legalMoves().empty()) {
        if (p.inCheck()) out = {p.sideToMove() == White ? "0-1" : "1-0", "Échec et mat", GameEnd::Checkmate};
        else out = {"1/2-1/2", "Pat", GameEnd::Stalemate};
        return true;
    }
    if (p.isInsufficientMaterial()) {
        out = {"1/2-1/2", "Matériel insuffisant", GameEnd::InsufficientMaterial};
        return true;
    }
    if (p.halfmoveClock() >= 100) {
        out = {"1/2-1/2", "Règle des 50 coups", GameEnd::FiftyMoves};
        return true;
    }
    const std::string key = repetitionKey(p);
    int count = 0;
    // A repetition is only possible since the last irreversible move.
    for (const Node* a = n; a; a = a->parent) {
        if (repetitionKey(a->pos) == key && ++count >= 3) {
            out = {"1/2-1/2", "Triple répétition", GameEnd::Repetition};
            return true;
        }
        if (a->pos.halfmoveClock() == 0) break;
    }
    return false;
}

Game::Game() { reset(); }
Game::Game(const Position& start) { reset(start); }

void Game::reset(const Position& start)
{
    m_root = std::make_unique<Node>();
    m_root->id = 0;
    m_root->pos = start;
    m_current = m_root.get();
    m_nextId = 1;
}

std::string Game::header(const std::string& key) const
{
    for (const auto& [k, v] : m_headers)
        if (k == key) return v;
    return {};
}

void Game::setHeader(const std::string& key, const std::string& value)
{
    for (auto& [k, v] : m_headers)
        if (k == key) {
            v = value;
            return;
        }
    m_headers.emplace_back(key, value);
}

Node* Game::addMove(Node* from, Move m, bool newBranch, bool withSan)
{
    if (!newBranch)
        for (auto& c : from->children)
            if (c->move == m) return c.get();

    auto n = std::make_unique<Node>();
    n->id = m_nextId++;
    n->move = m;
    if (withSan) n->san = from->pos.toSan(m);
    n->pos = from->pos.after(m);
    n->parent = from;
    from->children.push_back(std::move(n));
    return from->children.back().get();
}

int Game::merge(const Game& other, const std::string& label)
{
    if (other.startPosition().fen() != startPosition().fen()) return -1;
    int added = 0;
    // Walks both trees in parallel; `main`: we are still following the main line of `other`.
    struct Step {
        const Node* from;
        Node* to;
        bool main;
    };
    std::vector<Step> todo{{other.root(), m_root.get(), true}};
    while (!todo.empty()) {
        const Step step = todo.back();
        todo.pop_back();
        for (size_t i = 0; i < step.from->children.size(); ++i) {
            const Node* child = step.from->children[i].get();
            const size_t before = step.to->children.size();
            Node* mine = addMove(step.to, child->move); // joins an identical move if there is one
            const bool fresh = step.to->children.size() != before;
            if (fresh) {
                ++added;
                mine->comment = child->comment;
                mine->preComment = child->preComment;
                mine->nags = child->nags;
                mine->graphics = child->graphics;
                mine->medals = child->medals;
                mine->question = child->question;
                mine->questionText = child->questionText;
                mine->questionData = child->questionData;
                mine->clockMs = -1; // another game's clocks make no sense here
            } else if (!child->comment.empty() && mine->comment.find(child->comment) == std::string::npos) {
                mine->comment += (mine->comment.empty() ? "" : " ") + child->comment;
            }
            const bool main = step.main && i == 0;
            if (main && child->children.empty() && !label.empty() && mine->comment.find(label) == std::string::npos)
                mine->comment += (mine->comment.empty() ? "" : " ") + label; // end of the other game's main line
            todo.push_back({child, mine, main});
        }
    }
    return added;
}

bool Game::forward()
{
    if (Node* c = m_current->mainChild()) {
        m_current = c;
        return true;
    }
    return false;
}

bool Game::back()
{
    if (m_current->parent) {
        m_current = m_current->parent;
        return true;
    }
    return false;
}

void Game::toEnd()
{
    while (forward()) {}
}

std::vector<Node*> Game::mainline() const
{
    std::vector<Node*> out;
    for (Node* n = m_root->mainChild(); n; n = n->mainChild()) out.push_back(n);
    return out;
}

Node* Game::findById(int id) const
{
    std::function<Node*(Node*)> rec = [&](Node* n) -> Node* {
        if (n->id == id) return n;
        for (auto& c : n->children)
            if (Node* r = rec(c.get())) return r;
        return nullptr;
    };
    return rec(m_root.get());
}

void Game::deleteNode(Node* n)
{
    if (!n || !n->parent) return;
    // If the current node is in the deleted subtree, go up to the parent.
    for (Node* c = m_current; c; c = c->parent)
        if (c == n) {
            m_current = n->parent;
            break;
        }
    auto& siblings = n->parent->children;
    siblings.erase(std::remove_if(siblings.begin(), siblings.end(),
                                  [n](const std::unique_ptr<Node>& p) { return p.get() == n; }),
                   siblings.end());
}

void Game::promoteVariation(Node* n)
{
    // Goes up to the first node that is not the main continuation of its parent.
    while (n && n->parent && n->parent->children.front().get() == n) n = n->parent;
    if (!n || !n->parent) return;
    auto& siblings = n->parent->children;
    auto it = std::find_if(siblings.begin(), siblings.end(),
                           [n](const std::unique_ptr<Node>& p) { return p.get() == n; });
    if (it != siblings.end() && it != siblings.begin()) std::iter_swap(it, it - 1);
}

bool Game::demoteVariation(Node* n)
{
    // The head of the variation (or of the main continuation) the node belongs to
    while (n && n->parent && n->parent->children.front().get() == n && n->parent->children.size() == 1) n = n->parent;
    if (!n || !n->parent) return false;
    auto& siblings = n->parent->children;
    auto it = std::find_if(siblings.begin(), siblings.end(), [n](const std::unique_ptr<Node>& p) { return p.get() == n; });
    if (it == siblings.end() || it + 1 == siblings.end()) return false;
    std::iter_swap(it, it + 1);
    return true;
}

Node* Game::replaceMove(Node* n, Move m, bool keepFollowing)
{
    if (!n || !n->parent) return nullptr;
    Node* parent = n->parent;
    if (!m.isNull() && !parent->pos.isLegal(m)) return nullptr;
    if (m.isNull() && parent->pos.inCheck()) return nullptr;
    for (const auto& c : parent->children)
        if (c.get() != n && c->move == m) return nullptr; // already a line of its own
    const bool currentInside = [&] {
        for (const Node* c = m_current; c; c = c->parent)
            if (c == n) return true;
        return false;
    }();

    auto fresh = std::make_unique<Node>();
    fresh->id = m_nextId++;
    fresh->move = m;
    fresh->san = parent->pos.toSan(m);
    fresh->pos = parent->pos.after(m);
    fresh->parent = parent;
    if (keepFollowing) {
        // What followed, replayed from the new position; a line stops at its first move that no longer goes.
        auto graft = [&](auto&& self, const Node* from, Node* to) -> void {
            for (const auto& c : from->children) {
                const bool legal = c->move.isNull() ? !to->pos.inCheck() : to->pos.isLegal(c->move);
                if (!legal) continue;
                auto copy = std::make_unique<Node>();
                copy->id = m_nextId++;
                copy->move = c->move;
                copy->san = to->pos.toSan(c->move);
                copy->pos = to->pos.after(c->move);
                copy->parent = to;
                copy->comment = c->comment;
                copy->preComment = c->preComment;
                copy->nags = c->nags;
                copy->clockMs = c->clockMs;
                copy->graphics = c->graphics;
                copy->medals = c->medals;
                copy->question = c->question;
                copy->questionText = c->questionText;
                copy->questionData = c->questionData;
                Node* added = copy.get();
                to->children.push_back(std::move(copy));
                self(self, c.get(), added); // (the evaluation is not taken over: the position has changed)
            }
        };
        graft(graft, n, fresh.get());
    }
    Node* result = fresh.get();
    for (auto& slot : parent->children)
        if (slot.get() == n) {
            slot = std::move(fresh); // the old move and what followed it are destroyed here
            break;
        }
    if (currentInside) m_current = result;
    return result;
}

} // namespace chess
