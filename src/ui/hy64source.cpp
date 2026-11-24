#include "hy64source.h"

#include <QFile>

#include "chess/pgn.h"
#include "db/beauty.h"
#include "db/positionindex.h"

#include <QFileInfo>

#include <algorithm>
#include <iterator>

bool Hy64Source::isDatabasePath(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("hy64i") || s == QLatin1String("hy64g") || s == QLatin1String("hy64n") || s == QLatin1String("hy64p") || s == QLatin1String("hy64");
}

bool Hy64Source::create(const QString& path, QString* error)
{
    std::string err;
    if (hy64db::isPackedPath(path.toStdString())) { // single-file database: ordinary empty database, packed right away
        const std::string temp = path.toStdString() + ".new";
        const bool made = hy64db::Database::create(temp, &err) && hy64db::packDatabase(temp + ".hy64i", path.toStdString(), &err);
        for (const std::string& part : {hy64db::indexPath(temp), hy64db::namesPath(temp), hy64db::gamesPath(temp), hy64db::positionsPath(temp)}) QFile::remove(QString::fromStdString(part));
        if (!made && error) *error = QString::fromStdString(err);
        return made;
    }
    const bool ok = hy64db::Database::create(hy64db::basePathOf(path.toStdString()), &err);
    if (!ok && error) *error = QString::fromStdString(err);
    return ok;
}

