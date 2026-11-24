#include "format.h"

#include <cctype>
#include <cstdio>

namespace hy64db {

void Record::write(uint8_t* p) const
{
    std::memset(p, 0, RecordSize);
    put64(p + 0, (offset & ((uint64_t(1) << 40) - 1)) | (uint64_t(size & 0xFFFFFF) << 40));
    put32(p + 8, white);
    put32(p + 12, black);
    put32(p + 16, event);
    put32(p + 20, site);
    put32(p + 24, round);
    put32(p + 28, annotator);
    put32(p + 32, date);
    put16(p + 36, whiteElo);
    put16(p + 38, blackElo);
    put16(p + 40, eco);
    put16(p + 42, plies);
    p[44] = result;
    p[45] = variant;
    put16(p + 46, flags);
    put32(p + 48, material);
    put32(p + 52, source);
    put64(p + 56, pawns);
}

Record Record::read(const uint8_t* p)
{
    Record r;
    const uint64_t os = get64(p);
    r.offset = os & ((uint64_t(1) << 40) - 1);
    r.size = uint32_t(os >> 40);
    r.white = get32(p + 8);
    r.black = get32(p + 12);
    r.event = get32(p + 16);
    r.site = get32(p + 20);
    r.round = get32(p + 24);
    r.annotator = get32(p + 28);
    r.date = get32(p + 32);
    r.whiteElo = get16(p + 36);
    r.blackElo = get16(p + 38);
    r.eco = get16(p + 40);
    r.plies = get16(p + 42);
    r.result = p[44];
    r.variant = p[45];
    r.flags = get16(p + 46);
    r.material = get32(p + 48);
    r.source = get32(p + 52);
    r.pawns = get64(p + 56);
    return r;
}

uint32_t parseDate(const std::string& s)
{
    // "YYYY.MM.DD", each part possibly "??"; anything else is refused (kept as text)
    if (s.size() != 10 || s[4] != '.' || s[7] != '.') return 0;
    auto part = [&](size_t at, size_t n, int max) -> int {
        bool unknown = true, digits = true;
        int v = 0;
        for (size_t i = at; i < at + n; ++i) {
            if (s[i] != '?') unknown = false;
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) digits = false;
            else v = v * 10 + (s[i] - '0');
        }
        if (unknown) return 0;
        if (!digits || v < 1 || v > max) return -1;
        return v;
    };
    const int y = part(0, 4, 9999), m = part(5, 2, 12), d = part(8, 2, 31);
    if (y < 0 || m < 0 || d < 0) return 0;
    return uint32_t(y) << 9 | uint32_t(m) << 5 | uint32_t(d);
}

std::string formatDate(uint32_t date)
{
    const int y = int(date >> 9), m = int(date >> 5 & 15), d = int(date & 31);
    std::string ys = y ? std::to_string(y) : "????";
    while (ys.size() < 4) ys.insert(ys.begin(), '0');
    std::string r = ys + '.';
    r += m ? (m < 10 ? "0" : "") + std::to_string(m) : "??";
    r += '.';
    r += d ? (d < 10 ? "0" : "") + std::to_string(d) : "??";
    return r;
}

uint16_t parseEco(const std::string& eco)
{
    if (eco.size() != 3 || eco[0] < 'A' || eco[0] > 'E' || !std::isdigit(static_cast<unsigned char>(eco[1]))
        || !std::isdigit(static_cast<unsigned char>(eco[2])))
        return 0;
    return uint16_t((eco[0] - 'A') * 100 + (eco[1] - '0') * 10 + (eco[2] - '0') + 1);
}

std::string formatEco(uint16_t eco)
{
    if (eco == 0 || eco > 500) return {};
    const int v = eco - 1;
    return {char('A' + v / 100), char('0' + v / 10 % 10), char('0' + v % 10)};
}

uint8_t parseResult(const std::string& r)
{
    if (r == "1-0") return WhiteWins;
    if (r == "0-1") return BlackWins;
    if (r == "1/2-1/2") return Draw;
    return ResultNone;
}

const char* formatResult(uint8_t r)
{
    switch (r) {
    case WhiteWins: return "1-0";
    case BlackWins: return "0-1";
    case Draw: return "1/2-1/2";
    default: return "*";
    }
}

void writeHeader(uint8_t* out, const char magic[8], uint64_t count, uint64_t extra)
{
    std::memset(out, 0, HeaderSize);
    std::memcpy(out, magic, 8);
    put32(out + 8, FormatVersion);
    put32(out + 12, uint32_t(HeaderSize));
    put64(out + 16, count);
    put64(out + 24, extra);
}

bool checkHeader(const uint8_t* in, size_t size, const char magic[8], std::string* error)
{
    if (size < HeaderSize || std::memcmp(in, magic, 8) != 0) {
        if (error) *error = "fichier non reconnu (en-tête Hydra64 absent)";
        return false;
    }
    if (get32(in + 8) > FormatVersion) {
        if (error) *error = "version de format trop récente (" + std::to_string(get32(in + 8)) + ")";
        return false;
    }
    return true;
}

} // namespace hy64db
