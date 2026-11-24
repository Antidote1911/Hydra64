#pragma once

#include "chess/position.h"
#include "indexedsource.h"

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>
#include <vector>

class GameListModel;
class QTextBrowser;

// Opening report (after the "opening report" of SCID vs PC): for a position and the
// games of the database going through it, results, Elo, periods and popularity, next moves,
// most frequent players, best games and typical pawn structures.
struct OpeningReport {
    struct Period {
        int from = 0, to = 0;
        int games = 0, white = 0, draw = 0, black = 0;
        int baseGames = 0; // games of the starting view in the period (popularity)
    };
    struct MoveStat {
        QString san;
        int games = 0, white = 0, draw = 0, black = 0;
        double eloSum = 0;
        int eloCount = 0;
    };
    struct Player {
        QString name;
        int games = 0;
        double points = 0; // points scored with this colour
    };
    struct BestGame {
        int row = -1;
        int avgElo = 0;
        QString label; // "White – Black, result, year"
    };
    struct Structure {
        QString pawns; // 64 characters: 'P', 'p' or '.' (a8 … h1)
        int games = 0, white = 0, draw = 0, black = 0;
    };

    chess::Position position;
    QString eco, opening;
    int games = 0, white = 0, draw = 0, black = 0;
    int eloGames = 0;
    double eloSum = 0;
    int firstYear = 0, lastYear = 0;
    int sampled = 0; // games replayed (moves, structures)
    std::vector<Period> periods;
    std::vector<MoveStat> moves;
    std::vector<Player> whitePlayers, blackPlayers;
    std::vector<BestGame> best;
    std::vector<Structure> structures;
    int structurePly = 10; // structures: 10 half-moves after the position

    // `rows`: games going through the position (rows of the model); `baseline`: summaries of the
    // starting view (popularity by period). `progress(done, total)`; false = cancelled.
    static OpeningReport build(const chess::Position& pos, const GameListModel& model, const QList<int>& rows,
                               const std::vector<IndexedGameSource::Summary>& baseline, int maxSample = 5000,
                               const std::function<bool(int, int)>& progress = {});
    QString toHtml(const QString& baseName) const;
};

class OpeningReportDialog : public QDialog {
    Q_OBJECT
public:
    OpeningReportDialog(OpeningReport report, QString baseName, QWidget* parent = nullptr);
    const OpeningReport& report() const { return m_report; }
    QString html() const { return m_html; }
    bool exportHtml(const QString& path, QString* error) const;

signals:
    void gameRequested(int row);

private:
    OpeningReport m_report;
    QString m_html;
    QTextBrowser* m_view;
};
