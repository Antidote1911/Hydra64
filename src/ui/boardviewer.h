#pragma once

#include <QLabel>
#include <QWidget>

namespace chess {
struct Node;
}

class BoardWidget;
class EvalBar;
class GameSession;
class QLabel;

// Player strip: colour dot, name, Elo, material advantage.
class PlayerStrip : public QWidget {
    Q_OBJECT
public:
    explicit PlayerStrip(QWidget* parent = nullptr);
    void set(bool white, const QString& name, const QString& elo, int materialAdvantage, bool toMove);
    void setStatus(const QString& text); // e.g. "réfléchit…" (thinking)
    void setClock(const QString& text, bool active, bool low); // empty text: clock hidden

protected:
    void paintEvent(QPaintEvent*) override;

private:
    bool m_white = true;
    bool m_toMove = false;
    QLabel* m_name;
    QLabel* m_elo;
    QLabel* m_material;
    QLabel* m_status;
    QLabel* m_clock;
};

// Banner centred on the board (end of game). A click outside the buttons closes it.
class Banner : public QWidget {
    Q_OBJECT
public:
    explicit Banner(QWidget* parent = nullptr);
    // `offerActions`: end of a game against the engine (save, analyse, rematch…).
    void setText(const QString& title, const QString& text, bool offerActions);

signals:
    void saveRequested();
    void analyseRequested();
    void rematchRequested();
    void newGameRequested();

protected:
    void mousePressEvent(QMouseEvent*) override { hide(); }

private:
    QLabel* m_title;
    QLabel* m_text;
    QWidget* m_actions;
};

// Places the evaluation bar against the board, the whole centred in the area.
class BoardArea : public QWidget {
    Q_OBJECT
public:
    BoardArea(EvalBar* bar, BoardWidget* board, PlayerStrip* top, PlayerStrip* bottom, Banner* banner, QWidget* parent = nullptr);
    void relayout();

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    EvalBar* m_bar;
    BoardWidget* m_board;
    PlayerStrip* m_top;
    PlayerStrip* m_bottom;
    Banner* m_banner;
};

// Label of a line cut by "…" when room is short (long opening name).
class ElidedLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;
    QString fullText() const { return text(); }

protected:
    void paintEvent(QPaintEvent* e) override;
};

// Central "viewer" area: players, evaluation bar, board, transport.
class BoardViewer : public QWidget {
    Q_OBJECT
public:
    explicit BoardViewer(GameSession* session, QWidget* parent = nullptr);

    BoardWidget* board() const { return m_board; }
    EvalBar* evalBar() const { return m_evalBar; }
    void setFlipped(bool flipped);
    bool isFlipped() const;
    void setEvalBarVisible(bool visible);
    void setSendToAnalysisVisible(bool visible);
    void setPlayerStatus(bool white, const QString& text);
    void showBanner(const QString& title, const QString& text, bool offerActions = false);
    bool isBannerVisible() const;
    void hideBanner();
    // Live clock (game in progress); otherwise, times recorded in the game ([%clk]).
    void setLiveClocks(bool live, qint64 whiteMs = 0, qint64 blackMs = 0, int runningSide = -1);
    // Clocks displayed before the game (chosen time control); -1: nothing to display.
    void setPreviewClocks(qint64 whiteMs, qint64 blackMs);
    // Under the board: the opening of the displayed position ("B90 Sicilian Defense: Najdorf
    // Variation"), otherwise the last move.
    QString moveInfoText() const;
    // Deepest opening recognised between `node` and the start of the game (empty: none).
    static QString openingAt(const chess::Node* node);

signals:
    void flipRequested();
    void editRequested();
    void sendToAnalysisRequested();
    void saveRequested();
    void analyseRequested();  // end-of-game banner: analyse the game
    void rematchRequested();  // … rematch (colours reversed)
    void newGameRequested();  // … new game (settings)
    // Displayed times (‑1: unknown), side whose clock is running (‑1: none), live clock.
    void clocksChanged(qint64 whiteMs, qint64 blackMs, int runningSide, bool live);

private:
    void refresh();
    void updateClocks();

    GameSession* m_session;
    BoardWidget* m_board;
    EvalBar* m_evalBar;
    BoardArea* m_area;
    PlayerStrip* m_top;
    PlayerStrip* m_bottom;
    class ElidedLabel* m_moveInfo;
    Banner* m_banner;
    bool m_liveClocks = false;
    qint64 m_previewMs[2] = {-1, -1};
    class QToolButton* m_sendButton;
    qint64 m_liveMs[2] = {0, 0};
    int m_running = -1;
};
