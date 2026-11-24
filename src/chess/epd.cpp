#include "epd.h"

#include "game.h"
#include "pgn.h"
#include "position.h"

#include <cctype>
#include <algorithm>
#include <charconv>

namespace chess::epd {

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// Next field (whitespace-separated); `s` advances past it.
std::string_view nextField(std::string_view& s)
{
    s = trim(s);
    size_t end = 0;
    while (end < s.size() && !std::isspace(static_cast<unsigned char>(s[end]))) ++end;
    const std::string_view f = s.substr(0, end);
    s.remove_prefix(end);
    return f;
}

bool isNumber(std::string_view s, int& value)
{
    if (s.empty()) return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), value);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

// Operations: "code operands;"; a ";" inside quotes is part of the string.
void parseOps(std::string_view s, std::vector<std::pair<std::string, std::string>>& out)
{
    while (true) {
        s = trim(s);
        if (s.empty()) return;
        const std::string_view code = nextField(s);
        std::string operands;
        bool quoted = false;
        size_t i = 0;
        for (; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '"') {
                quoted = !quoted;
                continue;
            }
            if (c == ';' && !quoted) break;
            operands += c;
        }
        s.remove_prefix(std::min(i + 1, s.size()));
        const std::string_view t = trim(operands);
        std::string code_(code);
        if (!code_.empty() && code_.back() == ';') code_.pop_back(); // "code;" without operand
        if (!code_.empty()) out.emplace_back(std::move(code_), std::string(t));
    }
}

std::string escapeHeader(std::string_view s)
{
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string escapeComment(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        if (c == '}') c = ')';
        else if (c == '{') c = '(';
    return out;
}

std::vector<std::string> words(std::string_view s)
{
    std::vector<std::string> out;
    while (true) {
        const std::string_view w = nextField(s);
        if (w.empty()) return out;
        out.emplace_back(w);
    }
}

// Move in SAN (or UCI) in `pos`, null if it is not legal.
Move parseMove(const Position& pos, std::string_view text)
{
    Move m = pos.parseSan(text);
    if (m.isNull()) m = pos.parseUci(text);
    return !m.isNull() && pos.isLegal(m) ? m : Move();
}

std::string moveNumber(const Position& pos, bool forceBlack)
{
    if (pos.sideToMove() == White) return std::to_string(pos.fullmoveNumber()) + ". ";
    return forceBlack ? std::to_string(pos.fullmoveNumber()) + "... " : std::string();
}


// Operands made of moves (never quoted)
bool isMoveList(std::string_view code) { return code == "bm" || code == "am" || code == "pv" || code == "sm" || code == "pm"; }

// "code operands"; operands quoted if they contain whitespace or a ";"
// (strings: id, c0…c9…), an inner quote becomes an apostrophe.
std::string writeOp(const std::string& code, std::string value)
{
    if (value.empty()) return code;
    std::replace(value.begin(), value.end(), '"', '\'');
    const bool text = code == "id" || code == "eco" || code == "nic" || (code.size() == 2 && (code[0] == 'c' || code[0] == 'v') && std::isdigit(static_cast<unsigned char>(code[1])));
    const bool quote = text || (!isMoveList(code) && value.find_first_of(" \t;") != std::string::npos);
    return code + ' ' + (quote ? '"' + value + '"' : value);
}

// NAG "?", "??", "?!": move to avoid (solution test convention)
bool isBadNag(int nag) { return nag == 2 || nag == 4 || nag == 6; }
} // namespace

const std::string* Record::op(std::string_view code) const
{
    for (const auto& [c, v] : ops)
        if (c == code) return &v;
    return nullptr;
}

Operations parseOperations(std::string_view text)
{
    Operations out;
    parseOps(text, out);
    return out;
}

bool isStandardOpcode(std::string_view code)
{
    static const char* const known[] = {"acd", "acn", "acs", "am", "bm", "ce", "dm", "draw_accept", "draw_claim", "draw_offer",
                                         "draw_reject", "eco", "fmvn", "hmvc", "id", "nic", "noop", "pm", "pv", "rc", "resign", "sm",
                                         "tcgs", "tcri", "tcsi"};
    for (const char* k : known)
        if (code == k) return true;
    // c0…c9 (comments), v0…v9 (variations)
    return code.size() == 2 && (code[0] == 'c' || code[0] == 'v') && code[1] >= '0' && code[1] <= '9';
}

