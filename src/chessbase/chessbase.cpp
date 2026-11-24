#include "chessbase.h"
#include <zlib.h>
#include <QFile>
#include <QDir>

#include "chess/pgn.h"
#include <cstdlib>
#include <cstring>
#include "db/playerbase.h"
#include <cstdio>
#include <iterator>

#include <QFileInfo>

#include <algorithm>

#ifdef HYDRA64_CHESSBASE
#include "chessbase/bridge/cbbridge.h"
#endif

namespace {

QString suffix(const QString& path) { return QFileInfo(path).suffix().toLower(); }

#ifdef HYDRA64_CHESSBASE
CbbDb* db(void* p) { return static_cast<CbbDb*>(p); }
#endif

} // namespace

bool ChessBaseSource::available()
{
#ifdef HYDRA64_CHESSBASE
    return true;
#else
    return false;
#endif
}

bool ChessBaseSource::isDatabasePath(const QString& path)
{
    const QString s = suffix(path);
    return s == QLatin1String("cbh") || s == QLatin1String("2cbh");
}

bool ChessBaseSource::isArchivePath(const QString& path) { return suffix(path) == QLatin1String("cbv") || suffix(path) == QLatin1String("2cbv"); }

// .2cbv archive of ChessBase 18: a ZIP file (ZIP64 included). We read the central directory
// at the end of the file, then each entry: stored as is (method 0) or compressed with
// "deflate" (method 8, decompressed with zlib).
bool ChessBaseSource::extractZip(const QString& archive, const QString& dir, QString* error)
{
    auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    QFile f(archive);
    if (!f.open(QIODevice::ReadOnly)) return fail(f.errorString());
    const qint64 size = f.size();
    const uchar* d = f.map(0, size);
    if (!d || size < 22) return fail(QStringLiteral("archive ZIP illisible"));
    auto u16 = [&](qint64 at) { return quint32(d[at]) | quint32(d[at + 1]) << 8; };
    auto u32 = [&](qint64 at) { return quint32(d[at]) | quint32(d[at + 1]) << 8 | quint32(d[at + 2]) << 16 | quint32(d[at + 3]) << 24; };
    auto u64 = [&](qint64 at) { return quint64(u32(at)) | quint64(u32(at + 4)) << 32; };
    // End of central directory: searched from the end (it may be followed by a comment)
    qint64 end = -1;
    for (qint64 at = size - 22; at >= 0 && at >= size - 22 - 65535; --at)
        if (u32(at) == 0x06054b50) {
            end = at;
            break;
        }
    if (end < 0) return fail(QStringLiteral("ce n'est pas une archive ZIP"));
    quint64 entries = u16(end + 10), dirOffset = u32(end + 16);
    if (end >= 20 && u32(end - 20) == 0x07064b50) { // ZIP64: the real directory is pointed to by this locator
        const quint64 z = u64(end - 20 + 8);
        if (z + 56 <= quint64(size) && u32(qint64(z)) == 0x06064b50) {
            entries = u64(qint64(z) + 32);
            dirOffset = u64(qint64(z) + 48);
        }
    }
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("impossible de créer %1").arg(dir));
    quint64 at = dirOffset;
    int written = 0;
    for (quint64 i = 0; i < entries; ++i) {
        if (at + 46 > quint64(size) || u32(qint64(at)) != 0x02014b50) return fail(QStringLiteral("répertoire de l'archive ZIP endommagé"));
        const quint32 method = u16(qint64(at) + 10), nameLen = u16(qint64(at) + 28), extraLen = u16(qint64(at) + 30), commentLen = u16(qint64(at) + 32);
        quint64 packed = u32(qint64(at) + 20), unpacked = u32(qint64(at) + 24), local = u32(qint64(at) + 42);
        if (at + 46 + nameLen + extraLen > quint64(size)) return fail(QStringLiteral("répertoire de l'archive ZIP endommagé"));
        const QString name = QString::fromUtf8(reinterpret_cast<const char*>(d + at + 46), int(nameLen));
        // ZIP64 field (0x0001): sizes and offset when they exceed 32 bits
        for (quint64 x = at + 46 + nameLen, xe = x + extraLen; x + 4 <= xe;) {
            const quint32 id = u16(qint64(x)), len = u16(qint64(x) + 2);
            if (id == 1) {
                quint64 p = x + 4;
                if (unpacked == 0xFFFFFFFFu && p + 8 <= xe) unpacked = u64(qint64(p)), p += 8;
                if (packed == 0xFFFFFFFFu && p + 8 <= xe) packed = u64(qint64(p)), p += 8;
                if (local == 0xFFFFFFFFu && p + 8 <= xe) local = u64(qint64(p));
            }
            x += 4 + len;
        }
        at += 46 + nameLen + extraLen + commentLen;
        const QString fileName = QFileInfo(QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName(); // never a path
        if (fileName.isEmpty() || name.endsWith(QLatin1Char('/'))) continue; // folder
        if (local + 30 > quint64(size) || u32(qint64(local)) != 0x04034b50) return fail(QStringLiteral("entrée « %1 » endommagée").arg(fileName));
        const quint64 data = local + 30 + u16(qint64(local) + 26) + u16(qint64(local) + 28);
        if (data + packed > quint64(size) || unpacked > (quint64(1) << 32)) return fail(QStringLiteral("entrée « %1 » endommagée").arg(fileName));
        QByteArray out;
        if (method == 0) {
            out = QByteArray(reinterpret_cast<const char*>(d + data), qsizetype(packed));
        } else if (method == 8) {
            out.resize(qsizetype(unpacked));
            z_stream z{};
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return fail(QStringLiteral("zlib indisponible"));
            z.next_in = const_cast<Bytef*>(d + data);
            z.avail_in = uInt(packed);
            z.next_out = reinterpret_cast<Bytef*>(out.data());
            z.avail_out = uInt(out.size());
            const int rc = inflate(&z, Z_FINISH);
            const bool complete = rc == Z_STREAM_END && z.total_out == unpacked;
            inflateEnd(&z);
            if (!complete) return fail(QStringLiteral("entrée « %1 » : décompression impossible").arg(fileName));
        } else {
            return fail(QStringLiteral("entrée « %1 » : méthode de compression %2 non gérée").arg(fileName).arg(method));
        }
        QFile o(QDir(dir).filePath(fileName));
        if (!o.open(QIODevice::WriteOnly | QIODevice::Truncate) || o.write(out) != out.size()) return fail(o.errorString());
        ++written;
    }
    if (written == 0) return fail(QStringLiteral("archive ZIP vide"));
    return true;
}

