#include "gamecodec.h"

#include "beauty.h"

#include "chess/pgn.h"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>

using namespace chess;

namespace hy64db {

namespace {

// ---------------------------------------------------------------- moves

enum Marker : uint8_t { NullMove = 10, NagCode = 11, CommentCode = 12, StartVar = 13, EndVar = 14, Extension = 15 };
// ExtGraphics: count (varint), then per graphic annotation: from square, to square (255:
// marked square), colour ('G', 'R', 'Y', 'B').
// ExtMedals: ChessBase medals of the move (varint).
// ExtQuestion: training question (length as a varint, then the wording in UTF-8).
// ExtPreComment: pre-move comment (Node::preComment); its text is the next one of the
// text section, as for CommentCode (no extra byte in the moves).
// ExtQuestionData: full ChessBase question (Node::questionData), next text of the text
// section; always follows an ExtQuestion.
enum ExtCode : uint8_t { ExtEnd = 0, ExtClock = 1, ExtGraphics = 2, ExtMedals = 3, ExtQuestion = 4, ExtPreComment = 5, ExtQuestionData = 6 };

constexpr int KingDirs[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
constexpr int KnightJumps[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr PieceType PromoOrder[4] = {Queen, Rook, Bishop, Knight};

// Pieces of side `c`: king first, then by increasing square. Returns the count (≤ 16), -1 if more.
int pieceList(const Position& pos, Color c, Square out[16])
{
    int n = 1;
    out[0] = pos.kingSquare(c);
    if (out[0] == NoSquare) return -1;
    for (Square s = 0; s < 64; ++s) {
        const Piece p = pos.at(s);
        if (p == NoPiece || colorOf(p) != c || typeOf(p) == King) continue;
        if (n == 16) return -1;
        out[n++] = s;
    }
    return n;
}

int promoIndex(PieceType t)
{
    for (int i = 0; i < 4; ++i)
        if (PromoOrder[i] == t) return i;
    return 0;
}

bool encodeMove(const Position& pos, Move m, std::vector<uint8_t>& out)
{
    if (m.isNull()) {
        out.push_back(NullMove);
        return true;
    }
    // Number of the piece played in the side's list (see pieceList: king first, then by increasing
    // square, 16 pieces at most), without building the list.
    const Color us = pos.sideToMove();
    const Piece moved = pos.at(m.from);
    if (pos.kingSquare(us) == NoSquare || moved == NoPiece || colorOf(moved) != us) return false;
    int idx = 0;
    if (m.from != pos.kingSquare(us)) {
        if (typeOf(moved) == King) return false; // second king (irregular position)
        int before = 0, others = 0;
        for (Square s = 0; s < 64; ++s) {
            const Piece p = pos.at(s);
            if (p == NoPiece || colorOf(p) != us || typeOf(p) == King) continue;
            ++others;
            if (s < m.from) ++before;
        }
        if (others > 15) return false;
        idx = 1 + before;
    }
    const PieceType t = typeOf(pos.at(m.from));
    const int f0 = fileOf(m.from), r0 = rankOf(m.from), f1 = fileOf(m.to), r1 = rankOf(m.to);
    const int df = f1 - f0, dr = r1 - r0;
    int code = -1;
    switch (t) {
    case King:
        if (pos.isCastling(m)) code = pos.isKingsideCastling(m) ? 8 : 9; // castling (classical or Chess960)
        else
            for (int i = 0; i < 8; ++i)
                if (KingDirs[i][0] == df && KingDirs[i][1] == dr) code = i;
        break;
    case Knight:
        for (int i = 0; i < 8; ++i)
            if (KnightJumps[i][0] == df && KnightJumps[i][1] == dr) code = i;
        break;
    case Pawn: {
        const int dir = pos.sideToMove() == White ? 1 : -1;
        const bool promo = m.promo != NoType;
        if (df == 0 && dr == 2 * dir) code = 1;
        else if (df == 0 && dr == dir) code = promo ? 4 + promoIndex(m.promo) : 0;
        else if (df == -1 && dr == dir) code = promo ? 8 + promoIndex(m.promo) : 2;
        else if (df == 1 && dr == dir) code = promo ? 12 + promoIndex(m.promo) : 3;
        break;
    }
    case Rook:
        if (dr == 0 && df != 0) code = f1;
        else if (df == 0 && dr != 0) code = 8 + r1;
        break;
    case Bishop:
        if (df != 0 && dr == df) code = f1;
        else if (df != 0 && dr == -df) code = 8 + f1;
        break;
    case Queen:
        if (dr == 0 && df != 0) code = f1;
        else if (df == 0 && dr != 0) code = 8 + r1;
        else if (df != 0 && std::abs(dr) == std::abs(df)) {
            out.push_back(uint8_t(idx << 4 | f0)); // "same file": diagonal, next square
            out.push_back(uint8_t(0x40 | m.to)); // 64 + square: never mistaken for a marker
            return true;
        }
        break;
    default: break;
    }
    if (code < 0) return false;
    out.push_back(uint8_t(idx << 4 | code));
    return true;
}

// Decodes a move (byte `b`, possibly followed by a second one). False if inconsistent.
bool decodeMove(const Position& pos, uint8_t b, const uint8_t*& p, const uint8_t* end, Move& m)
{
    Square list[16];
    const int n = pieceList(pos, pos.sideToMove(), list);
    const int idx = b >> 4, code = b & 15;
    if (idx >= n) return false;
    const Square from = list[idx];
    const int f0 = fileOf(from), r0 = rankOf(from);
    int f1 = -1, r1 = -1;
    m = Move();
    m.from = from;
    switch (typeOf(pos.at(from))) {
    case King:
        if (code == 8 || code == 9) { // castling, in the position's form (Chess960: the king captures its rook)
            m = pos.castlingMove(code == 8);
            return !m.isNull() && m.from == from;
        } else if (code < 8) {
            f1 = f0 + KingDirs[code][0];
            r1 = r0 + KingDirs[code][1];
        }
        break;
    case Knight:
        if (code < 8) {
            f1 = f0 + KnightJumps[code][0];
            r1 = r0 + KnightJumps[code][1];
        }
        break;
    case Pawn: {
        const int dir = pos.sideToMove() == White ? 1 : -1;
        if (code == 1) f1 = f0, r1 = r0 + 2 * dir;
        else {
            f1 = code == 0 || (code >= 4 && code <= 7) ? f0 : (code == 2 || (code >= 8 && code <= 11)) ? f0 - 1 : f0 + 1;
            r1 = r0 + dir;
            if (code >= 4) m.promo = PromoOrder[code & 3];
        }
        break;
    }
    case Rook:
        if (code < 8) f1 = code, r1 = r0;
        else f1 = f0, r1 = code - 8;
        break;
    case Bishop:
        f1 = code & 7;
        r1 = code < 8 ? r0 + (f1 - f0) : r0 - (f1 - f0);
        break;
    case Queen:
        if (code == f0) { // diagonal: target square in the next byte
            if (p >= end) return false;
            const uint8_t to = *p++;
            if ((to & 0xC0) != 0x40) return false;
            f1 = fileOf(Square(to & 63));
            r1 = rankOf(Square(to & 63));
        } else if (code < 8) f1 = code, r1 = r0;
        else f1 = f0, r1 = code - 8;
        break;
    default: return false;
    }
    if (f1 < 0 || f1 > 7 || r1 < 0 || r1 > 7) return false;
    m.to = makeSquare(f1, r1);
    if (m.to == m.from) return false;
    const Piece target = pos.at(m.to);
    return target == NoPiece || colorOf(target) != pos.sideToMove();
}

// ---------------------------------------------------------------- text (headers, comments)

void putString(std::vector<uint8_t>& out, std::string_view s)
{
    putVarint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

bool getString(const uint8_t*& p, const uint8_t* end, std::string& s)
{
    uint64_t n = 0;
    if (!getVarint(p, end, n) || uint64_t(end - p) < n) return false;
    s.assign(reinterpret_cast<const char*>(p), size_t(n));
    p += n;
    return true;
}

uint16_t parseElo(const std::string& s)
{
    if (s.empty() || s.size() > 5 || s[0] == '0') return 0;
    unsigned v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return 0;
        v = v * 10 + unsigned(c - '0');
    }
    return v > 65535 ? 0 : uint16_t(v);
}

// ---------------------------------------------------------------- tree

struct Encoder {
    std::vector<uint8_t> moves;
    std::vector<std::string> comments;
    bool variations = false, hasComments = false, nags = false;
    unsigned medals = 0; // medals of all the moves of the game
    bool ok = true;
    std::string error;

    void annotations(const Node* n)
    {
        for (int nag : n->nags) {
            if (nag < 0 || nag > 255) continue;
            moves.push_back(NagCode);
            moves.push_back(uint8_t(nag));
            nags = true;
        }
        if (!n->comment.empty()) {
            moves.push_back(CommentCode);
            comments.push_back(n->comment);
            // "Commented game": a text to read, not just ChessBase commands
            // ([%evp …], [%emt …], [%eval …]) that the display hides.
            if (!hasComments) hasComments = n->comment.find("[%") == std::string::npos || !chess::pgn::withoutCommands(n->comment).empty();
        }
        if (!n->preComment.empty()) {
            moves.push_back(Extension);
            moves.push_back(ExtPreComment);
            comments.push_back(n->preComment);
            hasComments = true;
        }
        if (n->clockMs >= 0) {
            moves.push_back(Extension);
            moves.push_back(ExtClock);
            putVarint(moves, uint64_t(n->clockMs));
        }
        if (n->medals) {
            moves.push_back(Extension);
            moves.push_back(ExtMedals);
            putVarint(moves, n->medals);
            medals |= n->medals;
        }
        if (n->question) {
            moves.push_back(Extension);
            moves.push_back(ExtQuestion);
            putVarint(moves, n->questionText.size());
            moves.insert(moves.end(), n->questionText.begin(), n->questionText.end());
            if (!n->questionData.empty()) {
                moves.push_back(Extension);
                moves.push_back(ExtQuestionData);
                comments.push_back(n->questionData);
            }
        }
        if (!n->graphics.empty()) {
            moves.push_back(Extension);
            moves.push_back(ExtGraphics);
            putVarint(moves, n->graphics.size());
            for (const chess::Graphic& g : n->graphics) {
                moves.push_back(uint8_t(g.from));
                moves.push_back(g.isArrow() ? uint8_t(g.to) : uint8_t(255));
                moves.push_back(uint8_t(g.color));
            }
        }
    }
    void node(const Node* parent, const Node* n)
    {
        if (!ok) return;
        if (!encodeMove(parent->pos, n->move, moves)) {
            ok = false;
            error = "coup impossible à coder : " + n->san;
            return;
        }
        annotations(n);
    }
    void line(const Node* cur)
    {
        while (ok && !cur->children.empty()) {
            const Node* main = cur->children.front().get();
            node(cur, main);
            for (size_t i = 1; i < cur->children.size() && ok; ++i) {
                variations = true;
                moves.push_back(StartVar);
                node(cur, cur->children[i].get());
                line(cur->children[i].get());
                moves.push_back(EndVar);
            }
            cur = main;
        }
    }
};

} // namespace

// ---------------------------------------------------------------- signatures

uint32_t materialSignature(const Position& pos)
{
    int count[2][7] = {};
    for (Square s = 0; s < 64; ++s)
        if (const Piece p = pos.at(s); p != NoPiece) ++count[colorOf(p)][typeOf(p)];
    uint32_t sig = 0;
    for (int c = 0; c < 2; ++c) {
        auto sat = [](int v, int max) { return uint32_t(std::min(v, max)); };
        const uint32_t side = sat(count[c][Queen], 3) | sat(count[c][Rook], 3) << 2 | sat(count[c][Bishop], 3) << 4
                            | sat(count[c][Knight], 3) << 6 | sat(count[c][Pawn], 15) << 8;
        sig |= side << (12 * c);
    }
    return sig;
}

uint16_t PawnSignature::missing(const Position& pos)
{
    uint16_t m = 0;
    for (int f = 0; f < 8; ++f) {
        if (pos.at(makeSquare(f, 1)) != makePiece(White, Pawn)) m |= uint16_t(1 << f);
        if (pos.at(makeSquare(f, 6)) != makePiece(Black, Pawn)) m |= uint16_t(1 << (8 + f));
    }
    return m;
}

void PawnSignature::start(const Position& pos)
{
    m_missing = 0;
    m_order.clear();
    update(pos);
}

void PawnSignature::update(const Position& pos)
{
    const uint16_t now = missing(pos);
    const uint16_t gone = uint16_t(now & ~m_missing);
    if (!gone) return;
    for (int i = 0; i < 16; ++i)
        if (gone & (1 << i)) m_order.push_back(uint8_t(i));
    m_missing = now;
}

uint64_t PawnSignature::value() const
{
    uint64_t v = std::min<size_t>(m_order.size(), 15);
    for (size_t i = 0; i < m_order.size() && i < 12; ++i) v |= uint64_t(m_order[i]) << (4 + 5 * i);
    return v;
}

bool PawnSignature::compatible(uint64_t sig, uint16_t target)
{
    const int count = int(sig & 15);
    int need = 0;
    for (uint16_t t = target; t; t &= uint16_t(t - 1)) ++need;
    if (need > count && count < 15) return false; // the game never lost that many starting pawns
    uint16_t prefix = 0;
    for (int i = 0; i < std::min({need, count, 12}); ++i) prefix |= uint16_t(1 << ((sig >> (4 + 5 * i)) & 31));
    // The first `need` departures must be exactly the missing pawns (checkable on 12).
    if (need <= 12 && need <= count) return prefix == target;
    return (prefix & ~target) == 0;
}

// ---------------------------------------------------------------- game → block

namespace {
// Like compress2(…, 6), with a compressor kept per thread: its initialisation (allocating and
// zeroing its tables) cost more than compressing a text of a few hundred
// bytes. The result is byte-for-byte identical.
bool deflateText(uint8_t* out, uLongf* outLen, const uint8_t* in, size_t inLen)
{
    struct Stream {
        z_stream z{};
        bool ready = false;
        ~Stream()
        {
            if (ready) deflateEnd(&z);
        }
    };
    thread_local Stream s;
    if (!s.ready) {
        if (deflateInit(&s.z, 6) != Z_OK) return false;
        s.ready = true;
    } else if (deflateReset(&s.z) != Z_OK) {
        return false;
    }
    s.z.next_in = const_cast<Bytef*>(in);
    s.z.avail_in = uInt(inLen);
    s.z.next_out = out;
    s.z.avail_out = uInt(*outLen);
    if (deflate(&s.z, Z_FINISH) != Z_STREAM_END) return false;
    *outLen = s.z.total_out;
    return true;
}
} // namespace

bool encodeGame(const Game& game, std::vector<uint8_t>& blob, GameInfo& info, std::string* error)
{
    blob.clear();
    info = GameInfo();
    Record& r = info.record;

    Encoder enc;
    enc.annotations(game.root());
    enc.line(game.root());
    if (!enc.ok) {
        if (error) *error = enc.error;
        return false;
    }
    enc.moves.push_back(Extension);
    enc.moves.push_back(ExtEnd);

    // Headers: seven-tag roster and fields indexed in the index; the rest (and any value the
    // index would not reproduce identically) as text, in the original order.
    std::vector<uint8_t> text;
    std::vector<uint8_t> tags;
    uint64_t tagCount = 0;
    auto literal = [&](const std::string& k, const std::string& v) {
        putString(tags, k);
        tags.push_back(0);
        putString(tags, v);
        ++tagCount;
    };
    for (const auto& [k, v] : game.headers()) {
        if (k == "Event") info.event = v;
        else if (k == "Site") info.site = v;
        else if (k == "Round") info.round = v;
        else if (k == "White") info.white = v;
        else if (k == "Black") info.black = v;
        else if (k == "Date") {
            r.date = parseDate(v);
            if (formatDate(r.date) != v) literal(k, v);
        } else if (k == "Result") {
            r.result = parseResult(v);
            if (formatResult(r.result) != v) literal(k, v);
        } else if (k == "WhiteElo" && parseElo(v) && std::to_string(parseElo(v)) == v) {
            r.whiteElo = parseElo(v);
            putString(tags, k), tags.push_back(1), ++tagCount;
        } else if (k == "BlackElo" && parseElo(v) && std::to_string(parseElo(v)) == v) {
            r.blackElo = parseElo(v);
            putString(tags, k), tags.push_back(1), ++tagCount;
        } else if (k == "ECO" && parseEco(v)) {
            r.eco = parseEco(v);
            putString(tags, k), tags.push_back(1), ++tagCount;
        } else if (k == "Annotator" && !v.empty()) {
            info.annotator = v;
            putString(tags, k), tags.push_back(1), ++tagCount;
        } else if (k == "Source" && !v.empty()) {
            info.source = v;
            putString(tags, k), tags.push_back(1), ++tagCount;
        } else {
            literal(k, v);
        }
    }
    putVarint(text, tagCount);
    text.insert(text.end(), tags.begin(), tags.end());
    for (const std::string& c : enc.comments) putString(text, c);

    // Index fields computed on the main line
    const auto main = game.mainline();
    r.plies = uint16_t(std::min<size_t>(main.size(), 65535));
    const Position& start = game.startPosition();
    const bool customStart = start.fen() != Position::startPos().fen();
    PawnSignature pawns;
    pawns.start(start);
    bool promotion = false;
    BeautyRater beauty;
    beauty.start();
    const Position* before = &start;
    for (const Node* n : main) {
        pawns.update(n->pos);
        if (n->move.promo != NoType) promotion = true;
        if (!n->move.isNull()) beauty.move(*before, n->move);
        before = &n->pos;
    }
    r.pawns = pawns.value();
    r.material = materialSignature(main.empty() ? start : main.back()->pos);
    r.flags = uint16_t((customStart ? FlagStartFen : 0) | (enc.variations ? FlagVariations : 0) | (enc.hasComments ? FlagComments : 0)
                       | (enc.nags ? FlagNags : 0) | (promotion ? FlagPromotion : 0));
    r.flags = uint16_t(r.flags | beautyFlags(beauty.medals(Result(r.result))));
    // ChessBase medals → personal flags (Opening, Endgame, Tactics, Strategy, To review)
    if (enc.medals) {
        const unsigned m = enc.medals;
        unsigned user = 0;
        if (m & 0x000C) user |= 1 << 0;                   // model game, novelty → Opening
        if (m & 0x1000) user |= 1 << 2;                   // endgame → Endgame
        if (m & 0x23C0) user |= 1 << 3;                   // tactics, attack, sacrifice, defence, tactical blunder → Tactics
        if (m & 0x4C30) user |= 1 << 4;                   // structure, strategy, material, piece play, strategic mistake → Strategy
        if (m & 0x8003) user |= 1 << 5;                   // best game, decisive game, personal → To review
        r.flags = uint16_t(r.flags | (user << UserFlagShift));
    }

    // Block: flags, FEN, moves, text (compressed if worth it)
    uint8_t flags = customStart ? 1 : 0;
    std::vector<uint8_t> stored = text;
    if (text.size() > 96) {
        uLongf len = compressBound(uLong(text.size()));
        std::vector<uint8_t> z(len);
        if (deflateText(z.data(), &len, text.data(), text.size()) && len < text.size()) {
            z.resize(len);
            stored = std::move(z);
            flags |= 2;
        }
    }
    blob.push_back(flags);
    if (customStart) putString(blob, start.fen());
    putVarint(blob, enc.moves.size());
    blob.insert(blob.end(), enc.moves.begin(), enc.moves.end());
    putVarint(blob, text.size());
    putVarint(blob, stored.size());
    blob.insert(blob.end(), stored.begin(), stored.end());
    if (blob.size() >= (1u << 24)) {
        if (error) *error = "partie trop volumineuse (16 Mo au plus)";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- block → game

namespace {

struct Blob {
    Position start = Position::startPos();
    const uint8_t* moves = nullptr;
    const uint8_t* movesEnd = nullptr;
    std::vector<uint8_t> text;
};

bool splitBlob(const uint8_t* p, size_t size, Blob& b, bool wantText)
{
    const uint8_t* end = p + size;
    if (p >= end) return false;
    const uint8_t flags = *p++;
    if (flags & 1) {
        std::string fen;
        if (!getString(p, end, fen) || !b.start.setFen(fen)) return false;
    }
    uint64_t n = 0;
    if (!getVarint(p, end, n) || uint64_t(end - p) < n) return false;
    b.moves = p;
    b.movesEnd = p + n;
    p += n;
    if (!wantText) return true;
    uint64_t raw = 0, stored = 0;
    if (!getVarint(p, end, raw) || !getVarint(p, end, stored) || uint64_t(end - p) < stored) return false;
    if (flags & 2) {
        b.text.resize(size_t(raw));
        uLongf len = uLongf(raw);
        if (uncompress(b.text.data(), &len, p, uLong(stored)) != Z_OK || len != raw) return false;
    } else {
        b.text.assign(p, p + stored);
    }
    return true;
}

} // namespace

bool decodeGame(const uint8_t* data, size_t size, const Record& record, const Names& names, Game& out, std::string* error)
{
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    Blob b;
    if (!splitBlob(data, size, b, true)) return fail("partie abîmée");
    out.reset(b.start);

    // Headers: roster from the index, then the others in the original order
    auto nameOr = [&](NameType t, uint32_t id) { return id == NoName ? std::string() : names.name(t, id); };
    out.setHeader("Event", nameOr(Event, record.event));
    out.setHeader("Site", nameOr(Site, record.site));
    out.setHeader("Date", formatDate(record.date));
    out.setHeader("Round", nameOr(Round, record.round));
    out.setHeader("White", nameOr(Player, record.white));
    out.setHeader("Black", nameOr(Player, record.black));
    out.setHeader("Result", formatResult(record.result));
    const uint8_t* t = b.text.data();
    const uint8_t* tend = t + b.text.size();
    uint64_t tagCount = 0;
    if (!getVarint(t, tend, tagCount)) return fail("en-têtes abîmés");
    for (uint64_t i = 0; i < tagCount; ++i) {
        std::string k, v;
        if (!getString(t, tend, k) || t >= tend) return fail("en-têtes abîmés");
        const uint8_t kind = *t++;
        if (kind == 0) {
            if (!getString(t, tend, v)) return fail("en-têtes abîmés");
        } else if (k == "WhiteElo") v = std::to_string(record.whiteElo);
        else if (k == "BlackElo") v = std::to_string(record.blackElo);
        else if (k == "ECO") v = formatEco(record.eco);
        else if (k == "Annotator") v = nameOr(Annotator, record.annotator);
        else if (k == "Source") v = nameOr(Source, record.source);
        out.setHeader(k, v);
    }

    // Moves, variations, annotations
    Node* cur = out.root();
    std::vector<Node*> stack;
    const uint8_t* p = b.moves;
    while (p < b.movesEnd) {
        const uint8_t byte = *p++;
        if ((byte >> 4) == 0 && (byte & 15) >= NullMove) { // markers (king, codes 10–15)
            switch (byte & 15) {
            case NullMove: cur = out.addMove(cur, Move(), true); break;
            case NagCode:
                if (p >= b.movesEnd) return fail("NAG abîmé");
                cur->nags.push_back(*p++);
                break;
            case CommentCode: {
                std::string c;
                if (!getString(t, tend, c)) return fail("commentaire abîmé");
                cur->comment = std::move(c);
                break;
            }
            case StartVar:
                stack.push_back(cur);
                if (!cur->parent) return fail("variante sans coup");
                cur = cur->parent;
                break;
            case EndVar:
                if (stack.empty()) return fail("fin de variante en trop");
                cur = stack.back();
                stack.pop_back();
                break;
            case Extension: {
                if (p >= b.movesEnd) return fail("extension abîmée");
                const uint8_t ext = *p++;
                if (ext == ExtEnd) {
                    out.setCurrent(out.root());
                    return true;
                }
                if (ext == ExtPreComment) {
                    if (!getString(t, tend, cur->preComment)) return fail("commentaire d'avant-coup abîmé");
                } else if (ext == ExtQuestionData) {
                    if (!getString(t, tend, cur->questionData)) return fail("question d'entraînement abîmée");
                } else if (ext == ExtClock) {
                    uint64_t ms = 0;
                    if (!getVarint(p, b.movesEnd, ms)) return fail("pendule abîmée");
                    cur->clockMs = int(ms);
                } else if (ext == ExtMedals) {
                    uint64_t bits = 0;
                    if (!getVarint(p, b.movesEnd, bits)) return fail("médailles abîmées");
                    cur->medals = uint16_t(bits);
                } else if (ext == ExtQuestion) {
                    uint64_t size = 0;
                    if (!getVarint(p, b.movesEnd, size) || size > uint64_t(b.movesEnd - p)) return fail("question d'entraînement abîmée");
                    cur->question = true;
                    cur->questionText.assign(reinterpret_cast<const char*>(p), size_t(size));
                    p += size;
                } else if (ext == ExtGraphics) {
                    uint64_t count = 0;
                    if (!getVarint(p, b.movesEnd, count) || count * 3 > uint64_t(b.movesEnd - p)) return fail("annotations graphiques abîmées");
                    for (uint64_t k = 0; k < count; ++k, p += 3) {
                        chess::Graphic g;
                        g.from = Square(p[0] & 63);
                        g.to = p[1] == 255 ? NoSquare : Square(p[1] & 63);
                        g.color = char(p[2]);
                        cur->graphics.push_back(g);
                    }
                } else {
                    return fail("extension inconnue");
                }
                break;
            }
            }
            continue;
        }
        Move m;
        if (!decodeMove(cur->pos, byte, p, b.movesEnd, m)) return fail("coup abîmé");
        cur = out.addMove(cur, m, true);
    }
    out.setCurrent(out.root());
    return true;
}

namespace {
using MoveVisit = std::function<bool(const Position&, Move, int)>;
// `visit` after each move (and for the starting position), `moveVisit` before each move.
bool replayImpl(const uint8_t* data, size_t size, const std::function<bool(const Position&, int)>* visit, const MoveVisit* moveVisit)
{
    Blob b;
    if (!splitBlob(data, size, b, false)) return false;
    Position pos = b.start;
    int ply = 0;
    if (visit && !(*visit)(pos, 0)) return true;
    int depth = 0; // variation depth: everything that is not the main line is skipped
    const uint8_t* p = b.moves;
    while (p < b.movesEnd) {
        const uint8_t byte = *p++;
        if ((byte >> 4) == 0 && (byte & 15) >= NullMove) {
            switch (byte & 15) {
            case NullMove:
                if (depth == 0) {
                    pos.makeMove(Move());
                    ++ply;
                    if (visit && !(*visit)(pos, ply)) return true;
                }
                break;
            case NagCode: ++p; break;
            case CommentCode: break;
            case StartVar: ++depth; break;
            case EndVar: --depth; break;
            case Extension: {
                if (p >= b.movesEnd) return false;
                const uint8_t ext = *p++;
                if (ext == ExtEnd) return true;
                uint64_t v;
                if ((ext == ExtClock || ext == ExtMedals) && !getVarint(p, b.movesEnd, v)) return false;
                if (ext == ExtGraphics) {
                    if (!getVarint(p, b.movesEnd, v) || v * 3 > uint64_t(b.movesEnd - p)) return false;
                    p += v * 3;
                }
                if (ext == ExtQuestion) {
                    if (!getVarint(p, b.movesEnd, v) || v > uint64_t(b.movesEnd - p)) return false;
                    p += v;
                }
                break;
            }
            }
            continue;
        }
        // Variation move: skipped without being decoded (the second byte of a diagonal queen
        // move, 64 + square, cannot be mistaken for a marker).
        if (depth > 0) continue;
        Move m;
        if (!decodeMove(pos, byte, p, b.movesEnd, m)) return false;
        ++ply;
        if (moveVisit && !(*moveVisit)(pos, m, ply)) return true;
        pos.makeMove(m);
        if (visit && !(*visit)(pos, ply)) return true;
    }
    return true;
}
} // namespace

bool replayMainline(const uint8_t* data, size_t size, const std::function<bool(const Position&, int)>& visit)
{
    return replayImpl(data, size, &visit, nullptr);
}

bool replayMainlineMoves(const uint8_t* data, size_t size, const std::function<bool(const Position&, Move, int)>& visit)
{
    return replayImpl(data, size, nullptr, &visit);
}

} // namespace hy64db
