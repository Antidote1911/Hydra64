#include "deepplanner.h"

#include "chess/polyglot.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

using namespace chess;

namespace deep {

namespace {

constexpr char Magic[8] = {'B', '6', '4', 'D', 'E', 'E', 'P', '1'};
constexpr uint32_t Version = 1;
constexpr double ManualPriority = -1e12;    // + number: in order of arrival
constexpr double FalseAlarmPriority = -1e9; // right after the manual tasks

// Task created by a step (neither manual nor false-alarm check)
bool isStageTask(const Task& t) { return !t.manual && t.priority >= 0; }

// Moves of the PV legal from `pos` (the PV stops at the first illegal move).
std::vector<Move> legalPrefix(Position pos, const std::vector<Move>& pv)
{
    std::vector<Move> out;
    for (const Move& m : pv) {
        if (!pos.isLegal(m)) break;
        out.push_back(m);
        pos.makeMove(m);
    }
    return out;
}

// ---- binary writing / reading
struct Writer {
    std::string buf;
    void u8(uint8_t v) { buf.push_back(char(v)); }
    void u16(uint16_t v) { raw(&v, 2); }
    void u32(uint32_t v) { raw(&v, 4); }
    void i32(int32_t v) { raw(&v, 4); }
    void u64(uint64_t v) { raw(&v, 8); }
    void f64(double v) { raw(&v, 8); }
    void str(const std::string& s)
    {
        u32(uint32_t(s.size()));
        buf += s;
    }
    void moves(const std::vector<Move>& ms)
    {
        u16(uint16_t(std::min<size_t>(ms.size(), 65535)));
        for (size_t i = 0; i < ms.size() && i < 65535; ++i) u16(packMove(ms[i]));
    }
    void raw(const void* p, size_t n) // little-endian (target machines: x86-64, ARM)
    {
        buf.append(static_cast<const char*>(p), n);
    }
};

struct Reader {
    const std::string& buf;
    size_t pos = 0;
    bool ok = true;
    template <class T> T get()
    {
        T v{};
        if (pos + sizeof(T) > buf.size()) {
            ok = false;
            return v;
        }
        std::memcpy(&v, buf.data() + pos, sizeof(T));
        pos += sizeof(T);
        return v;
    }
    std::string str()
    {
        const uint32_t n = get<uint32_t>();
        if (!ok || pos + n > buf.size()) {
            ok = false;
            return {};
        }
        std::string s = buf.substr(pos, n);
        pos += n;
        return s;
    }
    std::vector<Move> moves()
    {
        const uint16_t n = get<uint16_t>();
        std::vector<Move> out;
        for (uint16_t i = 0; i < n && ok; ++i) out.push_back(unpackMove(get<uint16_t>()));
        return out;
    }
};

void writeEval(Writer& w, const Eval& e)
{
    w.u8(uint8_t((e.valid ? 1 : 0) | (e.mate ? 2 : 0)));
    w.i32(e.score);
    w.u16(uint16_t(std::clamp(e.depth, 0, 65535)));
    w.u32(e.timeMs);
    w.u8(e.engine);
    w.moves(e.pv);
}

Eval readEval(Reader& r)
{
    Eval e;
    const uint8_t f = r.get<uint8_t>();
    e.valid = f & 1;
    e.mate = f & 2;
    e.score = r.get<int32_t>();
    e.depth = r.get<uint16_t>();
    e.timeMs = r.get<uint32_t>();
    e.engine = r.get<uint8_t>();
    e.pv = r.moves();
    return e;
}

std::vector<std::string> splitWs(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

} // namespace

Eval childEval(const Eval& e)
{
    Eval c;
    if (!e.valid || e.pv.empty() || (e.mate && e.score == 0)) return c;
    c.valid = true;
    c.mate = e.mate;
    if (!e.mate) c.score = -e.score;
    else c.score = e.score > 0 ? -(e.score - 1) : -e.score; // mate in N → the opponent is mated in N−1
    c.depth = std::max(0, e.depth - 1);
    c.timeMs = e.timeMs;
    c.engine = e.engine;
    c.pv.assign(e.pv.begin() + 1, e.pv.end());
    return c;
}

bool Project::positionOf(const Task& t, Position& out) const
{
    if (t.root < 0 || t.root >= int(tree.roots().size())) return false;
    Position pos;
    if (!pos.setFen(tree.roots()[size_t(t.root)].fen)) return false;
    for (const Move& m : t.path) {
        if (!pos.isLegal(m)) return false;
        pos.makeMove(m);
    }
    out = pos;
    return true;
}

std::vector<Move> Project::searchMoves(const Task& t) const
{
    Position pos;
    if (!positionOf(t, pos)) return {};
    const uint32_t n = tree.find(pos);
    std::vector<Move> out;
    for (const Move& m : pos.legalMoves())
        if (n == NoNode || !tree.edge(n, m)) out.push_back(m);
    return out;
}

uint64_t Project::addTask(Task t)
{
    t.id = m_nextId++;
    if (t.manual) t.priority = ManualPriority + double(t.id);
    m_queue.push_back(std::move(t));
    return m_queue.back().id;
}

std::optional<Task> Project::takeNext()
{
    if (m_queue.empty()) return std::nullopt;
    auto it = std::min_element(m_queue.begin(), m_queue.end(), [](const Task& a, const Task& b) { return a.priority < b.priority; });
    Task t = std::move(*it);
    m_queue.erase(it);
    return t;
}

std::optional<Task> Project::takeNext(const std::function<bool(const Task&)>& accept)
{
    auto best = m_queue.end();
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
        if (accept(*it) && (best == m_queue.end() || it->priority < best->priority)) best = it;
    if (best == m_queue.end()) return std::nullopt;
    Task t = std::move(*best);
    m_queue.erase(best);
    return t;
}

bool Project::removeTask(uint64_t id)
{
    const auto it = std::find_if(m_queue.begin(), m_queue.end(), [&](const Task& t) { return t.id == id; });
    if (it == m_queue.end()) return false;
    m_queue.erase(it);
    return true;
}

int Project::manualCount() const
{
    return int(std::count_if(m_queue.begin(), m_queue.end(), [](const Task& t) { return t.manual; }));
}

bool Project::preferredToMove(Color c) const
{
    return options.preferred == Side::Both || (options.preferred == Side::White && c == White) || (options.preferred == Side::Black && c == Black);
}

int Project::limitFor(Color c) const { return std::max(1, c == White ? options.limitWhite : options.limitBlack); }

int Project::generateStage()
{
    ++m_stage;
    tree.minimaxAll();
    // The remaining automatic tasks are redone from the up-to-date tree.
    std::erase_if(m_queue, isStageTask);
    int created = 0;
    if (!options.autoExpand) {
        for (size_t r = 0; r < tree.roots().size(); ++r) {
            const Root& root = tree.roots()[r];
            if (!root.enabled || tree.node(root.node).eval.valid) continue;
            Task t;
            t.root = int(r);
            addTask(t);
            ++created;
        }
        return created;
    }
    std::vector<uint8_t> seen(tree.size(), 0);
    for (size_t r = 0; r < tree.roots().size(); ++r) {
        const Root& root = tree.roots()[r];
        Position pos;
        if (!root.enabled || !pos.setFen(root.fen)) continue;
        std::vector<Move> path;
        generateFrom(int(r), root.node, pos, path, 0, 0, seen, created);
    }
    // A given target position is only requested once (transpositions).
    std::unordered_set<uint64_t> targets;
    std::vector<Task> kept;
    std::sort(m_queue.begin(), m_queue.end(), [](const Task& a, const Task& b) { return a.priority < b.priority; });
    created = 0;
    for (Task& t : m_queue) {
        Position p;
        if (!positionOf(t, p)) continue;
        const uint64_t key = polyglot::key(p) ^ (uint64_t(t.kind) << 62);
        if (!t.manual && !targets.insert(key).second) continue;
        if (isStageTask(t)) ++created;
        kept.push_back(std::move(t));
    }
    m_queue = std::move(kept);
    return created;
}

void Project::generateFrom(int root, uint32_t node, Position pos, std::vector<Move>& path, int ply, double gap, std::vector<uint8_t>& seen,
                           int& created)
{
    if (node == NoNode) return;
    if (node >= seen.size()) seen.resize(tree.size(), 0);
    if (seen[node]) return;
    seen[node] = 1;
    auto task = [&](TaskKind kind, std::vector<Move> p, double extra) {
        Task t;
        t.kind = kind;
        t.root = root;
        t.path = std::move(p);
        t.priority = gap + ply * 3 + extra;
        addTask(std::move(t));
        ++created;
    };
    const std::vector<Move> legal = pos.legalMoves();
    if (legal.empty()) { // mate or stalemate: nothing to ask the engine
        if (!tree.node(node).eval.valid) {
            Eval e;
            e.valid = true;
            e.mate = pos.inCheck();
            e.depth = 99;
            tree.setEval(node, e);
        }
        return;
    }
    const Node& n = tree.node(node);
    if (!n.eval.valid) {
        task(TaskKind::Evaluate, path, 0);
        return;
    }
    if (options.maxPlies > 0 && ply >= options.maxPlies) return;
    if (options.useBounds && ply > 0 && n.score != Unknown) {
        const int white = pos.sideToMove() == White ? n.score : -n.score;
        if (white < options.lowCp || white > options.highCp) return;
    }
    const Color stm = pos.sideToMove();
    const int window = std::max(1, options.widthCp);
    std::vector<const Edge*> open, focus;
    int best = Unknown;
    for (const Edge& e : n.edges) {
        if (e.mark == Mark::Exclude) continue;
        open.push_back(&e);
        if (e.mark == Mark::Focus) focus.push_back(&e);
        const int s = tree.moveScore(e);
        if (s != Unknown && (best == Unknown || s > best)) best = s;
    }
    if (open.empty()) {
        // Leaf: extend with the engine's move
        const std::vector<Move> pv = legalPrefix(pos, n.eval.pv);
        if (!pv.empty() && !tree.edge(node, pv.front())) {
            std::vector<Move> p = path;
            p.push_back(pv.front());
            task(TaskKind::Evaluate, std::move(p), 1);
        }
    } else if (focus.empty() && !n.allMovesFound && int(open.size()) < limitFor(stm) && best != Unknown) {
        // Alternative: the previous one was still close to the best move, or the expected line disappointed
        const int own = toCp(n.eval);
        if (n.alternatives == 0 || n.lastAlternative >= best - window || (own != Unknown && own - best > window))
            task(TaskKind::Alternative, path, 2);
    }
    for (const std::string& san : options.thematic) {
        const Move m = pos.parseSan(san);
        if (m.isNull() || tree.edge(node, m)) continue;
        std::vector<Move> p = path;
        p.push_back(m);
        task(TaskKind::Evaluate, std::move(p), 0);
    }
    // Lines to follow: green moves if there are any, otherwise the moves close to the best one
    const bool preferred = preferredToMove(stm) && focus.empty();
    const std::vector<const Edge*>& follow = focus.empty() ? open : focus;
    const Edge* bestEdge = tree.bestEdge(node);
    for (const Edge* e : follow) {
        const int s = tree.moveScore(*e);
        if (s != Unknown) {
            if (preferred && e != bestEdge) continue; // preferred side: only its best move
            if (focus.empty() && s < best - window) continue;
        }
        path.push_back(e->move);
        generateFrom(root, e->child, pos.after(e->move), path, ply + 1, gap + (s == Unknown || best == Unknown ? 0 : best - s), seen, created);
        path.pop_back();
    }
}

std::vector<Task> Project::applyResult(const Task& t, const Eval& raw)
{
    std::vector<Task> follow;
    if (t.root < 0 || t.root >= int(tree.roots().size())) return follow;
    Position start;
    if (!start.setFen(tree.roots()[size_t(t.root)].fen)) return follow;
    Position pos;
    const uint32_t node = tree.ensurePath(start, t.path, &pos);
    Eval e = raw;
    e.pv = legalPrefix(pos, raw.pv);
    if (t.kind != TaskKind::Alternative) {
        if (e.valid) tree.setEval(node, e);
        const bool tooLong = options.maxPlies > 0 && int(t.path.size()) >= options.maxPlies;
        if (t.kind == TaskKind::AutoPlay && t.plies > 1 && !e.pv.empty() && !tooLong) {
            Task next = t;
            next.path.push_back(e.pv.front());
            next.plies = t.plies - 1;
            const double priority = t.priority;
            const uint64_t id = addTask(next);
            for (Task& q : m_queue)
                if (q.id == id) {
                    q.priority = priority; // keeps its place (manual or false alarm)
                    follow.push_back(q);
                }
        }
        return follow;
    }
    // Alternative: the best move among those not yet in the tree
    Node& n = tree.node(node);
    ++n.alternatives;
    const Edge* before = tree.bestEdge(node);
    const int bestBefore = before ? tree.moveScore(*before) : Unknown;
    if (!e.valid || e.pv.empty()) {
        tree.node(node).allMovesFound = true;
        return follow;
    }
    const Move m = e.pv.front();
    const uint32_t child = tree.addEdge(node, pos, m);
    tree.node(node).lastAlternative = toCp(e);
    tree.setEval(child, childEval(e));
    bool remaining = false;
    for (const Move& lm : pos.legalMoves())
        if (!tree.edge(node, lm)) remaining = true;
    if (!remaining) tree.node(node).allMovesFound = true;
    // False alarm: the alternative looks better than the best move → checked right away
    if (options.falseAlarmPlies > 0 && bestBefore != Unknown && toCp(e) > bestBefore + 10
        && (options.maxPlies == 0 || int(t.path.size()) + 1 < options.maxPlies)) {
        Task check;
        check.kind = TaskKind::AutoPlay;
        check.root = t.root;
        check.path = t.path;
        check.path.push_back(m);
        check.plies = options.falseAlarmPlies;
        const uint64_t id = addTask(check);
        for (Task& q : m_queue)
            if (q.id == id) {
                q.priority = FalseAlarmPriority + double(id);
                follow.push_back(q);
            }
    }
    return follow;
}

int Project::addEpd(const std::string& text, bool results)
{
    int count = 0;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f = splitWs(line);
        if (f.size() < 4 || f[0][0] == '#') continue;
        std::string fen = f[0] + ' ' + f[1] + ' ' + f[2] + ' ' + f[3];
        size_t rest = 4;
        auto isInt = [](const std::string& s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; };
        if (f.size() >= 6 && isInt(f[4]) && isInt(f[5])) {
            fen += ' ' + f[4] + ' ' + f[5];
            rest = 6;
        } else {
            fen += " 0 1";
        }
        Position pos;
        if (!pos.setFen(fen)) continue;
        // Operations: "ce 35; acd 20; pv e4 e5; id "x";"
        std::string ops;
        for (size_t i = rest; i < f.size(); ++i) ops += f[i] + ' ';
        Eval e;
        std::string comment;
        std::istringstream os(ops);
        for (std::string op; std::getline(os, op, ';');) {
            std::vector<std::string> w = splitWs(op);
            if (w.empty()) continue;
            if (w[0] == "ce" && w.size() > 1) {
                e.valid = true;
                e.score = std::atoi(w[1].c_str());
            } else if (w[0] == "acd" && w.size() > 1) {
                e.depth = std::atoi(w[1].c_str());
            } else if ((w[0] == "pv" || (w[0] == "bm" && e.pv.empty())) && w.size() > 1) {
                e.pv.clear();
                Position p = pos;
                for (size_t i = 1; i < w.size(); ++i) {
                    const Move m = p.parseSan(w[i]);
                    if (m.isNull()) break;
                    e.pv.push_back(m);
                    p.makeMove(m);
                    if (w[0] == "bm") break;
                }
            } else if (w[0] == "id" && w.size() > 1) {
                for (size_t i = 1; i < w.size(); ++i) comment += (i > 1 ? " " : "") + w[i];
                std::erase(comment, '"');
            }
        }
        const int r = tree.addRoot(pos, comment);
        if (results && e.valid) {
            tree.setEval(tree.roots()[size_t(r)].node, e);
        } else if (!results) {
            Task t;
            t.root = r;
            t.manual = true;
            addTask(t);
        }
        ++count;
    }
    return count;
}

bool Project::save(const std::string& path, std::string* error) const
{
    Writer w;
    w.raw(Magic, 8);
    w.u32(Version);
    w.str(meta);
    // Options
    const Options& o = options;
    w.i32(o.quality.minTimeMs);
    w.i32(o.quality.minDepth);
    w.i32(o.quality.maxTimeMs);
    w.u8(o.quality.waitNextDepth);
    w.u8(o.autoExpand);
    w.i32(o.limitWhite);
    w.i32(o.limitBlack);
    w.i32(o.widthCp);
    w.i32(o.falseAlarmPlies);
    w.u8(uint8_t(o.preferred));
    w.u32(uint32_t(o.thematic.size()));
    for (const std::string& s : o.thematic) w.str(s);
    w.i32(o.maxPlies);
    w.u8(o.useBounds);
    w.i32(o.lowCp);
    w.i32(o.highCp);
    w.u8(o.insertSpectators);
    w.i32(o.spectatorMinTimeMs);
    w.i32(o.spectatorMinDepth);
    // Roots
    w.u32(uint32_t(tree.roots().size()));
    for (const Root& r : tree.roots()) {
        w.str(r.fen);
        w.u32(r.node);
        w.u8(r.enabled);
        w.str(r.comment);
    }
    // Nodes
    w.u32(uint32_t(tree.size()));
    for (uint32_t i = 0; i < tree.size(); ++i) {
        const Node& n = tree.node(i);
        w.u64(n.key);
        writeEval(w, n.eval);
        w.u16(n.alternatives);
        w.u8(n.allMovesFound);
        w.i32(n.lastAlternative);
        w.str(n.comment);
        w.u8(uint8_t(n.note));
        w.u64(uint64_t(n.noteDate));
        w.i32(n.noteInitialScore);
        w.u16(uint16_t(n.edges.size()));
        for (const Edge& e : n.edges) {
            w.u16(packMove(e.move));
            w.u32(e.child);
            w.u8(uint8_t(e.mark));
        }
    }
    // Task queue
    w.u64(m_nextId);
    w.i32(m_stage);
    w.u32(uint32_t(m_queue.size()));
    for (const Task& t : m_queue) {
        w.u64(t.id);
        w.u8(uint8_t(t.kind));
        w.i32(t.root);
        w.moves(t.path);
        w.i32(t.plies);
        w.u8(t.manual);
        w.i32(t.timeMs);
        w.i32(t.depth);
        w.i32(t.engine);
        w.f64(t.priority);
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(w.buf.data(), std::streamsize(w.buf.size()))) {
            if (error) *error = "écriture impossible : " + tmp;
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (error) *error = "remplacement impossible : " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool Project::load(const std::string& path, std::string* error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "ouverture impossible : " + path;
        return false;
    }
    const std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buf.size() < 12 || std::memcmp(buf.data(), Magic, 8) != 0) {
        if (error) *error = "ce n'est pas un projet d'analyse approfondie";
        return false;
    }
    Reader r{buf, 8};
    if (r.get<uint32_t>() != Version) {
        if (error) *error = "version du projet non gérée";
        return false;
    }
    Project p;
    p.meta = r.str();
    Options& o = p.options;
    o.quality.minTimeMs = r.get<int32_t>();
    o.quality.minDepth = r.get<int32_t>();
    o.quality.maxTimeMs = r.get<int32_t>();
    o.quality.waitNextDepth = r.get<uint8_t>();
    o.autoExpand = r.get<uint8_t>();
    o.limitWhite = r.get<int32_t>();
    o.limitBlack = r.get<int32_t>();
    o.widthCp = r.get<int32_t>();
    o.falseAlarmPlies = r.get<int32_t>();
    o.preferred = Side(r.get<uint8_t>());
    const uint32_t nThematic = r.get<uint32_t>();
    for (uint32_t i = 0; i < nThematic && r.ok; ++i) o.thematic.push_back(r.str());
    o.maxPlies = r.get<int32_t>();
    o.useBounds = r.get<uint8_t>();
    o.lowCp = r.get<int32_t>();
    o.highCp = r.get<int32_t>();
    o.insertSpectators = r.get<uint8_t>();
    o.spectatorMinTimeMs = r.get<int32_t>();
    o.spectatorMinDepth = r.get<int32_t>();
    std::vector<Root> roots(r.get<uint32_t>());
    for (Root& root : roots) {
        if (!r.ok) break;
        root.fen = r.str();
        root.node = r.get<uint32_t>();
        root.enabled = r.get<uint8_t>();
        root.comment = r.str();
    }
    const uint32_t count = r.get<uint32_t>();
    // Nodes read in order: the edge numbers stay valid.
    std::vector<Node> nodes(r.ok ? count : 0);
    for (uint32_t i = 0; i < nodes.size() && r.ok; ++i) {
        Node& n = nodes[i];
        n.key = r.get<uint64_t>();
        n.eval = readEval(r);
        n.alternatives = r.get<uint16_t>();
        n.allMovesFound = r.get<uint8_t>();
        n.lastAlternative = r.get<int32_t>();
        n.comment = r.str();
        n.note = NoteList(r.get<uint8_t>());
        n.noteDate = int64_t(r.get<uint64_t>());
        n.noteInitialScore = r.get<int32_t>();
        const uint16_t ne = r.get<uint16_t>();
        for (uint16_t k = 0; k < ne && r.ok; ++k) {
            Edge e;
            e.move = unpackMove(r.get<uint16_t>());
            e.child = r.get<uint32_t>();
            e.mark = Mark(r.get<uint8_t>());
            if (e.child < count) n.edges.push_back(e);
        }
    }
    p.m_nextId = r.get<uint64_t>();
    p.m_stage = r.get<int32_t>();
    const uint32_t nTasks = r.get<uint32_t>();
    for (uint32_t i = 0; i < nTasks && r.ok; ++i) {
        Task t;
        t.id = r.get<uint64_t>();
        t.kind = TaskKind(r.get<uint8_t>());
        t.root = r.get<int32_t>();
        t.path = r.moves();
        t.plies = r.get<int32_t>();
        t.manual = r.get<uint8_t>();
        t.timeMs = r.get<int32_t>();
        t.depth = r.get<int32_t>();
        t.engine = r.get<int32_t>();
        t.priority = r.get<double>();
        p.m_queue.push_back(std::move(t));
    }
    if (!r.ok) {
        if (error) *error = "projet tronqué ou abîmé";
        return false;
    }
    // Rebuilding the tree (key index, parents)
    p.tree.restore(std::move(nodes), std::move(roots));
    *this = std::move(p);
    return true;
}

} // namespace deep
