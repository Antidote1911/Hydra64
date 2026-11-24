#pragma once

#include <QList>
#include <QString>

// Texts of a ChessBase database (.cbh): introductions, lessons, tournament reports. They are
// separate records, stored among the games; they are read here directly from the
// .cbh and .cbg files (format described in docs/chessbase-texts.md). The .2cbh databases of
// ChessBase 18 are not read.
struct CbText {
    quint32 record = 0;      // number of the record in the database (1, 2, …)
    quint32 gamesBefore = 0; // (non-deleted) games placed before it
    QString title;
    QString html;            // formatted text (old texts are converted)
    QString plain() const;   // plain text, for a comment
};

namespace cbtexts {

enum Language { English = 0, German = 1, French = 2, Spanish = 3, Italian = 4, Dutch = 5 };

// Non-deleted texts of the database, in order; the `language` version of each if it exists
// and is not empty, otherwise the English one, otherwise the longest. False if the files are unreadable.
bool read(const QString& cbhPath, int language, QList<CbText>& out, QString* error = nullptr);
// Number of texts, without reading them (walk of the .cbh).
int count(const QString& cbhPath);
// Language of the texts according to the interface's.
int interfaceLanguage();
// Plain text of a simple HTML (tags removed, paragraphs separated, entities decoded).
QString htmlToPlain(const QString& html);

} // namespace cbtexts
