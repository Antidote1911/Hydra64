#include "playerbase.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace hy64db {

namespace {

namespace fs = std::filesystem;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[3]) | uint32_t(p[2]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[0]) << 24; }

PlayerBase::Date date(uint32_t v) { return {int(v >> 9), int((v >> 5) & 15), int(v & 31)}; }

constexpr size_t TailSize = 145;  // fixed part of a record, after the two names
constexpr size_t RatingSize = 19; // a rating
constexpr size_t MediaSize = 89;  // a photo record

// Key (exclusive OR) of a rating, byte by byte.
constexpr uint8_t RatingKey[RatingSize] = {0xf7, 0xfd, 0xff, 0x55, 0xc7, 0xed, 0x4f, 0xc5, 0x97, 0xdd, 0x9e, 0x34, 0x67, 0xcd, 0xef, 0xa5, 0x37, 0xbd, 0x3f};
// Key of a photo record: sequence of 32 bytes, shifted by 25, restarted at each record.
constexpr uint8_t MediaCycle[32] = {0xee, 0x7a, 0x5e, 0x4a, 0x4e, 0x1a, 0x3e, 0x6a, 0x2e, 0x3a, 0x9e, 0x0a, 0x8e, 0xda, 0x7e, 0x2a,
                                    0x6e, 0xfa, 0xde, 0xca, 0xce, 0x9a, 0xbe, 0xea, 0xae, 0xba, 0x1e, 0x8a, 0x0e, 0x5a, 0xfe, 0xaa};
uint8_t mediaKey(size_t j) { return uint8_t(MediaCycle[(j + 25) % 32] ^ 0xCC); }

// Windows-1252 → UTF-8
std::string fromCp1252(const uint8_t* p, size_t n)
{
    static const char16_t high[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                      0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        char32_t c = p[i];
        if (c >= 0x80 && c < 0xA0) c = high[c - 0x80];
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// File of `dir` whose name ends with `suffix`, ignoring case.
std::string findBySuffix(const std::string& dir, const std::string& suffix)
{
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = lower(e.path().filename().string());
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) return e.path().string();
    }
    return {};
}

} // namespace

std::string PlayerBase::foldName(const std::string& utf8)
{
    // Accented Latin letters brought back to their base letter; the rest in lower case.
    static const char* const latin1 = "aaaaaaaceeeeiiiidnooooo ouuuuyts" // U+00C0…U+00DF
                                      "aaaaaaaceeeeiiiidnooooo ouuuuyty"; // U+00E0…U+00FF
    std::string out;
    for (size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            if (c == '-' || c == '\'' || c == '.') out += ' ';
            else out += char(std::tolower(c));
        } else if ((c == 0xC3) && i + 1 < utf8.size()) {
            const unsigned char d = static_cast<unsigned char>(utf8[++i]);
            const char f = d >= 0x80 && d <= 0xBF ? latin1[d - 0x80] : ' ';
            if (f != ' ') out += f;
        } else if ((c == 0xC5) && i + 1 < utf8.size()) { // Š š Ž ž Œ œ Ÿ
            const unsigned char d = static_cast<unsigned char>(utf8[++i]);
            out += d == 0xA0 || d == 0xA1 ? 's' : d == 0xBD || d == 0xBE ? 'z' : d == 0xB8 ? 'y' : 'o';
        } else {
            while (i + 1 < utf8.size() && (static_cast<unsigned char>(utf8[i + 1]) & 0xC0) == 0x80) ++i; // other character: ignored
        }
    }
    // spaces: a single one between words, none at the edges
    std::string t;
    for (char ch : out) {
        if (ch == ' ' && (t.empty() || t.back() == ' ')) continue;
        t += ch;
    }
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

