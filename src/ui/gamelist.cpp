#include "gamelist.h"

#include "chess/epd.h"
#include "openings.h"
#include "db/format.h"
#include "db/headerquery.h"
#include "hy64source.h"
#include "baseicons.h"
#include "conversion.h"
#include "display.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QFont>
#include <QMimeData>
#include <QDesktopServices>
#include <QMenu>
#include <QUrl>
#include <QEventLoop>
#include <QProgressDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QTimer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QTabBar>
#include <QThread>
#include <QLocale>
#include <QToolButton>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

using namespace chess;

// Filtered view of PGN databases: text of the filter field AND rows kept by the header search.
class RowFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void refilter()
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        const auto* m = qobject_cast<const GameListModel*>(sourceModel());
        if (m && !m->rowAllowed(row)) return false;
        // All the columns except "Moves": reading it would count the moves of every game of the
        // file at the first keystroke, and a typed number does not search for a game length.
        const QRegularExpression re = filterRegularExpression();
        if (!m || re.pattern().isEmpty()) return true;
        for (int c = 0; c < GameListModel::ColumnCount; ++c)
            if (c != GameListModel::Moves && m->data(m->index(row, c, parent), filterRole()).toString().contains(re)) return true;
        return false;
    }
};

// ---------------------------------------------------------------- GameListModel

namespace {
QString readOnlyMessage() { return GameListModel::tr("base en lecture seule"); }

// PGN game (text) → game, to write it into an indexed database.
bool parsePgn(const std::string& pgn, chess::Game& g, QString* error)
{
    std::string err;
    if (chess::pgn::parseGame(pgn, g, &err)) return true;
    if (error) *error = QString::fromStdString(err);
    return false;
}
} // namespace

bool GameListModel::isEpdPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("epd");
}

bool GameListModel::loadFile(const QString& path, QString* error)
{
    if (conversion::isProprietary(path)) { // ChessBase…: only through conversion
        if (error) *error = tr("format %1 : à convertir d'abord (Fichier → Convertir…)").arg(conversion::formatName(path));
        return false;
    }
    if (Hy64Source::isDatabasePath(path)) {
        auto b = std::make_unique<Hy64Source>();
        if (!b->open(path, error)) return false;
        std::unique_ptr<IndexedGameSource> source = std::move(b);
        beginResetModel();
        m_epd = false;
        m_indexed = std::move(source);
        m_data.clear();
        m_entries.clear();
        m_path = path;
        endResetModel();
        return true;
    }
    // Opening tree (.hy64book, Polyglot .bin…): not a database of games, it opens in the Opening tree panel.
    if (conversion::kindOf(path) == conversion::Kind::Tree) {
        if (error) *error = tr("%1 : ce n'est pas une base de parties (à ouvrir dans le panneau Arbre d'ouverture)").arg(conversion::formatName(path));
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const bool epdFile = isEpdPath(path);
    std::string data = f.readAll().toStdString();
    // Everything else is read as text (PGN, EPD): a binary file would only give unreadable rows.
    if (std::string_view(data).substr(0, 65536).find('\0') != std::string_view::npos) {
        if (error) *error = tr("fichier binaire : ce n'est ni une base Hydra64, ni du PGN, ni de l'EPD");
        return false;
    }
    if (epdFile) {
        int positions = 0, skipped = 0;
        data = chess::epd::toPgn(data, QFileInfo(path).fileName().toStdString(), &positions, &skipped);
        if (positions == 0 && skipped > 0) { // an empty file is a new collection
            if (error) *error = tr("aucune position EPD valide");
            return false;
        }
    }
    std::vector<pgn::GameEntry> entries = pgn::indexDatabase(data);
    // Text without a single game: not a PGN (an empty file stays a new database).
    if (entries.empty() && data.find_first_not_of(" \t\r\n\xEF\xBB\xBF") != std::string::npos) {
        if (error) *error = epdFile ? tr("aucune position EPD valide") : tr("aucune partie PGN dans ce fichier");
        return false;
    }
    beginResetModel();
    m_indexed.reset();
    m_epd = epdFile;
    m_data = std::move(data);
    m_entries = std::move(entries);
    m_rowFilter.clear();
    m_moves.clear();
    m_path = path;
    endResetModel();
    return true;
}

void GameListModel::setIndexedView(std::unique_ptr<IndexedGameSource::View> view, bool filtered)
{
    if (!m_indexed) return;
    beginResetModel();
    m_indexed->setView(std::move(view), filtered);
    endResetModel();
}

bool GameListModel::parseGame(int row, Game& out, QString* error) const
{
    if (row < 0 || row >= rowCount()) return false;
    if (m_indexed) return m_indexed->game(row, out, error);
    const auto& e = m_entries[size_t(row)];
    std::string err;
    const bool ok = pgn::parseGame(std::string_view(m_data).substr(e.offset, e.length), out, &err);
    if (!ok && error) *error = QString::fromStdString(err);
    return ok;
}

QString GameListModel::gameText(int row) const
{
    if (row < 0 || row >= rowCount()) return {};
    if (m_indexed) return m_indexed->gameText(row);
    const auto& e = m_entries[size_t(row)];
    return QString::fromStdString(m_data.substr(e.offset, e.length)).trimmed();
}

bool GameListModel::summary(int row, IndexedGameSource::Summary& out) const
{
    if (m_indexed) return m_indexed->summary(row, out);
    if (row < 0 || row >= int(m_entries.size())) return false;
    const auto& e = m_entries[size_t(row)];
    out.year = std::atoi(e.date.c_str());
    out.whiteElo = std::atoi(e.whiteElo.c_str());
    out.blackElo = std::atoi(e.blackElo.c_str());
    out.result = e.result == "1-0" ? 1 : e.result == "0-1" ? -1 : e.result == "1/2-1/2" ? 0 : 2;
    out.eco = hy64db::parseEco(e.eco);
    return true;
}

std::vector<char> GameListModel::matchHeaders(const hy64db::HeaderQuery& q) const
{
    std::vector<char> out(m_entries.size(), 0);
    if (m_indexed) return out;
    const std::string_view data(m_data);
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const auto& e = m_entries[i];
        out[i] = hy64db::matches(q, e, data.substr(e.offset, e.length)) ? 1 : 0;
    }
    return out;
}

void GameListModel::setRowFilter(std::vector<char> rows)
{
    m_rowFilter = std::move(rows);
    if (!m_rowFilter.empty()) m_rowFilter.resize(m_entries.size(), 0);
}

bool GameListModel::removeGames(QList<int> rows, QString* error)
{
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (m_indexed) {
        if (m_indexed->isReadOnly()) {
            if (error) *error = readOnlyMessage();
            return false;
        }
        const bool ok = m_indexed->setDeleted(rows, true, error);
        if (!rows.isEmpty()) emit dataChanged(index(rows.first(), 0), index(rows.last(), ColumnCount - 1));
        return ok;
    }
    if (rows.isEmpty() || m_path.isEmpty()) return false;

    // New content: everything except the slices of the deleted games.
    std::string out;
    out.reserve(m_data.size());
    size_t pos = 0;
    for (int row : rows) {
        if (row < 0 || row >= rowCount()) continue;
        const auto& e = m_entries[size_t(row)];
        out.append(m_data, pos, e.offset - pos);
        pos = e.offset + e.length;
    }
    out.append(m_data, pos, std::string::npos);

    return writeData(std::move(out), error);
}

bool GameListModel::writeData(std::string data, QString* error)
{
    if (m_epd) {
        // EPD file: rewritten as EPD (one line per game), then reread so that the list shows
        // exactly what the file contains.
        const std::string epdText = chess::epd::fromPgn(data);
        QSaveFile f(m_path);
        if (!f.open(QIODevice::WriteOnly) || f.write(epdText.data(), qint64(epdText.size())) != qint64(epdText.size()) || !f.commit()) {
            if (error) *error = f.errorString();
            return false;
        }
        data = chess::epd::toPgn(epdText, QFileInfo(m_path).fileName().toStdString());
    } else {
        QSaveFile f(m_path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data.data(), qint64(data.size())) != qint64(data.size()) || !f.commit()) {
            if (error) *error = f.errorString();
            return false;
        }
    }
    beginResetModel();
    m_data = std::move(data);
    m_entries = pgn::indexDatabase(m_data);
    m_rowFilter.clear();
    m_moves.clear(); // rows may have moved: search filter removed
    endResetModel();
    return true;
}

// PGN database: games appended to the end of the file (the file is neither rewritten nor reindexed in full).
// An EPD file, however, is rewritten (one line per game).
int GameListModel::appendText(const std::vector<std::string>& pgns, QString* error)
{
    std::string out = m_data;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    const bool clean = out.size() + 1 == m_data.size() && m_data.back() == '\n'; // file ended by a single line ending
    std::string added;
    for (const std::string& pgn : pgns) {
        if (!out.empty() || !added.empty()) added += "\n\n";
        added += pgn;
    }
    while (!added.empty() && (added.back() == '\n' || added.back() == '\r' || added.back() == ' ')) added.pop_back();
    added += '\n';
    if (m_epd || (!clean && !m_data.empty())) { // EPD, or end of file to normalise: rewrite
        const int before = rowCount();
        if (!writeData(out + added, error)) return -1;
        return rowCount() - before == int(pgns.size()) ? int(pgns.size()) : rowCount() - before;
    }
    // `added` starts with the separating blank line: the file already ends with "\n".
    const std::string tail = m_data.empty() ? added : added.substr(1);
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append) || f.write(tail.data(), qint64(tail.size())) != qint64(tail.size()) || !f.flush()) {
        if (error) *error = f.errorString();
        return -1;
    }
    f.close();
    const size_t base = m_data.size();
    std::vector<chess::pgn::GameEntry> entries = pgn::indexDatabase(tail);
    if (entries.empty()) return 0;
    beginInsertRows({}, int(m_entries.size()), int(m_entries.size() + entries.size()) - 1);
    m_data += tail;
    for (chess::pgn::GameEntry& e : entries) {
        e.offset += base;
        m_entries.push_back(std::move(e));
    }
    if (!m_rowFilter.empty()) m_rowFilter.resize(m_entries.size(), 1); // the added games stay visible
    if (!m_moves.empty()) m_moves.resize(m_entries.size(), -1);
    endInsertRows();
    return int(entries.size());
}

