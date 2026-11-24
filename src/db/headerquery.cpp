#include "headerquery.h"

#include "format.h"
#include "names.h"

#include <cstdlib>

namespace hy64db {

bool HeaderQuery::isEmpty() const
{
    return white.empty() && black.empty() && event.empty() && site.empty() && round.empty() && annotator.empty() && !whiteEloMin && !whiteEloMax &&
           !blackEloMin && !blackEloMax && !yearFrom && !yearTo && !ecoFrom && !ecoTo && results == AllResults && !movesMin && !movesMax &&
           !withVariations && !withComments && !userFlags && material.isEmpty();
}

// A comment to read: text between braces once the commands "[%clk …]", "[%eval …]"… are
// removed (same rule as the "commented" flag of Hydra64 databases).
bool hasTextComment(std::string_view t)
{
    for (size_t b = t.find('{'); b != std::string_view::npos; b = t.find('{', b + 1)) {
        const size_t e = t.find('}', b);
        if (e == std::string_view::npos) break;
        const std::string_view body = t.substr(b + 1, e - b - 1);
        if (body.find("[%") == std::string_view::npos) {
            if (body.find_first_not_of(" \t\r\n") != std::string_view::npos) return true;
        } else if (!chess::pgn::withoutCommands(std::string(body)).empty()) {
            return true;
        }
        b = e;
    }
    return false;
}

int countMoves(std::string_view t)
{
    // Headers skipped, then the highest move number "N." met at level 0.
    size_t i = 0;
    int depth = 0, best = 0;
    bool inComment = false;
    while (i < t.size()) {
        const char c = t[i];
        if (inComment) {
            if (c == '}') inComment = false;
            ++i;
            continue;
        }
        if (c == '[' && depth == 0 && (i == 0 || t[i - 1] == '\n')) { // header line
            const size_t e = t.find('\n', i);
            i = e == std::string_view::npos ? t.size() : e + 1;
            continue;
        }
        if (c == '{') inComment = true;
        else if (c == ';') {
            const size_t e = t.find('\n', i);
            i = e == std::string_view::npos ? t.size() : e + 1;
            continue;
        } else if (c == '(') ++depth;
        else if (c == ')') depth = std::max(0, depth - 1);
        else if (depth == 0 && c >= '0' && c <= '9' && (i == 0 || t[i - 1] == ' ' || t[i - 1] == '\n' || t[i - 1] == ')')) {
            size_t j = i;
            int n = 0;
            while (j < t.size() && t[j] >= '0' && t[j] <= '9') n = n * 10 + (t[j++] - '0');
            if (j < t.size() && t[j] == '.') best = std::max(best, n);
            i = j;
            continue;
        }
        ++i;
    }
    return best;
}

namespace {
bool nameOk(const std::string& value, const std::string& want) { return want.empty() || wordPrefix(fold(value), fold(want)); }
bool range(int v, int lo, int hi) { return (!lo || v >= lo) && (!hi || v <= hi); }
} // namespace

bool matches(const HeaderQuery& q, const chess::pgn::GameEntry& e, std::string_view text)
{
    if (q.ignoreColors) {
        const bool direct = nameOk(e.white, q.white) && nameOk(e.black, q.black);
        const bool swapped = nameOk(e.black, q.white) && nameOk(e.white, q.black);
        if (!direct && !swapped) return false;
    } else if (!nameOk(e.white, q.white) || !nameOk(e.black, q.black)) {
        return false;
    }
    if (!nameOk(e.event, q.event) || !nameOk(e.site, q.site) || !nameOk(e.round, q.round)) return false;
    if (!q.annotator.empty()) {
        // the annotator is not in the PGN index: read from the text
        const size_t at = text.find("[Annotator \"");
        if (at == std::string_view::npos) return false;
        const size_t end = text.find('"', at + 12);
        if (!nameOk(std::string(text.substr(at + 12, end - at - 12)), q.annotator)) return false;
    }
    const int we = std::atoi(e.whiteElo.c_str()), be = std::atoi(e.blackElo.c_str());
    if (!range(we, q.whiteEloMin, q.whiteEloMax) || !range(be, q.blackEloMin, q.blackEloMax)) return false;
    if (q.yearFrom || q.yearTo) {
        const int y = std::atoi(e.date.c_str());
        if (!y || !range(y, q.yearFrom, q.yearTo)) return false;
    }
    if (q.ecoFrom || q.ecoTo) {
        const uint16_t eco = parseEco(e.eco);
        if (!eco || !range(eco, q.ecoFrom, q.ecoTo)) return false;
    }
    if (q.results != HeaderQuery::AllResults) {
        const uint8_t bit = e.result == "1-0"       ? HeaderQuery::WhiteWin
                            : e.result == "0-1"     ? HeaderQuery::BlackWin
                            : e.result == "1/2-1/2" ? HeaderQuery::DrawResult
                                                    : HeaderQuery::Unknown;
        if (!(q.results & bit)) return false;
    }
    if ((q.movesMin || q.movesMax) && !range(countMoves(text), q.movesMin, q.movesMax)) return false;
    if (q.withComments && !hasTextComment(text)) return false;
    if (q.withVariations && text.find('(') == std::string_view::npos) return false;
    if (q.userFlags) return false; // no flags in a PGN
    if (!q.material.isEmpty()) {
        chess::Game g;
        if (!chess::pgn::parseGame(text, g)) return false;
        MaterialMatcher m(q.material);
        if (m.feed(g.root()->pos, 0)) {
            int ply = 0;
            for (const chess::Node* n : g.mainline())
                if (!m.feed(n->pos, ++ply)) break;
        }
        return m.matched();
    }
    return true;
}

} // namespace hy64db
