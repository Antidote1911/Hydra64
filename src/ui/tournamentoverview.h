#pragma once

#include <QHash>
#include <QWidget>

class BoardWidget;
class QGridLayout;
class QLabel;
class QTimer;
class Tournament;

// Overview of a tournament with simultaneous games: a small board per game in progress, with
// the names and the clocks. A click on a board shows its game in the notation (it is
// marked "●"); a double-click shows it full size.
class TournamentOverview : public QWidget {
    Q_OBJECT
public:
    explicit TournamentOverview(Tournament* tournament, QWidget* parent = nullptr);
    int boardCount() const { return int(m_tiles.size()); }
    QList<int> shownGames() const { return m_order; }
    void select(int index) { emit gameSelected(index); } // like a click on the game's board
    void choose(int index) { emit gameChosen(index); }   // like a double-click
    void setFlipped(bool flipped);

signals:
    void gameSelected(int index); // click: the game becomes that of the notation, the overview stays
    void gameChosen(int index);   // double-click: the game goes full size

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Tile {
        QWidget* frame = nullptr;
        QLabel* title = nullptr;
        BoardWidget* board = nullptr;
        QLabel* clocks = nullptr;
        int plies = -1; // half-moves displayed (to redraw only what changes)
    };
    void rebuild();          // one tile per game in progress
    void refresh(int index); // position and clocks of a game
    void refreshClocks();

    Tournament* m_tournament;
    QGridLayout* m_grid;
    QLabel* m_empty;
    QHash<int, Tile> m_tiles; // by game number
    QList<int> m_order;
    QTimer* m_timer;
    bool m_flipped = false;
};