int GameListModel::appendGame(const std::string& pgn, QString* error)
{
    if (m_indexed) {
        if (m_indexed->isReadOnly()) {
            if (error) *error = readOnlyMessage();
            return -1;
        }
        chess::Game g;
        if (!parsePgn(pgn, g, error)) return -1;
        const int row = m_indexed->count();
        beginInsertRows({}, row, row);
        const int added = m_indexed->append(g, error);
        endInsertRows();
        return added;
    }
    if (m_path.isEmpty()) return -1;
    return appendText({pgn}, error) == 1 ? rowCount() - 1 : -1;
}

int GameListModel::appendGames(const std::vector<std::string>& pgns, QString* error)
{
    if (pgns.empty()) return 0;
    if (m_indexed) {
        int added = 0;
        for (const std::string& pgn : pgns) {
            if (appendGame(pgn, error) < 0) return added ? added : -1;
            ++added;
        }
        return added;
    }
    if (m_path.isEmpty()) return -1;
    return appendText(pgns, error);
}

int GameListModel::copyFrom(const GameListModel& from, const QList<int>& rows, QString* error)
{
    auto* target = dynamic_cast<Hy64Source*>(m_indexed.get());
    const auto* source = dynamic_cast<const Hy64Source*>(from.m_indexed.get());
    if (!target || !source) return -2;
    QList<int> wanted;
    for (int r : rows)
        if (r >= 0 && r < source->count() && !source->isDeleted(r)) wanted << r;
    if (wanted.isEmpty()) return 0;
    const int first = target->count();
    beginInsertRows({}, first, first + int(wanted.size()) - 1);
    int added = 0;
    for (int r : wanted) {
        if (target->appendFrom(*source, r, error) < 0) break;
        ++added;
    }
    target->sync(); // the whole series is on disk
    endInsertRows();
    if (added != wanted.size()) { // error along the way: the view reflects what was written
        beginResetModel();
        endResetModel();
        return added ? added : -1;
    }
    return added;
}

int GameListModel::rewriteGames(const std::function<bool(chess::Game&)>& edit, QString* error, const std::function<bool(int, int)>& progress)
{
    if (m_indexed || m_path.isEmpty()) return -1;
    std::string out;
    out.reserve(m_data.size() + m_data.size() / 20);
    int changed = 0;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (progress && i % 256 == 0 && !progress(int(i), int(m_entries.size()))) return -1;
        const auto& e = m_entries[i];
        const std::string_view text = std::string_view(m_data).substr(e.offset, e.length);
        chess::Game g;
        std::string written;
        if (pgn::parseGame(text, g) && edit(g)) {
            written = pgn::writeGame(g);
            ++changed;
        } else {
            written = std::string(text);
        }
        while (!written.empty() && (written.back() == '\n' || written.back() == ' ' || written.back() == '\r')) written.pop_back();
        if (!out.empty()) out += "\n\n";
        out += written;
    }
    out += '\n';
    if (changed && !writeData(std::move(out), error)) return -1;
    return changed;
}

Qt::ItemFlags GameListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QMimeData* GameListModel::mimeData(const QModelIndexList& indexes) const
{
    QList<int> rows;
    for (const QModelIndex& i : indexes)
        if (i.isValid()) rows << i.row();
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.isEmpty()) return nullptr;
    auto* mime = new QMimeData;
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << m_path << rows;
    mime->setData(MimeType, bytes);
    // PGN text (to an editor, another program): only for a reasonable selection.
    if (rows.size() <= 1000) {
        QStringList texts;
        for (int r : rows) texts << gameText(r);
        mime->setText(texts.join(QStringLiteral("\n\n")) + QLatin1Char('\n'));
    }
    return mime;
}

bool GameListModel::replaceGame(int row, const std::string& pgn, QString* error)
{
    if (m_indexed) {
        if (m_indexed->isReadOnly()) {
            if (error) *error = readOnlyMessage();
            return false;
        }
        chess::Game g;
        if (!parsePgn(pgn, g, error) || !m_indexed->replace(row, g, error)) return false;
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        return true;
    }
    if (row < 0 || row >= rowCount()) return false;
    const auto& e = m_entries[size_t(row)];
    std::string out = m_data.substr(0, e.offset) + pgn;
    out += m_data.substr(e.offset + e.length);
    return writeData(std::move(out), error);
}

QString GameListModel::userFlagName(int flag)
{
    static const char* names[] = {QT_TR_NOOP("Ouverture"), QT_TR_NOOP("Milieu de partie"), QT_TR_NOOP("Finale"),
                                  QT_TR_NOOP("Tactique"),  QT_TR_NOOP("Stratégie"),        QT_TR_NOOP("À revoir")};
    return flag >= 0 && flag < 6 ? tr(names[flag]) : QString();
}

QString GameListModel::userFlagLetter(int flag)
{
    static const char* letters[] = {QT_TR_NOOP("O"), QT_TR_NOOP("M"), QT_TR_NOOP("F"), QT_TR_NOOP("T"), QT_TR_NOOP("S"), QT_TR_NOOP("R")};
    return flag >= 0 && flag < 6 ? tr(letters[flag]) : QString();
}

int GameListModel::numberOf(int row) const
{
    if (!m_indexed) return row + 1;
    IndexedGameSource::Header h;
    return m_indexed->header(row, h) ? int(h.id) : -1;
}

uint8_t GameListModel::userFlags(int row) const
{
    IndexedGameSource::Header h;
    return m_indexed && m_indexed->header(row, h) ? h.userFlags : 0;
}

bool GameListModel::setUserFlag(const QList<int>& rows, int flag, bool on, QString* error)
{
    if (!supportsUserFlags()) return false;
    const bool ok = m_indexed->setUserFlag(rows, flag, on, error);
    if (!rows.isEmpty()) {
        const auto [lo, hi] = std::minmax_element(rows.begin(), rows.end());
        emit dataChanged(index(*lo, 0), index(*hi, ColumnCount - 1));
    }
    return ok;
}

bool GameListModel::restoreGames(const QList<int>& rows, QString* error)
{
    if (!m_indexed || m_indexed->isReadOnly()) return false;
    const bool ok = m_indexed->setDeleted(rows, false, error);
    if (!rows.isEmpty()) {
        const auto [lo, hi] = std::minmax_element(rows.begin(), rows.end());
        emit dataChanged(index(*lo, 0), index(*hi, ColumnCount - 1));
    }
    return ok;
}

void GameListModel::setSuspended(bool suspended)
{
    if (m_suspended == suspended) return;
    m_suspended = suspended;
    if (!suspended && rowCount() > 0) emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
}

QVariant GameListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || m_suspended) return {};
    if (role == Qt::ToolTipRole && index.column() == Beauty)
        return tr("Beauté de la partie, de ★ à ★★★ : sacrifices, mat, chasse au roi, sous-promotion.\nCalculée d'après les coups, sans moteur (Base → Évaluer la beauté des parties…).");
    if (role == Qt::ToolTipRole && index.column() == Eco) { // name of the opening
        const QString eco = data(index, Qt::DisplayRole).toString();
        return eco.isEmpty() ? QVariant() : QVariant(openingLabel(eco));
    }
    if (m_indexed && role == Qt::FontRole) { // deleted game: struck through
        if (!m_indexed->isDeleted(index.row())) return {};
        QFont f;
        f.setStrikeOut(true);
        return f;
    }
    if (m_indexed && (role == Qt::DisplayRole || role == Qt::ForegroundRole)) {
        // Header read on demand: only the displayed rows are decoded.
        IndexedGameSource::Header h;
        if (!m_indexed->header(index.row(), h)) return {};
        if (role == Qt::ForegroundRole) {
            if (h.deleted) return theme::TextFaint;
            switch (index.column()) {
            case Beauty: return QColor(0xe2, 0xb3, 0x3c); // medals in gold
            case White:
            case Black:
            case Event: return theme::Text;
            case Result: return h.result == QLatin1String("1-0") ? QVariant(QColor(0xee, 0xec, 0xe6)) : QVariant(theme::TextDim);
            default: return theme::TextDim;
            }
        }
        switch (index.column()) {
        case Number: { // number in the database, followed by the flag letters
            if (!h.userFlags) return h.id;
            QString letters;
            for (int f = 0; f < hy64db::UserFlagCount; ++f)
                if (h.userFlags & (1 << f)) letters += userFlagLetter(f);
            return QStringLiteral("%1 %2").arg(h.id).arg(letters);
        }
        case White: return h.white;
        case WhiteElo: return h.whiteElo > 0 ? QVariant(h.whiteElo) : QVariant();
        case Black: return h.black;
        case BlackElo: return h.blackElo > 0 ? QVariant(h.blackElo) : QVariant();
        case Result: return QString(h.result).replace(QStringLiteral("1/2"), QStringLiteral("½"));
        case Date: return display::date(h.date.toStdString());
        case Event: return h.event;
        case Eco: return h.eco;
        case Site: return h.site;
        case Round: return h.round;
        case Moves: return h.plies >= 0 ? QVariant((h.plies + 1) / 2) : QVariant();
        case Beauty: return h.beauty > 0 ? QVariant(QString(h.beauty, QChar(0x2605))) : QVariant(); // ★
        default: return {};
        }
    }
    if (m_indexed) {
        if (role == Qt::TextAlignmentRole) {
            switch (index.column()) {
            case Number:
            case WhiteElo:
            case BlackElo:
            case Moves: return int(Qt::AlignRight | Qt::AlignVCenter);
            case Result:
            case Eco:
            case Round: return int(Qt::AlignCenter);
            default: return int(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }
        return {};
    }
    if (index.column() == Beauty) return {}; // PGN: no mark
    const auto& e = m_entries[size_t(index.row())];
    auto s = [](const std::string& v) { return QString::fromStdString(v); };

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Number: return index.row() + 1;
        case White: return s(e.white);
        case WhiteElo: return e.whiteElo.empty() ? QVariant() : QVariant(std::atoi(e.whiteElo.c_str()));
        case Black: return s(e.black);
        case BlackElo: return e.blackElo.empty() ? QVariant() : QVariant(std::atoi(e.blackElo.c_str()));
        case Result: return s(e.result).replace(QStringLiteral("1/2"), QStringLiteral("½"));
        case Date: return display::date(e.date);
        case Event: return s(e.event);
        case Eco: return s(e.eco);
        case Site: return s(e.site);
        case Round: return s(e.round);
        case Moves: {
            if (m_moves.size() != m_entries.size()) m_moves.assign(m_entries.size(), -1);
            int& n = m_moves[size_t(index.row())];
            if (n < 0) n = hy64db::countMoves(std::string_view(m_data).substr(e.offset, e.length));
            return n;
        }
        default: break;
        }
    } else if (role == Qt::UserRole) { // sort key
        if (index.column() == Date) return QString::fromStdString(e.date).replace('?', '0');
        return data(index, Qt::DisplayRole);
    } else if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case Number:
        case WhiteElo:
        case BlackElo: return int(Qt::AlignRight | Qt::AlignVCenter);
        case Result:
        case Eco: return int(Qt::AlignCenter);
        default: return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    } else if (role == Qt::ForegroundRole) {
        switch (index.column()) {
        case Number:
        case WhiteElo:
        case BlackElo:
        case Date:
        case Eco: return theme::TextDim;
        case Result:
            if (e.result == "1-0") return QColor(0xee, 0xec, 0xe6);
            if (e.result == "0-1") return theme::TextDim;
            return theme::TextDim;
        default: return theme::Text;
        }
    }
    return {};
}

