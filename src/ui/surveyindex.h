#pragma once

#include <QList>
#include <QString>

#include <functional>

// Index of the opening surveys that accompany a game database (ChessBase Opening Encyclopaedia:
// one main database plus one small database per survey). The index is a JSON file next to the
// main database, `<database name>.surveys.json`; the surveys stay ordinary .hy64 databases, listed
// with paths relative to the index. Built by `hydra64-convert --survey-index`.
namespace surveyindex {

struct Survey {
    QString file;   // absolute path of the survey database
    QString eco;    // first ECO code ("C55"), empty for a survey without code
    QString ecoTo;  // last code of a range ("C56"), empty otherwise
    QString date;   // "2019-03" (year and issue), empty if unknown
    QString title;
    QString author; // most frequent annotator of the survey
    int games = 0;  // entries of the database, the survey text included

    QString ecoLabel() const { return ecoTo.isEmpty() ? eco : eco + QChar(0x2013) + ecoTo; }
};

// Index file written for database `databasePath`.
QString pathFor(const QString& databasePath);
// Index that concerns database `databasePath`: its own, otherwise the one of its folder, otherwise
// the one of the parent folder (the database is one of the surveys). Empty if none.
QString find(const QString& databasePath);
// Surveys of index file `indexPath`, sorted by ECO code then date (those without code last).
QList<Survey> load(const QString& indexPath, QString* error = nullptr);
// Reads every database (.hy64, .hy64i) of `surveysDir` and writes the index of `databasePath`. Returns the number of
// surveys, -1 on error. `progress(done, total)`.
int build(const QString& databasePath, const QString& surveysDir, QString* error, const std::function<void(int, int)>& progress = {});

} // namespace surveyindex
