#pragma once

#include <QWidget>

class QLabel;

// "Clock" panel, Fritz style: two large displays (White on the left, Black on the right),
// the side whose time is running highlighted. Follows the clock displayed on the board
// (game in progress, or recorded [%clk] times on replay).
class ClockPanel : public QWidget {
    Q_OBJECT
public:
    explicit ClockPanel(QWidget* parent = nullptr);

    void setClocks(qint64 whiteMs, qint64 blackMs, int runningSide, bool live);
    // "300+3" (PGN TimeControl header) → "Blitz 5+3"; empty if unknown.
    static QString timeControlLabel(const QString& pgnTimeControl);
    static QString format(qint64 ms); // « 0:04:10 », « 0:00:09.4 »

private:
    QLabel* m_time[2];
    QLabel* m_mark[2];
};