std::string PlayerBase::nationCode(int code)
{
    // Codes recorded on the strongest players of each number (docs/playerbase-re.md);
    // "?": number not identified with certainty.
    static const char* const table[] = {
        "",    "AFG", "ALB", "ALG", "AND", "ANG", "ANT", "ARG", "ARM", "AUS", "AUT", "AZE", "BAH", "BRN", "BAN", "BAR", // 0
        "BLR", "BEL", "?",   "BER", "BOL", "BIH", "BOT", "BRA", "IVB", "BRU", "BUL", "?",   "CAN", "CHI", "CHN", "COL", // 16
        "CRC", "CRO", "CUB", "CYP", "CZE", "DEN", "DJI", "DOM", "ECU", "EGY", "ENG", "ESP", "EST", "ETH", "FAI", "FIJ", // 32
        "FIN", "FRA", "MKD", "GAM", "GEO", "GER", "GHA", "GRE", "GUA", "GCI", "GUY", "HAI", "HON", "HKG", "HUN", "ISL", // 48
        "IND", "INA", "IRI", "IRQ", "ISR", "IRL", "ITA", "CIV", "JAM", "JPN", "JCI", "JOR", "KAZ", "KEN", "KOR", "KGZ", // 64
        "KUW", "LAT", "LBN", "LBA", "LIE", "LTU", "LUX", "MAC", "MAD", "MAS", "MLI", "MLT", "MTN", "MRI", "MEX", "MDA", // 80
        "MNC", "MGL", "MAR", "MOZ", "MYA", "NAM", "NEP", "NED", "AHO", "NZL", "NCA", "NGR", "NOR", "PAK", "PLE", "PAN", // 96
        "PNG", "PAR", "PER", "PHI", "POL", "POR", "PUR", "QAT", "ROU", "RUS", "ESA", "SMR", "SCO", "SEN", "SEY", "SGP", // 112
        "SVK", "SLO", "RSA", "SRI", "SUD", "SUR", "SWE", "SUI", "SYR", "TJK", "TAN", "THA", "TTO", "TUN", "TUR", "TKM", // 128
        "UGA", "UKR", "UAE", "USA", "URU", "URS", "UZB", "VEN", "VIE", "ISV", "WLS", "YEM", "YUG", "ZAM", "ZIM", "?",   // 144
        "GDR", "TCH", "?",   "?",   "?",   "?",   "?",   "COD", "?",   "LAO", "LES", "MAW", "MDV", "?",   "OMA", "?",   // 160
        "?",   "?",   "KSA", "TOG", "?",   "?",   "?",   "?",   "SWZ", "SLE", "?",   "?",   "?",   "?",   "?",   "SOL", // 176
        "SAA", "?",   "RWA", "LBR", "?",   "TPE", "?",   "?",   "ARU", "?",   "BHU", "BDI", "?",   "?",   "?",   "?",   // 192
        "?",   "?",   "?",   "?",   "ERI", "?",   "?",   "?",   "GAB", "?",   "?",   "?",   "?",   "GUM", "?",   "?",   // 208
        "IOM", "?",   "?",   "?",   "?",   "?",   "?",   "?",   "?",   "?",   "PLW", "?",   "?",   "STP", "SOM", "?",   // 224
        "?",   "?",   "?",   "?",   "?",   "?",   "?",   "?",   "?",   "MNE", "SRB", "?",   "?",   "KOS",               // 240
    };
    if (code <= 0 || code >= int(std::size(table))) return {};
    const std::string c = table[code];
    return c == "?" ? std::string() : c;
}

