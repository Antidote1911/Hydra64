#pragma once

#include <QWidget>

class GameSession;
namespace chess { struct Node; }

// Evaluation graph of the main line; a click jumps to the corresponding move.
class EvalGraph : public QWidget {
    Q_OBJECT
public:
    explicit EvalGraph(GameSession* session, QWidget* parent = nullptr);

    QSize sizeHint() const override { return {600, 140}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF plotRect() const;
    int plyAt(qreal x) const;

    GameSession* m_session;
    int m_hoverPly = -1;
};
