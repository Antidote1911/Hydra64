#pragma once

#include "chess/game.h"
#include "engine/engineregistry.h"

#include <QDialog>
#include <QList>
#include <QObject>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class UciEngine;
struct EngineLine;

// Automatic annotation of a game (in the manner of SCID vs PC, "Annotate"): the engine
// analyses each position of the main line; a move that loses winning chances
// gets ?!, ? or ?? and the engine's best continuation as a variation, with its evaluation.
// The thresholds bear on winning chances (like Lichess): losing 1 pawn at equality counts,
// not when you are already 8 pawns up.
namespace annotation {

struct Options {
    int movetimeMs = 1000;
    enum Side { Both, WhiteOnly, BlackOnly } side = Both;
    bool inaccuracies = true; // ?! in addition to ? and ??
    bool missedMates = true;  // missed (or longer) mate reported
    bool variations = true;   // best continuation as a variation
    int variationPlies = 8;   // length of the variation (half-moves)
    int skipMoves = 0;        // first moves ignored (opening)
    bool bookMarker = true;   // "Last book move" according to the open opening tree
    int bookPlies = 0;        // half-moves of the main line in the book (computed by the caller)
    QString bookName;
    // Tactical exercises (like "Mark tactical exercises" of SCID vs PC): the position
    // following a blunder, when the side to move has a clear win, is marked "Exercice" and stored
    // in Summary::exercises (EPD, bm = engine's move).
    bool markExercises = false;
    QString exercisesFile; // EPD collection added to (empty: none)
};

// Analysis of a position (side to move)
struct PositionEval {
    bool valid = false;
    bool mate = false;
    int score = 0; // centipawns, or moves to mate (signed), from the point of view of the side to move
    int depth = 0;
    QStringList pv; // UCI moves
};

// Winning chances, from -1 to 1, from the point of view of the side to move.
double winningChances(const PositionEval& e);
// Readable evaluation from White's point of view: "+1.25", "−0.40", "#3", "#−2".
QString formatWhite(const PositionEval& e, chess::Color toMove);

struct Summary {
    int inaccuracies = 0, mistakes = 0, blunders = 0, missedMates = 0;
    QStringList exercises; // EPD lines
    QString text() const;
};
// Adds the exercises to the EPD collection (created if needed); false if the file cannot be accessed.
bool appendExercises(const QString& path, const QStringList& epd, QString* error = nullptr);
QString defaultExercisesFile(); // ~/Documents/hydra64/exercises/exercises.epd

// Adds to `game` the annotations from the evaluations of the positions of the main
// line (evals[0] = starting position, evals[i] = after the i-th move). The evaluations
// are also stored in the nodes (graph). Annotator header = `annotator`.
Summary apply(chess::Game& game, const QList<PositionEval>& evals, const Options& options, const QString& annotator);

} // namespace annotation

// Has the engine analyse the positions of the main line, one by one.
class Annotator : public QObject {
    Q_OBJECT
public:
    explicit Annotator(QObject* parent = nullptr);
    ~Annotator() override;

    // `fens`: positions of the main line (start included).
    bool start(const EngineConfig& engine, const QStringList& fens, int movetimeMs, QString* error);
    void cancel();
    bool isRunning() const { return m_engine != nullptr; }
    const QList<annotation::PositionEval>& evaluations() const { return m_evals; }
    QString engineName() const { return m_name; }

signals:
    void progress(int done, int total);
    void finished(bool completed);
    void errorOccurred(const QString& message);

private:
    void next();
    void onInfo(const EngineLine& line);
    void onBestMove(const QString& uci, const QString& fen);
    void shutdown();

    UciEngine* m_engine = nullptr;
    QString m_name;
    QStringList m_fens;
    QList<annotation::PositionEval> m_evals;
    int m_current = -1;
    int m_movetime = 1000;
};

// Game → Annotate game…: engine, time per move, side, what is added.
class AnnotateDialog : public QDialog {
    Q_OBJECT
public:
    explicit AnnotateDialog(QWidget* parent = nullptr);
    annotation::Options options() const;
    EngineConfig engine() const;

private:
    QComboBox* m_engine;
    QDoubleSpinBox* m_time;
    QComboBox* m_side;
    QCheckBox* m_inaccuracies;
    QCheckBox* m_missedMates;
    QCheckBox* m_variations;
    QSpinBox* m_plies;
    QSpinBox* m_skip;
    QCheckBox* m_book;
    QCheckBox* m_exercises;
    class QLineEdit* m_exercisesFile;
};