QVariant GameListModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case Number: return tr("N°");
    case White: return tr("Blancs");
    case WhiteElo: return tr("Elo");
    case Black: return tr("Noirs");
    case BlackElo: return tr("Elo");
    case Result: return tr("Rés.");
    case Date: return tr("Date");
    case Event: return tr("Tournoi");
    case Eco: return tr("ECO");
    case Site: return tr("Lieu");
    case Round: return tr("Ronde");
    case Moves: return tr("Coups");
    case Beauty: return tr("Beauté");
    default: return {};
    }
}

// ---------------------------------------------------------------- GameListPanel

namespace {
// Icon view of the open databases: laid over an icon, shows its close cross (top right corner)
// only while the pointer is over it. Clicks elsewhere go through to the list.
class HoverCross : public QWidget {
public:
    explicit HoverCross(QToolButton* cross)
        : m_cross(cross)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        auto* top = new QHBoxLayout;
        top->setContentsMargins(0, 0, 0, 0);
        top->addStretch(1);
        top->addWidget(cross);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 2, 2, 0);
        layout->addLayout(top);
        layout->addStretch(1);
        cross->hide();
    }

protected:
    void enterEvent(QEnterEvent*) override { m_cross->show(); }
    void leaveEvent(QEvent*) override { m_cross->hide(); }

private:
    QToolButton* m_cross;
};
} // namespace

GameListPanel::GameListPanel(QWidget* parent)
    : QWidget(parent)
    , m_empty(new GameListModel(this))
    , m_model(m_empty)
    , m_proxy(new RowFilterProxy(this))
{
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setSortRole(Qt::UserRole);
    m_proxy->setFilterRole(Qt::DisplayRole);

    auto* bar = new QWidget;
    bar->setObjectName("PanelBody");
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 6, 8, 6);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filtrer : joueur, tournoi, ECO…"));
    m_filter->addAction(theme::icon("search"), QLineEdit::LeadingPosition);
    m_filter->setClearButtonEnabled(true);
    m_count = new QLabel;
    m_count->setObjectName("Faint");
    barLayout->addWidget(m_filter, 1);
    barLayout->addWidget(m_count);

    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(GameListModel::Number, Qt::AscendingOrder);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(26);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // A single column takes the remaining width, "Round" in last place or failing that a wide
    // column (see arrangeColumns): stretched automatically, a narrow final column ("Moves"…) could
    // no longer be shrunk.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setColumnWidth(GameListModel::Number, 52);
    m_table->setColumnWidth(GameListModel::White, 170);
    m_table->setColumnWidth(GameListModel::WhiteElo, 52);
    m_table->setColumnWidth(GameListModel::Black, 170);
    m_table->setColumnWidth(GameListModel::BlackElo, 52);
    m_table->setColumnWidth(GameListModel::Result, 50);
    m_table->setColumnWidth(GameListModel::Date, 96);
    m_table->setColumnWidth(GameListModel::Event, 180);
    m_table->setColumnWidth(GameListModel::Eco, 52);
    m_table->setColumnWidth(GameListModel::Site, 140);
    m_table->setColumnWidth(GameListModel::Round, 52);
    m_table->setColumnWidth(GameListModel::Moves, m_table->horizontalHeader()->fontMetrics().horizontalAdvance(tr("Coups")) + 30); // the word, barely more
    // Displayed columns (right-click on the header), remembered
    {
        QHeaderView* header = m_table->horizontalHeader();
        const QVariant saved = QSettings().value("gameList/hiddenColumns");
        QList<int> hidden = GameListModel::defaultHiddenColumns();
        if (saved.isValid()) {
            hidden.clear();
            for (const QVariant& v : saved.toList()) hidden << v.toInt();
        }
        for (int c = 0; c < GameListModel::ColumnCount; ++c) header->setSectionHidden(c, hidden.contains(c));
        // Widths set by hand (edge of a header dragged), remembered: they are kept by the later
        // layouts. A double-click on the edge gives the column back to the automatic width.
        const QVariantMap manual = QSettings().value("gameList/columnWidths").toMap();
        for (auto it = manual.begin(); it != manual.end(); ++it) {
            const int c = it.key().toInt(), w = it.value().toInt();
            if (c > GameListModel::Number && c < GameListModel::ColumnCount && w >= 16) m_manualWidths.insert(c, w);
        }
        header->viewport()->installEventFilter(this); // a press on the header: the resize that follows is the user's
        connect(header, &QHeaderView::sectionResized, this, [this](int c, int, int width) {
            if (m_layingOut || !m_headerPressed || width <= 0 || c == m_fillerColumn) return;
            m_manualWidths.insert(c, width);
            saveManualWidths();
            layoutColumns(); // the end column takes up the difference
        });
        connect(header, &QHeaderView::sectionHandleDoubleClicked, this, [this](int c) {
            m_headerPressed = false;
            if (m_manualWidths.remove(c)) saveManualWidths();
            fitColumns();
        });
        arrangeColumns();
        header->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(header, &QWidget::customContextMenuRequested, this, [this, header](const QPoint& at) {
            QMenu menu(this);
            for (int c = GameListModel::White; c < GameListModel::ColumnCount; ++c) { // the number always stays displayed
                QString name = m_empty->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
                if (c == GameListModel::WhiteElo) name = tr("Elo des Blancs");
                if (c == GameListModel::BlackElo) name = tr("Elo des Noirs");
                QAction* a = menu.addAction(name);
                a->setCheckable(true);
                a->setChecked(!header->isSectionHidden(c));
                connect(a, &QAction::toggled, this, [this, header, c](bool on) { setColumnVisible(c, on); });
            }
            menu.addSeparator();
            menu.addAction(tr("Colonnes par défaut"), this, [this] {
                m_manualWidths.clear();
                saveManualWidths();
                for (int c = 0; c < GameListModel::ColumnCount; ++c) setColumnVisible(c, !GameListModel::defaultHiddenColumns().contains(c));
            });
            menu.exec(header->mapToGlobal(at));
        });
    }

    m_tabs = new QTabBar;
    m_tabs->setObjectName("DatabaseTabs");
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->setMovable(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideMiddle);
    // "+" right after the last tab: open another database without going through the menu.
    auto* open = new QToolButton;
    open->setObjectName("DatabaseOpen");
    open->setIcon(theme::icon("new"));
    open->setIconSize(QSize(14, 14));
    open->setAutoRaise(true);
    open->setToolTip(tr("Ouvrir une base…"));
    connect(open, &QToolButton::clicked, this, &GameListPanel::openRequested);
    // Open databases are chosen in a separate list (basesWidget(), placed by the window
    // in the "Databases" panel); the tab bar remains the reference state — current
    // database, order — but is no longer displayed.
    m_tabs->setParent(this);
    m_tabs->hide();
    m_basesBox = new QWidget;
    m_basesBox->setObjectName("PanelBody");
    auto* basesLayout = new QVBoxLayout(m_basesBox);
    basesLayout->setContentsMargins(0, 0, 0, 0);
    basesLayout->setSpacing(0);
    auto* basesBar = new QWidget;
    basesBar->setObjectName("ListBar");
    auto* basesBarLayout = new QHBoxLayout(basesBar);
    basesBarLayout->setContentsMargins(10, 6, 6, 6);
    basesBarLayout->setSpacing(6);
    auto* basesTitle = new QLabel(tr("Bases ouvertes"));
    basesTitle->setObjectName("Dim");
    basesBarLayout->addWidget(basesTitle);
    basesBarLayout->addStretch(1);
    // Display: list or large icons (useful when the panel is placed above the game list)
    auto viewButton = [this, basesBarLayout](const char* icon, const QString& tip, bool icons) {
        auto* b = new QToolButton;
        b->setIcon(theme::icon(icon));
        b->setIconSize(QSize(14, 14));
        b->setAutoRaise(true);
        b->setCheckable(true);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, [this, icons] { setBasesAsIcons(icons); });
        basesBarLayout->addWidget(b);
        return b;
    };
    m_basesListButton = viewButton("list", tr("Afficher les bases en liste"), false);
    m_basesIconsButton = viewButton("grid", tr("Afficher les bases en icônes"), true);
    basesBarLayout->addWidget(open);
    m_baseList = new QListWidget;
    m_baseList->setObjectName("DatabaseList");
    m_baseList->setFrameShape(QFrame::NoFrame);
    m_baseList->setIconSize(QSize(16, 16));
    m_baseList->setTextElideMode(Qt::ElideMiddle);
    m_baseList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_baseList->setAcceptDrops(true);
    m_baseList->viewport()->setAcceptDrops(true);
    m_baseList->viewport()->installEventFilter(this); // dragging games onto a database of the list
    m_table->viewport()->installEventFilter(this);     // list width: columns laid out (layoutColumns)
    basesLayout->addWidget(basesBar);
    basesLayout->addWidget(m_baseList, 1);
    // Right-click on a database: compact it (Hydra64 database), open its folder.
    m_baseList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_baseList, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const int row = m_baseList->row(m_baseList->itemAt(at));
        if (row < 0 || row >= m_bases.size()) return;
        GameListModel* model = m_bases[row].model;
        const QString path = model->filePath();
        QMenu menu(this);
        QAction* compact = menu.addAction(tr("Compacter la base"));
        compact->setToolTip(tr("Retire définitivement les parties supprimées (rayées)"));
        compact->setEnabled(dynamic_cast<Hy64Source*>(model->indexed()) != nullptr);
        QAction* folder = menu.addAction(theme::icon("open"), tr("Ouvrir le dossier"));
        folder->setEnabled(!path.isEmpty());
        QAction* changeIcon = menu.addAction(tr("Changer l'icône…"));
        menu.addSeparator();
        QAction* closeBase = menu.addAction(theme::icon("close"), tr("Fermer la base"));
        menu.setToolTipsVisible(true);
        QAction* chosen = menu.exec(m_baseList->viewport()->mapToGlobal(at));
        if (chosen == compact) {
            m_tabs->setCurrentIndex(row); // compaction bears on the displayed database
            emit compactRequested();
        } else if (chosen == folder) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        } else if (chosen == changeIcon) {
            QString id = baseicons::chosen(path);
            if (baseicons::choose(this, m_tabs->tabText(row), &id)) {
                baseicons::setChosen(path, id);
                syncBaseList();
            }
        } else if (chosen == closeBase) {
            QTimer::singleShot(0, this, [this, row] { closeDatabase(row); }); // the list is rebuilt: not during the menu
        }
    });
    connect(m_baseList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_tabs->count() && row != m_tabs->currentIndex()) m_tabs->setCurrentIndex(row);
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) {
        const QSignalBlocker quiet(m_baseList);
        m_baseList->setCurrentRow(i);
    });
    setBasesAsIcons(QSettings().value("gameList/basesAsIcons", true).toBool()); // icons by default, the panel being above the list

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(m_table, 1);

    // Indexed database (millions of games): the search follows typing, started a moment after
    // the last key and from two characters on; the one in progress is dropped. Enter
    // starts it right away, even for a single character.
    m_typing = new QTimer(this);
    m_typing->setSingleShot(true);
    m_typing->setInterval(300);
    connect(m_typing, &QTimer::timeout, this, [this] {
        if (m_model->isIndexed() && m_filter->text().trimmed().size() >= 2) startSearch();
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& t) {
        const int i = currentIndex();
        if (i >= 0) m_bases[i].filter = t;
        if (m_model->isIndexed()) {
            // Field emptied = the whole database (in the chosen sort order), right away.
            if (t.trimmed().isEmpty()) {
                m_typing->stop();
                if (m_searchModel == m_model) stopSearch();
                if (m_model->indexed()->isFiltered()) startIndexedJob(Job::Sort);
            } else {
                m_typing->start();
            }
            updateCount();
            return;
        }
        m_proxy->setFilterFixedString(t);
        updateCount();
    });
    connect(m_filter, &QLineEdit::returnPressed, this, [this] {
        m_typing->stop();
        if (m_model->isIndexed()) startSearch();
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) { show(i); });
    connect(m_tabs, &QTabBar::tabMoved, this, [this](int from, int to) {
        m_bases.move(from, to);
        syncBaseList();
        emit databasesChanged();
    });
    connect(m_table, &QWidget::customContextMenuRequested, this, &GameListPanel::showContextMenu);
    auto* del = new QAction(this);
    del->setShortcut(QKeySequence::Delete);
    del->setShortcutContext(Qt::WidgetShortcut);
    m_table->addAction(del);
    connect(del, &QAction::triggered, this, &GameListPanel::deleteSelected);
    // One click opens the game in the notation (not while extending the selection with Ctrl or Shift);
    // a double click opens its information. Enter opens the game too.
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& idx) {
        if (QGuiApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) return;
        emit gameActivated(sourceRow(idx));
    });
    connect(m_table, &QTableView::activated, this, [this](const QModelIndex& idx) {
        if (QGuiApplication::mouseButtons() != Qt::NoButton) return; // double click: handled below
        emit gameActivated(sourceRow(idx));
    });
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!m_model->isReadOnly()) emit editInfoRequested(sourceRow(idx));
    });
    // Dragging games onto another database's tab: they are added to it.
    m_table->setDragEnabled(true);
    m_table->setDragDropMode(QAbstractItemView::DragOnly);
    m_table->setDefaultDropAction(Qt::CopyAction);
    m_tabs->setAcceptDrops(true);
    m_tabs->installEventFilter(this);
    // ChessBase database: sorting is done in the database (in the background), not in the view.
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
        if (m_model->isIndexed()) sortIndexed(column);
    });
    updateCount();
}

