#include "chessassistant.h"

#include "fileoffset.h"
#include "format.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace chess;

namespace hy64db {

namespace {

// ---------------------------------------------------------------- files

const char* const Extensions[] = {"cdp", "d00", "d01", "d02", "a01", "a02", "axx", "dxx", "emb", "st0", "bo0", "ti0", "td0", "ue0", "uh0"};

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// File `base.ext`, whatever the case of the extension on disk.
std::string existing(const std::string& base, const std::string& ext)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(base + "." + ext, ec)) return base + "." + ext;
    std::string up = ext;
    for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (fs::exists(base + "." + up, ec)) return base + "." + up;
    return base + "." + ext;
}

uint32_t u24(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16; }

// ---------------------------------------------------------------- texts

// cp1251 0x80–0xBF → Unicode (0xC0–0xFF = U+0410…U+044F).
constexpr uint16_t Cp1251High[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};
// cp1252 0x80–0x9F → Unicode (the rest = Latin-1).
constexpr uint16_t Cp1252High[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                     0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                     0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

uint32_t fromCp1251(uint8_t b) { return b < 0x80 ? b : b >= 0xC0 ? 0x0410u + (b - 0xC0) : Cp1251High[b - 0x80]; }
uint32_t fromCp1252(uint8_t b) { return b < 0x80 || b >= 0xA0 ? b : Cp1252High[b - 0x80]; }
int toCp1251(uint32_t cp)
{
    if (cp < 0x80) return int(cp);
    if (cp >= 0x0410 && cp <= 0x044F) return int(0xC0 + cp - 0x0410);
    for (int i = 0; i < 64; ++i)
        if (Cp1251High[i] == cp) return 0x80 + i;
    return -1;
}

void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) {
        out += char(0xC0 | cp >> 6);
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xE0 | cp >> 12);
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// Code points of a UTF-8 text; false if it is not valid.
bool decodeUtf8(const uint8_t* p, size_t n, std::vector<uint32_t>& out)
{
    out.clear();
    for (size_t i = 0; i < n;) {
        const uint8_t c = p[i];
        int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!len || i + size_t(len) > n) return false;
        uint32_t cp = len == 1 ? c : c & (0xFF >> (len + 1));
        for (int k = 1; k < len; ++k) {
            if ((p[i + size_t(k)] & 0xC0) != 0x80) return false;
            cp = cp << 6 | (p[i + size_t(k)] & 0x3F);
        }
        out.push_back(cp);
        i += size_t(len);
    }
    return true;
}

bool asciiLetter(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// 8-bit text (cp1251 or cp1252, with no indication) → UTF-8. Russian: high letters come
// in a row; Latin languages: an isolated accented letter between ASCII letters.
std::string from8bit(const uint8_t* p, size_t n)
{
    int cyrillic = 0, latin = 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < 0xC0) continue;
        const bool prevHigh = i > 0 && p[i - 1] >= 0xC0, nextHigh = i + 1 < n && p[i + 1] >= 0xC0;
        const bool prevAscii = i > 0 && asciiLetter(p[i - 1]), nextAscii = i + 1 < n && asciiLetter(p[i + 1]);
        if (prevHigh || nextHigh) ++cyrillic;
        else if (prevAscii || nextAscii) ++latin;
    }
    std::string out;
    out.reserve(n + n / 4);
    for (size_t i = 0; i < n; ++i) appendUtf8(out, cyrillic > latin ? fromCp1251(p[i]) : fromCp1252(p[i]));
    return out;
}

// Text of a dictionary (UTF-8) or of a comment (8 bits). In the dictionaries, the original
// accented letters (Latin-1) were reread as cp1251 when the database was created ("é" turned into
// "й"): we restore them when a Cyrillic letter touches a Latin letter.
std::string text(const uint8_t* p, size_t n, bool dictionary)
{
    bool ascii = true;
    for (size_t i = 0; i < n && ascii; ++i) ascii = p[i] < 0x80;
    if (ascii) return std::string(reinterpret_cast<const char*>(p), n);
    std::vector<uint32_t> cps;
    if (!decodeUtf8(p, n, cps)) return from8bit(p, n);
    if (!dictionary) return std::string(reinterpret_cast<const char*>(p), n);
    bool mixed = false, convertible = true;
    for (size_t i = 0; i < cps.size(); ++i) {
        if (cps[i] < 0x80) continue;
        if (toCp1251(cps[i]) < 0) convertible = false;
        if ((i > 0 && asciiLetter(cps[i - 1])) || (i + 1 < cps.size() && asciiLetter(cps[i + 1]))) mixed = true;
    }
    if (!mixed || !convertible) return std::string(reinterpret_cast<const char*>(p), n);
    std::string out;
    for (uint32_t cp : cps) appendUtf8(out, fromCp1252(uint8_t(toCp1251(cp))));
    return out;
}

