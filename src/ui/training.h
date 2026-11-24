#pragma once

#include "chess/position.h"
#include "solutiontest.h"
#include "trainingprogress.h"

#include <QHash>
#include <memory>

#include <QDialog>

#include <functional>

class BoardWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class UciEngine;
struct EngineLine;

// Training (after the trainings of SCID vs PC):
// - opening: the open opening tree replies to your moves, leaving the tree is reported;
// - best move: positions of a collection (EPD bm/am, PGN, Hydra64), you play, the solution
//   is checked;
// - mates in N: you must mate; Stockfish defends as well as possible and checks that the mate is still there.
class TrainingDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { Opening = 0, BestMove = 1, Mate = 2 };
    // Opening tree: known moves of a position, and move drawn at random (weighted, without the
    // moves to avoid); name of the tree.
    struct Book {
        std::function<std::vector<chess::Move>(const chess::Position&)> known;
        std::function<chess::Move(const chess::Position&)> pick;
        std::function<QString()> name;
    };
    TrainingDialog(Book book, QWidget* parent = nullptr);
    ~TrainingDialog() override;

    // Collections: path, displayed name and, for the built-in databases, the category (Event field:
    // "Mat en 2", "Meilleur coup, facile"…) that filters the positions.
    struct Collection {
        QString path, name, category{};
    };
    // Built-in databases (Lichess puzzles, public domain): databases/training/best_moves/
    // (best_move_easy, _medium, _hard) and databases/training/mates/ (mate_in_1 … mate_in_5), next to
    // the executable.
    static QString builtInDir();
    static QList<Collection> builtInCollections(Mode mode);
    // Your collections: EPD, PGN and Hydra64 files of ~/Documents/hydra64/exercises
    static QList<Collection> userCollections();

    // For the tests and the dialog. `shuffle`: positions in an order drawn at random.
    void startOpening(chess::Color side);
    bool startPositions(Mode mode, const QString& path, QString* error, bool shuffle = false, const QString& category = QString());
    void playMove(chess::Move m); // player's move (as on the board)
    void nextPosition();          // "Next"
    QString status() const;
    int solved() const { return m_solved; }
    int tried() const { return m_tried; }
    bool isWaiting() const { return m_waiting; } // the engine or the tree is going to reply
    const chess::Position& position() const { return m_pos; }
    int current() const { return m_index; }
    int positionCount() const { return int(m_positions.size()); }
    const TestPosition& positionAt(int i) const { return m_positions[i]; }
    TrainingProgress& progress() { return *m_progress; }
    void setSpacedRepetition(bool on);
    static constexpr int RequeueGap = 4; // a missed position comes back 4 positions later

signals:
    void analyseRequested(const QString& fen);
    void checkRequested(const QString& collection); // "Check…": check the chosen collection

protected:
    void showEvent(QShowEvent* e) override;

private:
    void refreshCollections();
    static QString positionKey(const TestPosition& t);
    static QString collectionId(const Collection& c); // data of the "Collection" list
    static void splitCollectionId(const QString& id, QString& path, QString& category);
    TrainingProgress::Stats progressStats() const;
    void updateFileRow();
    void showPosition(chess::Move last = {});
    void setStatus(const QString& text);
    void engineSearch(const chess::Position& pos, int ms);
    void onEngineResult(const EngineLine& line, const QString& best);
    void finishExercise(bool success, const QString& text);
    QString bookName() const { return m_book.name ? m_book.name() : QString(); }

    Book m_book;
    Mode m_mode = Opening;
    chess::Color m_side = chess::White;
    chess::Position m_pos;
    chess::Position m_startPos; // starting position of the exercise
    QList<TestPosition> m_positions;
    int m_index = -1;
    int m_solved = 0, m_tried = 0;
    bool m_done = false;     // exercise over (right or wrong)
    bool m_waiting = false;
    int m_mateIn = 0;        // mates: moves left to mate
    int m_movesPlayed = 0;
    BoardWidget* m_board;
    QComboBox* m_modeBox;
    QComboBox* m_sideBox;
    QComboBox* m_collection; // built-in collections of the mode, then "Other file…" (empty data)
    QLineEdit* m_file;
    QPushButton* m_browse;
    QPushButton* m_start;
    QPushButton* m_next;
    QPushButton* m_solution;
    QPushButton* m_analyse;
    QLabel* m_status;
    QLabel* m_score;
    UciEngine* m_engine = nullptr;
    bool m_engineReady = false;
    QString m_engineFen;
    std::function<void()> m_pendingSearch;
    EngineLine* m_line;
    QCheckBox* m_spaced;
    QPushButton* m_resetProgress;
    QPushButton* m_check;
    std::unique_ptr<TrainingProgress> m_progress;
    QString m_collectionPath;     // current collection (key of the progress)
    bool m_spacedActive = false;  // spaced repetition for the current session
    QHash<QString, int> m_requeued; // missed positions put back further on in the session
};
