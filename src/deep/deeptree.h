#pragma once

// Deep analysis tree (in the manner of Aquarium's IDeA and BanksiaGUI's ECA).
//
// Graph of positions merged by Polyglot key: a transposition shares its analysis.
// Each node keeps the engine's evaluation (score of the side to move), the score backed up by
// minimax and the moves (edges) to the following positions, each with a user
// mark: green (only move to analyse here) or red (excluded from the analysis).
//
// .hy64deep file (little-endian): "B64DEEP1", version (u32), options (string), roots,
// nodes (key, evaluation, PV, alternatives, notes, edges), pending tasks. Strings:
// u32 length + UTF-8. Moves: u16 (from | to << 6 | promotion << 12).

#include "chess/position.h"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace deep {

constexpr int MateValue = 30000;
constexpr int Unknown = std::numeric_limits<int>::min();
constexpr uint32_t NoNode = 0xFFFFFFFFu;

// Evaluation of a position by the engine, from the point of view of the side to move.
struct Eval {
    bool valid = false;
    bool mate = false;
    int score = 0; // centipawns, or moves to mate (signed; 0 = the side to move is mated)
    int depth = 0;
    uint32_t timeMs = 0;
    uint8_t engine = 0; // number of the engine in the project
    std::vector<chess::Move> pv;
};

// Comparable score (centipawns; mate = ±(MateValue − half-moves)).
int toCp(const Eval& e);

enum class Mark : uint8_t { None = 0, Focus = 1, Exclude = 2 }; // green, red
enum class NoteList : uint8_t { None = 0, Good, Interesting, Critical, Dubious };

struct Edge {
    chess::Move move;
    uint32_t child = NoNode;
    Mark mark = Mark::None;
};

struct Node {
    uint64_t key = 0;
    Eval eval;
    int score = Unknown;          // minimax (side to move); Unknown without evaluation
    uint32_t positions = 1;       // positions of the branch (computed by minimaxAll)
    uint16_t alternatives = 0;    // alternatives searched in this position
    bool allMovesFound = false;   // no alternative possible any more
    int lastAlternative = Unknown;// score of the last alternative found
    std::vector<Edge> edges;
    std::vector<uint32_t> parents;
    // User notes
    std::string comment;
    NoteList note = NoteList::None;
    int64_t noteDate = 0;          // seconds since 1970
    int noteInitialScore = Unknown;
};

struct Root {
    std::string fen;
    uint32_t node = NoNode;
    bool enabled = true;
    std::string comment;
};

class Tree {
public:
    // Nodes
    uint32_t find(uint64_t key) const;
    uint32_t find(const chess::Position& pos) const;
    uint32_t ensure(const chess::Position& pos); // creates the node if missing
    Node& node(uint32_t i) { return m_nodes[i]; }
    const Node& node(uint32_t i) const { return m_nodes[i]; }
    size_t size() const { return m_nodes.size(); }

    // Edge `move` from `parent` to the next position (created if needed). Returns the child.
    uint32_t addEdge(uint32_t parent, const chess::Position& parentPos, chess::Move move);
    Edge* edge(uint32_t parent, chess::Move move);
    const Edge* edge(uint32_t parent, chess::Move move) const;
    // Path from a position: creates the nodes and edges; returns the last node.
    uint32_t ensurePath(const chess::Position& start, const std::vector<chess::Move>& path, chess::Position* end = nullptr);

    // Roots
    std::vector<Root>& roots() { return m_roots; }
    const std::vector<Root>& roots() const { return m_roots; }
    int addRoot(const chess::Position& pos, const std::string& comment = {}); // index; if existing: its index

    // Evaluation of a node; the minimax of the ancestors is updated.
    void setEval(uint32_t node, const Eval& e);
    // Score of the move from the point of view of the side playing it (Unknown if the child has no score).
    int moveScore(const Edge& e) const;
    // Best (non-red) move of a node, or nullptr.
    const Edge* bestEdge(uint32_t node) const;
    // Recomputes everything: minimax (repetition = draw) and positions of the branches.
    void minimaxAll();
    // Updates the minimax of `node` then of its ancestors.
    void propagate(uint32_t node);
    // Best line from a node (without loops), at most `maxPlies` moves.
    std::vector<chess::Move> bestLine(uint32_t node, int maxPlies = 60) const;

    // Merge: the deeper evaluations and the edges of `other` go into this tree
    // (master tree). Roots are not copied.
    void merge(const Tree& other);

    void clear();
    // Loading: nodes (edges included) and roots; index and parents rebuilt, full minimax.
    void restore(std::vector<Node> nodes, std::vector<Root> roots);

private:
    int localScore(uint32_t i) const;
    std::vector<Node> m_nodes;
    std::unordered_map<uint64_t, uint32_t> m_index;
    std::vector<Root> m_roots;
};

uint16_t packMove(chess::Move m);
chess::Move unpackMove(uint16_t v);

} // namespace deep