// ---------------------------------------------------------------- volume (.emb)

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    hy64db::seek64(f, 0, SEEK_END);
    const int64_t size = hy64db::tell64(f);
    hy64db::seek64(f, 0, SEEK_SET);
    out.resize(size > 0 ? size_t(size) : 0);
    const bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// Embedded file `name`: directory entry (at multiples of 0x100) = u32 0, u32 2,
// u32 size, u32 offset of the extent table, 12 bytes, name; table = (offset, length) pairs.
bool embedded(const std::vector<uint8_t>& vol, const char* name, std::vector<uint8_t>& out)
{
    out.clear();
    const size_t len = std::strlen(name);
    for (size_t p = 0x200; p + 0x100 <= vol.size(); p += 0x100) {
        if (std::memcmp(vol.data() + p + 0x1c, name, len + 1) != 0) continue;
        const uint32_t size = get32(vol.data() + p + 8);
        size_t q = get32(vol.data() + p + 12);
        while (out.size() < size) {
            if (q + 8 > vol.size()) return false;
            const uint32_t off = get32(vol.data() + q), n = get32(vol.data() + q + 4);
            q += 8;
            if (n == 0 || uint64_t(off) + n > vol.size()) return false;
            out.insert(out.end(), vol.begin() + off, vol.begin() + off + n);
        }
        out.resize(size);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- moves

constexpr int KnightD[8][2] = {{-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}, {1, 2}, {2, 1}, {2, -1}, {1, -2}};
constexpr int KingD[8][2] = {{1, 1}, {7, 7}, {7, 1}, {1, 7}, {7, 0}, {1, 0}, {0, 7}, {0, 1}};
// Displacements modulo 8: diagonal A (7), diagonal B (6), rook (14). Bishop = 0…12, rook = 13…26, queen = 0…26.
constexpr int SliderD[27][2] = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}, {7, 1}, {6, 2}, {5, 3}, {3, 5}, {2, 6}, {1, 7}, {7, 0},
                                {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}, {0, 7}, {0, 6}, {0, 5}, {0, 4}, {0, 3}, {0, 2}, {0, 1}};

// Slots of one side: pawns 0–7, knights 8–9, bishops 10–11, rooks 12–13, queen 14, king 15,
// extra promoted pieces 16–17. The number follows the piece for the whole game.
struct Side {
    int8_t sq[18];
    uint8_t promoted = 0; // pawn slots occupied by a promoted piece
};

struct State {
    Position pos;
    Side side[2];

    Side& us() { return side[pos.sideToMove()]; }
    Side& them() { return side[pos.sideToMove() ^ 1]; }
};

void startState(State& st)
{
    st.pos = Position::startPos();
    for (int c = 0; c < 2; ++c) {
        Side& s = st.side[c];
        const int back = c == White ? 0 : 7, pawns = c == White ? 1 : 6;
        for (int f = 0; f < 8; ++f) s.sq[f] = makeSquare(f, pawns);
        const int files[8] = {1, 6, 2, 5, 0, 7, 3, 4}; // C, C, F, F, T, T, D, R
        for (int i = 0; i < 8; ++i) s.sq[8 + i] = makeSquare(files[i], back);
        s.sq[16] = s.sq[17] = NoSquare;
        s.promoted = 0;
    }
}

struct Decoder {
    const uint8_t* p;
    const uint8_t* end;
    Game& game;
    std::string error;

    bool fail(const std::string& why)
    {
        if (error.empty()) error = why;
        return false;
    }

