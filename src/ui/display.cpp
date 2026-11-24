#include "display.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

namespace display {

QString san(const std::string& s) { return san(s, prefs::notation()); }

QString san(const std::string& s, prefs::Notation notation)
{
    QString out = QString::fromStdString(s);
    if (notation == prefs::Notation::English) return out;
    for (QChar& c : out) {
        const char16_t u = c.unicode();
        if (notation == prefs::Notation::French) {
            switch (u) {
            case 'K': c = QLatin1Char('R'); break; // King
            case 'Q': c = QLatin1Char('D'); break; // Queen
            case 'R': c = QLatin1Char('T'); break; // Rook
            case 'B': c = QLatin1Char('F'); break; // Bishop
            case 'N': c = QLatin1Char('C'); break; // Knight
            default: break;                        // files, x, +, #, =, O (castling) unchanged
            }
        } else { // solid figurines ♚♛♜♝♞: far more readable than outlines on a dark background
            switch (u) {
            case 'K': c = QChar(0x265A); break;
            case 'Q': c = QChar(0x265B); break;
            case 'R': c = QChar(0x265C); break;
            case 'B': c = QChar(0x265D); break;
            case 'N': c = QChar(0x265E); break;
            default: break;
            }
        }
    }
    return out;
}

QString endReason(int kind)
{
    switch (kind) {
    case 0: return QCoreApplication::translate("display", "Échec et mat");
    case 1: return QCoreApplication::translate("display", "Pat");
    case 2: return QCoreApplication::translate("display", "Matériel insuffisant");
    case 3: return QCoreApplication::translate("display", "Règle des 50 coups");
    case 4: return QCoreApplication::translate("display", "Triple répétition");
    default: return {};
    }
}

namespace {
bool english() { return prefs::language() == QLatin1String("en"); }
} // namespace

namespace {

struct Parts {
    QString year, month, day; // empty if unknown
};

Parts split(const std::string& pgnDate)
{
    const QStringList p = QString::fromStdString(pgnDate).split('.');
    auto known = [](const QString& v) { return !v.isEmpty() && !v.contains('?'); };
    Parts r;
    if (known(p.value(0))) r.year = p.value(0);
    if (known(p.value(1))) r.month = p.value(1);
    if (known(p.value(2))) r.day = p.value(2);
    return r;
}

} // namespace

QString date(const std::string& pgnDate)
{
    const Parts p = split(pgnDate);
    if (p.year.isEmpty()) return QStringLiteral("?");
    if (p.month.isEmpty()) return p.year;
    if (english()) return p.day.isEmpty() ? p.year + '.' + p.month : p.year + '.' + p.month + '.' + p.day;
    if (p.day.isEmpty()) return p.month + '/' + p.year;
    return p.day + '/' + p.month + '/' + p.year;
}

QString dateForEditing(const std::string& pgnDate)
{
    const Parts p = split(pgnDate);
    if (p.year.isEmpty() && p.month.isEmpty() && p.day.isEmpty()) return {};
    auto or_ = [](const QString& v, const char* unknown) { return v.isEmpty() ? QString::fromLatin1(unknown) : v; };
    if (english()) return or_(p.year, "????") + '.' + or_(p.month, "??") + '.' + or_(p.day, "??");
    return or_(p.day, "??") + '/' + or_(p.month, "??") + '/' + or_(p.year, "????");
}

QString datePlaceholder() { return english() ? QStringLiteral("yyyy.mm.dd") : QStringLiteral("jj/mm/aaaa"); }

std::string pgnDate(const QString& text)
{
    QString day, month, year;
    if (english()) { // yyyy.mm.dd, yyyy.mm, yyyy
        const QStringList p = text.trimmed().split(QRegularExpression(QStringLiteral("[./-]")), Qt::KeepEmptyParts);
        year = p.value(0);
        month = p.value(1);
        day = p.value(2);
    } else { // dd/mm/yyyy, mm/yyyy, yyyy
        const QStringList p = text.trimmed().split('/', Qt::KeepEmptyParts);
        year = p.value(p.size() - 1);
        if (p.size() >= 2) month = p.value(p.size() - 2);
        if (p.size() >= 3) day = p.value(0);
    }
    auto norm = [](QString v, int width) {
        v = v.trimmed();
        if (v.isEmpty() || v.contains('?')) return QString(width, '?');
        return v.rightJustified(width, '0');
    };
    const QString y = norm(year, 4), m = norm(month, 2), d = norm(day, 2);
    if (y.contains('?') && m.contains('?') && d.contains('?')) return {};
    return (y + '.' + m + '.' + d).toStdString();
}

} // namespace display

QString display::comment(const QString& text) { return comment(text, prefs::notation()); }