void GameListPanel::sortIndexed(int column)
{
    const int i = currentIndex();
    if (i < 0 || !m_model->isIndexed()) return;
    Base& b = m_bases[i];
    if (b.sortColumn == column) b.sortOrder = b.sortOrder == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
    else {
        b.sortColumn = column;
        b.sortOrder = column == GameListModel::Beauty ? Qt::DescendingOrder : Qt::AscendingOrder; // the finest games first
    }
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    m_table->horizontalHeader()->setSortIndicator(b.sortColumn, b.sortOrder);
    startIndexedJob(Job::Sort);
}

bool GameListPanel::searchPosition(const chess::Position& pos)
{
    if (!m_model->isIndexed() || !m_model->indexed()->hasPositions()) return false;
    startIndexedJob(Job::Position, &pos);
    return true;
}

bool GameListPanel::searchByReplay(const QString& label, Scan scan)
{
    if (!m_model->isIndexed() || !dynamic_cast<const Hy64Source*>(m_model->indexed())) return false;
    m_scan = std::move(scan);
    m_scanLabel = label;
    startIndexedJob(Job::Replay);
    return true;
}

GameListPanel::~GameListPanel()
{
    if (m_search) {
        m_cancel->store(1);
        m_search->wait();
        delete m_search;
    }
}

int GameListPanel::sourceRow(const QModelIndex& index) const
{
    if (!index.isValid()) return -1;
    return index.model() == m_proxy ? m_proxy->mapToSource(index).row() : index.row();
}

void GameListPanel::stopSearch()
{
    if (!m_search) return;
    m_cancel->store(1);
    m_search->wait(); // the thread ends at the next block (a few milliseconds)
    m_search = nullptr; // it will be freed when it ends (finished)
    m_searchModel = nullptr;
    updateCount();
}

bool GameListPanel::searchHeaders(const hy64db::HeaderQuery& q, IndexedGameSource::Combine combine)
{
    if (currentIndex() < 0) return false;
    using C = IndexedGameSource::Combine;
    if (m_model->isIndexed()) {
        startIndexedJob(Job::Header, nullptr, &q, combine);
        return true;
    }
    std::vector<char> rows = m_model->matchHeaders(q);
    const std::vector<char>& current = m_model->rowFilter();
    if (!current.empty() && combine != C::Replace)
        for (size_t i = 0; i < rows.size() && i < current.size(); ++i)
            rows[i] = combine == C::Within ? (rows[i] && current[i]) : (rows[i] || current[i]);
    m_model->setRowFilter(std::move(rows));
    m_proxy->refilter();
    updateCount();
    emit statusMessage(tr("Recherche : %n partie(s)", nullptr, int(std::count(m_model->rowFilter().begin(), m_model->rowFilter().end(), 1))));
    return true;
}

void GameListPanel::setColumnVisible(int column, bool visible)
{
    if (column <= GameListModel::Number || column >= GameListModel::ColumnCount) return;
    m_table->horizontalHeader()->setSectionHidden(column, !visible);
    QVariantList hidden;
    for (int c = 0; c < GameListModel::ColumnCount; ++c)
        if (m_table->horizontalHeader()->isSectionHidden(c)) hidden << c;
    QSettings().setValue("gameList/hiddenColumns", hidden);
    arrangeColumns();
    fitColumns(); // the column that has just appeared, or the one no longer stretched
}

// Each column takes the width of its widest content (header included), without exceeding it:
// no more wasted room or cut text. Measured on the displayed rows and the few hundred
// following — not on millions of games. The layout then follows the width of the list
// (layoutColumns). A double-click on a column's edge refits it to what is on screen.
void GameListPanel::fitColumns()
{
    QHeaderView* header = m_table->horizontalHeader();
    // (empty list: width of the headers, so that the columns still fill the list)
    if (!m_table->model() || header->count() < GameListModel::ColumnCount) return;
    header->setResizeContentsPrecision(300);
    m_naturalWidths.resize(GameListModel::ColumnCount);
    for (int c = 0; c < GameListModel::ColumnCount; ++c) {
        // (sizeHintForColumn is public in QAbstractItemView, protected in QTableView)
        // Header: its text and margins, without the room Qt reserves by default for the sort arrow
        // (it would widen all the short columns: Elo, Res., ECO…).
        const QString title = m_table->model()->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
        int width = std::max(static_cast<QAbstractItemView*>(m_table)->sizeHintForColumn(c) + 12, header->fontMetrics().horizontalAdvance(title) + 22);
        if (c == GameListModel::Number && m_model->isIndexed()) // numbers up to eight digits further down in the database
            width = std::max(width, m_table->fontMetrics().horizontalAdvance(QStringLiteral("88888888")) + 18);
        m_naturalWidths[c] = std::min(width, 420);
    }
    layoutColumns();
}

