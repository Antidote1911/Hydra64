#pragma once

#include "chess/game.h"

#include <QDialog>

class BoardWidget;
class QLabel;
class QTextBrowser;

// Preview of a game (like "Browse game" of SCID vs PC): board and moves, without loading the
// game into the page. Arrows or wheel to go forward and back, Home / End.
class GamePreviewDialog : public QDialog {
    Q_OBJECT
public:
    GamePreviewDialog(chess::Game game, const QString& title, QWidget* parent = nullptr);
    void goTo(int ply); // 0: starting position
    int ply() const { return m_ply; }
    int plies() const { return int(m_line.size()); }

signals:
    void openRequested(); // "Open the game"

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void refresh();

    chess::Game m_game;
    std::vector<chess::Node*> m_line;
    int m_ply = 0;
    BoardWidget* m_board;
    QLabel* m_header;
    QTextBrowser* m_moves;
};
