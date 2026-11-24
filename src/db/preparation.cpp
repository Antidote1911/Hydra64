#include "preparation.h"

#include "chess/position.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace hy64db {

namespace {
void addOutcome(PlayerStats::Score& s, int outcome) // 1 win, 0 draw, -1 loss, 2 unknown
{
    if (outcome == 1) ++s.wins;
    else if (outcome == 0) ++s.draws;
    else if (outcome == -1) ++s.losses;
}

// What is known of a move played in a position, all move orders together.
struct Edge {
    uint32_t games = 0;
    PlayerStats::Score score;
    int lastYear = 0;
    std::vector<uint32_t> ids;
    std::string after; // key of the position reached
};
using Edges = std::map<std::string, Edge>;          // by move, in notation
using Graph = std::unordered_map<std::string, Edges>; // by position

// Board, side to move and castling rights: the first three fields of the FEN.
std::string keyOf(const chess::Position& pos)
{
    const std::string fen = pos.fen();
    size_t end = 0;
    for (int field = 0; field < 3 && end != std::string::npos; ++field) end = fen.find(' ', end + 1);
    return fen.substr(0, end);
}
} // namespace

Preparation prepareAgainst(const Database& db, uint32_t player, chess::Color side, const PrepOptions& options)
{
    Preparation prep;
    prep.side = side;
    const std::string start = chess::Position::startPos().fen();
    std::map<uint16_t, PlayerStats::Score> eco;
    std::vector<std::pair<int, uint32_t>> quick; // (half-moves, game)
    uint64_t lossPlies = 0, winPlies = 0;
    uint32_t lossCount = 0, winCount = 0;
    std::vector<uint8_t> blob;
    Graph graph;
    const uint32_t n = db.count();
    for (uint32_t id = 0; id < n; ++id) {
        const Record r = db.record(id);
        if (r.deleted() || (side == chess::White ? r.white : r.black) != player) continue;
        const int year = int(r.date >> 9);
        if (options.fromYear > 0 && year < options.fromYear) continue;
        int outcome = 2;
        if (r.result == Draw) outcome = 0;
        else if (r.result == WhiteWins) outcome = side == chess::White ? 1 : -1;
        else if (r.result == BlackWins) outcome = side == chess::White ? -1 : 1;

        // The moves first: a game that cannot be followed from the initial position is left out of everything.
        struct Step {
            std::string before, san;
        };
        std::vector<Step> steps;
        std::string last; // key of the position after the last move kept
        bool standard = true;
        if (!db.blob(id, blob)) standard = false;
        else {
            chess::Position end;
            const bool read = replayMainlineMoves(blob.data(), blob.size(), [&](const chess::Position& before, chess::Move m, int ply) {
                if (ply == 1 && before.fen() != start) {
                    standard = false;
                    return false;
                }
                if (ply > options.maxPlies) return false;
                steps.push_back({keyOf(before), before.toSan(m)});
                end = before;
                end.makeMove(m);
                return true;
            });
            if (!read && steps.empty()) standard = false;
            if (!steps.empty()) last = keyOf(end);
        }
        if (!standard) {
            ++prep.otherStarts;
            continue;
        }

        ++prep.root.games;
        addOutcome(prep.root.score, outcome);
        prep.root.lastYear = std::max(prep.root.lastYear, year);
        prep.root.ids.push_back(id);
        for (size_t k = 0; k < steps.size(); ++k) {
            Edge& e = graph[steps[k].before][steps[k].san];
            if (!e.ids.empty() && e.ids.back() == id) continue; // position repeated in the game: counted once
            ++e.games;
            addOutcome(e.score, outcome);
            e.lastYear = std::max(e.lastYear, year);
            e.ids.push_back(id);
            if (e.after.empty()) e.after = k + 1 < steps.size() ? steps[k + 1].before : last;
        }

        if (year) {
            if (!prep.firstYear || year < prep.firstYear) prep.firstYear = year;
            prep.lastYear = std::max(prep.lastYear, year);
        }
        if (r.eco && outcome != 2) addOutcome(eco[r.eco], outcome);
        const int moves = (r.plies + 1) / 2;
        if (outcome == -1) prep.losses.push_back(id);
        if (outcome == -1 && r.plies > 0) {
            ++(moves < 25 ? prep.lossesShort : moves <= 40 ? prep.lossesMiddle : prep.lossesLong);
            lossPlies += r.plies;
            ++lossCount;
            if (moves <= 25) quick.push_back({int(r.plies), id});
        } else if (outcome == 1 && r.plies > 0) {
            winPlies += r.plies;
            ++winCount;
        }
    }
    // The tree, level by level: on each level the moves met in the most games develop their position
    // first; a position already developed (at this level or above) makes a transposition.
    const uint32_t minGames = std::max<uint32_t>(1, options.minGames);
    prep.root.key = keyOf(chess::Position::startPos());
    std::unordered_set<std::string> developed{prep.root.key};
    std::vector<PrepNode*> level{&prep.root};
    while (!level.empty()) {
        std::vector<PrepNode*> made;
        for (PrepNode* node : level) {
            const auto at = graph.find(node->key);
            if (at == graph.end() || node->ply >= options.maxPlies) continue;
            for (const auto& [san, e] : at->second) {
                if (e.games < minGames) {
                    node->dropped += e.games;
                    continue;
                }
                PrepNode child;
                child.san = san;
                child.ply = node->ply + 1;
                child.byPlayer = (child.ply % 2 == 1) == (side == chess::White);
                child.games = e.games;
                child.score = e.score;
                child.lastYear = e.lastYear;
                child.ids = e.ids;
                child.key = e.after;
                node->children.push_back(std::move(child));
            }
            std::stable_sort(node->children.begin(), node->children.end(), [](const PrepNode& a, const PrepNode& b) { return a.games > b.games; });
            for (PrepNode& c : node->children) made.push_back(&c); // (the children of a node no longer move)
        }
        std::stable_sort(made.begin(), made.end(), [](const PrepNode* a, const PrepNode* b) { return a->games > b->games; });
        level.clear();
        for (PrepNode* c : made) {
            if (developed.insert(c->key).second) level.push_back(c);
            else c->transposition = true;
        }
    }
    for (const auto& [code, score] : eco) prep.openings.push_back({code, score});
    std::stable_sort(prep.openings.begin(), prep.openings.end(), [](const auto& a, const auto& b) { return a.score.games() > b.score.games(); });
    if (prep.openings.size() > 12) prep.openings.resize(12);
    if (lossCount) prep.averageLossMoves = int((lossPlies / lossCount + 1) / 2);
    if (winCount) prep.averageWinMoves = int((winPlies / winCount + 1) / 2);
    std::stable_sort(quick.begin(), quick.end());
    if (quick.size() > 10) quick.resize(10);
    for (const auto& q : quick) prep.quickLosses.push_back(q.second);
    return prep;
}

