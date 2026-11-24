#pragma once

// Deep analysis project: tree, options, task queue. Generation of tasks in
// steps (like IDeA) and integration of the engine's results. Pure logic, with no engine or Qt.

#include "deeptree.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace deep {

// Quality of a position's analysis: at least `minTimeMs` AND `minDepth`, at most `maxTimeMs`
// (0: no limit), finishing the depth in progress if `waitNextDepth`.
struct Quality {
    int minTimeMs = 5000;
    int minDepth = 0;
    int maxTimeMs = 0;
    bool waitNextDepth = false;
};

enum class Side : uint8_t { None = 0, White, Black, Both };

struct Options {
    Quality quality;
    bool autoExpand = true;     // otherwise: only the given positions (EPD mode)
    int limitWhite = 6;         // alternatives at most per position, depending on the side to move
    int limitBlack = 6;
    int widthCp = 40;           // tree width: accepted gap from the best move
    int falseAlarmPlies = 4;    // 0: no immediate check
    Side preferred = Side::None;
    std::vector<std::string> thematic; // moves always tried (SAN: "Bxh7", "Ng5")
    int maxPlies = 0;           // maximum length of the variations from the root (0: none)
    bool useBounds = false;     // score bounds (White's point of view, centipawns)
    int lowCp = -300, highCp = 300;
    bool insertSpectators = false; // lines of the spectator engines sent to the tree
    int spectatorMinTimeMs = 10000;
    int spectatorMinDepth = 20;
};

enum class TaskKind : uint8_t { Evaluate = 0, Alternative = 1, AutoPlay = 2, Infinite = 3 };

struct Task {
    uint64_t id = 0;
    TaskKind kind = TaskKind::Evaluate;
    int root = 0;                  // root the path starts from
    std::vector<chess::Move> path; // moves from the root
    int plies = 0;                 // auto-play: moves left to play
    bool manual = false;           // requested by the user (takes priority)
    int timeMs = 0, depth = 0;     // custom task (0: project quality)
    int engine = -1;               // imposed engine (infinite task), -1: any
    double priority = 0;           // small = sooner
};

class Project {
public:
    Tree tree;
    Options options;
    std::string meta; // interface settings (JSON): state, CPU share, engines, dates…

    // Target position of a task (false if illegal path or unknown root).
    bool positionOf(const Task& t, chess::Position& out) const;
    // Moves to search for an alternative: legal moves not yet in the tree.
    std::vector<chess::Move> searchMoves(const Task& t) const;

    // Queue: pending tasks (manual ones first, then by priority).
    std::vector<Task>& queue() { return m_queue; }
    const std::vector<Task>& queue() const { return m_queue; }
    uint64_t addTask(Task t);           // number assigned; manual task takes priority
    std::optional<Task> takeNext();     // removes the next task
    std::optional<Task> takeNext(const std::function<bool(const Task&)>& accept); // the next one accepted
    bool removeTask(uint64_t id);
    int manualCount() const;

    // New step: full minimax, then automatic tasks added to the queue. Returns the
    // number of tasks created (0: nothing to do, the project waits for tasks).
    int generateStage();
    int stage() const { return m_stage; }

    // Results. `e`: evaluation of the task's target position (for an alternative:
    // of the starting position, with the search limited to the moves of searchMoves()).
    // Returns the tasks that follow from it (continuation of an auto-play, check of a false alarm),
    // already added to the queue.
    std::vector<Task> applyResult(const Task& t, const Eval& e);

    // EPD: one root per position; `results`: evaluations given by the operations
    // ce / acd / pv (or bm), otherwise evaluation tasks. Returns the number of positions read.
    int addEpd(const std::string& text, bool results);

    bool save(const std::string& path, std::string* error) const;
    bool load(const std::string& path, std::string* error);

private:
    void generateFrom(int root, uint32_t node, chess::Position pos, std::vector<chess::Move>& path, int ply, double gap,
                      std::vector<uint8_t>& seen, int& created);
    bool preferredToMove(chess::Color c) const;
    int limitFor(chess::Color c) const;

    std::vector<Task> m_queue;
    uint64_t m_nextId = 1;
    int m_stage = 0;
};

// Evaluation of the position after the first move of `e`'s PV (reversed point of view).
Eval childEval(const Eval& e);

} // namespace deep
