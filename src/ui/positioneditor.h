#pragma once

#include "chess/position.h"
#include "piecerenderer.h"

#include <QDialog>

#include <array>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// Editable board: place, remove, move pieces.
class EditorBoard : public QWidget {
    Q_OBJECT
public:
    // Tools: a FEN letter (piece to place), MoveTool (move) or EraseTool (eraser).
    static constexpr char MoveTool = '>';
    static constexpr char EraseTool = 'x';
    using Squares = std::array<char, 64>; // ' ' = empty, otherwise FEN letter; index 0 = a1

    explicit EditorBoard(QWidget* parent = nullptr);
    void setSquares(const Squares& s);
    const Squares& squares() const { return m_squares; }
    void setTool(char tool) { m_tool = tool; update(); }
    void setFlipped(bool f) { m_flipped = f; update(); }
    bool isFlipped() const { return m_flipped; }
    QPixmap pieceIcon(char piece, int size);

    QSize sizeHint() const override { return {560, 560}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF boardRect() const;
    QRectF squareRect(int sq) const;
    int squareAt(QPointF p) const;
    void put(int sq, char piece); // handles the uniqueness of the kings

    Squares m_squares{};
    char m_tool = 'P';
    bool m_flipped = false;
    int m_hover = -1;
    int m_dragFrom = -1;
    QPointF m_dragPos;
    enum class Paint { None, Place, Erase } m_paint = Paint::None;
    char m_paintPiece = ' ';
    PieceRenderer m_pieces;
};

// Position editor (inspired by Aquarium's "Set up position").
class PositionEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit PositionEditorDialog(const chess::Position& start, bool flipped, QWidget* parent = nullptr);
    chess::Position position() const { return m_result; }

private:
    void loadFen(const QString& fen);
    QString buildFen() const;
    void refresh(); // possible castling rights, en passant squares, validation, FEN
    QString validate(chess::Position* out) const;
    void mirrorFiles();
    void invertColors();

    EditorBoard* m_board;
    QButtonGroup* m_tools;
    QButtonGroup* m_side;
    QCheckBox* m_castle[4]; // K Q k q
    // File of the rook of each right, as read from the position (Chess960: not necessarily the
    // outermost); -1: the outermost rook.
    int m_castleRookFile[4] = {-1, -1, -1, -1};
    QComboBox* m_ep;
    QSpinBox* m_fullmove;
    QSpinBox* m_halfmove;
    QLineEdit* m_fen;
    QLabel* m_status;
    QPushButton* m_ok;
    chess::Position m_result;
    bool m_updating = false;
};
