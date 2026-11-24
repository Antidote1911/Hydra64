#include "gamesession.h"

#include "chess/pgn.h"

#include <algorithm>

using namespace chess;

GameSession::GameSession(QObject* parent)
    : QObject(parent)
{
    // A move waiting to be replaced: any other change to the game, or another game, gives it up.
    connect(this, &GameSession::treeChanged, this, &GameSession::cancelReplace);
    connect(this, &GameSession::gameReplaced, this, &GameSession::cancelReplace);
    connect(this, &GameSession::slotSwitched, this, &GameSession::cancelReplace);
}

void GameSession::armReplace(Node* node, bool keepFollowing)
{
    if (!node || !node->parent) return;
    cancelReplace();
    moveTo(node->parent);
    m_replaceId = node->id;
    m_replaceKeep = keepFollowing;
    emit replaceModeChanged(true);
}

void GameSession::cancelReplace()
{
    if (m_replaceId < 0) return;
    m_replaceId = -1;
    emit replaceModeChanged(false);
}

bool GameSession::playNullMove()
{
    if (m_game.current()->pos.inCheck()) return false;
    playMove(Move());
    return true;
}

void GameSession::demoteVariation(Node* node)
{
    checkpoint();
    if (!m_game.demoteVariation(node)) return;
    emit treeChanged();
    emit evaluationsChanged();
}

// ---------------------------------------------------------------- undo / redo

GameSession::Snapshot GameSession::snapshot() const { return {pgn::writeGame(m_game), pathTo(m_game.current())}; }

void GameSession::checkpoint()
{
    if (m_slot != DatabaseSlot && m_slot != AnalysisSlot) return; // not during a game against the engine or a tournament
    auto& undo = m_undo[m_slot];
    Snapshot now = snapshot();
    if (!undo.empty() && undo.back().pgn == now.pgn) return; // nothing has changed since the last checkpoint
    undo.push_back(std::move(now));
    if (undo.size() > 100) undo.erase(undo.begin());
    m_redo[m_slot].clear();
    emit undoStateChanged();
}

void GameSession::restore(const Snapshot& s)
{
    Game game;
    pgn::parseGame(s.pgn, game);
    Node* n = game.root();
    for (int i : s.path)
        if (i >= 0 && size_t(i) < n->children.size()) n = n->children[size_t(i)].get();
    game.setCurrent(n);
    m_game = std::move(game); // the origin (database, row) is kept: it is the same game
    emit headersChanged();
    emit treeChanged();
    emit evaluationsChanged();
    emit currentChanged(false);
    emit undoStateChanged();
}

bool GameSession::undo()
{
    auto& undo = m_undo[m_slot];
    if (undo.empty()) return false;
    Snapshot now = snapshot();
    Snapshot back = std::move(undo.back());
    undo.pop_back();
    if (back.pgn == now.pgn && !undo.empty()) { // checkpoint set with no change afterwards
        back = std::move(undo.back());
        undo.pop_back();
    }
    m_redo[m_slot].push_back(std::move(now));
    restore(back);
    return true;
}

bool GameSession::redo()
{
    auto& redo = m_redo[m_slot];
    if (redo.empty()) return false;
    m_undo[m_slot].push_back(snapshot());
    const Snapshot next = std::move(redo.back());
    redo.pop_back();
    restore(next);
    return true;
}

void GameSession::forgetHistory(Slot s)
{
    m_undo[s].clear();
    m_redo[s].clear();
    emit undoStateChanged();
}

void GameSession::newGame(const Position& start)
{
    forgetHistory(m_slot);
    m_game = Game(start);
    m_origin = {};
    if (start.fen() != Position::startPos().fen()) {
        m_game.setHeader("SetUp", "1");
        m_game.setHeader("FEN", start.fen());
    }
    emit gameReplaced();
    emit headersChanged();
    emit treeChanged();
    emit currentChanged(false);
}

void GameSession::loadGame(Game&& game)
{
    forgetHistory(m_slot);
    m_game = std::move(game);
    chess::pgn::applyEvalProfile(m_game); // ChessBase evaluation profile → graph
    m_game.toStart();
    m_origin = {};
    emit gameReplaced();
    emit headersChanged();
    emit treeChanged();
    emit currentChanged(false);
}

void GameSession::moveTo(Node* node)
{
    if (!node || node == m_game.current()) return;
    cancelReplace(); // going elsewhere gives up the replacement
    const bool oneStep = node->parent == m_game.current();
    m_game.setCurrent(node);
    emit currentChanged(oneStep);
}

void GameSession::playMove(Move m)
{
    if (m_replaceId >= 0) { // the move played takes the place of the one waiting
        Node* old = m_game.findById(m_replaceId);
        const bool keep = m_replaceKeep;
        if (old && old->parent == m_game.current()) {
            if (old->move == m) { // the same move: nothing to replace
                cancelReplace();
                moveTo(old);
                return;
            }
            checkpoint();
            m_replaceId = -1;
            emit replaceModeChanged(false);
            Node* fresh = m_game.replaceMove(old, m, keep);
            if (!fresh) {
                emit replaceRefused();
                return;
            }
            emit treeChanged();
            emit evaluationsChanged();
            m_game.setCurrent(fresh);
            emit currentChanged(true);
            return;
        }
        cancelReplace();
    }
    Node* before = m_game.current();
    const size_t childCount = before->children.size();
    if (std::none_of(before->children.begin(), before->children.end(), [&](const auto& c) { return c->move == m; })) checkpoint(); // new move
    Node* n = m_game.addMove(before, m);
    if (before->children.size() != childCount) emit treeChanged();
    moveTo(n);
}