ChessBaseSource::Ids::~Ids()
{
#ifdef HYDRA64_CHESSBASE
    cbb_free_ids(static_cast<CbbIds*>(m_ids));
#endif
}

ChessBaseSource::Ids& ChessBaseSource::Ids::operator=(Ids&& other) noexcept
{
    if (this != &other) {
#ifdef HYDRA64_CHESSBASE
        cbb_free_ids(static_cast<CbbIds*>(m_ids));
#endif
        m_ids = std::exchange(other.m_ids, nullptr);
    }
    return *this;
}

int ChessBaseSource::Ids::count() const
{
#ifdef HYDRA64_CHESSBASE
    return int(cbb_ids_count(static_cast<const CbbIds*>(m_ids)));
#else
    return 0;
#endif
}

ChessBaseSource::~ChessBaseSource()
{
#ifdef HYDRA64_CHESSBASE
    cbb_close(db(m_db));
#endif
}

#ifdef HYDRA64_CHESSBASE

bool ChessBaseSource::extractArchive(const QString& archive, const QString& dir, QString* error)
{
    {
        QFile head(archive); // .2cbv (ChessBase 18): a ZIP
        if (head.open(QIODevice::ReadOnly) && head.read(4) == QByteArray("PK\x03\x04", 4)) return extractZip(archive, dir, error);
    }
    char err[512] = "";
    const bool ok = cbb_extract_archive(archive.toUtf8().constData(), dir.toUtf8().constData(), err, sizeof err) == 1;
    if (!ok && error) *error = QString::fromUtf8(err);
    return ok;
}

