#pragma once

#include "chess/game.h"

#include <QWidget>

class QVariantAnimation;

// Vertical evaluation bar (White at the bottom, reversed if the board is).
class EvalBar : public QWidget {
    Q_OBJECT
public:
    explicit EvalBar(QWidget* parent = nullptr);

    void setEvaluation(const chess::Evaluation& eval);
    void setFlipped(bool flipped);

    // "White" fraction between 0 and 1 for an evaluation (also used by the graph).
    static qreal whiteShare(const chess::Evaluation& eval);
    static QString format(const chess::Evaluation& eval);

    QSize sizeHint() const override { return {22, 400}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    chess::Evaluation m_eval;
    qreal m_share = 0.5;
    bool m_flipped = false;
    QVariantAnimation* m_anim;
};