bool parseLine(std::string_view line, Record& out)
{
    line = trim(line);
    if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '%') return false;
    std::string_view rest = line;
    std::string fields[4];
    for (std::string& f : fields) {
        f = std::string(nextField(rest));
        if (f.empty()) return false;
    }
    // Optional counters (full FEN line)
    int half = 0, full = 1;
    {
        std::string_view probe = rest;
        int a = 0, b = 0;
        const std::string_view f1 = nextField(probe);
        const std::string_view f2 = nextField(probe);
        if (isNumber(f1, a) && isNumber(f2, b)) {
            half = a;
            full = b;
            rest = probe;
        }
    }
    out.ops.clear();
    parseOps(rest, out.ops);
    int v = 0;
    if (const std::string* h = out.op("hmvc"); h && isNumber(*h, v)) half = v;
    if (const std::string* f = out.op("fmvn"); f && isNumber(*f, v)) full = v;
    out.fen = fields[0] + ' ' + fields[1] + ' ' + fields[2] + ' ' + fields[3] + ' ' + std::to_string(half) + ' ' + std::to_string(std::max(1, full));
    Position pos;
    if (!pos.setFen(out.fen)) return false;
    out.fen = pos.fen();
    return true;
}

std::string toPgn(std::string_view text, std::string_view source, int* positions, int* skipped)
{
    std::string pgn;
    int n = 0, bad = 0;
    while (!text.empty()) {
        const size_t eol = text.find('\n');
        const std::string_view line = text.substr(0, eol);
        text.remove_prefix(eol == std::string_view::npos ? text.size() : eol + 1);
        Record r;
        if (!parseLine(line, r)) {
            const std::string_view t = trim(line);
            if (!t.empty() && t.front() != '#' && t.front() != ';' && t.front() != '%') ++bad;
            continue;
        }
        ++n;
        Position pos;
        pos.setFen(r.fen);

        // Solutions and moves to avoid
        std::vector<Move> best, avoid;
        for (const auto& [code, value] : r.ops) {
            if (code != "bm" && code != "am") continue;
            for (const std::string& w : words(value))
                if (const Move m = parseMove(pos, w); !m.isNull()) (code == "bm" ? best : avoid).push_back(m);
        }
        std::vector<Move> line_;
        if (const std::string* pv = r.op("pv")) {
            Position p = pos;
            for (const std::string& w : words(*pv)) {
                const Move m = parseMove(p, w);
                if (m.isNull()) break;
                line_.push_back(m);
                p.makeMove(m);
            }
        }
        if (line_.empty() && !best.empty()) line_.push_back(best.front());

        const std::string* id = r.op("id");
        pgn += "[Event \"" + escapeHeader(id && !id->empty() ? *id : "Position " + std::to_string(n)) + "\"]\n";
        pgn += "[Site \"" + escapeHeader(source) + "\"]\n";
        pgn += "[Date \"????.??.??\"]\n";
        pgn += "[Round \"" + std::to_string(n) + "\"]\n";
        pgn += "[White \"?\"]\n[Black \"?\"]\n[Result \"*\"]\n";
        pgn += "[SetUp \"1\"]\n[FEN \"" + r.fen + "\"]\n";

        // Comment: operations other than those translated into moves or headers
        // (bm / am: only if they are not already among the moves).
        std::string comment;
        for (const auto& [code, value] : r.ops) {
            if (code == "id" || code == "pv" || code == "hmvc" || code == "fmvn") continue;
            if ((code == "bm" || code == "am") && !line_.empty()) continue;
            if (!comment.empty()) comment += "; ";
            comment += writeOp(code, value);
        }
        std::string moves;
        if (!comment.empty()) moves += '{' + escapeComment(comment) + "} ";
        Position p = pos;
        for (size_t i = 0; i < line_.size(); ++i) {
            moves += moveNumber(p, i == 0) + p.toSan(line_[i]) + ' ';
            if (i == 0) {
                for (const Move& m : best)
                    if (!(m == line_[0])) moves += "( " + moveNumber(pos, true) + pos.toSan(m) + " ) ";
                for (const Move& m : avoid) moves += "( " + moveNumber(pos, true) + pos.toSan(m) + "? ) ";
            }
            p.makeMove(line_[i]);
        }
        pgn += '\n' + moves + "*\n\n";
    }
    if (positions) *positions = n;
    if (skipped) *skipped = bad;
    return pgn;
}

