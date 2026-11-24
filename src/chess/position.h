#pragma once

#include "types.h"

#include <array>
#include <string>
#include <vector>

namespace chess {

class Position {
public:
    Position();
    static Position startPos();

    bool setFen(const std::string& fen);
    std::string fen() const;

    Piece at(Square s) const { return m_board[s]; }
    Color sideToMove() const { return m_stm; }
    int fullmoveNumber() const { return m_fullmove; }
    int halfmoveClock() const { return m_halfmove; }
    Square epSquare() const { return m_ep; }
    uint8_t castlingRights() const { return m_castling; } // 1 = K, 2 = Q, 4 = k, 8 = q
    // Chess960: king or castling rook off their classical squares (e1/e8, a/h). Castling is then
    // written "king captures its rook" (Move{king, rook's square}); in a classical position it stays
    // e1g1 / e1c1. isCastling() recognises both forms.
    // Move playable without checking that the king stays out of check (castling: full check).
    // For moves already validated by their source: much cheaper than isLegal().
    bool isPseudoLegal(Move m) const;
    bool isChess960() const;
    bool isCastling(Move m) const;
    bool isKingsideCastling(Move m) const { return fileOf(m.to) > fileOf(m.from); } // for a castling move
    Square castlingRook(Color c, bool kingside) const { return m_castleRook[(c == White ? 0 : 2) + (kingside ? 0 : 1)]; }
    // Castling move of the side to move, in the position's form (without checking it is legal); null without the right.
    Move castlingMove(bool kingside) const;
    // Chess960 starting position no. 0…959 (518 = classical position).
    static Position chess960Start(int number);

    std::vector<Move> legalMoves() const;
    bool isLegal(Move m) const;
    bool hasLegalMove() const; // at least one legal move (stops at the first)
    void makeMove(Move m); // the move must be legal; a null move (Move()) passes the turn
    Position after(Move m) const { Position p = *this; p.makeMove(m); return p; }

    bool isAttacked(Square s, Color by) const;
    Square kingSquare(Color c) const;
    bool inCheck() const;
    bool isCheckmate() const;
    bool isStalemate() const;
    bool isInsufficientMaterial() const;

    // Notation
    std::string toSan(Move m) const;
    Move parseSan(std::string_view san) const;
    // Like parseSan, but accepts an irregular move that plays mechanically: king or pinned
    // piece leaving the king in check, pawn capture written onto an empty square. ChessBase keeps
    // such moves (badly entered games, transposition notes); to be tried only after parseSan.
    Move parseSanLenient(std::string_view san) const;
    Move parseUci(std::string_view uci) const;
    static std::string toUci(Move m);

private:
    Move parseSanWith(std::string_view san, bool lenient) const;
    void generatePseudo(std::vector<Move>& out) const;
    // Can the piece of the side to move on `from` go to `to` (geometry, free squares, capture;
    // without castling, ignoring check)?
    bool canReach(Square from, Square to) const;
    // After the (pseudo-legal) move, the king of the side that played it is not in check.
    bool leavesKingSafe(Move m) const;
    void addPawnMove(std::vector<Move>& out, Square from, Square to) const;

    std::array<Piece, 64> m_board{};
    Color m_stm = White;
    uint8_t m_castling = 0; // 1 = K, 2 = Q, 4 = k, 8 = q
    std::array<Square, 4> m_castleRook{7, 0, 63, 56}; // rook of each castling right (K, Q, k, q)
    std::array<Square, 2> m_king{NoSquare, NoSquare}; // square of each king, kept up to date by setFen and makeMove
    Square findKing(Color c) const;
    Square m_ep = NoSquare;
    int m_halfmove = 0;
    int m_fullmove = 1;
};

} // namespace chess
