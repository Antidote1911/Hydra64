#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chess {

enum Color : int8_t { White = 0, Black = 1 };
inline Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int8_t { NoType = 0, Pawn, Knight, Bishop, Rook, Queen, King };

// Piece encoded in one byte: type | 8 if black. 0 = empty square.
using Piece = int8_t;
constexpr Piece NoPiece = 0;
inline Piece makePiece(Color c, PieceType t) { return Piece(t | (c == Black ? 8 : 0)); }
inline PieceType typeOf(Piece p) { return PieceType(p & 7); }
inline Color colorOf(Piece p) { return (p & 8) ? Black : White; }

// Squares: 0 = a1, 7 = h1, 56 = a8, 63 = h8.
using Square = int8_t;
constexpr Square NoSquare = -1;
inline int fileOf(Square s) { return s & 7; }
inline int rankOf(Square s) { return s >> 3; }
inline Square makeSquare(int file, int rank) { return Square(rank * 8 + file); }

inline std::string squareName(Square s)
{
    if (s < 0 || s > 63) return "-";
    return {char('a' + fileOf(s)), char('1' + rankOf(s))};
}

inline Square parseSquare(std::string_view sv)
{
    if (sv.size() < 2) return NoSquare;
    int f = sv[0] - 'a', r = sv[1] - '1';
    if (f < 0 || f > 7 || r < 0 || r > 7) return NoSquare;
    return makeSquare(f, r);
}

struct Move {
    Square from = NoSquare;
    Square to = NoSquare;
    PieceType promo = NoType;

    bool isNull() const { return from == NoSquare; }
    bool operator==(const Move&) const = default;
};

} // namespace chess