// Shares out the width of the list between the displayed columns, so that they all fit without
// a scroll bar, whatever the width:
//  - enough room: each column at the width of its content, the rest to the end column;
//  - tight room: the text columns (White, Black, Event, Site) shrink together, in
//    proportion (text cut by "…"), the short columns keeping their whole content;
//  - very narrow: all the columns shrink in proportion.
// Redone at each change of the list's width (eventFilter) and of the displayed columns.
void GameListPanel::layoutColumns()
{
    QHeaderView* header = m_table->horizontalHeader();
    if (m_naturalWidths.size() != GameListModel::ColumnCount || header->count() < GameListModel::ColumnCount) return;
    const int available = m_table->viewport()->width();
    if (available <= 0) return;
    constexpr int MinText = 48, MinAny = 16;
    QList<int> shown;
    for (int c = 0; c < GameListModel::ColumnCount; ++c)
        if (!header->isSectionHidden(c)) shown << c;
    if (shown.isEmpty()) return;
    const int filler = shown.contains(m_fillerColumn) ? m_fillerColumn : shown.last();
    // A width set by hand replaces the measured one and is not shrunk with the text columns
    // (the end column excepted: it always takes what remains).
    auto isManual = [&](int c) { return c != filler && m_manualWidths.contains(c); };
    auto natural = [&](int c) { return isManual(c) ? m_manualWidths.value(c) : m_naturalWidths[c]; };
    auto isText = [&](int c) {
        return !isManual(c) && (c == GameListModel::White || c == GameListModel::Black || c == GameListModel::Event || c == GameListModel::Site);
    };
    int fixed = 0, text = 0, texts = 0;
    for (int c : std::as_const(shown)) {
        (isText(c) ? text : fixed) += natural(c);
        texts += isText(c);
    }
    QList<int> widths(GameListModel::ColumnCount, 0);
    if (fixed + text <= available) {
        for (int c : std::as_const(shown)) widths[c] = natural(c);
    } else if (fixed + texts * MinText <= available) {
        const double scale = double(available - fixed) / text;
        for (int c : std::as_const(shown)) widths[c] = isText(c) ? std::max(MinText, int(natural(c) * scale)) : natural(c);
    } else {
        const double scale = double(available) / (fixed + texts * MinText);
        for (int c : std::as_const(shown)) widths[c] = std::max(MinAny, int((isText(c) ? MinText : natural(c)) * scale));
    }
    // What remains (free room, rounding) goes to the end column: the list is filled to the pixel.
    int used = 0;
    for (int c : std::as_const(shown)) used += widths[c];
    widths[filler] = std::max(MinAny, widths[filler] + available - used);
    m_layingOut = true;
    for (int c : std::as_const(shown))
        if (header->sectionSize(c) != widths[c]) header->resizeSection(c, widths[c]);
    m_layingOut = false;
}

void GameListPanel::saveManualWidths()
{
    QVariantMap map;
    for (auto it = m_manualWidths.begin(); it != m_manualWidths.end(); ++it) map.insert(QString::number(it.key()), it.value());
    QSettings().setValue("gameList/columnWidths", map);
}

// "Round" is displayed last and receives the remaining width (layoutColumns). When it is
// hidden, it is the first displayed column among Event, Site, Black, White, in its place: there is
// never a gap on the right.
void GameListPanel::arrangeColumns()
{
    QHeaderView* header = m_table->horizontalHeader();
    if (header->count() < GameListModel::ColumnCount) return; // no model yet
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(16);
    const int at = header->visualIndex(GameListModel::Round);
    if (at != GameListModel::ColumnCount - 1) header->moveSection(at, GameListModel::ColumnCount - 1);
    m_fillerColumn = -1;
    for (int c : {GameListModel::Round, GameListModel::Event, GameListModel::Site, GameListModel::Black, GameListModel::White})
        if (!header->isSectionHidden(c)) {
            m_fillerColumn = c;
            break;
        }
    for (int c = 0; c < GameListModel::ColumnCount; ++c) header->setSectionResizeMode(c, QHeaderView::Interactive);
    layoutColumns();
}

bool GameListPanel::isColumnVisible(int column) const { return !m_table->horizontalHeader()->isSectionHidden(column); }

QList<int> GameListPanel::visibleRows() const
{
    QList<int> rows;
    const QAbstractItemModel* view = m_table->model();
    const int n = view->rowCount();
    rows.reserve(n);
    for (int r = 0; r < n; ++r) rows << sourceRow(view->index(r, 0));
    return rows;
}

int GameListPanel::randomRow() const
{
    const int n = m_table->model()->rowCount();
    if (n <= 0) return -1;
    return sourceRow(m_table->model()->index(int(QRandomGenerator::global()->bounded(n)), 0));
}

int GameListPanel::rowOfNumber(int number)
{
    if (currentIndex() < 0 || number < 1) return -1;
    if (m_model->isIndexed()) {
        int row = m_model->indexed()->rowOfNumber(number);
        if (row < 0 && number <= m_model->totalGames() + m_model->indexed()->deletedCount()) { // outside the search: the whole database
            clearFilter();
            waitForSearch();
            row = m_model->indexed()->rowOfNumber(number);
        }
        return row;
    }
    if (number > m_model->rowCount()) return -1;
    if (!m_model->rowAllowed(number - 1) || !m_proxy->mapFromSource(m_model->index(number - 1, 0)).isValid()) clearFilter();
    return number - 1;
}

void GameListPanel::waitForSearch()
{
    if (m_search) m_search->wait();
    QCoreApplication::processEvents(); // result installed (finished signal)
}

bool GameListPanel::negateFilter()
{
    if (currentIndex() < 0) return false;
    if (m_model->isIndexed()) {
        startIndexedJob(Job::Negate);
        return true;
    }
    std::vector<char> rows = m_model->rowFilter();
    if (rows.empty()) rows.assign(size_t(m_model->rowCount()), 1);
    for (char& c : rows) c = !c;
    m_model->setRowFilter(std::move(rows));
    m_proxy->refilter();
    updateCount();
    return true;
}

void GameListPanel::clearFilter()
{
    if (currentIndex() < 0) return;
    {
        QSignalBlocker block(m_filter);
        m_filter->clear();
    }
    m_bases[currentIndex()].filter.clear();
    if (m_model->isIndexed()) {
        stopSearch();
        startIndexedJob(Job::Sort);
        return;
    }
    m_model->setRowFilter({});
    m_proxy->setFilterFixedString(QString());
    m_proxy->refilter();
    updateCount();
}

void GameListPanel::startIndexedJob(Job job, const chess::Position* position, const hy64db::HeaderQuery* query, IndexedGameSource::Combine combine)
{
    GameListModel* model = m_model;
    const int i = currentIndex();
    if (i < 0 || !model->isIndexed()) return;
    stopSearch();
    const IndexedGameSource* source = model->indexed();
    const QString text = m_filter->text().trimmed();
    // New search (text or position), or the current view: result or the whole database.
    const bool filtered = job == Job::Position || job == Job::Header || job == Job::Negate || job == Job::Replay
                          || (job == Job::Search ? !text.isEmpty() : (source->isFiltered() && !text.isEmpty()));
    const int column = m_bases[i].sortColumn;
    const bool ascending = m_bases[i].sortOrder == Qt::AscendingOrder;
    const bool sorted = column > GameListModel::Number || (column == GameListModel::Number && !ascending);
    if (!filtered && !sorted) { // the whole database, in its order: immediate
        model->setIndexedView(nullptr, false);
        updateCount();
        return;
    }
    if (job == Job::Search && !filtered) job = Job::Sort;
    auto cancel = std::make_shared<std::atomic<int32_t>>(0);
    auto result = std::make_shared<std::unique_ptr<IndexedGameSource::View>>();
    auto error = std::make_shared<QString>();
    if (job == Job::Sort) *result = source->viewIds(!filtered); // copy, taken in the interface thread
    // Current view (combination of searches, negation): copied here too
    std::shared_ptr<IndexedGameSource::View> current;
    if ((job == Job::Header && combine != IndexedGameSource::Combine::Replace && source->isFiltered()) || job == Job::Negate)
        current = source->viewIds(!source->isFiltered());
    const hy64db::HeaderQuery q = query ? *query : hy64db::HeaderQuery{};
    const chess::Position pos = position ? *position : chess::Position();
    const Scan scan = m_scan;
    const QString scanLabel = m_scanLabel;
    QThread* thread = QThread::create([source, text, job, pos, q, combine, current, column, ascending, cancel, result, error, scan] {
        if (job == Job::Replay) {
            std::vector<uint32_t> ids = scan(static_cast<const Hy64Source*>(source)->database(), cancel.get());
            if (!cancel->load()) *result = Hy64Source::viewOf(std::move(ids));
        }
        if (job == Job::Search) *result = source->search(text, cancel.get(), error.get());
        else if (job == Job::Position) *result = source->searchPosition(pos, cancel.get(), error.get());
        else if (job == Job::Header) *result = source->searchHeaders(q, combine, current.get(), cancel.get(), error.get());
        else if (job == Job::Negate && current) *result = source->negate(*current);
        if (*result && !cancel->load() && column >= 0 && !source->sort(**result, column, ascending, cancel.get(), error.get())) result->reset();
    });
    m_search = thread;
    m_searchModel = model;
    m_cancel = cancel;
    m_job = job;
    connect(thread, &QThread::finished, this, [this, thread, model, cancel, result, error, text, job, filtered, scanLabel] {
        thread->deleteLater();
        if (m_search == thread) {
            m_search = nullptr;
            m_searchModel = nullptr;
        }
        if (cancel->load()) return; // interrupted (other task, database closed)
        if (!*result) {
            emit statusMessage((job == Job::Sort ? tr("Tri impossible : %1") : tr("Recherche impossible : %1")).arg(*error));
            updateCount();
            return;
        }
        const int n = (*result)->count();
        model->setIndexedView(std::move(*result), filtered);
        updateCount();
        if (job == Job::Search) emit statusMessage(tr("« %1 » : %n partie(s)", nullptr, n).arg(text));
        else if (job == Job::Position) emit statusMessage(tr("Position de l'échiquier : %n partie(s)", nullptr, n));
        else if (job == Job::Header || job == Job::Negate) emit statusMessage(tr("Recherche : %n partie(s)", nullptr, n));
        else if (job == Job::Replay) emit statusMessage(tr("%1 : %n partie(s)", nullptr, n).arg(scanLabel));
    });
    thread->start();
    updateCount();
}

