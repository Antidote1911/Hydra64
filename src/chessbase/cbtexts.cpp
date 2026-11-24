#include "cbtexts.h"

#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStringDecoder>

#include <cstring>

namespace {

quint32 be32(const uchar* p) { return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3]; }
quint16 le16(const uchar* p) { return quint16(p[0] | p[1] << 8); }
quint32 le32(const uchar* p) { return quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24; }

constexpr int HeaderSize = 46; // .cbh header, then one 46-byte record per game or text

// Bytes of a text → Unicode: UTF-8 if valid, otherwise Windows-1251 (Cyrillic) or 1252.
QString decode(const QByteArray& bytes)
{
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(bytes);
    if (!utf8.hasError()) return text;
    int high = 0, letters = 0;
    for (const char ch : bytes) {
        const uchar c = uchar(ch);
        if (c >= 0xC0) ++high;
        else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') ++letters;
    }
    const bool cyrillic = !bytes.contains("charset=windows-1252") && (bytes.contains("charset=windows-1251") || high > letters);
    static const char16_t cp1252[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                        0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    static const char16_t cp1251[64] = {0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
                                        0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x98, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
                                        0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
                                        0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};
    text.clear();
    text.reserve(bytes.size());
    for (const char ch : bytes) {
        const uchar c = uchar(ch);
        if (c < 0x80) text += QChar(c);
        else if (cyrillic) text += c >= 0xC0 ? QChar(0x0410 + (c - 0xC0)) : QChar(cp1251[c - 0x80]);
        else text += c < 0xA0 ? QChar(cp1252[c - 0x80]) : QChar(c);
    }
    return text;
}

// Old text (ChessBase up to version 8): carriage returns, tabs, and byte 4 in
// place of each link to a game.
QString legacyToHtml(const QByteArray& bytes)
{
    QByteArray clean = bytes;
    clean.replace('\x04', "");
    QString text = decode(clean).toHtmlEscaped();
    text.replace(QLatin1Char('\t'), QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;"));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("<br>"));
    text.replace(QLatin1Char('\r'), QStringLiteral("<br>"));
    text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return QStringLiteral("<html><body><p>%1</p></body></html>").arg(text);
}

struct Entity {
    const char* name;
    char16_t code;
};
const Entity entities[] = {
#include "html_entities.inc"
};

struct Version {
    int language;
    QByteArray bytes;
};

// A text record of the .cbg: titles per language, then texts per language.
bool parseRecord(const QByteArray& b, int language, bool& legacy, QString& title, QByteArray& body)
{
    const uchar* d = reinterpret_cast<const uchar*>(b.constData());
    const qsizetype n = b.size();
    if (n < 11) return false;
    legacy = le16(d + 4) < 3;
    const int titles = le16(d + 6);
    qsizetype p = 8;
    QList<Version> names, texts;
    for (int k = 0; k < titles; ++k) {
        if (p + 4 > n) return false;
        const int lang = le16(d + p), len = le16(d + p + 2);
        p += 4;
        if (p + len > n) return false;
        names.append({lang, b.mid(p, len)});
        p += len;
    }
    if (p + 3 > n) return false;
    const int count = le16(d + p + 1); // (the preceding byte is 0 or 1; meaning unknown)
    p += 3;
    for (int k = 0; k < count; ++k) {
        if (p + (legacy ? 4 : 6) > n) return false;
        const int lang = le16(d + p);
        const qsizetype len = legacy ? le16(d + p + 2) : qsizetype(le32(d + p + 2));
        p += legacy ? 4 : 6;
        if (len < 0 || p + len > n) return false;
        texts.append({lang, b.mid(p, len)});
        p += len;
        // Rest ignored: formatting and links of old texts, links of HTML texts
        if (p + (legacy ? 2 : 4) > n) return false;
        const qsizetype extra = legacy ? le16(d + p) : qsizetype(le32(d + p));
        p += (legacy ? 2 : 4) + extra;
        if (p > n) return false;
    }
    auto pick = [&](const QList<Version>& list, bool html) {
        auto empty = [&](const QByteArray& bytes) {
            if (!html) return bytes.trimmed().isEmpty();
            // empty ChessBase page: tags, no text
            bool inTag = false;
            for (qsizetype i = bytes.indexOf("<body") < 0 ? bytes.indexOf("<BODY") : bytes.indexOf("<body"); i >= 0 && i < bytes.size(); ++i) {
                const char c = bytes[i];
                if (c == '<') inTag = true;
                else if (c == '>') inTag = false;
                else if (!inTag && !std::strchr(" \t\r\n", c)) return false;
            }
            return true;
        };
        const Version* best = nullptr;
        for (int wanted : {language, int(cbtexts::English)})
            for (const Version& v : list)
                if (!best && v.language == wanted && !empty(v.bytes)) best = &v;
        if (!best)
            for (const Version& v : list)
                if (!empty(v.bytes) && (!best || v.bytes.size() > best->bytes.size())) best = &v;
        return best ? best->bytes : QByteArray();
    };
    title = decode(pick(names, false)).trimmed();
    body = pick(texts, !legacy);
    return true;
}

} // namespace

QString CbText::plain() const { return cbtexts::htmlToPlain(html); }

namespace cbtexts {

int interfaceLanguage()
{
    switch (QLocale().language()) {
    case QLocale::French: return French;
    case QLocale::German: return German;
    case QLocale::Spanish: return Spanish;
    case QLocale::Italian: return Italian;
    case QLocale::Dutch: return Dutch;
    default: return English;
    }
}

int count(const QString& cbhPath)
{
    QFile h(cbhPath);
    if (!h.open(QIODevice::ReadOnly)) return 0;
    const QByteArray index = h.readAll();
    int texts = 0;
    for (qsizetype at = HeaderSize; at + HeaderSize <= index.size(); at += HeaderSize) {
        const uchar flags = uchar(index[at]);
        if ((flags & 0x83) == 0x03) ++texts; // record, text, not deleted
    }
    return texts;
}

bool read(const QString& cbhPath, int language, QList<CbText>& out, QString* error)
{
    out.clear();
    QFile h(cbhPath);
    const QFileInfo info(cbhPath);
    QFile g(info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".cbg"));
    if (!h.open(QIODevice::ReadOnly) || !g.open(QIODevice::ReadOnly)) {
        if (error) *error = (h.isOpen() ? g : h).errorString();
        return false;
    }
    const QByteArray index = h.readAll();
    quint32 games = 0, record = 0;
    for (qsizetype at = HeaderSize; at + HeaderSize <= index.size(); at += HeaderSize) {
        ++record;
        const uchar* r = reinterpret_cast<const uchar*>(index.constData()) + at;
        if (!(r[0] & 1) || (r[0] & 0x80)) continue; // outside the database, or deleted
        if (!(r[0] & 2)) {
            ++games;
            continue;
        }
        uchar head[4];
        if (!g.seek(be32(r + 1)) || g.read(reinterpret_cast<char*>(head), 4) != 4) continue;
        const quint32 size = be32(head) & 0x7FFFFFFF;
        if (!(head[0] & 0x80) || size < 11 || size > (64u << 20)) continue; // not a text
        QByteArray bytes(reinterpret_cast<const char*>(head), 4);
        bytes += g.read(size - 4);
        if (quint32(bytes.size()) != size) continue;
        CbText text;
        bool legacy = false;
        QByteArray body;
        if (!parseRecord(bytes, language, legacy, text.title, body)) continue;
        text.record = record;
        text.gamesBefore = games;
        text.html = legacy ? legacyToHtml(body) : decode(body);
        out.append(std::move(text));
    }
    return true;
}

QString htmlToPlain(const QString& html)
{
    QString s = html;
    static const QRegularExpression blocks(QStringLiteral("<(head|style|script)\\b.*?</\\1\\s*>"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression breaks(QStringLiteral("<(br|/p|/div|/h[1-6]|/li|/tr|/table)\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    static const QRegularExpression spaces(QStringLiteral("[ \\t\\r\\n\\x{00A0}]+"));
    static const QRegularExpression entity(QStringLiteral("&(#[0-9]+|#[xX][0-9a-fA-F]+|[A-Za-z][A-Za-z0-9]*);"));
    s.remove(blocks);
    s.remove(comments);
    s.replace(spaces, QStringLiteral(" ")); // in HTML, a line break is just a space
    s.replace(breaks, QStringLiteral("\n"));
    s.remove(tags);
    QString out;
    out.reserve(s.size());
    qsizetype from = 0;
    for (auto it = entity.globalMatch(s); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(s).mid(from, m.capturedStart() - from);
        from = m.capturedEnd();
        const QString name = m.captured(1);
        char32_t code = 0;
        if (name.startsWith(QLatin1String("#x"), Qt::CaseInsensitive)) code = name.mid(2).toUInt(nullptr, 16);
        else if (name.startsWith(QLatin1Char('#'))) code = name.mid(1).toUInt();
        else
            for (const Entity& e : entities)
                if (name == QLatin1String(e.name)) code = e.code;
        if (code == 0xA0) out += QLatin1Char(' ');
        else if (code) out += QString::fromUcs4(&code, 1);
        else out += m.captured(0);
    }
    out += QStringView(s).mid(from);
    out.remove(QChar(0xFEFF)); // stray byte order mark in some texts
    // Cleaned lines, two line breaks at most
    QStringList lines;
    int blank = 0;
    for (const QString& raw : out.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            if (++blank > 1 || lines.isEmpty()) continue;
        } else {
            blank = 0;
        }
        lines << line;
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

} // namespace cbtexts