    // Length of an element starting at p (to skip a variation); 0 if truncated.
    static size_t tokenSize(const uint8_t* q, const uint8_t* end)
    {
        const size_t left = size_t(end - q);
        size_t n = 1;
        switch (*q) {
        case 0xD1: n = 3; break;
        case 0xD4: case 0xD5: case 0xD6: case 0xD7: case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD:
        case 0xDE: case 0xDF: case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: n = 2; break;
        case 0xE6: n = 77; break;
        case 0xE9: n = 2; break;
        case 0xEA: n = 3; break;
        case 0xEB: n = left >= 2 ? 2 + size_t(q[1]) : 0; break;
        case 0xEC: n = left >= 5 ? 5 + size_t(get32(q + 1)) : 0; break;
        default: break;
        }
        return n && n <= left ? n : 0;
    }

    // Annotation at p (E9…EC) → comment or NAG of `node`.
    bool annotation(Node* node)
    {
        const uint8_t marker = *p;
        const size_t size = tokenSize(p, end);
        if (!size) return fail("annotation tronquée");
        const uint8_t* body = marker == 0xEB ? p + 2 : marker == 0xEC ? p + 5 : p + 1;
        const uint8_t kind = *body++;
        const size_t n = size_t(p + size - body);
        p += size;
        // Kinds (Aquarium's table): 0 Ctrl, 1 bCtr, 2 Shrt, 3 bShr, 4 Long, 5 bLon, 6 Val, 7 Temp,
        // 8 Dgrm, 9 bDgr, 10 Time, 11 bTim, 12 Tab, 13 Tabs, 14 Mrkr, 15 bMrk, 16 Tutr, 17 bTut,
        // 18 1Anl, 19 b1An, 20 Summ, 21 Fldr, 22 3Anl, 23 b2An, 24 Medi, 25 b4An, 26 Obst, 27 Numb,
        // 28 Refr, 29 CVal, 30 Clas, 31 NTut ("b" = before the move). Kept: symbols (2, 3),
        // texts (4, 5) and engine evaluation (29); the rest is specific to Aquarium.
        if (n == 0) return true;
        if (kind == 29) { // evaluation: i16 centipawns (White's point of view), depth (255: unknown), flags, engine
            if (n < 2 || !node->parent) return true;
            const int score = int16_t(get16(body));
            char buf[40];
            if (score >= 29000 || score <= -29000) std::snprintf(buf, sizeof buf, "[%%eval #%s%d]", score < 0 ? "-" : "", 30000 - std::abs(score));
            // Written as integers: "%.2f" follows the system language and gave "-0,47" in French.
            else std::snprintf(buf, sizeof buf, "[%%eval %s%d.%02d]", score < 0 ? "-" : "", std::abs(score) / 100, std::abs(score) % 100);
            append(node, buf);
            return true;
        }
        if (kind == 10 && (n == 6 || n == 9)) { // clocks: White, Black (and move time), in tenths of a second
            // Three digits in base 200, each offset by 0x21. The clock kept is that of the side that just moved.
            if (!node->parent) return true;
            const uint8_t* t = node->parent->pos.sideToMove() == White ? body : body + 3;
            if (t[0] >= 0x21 && t[1] >= 0x21 && t[2] >= 0x21) node->clockMs = (((t[0] - 0x21) * 200 + (t[1] - 0x21)) * 200 + (t[2] - 0x21)) * 100;
            return true;
        }
        if (kind != 2 && kind != 3 && kind != 4 && kind != 5) return true;
        if (kind == 2 || kind == 3) { // symbol(s) of the annotation font
            struct Nag {
                const char* bytes;
                int nag;
            };
            static const Nag nags[] = {{"!", 1}, {"?", 2}, {"!!", 3}, {"??", 4}, {"!?", 5}, {"?!", 6}, {"=", 10}, {"\x9b", 13},
                                       {"\xb2", 14}, {"\xb3", 15}, {"\xb1", 16}, {"\xb5", 17}, {"\x9d", 18}, {"\xb0", 19}};
            for (const Nag& g : nags)
                if (std::strlen(g.bytes) == n && std::memcmp(g.bytes, body, n) == 0) {
                    if (node->parent) node->nags.push_back(g.nag);
                    return true;
                }
            static const std::pair<uint8_t, const char*> symbols[] = {{0x82, "→"}, {0x83, "↑"}, {0x84, "⇄"}, {0x87, "⨀"}, {0x93, "⊕"},
                                                                      {0x99, "□"}, {0x9b, "∞"}, {0x9d, "+−"}, {0xb0, "−+"}, {0xb1, "±"},
                                                                      {0xb2, "⩲"}, {0xb3, "⩱"}, {0xb5, "∓"}};
            std::string s;
            for (size_t i = 0; i < n; ++i) {
                const char* sym = nullptr;
                for (const auto& [code, t] : symbols)
                    if (code == body[i]) sym = t;
                if (sym) s += sym;
                else if (body[i] >= 0x20 && body[i] < 0x7F) s += char(body[i]);
            }
            append(node, s);
            return true;
        }
        std::string t = text(body, n, false);
        for (char& c : t)
            if (c == '\r' || c == '\n' || c == '}') c = c == '}' ? ')' : ' ';
        // Signs of the annotation font in the middle of a text ("[…¥b7]", "[…g4 +-]"): in front of
        // a square, a capture or another figurine, they are a figurine or the "with the
        // idea" sign, not "¥" nor an ellipsis. Table aligned with Aquarium's PGN export.
        if (t.find("\xC2") != std::string::npos || t.find("\xE2\x80\xA6") != std::string::npos) {
            static const std::pair<const char*, const char*> glyphs[] = {{"\xC2\xA2", "♔"}, {"\xC2\xA3", "♕"}, {"\xC2\xA4", "♘"}, {"\xC2\xA5", "♗"},
                                                                          {"\xC2\xA6", "♖"}, {"\xE2\x80\xA6", "△"}};
            auto startsMove = [&](size_t at) {
                if (at >= t.size()) return false;
                const unsigned char c = static_cast<unsigned char>(t[at]);
                if ((c >= 'a' && c <= 'h') || c == 'x' || (c >= '1' && c <= '8')) return true;
                for (const auto& [from, to] : glyphs)
                    if (std::string_view(from) != "\xE2\x80\xA6" && t.compare(at, std::strlen(from), from) == 0) return true;
                return t.compare(at, 3, "♔") == 0 || t.compare(at, 3, "♕") == 0 || t.compare(at, 3, "♘") == 0 || t.compare(at, 3, "♗") == 0 || t.compare(at, 3, "♖") == 0;
            };
            std::string out;
            for (size_t i = 0; i < t.size();) {
                const char* to = nullptr;
                size_t len = 0;
                for (const auto& [from, target] : glyphs)
                    if (t.compare(i, std::strlen(from), from) == 0 && startsMove(i + std::strlen(from))) to = target, len = std::strlen(from);
                if (to) out += to, i += len;
                else out += t[i++];
            }
            t = std::move(out);
        }
        append(node, t);
        return true;
    }

