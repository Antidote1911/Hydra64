#pragma once

#include "chess/game.h"
#include "chess/position.h"
#include "piecerenderer.h"

#include <QHash>
#include <QSet>
#include <QColor>
#include <QMap>
#include <QPair>
#include <QWidget>

#include <vector>

class QVariantAnimation;

class BoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit BoardWidget(QWidget* parent = nullptr);

    void setPosition(const chess::Position& pos, chess::Move lastMove, bool animate);
    const chess::Position& position() const { return m_pos; }

    void setFlipped(bool flipped);
    bool isFlipped() const { return m_flipped; }

    // Arrow of the move proposed by an engine (`key`: the engine), in its colour; null = none.
    void setEngineArrow(const QString& key, chess::Move m, const QColor& color);
    // Assisted analysis: quality of the moves of the displayed position, by from × 64 + to
    // (1 good, 2 playable, 3 mistake); the target squares of the piece picked up take its colour.
    // Forgotten at the next position.
    void setMoveQualities(const QHash<int, int>& qualities);
    static QColor qualityColor(int quality);
    // Sides the user can move (both by default; none while the engine is playing).
    void setMovableSides(bool white, bool black);
    bool isMovable(chess::Color c) const { return m_movable[c]; } // can this side play?

    QSize sizeHint() const override { return {640, 640}; }
    QSize minimumSizeHint() const override { return {240, 240}; }

    // Arrows and marked squares of the position (right-click: square; right-drag: arrow;
    // green, Shift: red, Ctrl: yellow, Shift+Ctrl: blue; repeating the same gesture removes).
    void setGraphics(const std::vector<chess::Graphic>& graphics);
    const std::vector<chess::Graphic>& graphics() const { return m_graphics; }
    static QColor graphicColor(char color);
    QPointF squareCenter(chess::Square s) const { return squareRect(s).center(); } // in the widget

signals:
    void graphicsChanged(const std::vector<chess::Graphic>& graphics); // changed with the mouse
    void moveRequested(chess::Move move);
    void wheelSteps(int steps); // > 0: wheel down

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    QRectF boardRect() const;
    qreal squareSize() const { return boardRect().width() / 8.0; }
    QRectF squareRect(chess::Square s) const;
    chess::Square squareAt(QPointF p) const;
    bool isTarget(chess::Square from, chess::Square to) const;
    chess::Move resolve(chess::Square from, chess::Square to) const; // legal move, Chess960 castling included
    void tryMove(chess::Square from, chess::Square to, bool fromDrag);
    void drawArrow(QPainter& p, chess::Square from, chess::Square to, const QColor& color, qreal width) const;

    chess::Position m_pos;
    std::vector<chess::Move> m_legal;
    chess::Move m_lastMove;
    QMap<QString, QPair<chess::Move, QColor>> m_engineArrows;
    QHash<int, int> m_qualities;
    bool m_flipped = false;
    bool m_movable[2] = {true, true};

    // Interaction
    chess::Square m_selected = chess::NoSquare;
    chess::Square m_pressSquare = chess::NoSquare;
    chess::Square m_hover = chess::NoSquare;
    bool m_wasSelected = false;
    bool m_dragging = false;
    QPointF m_pressPos, m_dragPos;
    bool m_suppressAnimation = false;
    int m_wheelAccum = 0; // touchpads: small increments to accumulate

    // Right-click annotations
    chess::Square m_arrowFrom = chess::NoSquare;
    std::vector<chess::Graphic> m_graphics;

    // Animation of the last move
    QVariantAnimation* m_anim = nullptr;
    chess::Piece m_animPiece = chess::NoPiece;
    chess::Square m_animFrom = chess::NoSquare, m_animTo = chess::NoSquare;
    qreal m_animT = 1.0;

    PieceRenderer m_pieces;
};
