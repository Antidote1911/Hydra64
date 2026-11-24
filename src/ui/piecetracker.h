#pragma once

#include "chess/game.h"

#include <QDialog>
#include <QWidget>

#include <array>

class QComboBox;
class QLabel;
class QSpinBox;

// Piece tracking (after the "piece tracker" of SCID vs PC): for the main line of the
// game, the share of half-moves a piece spent on each square. A specific piece is followed
// (by its starting square, castling and promotions included) or all the pieces of a type.
class PieceTracker : public QDialog {
    Q_OBJECT
public:
    struct Spec {
        chess::Square origin = chess::NoSquare; // specific piece (square in the starting position)
        chess::Piece piece = chess::NoPiece;    // otherwise: all the pieces of this type and colour
    };
    // Half-moves spent on each square (positions from plyFrom to plyTo inclusive, 0 = start;
    // plyTo < 0: to the end). `positions`: number of positions counted.
    static std::array<int, 64> track(const chess::Game& game, const Spec& spec, int plyFrom, int plyTo, int* positions = nullptr);

    PieceTracker(const chess::Game& game, QWidget* parent = nullptr);
    void setSpec(const Spec& spec); // for the tests
    const std::array<int, 64>& counts() const { return m_counts; }

private:
    void refresh();
    chess::Game m_game;
    QComboBox* m_piece;
    QSpinBox* m_from;
    QSpinBox* m_to;
    QLabel* m_info;
    class HeatBoard* m_board;
    std::array<int, 64> m_counts{};
};

class HeatBoard : public QWidget {
    Q_OBJECT
public:
    explicit HeatBoard(QWidget* parent = nullptr);
    void setData(const std::array<int, 64>& counts, int positions, const chess::Position& shown);
    QSize sizeHint() const override { return {440, 440}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::array<int, 64> m_counts{};
    int m_positions = 0;
    chess::Position m_pos;
};