    static void append(Node* node, const std::string& t)
    {
        size_t b = 0, e = t.size();
        while (b < e && t[b] == ' ') ++b;
        while (e > b && t[e - 1] == ' ') --e;
        if (b == e) return;
        if (!node->comment.empty()) node->comment += ' ';
        node->comment.append(t, b, e - b);
    }

    // Move at p; the state is updated. False if the move is unreadable or illegal.
    bool move(State& st, Move& m)
    {
        if (p >= end) return fail("partie tronquée");
        uint8_t c = *p++;
        bool enPassant = false;
        int promo = -1;
        if (c == 0xD2) {
            if (p >= end) return fail("partie tronquée");
            enPassant = true;
            c = *p++;
        }
        if (c == 0xD1) {
            if (end - p < 2) return fail("promotion tronquée");
            c = p[0];
            promo = p[1];
            p += 2;
        }
        const Color us = st.pos.sideToMove();
        Side& mine = st.us();
        m = Move();
        if (c == 0xD3) { // null move
            st.pos.makeMove(m);
            return true;
        }
        int slot = -1, df = 0, dr = 0;
        bool torus = true, castle = false;
        if (c < 0x20) {
            slot = c >> 2;
            const int dir = us == White ? 1 : -1, k = c & 3;
            df = k == 1 ? -1 : k == 2 ? 1 : 0;
            dr = k == 3 ? 2 * dir : dir;
            torus = (mine.promoted >> slot) & 1; // promoted piece left in the pawn's slot
        } else if (c < 0x30) {
            slot = 8 + (c - 0x20) / 8;
            df = KnightD[(c - 0x20) % 8][0], dr = KnightD[(c - 0x20) % 8][1];
            torus = false;
        } else if (c < 0x4A) {
            slot = 10 + (c - 0x30) / 13;
            df = SliderD[(c - 0x30) % 13][0], dr = SliderD[(c - 0x30) % 13][1];
        } else if (c < 0x66) {
            slot = 12 + (c - 0x4A) / 14;
            df = SliderD[13 + (c - 0x4A) % 14][0], dr = SliderD[13 + (c - 0x4A) % 14][1];
        } else if (c < 0x81) {
            slot = 14;
            df = SliderD[c - 0x66][0], dr = SliderD[c - 0x66][1];
        } else if (c < 0x89) {
            slot = 15;
            df = KingD[c - 0x81][0], dr = KingD[c - 0x81][1];
        } else if (c < 0xCF) { // extra piece: 27 queen codes then 8 knight codes
            slot = 16 + (c - 0x89) / 35;
            const int code = (c - 0x89) % 35;
            if (code < 27) df = SliderD[code][0], dr = SliderD[code][1];
            else df = KnightD[code - 27][0], dr = KnightD[code - 27][1], torus = false;
        } else if (c == 0xCF || c == 0xD0) {
            slot = 15;
            castle = true;
        } else if (c >= 0xD4 && c <= 0xE5) { // long move of piece no. (c − D4): Δrank << 4 | Δfile, modulo 8
            if (p >= end) return fail("partie tronquée");
            slot = c - 0xD4;
            if (slot < 8 && !((mine.promoted >> slot) & 1)) return fail("coup long d'un pion");
            df = *p & 15, dr = *p >> 4;
            ++p;
        } else {
            char buf[48];
            std::snprintf(buf, sizeof buf, "octet de coup inconnu (%02X)", c);
            return fail(buf);
        }
        const Square from = mine.sq[slot];
        if (from == NoSquare) return fail("coup d'une pièce absente");
        int f = fileOf(from) + df, r = rankOf(from) + dr;
        bool kingside = c == 0xD0;
        if (castle) {
            m = st.pos.castlingMove(kingside); // the position's form (Chess960: the king captures its rook)
            if (m.isNull()) return fail("roque sans droit de roque");
        } else {
            if (torus) f &= 7, r &= 7;
            else if (f < 0 || f > 7 || r < 0 || r > 7) return fail("coup hors de l'échiquier");
            m.from = from;
            m.to = makeSquare(f, r);
            // Chess960: Chess Assistant sometimes writes castling as the lone move of the king (c1→g1) or
            // of the rook (a8→d8, over the king) to its target square.
            if (st.pos.isChess960() && promo < 0 && !st.pos.isLegal(m)) {
                for (bool side : {true, false}) {
                    const Move cm = st.pos.castlingMove(side);
                    if (cm.isNull() || !st.pos.isLegal(cm)) continue;
                    const int rank = rankOf(cm.from);
                    const bool kingJump = slot == 15 && m.to == makeSquare(side ? 6 : 2, rank);
                    const bool rookJump = from == st.pos.castlingRook(us, side) && m.to == makeSquare(side ? 5 : 3, rank);
                    if (kingJump || rookJump) {
                        m = cm;
                        castle = true;
                        kingside = side;
                        break;
                    }
                }
            }
        }
        const bool pawnMove = !castle && typeOf(st.pos.at(from)) == Pawn;
        if (promo >= 0) {
            static constexpr PieceType kinds[4] = {Knight, Bishop, Rook, Queen};
            const int kind = (promo - 0x40) >> 5;
            if (!pawnMove || promo < 0x40 || kind > 3) return fail("promotion incohérente");
            m.promo = kinds[kind];
        }
        const bool isEp = pawnMove && m.to == st.pos.epSquare() && fileOf(m.to) != fileOf(from);
        if (isEp != enPassant || !st.pos.isLegal(m)) return fail("coup illégal (" + Position::toUci(m) + ") dans " + st.pos.fen());

        // Slots: captured piece, castling, moved piece, promotion
        Side& theirs = st.them();
        const Square captured = isEp ? makeSquare(fileOf(m.to), rankOf(from)) : m.to;
        for (int i = 0; i < 18 && !castle; ++i)
            if (theirs.sq[i] == captured) {
                theirs.sq[i] = NoSquare;
                if (i < 8) theirs.promoted &= uint8_t(~(1u << i));
            }
        if (castle) { // king to g or c, rook to f or d
            const int rank = rankOf(m.from);
            const Square rookFrom = st.pos.castlingRook(us, kingside);
            for (int i = 0; i < 18; ++i)
                if (i != 15 && mine.sq[i] == rookFrom) mine.sq[i] = makeSquare(kingside ? 5 : 3, rank);
            mine.sq[15] = makeSquare(kingside ? 6 : 2, rank);
        } else {
            mine.sq[slot] = m.to;
        }
        if (promo >= 0) {
            const int index = promo & 0x1F, kind = (promo - 0x40) >> 5;
            mine.sq[slot] = NoSquare;
            if (index < 8) { // no free slot left: the piece keeps the pawn's number
                if (index != slot) return fail("promotion incohérente");
                mine.promoted |= uint8_t(1u << index);
            } else if (index >= 18 || index == 15 || (index < 14 && (index - 8) / 2 != kind) || (index == 14 && kind != 3) || mine.sq[index] != NoSquare) {
                return fail("promotion incohérente");
            }
            mine.sq[index] = m.to;
        }
        st.pos.makeMove(m);
        return true;
    }

