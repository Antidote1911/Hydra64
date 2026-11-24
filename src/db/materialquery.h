#pragma once

// Search by material and patterns (after the "Material / pattern search" of SCID vs PC), and
// kinds of position search (same pawns, same pawn files, same material).
// A game matches if a position of its main line meets all the criteria for
// at least `minPlies` half-moves in a row, between half-moves `plyFrom` and `plyTo`.

#include "chess/position.h"

#include <cstdint>
#include <optional>

namespace hy64db {

struct MaterialQuery {
    // Number of pieces [side][type] (Pawn…Queen; index 0 and King unused), bounds included.
    int8_t minCount[2][7] = {};
    int8_t maxCount[2][7] = {{9, 8, 9, 9, 9, 9, 9}, {9, 8, 9, 9, 9, 9, 9}};
    // Material difference White − Black (pawn 1, knight and bishop 3, rook 5, queen 9)
    bool useDiff = false;
    int diffMin = 0, diffMax = 0;
    enum class Bishops : uint8_t { Any, SameColor, OppositeColor }; // one bishop each
    Bishops bishops = Bishops::Any;

    enum Pattern : uint32_t {
        WhiteIsolatedQueenPawn = 1u << 0,  // isolated white d-pawn
        BlackIsolatedQueenPawn = 1u << 1,
        WhiteHangingPawns = 1u << 2,       // c- and d-pawns side by side with no b- and e-pawns
        BlackHangingPawns = 1u << 3,
        WhiteMaroczy = 1u << 4,            // white pawns c4 and e4, no white d-pawn; black pawn d6
        BlackMaroczy = 1u << 5,            // (mirror for Black)
        WhiteKingFianchetto = 1u << 6,     // bishop g2, pawn g3
        BlackKingFianchetto = 1u << 7,     // bishop g7, pawn g6
        WhiteQueenFianchetto = 1u << 8,    // bishop b2, pawn b3
        BlackQueenFianchetto = 1u << 9,
        OppositeCastling = 1u << 10,       // kings on opposite wings (a–c and g–h), after castling
        RookSacC3 = 1u << 11,              // a black rook captures on c3 ("exchange sacrifice on c3")
        WhitePassedPawn = 1u << 12,        // at least one passed pawn
        BlackPassedPawn = 1u << 13,
    };
    uint32_t patterns = 0; // all required

    // Comparison with a reference position
    enum class Reference : uint8_t { None, SamePawns, SamePawnFiles, SameMaterial };
    Reference reference = Reference::None;
    std::optional<chess::Position> refPos;

    int plyFrom = 0, plyTo = 0; // 0: no bound
    int minPlies = 1;           // criteria true over at least this many half-moves in a row

    bool isEmpty() const;
};

// Evaluates a game position by position (replayed in order from half-move 0).
class MaterialMatcher {
public:
    explicit MaterialMatcher(const MaterialQuery& q);
    // False: no point in going on (found, or no chance left: material too low, beyond
    // plyTo).
    bool feed(const chess::Position& pos, int ply);
    bool matched() const { return m_matched; }
    void reset();

    static bool positionMatches(const MaterialQuery& q, const chess::Position& pos, const chess::Position* prev);

private:
    const MaterialQuery& m_q;
    chess::Position m_prev;
    bool m_hasPrev = false;
    int m_run = 0;
    bool m_matched = false;
};

// Displayable name of a pattern (for the dialog), in French.
const char* patternName(MaterialQuery::Pattern p);

} // namespace hy64db