bool ChessBaseSource::open(const QString& path, QString* error)
{
    char err[512] = "";
    CbbDb* d = cbb_open(path.toUtf8().constData(), err, sizeof err);
    if (!d) {
        if (error) *error = QString::fromUtf8(err);
        return false;
    }
    cbb_close(db(m_db));
    m_db = d;
    m_filtered = false;
    std::fill(std::begin(m_cacheRow), std::end(m_cacheRow), -1);
    return true;
}

bool ChessBaseSource::isTwoCbh() const { return cbb_is_twocbh(db(m_db)) == 1; }
int ChessBaseSource::total() const { return int(cbb_total(db(m_db))); }
int ChessBaseSource::count() const { return int(cbb_count(db(m_db))); }

bool ChessBaseSource::header(int row, Header& out) const
{
    if (!m_db || row < 0) return false;
    const int slot = row % 256;
    if (m_cacheRow[slot] == row) {
        out = m_cache[slot];
        return true;
    }
    CbbHeader h{};
    if (!cbb_header(db(m_db), quint32(row), &h)) return false;
    Header& c = m_cache[slot];
    c.id = h.id;
    c.white = QString::fromUtf8(h.white);
    c.black = QString::fromUtf8(h.black);
    c.event = QString::fromUtf8(h.event);
    c.date = QString::fromUtf8(h.date);
    c.result = QString::fromUtf8(h.result);
    c.eco = QString::fromUtf8(h.eco);
    c.whiteElo = h.white_elo;
    c.blackElo = h.black_elo;
    m_cacheRow[slot] = row;
    out = c;
    return true;
}

bool ChessBaseSource::headerDirect(int row, Header& out) const
{
    if (!m_db || row < 0) return false;
    CbbHeader h{};
    if (!cbb_header(db(m_db), quint32(row), &h)) return false;
    out.id = h.id;
    out.white = QString::fromUtf8(h.white);
    out.black = QString::fromUtf8(h.black);
    out.event = QString::fromUtf8(h.event);
    out.date = QString::fromUtf8(h.date);
    out.result = QString::fromUtf8(h.result);
    out.eco = QString::fromUtf8(h.eco);
    out.whiteElo = h.white_elo;
    out.blackElo = h.black_elo;
    return true;
}

