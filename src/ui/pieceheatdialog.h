#pragma once

#include "db/heatmap.h"

#include <QDialog>

class HeatBoard;
class QComboBox;
class QLabel;
class QToolButton;

// Database → Statistics → Pieces on the displayed games…: where a kind of piece stands, over all
// the displayed games (share of the positions, by slice of the game), or where the first move of
// a piece of the starting position takes it.
class PieceHeatDialog : public QDialog {
    Q_OBJECT
public:
    PieceHeatDialog(const hy64db::PieceHeat& heat, const QString& what, QWidget* parent = nullptr);
    enum Mode { Presence, FirstMove };
    void setMode(Mode mode);
    void setPiece(chess::Piece piece);     // Presence
    void setOrigin(chess::Square origin);  // FirstMove
    void setSlice(int slice);              // -1: the whole game
    const std::array<int, 64>& shown() const { return m_shown; } // millionths
    QString infoText() const;

private:
    void fill();
    void refresh();
    hy64db::PieceHeat m_heat;
    Mode m_mode = Presence;
    QToolButton* m_modeButton[2];
    QComboBox* m_piece;
    QComboBox* m_slice;
    QLabel* m_sliceLabel;
    QLabel* m_info;
    HeatBoard* m_board;
    std::array<int, 64> m_shown{};
};