    // Line of moves from `from` (state `st`), up to E8 or the end.
    bool line(Node* from, State st)
    {
        Node* cur = from;
        std::vector<const uint8_t*> pending; // variations met before the move they replace
        while (p < end && *p != 0xE8) {
            const uint8_t c = *p;
            if (c == 0xE7) {
                pending.push_back(p + 1);
                int depth = 0;
                do {
                    if (p >= end) return fail("variante non fermée");
                    if (*p == 0xE7) ++depth;
                    else if (*p == 0xE8) --depth;
                    const size_t n = tokenSize(p, end);
                    if (!n) return fail("variante tronquée");
                    p += n;
                } while (depth > 0);
                continue;
            }
            if (c >= 0xE9 && c <= 0xEC) {
                if (!annotation(cur)) return false;
                continue;
            }
            if (c == 0xE6) return fail("position de départ au milieu de la partie");
            const State before = st;
            Move m;
            if (!move(st, m)) return false;
            Node* next = game.addMove(cur, m, true);
            if (!flush(cur, before, pending)) return false;
            cur = next;
        }
        return flush(cur, st, pending); // variations with no main move after them
    }

    bool flush(Node* parent, const State& st, std::vector<const uint8_t*>& pending)
    {
        if (pending.empty()) return true;
        const uint8_t* resume = p;
        for (const uint8_t* start : pending) {
            p = start;
            if (!line(parent, st)) return false;
            if (p >= end || *p != 0xE8) return fail("variante non fermée");
        }
        pending.clear();
        p = resume;
        return true;
    }
};

