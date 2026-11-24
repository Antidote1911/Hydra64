#pragma once

#include "chess/game.h"
#include "engine/engineregistry.h"
#include "engine/uciengine.h"

#include <QColor>
#include <QMap>
#include <QWidget>

class QLabel;
namespace module {
class VariantsStepper;
}
class QSpinBox;
class QToolButton;

// Engine lines, drawn by hand (score badge + line in SAN).
class EngineLinesView : public QWidget {
    Q_OBJECT
public:
    struct Row {
        chess::Evaluation eval; // White's point of view
        QString text;           // line in SAN
        chess::Move firstMove;
        std::vector<chess::Move> moves; // full line
    };
    explicit EngineLinesView(QWidget* parent = nullptr);
    // Line of an engine (score from the point of view of the side to move) → displayed line.
    static Row rowFor(const chess::Position& pos, const EngineLine& line);
    void setRows(const QList<Row>& rows);
    void setPlaceholder(const QString& text);

    const QList<Row>& rows() const { return m_rows; }

signals:
    void movePicked(chess::Move m);
    void rowMenuRequested(int row, const QPoint& globalPos); // right-click (-1: outside the lines)

protected:
    void paintEvent(QPaintEvent*) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    static constexpr int RowHeight = 28;
    QList<Row> m_rows;
    QString m_placeholder;
    int m_hover = -1;
};

class EnginePanel : public QWidget {
    Q_OBJECT
public:
    explicit EnginePanel(QWidget* parent = nullptr);

    UciEngine* engine() const { return m_engine; }
    void setEngine(const EngineConfig& cfg);
    void setEnginePath(const QString& path); // engine outside the registry (command line)
    // Configuration used by this tab: engine settings + those of the tab (tabengine).
    const EngineConfig& engineConfig() const { return m_config; }
    // Key of the tab ("analysis", "spectator3"…), to be given before setEngine(); empty: no settings of its own.
    void setTabKey(const QString& key) { m_tabKey = key; }
    const QString& tabKey() const { return m_tabKey; }
    QString enginePath() const { return m_config.path; }

    void setPosition(const chess::Position& pos);
    int multiPv() const;
    void setMultiPv(int n);
    // First panel: its choice is remembered as the analysis engine (true by default).
    // Extra spectators manage their choice themselves.
    void setRemembersChoice(bool on) { m_remembersChoice = on; }
    // Colour of this engine's arrow on the board (recalled under its name).
    void setArrowColor(const QColor& color);
    QColor arrowColor() const { return m_arrowColor; }
    bool isAnalysing() const;
    void setAnalysing(bool on);

    // Analysed side: the engine only searches the moves of that side (waiting otherwise).
    enum Sides { BothSides = 0, WhiteOnly = 1, BlackOnly = 2 };
    Sides sides() const { return m_sides; }
    void setSides(Sides s);

    // Lock: the engine stays on its position when the board changes (SCID: "Lock engine").
    bool isLocked() const { return m_locked; }
    void setLocked(bool on);
    // Moves excluded from the search in the analysed position (UCI searchmoves).
    void excludeMove(chess::Move m);
    void clearExcluded();
    const QList<chess::Move>& excluded() const { return m_excluded; }
    const chess::Position& analysedPosition() const { return m_pos; }
    QList<std::vector<chess::Move>> lineMoves() const; // lines displayed

signals:
    void evaluationChanged(const chess::Evaluation& eval); // line 1, White's point of view
    void bestMoveChanged(chess::Move m);
    void movePicked(chess::Move m);
    void statusMessage(const QString& msg);
    void analysingChanged(bool on);
    void engineChanged(const EngineConfig& cfg);
    void engineSettingsRequested(const QString& engineId); // CPU button: settings of this tab
    void sidesChanged(EnginePanel::Sides s);
    // Variations to add to the notation, from position `pos`
    void linesInsertRequested(const chess::Position& pos, const QList<std::vector<chess::Move>>& lines);

private:
    void onInfo(const EngineLine& line);
    void refreshRows();
    void ensureStarted();
    void refreshEngineList();
    void refreshStatus(); // Play / Stop button
    void analyseCurrent(); // starts the search on m_pos, or waits (mate, stalemate, side not analysed)
    bool analysesSideToMove() const;
    void rowMenu(int row, const QPoint& at);

    UciEngine* m_engine;
    EngineConfig m_config; // engine settings + those of the tab
    QString m_tabKey;
    chess::Position m_pos;
    QMap<int, EngineLine> m_lines;
    class QTimer* m_refresh = nullptr; // groups the "info" lines received in bursts
    void flushInfo();

    // Same presentation as the engine tab of the Play page
    QToolButton* m_name;    // engine name; menu to change it
    QToolButton* m_toggle;  // Play / Stop
    bool m_on = false;      // analysis in progress
    QColor m_arrowColor;
    module::VariantsStepper* m_pv; // number of lines (1 to 10)
    QToolButton* m_cpus;    // processors (Threads); click: engine settings
    QToolButton* m_sidesButton; // analysed side (white, black or two-tone dot)
    Sides m_sides = BothSides;
    QLabel* m_eval;
    QLabel* m_depth;
    QLabel* m_speed;
    bool m_remembersChoice = true;
    QToolButton* m_lock;
    bool m_locked = false;
    chess::Position m_boardPos; // board position (differs from m_pos when locked)
    QList<chess::Move> m_excluded;
    EngineLinesView* m_view;
};
