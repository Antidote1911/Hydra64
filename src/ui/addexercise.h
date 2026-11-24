#pragma once

#include "chess/game.h"
#include "chess/position.h"

#include <QDialog>
#include <QString>
#include <QStringList>

#include <vector>

class GameListModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

// Personal exercise collections: a position and its solution, added to an EPD file, a PGN or
// a Hydra64 database that Training and the Solution test read as they are (starting
// position, first move = solution, main line = continuation, "dm N" in the starting
// comment for a mate in N).
namespace exercise {

struct Exercise {
    chess::Position pos;
    std::vector<chess::Move> line; // solution (first move) then the continuation kept
    QString name;                  // id / Event (empty: none)
    int mateIn = 0;                // mate in N (0: best move)
    QString comment;               // free text (c0)
};

chess::Game toGame(const Exercise& ex);
// N if the line ends with a mate delivered by the side to move of `pos` (the line is then
// cut at the mate), otherwise 0.
int mateLength(const chess::Position& pos, std::vector<chess::Move>& line);
QString collectionsDir();   // ~/Documents/hydra64/exercises
QStringList collections();  // collections of this folder (.epd, .pgn, .hy64, .hy64i), by name
bool isCollectionPath(const QString& path);
// Adds the exercise to the collection (created if it does not exist). `open`: the model of the Databases page if the
// file is open there (it is then updated directly), otherwise nullptr.
bool append(const QString& path, const Exercise& ex, GameListModel* open, QString* error);

} // namespace exercise

class AddExerciseDialog : public QDialog {
    Q_OBJECT
public:
    // `following`: the next moves of the notation from `pos` (the first one is the solution).
    AddExerciseDialog(const chess::Position& pos, std::vector<chess::Move> following, const QString& defaultName, QWidget* parent = nullptr);

    exercise::Exercise exercise() const;
    QString collectionPath() const;
    bool hasSolution() const { return !m_following.empty(); }

    // For the tests
    void setCollectionPath(const QString& path);
    void setKeepLine(bool on);
    void setMate(bool on);

private:
    void updateSolution();
    void chooseFile();

    chess::Position m_pos;
    std::vector<chess::Move> m_following;
    int m_detectedMate = 0;
    QComboBox* m_collection;
    QLineEdit* m_name;
    QComboBox* m_type;
    QSpinBox* m_mateIn;
    QCheckBox* m_keepLine;
    QLineEdit* m_comment;
    QLabel* m_solution;
};
