#pragma once

#include "position.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chess {

struct Evaluation {
    bool valid = false;
    bool mate = false;
    int value = 0; // centipawns (or moves to mate), from White's point of view
    int depth = 0;
};

// Graphic annotation of a position: arrow (from → to) or marked square (to = NoSquare),
// in a colour 'G' (green), 'R' (red), 'Y' (yellow) or 'B' (blue). In PGN: [%cal Ge2e4] and
// [%csl Rd5] in the move's comment (ChessBase and Lichess convention).
struct Graphic {
    Square from = NoSquare, to = NoSquare;
    char color = 'G';
    bool isArrow() const { return to != NoSquare; }
    bool operator==(const Graphic&) const = default;
};

struct Node {
    int id = 0;
    Move move;           // move leading to this node (null for the root)
    std::string san;
    Position pos;        // position after the move
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children; // children[0] = main continuation
    std::string comment;
    // Comment placed before the move ("({or} 8. Nbd2)": ChessBase tells the two apart). In PGN it
    // only exists at the start of a variation, after the previous move's comment or after a
    // variation; elsewhere, a pre-move text reads as the previous move's comment.
    std::string preComment;
    std::vector<int> nags;
    Evaluation eval;
    int clockMs = -1; // player's remaining time after this move ([%clk] in PGN), -1 if unknown
    std::vector<Graphic> graphics; // arrows and marked squares on the position after this move
    uint16_t medals = 0;           // ChessBase medals ([%mdl n] in PGN), see pgn::medalNames
    // Training question: this move is to be found ([%tqu …] in PGN); the wording may be empty.
    bool question = false;
    std::string questionText;
    // Full question coming from ChessBase: the arguments of its "[%tqu … ]" as they are (per
    // language: question, two hints, move, feedback, points). Empty for a question asked here, of which
    // only the wording is kept; cleared when the wording is edited.
    std::string questionData;

    Node* mainChild() const { return children.empty() ? nullptr : children.front().get(); }
    bool isMainline() const;
    int ply() const;
};

// End of game by the rules (no resignation, no time).
struct GameEnd {
    enum Kind { Checkmate, Stalemate, InsufficientMaterial, FiftyMoves, Repetition };
    std::string result; // "1-0", "0-1" ou "1/2-1/2"
    std::string reason; // French label (the interface translates it from `kind`)
    Kind kind = Checkmate;
};

// True if the game is over after node `n` (mate, stalemate, insufficient material,
// 50-move rule, threefold repetition along the ancestors).
bool detectGameEnd(const Node* n, GameEnd& out);

class Game {
public:
    using Headers = std::vector<std::pair<std::string, std::string>>;

    Game();
    explicit Game(const Position& start);
    Game(Game&&) noexcept = default;
    Game& operator=(Game&&) noexcept = default;

    void reset(const Position& start = Position::startPos());

    Headers& headers() { return m_headers; }
    const Headers& headers() const { return m_headers; }
    std::string header(const std::string& key) const;
    void setHeader(const std::string& key, const std::string& value);

    Node* root() const { return m_root.get(); }
    Node* current() const { return m_current; }
    void setCurrent(Node* n) { if (n) m_current = n; }
    const Position& startPosition() const { return m_root->pos; }

    // Adds (or finds) move m played from `from`. `newBranch`: always a new
    // node, even if this move already exists (variation repeating the move it replaces).
    // `withSan` false: the move's notation is not computed (Node::san empty) — for games
    // only meant to be encoded into a database; pgn::writeGame recomputes it when needed.
    Node* addMove(Node* from, Move m, bool newBranch = false, bool withSan = true);
    Node* playMove(Move m) { return m_current = addMove(m_current, m); }

    bool forward();
    bool back();
    void toStart() { m_current = m_root.get(); }
    void toEnd();

    std::vector<Node*> mainline() const; // without the root
    Node* findById(int id) const;

    // Merges `other` into this game: its moves join the tree, as variations where they
    // diverge; comments, symbols and arrows of the new moves are taken over. `label`
    // ("White – Black, site year") is added as a comment at the end of its main line.
    // Number of moves added, or -1 if the two games do not start from the same position.
    int merge(const Game& other, const std::string& label = {});
    void deleteNode(Node* n);
    void promoteVariation(Node* n);
    // The variation of `n` goes down one rank among those of its position (the main line becomes
    // the first variation). False if it is already the last.
    bool demoteVariation(Node* n);
    // Replaces move `n` with `m`, in its place (main line or variation). `keepFollowing` false:
    // what followed `n` is dropped ("overwrite"); true: the moves that followed are kept as long as
    // they are still legal, with their comments, symbols and variations ("insert"). The new move
    // carries no annotation. Returns its node; null if `m` is not legal there or is the move of a
    // sibling of `n`. The current node goes to the new move if it was in the replaced part.
    Node* replaceMove(Node* n, Move m, bool keepFollowing);

private:
    Headers m_headers;
    std::unique_ptr<Node> m_root;
    Node* m_current = nullptr;
    int m_nextId = 1;
};

} // namespace chess