bool GameListPanel::openFile(const QString& path, QString* error)
{
    if (activate(path)) {
        // Already open: the file is reread (it may have changed on disk).
        if (!m_model->loadFile(path, error)) return false;
        updateCount();
        return true;
    }
    auto* model = new GameListModel(this);
    if (!model->loadFile(path, error)) {
        delete model;
        return false;
    }
    m_bases << Base{model, {}};
    addTab(path);
    m_tabs->setCurrentIndex(int(m_bases.size()) - 1); // → show()
    return true;
}

void GameListPanel::addTab(const QString& path)
{
    const int i = m_tabs->addTab(QFileInfo(path).fileName());
    m_tabs->setTabToolTip(i, QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())
                                 );
    auto* close = new QToolButton;
    close->setIcon(theme::icon("close"));
    close->setIconSize(QSize(12, 12));
    close->setAutoRaise(true);
    close->setToolTip(tr("Fermer la base"));
    close->setCursor(Qt::ArrowCursor);
    connect(close, &QToolButton::clicked, this, [this, close] {
        for (int t = 0; t < m_tabs->count(); ++t)
            if (m_tabs->tabButton(t, QTabBar::RightSide) == close) {
                closeDatabase(t);
                return;
            }
    });
    m_tabs->setTabButton(i, QTabBar::RightSide, close);
    syncBaseList();
}

// Open databases as a list (one row per database) or as large icons flowing across the width.
void GameListPanel::setBasesAsIcons(bool icons)
{
    m_basesAsIcons = icons;
    QSettings().setValue("gameList/basesAsIcons", icons);
    m_basesListButton->setChecked(!icons);
    m_basesIconsButton->setChecked(icons);
    m_baseList->setViewMode(icons ? QListView::IconMode : QListView::ListMode);
    m_baseList->setFlow(icons ? QListView::LeftToRight : QListView::TopToBottom);
    m_baseList->setWrapping(icons);
    m_baseList->setResizeMode(QListView::Adjust);
    m_baseList->setMovement(QListView::Static);
    m_baseList->setIconSize(icons ? QSize(48, 48) : QSize(16, 16));
    m_baseList->setGridSize(icons ? QSize(124, 100) : QSize());
    m_baseList->setWordWrap(icons);
    m_baseList->setSpacing(icons ? 4 : 0);
    // setMovement(Static) disables dropping: we restore it (dragging games onto a database).
    m_baseList->setAcceptDrops(true);
    m_baseList->viewport()->setAcceptDrops(true);
    syncBaseList();
}

// List of the open databases, rebuilt from the tabs: one entry per database (its icon, its name,
// the path as a tooltip; in list mode, a close button), the current database selected.
void GameListPanel::syncBaseList()
{
    if (!m_baseList) return;
    const QSignalBlocker quiet(m_baseList);
    m_dropRow = -1;
    m_baseList->clear();
    for (int t = 0; t < m_tabs->count(); ++t) {
        const QString path = t < m_bases.size() ? m_bases[t].model->filePath() : QString();
        auto* item = new QListWidgetItem(baseicons::icon(baseicons::chosen(path)), m_tabs->tabText(t));
        item->setToolTip(m_tabs->tabToolTip(t));
        m_baseList->addItem(item);
        auto* close = new QToolButton;
        close->setObjectName("DatabaseClose");
        close->setIcon(theme::icon("close"));
        close->setIconSize(QSize(12, 12));
        close->setAutoRaise(true);
        close->setToolTip(tr("Fermer la base"));
        close->setCursor(Qt::ArrowCursor);
        connect(close, &QToolButton::clicked, this, [this, item] {
            const int at = m_baseList->row(item);
            if (at >= 0) QTimer::singleShot(0, this, [this, at] { closeDatabase(at); }); // the list is rebuilt: not during the click
        });
        if (m_basesAsIcons) { // large icon, name below; the close cross appears under the pointer
            item->setSizeHint(QSize(116, 92));
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            m_baseList->setItemWidget(item, new HoverCross(close));
            continue;
        }
        item->setSizeHint(QSize(0, 30));
        auto* row = new QWidget;
        row->setAttribute(Qt::WA_TranslucentBackground);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 6, 0);
        rowLayout->addStretch(1);
        rowLayout->addWidget(close);
        m_baseList->setItemWidget(item, row);
    }
    m_baseList->setCurrentRow(m_tabs->currentIndex());
    m_baseList->doItemsLayout(); // at once: a database being dragged is looked up under the pointer right after
}

int GameListPanel::indexOf(const QString& path) const
{
    for (int i = 0; i < m_bases.size(); ++i)
        if (QFileInfo(m_bases[i].model->filePath()) == QFileInfo(path)) return i;
    return -1;
}

bool GameListPanel::activate(const QString& path)
{
    const int i = indexOf(path);
    if (i < 0) return false;
    m_tabs->setCurrentIndex(i);
    return true;
}

void GameListPanel::reload(const QString& path)
{
    const int i = indexOf(path);
    if (i < 0) return;
    if (!m_bases[i].model->loadFile(m_bases[i].model->filePath(), nullptr)) return;
    if (i == currentIndex()) updateCount();
}

int GameListPanel::currentIndex() const { return m_bases.isEmpty() ? -1 : m_tabs->currentIndex(); }

GameListModel* GameListPanel::modelFor(const QString& path) const
{
    const int i = indexOf(path);
    return i < 0 ? nullptr : m_bases[i].model;
}

QStringList GameListPanel::openPaths() const
{
    QStringList paths;
    for (const Base& b : m_bases) paths << b.model->filePath();
    return paths;
}

// Single-file database whose journal has grown: moved into the body of the file, with a
// wait dialog. Nothing depends on this step for data safety: everything is already written, and
// the file is only replaced once its new version is complete.
bool GameListPanel::tidyDatabase(GameListModel* model, bool always)
{
    auto* b = model ? dynamic_cast<Hy64Source*>(model->indexed()) : nullptr;
    if (!b || !b->database().isPacked() || b->database().journalBytes() == 0) return true;
    if (!always && b->database().journalBytes() < quint64(TidyThreshold)) return true;
    if (m_searchModel == model) stopSearch();
    QProgressDialog wait(tr("Rangement du journal de « %1 »…\nVos modifications sont déjà enregistrées ; cette étape les range dans le corps de la base.")
                             .arg(QFileInfo(model->filePath()).fileName()),
                         QString(), 0, 0, this);
    wait.setWindowTitle(tr("Fermeture de la base"));
    wait.setWindowModality(Qt::WindowModal);
    wait.setMinimumDuration(300);
    bool ok = false;
    std::string err;
    QEventLoop loop;
    // The tidying closes then reopens the database in its thread, while the event loop
    // runs here: the list must no longer read it (a simple display refresh used to read
    // a record of a database being closed — crash met on 21 November 2026).
    // The model is put to sleep rather than detached from its view: several views may read it.
    model->setSuspended(true);
    QThread* thread = QThread::create([&] { ok = b->database().tidy(&err); });
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec();
    thread->wait();
    delete thread;
    model->setSuspended(false);
    wait.close();
    if (!ok) emit statusMessage(tr("Journal de « %1 » non rangé (%2) : la base reste intacte et complète").arg(QFileInfo(model->filePath()).fileName(), QString::fromStdString(err)));
    return ok;
}

void GameListPanel::tidyAll()
{
    for (const Base& base : std::as_const(m_bases)) tidyDatabase(base.model, false);
}

void GameListPanel::closeDatabase(int index)
{
    if (index < 0 || index >= m_bases.size()) return;
    GameListModel* model = m_bases[index].model;
    if (m_searchModel == model) stopSearch(); // the search reads this database
    tidyDatabase(model, false);
    m_bases.removeAt(index);
    if (m_model == model) { // display nothing more of this database before destroying it
        m_model = m_empty;
        m_proxy->setSourceModel(m_empty);
    }
    m_tabs->removeTab(index); // → show() of the neighbouring tab
    syncBaseList();
    model->deleteLater();
    if (m_bases.isEmpty()) show(-1);
}

void GameListPanel::show(int index)
{
    if (index >= m_bases.size()) return; // tab being added
    m_model = index < 0 ? m_empty : m_bases[index].model;
    // ChessBase database (up to millions of games): the table reads the model directly,
    // with no row-by-row sort or filter; the filter field becomes a search.
    const bool direct = m_model->isIndexed();
    QAbstractItemModel* shown = direct ? static_cast<QAbstractItemModel*>(m_model) : m_proxy;
    if (m_proxy->sourceModel() != (direct ? m_empty : m_model)) m_proxy->setSourceModel(direct ? m_empty : m_model);
    if (m_table->model() != shown) {
        QList<int> widths;
        for (int c = 0; c < GameListModel::ColumnCount; ++c) widths << m_table->columnWidth(c);
        m_table->setModel(shown);
        for (int c = 0; c < widths.size(); ++c) m_table->setColumnWidth(c, widths[c]);
        arrangeColumns(); // the column order starts again from the model's
    }
    if (direct) { // ChessBase numbers: up to eight digits
        const int needed = m_table->fontMetrics().horizontalAdvance(QStringLiteral("88888888")) + 18;
        if (m_table->columnWidth(GameListModel::Number) < needed) m_table->setColumnWidth(GameListModel::Number, needed);
    }
    QHeaderView* header = m_table->horizontalHeader();
    if (direct) { // sort in the database (click on a header), remembered for each database
        m_table->setSortingEnabled(false);
        header->setSectionsClickable(true);
        const Base& b = m_bases[index];
        header->setSortIndicatorShown(b.sortColumn >= 0);
        header->setSortIndicator(b.sortColumn, b.sortOrder);
    } else { // PGN: sort of the view, as before
        header->setSortIndicator(m_proxy->sortColumn(), m_proxy->sortOrder());
        header->setSortIndicatorShown(true);
        m_table->setSortingEnabled(true);
    }
    m_filter->setPlaceholderText(direct ? tr("Rechercher un joueur ou un tournoi…") : tr("Filtrer : joueur, tournoi, ECO…"));
    m_typing->stop(); // a pending keystroke concerned the database being left
    {
        QSignalBlocker block(m_filter);
        m_filter->setText(index < 0 ? QString() : m_bases[index].filter);
    }
    if (!direct) m_proxy->setFilterFixedString(m_filter->text());
    m_filter->setEnabled(index >= 0);
    updateCount();
    // Widths from the content: when the database is shown, then at each new content (search, sort).
    disconnect(m_fitOnReset);
    m_fitOnReset = connect(shown, &QAbstractItemModel::modelReset, this, [this] { QTimer::singleShot(0, this, &GameListPanel::fitColumns); });
    fitColumns();
    emit databasesChanged();
}