std::string fromGame(const Game& game)
{
    const Position& pos = game.startPosition();
    std::string fen = pos.fen();
    {
        // first 4 fields
        size_t cut = 0;
        for (int i = 0; i < 4 && cut != std::string::npos; ++i) cut = fen.find(' ', cut + (i ? 1 : 0));
        if (cut != std::string::npos) fen.resize(cut);
    }
    const Node* root = game.root();
    std::vector<std::string> best, avoid;
    for (const auto& child : root->children) {
        const std::string san = pos.toSan(child->move);
        const bool bad = std::any_of(child->nags.begin(), child->nags.end(), isBadNag);
        (bad ? avoid : best).push_back(san);
    }
    std::string pv;
    const std::vector<Node*> line = game.mainline();
    if (line.size() > 1 && !std::any_of(line.front()->nags.begin(), line.front()->nags.end(), isBadNag)) {
        Position p = pos;
        for (const Node* n : line) {
            if (!pv.empty()) pv += ' ';
            pv += p.toSan(n->move);
            p.makeMove(n->move);
        }
    }

    // Starting comment: EPD operations (produced by toPgn), otherwise free text (c0)
    Operations fromComment;
    const std::string comment(trim(root->comment));
    if (!comment.empty()) {
        Operations ops = parseOperations(comment);
        const bool allOps = !ops.empty() && std::all_of(ops.begin(), ops.end(), [](const auto& o) { return isStandardOpcode(o.first); });
        if (allOps) fromComment = std::move(ops);
        else fromComment.emplace_back("c0", comment);
    }
    const bool noMoves = root->children.empty();

    std::vector<std::string> ops;
    auto join = [](const std::vector<std::string>& v) {
        std::string out;
        for (const std::string& m : v) out += (out.empty() ? "" : " ") + m;
        return out;
    };
    if (!best.empty()) ops.push_back("bm " + join(best));
    if (!avoid.empty()) ops.push_back("am " + join(avoid));
    for (const auto& [code, value] : fromComment) {
        if (code == "id" || code == "pv" || code == "hmvc" || code == "fmvn") continue; // come from the game
        if ((code == "bm" || code == "am") && !noMoves) continue;
        ops.push_back(writeOp(code, value));
    }
    if (!pv.empty()) ops.push_back("pv " + pv);
    std::string id = game.header("Event");
    {
        bool generated = id.rfind("Position ", 0) == 0 && id.size() > 9;
        for (size_t i = 9; generated && i < id.size(); ++i) generated = std::isdigit(static_cast<unsigned char>(id[i]));
        if (generated || id == "?") id.clear();
    }
    if (!id.empty()) ops.push_back(writeOp("id", id));
    if (pos.halfmoveClock() != 0) ops.push_back("hmvc " + std::to_string(pos.halfmoveClock()));
    if (pos.fullmoveNumber() != 1) ops.push_back("fmvn " + std::to_string(pos.fullmoveNumber()));

    std::string out = fen;
    for (const std::string& o : ops) out += ' ' + o + ';';
    return out;
}

std::string fromPgn(std::string_view pgn, int* games)
{
    std::string out;
    int n = 0;
    for (const pgn::GameEntry& e : pgn::indexDatabase(pgn)) {
        Game g;
        if (!pgn::parseGame(pgn.substr(e.offset, e.length), g)) continue;
        out += fromGame(g) + '\n';
        ++n;
    }
    if (games) *games = n;
    return out;
}

} // namespace chess::epd
