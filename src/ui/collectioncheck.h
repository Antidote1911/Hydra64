#pragma once

#include "solutiontest.h"

#include <QDialog>
#include <QList>
#include <QString>

#include <optional>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class UciEngine;
struct EngineLine;

// Check of an exercise collection by Stockfish: wrong solution, ambiguous exercise (another
// move is worth as much), move to avoid played by the engine, announced mate (dm) that is not forced.
namespace collectioncheck {

// One engine line, from the point of view of the side to move.
struct Line {
    QString move; // first move, in UCI
    int score = 0;
    bool mate = false; // score = moves to mate (negative: being mated)
};
int value(const Line& l); // centipawns, a mate counts ±(30000 − moves)
QString scoreText(const Line& l); // "+1.25", "mat en 3", "mat subi en 2"

enum class Verdict { Ok, Ambiguous, Doubtful, Avoided, MateNotForced, MateShorter };
struct Result {
    Verdict verdict = Verdict::Ok;
    QString detail;
};
constexpr int AmbiguityMarginCp = 60; // another move within 0.6 pawn: ambiguous exercise
constexpr int WinningCp = 400;        // two moves that clearly win: ambiguous too

// `top`: the best lines of the free search (MultiPV, the best first);
// `solution`: the best line among the solution's moves (search limited to those moves),
// useful when the engine prefers another move.
Result judge(const TestPosition& p, const QList<Line>& top, const std::optional<Line>& solution);
QString verdictText(Verdict v);
bool needsSolutionSearch(const TestPosition& p, const QList<Line>& top);

} // namespace collectioncheck

class CollectionCheckDialog : public QDialog {
    Q_OBJECT
public:
    explicit CollectionCheckDialog(QWidget* parent = nullptr);
    ~CollectionCheckDialog() override;

    void setCollection(const QString& path);
    bool start(const QString& path, int moveTimeMs, QString* error);
    void stop();
    bool isRunning() const { return m_running; }
    const QList<collectioncheck::Result>& results() const { return m_results; }
    const QList<TestPosition>& positions() const { return m_positions; }

signals:
    void analyseRequested(const QString& fen);
    void finished();

private:
    void searchNext();
    void onBestMove(const QString& fen);
    void addRow(int index, const collectioncheck::Result& r);
    void finish(const QString& text);

    QLineEdit* m_file;
    QSpinBox* m_time;
    QPushButton* m_startButton;
    QLabel* m_summary;
    QTableWidget* m_table;
    UciEngine* m_engine = nullptr;
    bool m_engineReady = false;
    bool m_running = false;
    QList<TestPosition> m_positions;
    QList<collectioncheck::Result> m_results;
    int m_index = -1;
    int m_moveTime = 1000;
    bool m_restricted = false; // search limited to the moves of the current solution
    QList<collectioncheck::Line> m_top;
    QList<collectioncheck::Line> m_lines; // lines received for the current search (per MultiPV)
    QString m_fen;
};