bool Hy64Source::open(const QString& path, QString* error)
{
    std::string err;
    if (!m_db.open(path.toStdString(), &err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    reload();
    return true;
}

void Hy64Source::reload()
{
    // Deleted games stay displayed (struck through) until compaction.
    m_view = m_db.allGames(true);
    m_deleted = int(std::count_if(m_view.begin(), m_view.end(), [&](uint32_t id) { return m_db.record(id).deleted(); }));
    m_total = int(m_view.size()) - m_deleted;
    m_filtered = false;
}

bool Hy64Source::isDeleted(int row) const { return row >= 0 && row < int(m_view.size()) && m_db.record(m_view[size_t(row)]).deleted(); }

bool Hy64Source::header(int row, Header& out) const
{
    if (row < 0 || row >= int(m_view.size())) return false;
    const uint32_t id = m_view[size_t(row)];
    const hy64db::Record r = m_db.record(id);
    const hy64db::Names& n = m_db.names();
    auto name = [&](hy64db::NameType t, uint32_t v) { return v == hy64db::NoName ? QString() : QString::fromStdString(n.name(t, v)); };
    out.id = id + 1; // numbered from 1, as in the PGN list
    out.white = name(hy64db::Player, r.white);
    out.black = name(hy64db::Player, r.black);
    out.event = name(hy64db::Event, r.event);
    out.date = QString::fromStdString(hy64db::formatDate(r.date));
    out.result = QString::fromLatin1(hy64db::formatResult(r.result));
    out.eco = QString::fromStdString(hy64db::formatEco(r.eco));
    out.site = name(hy64db::Site, r.site);
    out.round = name(hy64db::Round, r.round);
    out.plies = r.plies;
    out.whiteElo = r.whiteElo;
    out.blackElo = r.blackElo;
    out.deleted = r.deleted();
    out.userFlags = uint8_t((r.flags >> hy64db::UserFlagShift) & 0x3F);
    out.beauty = hy64db::beautyOf(r.flags);
    return true;
}

bool Hy64Source::summary(int row, Summary& out) const
{
    if (row < 0 || row >= int(m_view.size())) return false;
    const hy64db::Record r = m_db.record(m_view[size_t(row)]);
    out.year = int(r.date >> 9);
    out.whiteElo = r.whiteElo;
    out.blackElo = r.blackElo;
    out.result = r.result == hy64db::WhiteWins ? 1 : r.result == hy64db::BlackWins ? -1 : r.result == hy64db::Draw ? 0 : 2;
    out.eco = r.eco;
    return true;
}

bool Hy64Source::game(int row, chess::Game& out, QString* error) const
{
    if (row < 0 || row >= int(m_view.size())) return false;
    std::string err;
    const bool ok = m_db.game(m_view[size_t(row)], out, &err);
    if (!ok && error) *error = QString::fromStdString(err);
    return ok;
}

QString Hy64Source::gameText(int row) const
{
    chess::Game g;
    return game(row, g, nullptr) ? QString::fromStdString(chess::pgn::writeGame(g)).trimmed() : QString();
}

std::unique_ptr<IndexedGameSource::View> Hy64Source::search(const QString& text, const Cancel* cancel, QString*) const
{
    auto v = std::make_unique<Ids>();
    v->ids = m_db.search(text.toStdString(), cancel, true);
    if (cancel && cancel->load()) return nullptr;
    return v;
}

std::unique_ptr<IndexedGameSource::View> Hy64Source::viewIds(bool all) const
{
    auto v = std::make_unique<Ids>();
    v->ids = all ? m_db.allGames(true) : m_view;
    return v;
}

bool Hy64Source::sort(View& view, int column, bool ascending, const Cancel* cancel, QString* error) const
{
    const bool ok = m_db.sort(static_cast<Ids&>(view).ids, hy64db::Column(column), ascending, cancel);
    if (!ok && error && !(cancel && cancel->load())) *error = QStringLiteral("tri impossible");
    return ok;
}

void Hy64Source::setView(std::unique_ptr<View> view, bool filtered)
{
    if (view) m_view = std::move(static_cast<Ids*>(view.get())->ids);
    else m_view = m_db.allGames(true);
    m_filtered = filtered && view;
}

std::unique_ptr<IndexedGameSource::View> Hy64Source::searchHeaders(const hy64db::HeaderQuery& q, Combine combine, const View* current,
                                                                 const Cancel* cancel, QString*) const
{
    auto v = std::make_unique<Ids>();
    const std::vector<uint32_t>* cur = current ? &static_cast<const Ids*>(current)->ids : nullptr;
    if (combine == Combine::Within && cur) {
        v->ids = m_db.searchHeaders(q, cur, cancel, true);
    } else {
        v->ids = m_db.searchHeaders(q, nullptr, cancel, true);
        if (combine == Combine::Add && cur) { // union, in database order
            std::vector<uint32_t> a = *cur;
            std::sort(a.begin(), a.end());
            std::vector<uint32_t> out;
            std::set_union(a.begin(), a.end(), v->ids.begin(), v->ids.end(), std::back_inserter(out));
            v->ids = std::move(out);
        }
    }
    if (cancel && cancel->load()) return nullptr;
    return v;
}

bool Hy64Source::setUserFlag(const QList<int>& rows, int flag, bool on, QString* error)
{
    if (flag < 0 || flag >= hy64db::UserFlagCount) return false;
    for (int row : rows) {
        if (row < 0 || row >= int(m_view.size())) continue;
        const uint32_t id = m_view[size_t(row)];
        uint8_t f = uint8_t((m_db.record(id).flags >> hy64db::UserFlagShift) & 0x3F);
        f = on ? uint8_t(f | (1 << flag)) : uint8_t(f & ~(1 << flag));
        std::string err;
        if (!m_db.setUserFlags(id, f, &err)) {
            if (error) *error = QString::fromStdString(err);
            return false;
        }
    }
    m_db.sync();
    return true;
}

std::unique_ptr<IndexedGameSource::View> Hy64Source::viewOf(std::vector<uint32_t> ids)
{
    auto v = std::make_unique<Ids>();
    v->ids = std::move(ids);
    return v;
}

bool Hy64Source::setDeletedIds(const std::vector<uint32_t>& ids, bool deleted, QString* error)
{
    for (uint32_t id : ids) {
        if (id >= m_db.count() || m_db.record(id).deleted() == deleted) continue;
        std::string err;
        if (!m_db.setDeleted(id, deleted, &err)) {
            if (error) *error = QString::fromStdString(err);
            return false;
        }
        m_total += deleted ? -1 : 1;
        m_deleted += deleted ? 1 : -1;
    }
    m_db.sync();
    return true;
}

int Hy64Source::rowOfNumber(int number) const
{
    const uint32_t id = uint32_t(number - 1);
    if (number < 1) return -1;
    if (!m_filtered && id < m_view.size() && m_view[id] == id) return int(id); // view in database order
    const auto it = std::find(m_view.begin(), m_view.end(), id);
    return it == m_view.end() ? -1 : int(it - m_view.begin());
}

std::unique_ptr<IndexedGameSource::View> Hy64Source::negate(const View& current) const
{
    std::vector<uint32_t> cur = static_cast<const Ids&>(current).ids;
    std::sort(cur.begin(), cur.end());
    auto v = std::make_unique<Ids>();
    const std::vector<uint32_t> all = m_db.allGames(true);
    std::set_difference(all.begin(), all.end(), cur.begin(), cur.end(), std::back_inserter(v->ids));
    return v;
}

std::unique_ptr<IndexedGameSource::View> Hy64Source::searchPosition(const chess::Position& pos, const Cancel* cancel, QString*) const
{
    auto v = std::make_unique<Ids>();
    v->ids = hy64db::searchPosition(m_db, pos, cancel);
    if (cancel && cancel->load()) return nullptr;
    return v;
}

std::vector<chess::treebook::BookMove> Hy64Source::tree(const chess::Position& pos) const { return hy64db::positionTree(m_db, pos); }

int Hy64Source::append(const chess::Game& game, QString* error)
{
    std::string err;
    uint32_t id = 0;
    if (!m_db.append(game, &id, &err)) {
        if (error) *error = QString::fromStdString(err);
        return -1;
    }
    m_db.sync();
    m_view.push_back(id); // the new game appears at the end of the list
    ++m_total;
    return int(m_view.size()) - 1;
}

int Hy64Source::appendFrom(const Hy64Source& from, int row, QString* error)
{
    if (row < 0 || row >= from.count()) return -1;
    std::string err;
    uint32_t id = 0;
    if (!m_db.copyGame(from.m_db, from.m_view[size_t(row)], &id, &err)) {
        if (error) *error = QString::fromStdString(err);
        return -1;
    }
    m_view.push_back(id);
    ++m_total;
    return int(m_view.size()) - 1;
}

bool Hy64Source::replace(int row, const chess::Game& game, QString* error)
{
    if (row < 0 || row >= int(m_view.size())) return false;
    std::string err;
    const bool ok = m_db.replace(m_view[size_t(row)], game, &err);
    m_db.sync();
    if (!ok && error) *error = QString::fromStdString(err);
    return ok;
}

bool Hy64Source::setDeleted(QList<int> rows, bool deleted, QString* error)
{
    for (int row : rows) {
        if (row < 0 || row >= int(m_view.size())) continue;
        const uint32_t id = m_view[size_t(row)];
        if (m_db.record(id).deleted() == deleted) continue;
        std::string err;
        if (!m_db.setDeleted(id, deleted, &err)) {
            if (error) *error = QString::fromStdString(err);
            return false;
        }
        m_total += deleted ? -1 : 1;
        m_deleted += deleted ? 1 : -1;
    }
    m_db.sync();
    return true;
}