namespace {
// Country code that ChessBase writes in its tags (EventCountry, WhiteTeamCountry…) for a country
// number of its files. Table recorded on 21 November 2026 by matching the 105,000 tournaments (.cbt)
// and the 67,000 teams (.cbe) of the Mega Database 2025 with the PGN export of ChessBase '26, with no
// disagreement between the two: these are its own codes, which sometimes differ from FIDE codes (SIN,
// CRI, SAU, DDR, CSR, GE2 for the Saar…). A number missing from the table (no tournament or team of
// the Mega carries it) takes the code of the players table.
std::string chessbaseNation(int number)
{
    static const std::pair<int, const char*> table[] = {
        {1, "AFG"}, {2, "ALB"}, {3, "ALG"}, {4, "AND"}, {5, "ANG"}, {6, "ANT"}, {7, "ARG"}, {8, "ARM"}, {9, "AUS"}, {10, "AUT"}, 
        {11, "AZE"}, {12, "BAH"}, {13, "BHN"}, {14, "BAN"}, {15, "BAR"}, {16, "BLR"}, {17, "BEL"}, {18, "BLZ"}, {19, "BER"}, {20, "BOL"}, 
        {21, "BIH"}, {22, "BOT"}, {23, "BRA"}, {24, "IVB"}, {25, "BRN"}, {26, "BUL"}, {27, "BUR"}, {28, "CAN"}, {29, "CHI"}, {30, "CHN"}, 
        {31, "COL"}, {32, "CRI"}, {33, "CRO"}, {34, "CUB"}, {35, "CYP"}, {36, "CZE"}, {37, "DEN"}, {38, "DJI"}, {39, "DOM"}, {40, "ECU"}, 
        {41, "EGY"}, {42, "ENG"}, {43, "ESP"}, {44, "EST"}, {45, "ETH"}, {46, "FAI"}, {47, "FIJ"}, {48, "FIN"}, {49, "FRA"}, {50, "MKD"}, 
        {51, "GAM"}, {52, "GEO"}, {53, "GER"}, {54, "GHA"}, {55, "GRE"}, {56, "GUA"}, {57, "GCI"}, {58, "GUY"}, {59, "HAI"}, {60, "HON"}, 
        {61, "HKG"}, {62, "HUN"}, {63, "ISL"}, {64, "IND"}, {65, "INA"}, {66, "IRI"}, {67, "IRQ"}, {68, "ISR"}, {69, "IRL"}, {70, "ITA"}, 
        {71, "IVO"}, {72, "JAM"}, {73, "JPN"}, {74, "JCI"}, {75, "JOR"}, {76, "KAZ"}, {77, "KEN"}, {78, "KOR"}, {79, "KGZ"}, {80, "KUW"}, 
        {81, "LAT"}, {82, "LBN"}, {83, "LBY"}, {84, "LIE"}, {85, "LTU"}, {86, "LUX"}, {87, "MAC"}, {88, "MAD"}, {89, "MAS"}, {90, "MLI"}, 
        {91, "MLT"}, {92, "MAU"}, {93, "MRI"}, {94, "MEX"}, {95, "MDA"}, {96, "MNC"}, {97, "MGL"}, {98, "MAR"}, {99, "MOZ"}, {100, "MYA"}, 
        {101, "NAM"}, {102, "NEP"}, {103, "NED"}, {104, "AHO"}, {105, "NZL"}, {106, "NCA"}, {107, "NGR"}, {108, "NOR"}, {109, "PAK"}, {110, "PLE"}, 
        {111, "PAN"}, {112, "PNG"}, {113, "PAR"}, {114, "PER"}, {115, "PHI"}, {116, "POL"}, {117, "POR"}, {118, "PUR"}, {119, "QAT"}, {120, "ROU"}, 
        {121, "RUS"}, {122, "ESA"}, {123, "SMR"}, {124, "SCO"}, {125, "SEN"}, {126, "SEY"}, {127, "SIN"}, {128, "SVK"}, {129, "SLO"}, {130, "RSA"}, 
        {131, "SRI"}, {132, "SUD"}, {133, "SUR"}, {134, "SWE"}, {135, "SUI"}, {136, "SYR"}, {137, "TJK"}, {138, "TAN"}, {139, "THA"}, {140, "TRI"}, 
        {141, "TUN"}, {142, "TUR"}, {143, "TKM"}, {144, "UGA"}, {145, "UKR"}, {146, "UAE"}, {147, "USA"}, {148, "URU"}, {149, "URS"}, {150, "UZB"}, 
        {151, "VEN"}, {152, "VIE"}, {153, "ISV"}, {154, "WLS"}, {155, "YEM"}, {156, "YUG"}, {157, "ZAM"}, {158, "ZIM"}, {159, "ZRE"}, {160, "DDR"}, 
        {161, "CSR"}, {162, "CAM"}, {163, "CHD"}, {164, "KAP"}, {166, "COM"}, {167, "CON"}, {169, "LAO"}, {170, "LES"}, {171, "MWI"}, {172, "MDV"}, 
        {174, "OMN"}, {175, "NAU"}, {177, "NIG"}, {178, "SAU"}, {179, "TOG"}, {181, "VAN"}, {184, "SWA"}, {185, "SIE"}, {186, "SLU"}, {188, "SVI"}, 
        {190, "SKI"}, {191, "SAL"}, {192, "GE2"}, {193, "ZAR"}, {194, "RWA"}, {195, "LBR"}, {196, "NET"}, {197, "TPE"}, {200, "ARN"}, {201, "BNN"}, 
        {202, "BTN"}, {203, "BRI"}, {204, "CMB"}, {205, "CAY"}, {206, "CAR"}, {211, "ELG"}, {212, "ERI"}, {216, "GAB"}, {217, "GGB"}, {218, "GRA"}, 
        {219, "GRL"}, {221, "GMA"}, {222, "GUI"}, {224, "IOM"}, {226, "MFR"}, {234, "PAL"}, {236, "RUF"}, {237, "SAO"}, {238, "SOM"}, {245, "NIR"}, 
        {246, "ISS"}, {247, "GBR"}, {249, "MNE"}, {250, "SRB"}, {251, "CAT"}, {253, "KOS"}, {255, "DMA"}, 
    };
    for (const auto& [n, code] : table)
        if (n == number) return code;
    return hy64db::PlayerBase::nationCode(number);
}

// Tags that ChessBase's PGN export adds to the seven basic tags (.cbh databases; nothing for a
// .2cbh database): annotator, teams, tournament, source. The bridge reads them; here, only the translation of
// country numbers.
void applyExtraTags(CbbDb* d, int row, chess::Game& g, bool* noMoves = nullptr)
{
    char* text = cbb_extra_tags(d, quint32(row));
    if (!text) return;
    for (const char* p = text; *p;) {
        const char* tab = std::strchr(p, '\t');
        const char* end = std::strchr(p, '\n');
        if (!tab || !end || tab > end) break;
        const std::string key(p, tab);
        std::string value(tab + 1, end);
        p = end + 1;
        if (key == "Hy64Moves") { // not a tag: the database header says the game has no moves
            if (noMoves) *noMoves = value == "0";
            continue;
        }
        if (!value.empty() && value[0] == '#') {
            const int nation = std::atoi(value.c_str() + 1);
            value = chessbaseNation(nation);
        }
        if (!value.empty() && g.header(key).empty()) g.setHeader(key, value);
    }
    cbb_free_string(text);
}
} // namespace

