#include "names.h"

#include <algorithm>
#include <numeric>

namespace hy64db {

namespace {

// Accented Latin letters (two-byte UTF-8, U+00C0…U+017F) → base letter.
char baseLetter(uint32_t cp)
{
    static const char* const latin1 = // U+00C0 … U+00FF
        "aaaaaaaceeeeiiii" // C0–CF
        "dnooooo*ouuuuyts" // D0–DF (× → *, Þ → t, ß → s)
        "aaaaaaaceeeeiiii" // E0–EF
        "dnooooo/ouuuuyty"; // F0–FF
    static const char* const extA = // U+0100 … U+017F, in upper/lower case pairs
        "aaaaaaccccccccdd" "ddeeeeeeeeeegggg" "gggghhhhiiiiiiii" "iijjjjkkklllllll"
        "lllnnnnnnnnnoooo" "oooorrrrrrssssss" "ssttttttuuuuuuuu" "uuuuwwyyyzzzzzzs";
    if (cp >= 0xC0 && cp <= 0xFF) return latin1[cp - 0xC0];
    if (cp >= 0x100 && cp <= 0x17F) return extA[cp - 0x100];
    return 0;
}

bool isWordStart(const std::string& s, size_t i)
{
    if (i == 0) return true;
    const char c = s[i - 1];
    return c == ' ' || c == ',' || c == '-' || c == '(' || c == '.' || c == '\'' || c == '/';
}

} // namespace

std::string fold(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += char(c >= 'A' && c <= 'Z' ? c + 32 : c);
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            const uint32_t cp = uint32_t(c & 0x1F) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            if (const char b = baseLetter(cp)) {
                out += b;
                ++i;
            } else {
                out += s.substr(i, 2);
                ++i;
            }
        } else {
            out += char(c);
        }
    }
    return out;
}

void Names::encode(std::vector<uint8_t>& out, NameType t, std::string_view name)
{
    putVarint(out, uint64_t(name.size()) * 8 + t);
    out.insert(out.end(), name.begin(), name.end());
}

bool Names::load(const uint8_t* data, size_t size, std::string* error)
{
    {
        std::unique_lock lock(m_mutex);
        for (Table& t : m_tables) t = Table();
    }
    return loadMore(data, size, error);
}

bool Names::loadMore(const uint8_t* data, size_t size, std::string* error)
{
    std::unique_lock lock(m_mutex);
    const uint8_t* p = data;
    const uint8_t* end = data + size;
    while (p < end) {
        uint64_t v = 0;
        if (!getVarint(p, end, v) || (v & 7) >= NameTypeCount || uint64_t(end - p) < (v >> 3)) {
            if (error) *error = "fichier de noms abîmé";
            return false;
        }
        const size_t len = size_t(v >> 3);
        m_tables[v & 7].names.emplace_back(reinterpret_cast<const char*>(p), len);
        p += len;
    }
    return true;
}

size_t Names::count(NameType t) const
{
    std::shared_lock lock(m_mutex);
    return m_tables[t].names.size();
}

std::string Names::name(NameType t, uint32_t id) const
{
    std::shared_lock lock(m_mutex);
    const auto& n = m_tables[t].names;
    return id < n.size() ? n[id] : std::string();
}

void Names::buildLookup(const Table& table) const
{
    if (!table.lookup.empty() || table.names.empty()) return;
    table.lookup.reserve(table.names.size() * 2);
    for (uint32_t i = 0; i < table.names.size(); ++i) table.lookup.emplace(table.names[i], i);
}

uint32_t Names::find(NameType t, std::string_view name) const
{
    std::unique_lock lock(m_mutex); // the lookup table is built on the first call
    buildLookup(m_tables[t]);
    const auto it = m_tables[t].lookup.find(std::string(name));
    return it == m_tables[t].lookup.end() ? NoName : it->second;
}

uint32_t Names::intern(NameType t, std::string_view name, std::vector<uint8_t>* appended)
{
    std::unique_lock lock(m_mutex);
    Table& table = m_tables[t];
    buildLookup(table);
    const auto [it, added] = table.lookup.emplace(std::string(name), uint32_t(table.names.size()));
    if (added) {
        table.names.emplace_back(name);
        table.folded.reset();
        table.ranks.reset();
        if (appended) encode(*appended, t, name);
    }
    return it->second;
}

std::shared_ptr<const std::vector<std::string>> Names::foldedOf(NameType t) const
{
    {
        std::shared_lock lock(m_mutex);
        if (m_tables[t].folded) return m_tables[t].folded;
    }
    std::unique_lock lock(m_mutex);
    const Table& table = m_tables[t];
    if (!table.folded) {
        auto f = std::make_shared<std::vector<std::string>>();
        f->reserve(table.names.size());
        for (const std::string& n : table.names) f->push_back(fold(n));
        table.folded = std::move(f);
    }
    return table.folded;
}

std::shared_ptr<const std::vector<uint32_t>> Names::ranks(NameType t) const
{
    {
        std::shared_lock lock(m_mutex);
        if (m_tables[t].ranks) return m_tables[t].ranks;
    }
    const auto folded = foldedOf(t);
    std::vector<std::string> originals;
    {
        std::shared_lock lock(m_mutex);
        originals = m_tables[t].names;
    }
    std::vector<uint32_t> order(folded->size());
    std::iota(order.begin(), order.end(), 0u);
    // Letters first: names starting with a digit or a sign come after.
    auto letterFirst = [](const std::string& s) { return s.empty() || !(s[0] >= 'a' && s[0] <= 'z'); };
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const std::string &fa = (*folded)[a], &fb = (*folded)[b];
        const bool na = letterFirst(fa), nb = letterFirst(fb);
        if (na != nb) return !na;
        if (fa != fb) return fa < fb;
        if (originals[a] != originals[b]) return originals[a] < originals[b];
        return a < b;
    });
    auto r = std::make_shared<std::vector<uint32_t>>(order.size());
    for (uint32_t i = 0; i < order.size(); ++i) (*r)[order[i]] = i;
    std::unique_lock lock(m_mutex);
    if (m_tables[t].names.size() == r->size()) m_tables[t].ranks = r;
    return r;
}

bool wordPrefix(std::string_view n, std::string_view want)
{
    if (want.empty()) return true;
    for (size_t at = n.find(want[0]); at != std::string_view::npos; at = n.find(want[0], at + 1)) {
        const char c = at ? n[at - 1] : ' ';
        const bool start = at == 0 || c == ' ' || c == ',' || c == '-' || c == '(' || c == '.' || c == '\'' || c == '/';
        if (start && n.compare(at, want.size(), want) == 0) return true;
    }
    return false;
}

std::vector<uint32_t> Names::findPrefix(NameType t, std::string_view prefix) const
{
    const std::string want = fold(prefix);
    std::vector<uint32_t> ids;
    if (want.empty()) return ids;
    const auto folded = foldedOf(t);
    for (uint32_t i = 0; i < folded->size(); ++i) {
        const std::string& n = (*folded)[i];
        for (size_t at = n.find(want[0]); at != std::string::npos; at = n.find(want[0], at + 1)) {
            if (isWordStart(n, at) && n.compare(at, want.size(), want) == 0) {
                ids.push_back(i);
                break;
            }
        }
    }
    return ids;
}

} // namespace hy64db