// E6 block: 36 × (square, type) — 18 white slots then black — u16 half-move, castling rights, byte.
bool setup(const uint8_t* d, State& st, std::string* error)
{
    static constexpr char letters[] = " PNBRQK";
    Piece board[64] = {};
    for (int c = 0; c < 2; ++c) {
        Side& s = st.side[c];
        s.promoted = 0;
        for (int i = 0; i < 18; ++i) {
            const uint8_t sq = d[(c * 18 + i) * 2], type = d[(c * 18 + i) * 2 + 1];
            s.sq[i] = NoSquare;
            if (type == 0) continue;
            if (type > 6 || (sq & 0x88)) {
                if (error) *error = "position de départ abîmée";
                return false;
            }
            s.sq[i] = makeSquare(sq & 7, sq >> 4);
            board[s.sq[i]] = makePiece(Color(c), PieceType(type));
        }
    }
    const int ply = d[72] | d[73] << 8;
    std::string fen;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            const Piece pc = board[makeSquare(f, r)];
            if (pc == NoPiece) {
                ++empty;
                continue;
            }
            if (empty) fen += char('0' + empty);
            empty = 0;
            const char l = letters[typeOf(pc)];
            fen += colorOf(pc) == White ? l : char(l + 32);
        }
        if (empty) fen += char('0' + empty);
        if (r) fen += '/';
    }
    fen += ply % 2 ? " b " : " w ";
    // Castling: rights from the mask, resolved by Position (outermost rook on that side of the king;
    // a right without a king on its rank or without a rook is ignored), Chess960 included.
    std::string rights;
    if (d[74] & 1) rights += 'K';
    if (d[74] & 2) rights += 'Q';
    if (d[74] & 4) rights += 'k';
    if (d[74] & 8) rights += 'q';
    fen += (rights.empty() ? "-" : rights) + " - 0 " + std::to_string(ply / 2 + 1);
    if (!st.pos.setFen(fen)) {
        if (error) *error = "position de départ invalide : " + fen;
        return false;
    }
    return true;
}