bool ChessBaseSource::headerExtras(int row, chess::Game& game) const
{
    bool noMoves = false;
    applyExtraTags(db(m_db), row, game, &noMoves);
    return noMoves;
}

bool ChessBaseSource::gameForEncoding(int row, chess::Game& out, std::string* error) const
{
    char err[512] = "";
    uint16_t moves[1024];
    uint32_t count = UINT32_MAX;
    char* pgn = cbb_game_pgn_moves(db(m_db), quint32(row), moves, 1024, &count, err, sizeof err);
    if (!pgn) {
        if (error) *error = err;
        return false;
    }
    chess::pgn::ParseOptions options;
    options.withSan = false;
    if (count != UINT32_MAX) {
        options.hasMainline = true;
        options.mainline = moves;
        options.mainlineSize = count;
    }
    const bool ok = chess::pgn::parseGame(pgn, out, error, options);
    cbb_free_string(pgn);
    if (ok) applyExtraTags(db(m_db), row, out);
    return ok;
}

QString ChessBaseSource::gamePgn(int row, QString* error) const
{
    char err[512] = "";
    char* pgn = cbb_game_pgn(db(m_db), quint32(row), err, sizeof err);
    if (!pgn) {
        if (error) *error = QString::fromUtf8(err);
        return {};
    }
    const QString text = QString::fromUtf8(pgn);
    cbb_free_string(pgn);
    return text;
}

std::unique_ptr<IndexedGameSource::View> ChessBaseSource::search(const QString& text, const Cancel* cancel, QString* error) const
{
    static_assert(sizeof(std::atomic<int32_t>) == sizeof(int32_t));
    char err[512] = "";
    CbbIds* ids = cbb_search(db(m_db), text.toUtf8().constData(), reinterpret_cast<const volatile int32_t*>(cancel), err, sizeof err);
    if (!ids) {
        if (error) *error = QString::fromUtf8(err);
        return nullptr;
    }
    return std::unique_ptr<View>(new Ids(ids));
}

bool ChessBaseSource::game(int row, chess::Game& out, QString* error) const
{
    QString err;
    const QString text = gamePgn(row, &err);
    if (text.isEmpty()) {
        if (error) *error = err;
        return false;
    }
    std::string perr;
    const bool ok = chess::pgn::parseGame(text.toStdString(), out, &perr);
    if (!ok && error) *error = QString::fromStdString(perr);
    if (ok) applyExtraTags(db(m_db), row, out);
    return ok;
}