void GameListPanel::updateCount()
{
    if (m_model->isIndexed()) {
        const QLocale loc;
        const int total = m_model->totalGames(), shown = m_model->rowCount();
        if (m_search && m_searchModel == m_model) m_count->setText(m_job == Job::Sort ? tr("Tri…") : tr("Recherche…"));
        else if (!m_model->indexed()->isFiltered()) m_count->setText(tr("%1 parties").arg(loc.toString(total)));
        else m_count->setText(tr("%1 / %2 parties").arg(loc.toString(shown), loc.toString(total)));
        if (const int deleted = m_model->indexed()->deletedCount(); deleted > 0 && !(m_search && m_searchModel == m_model))
            m_count->setText(m_count->text() + tr(" · %1 supprimée(s)").arg(loc.toString(deleted)));
        // Single-file database: additions to the journal (already saved, tidied on closing)
        QString tip;
        if (auto* b = dynamic_cast<Hy64Source*>(m_model->indexed()); b && b->database().journalBytes() > 0) {
            m_count->setText(m_count->text() + tr(" · journal : %1").arg(QLocale().formattedDataSize(qint64(b->database().journalBytes()))));
            tip = tr("Base en un fichier : %1 partie(s) ajoutée(s) depuis le dernier rangement (%2 dans le journal en fin de fichier).\n"
                     "Tout est déjà enregistré sur le disque : rien n'est à sauver avant de fermer ou de quitter.\n"
                     "Le journal sera rangé dans le corps de la base à sa fermeture s'il dépasse %3, ou par Base → Compacter la base.")
                      .arg(b->database().journalGames())
                      .arg(QLocale().formattedDataSize(qint64(b->database().journalBytes())), QLocale().formattedDataSize(TidyThreshold));
        }
        m_count->setToolTip(tip);
        return;
    }
    m_count->setToolTip(QString());
    const int total = m_model->rowCount(), shown = m_proxy->rowCount();
    if (total == 0) m_count->setText(tr("Aucune base ouverte"));
    else if (shown == total) m_count->setText(tr("%n partie(s)", nullptr, total));
    else m_count->setText(tr("%1 / %2 parties").arg(shown).arg(total));
}

int GameListPanel::dropTab(const QMimeData* mime, const QPoint& pos) const { return dropBase(mime, m_tabs->tabAt(pos)); }

// Does database no. `i` accept the dragged games? Returns `i`, or -1.
int GameListPanel::dropBase(const QMimeData* mime, int i) const
{
    if (!mime || !mime->hasFormat(GameListModel::MimeType)) return -1;
    if (i < 0 || i >= m_bases.size()) return -1;
    QDataStream in(mime->data(GameListModel::MimeType));
    QString from;
    in >> from;
    const GameListModel* target = m_bases[i].model;
    if (target->isReadOnly() || QFileInfo(target->filePath()) == QFileInfo(from)) return -1;
    return i;
}

void GameListPanel::highlightTab(int index)
{
    if (index == m_highlighted) return;
    m_highlighted = index;
    if (!m_dropMark) {
        m_dropMark = new QWidget(m_tabs);
        m_dropMark->setObjectName("DropMark");
        m_dropMark->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_dropMark->setAttribute(Qt::WA_StyledBackground);
        m_dropMark->setStyleSheet(QStringLiteral("#DropMark { background: rgba(230,138,53,64); border: 1px solid rgb(230,138,53); border-radius: 3px; }"));
    }
    if (index < 0 || index >= m_tabs->count()) {
        m_dropMark->hide();
        return;
    }
    m_dropMark->setGeometry(m_tabs->tabRect(index).adjusted(1, 1, -1, -1));
    m_dropMark->raise();
    m_dropMark->show();
}

// Row of the database list marked during a drag (the database that would receive the games).
void GameListPanel::highlightBase(int row)
{
    if (!m_baseList || row == m_dropRow) return;
    if (QListWidgetItem* old = m_baseList->item(m_dropRow)) old->setBackground(QBrush());
    m_dropRow = row;
    if (QListWidgetItem* item = m_baseList->item(row)) item->setBackground(QColor(230, 138, 53, 64));
}

bool GameListPanel::eventFilter(QObject* watched, QEvent* event)
{
    // Games dragged onto a database: onto its row of the list, or onto its tab (hidden bar).
    if (watched == m_table->viewport()) { // the list changes width: the columns are laid out again
        if (event->type() == QEvent::Resize) layoutColumns();
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_table->horizontalHeader()->viewport()) { // a section resized between press and release: by hand
        if (event->type() == QEvent::MouseButtonPress) m_headerPressed = true;
        else if (event->type() == QEvent::MouseButtonRelease) m_headerPressed = false;
        return QWidget::eventFilter(watched, event);
    }
    const bool onList = m_baseList && watched == m_baseList->viewport();
    if (watched != m_tabs && !onList) return QWidget::eventFilter(watched, event);
    // Open databases reordered with the mouse: a database dragged over another takes its place
    // at once (the tab bar, hidden, keeps the order: see tabMoved).
    if (onList) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* e = static_cast<QMouseEvent*>(event);
            m_reordering = false;
            m_dragRow = e->button() == Qt::LeftButton ? m_baseList->row(m_baseList->itemAt(e->position().toPoint())) : -1;
            m_dragStart = e->position().toPoint();
            return false; // the press selects the database as usual
        }
        case QEvent::MouseMove: {
            auto* e = static_cast<QMouseEvent*>(event);
            if (m_dragRow < 0 || !(e->buttons() & Qt::LeftButton)) return false;
            const QPoint at = e->position().toPoint();
            if (!m_reordering) {
                if ((at - m_dragStart).manhattanLength() < QApplication::startDragDistance()) return true;
                m_reordering = true;
                m_baseList->viewport()->setCursor(Qt::ClosedHandCursor);
            }
            const int over = m_baseList->row(m_baseList->itemAt(at));
            if (over >= 0 && over != m_dragRow && m_dragRow < m_tabs->count() && over < m_tabs->count()) {
                const int from = m_dragRow;
                m_dragRow = over;
                m_tabs->moveTab(from, over); // → tabMoved: m_bases and the list follow
            }
            return true; // (otherwise the selection would follow the pointer)
        }
        case QEvent::MouseButtonRelease: {
            const bool was = m_reordering;
            m_reordering = false;
            m_dragRow = -1;
            if (was) m_baseList->viewport()->unsetCursor();
            return was;
        }
        default: break;
        }
    }
    auto target = [&](const QMimeData* mime, const QPoint& pos) {
        return onList ? dropBase(mime, m_baseList->row(m_baseList->itemAt(pos))) : dropTab(mime, pos);
    };
    auto mark = [&](int i) {
        if (onList) highlightBase(i);
        else highlightTab(i);
    };
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* e = static_cast<QDragMoveEvent*>(event);
        const int i = target(e->mimeData(), e->position().toPoint());
        mark(i);
        if (e->mimeData()->hasFormat(GameListModel::MimeType)) {
            // DragEnter accepted in order to receive the DragMove events; each position says whether the database accepts.
            if (event->type() == QEvent::DragEnter) e->acceptProposedAction();
            if (i >= 0) e->accept(onList ? m_baseList->visualItemRect(m_baseList->item(i)) : m_tabs->tabRect(i));
            else e->ignore(onList ? m_baseList->viewport()->rect() : m_tabs->rect());
            if (i >= 0) e->setDropAction(Qt::CopyAction);
            return true;
        }
        return false;
    }
    case QEvent::DragLeave:
        mark(-1);
        return false;
    case QEvent::Drop: {
        auto* e = static_cast<QDropEvent*>(event);
        mark(-1);
        const int i = target(e->mimeData(), e->position().toPoint());
        if (i < 0) return false;
        QDataStream in(e->mimeData()->data(GameListModel::MimeType));
        QString from;
        QList<int> rows;
        in >> from >> rows;
        e->setDropAction(Qt::CopyAction);
        e->accept();
        const QString to = m_bases[i].model->filePath();
        QString error;
        if (copyGames(from, rows, to, &error) < 0)
            QMessageBox::warning(this, tr("Ajouter les parties"), tr("Impossible d'ajouter les parties à « %1 » :\n%2").arg(QFileInfo(to).fileName(), error));
        return true;
    }
    default: return QWidget::eventFilter(watched, event);
    }
}

int GameListPanel::copyGames(const QString& from, const QList<int>& rows, const QString& to, QString* error)
{
    GameListModel* source = modelFor(from);
    GameListModel* target = modelFor(to);
    if (!source || !target || source == target || target->isReadOnly()) {
        if (error) *error = tr("base de destination non modifiable");
        return -1;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int added = target->copyFrom(*source, rows, error); // two Hydra64 databases: without going through PGN
    if (added == -2) {
        std::vector<std::string> pgns;
        pgns.reserve(size_t(rows.size()));
        for (int r : rows) {
            if (r < 0 || r >= source->rowCount() || source->isDeleted(r)) continue;
            const QString text = source->gameText(r);
            if (!text.isEmpty()) pgns.push_back(text.toStdString());
        }
        added = target->appendGames(pgns, error);
    }
    QApplication::restoreOverrideCursor();
    if (added < 0) return -1;
    if (target == m_model) updateCount();
    emit statusMessage(tr("%n partie(s) ajoutée(s) à %1", nullptr, added).arg(QFileInfo(to).fileName()));
    return added;
}

QList<int> GameListPanel::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex& idx : m_table->selectionModel()->selectedRows()) rows << sourceRow(idx);
    std::sort(rows.begin(), rows.end());
    return rows;
}

