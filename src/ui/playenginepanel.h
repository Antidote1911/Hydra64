#pragma once

#include "engine/engineregistry.h"
#include "engine/uciengine.h"

#include <QMap>
#include <QWidget>

class EngineGame;
class EngineLinesView;
class QLabel;
namespace module {
class VariantsStepper;
}
class QToolButton;

// Tab of the opponent engine (Play page), in the manner of Fritz's modules:
// its name, its thinking, Play / Stop, number of lines, number of CPUs.
class PlayEnginePanel : public QWidget {
    Q_OBJECT
public:
    explicit PlayEnginePanel(EngineGame* game, QWidget* parent = nullptr);

    // Engine displayed: that of the game in progress, otherwise that of the next game.
    EngineConfig engine() const;
    void refresh();
    // Play / Stop button: the engine is thinking → it stops; otherwise it plays the side to move.
    void togglePlay();
    // Click on the evaluation line during its search, or Ctrl+Space: the engine plays its best move right away.
    void forceMove();
    // Opponent's strength (Elo, maximum strength or beginner): game in progress, otherwise next game.
    void setStrength(bool fullStrength, int elo, int beginnerLevel = 0, bool tablebases = true);

signals:
    void startRequested();               // Play with no game in progress
    void chooseEngineRequested();        // "New game against the engine…" (Ctrl+G)
    void engineSelected(const QString& engineId); // new opponent, without restarting the game
    void engineSettingsRequested(const QString& engineId); // click on the number of CPUs
    void titleChanged(const QString& name);
    void runningChanged(bool running); // it is thinking or not (tab icon: red or green)
    void strengthChanged(); // strength set from the panel (Play page hint to refresh)
    void spectatorRequested(const QString& engineId); // "+" button: a spectator engine watches the game

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onInfo(const EngineLine& line);
    void refreshRows();

    EngineGame* m_game;
    QMap<int, EngineLine> m_lines;
    QWidget* m_stats;     // evaluation / depth / speed line — click during the search: play now
    bool m_running = false;
    QToolButton* m_name;
    QToolButton* m_play;
    module::VariantsStepper* m_pv; // number of lines displayed (1 to 10)
    QToolButton* m_cpus;
    int m_strengthStep = -1; // level when the menu opened
    QToolButton* m_strength; // "Elo 1500" / "Max." — click: change the strength, even during a game
    QToolButton* m_book; // the engine plays the moves of the open book
    QToolButton* m_spectator; // invites a spectator engine (menu: which one)
    QLabel* m_eval;
    QLabel* m_depth;
    QLabel* m_speed;
    EngineLinesView* m_view;
    QString m_title;
};
