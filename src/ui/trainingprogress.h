#pragma once

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>


// Spaced repetition of Training (Leitner boxes): for each collection and each position,
// the box (0 to 5), the date of the next review and the number of attempts and successes.
// Success: next box, reviewed after 1, 3, 7, 14 then 30 days; failure: box 0, to review
// today. Collections are never modified: everything is in an application file
// (`<data>/training-progress.json`), indexed by the collection's path and the position's FEN.
class TrainingProgress {
public:
    struct Entry {
        int box = 0;
        QDate due;       // next review (invalid: never seen)
        int tries = 0, successes = 0;
        bool seen() const { return tries > 0; }
    };
    struct Stats {
        int total = 0;
        int due = 0;      // to review today or overdue
        int fresh = 0;    // never seen
        int learned = 0;  // box 3 or more (reviewed after a week at least)
        int tries = 0, successes = 0;
    };
    static constexpr int MaxBox = 5;
    static int intervalDays(int box); // delay after a success leading to this box

    explicit TrainingProgress(QString storePath = defaultStorePath());
    static QString defaultStorePath();

    Entry entry(const QString& collection, const QString& key) const;
    void record(const QString& collection, const QString& key, bool success, const QDate& today = QDate::currentDate());
    void reset(const QString& collection);
    bool save() const;

    // Working order: first the positions to review (overdue first, then by box),
    // then the new ones (in the given order), finally those not yet due (the
    // nearest first). Returns indices into `keys`.
    QList<int> order(const QString& collection, const QStringList& keys, const QDate& today = QDate::currentDate()) const;
    Stats stats(const QString& collection, const QStringList& keys, const QDate& today = QDate::currentDate()) const;

private:
    QString m_path;
    QJsonObject m_root; // { "collections": { path: { key: { b, d, t, s } } } }
};