enum { Players = 0, Events = 1, Places = 2, Sources = 3 };

} // namespace

// ---------------------------------------------------------------- database

bool ChessAssistantSource::isDatabaseFile(const std::string& path)
{
    const std::string ext = lower(std::filesystem::path(path).extension().string());
    if (ext.size() < 2) return false;
    for (const char* e : Extensions)
        if (ext.compare(1, std::string::npos, e) == 0) {
            // .d00… exist elsewhere: the database's .cdp is needed beside them
            std::error_code ec;
            return std::filesystem::exists(existing(basePath(path), "cdp"), ec);
        }
    return false;
}

std::string ChessAssistantSource::basePath(const std::string& path)
{
    const std::filesystem::path p(path);
    return (p.parent_path() / p.stem()).string();
}

bool ChessAssistantSource::open(const std::string& path, std::string* error)
{
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    const std::string base = basePath(path);
    std::vector<uint8_t> cdp;
    if (!readFile(existing(base, "cdp"), cdp) || cdp.size() < 0x70) return fail(base + ".cdp : illisible");
    const std::string head(reinterpret_cast<const char*>(cdp.data()), 0x68);
    if (head.compare(0, 15, "Chess Assistant") != 0 || head.find("Chess Data Pro format") == std::string::npos)
        return fail("ce n'est pas une base Chess Assistant (Chess Data Pro)");
    m_total = get32(cdp.data() + 0x68);
    if (!m_headers.open(existing(base, "d00"), error) || !m_moves.open(existing(base, "d02"), error) || !m_moveIndex.open(existing(base, "a02"), error))
        return false;
    if (m_headers.size() < m_total * 38 || m_moveIndex.size() < m_total * 4) return fail("base Chess Assistant tronquée (ou d'un format d'en-têtes inconnu)");

    // Deleted games: .dxx, one bit per game, most significant first
    std::vector<uint8_t> deleted;
    readFile(existing(base, "dxx"), deleted);
    m_games.clear();
    m_games.reserve(m_total);
    for (uint64_t i = 0; i < m_total; ++i)
        if (i / 8 >= deleted.size() || !((deleted[i / 8] >> (7 - i % 8)) & 1)) m_games.push_back(uint32_t(i));

    // Dictionaries
    std::vector<uint8_t> vol, index;
    if (!readFile(existing(base, "emb"), vol)) return fail(base + ".emb : illisible");
    static const char* const names[4] = {"Players", "Events", "Places", "Sources"};
    for (int d = 0; d < 4; ++d) {
        Dictionary& dict = m_dict[d];
        const std::string n = names[d];
        if (!embedded(vol, (n + ".a01").c_str(), index) || !embedded(vol, (n + ".d01").c_str(), dict.text)) return fail(base + ".emb : dictionnaire " + n + " illisible");
        dict.offsets.resize(index.size() / 4 + 1);
        for (size_t i = 0; i + 1 < dict.offsets.size(); ++i) dict.offsets[i] = get32(index.data() + i * 4);
        dict.offsets.back() = uint32_t(dict.text.size());
    }
    return true;
}

// Record of a dictionary: one length (varint) per field, then the texts. Players: name, title.
std::string ChessAssistantSource::name(int dictionary, uint32_t id, std::string* title) const
{
    const Dictionary& d = m_dict[dictionary];
    if (id + 1 >= d.offsets.size() || d.offsets[id] > d.offsets[id + 1] || d.offsets[id + 1] > d.text.size()) return {};
    const uint8_t* p = d.text.data() + d.offsets[id];
    const uint8_t* end = d.text.data() + d.offsets[id + 1];
    uint64_t len[2] = {0, 0};
    const int fields = dictionary == Players ? 2 : 1;
    for (int i = 0; i < fields; ++i)
        if (!getVarint(p, end, len[i])) return {};
    if (len[0] + len[1] > uint64_t(end - p)) return {};
    if (title) title->assign(reinterpret_cast<const char*>(p + len[0]), size_t(len[1]));
    return text(p, size_t(len[0]), true);
}

