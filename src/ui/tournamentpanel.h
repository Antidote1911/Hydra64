#pragma once

#include "tournament.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QTabWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QToolButton;

// "Engine tournament" page: participants, time control, standings and games.
class TournamentPanel : public QWidget {
    Q_OBJECT
public:
    explicit TournamentPanel(Tournament* tournament, QWidget* parent = nullptr);

    void newTournament();   // creates the tournament with the displayed settings and starts it
    void toggleRunning();   // start / resume or interrupt
    void exportGames();     // all the games played into a PGN file, or as an HTML page depending on the chosen type
    void exportHtml();      // standings, crosstable and replayable games in a standalone HTML page
    bool writeHtml(const QString& path, QString* error) const;

    // Participants of the next tournament (engine + settings specific to the tournament).
    const QList<Tournament::Participant>& participants() const { return m_participants; }
    void addParticipant(Tournament::Participant p); // name made unique
    void replaceParticipant(int row, Tournament::Participant p);
    void removeParticipant(int row);

signals:
    void gameViewRequested(int index); // double-click on a game
    void overviewRequested(bool on);   // "Overview" button
    void databaseRequested(const QString& path); // open the tournament database (Databases page)
    void statusMessage(const QString& text);

private:
    void refreshEngines();  // list of participants (engines available or not)
    void updateSummary();
    void saveParticipants() const;
    void editParticipant(int row); // configuration dialog; row < 0: new
    QString uniqueName(QString name, int except) const;
    void refreshSettings(); // settings of the existing tournament → form
    void refresh();         // standings, games, buttons
    void chooseOpenings();

    Tournament* m_tournament;
    QList<Tournament::Participant> m_participants;
    QListWidget* m_engines;
    QToolButton* m_addEngine;
    QToolButton* m_removeEngine;
    QToolButton* m_editEngine;
    QDoubleSpinBox* m_minutes;
    QDoubleSpinBox* m_increment;
    QSpinBox* m_cycles;
    QCheckBox* m_double;
    QComboBox* m_openingMode; // initial position, starting positions (file), engine book
    QLineEdit* m_openingsPath;
    QPushButton* m_openingsBrowse;
    QCheckBox* m_openingsRandom;
    QLineEdit* m_bookPath;
    QPushButton* m_bookBrowse;
    QSpinBox* m_bookMoves;
    enum OpeningMode { InitialPosition = 0, StartPositions = 1, EngineBook = 2 };
    void updateOpenings();
    void chooseBook();
    QLineEdit* m_pgnDir;
    QPushButton* m_pgnBrowse;
    QPushButton* m_openPgn;
    QLabel* m_summary;
    QPushButton* m_new;
    QPushButton* m_run;
    QPushButton* m_export;
    QLabel* m_status;
    QTableWidget* m_standings;
    QTableWidget* m_cross;
    QTableWidget* m_games;
    QComboBox* m_schedule;
    QComboBox* m_limit;
    QDoubleSpinBox* m_moveTime;
    QSpinBox* m_depth;
    QPushButton* m_options;
    QPushButton* m_overview; // all the boards of the simultaneous games

public:
    void setOverviewChecked(bool on);

private:
    void editOptions(); // automatic adjudication, engine comments
    void updateLimit();
};