bool PlayerBase::open(const std::string& dir, std::string* error)
{
    close();
    const std::string players = findBySuffix(dir, ".player");
    if (players.empty()) {
        if (error) *error = "fichier .player introuvable dans " + dir;
        return false;
    }
    if (!m_players.open(players, error)) return false;
    m_dir = dir;
    if (const std::string p = findBySuffix(dir, ".rating"); !p.empty()) m_ratings.open(p);
    if (const std::string p = findBySuffix(dir, ".media"); !p.empty()) m_media.open(p);

    // Records: 8-byte header (number of records, version), then for each its length
    // (32 bits, big-endian) and its content.
    const uint8_t* d = m_players.data();
    const size_t size = m_players.size();
    if (size < 8) {
        close();
        if (error) *error = "fichier .player trop court";
        return false;
    }
    m_index.reserve(be32(d));
    size_t pos = 8;
    while (pos + 4 <= size) {
        const size_t len = be32(d + pos);
        pos += 4;
        if (len < 8 + TailSize || len > size - pos) break;
        const size_t lastLen = le32(d + pos);
        if (lastLen <= len - 8 - TailSize) m_index.push_back({foldName(fromCp1252(d + pos + 4, lastLen)), uint32_t(pos)});
        pos += len;
    }
    std::sort(m_index.begin(), m_index.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    if (m_index.empty()) {
        close();
        if (error) *error = "aucune fiche lisible dans " + players;
        return false;
    }
    return true;
}

void PlayerBase::close()
{
    m_players.close();
    m_ratings.close();
    m_media.close();
    m_index.clear();
    m_groups.clear();
    m_groupsBuilt = false;
    m_folders.clear();
    m_dir.clear();
}

PlayerBase::Player PlayerBase::read(size_t offset) const
{
    Player p;
    const uint8_t* d = m_players.data() + offset;
    const size_t avail = m_players.size() - offset;
    const size_t a = le32(d);
    if (a + 8 > avail) return p;
    const size_t b = le32(d + 4 + a);
    if (a + b + 8 + TailSize > avail) return p;
    p.last = fromCp1252(d + 4, a);
    p.first = fromCp1252(d + 8 + a, b);
    const uint8_t* t = d + 8 + a + b;
    p.birth = date(le32(t));
    p.homonym = le16(t + 4);
    p.id = le32(t + 6);
    p.death = date(le32(t + 10));
    const unsigned flags = le16(t + 14);
    static const char* const titles[] = {"", "FM", "IM", "GM"};
    static const char* const womenTitles[] = {"", "WFM", "WIM", "WGM"};
    p.title = titles[(flags >> 4) & 3];
    p.womenTitle = womenTitles[(flags >> 2) & 3];
    p.womenWorldChampion = flags & 0x40;
    p.worldChampion = flags & 0x80;
    p.reigningChampion = flags & 0x100;
    p.nation = nationCode(le16(t + 16));
    p.formerNation = nationCode(le16(t + 18));
    p.male = le32(t + 20) & 1;
    p.media = int32_t(le32(t + 115));
    return p;
}

std::vector<PlayerBase::Player> PlayerBase::lookup(const std::string& last, const std::string& first, const std::string& nation, bool* exactFirst) const
{
    std::vector<Player> out;
    if (exactFirst) *exactFirst = false;
    if (last.empty()) return out;
    auto it = std::lower_bound(m_index.begin(), m_index.end(), last, [](const Entry& e, const std::string& k) { return e.key < k; });
    struct Scored {
        Player p;
        int exact, nation, elo;
        // At equal Elo (players from before the FIDE lists): the best documented first.
        int known() const { return (p.media >= 0 ? 4 : 0) + (p.worldChampion || p.womenWorldChampion ? 4 : 0) + (p.title.empty() ? 0 : 2) + (p.birth.known() ? 1 : 0); }
    };
    std::vector<Scored> found;
    for (; it != m_index.end() && it->key == last; ++it) {
        Player p = read(it->offset);
        const std::string f = foldName(p.first);
        // First name: identical, or one starts with the other (initial, middle name omitted)
        const bool exact = f == first;
        if (!exact && !first.empty() && f.compare(0, first.size(), first) != 0 && first.compare(0, f.size(), f) != 0) continue;
        const bool sameNation = !nation.empty() && (p.nation == nation || p.formerNation == nation);
        found.push_back({std::move(p), exact ? 1 : 0, sameNation ? 1 : 0, 0});
    }
    for (Scored& s : found) s.elo = peakElo(s.p.id);
    std::stable_sort(found.begin(), found.end(), [](const Scored& a, const Scored& b) {
        if (a.exact != b.exact) return a.exact > b.exact;
        if (a.nation != b.nation) return a.nation > b.nation;
        if (a.elo != b.elo) return a.elo > b.elo;
        return a.known() > b.known();
    });
    if (exactFirst && !found.empty()) *exactFirst = found.front().exact;
    for (Scored& s : found) out.push_back(std::move(s.p));
    return out;
}

std::vector<PlayerBase::Player> PlayerBase::find(const std::string& name) const
{
    if (!isOpen()) return {};
    // Federation in parentheses at the end ("Kasparov Garry (RUS)", Chess Assistant databases):
    // removed from the name, it separates namesakes.
    std::string text = name, nation;
    if (const size_t open = text.rfind('('); open != std::string::npos && text.find(')', open) != std::string::npos) {
        for (size_t i = open + 1; i < text.size() && text[i] != ')'; ++i) nation += char(std::toupper(static_cast<unsigned char>(text[i])));
        text.erase(open);
        if (nation.size() != 3) nation.clear();
    }
    const size_t comma = text.find(',');
    if (comma != std::string::npos) return lookup(foldName(text.substr(0, comma)), foldName(text.substr(comma + 1)), nation, nullptr);

    // Without a comma: the whole name as the family name ("Anderssen", "Van Wely"), then all
    // the splits "Last First" (Chess Assistant) and "First Last" (playing sites). A split
    // whose first name matches exactly wins; otherwise the first one that finds somebody.
    const std::string whole = foldName(text);
    std::vector<std::string> words;
    for (size_t i = 0; i < whole.size();) {
        const size_t e = whole.find(' ', i);
        words.push_back(whole.substr(i, e == std::string::npos ? std::string::npos : e - i));
        if (e == std::string::npos) break;
        i = e + 1;
    }
    auto join = [&](size_t from, size_t to) {
        std::string out;
        for (size_t i = from; i < to; ++i) out += (out.empty() ? "" : " ") + words[i];
        return out;
    };
    std::vector<Player> fallback = lookup(whole, {}, nation, nullptr);
    for (size_t k = 1; k < words.size(); ++k) {
        const std::pair<std::string, std::string> splits[] = {
            {join(0, k), join(k, words.size())},                                    // Last First
            {join(words.size() - k, words.size()), join(0, words.size() - k)},      // First Last
        };
        for (const auto& [last, first] : splits) {
            bool exact = false;
            std::vector<Player> r = lookup(last, first, nation, &exact);
            if (r.empty()) continue;
            if (exact) return r;
            if (fallback.empty()) fallback = std::move(r);
        }
    }
    return fallback;
}

void PlayerBase::buildRatingIndex() const
{
    std::lock_guard lock(m_mutex);
    if (m_groupsBuilt) return;
    m_groupsBuilt = true;
    if (!m_ratings.isOpen() || m_ratings.size() < 4 + RatingSize) return;
    // A player's ratings follow one another (with rare exceptions: several groups then).
    const uint8_t* d = m_ratings.data() + 4;
    const size_t n = (m_ratings.size() - 4) / RatingSize;
    auto idAt = [&](size_t i) {
        const uint8_t* r = d + i * RatingSize;
        return uint32_t(r[0] ^ RatingKey[0]) | uint32_t(r[1] ^ RatingKey[1]) << 8 | uint32_t(r[2] ^ RatingKey[2]) << 16 | uint32_t(r[3] ^ RatingKey[3]) << 24;
    };
    size_t start = 0;
    uint32_t cur = idAt(0);
    for (size_t i = 1; i <= n; ++i) {
        const uint32_t id = i < n ? idAt(i) : ~cur;
        if (id == cur) continue;
        m_groups.push_back({cur, uint32_t(start), uint32_t(i - start)});
        start = i;
        cur = id;
    }
    std::stable_sort(m_groups.begin(), m_groups.end(), [](const Group& a, const Group& b) { return a.id < b.id; });
}

std::vector<PlayerBase::Rating> PlayerBase::ratings(uint32_t id) const
{
    std::vector<Rating> out;
    if (!m_groupsBuilt) buildRatingIndex();
    auto it = std::lower_bound(m_groups.begin(), m_groups.end(), id, [](const Group& g, uint32_t v) { return g.id < v; });
    for (; it != m_groups.end() && it->id == id; ++it) {
        for (uint32_t i = 0; i < it->count; ++i) {
            uint8_t r[RatingSize];
            const uint8_t* src = m_ratings.data() + 4 + size_t(it->first + i) * RatingSize;
            for (size_t k = 0; k < RatingSize; ++k) r[k] = src[k] ^ RatingKey[k];
            Rating x;
            x.elo = r[6] << 8 | r[7]; // most significant first, unlike the other fields
            x.date = date(le32(r + 11));
            x.rank = int32_t(le32(r + 15));
            if (x.elo > 0 && x.date.known()) out.push_back(x);
        }
    }
    std::sort(out.begin(), out.end(), [](const Rating& a, const Rating& b) {
        return std::tie(a.date.year, a.date.month, a.date.day) < std::tie(b.date.year, b.date.month, b.date.day);
    });
    return out;
}

int PlayerBase::peakElo(uint32_t id) const
{
    int best = 0;
    for (const Rating& r : ratings(id)) best = std::max(best, r.elo);
    return best;
}

std::vector<PlayerBase::Photo> PlayerBase::photos(const Player& player) const
{
    std::vector<Photo> out;
    if (!m_media.isOpen() || player.media < 0) return out;
    const size_t n = m_media.size() / MediaSize;
    auto record = [&](size_t i, uint8_t* r) {
        const uint8_t* src = m_media.data() + i * MediaSize;
        for (size_t k = 0; k < MediaSize; ++k) r[k] = src[k] ^ mediaKey(k);
    };
    auto text = [](const uint8_t* p, size_t max) {
        size_t len = 0;
        while (len < max && p[len]) ++len;
        return fromCp1252(p, len);
    };
    uint8_t r[MediaSize];
    if (size_t(player.media) >= n) return out;
    record(size_t(player.media), r); // the player's anchor record: only the link to their first photo
    int32_t next = int32_t(le32(r + 82));
    for (size_t guard = 0; next >= 0 && size_t(next) < n && guard < 2000; ++guard) {
        record(size_t(next), r);
        Photo ph;
        const std::string prefix = text(r, 4);
        ph.photographer = text(r + 4, 40);
        ph.place = text(r + 44, 30);
        ph.year = date(le32(r + 74)).year;
        const uint32_t number = le32(r + 78);
        next = int32_t(le32(r + 82));
        if (prefix.empty()) continue;
        // File: PlayBase.MM<initial>/<prefix><5-digit number>.jpg, variable case
        char num[16];
        std::snprintf(num, sizeof num, "%05u", number);
        const std::string folderName = std::string("playbase.mm") + char(std::tolower(static_cast<unsigned char>(prefix[0])));
        const std::string fileName = lower(prefix) + num + ".jpg";
        {
            std::lock_guard lock(m_mutex);
            auto f = m_folders.find(folderName);
            if (f == m_folders.end()) {
                std::map<std::string, std::string> files;
                std::error_code ec;
                for (const auto& e : fs::directory_iterator(m_dir, ec)) {
                    if (lower(e.path().filename().string()) != folderName) continue;
                    for (const auto& file : fs::directory_iterator(e.path(), ec)) files[lower(file.path().filename().string())] = file.path().string();
                }
                f = m_folders.emplace(folderName, std::move(files)).first;
            }
            if (auto file = f->second.find(fileName); file != f->second.end()) ph.path = file->second;
        }
        out.push_back(std::move(ph));
    }
    std::stable_sort(out.begin(), out.end(), [](const Photo& a, const Photo& b) { return a.year < b.year; });
    return out;
}

} // namespace hy64db