std::vector<std::vector<const PrepNode*>> rankedLines(const Preparation& prep, bool weakest, uint32_t minGames, size_t max, double margin)
{
    using Path = std::vector<const PrepNode*>;
    std::vector<Path> candidates;
    const double overall = prep.root.score.percent();
    Path path;
    auto walk = [&](auto&& self, const PrepNode& n) -> void {
        for (const PrepNode& c : n.children) {
            path.push_back(&c);
            const double p = c.score.percent();
            if (!c.byPlayer && c.score.games() >= minGames && (weakest ? p <= overall - margin : p >= overall + margin)) candidates.push_back(path);
            self(self, c);
            path.pop_back();
        }
    };
    walk(walk, prep.root);
    // The most telling first: score, then number of games, then the shorter line.
    std::stable_sort(candidates.begin(), candidates.end(), [&](const Path& a, const Path& b) {
        const double pa = a.back()->score.percent(), pb = b.back()->score.percent();
        if (pa != pb) return weakest ? pa < pb : pa > pb;
        if (a.back()->score.games() != b.back()->score.games()) return a.back()->score.games() > b.back()->score.games();
        return a.size() < b.size();
    });
    std::vector<Path> out;
    for (const Path& c : candidates) {
        if (out.size() >= max) break;
        const bool related = std::any_of(out.begin(), out.end(), [&](const Path& o) {
            const size_t common = std::min(o.size(), c.size());
            return std::equal(o.begin(), o.begin() + long(common), c.begin());
        });
        if (!related) out.push_back(c);
    }
    return out;
}

PrepCover PrepCoverage::of(const PrepNode* n) const
{
    for (const auto& [node, state] : states)
        if (node == n) return state;
    return PrepCover::Outside;
}

PrepCoverage coverage(const Preparation& prep, const chess::Game& repertoire)
{
    PrepCoverage out;
    if (repertoire.root()->pos.fen() != chess::Position::startPos().fen()) return out; // another starting position: nothing to compare
    std::vector<const PrepNode*> path;
    // `rep`: node of the repertoire reached by the moves of the path, null once outside it.
    auto walk = [&](auto&& self, const PrepNode& node, const chess::Node* rep) -> void {
        for (const PrepNode& c : node.children) {
            path.push_back(&c);
            PrepCover state = PrepCover::Outside;
            const chess::Node* next = nullptr;
            if (rep) {
                for (const auto& r : rep->children)
                    if (r->san == c.san) next = r.get();
                if (next) state = PrepCover::Inside;
                else if (rep->children.empty()) state = PrepCover::Ends;
                else if (c.byPlayer) state = PrepCover::Uncovered;
                if (state == PrepCover::Ends || state == PrepCover::Uncovered) {
                    PrepGap gap;
                    gap.path = path;
                    gap.kind = state;
                    for (const auto& r : rep->children) gap.known.push_back(r->san);
                    out.gaps.push_back(std::move(gap));
                }
            }
            out.states.push_back({&c, state});
            self(self, c, next);
            path.pop_back();
        }
    };
    walk(walk, prep.root, repertoire.root());
    std::stable_sort(out.gaps.begin(), out.gaps.end(), [](const PrepGap& a, const PrepGap& b) { return a.path.back()->games > b.path.back()->games; });
    return out;
}

} // namespace hy64db