bool ChessAssistantSource::read(uint64_t index, Game& game, std::string* error) const
{
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (index >= m_games.size()) return fail("partie hors de la base");
    const uint32_t id = m_games[size_t(index)];

    // Moves: length as a varint, then the game
    const uint64_t at = get32(m_moveIndex.data() + uint64_t(id) * 4);
    if (at >= m_moves.size()) return fail("partie hors du fichier des coups");
    const uint8_t* p = m_moves.data() + at;
    const uint8_t* fileEnd = m_moves.data() + m_moves.size();
    uint64_t size = 0;
    if (!getVarint(p, fileEnd, size) || size > uint64_t(fileEnd - p)) return fail("partie tronquée");
    const uint8_t* end = p + size;

    State st;
    bool customStart = false;
    if (p < end && *p == 0xE6) {
        if (end - p < 77) return fail("position de départ tronquée");
        if (!setup(p + 1, st, error)) return false;
        p += 77;
        customStart = true;
        game.reset(st.pos);
    } else {
        startState(st);
        game.reset();
    }
    game.headers().clear();

    // Headers: 38 bytes
    const uint8_t* h = m_headers.data() + uint64_t(id) * 38;
    auto orUnknown = [](const std::string& s) { return s.empty() ? std::string("?") : s; };
    std::string whiteTitle, blackTitle;
    const std::string white = name(Players, u24(h), &whiteTitle), black = name(Players, u24(h + 3), &blackTitle);
    game.setHeader("Event", orUnknown(name(Events, u24(h + 14))));
    game.setHeader("Site", orUnknown(name(Places, u24(h + 17))));
    {
        const uint32_t date = u24(h + 11), year = date / 416, month = date % 416 / 32, day = date % 32;
        char buf[16];
        std::string s;
        if (year) std::snprintf(buf, sizeof buf, "%04u.", year), s = buf;
        else s = "????.";
        if (month && month <= 12) std::snprintf(buf, sizeof buf, "%02u.", month), s += buf;
        else s += "??.";
        // A day that does not exist in the month (31 November) is unknown, as Aquarium exports it.
        static const unsigned days[13] = {31, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const unsigned limit = month == 2 && year % 4 != 0 ? 28 : days[month <= 12 ? month : 0];
        if (day && day <= limit) std::snprintf(buf, sizeof buf, "%02u", day), s += buf;
        else s += "??";
        game.setHeader("Date", s);
    }
    {
        // Signed integer: round = value / 512, board = value mod 512 (read from Aquarium's DLL,
        // confirmed by its export: "-57", and ".2" for a game without a round number).
        const int round = int16_t(get16(h + 28));
        std::string s = "?";
        if (round) {
            s = round / 512 ? std::to_string(round / 512) : std::string();
            if (round % 512) s += "." + std::to_string(std::abs(round % 512));
        }
        game.setHeader("Round", s);
    }
    game.setHeader("White", orUnknown(white));
    game.setHeader("Black", orUnknown(black));
    static const char* const results[4] = {"*", "1/2-1/2", "1-0", "0-1"};
    game.setHeader("Result", h[10] < 4 ? results[h[10]] : "*");
    if (const uint16_t elo = get16(h + 6)) game.setHeader("WhiteElo", std::to_string(elo));
    if (const uint16_t elo = get16(h + 8)) game.setHeader("BlackElo", std::to_string(elo));
    if (const uint16_t eco = get16(h + 30); eco < 500) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%c%02d", 'A' + eco / 100, eco % 100);
        game.setHeader("ECO", buf);
    }
    if (!whiteTitle.empty()) game.setHeader("WhiteTitle", whiteTitle);
    if (!blackTitle.empty()) game.setHeader("BlackTitle", blackTitle);
    if (const uint32_t a = u24(h + 32); a != 0xFFFFFF)
        if (const std::string s = name(Players, a); !s.empty()) game.setHeader("Annotator", s);
    if (const uint32_t s = u24(h + 35); s != 0xFFFFFF)
        if (const std::string t = name(Sources, s); !t.empty()) game.setHeader("Source", t);
    if (customStart) {
        game.setHeader("SetUp", "1");
        game.setHeader("FEN", st.pos.fen());
    }

    Decoder dec{p, end, game, {}};
    if (!dec.line(game.root(), st) || dec.p != end) {
        game.setCurrent(game.root());
        return fail(dec.error.empty() ? "partie abîmée" : dec.error);
    }
    game.setCurrent(game.root());
    return true;
}

} // namespace hy64db