void GameSession::goTo(Node* node) { moveTo(node); }
void GameSession::forward() { moveTo(m_game.current()->mainChild()); }
void GameSession::back() { moveTo(m_game.current()->parent); }
void GameSession::toStart() { moveTo(m_game.root()); }

void GameSession::toEnd()
{
    Node* n = m_game.current();
    while (n->mainChild()) n = n->mainChild();
    moveTo(n);
}

void GameSession::deleteNode(Node* node)
{
    if (!node || !node->parent) return;
    checkpoint();
    Node* before = m_game.current();
    m_game.deleteNode(node);
    emit treeChanged();
    if (m_game.current() != before) emit currentChanged(false);
}

void GameSession::promoteVariation(Node* node)
{
    checkpoint();
    m_game.promoteVariation(node);
    emit treeChanged();
    emit evaluationsChanged();
}

void GameSession::setComment(Node* node, const QString& text)
{
    if (!node || node->comment == text.simplified().toStdString()) return;
    checkpoint();
    node->comment = text.simplified().toStdString();
    emit treeChanged();
}

void GameSession::setPreComment(Node* node, const QString& text)
{
    if (!node || node->preComment == text.simplified().toStdString()) return;
    checkpoint();
    node->preComment = text.simplified().toStdString();
    emit treeChanged();
}

void GameSession::setGraphics(Node* node, const std::vector<Graphic>& graphics)
{
    if (!node || node->graphics == graphics) return;
    checkpoint();
    node->graphics = graphics;
    emit graphicsChanged();
}

void GameSession::setEvaluation(Node* node, const Evaluation& eval)
{
    if (!node || eval.depth < node->eval.depth) return;
    node->eval = eval;
    emit evaluationsChanged();
}

void GameSession::editGame(const std::function<void(chess::Game&)>& edit)
{
    checkpoint();
    edit(m_game);
    emit headersChanged();
    emit treeChanged();
    emit evaluationsChanged();
    emit currentChanged(false);
}

void GameSession::setHeader(const std::string& key, const std::string& value)
{
    m_game.setHeader(key, value);
    emit headersChanged();
}

void GameSession::setHeaders(const Game::Headers& headers)
{
    if (m_game.headers() != headers) checkpoint();
    m_game.headers() = headers;
    emit headersChanged();
}

void GameSession::switchSlot(Slot slot)
{
    if (slot == m_slot) return;
    std::swap(m_game, m_stash[m_slot]);
    std::swap(m_origin, m_stashOrigin[m_slot]);
    std::swap(m_game, m_stash[slot]);
    std::swap(m_origin, m_stashOrigin[slot]);
    m_slot = slot;
    emit slotSwitched();
    emit undoStateChanged();
    emit headersChanged();
    emit treeChanged();
    emit currentChanged(false);
}

std::vector<int> GameSession::pathTo(const Node* node)
{
    std::vector<int> path;
    for (const Node* n = node; n && n->parent; n = n->parent)
        for (size_t i = 0; i < n->parent->children.size(); ++i)
            if (n->parent->children[i].get() == n) path.insert(path.begin(), int(i));
    return path;
}

void GameSession::restoreSlot(Slot s, Game&& game, const GameOrigin& origin, const std::vector<int>& path)
{
    Node* n = game.root();
    for (int i : path)
        if (i >= 0 && size_t(i) < n->children.size()) n = n->children[size_t(i)].get();
    game.setCurrent(n);
    forgetHistory(s);
    if (s != m_slot) {
        m_stash[s] = std::move(game);
        m_stashOrigin[s] = origin;
        return;
    }
    m_game = std::move(game);
    m_origin = origin;
    emit headersChanged();
    emit treeChanged();
    emit currentChanged(false);
}

void GameSession::replaceGameIn(Slot s, Game&& game)
{
    Node* n = game.root();
    while (n->mainChild()) n = n->mainChild();
    game.setCurrent(n);
    forgetHistory(s);
    if (s != m_slot) {
        m_stash[s] = std::move(game);
        m_stashOrigin[s] = {};
        return;
    }
    m_game = std::move(game);
    m_origin = {};
    emit gameReplaced();
    emit headersChanged();
    emit treeChanged();
    emit currentChanged(false);
}

void GameSession::appendMoveIn(Slot s, Move m, int clockMs)
{
    Game& g = s == m_slot ? m_game : m_stash[s];
    Node* last = g.root();
    while (last->mainChild()) last = last->mainChild();
    const bool follow = g.current() == last; // we were following the game: we move on with it
    Node* n = g.addMove(last, m);
    n->clockMs = clockMs;
    if (follow) g.setCurrent(n);
    if (s != m_slot) return;
    emit treeChanged();
    if (follow) emit currentChanged(true);
}

void GameSession::setHeaderIn(Slot s, const std::string& key, const std::string& value)
{
    if (s == m_slot) {
        setHeader(key, value);
        return;
    }
    m_stash[s].setHeader(key, value);
}