namespace {
// Private-use characters (U+E000…U+E02F) that ChessBase writes in its comments for its symbol
// font: figurines and Informant signs. Without that font they display any old
// way. Table taken from cbvault (codepage.rs); the four signs missing from common
// fonts are rendered by neighbouring signs.
QString symbolText(char16_t c, prefs::Notation notation)
{
    auto piece = [&](char letter, char16_t figurine) {
        if (notation == prefs::Notation::Figurine) return QString(QChar(figurine));
        if (letter == 'P') return QStringLiteral("P");
        QString moved = display::san(std::string(1, letter) + "a1", notation); // « Na1 » → « Ca1 »
        moved.chop(2);
        return moved;
    };
    switch (c) {
    case 0xE024: return piece('K', 0x2654);
    case 0xE025: return piece('Q', 0x2655);
    case 0xE026: return piece('R', 0x2656);
    case 0xE027: return piece('B', 0x2657);
    case 0xE028: return piece('N', 0x2658);
    case 0xE029: return piece('P', 0x2659);
    case 0xE000: return QStringLiteral("=/∞"); // with compensation
    case 0xE001: return QStringLiteral("♗♗");  // bishop pair
    case 0xE002: return QStringLiteral("♗♝");  // opposite-coloured bishops
    case 0xE004: case 0xE02E: return QStringLiteral("⩱");
    case 0xE005: return QString();              // diagram marker
    case 0xE007: case 0xE02F: return QStringLiteral("⩲");
    case 0xE008: return QStringLiteral("♙↑");  // passed pawn
    case 0xE009: return QStringLiteral("∞");
    case 0xE00A: return QStringLiteral("±");
    case 0xE00C: return QStringLiteral("⌓");   // better is
    case 0xE00D: return QStringLiteral("△");   // with the idea
    case 0xE00E: return QStringLiteral("«");    // queenside
    case 0xE00F: return QStringLiteral("»");    // kingside
    case 0xE010: return QStringLiteral("×");    // weak point
    case 0xE012: return QStringLiteral("↑");    // initiative
    case 0xE013: return QStringLiteral("→");    // attack
    case 0xE017: return QStringLiteral("⇆");    // counterplay
    case 0xE018: return QStringLiteral("⇔");    // file
    case 0xE019: return QStringLiteral("⇗");    // diagonal
    case 0xE01A: return QStringLiteral("∓");
    case 0xE01D: return QStringLiteral("⊙");    // zugzwang
    case 0xE01E: return QStringLiteral("⊞");    // centre
    case 0xE01F: return QStringLiteral("⊥");    // endgame
    case 0xE020: return QStringLiteral("¹");
    case 0xE021: return QStringLiteral("□");    // only move
    case 0xE023: return QStringLiteral("○");    // space
    case 0xE02A: return QStringLiteral("•");
    case 0xE02D: return QStringLiteral("‰");
    case 0xE003: return QStringLiteral("¯");
    case 0xE01B: return QStringLiteral("ª");
    case 0xE01C: return QStringLiteral("“");
    default: return QString(QChar(c));
    }
}

QString withSymbols(const QString& text, prefs::Notation notation)
{
    bool any = false;
    for (const QChar c : text) any = any || (c.unicode() >= 0xE000 && c.unicode() <= 0xE02F);
    if (!any) return text;
    QString out;
    for (const QChar c : text) out += c.unicode() >= 0xE000 && c.unicode() <= 0xE02F ? symbolText(c.unicode(), notation) : QString(c);
    return out;
}
} // namespace

QString display::comment(const QString& text, prefs::Notation notation)
{
    if (text.isEmpty()) return text;
    if (notation == prefs::Notation::English) return withSymbols(text, notation);
    // A piece move (Qh5, Nbd7, R1e2, Bxf7+, Qd1-h5) or a promotion (e8=Q, exd8=N#). A pawn
    // without promotion has no letter to translate. It must be a whole word: no letter or
    // digit stuck before or after ("B2B", "Nf3s" are not moves).
    static const QRegularExpression move(QStringLiteral(
        "(?<![A-Za-z0-9])(?:[KQRBN][a-h]?[1-8]?[x-]?[a-h][1-8]|(?:[a-h]x)?[a-h][18]=[QRBN])(?![A-Za-z0-9=])"));
    QString out;
    qsizetype from = 0;
    for (auto it = move.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(text).mid(from, m.capturedStart() - from);
        out += san(m.captured(0).toStdString(), notation);
        from = m.capturedEnd();
    }
    out += QStringView(text).mid(from);
    return withSymbols(out, notation); // after the moves: "R" (king) in French must not be reread as a rook
}