std::unique_ptr<IndexedGameSource::View> ChessBaseSource::viewIds(bool all) const
{
    return std::unique_ptr<View>(new Ids(cbb_view_ids(db(m_db), all ? 1 : 0)));
}

bool ChessBaseSource::sort(View& view, int column, bool ascending, const Cancel* cancel, QString* error) const
{
    auto& ids = static_cast<Ids&>(view);
    char err[512] = "";
    const bool ok = cbb_sort_ids(db(m_db), static_cast<CbbIds*>(ids.m_ids), column, ascending ? 1 : 0,
                                 reinterpret_cast<const volatile int32_t*>(cancel), err, sizeof err) == 1;
    if (!ok && error) *error = QString::fromUtf8(err);
    return ok;
}

void ChessBaseSource::setView(std::unique_ptr<View> view, bool filtered)
{
    auto* ids = static_cast<Ids*>(view.get());
    m_filtered = filtered && ids && ids->isValid();
    cbb_set_view(db(m_db), ids ? static_cast<CbbIds*>(std::exchange(ids->m_ids, nullptr)) : nullptr);
    std::fill(std::begin(m_cacheRow), std::end(m_cacheRow), -1);
}

#else // ChessBase reading not compiled in

bool ChessBaseSource::extractArchive(const QString&, const QString&, QString* error)
{
    if (error) *error = QStringLiteral("lecture ChessBase non compilée (cargo absent)");
    return false;
}
bool ChessBaseSource::open(const QString&, QString* error)
{
    if (error) *error = QStringLiteral("lecture ChessBase non compilée (cargo absent)");
    return false;
}
bool ChessBaseSource::isTwoCbh() const { return false; }
int ChessBaseSource::total() const { return 0; }
int ChessBaseSource::count() const { return 0; }
bool ChessBaseSource::header(int, Header&) const { return false; }
QString ChessBaseSource::gamePgn(int, QString*) const { return {}; }
bool ChessBaseSource::gameForEncoding(int, chess::Game&, std::string*) const { return false; }
bool ChessBaseSource::headerDirect(int, Header&) const { return false; }
bool ChessBaseSource::headerExtras(int, chess::Game&) const { return false; }
std::unique_ptr<IndexedGameSource::View> ChessBaseSource::search(const QString&, const Cancel*, QString*) const { return nullptr; }
bool ChessBaseSource::game(int, chess::Game&, QString* error) const { return fail(error) >= 0; }
std::unique_ptr<IndexedGameSource::View> ChessBaseSource::viewIds(bool) const { return nullptr; }
bool ChessBaseSource::sort(View&, int, bool, const Cancel*, QString*) const { return false; }
void ChessBaseSource::setView(std::unique_ptr<View>, bool) {}

#endif

#include "chess/pgn.h"

bool ChessBaseGameSource::open(const QString& path, QString* error)
{
    if (!m_source.open(path, error)) return false;
    m_count = uint64_t(m_source.total());
    m_texts.clear();
    m_textAt.clear();
    if (path.endsWith(QLatin1String(".cbh"), Qt::CaseInsensitive) && cbtexts::read(path, cbtexts::interfaceLanguage(), m_texts)) {
        // A text with nothing to read, or placed beyond the games known to the reader, is left aside
        QList<CbText> kept;
        for (CbText& t : m_texts)
            if (t.gamesBefore <= m_count && !(t.title.isEmpty() && t.plain().isEmpty())) kept.append(std::move(t));
        m_texts = std::move(kept);
        for (qsizetype k = 0; k < m_texts.size(); ++k) m_textAt.push_back(uint64_t(m_texts[k].gamesBefore) + uint64_t(k));
        m_count += uint64_t(m_texts.size());
    }
    return true;
}

int64_t ChessBaseGameSource::locate(uint64_t index) const
{
    const auto it = std::lower_bound(m_textAt.begin(), m_textAt.end(), index);
    if (it != m_textAt.end() && *it == index) return -1 - int64_t(it - m_textAt.begin());
    return int64_t(index) - int64_t(it - m_textAt.begin());
}

