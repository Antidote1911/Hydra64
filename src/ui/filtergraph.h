#pragma once

#include "indexedsource.h"

#include <QDialog>
#include <QWidget>

#include <vector>

// Filter charts (after the "filter graph" of SCID vs PC): distribution of the displayed
// games by year and by average Elo, with stacked results (White wins, draws,
// Black wins) and White's score.
class FilterGraphDialog : public QDialog {
    Q_OBJECT
public:
    struct Bucket {
        int from = 0, to = 0; // bounds (years, or Elo)
        int white = 0, draw = 0, black = 0, unknown = 0;
        int total() const { return white + draw + black + unknown; }
    };
    FilterGraphDialog(const std::vector<IndexedGameSource::Summary>& games, int baseTotal, QWidget* parent = nullptr);

    // Computation (for the tests): year per slice of `step` years, average Elo per slice of 100.
    // Games without a year (or without both Elos) are counted separately.
    static std::vector<Bucket> byYear(const std::vector<IndexedGameSource::Summary>& games, int* missing, int step = 0);
    static std::vector<Bucket> byElo(const std::vector<IndexedGameSource::Summary>& games, int* missing);
};

class BucketChart : public QWidget {
    Q_OBJECT
public:
    BucketChart(std::vector<FilterGraphDialog::Bucket> buckets, QString unit, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {720, 360}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QRectF plotRect() const;
    int bucketAt(QPointF p) const;
    std::vector<FilterGraphDialog::Bucket> m_buckets;
    QString m_unit;
    int m_hover = -1;
};