void GameListPanel::showContextMenu(const QPoint& pos)
{
    const QModelIndex idx = m_table->indexAt(pos);
    if (!idx.isValid()) return;
    // Right-click outside the selection: the clicked row is selected.
    if (!m_table->selectionModel()->isRowSelected(idx.row(), idx.parent())) m_table->selectRow(idx.row());
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) return;

    QMenu menu(this);
    QAction* preview = menu.addAction(theme::icon("board"), tr("Aperçu…"));
    preview->setEnabled(rows.size() == 1);
    connect(preview, &QAction::triggered, this, [this, row = rows.first()] { emit previewRequested(row); });
    QAction* send = menu.addAction(theme::icon("analysis"), tr("Envoyer dans l'analyse"));
    send->setEnabled(rows.size() == 1);
    QAction* info = menu.addAction(theme::icon("edit"), tr("Modifier les informations…"));
    info->setEnabled(rows.size() == 1 && !m_model->isReadOnly());
    QAction* copy = menu.addAction(theme::icon("copy"), rows.size() == 1 ? tr("Copier le PGN") : tr("Copier le PGN des %1 parties").arg(rows.size()));
    connect(menu.addAction(theme::icon("copy"), rows.size() == 1 ? tr("Copier dans la base presse-papiers") : tr("Copier les %1 parties dans la base presse-papiers").arg(rows.size())),
            &QAction::triggered, this, [this, rows] { emit clipboardRequested(rows); });
    // Merge: several games into one with variations (opening file), or into the analysis game
    QMenu* merge = menu.addMenu(tr("Fusionner"));
    QAction* mergeNew = merge->addAction(rows.size() == 1 ? tr("En une nouvelle partie (page Analyse)") : tr("Les %1 parties en une seule (page Analyse)").arg(rows.size()));
    mergeNew->setEnabled(rows.size() >= 2);
    connect(mergeNew, &QAction::triggered, this, [this, rows] { emit mergeRequested(rows, false); });
    connect(merge->addAction(rows.size() == 1 ? tr("Dans la partie de la page Analyse") : tr("Les %1 parties dans la partie de la page Analyse").arg(rows.size())), &QAction::triggered, this,
            [this, rows] { emit mergeRequested(rows, true); });
    QAction* html = menu.addAction(theme::icon("save"), tr("Exporter en page HTML…"));
    html->setEnabled(rows.size() == 1);
    connect(html, &QAction::triggered, this, [this, row = rows.first()] { emit exportHtmlRequested(row); });
    QAction* annotate = menu.addAction(theme::icon("engine"), rows.size() == 1 ? tr("Annoter la partie…") : tr("Annoter les %1 parties…").arg(rows.size()));
    // Profile of the game's players (Hydra64 databases)
    if (m_model->isIndexed() && rows.size() == 1) {
        for (int col : {GameListModel::White, GameListModel::Black}) {
            const QString name = m_model->data(m_model->index(rows.first(), col), Qt::DisplayRole).toString();
            if (!name.isEmpty() && name != QLatin1String("?"))
                menu.addAction(theme::icon("search"), tr("Fiche de %1").arg(name), this, [this, name] { emit playerReportRequested(name); });
        }
    }
    if (m_model->isIndexed() && rows.size() == 1) {
        const int row = rows.first();
        menu.addAction(theme::icon("tournament"), tr("Tableau croisé du tournoi"), this, [this, row] { emit eventTableRequested(row); });
    }
    // Flags (Hydra64 databases): ticked according to the first selected game
    QMenu* flags = menu.addMenu(tr("Drapeaux"));
    flags->setEnabled(m_model->supportsUserFlags());
    if (!m_model->supportsUserFlags()) flags->setToolTip(tr("Drapeaux : bases Hydra64 seulement"));
    const uint8_t current = m_model->supportsUserFlags() ? m_model->userFlags(rows.first()) : 0;
    for (int f = 0; f < hy64db::UserFlagCount; ++f) {
        QAction* a = flags->addAction(QStringLiteral("%1 — %2").arg(GameListModel::userFlagLetter(f), GameListModel::userFlagName(f)));
        a->setCheckable(true);
        a->setChecked(current & (1 << f));
        connect(a, &QAction::toggled, this, [this, rows, f](bool on) {
            QString error;
            if (!m_model->setUserFlag(rows, f, on, &error)) emit statusMessage(tr("Drapeau non enregistré : %1").arg(error));
        });
    }
    annotate->setEnabled(!m_model->isReadOnly());
    menu.addSeparator();
    // Hydra64 database: reversible deletion (game struck through); Del toggles.
    const bool indexed = m_model->isIndexed();
    const bool allDeleted = indexed && std::all_of(rows.begin(), rows.end(), [&](int r) { return m_model->isDeleted(r); });
    const bool anyDeleted = indexed && std::any_of(rows.begin(), rows.end(), [&](int r) { return m_model->isDeleted(r); });
    QAction* del = nullptr;
    if (!allDeleted) {
        const QString dots = indexed ? QString() : QStringLiteral("…");
        del = menu.addAction(theme::icon("trash"), (rows.size() == 1 ? tr("Supprimer la partie") : tr("Supprimer les %1 parties").arg(rows.size())) + dots);
        del->setShortcut(QKeySequence::Delete);
        del->setEnabled(!m_model->isReadOnly());
    }
    QAction* restore = nullptr;
    if (anyDeleted) {
        restore = menu.addAction(theme::icon("undo"), rows.size() == 1 ? tr("Restaurer la partie") : tr("Restaurer les parties supprimées"));
        if (allDeleted) restore->setShortcut(QKeySequence::Delete);
    }
    if (m_model->isReadOnly()) {
        info->setToolTip(tr("Base en lecture seule (ChessBase, ou Hydra64 en un fichier)"));
        if (del) del->setToolTip(tr("Base en lecture seule (ChessBase, ou Hydra64 en un fichier)"));
        menu.setToolTipsVisible(true);
    }

    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosen == send) {
        emit sendToAnalysisRequested(rows.first());
    } else if (chosen == info) {
        emit editInfoRequested(rows.first());
    } else if (chosen == copy) {
        QStringList texts;
        for (int r : rows) texts << m_model->gameText(r);
        QApplication::clipboard()->setText(texts.join(QStringLiteral("\n\n")) + '\n');
        emit statusMessage(tr("PGN copié"));
    } else if (chosen == annotate) {
        emit annotateRequested(rows);
    } else if (chosen && chosen == del) {
        deleteSelected();
    } else if (chosen && chosen == restore) {
        restoreSelected();
    }
}

void GameListPanel::deleteSelected()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) return;
    if (m_model->isReadOnly()) {
        emit statusMessage(tr("Base en lecture seule : les parties ne peuvent pas y être supprimées"));
        return;
    }
    const QString file = QFileInfo(m_model->filePath()).fileName();
    if (m_model->isIndexed()) {
        // Hydra64 database: marked deleted, displayed struck through, restorable until compaction.
        if (std::all_of(rows.begin(), rows.end(), [&](int r) { return m_model->isDeleted(r); })) {
            restoreSelected();
            return;
        }
        QString error;
        if (!m_model->removeGames(rows, &error)) {
            QMessageBox::warning(this, tr("Supprimer"), tr("Impossible de modifier « %1 » :\n%2").arg(file, error));
            return;
        }
        updateCount();
        emit statusMessage(tr("%n partie(s) marquée(s) supprimée(s) — Suppr pour restaurer, « Compacter la base » pour les retirer", nullptr,
                              int(rows.size())));
        return;
    }
    QString what;
    if (rows.size() == 1) {
        const auto cell = [&](int col) { return m_model->data(m_model->index(rows.first(), col), Qt::DisplayRole).toString(); };
        what = tr("la partie n° %1 (%2 – %3)").arg(rows.first() + 1).arg(cell(GameListModel::White), cell(GameListModel::Black));
    } else {
        what = tr("les %1 parties sélectionnées").arg(rows.size());
    }
    QMessageBox box(QMessageBox::Warning, tr("Supprimer"),
                    tr("Supprimer définitivement %1 du fichier « %2 » ?").arg(what, file), QMessageBox::NoButton, this);
    box.setInformativeText(tr("Le fichier PGN est modifié sur le disque. Cette action ne peut pas être annulée."));
    QPushButton* yes = box.addButton(tr("Supprimer"), QMessageBox::DestructiveRole);
    box.addButton(tr("Annuler"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(box.buttons().last()));
    box.exec();
    if (box.clickedButton() != yes) return;

    const int firstRow = m_table->selectionModel()->selectedRows().isEmpty() ? 0 : m_table->selectionModel()->selectedRows().first().row();
    QString error;
    if (!m_model->removeGames(rows, &error)) {
        QMessageBox::warning(this, tr("Supprimer"), tr("Impossible de modifier « %1 » :\n%2").arg(file, error));
        return;
    }
    updateCount();
    emit gamesRemoved(rows);
    const int visible = m_table->model()->rowCount();
    if (visible > 0) m_table->selectRow(std::min(firstRow, visible - 1));
    emit statusMessage(tr("%n partie(s) supprimée(s) de %1", nullptr, int(rows.size())).arg(file));
}

void GameListPanel::restoreSelected()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty() || !m_model->isIndexed() || m_model->isReadOnly()) return;
    QString error;
    if (!m_model->restoreGames(rows, &error)) {
        QMessageBox::warning(this, tr("Restaurer"), tr("Impossible de modifier « %1 » :\n%2").arg(QFileInfo(m_model->filePath()).fileName(), error));
        return;
    }
    updateCount();
    emit statusMessage(tr("%n partie(s) restaurée(s)", nullptr, int(rows.size())));
}

void GameListPanel::selectGame(int row)
{
    updateCount();
    const QModelIndex idx = m_table->model() == m_proxy ? m_proxy->mapFromSource(m_model->index(row, 0)) : m_model->index(row, 0);
    if (!idx.isValid()) return; // hidden by the filter
    m_table->selectRow(idx.row());
    m_table->scrollTo(idx);
}