void ChessBaseGameSource::textAsGame(const CbText& text, chess::Game& game)
{
    game = chess::Game();
    game.setHeader("White", text.title.isEmpty() ? "Texte" : text.title.toStdString());
    game.setHeader("Black", "");
    game.setHeader("Event", "Texte");
    game.setHeader("Result", "*");
    game.root()->comment = text.plain().toStdString();
}

namespace {
const char NoMovesNote[] = "Coups illisibles dans la base ChessBase";
const char NoAnnotationsNote[] = "Annotations illisibles dans la base ChessBase"; // text written by the bridge (lib.rs)
} // namespace

bool ChessBaseGameSource::unreadableAsGame(int row, const std::string& why, chess::Game& game) const
{
    ChessBaseSource::Header h;
    if (!m_source.headerDirect(row, h)) return false;
    game = chess::Game();
    auto set = [&](const char* key, const QString& value) {
        if (!value.isEmpty()) game.setHeader(key, value.toStdString());
    };
    set("White", h.white);
    set("Black", h.black);
    set("Event", h.event);
    set("Date", h.date);
    set("ECO", h.eco);
    if (h.whiteElo > 0) game.setHeader("WhiteElo", std::to_string(h.whiteElo));
    if (h.blackElo > 0) game.setHeader("BlackElo", std::to_string(h.blackElo));
    game.setHeader("Result", h.result.isEmpty() ? "*" : h.result.toStdString());
    // Site, round and tags from the database files; a game whose header says it has no moves is
    // returned empty, without a note: this is how ChessBase exports it (unplayed game, "BYE").
    if (m_source.headerExtras(row, game)) return true;
    // Short reason: the end of the message, without the file paths
    std::string reason = why;
    if (const size_t at = reason.rfind(": "); at != std::string::npos) reason.erase(0, at + 2);
    for (char& c : reason)
        if (c == '{' || c == '}') c = ' ';
    game.root()->comment = std::string(NoMovesNote) + (reason.empty() ? std::string() : " (" + reason + ")") + " : en-têtes conservés.";
    return true;
}

void ChessBaseGameSource::notePartial(uint64_t index, const chess::Game& game) const
{
    const std::string& head = game.root()->comment;
    if (head.empty()) return;
    const bool noMoves = head.rfind(NoMovesNote, 0) == 0;
    if (!noMoves && head.rfind(NoAnnotationsNote, 0) != 0) return;
    std::lock_guard<std::mutex> lock(m_partialLock);
    m_partial[index] = {noMoves, head};
}

hy64db::GameSource::Partial ChessBaseGameSource::partial() const
{
    std::lock_guard<std::mutex> lock(m_partialLock);
    Partial p;
    for (const auto& [index, what] : m_partial) {
        ++(what.first ? p.withoutMoves : p.withoutAnnotations);
        if (p.first.size() < 40) p.first.push_back("partie " + std::to_string(index + 1) + " : " + what.second);
    }
    return p;
}

bool ChessBaseGameSource::readForEncoding(uint64_t index, chess::Game& game, std::string* error) const
{
    const int64_t at = locate(index);
    if (at < 0) {
        textAsGame(m_texts[qsizetype(-1 - at)], game);
        return true;
    }
    std::string why;
    if (m_source.gameForEncoding(int(at), game, &why)) {
        notePartial(index, game);
        return true;
    }
    if (unreadableAsGame(int(at), why, game)) {
        notePartial(index, game);
        return true;
    }
    if (error) *error = why;
    return false;
}

bool ChessBaseGameSource::read(uint64_t index, chess::Game& game, std::string* error) const
{
    const int64_t at = locate(index);
    if (at < 0) {
        textAsGame(m_texts[qsizetype(-1 - at)], game);
        return true;
    }
    QString err;
    if (m_source.game(int(at), game, &err)) { // Full PGN, then the tournament tags like readForEncoding
        notePartial(index, game);
        return true;
    }
    std::string why = err.toStdString();
    if (unreadableAsGame(int(at), why, game)) {
        notePartial(index, game);
        return true;
    }
    if (error) *error = why;
    return false;
}
