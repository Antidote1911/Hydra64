#include "nameindex.h"

#include <algorithm>

namespace hy64db {

namespace {
uint32_t field(const Record& r, NameType type, bool second = false)
{
    switch (type) {
    case Player: return second ? r.black : r.white;
    case Event: return r.event;
    case Site: return r.site;
    case Round: return r.round;
    case Annotator: return r.annotator;
    case Source: return r.source;
    default: return NoName;
    }
}
} // namespace

std::vector<IndexEntry> nameIndex(const Database& db, NameType type)
{
    std::vector<IndexEntry> all(db.names().count(type));
    auto note = [&](uint32_t id, const Record& r, int side) {
        if (id == NoName || id >= all.size()) return;
        IndexEntry& e = all[id];
        ++e.games;
        if (const int year = dateYear(r.date)) {
            if (!e.firstYear || year < e.firstYear) e.firstYear = year;
            if (year > e.lastYear) e.lastYear = year;
        }
        if (side < 0) return;
        e.bestElo = std::max(e.bestElo, int(side == 0 ? r.whiteElo : r.blackElo));
        if (r.result == Draw) ++e.draws;
        else if (r.result == WhiteWins) ++(side == 0 ? e.wins : e.losses);
        else if (r.result == BlackWins) ++(side == 0 ? e.losses : e.wins);
    };
    for (uint32_t id = 0; id < db.count(); ++id) {
        const Record r = db.record(id);
        if (r.deleted()) continue;
        if (type == Player) {
            note(r.white, r, 0);
            if (r.black != r.white) note(r.black, r, 1);
        } else {
            note(field(r, type), r, -1);
        }
    }
    std::vector<IndexEntry> out;
    const auto ranks = db.names().ranks(type);
    for (uint32_t id = 0; id < all.size(); ++id) {
        if (!all[id].games) continue;
        all[id].id = id;
        all[id].name = db.names().name(type, id);
        out.push_back(std::move(all[id]));
    }
    std::sort(out.begin(), out.end(), [&](const IndexEntry& a, const IndexEntry& b) { return (*ranks)[a.id] < (*ranks)[b.id]; });
    return out;
}

std::vector<uint32_t> gamesOf(const Database& db, NameType type, uint32_t nameId, const std::atomic<int32_t>* cancel)
{
    std::vector<uint32_t> out;
    for (uint32_t id = 0; id < db.count(); ++id) {
        if ((id & 0xFFFF) == 0 && cancel && cancel->load()) return {};
        const Record r = db.record(id);
        if (r.deleted()) continue;
        if (field(r, type) == nameId || (type == Player && r.black == nameId)) out.push_back(id);
    }
    return out;
}

} // namespace hy64db
