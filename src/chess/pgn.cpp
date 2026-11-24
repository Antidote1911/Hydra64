#include "pgn.h"

#include <algorithm>

#include <cstring>

#include <cctype>
#include <cstdio>
#include <functional>

namespace chess::pgn {

namespace {

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)); }

std::string trim(std::string_view s)
{
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

// Reads a tag `[Name "Value"]` starting at text[i] == '['. Advances i past the ']'.
bool readTag(std::string_view text, size_t& i, std::string& name, std::string& value)
{
    size_t n = text.size();
    ++i; // '['
    while (i < n && isSpace(text[i])) ++i;
    size_t b = i;
    while (i < n && !isSpace(text[i]) && text[i] != '"' && text[i] != ']') ++i;
    name.assign(text.substr(b, i - b));
    while (i < n && text[i] != '"' && text[i] != ']') ++i;
    value.clear();
    if (i < n && text[i] == '"') {
        ++i;
        while (i < n && text[i] != '"') {
            // "\" only escapes '"' and "\" (PGN standard); ChessBase writes a lone backslash
            // as is ("Survey\Übersicht"): it is then part of the value.
            // ChessBase also escapes brackets ("\[10 sec/move\]").
            if (text[i] == '\\' && i + 1 < n && (text[i + 1] == '"' || text[i + 1] == '\\' || text[i + 1] == '[' || text[i + 1] == ']')) ++i;
            value += text[i++];
        }
        if (i < n) ++i;
    }
    while (i < n && text[i] != ']' && text[i] != '\n') ++i;
    if (i < n && text[i] == ']') ++i;
    return !name.empty();
}

int nagFromSuffix(std::string_view s)
{
    if (s == "!") return 1;
    if (s == "?") return 2;
    if (s == "!!") return 3;
    if (s == "??") return 4;
    if (s == "!?") return 5;
    if (s == "?!") return 6;
    return 0;
}

// French notation (R D T F C) → English (K Q R B N).
std::string frenchToEnglish(std::string_view san)
{
    std::string out(san);
    for (char& c : out) {
        switch (c) {
        case 'R': c = 'K'; break;
        case 'D': c = 'Q'; break;
        case 'T': c = 'R'; break;
        case 'F': c = 'B'; break;
        case 'C': c = 'N'; break;
        default: break;
        }
    }
    return out;
}

// Extracts "[%clk 0:04:58.3]" from a comment (Lichess/ChessBase format); removes the command.
// A comment carrying several of them (merged games in some databases): the first
// readable value is kept, the others are removed — otherwise they would stay in the text and
// the game would not come out identical from a second pass through PGN.
int extractClock(std::string& comment)
{
    int ms = -1;
    for (size_t b; (b = comment.find("[%clk")) != std::string::npos;) {
        const size_t e = comment.find(']', b);
        if (e == std::string::npos) break;
        const std::string value = comment.substr(b + 5, e - b - 5);
        int h = 0, m = 0;
        double s = 0;
        if (ms < 0 && std::sscanf(value.c_str(), " %d:%d:%lf", &h, &m, &s) == 3) ms = int((h * 3600 + m * 60 + s) * 1000 + 0.5);
        comment.erase(b, e - b + 1);
    }
    // doubled spaces left by the extraction
    while (!comment.empty() && comment.front() == ' ') comment.erase(0, 1);
    while (!comment.empty() && comment.back() == ' ') comment.pop_back();
    for (size_t p; (p = comment.find("  ")) != std::string::npos;) comment.erase(p, 1);
    return ms;
}

// Extracts "[%csl Gd4,Rf6]" and "[%cal Ge2e4]" from a comment; removes the commands.
void extractGraphics(std::string& comment, std::vector<Graphic>& out)
{
    for (const char* tag : {"[%csl", "[%cal"}) {
        const bool arrows = tag[3] == 'a';
        for (size_t b; (b = comment.find(tag)) != std::string::npos;) {
            const size_t e = comment.find(']', b);
            if (e == std::string::npos) break;
            const std::string list = comment.substr(b + 5, e - b - 5);
            for (size_t i = 0; i < list.size();) {
                while (i < list.size() && (list[i] == ' ' || list[i] == ',')) ++i;
                size_t j = i;
                while (j < list.size() && list[j] != ',' && list[j] != ' ') ++j;
                const std::string tok = list.substr(i, j - i);
                i = j;
                const size_t want = arrows ? 5 : 3;
                if (tok.size() != want) continue;
                Graphic g;
                g.color = char(std::toupper(static_cast<unsigned char>(tok[0])));
                if (g.color != 'G' && g.color != 'R' && g.color != 'Y' && g.color != 'B') g.color = 'G';
                g.from = parseSquare(tok.substr(1, 2));
                if (arrows) g.to = parseSquare(tok.substr(3, 2));
                if (g.from == NoSquare || (arrows && (g.to == NoSquare || g.to == g.from))) continue;
                if (std::find(out.begin(), out.end(), g) == out.end()) out.push_back(g);
            }
            comment.erase(b, e - b + 1);
        }
    }
    // "[%draw arrow,e2,e4,green]" and "[%draw full,d5,red]" (Scid and its derivatives): same arrows and squares.
    // An unknown form (text, other shape) stays in the comment, hidden on display.
    for (size_t from = 0, b; (b = comment.find("[%draw", from)) != std::string::npos;) {
        const size_t e = comment.find(']', b);
        if (e == std::string::npos) break;
        std::vector<std::string> parts;
        for (size_t i = b + 6; i < e;) {
            while (i < e && (comment[i] == ' ' || comment[i] == ',')) ++i;
            size_t j = i;
            while (j < e && comment[j] != ',' && comment[j] != ' ') ++j;
            if (j > i) parts.push_back(comment.substr(i, j - i));
            i = j;
        }
        const bool arrow = !parts.empty() && parts[0] == "arrow";
        const bool mark = !parts.empty() && (parts[0] == "full" || parts[0] == "square" || parts[0] == "circle");
        Graphic g;
        if (arrow && parts.size() >= 3) g.from = parseSquare(parts[1]), g.to = parseSquare(parts[2]);
        else if (mark && parts.size() >= 2) g.from = parseSquare(parts[1]);
        if (g.from == NoSquare || (arrow && (g.to == NoSquare || g.to == g.from))) {
            from = e + 1;
            continue;
        }
        const std::string color = parts.size() > (arrow ? 3u : 2u) ? parts[arrow ? 3 : 2] : "green";
        g.color = color == "red" ? 'R' : color == "yellow" ? 'Y' : color == "blue" ? 'B' : 'G';
        if (std::find(out.begin(), out.end(), g) == out.end()) out.push_back(g);
        comment.erase(b, e - b + 1);
        from = b;
    }
    while (!comment.empty() && comment.front() == ' ') comment.erase(0, 1);
    while (!comment.empty() && comment.back() == ' ') comment.pop_back();
    for (size_t p; (p = comment.find("  ")) != std::string::npos;) comment.erase(p, 1);
}

// "[%csl …][%cal …]" of a node's graphic annotations (empty if it has none).
std::string formatGraphics(const std::vector<Graphic>& graphics)
{
    std::string marks, arrows;
    for (const Graphic& g : graphics) {
        std::string& list = g.isArrow() ? arrows : marks;
        if (!list.empty()) list += ',';
        list += g.color;
        list += squareName(g.from);
        if (g.isArrow()) list += squareName(g.to);
    }
    return (marks.empty() ? "" : "[%csl " + marks + "]") + (arrows.empty() ? "" : "[%cal " + arrows + "]");
}

std::string formatClock(int ms)
{
    const int tenths = (ms + 50) / 100;
    const int h = tenths / 36000, m = tenths / 600 % 60, s = tenths / 10 % 60, t = tenths % 10;
    char buf[32];
    if (t) std::snprintf(buf, sizeof buf, "%d:%02d:%02d.%d", h, m, s, t);
    else std::snprintf(buf, sizeof buf, "%d:%02d:%02d", h, m, s);
    return buf;
}

bool isResult(std::string_view t) { return t == "1-0" || t == "0-1" || t == "1/2-1/2" || t == "*"; }

} // namespace

std::string nagSymbol(int nag)
{
    switch (nag) {
    case 1: return "!";
    case 2: return "?";
    case 3: return "!!";
    case 4: return "??";
    case 5: return "!?";
    case 6: return "?!";
    case 10: return "=";
    case 13: return "∞";
    case 14: return "⩲";
    case 15: return "⩱";
    case 16: return "±";
    case 17: return "∓";
    case 18: return "+−";
    case 19: return "−+";
    case DiagramNag: return "▦";
    default: return "$" + std::to_string(nag);
    }
}

namespace {
// Bounds of "[%evp …]" in a comment; false if there is none.
bool findEvalProfile(const std::string& c, size_t& begin, size_t& end)
{
    begin = c.find("[%evp");
    if (begin == std::string::npos) return false;
    end = c.find(']', begin);
    if (end == std::string::npos) return false;
    ++end;
    return true;
}
} // namespace

std::string withoutCommands(const std::string& comment)
{
    if (comment.find("[%") == std::string::npos) return comment;
    std::string out;
    for (size_t p = 0; p < comment.size();) {
        if (comment.compare(p, 2, "[%") == 0 && p + 2 < comment.size() && std::isalpha(static_cast<unsigned char>(comment[p + 2]))) {
            const size_t e = comment.find(']', p);
            if (e != std::string::npos) {
                p = e + 1;
                continue;
            }
        }
        out += comment[p++];
    }
    for (size_t p; (p = out.find("  ")) != std::string::npos;) out.erase(p, 1);
    while (!out.empty() && (out.front() == ' ' || out.front() == '\n')) out.erase(0, 1);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    return out;
}

int elapsedSeconds(const std::string& comment)
{
    const size_t at = comment.find("[%emt");
    if (at == std::string::npos) return -1;
    int a = 0, b = 0;
    double c = 0;
    const char* text = comment.c_str() + at + 5;
    if (std::sscanf(text, " %d:%d:%lf", &a, &b, &c) == 3) return a < 0 || b < 0 || c < 0 ? -1 : a * 3600 + b * 60 + int(c);
    if (std::sscanf(text, " %d:%lf", &a, &c) == 2) return a < 0 || c < 0 ? -1 : a * 60 + int(c);
    if (std::sscanf(text, " %lf", &c) == 1) return c < 0 ? -1 : int(c);
    return -1;
}

int applyEvalProfile(Game& game)
{
    // "[%eval centipawns,depth]" on a move (White's side)
    int applied = 0;
    std::function<void(Node*)> perMove = [&](Node* n) {
        const size_t at = n->comment.find("[%eval");
        if (at != std::string::npos && !n->eval.valid) {
            int cp = 0, depth = 0;
            const int got = std::sscanf(n->comment.c_str() + at + 6, " %d , %d", &cp, &depth);
            if (got >= 1) {
                n->eval = {true, false, cp, got == 2 ? std::max(0, depth) : 0};
                ++applied;
            }
        }
        for (const auto& child : n->children) perMove(child.get());
    };
    perMove(game.root());
    const std::string& c = game.root()->comment;
    size_t b, e;
    if (!findEvalProfile(c, b, e)) return applied;
    std::vector<int> numbers;
    for (size_t p = b + 5; p < e - 1;) {
        char* stop = nullptr;
        const long v = std::strtol(c.c_str() + p, &stop, 10);
        if (stop == c.c_str() + p) {
            ++p; // comma, space, line break
            continue;
        }
        numbers.push_back(int(v));
        p = size_t(stop - c.c_str());
    }
    if (numbers.size() < 3) return applied;
    const int first = numbers[0];
    std::vector<Node*> line{game.root()};
    for (Node* n : game.mainline()) line.push_back(n);
    for (size_t k = 2; k < numbers.size(); ++k) {
        const size_t ply = size_t(first) + (k - 2);
        if (first < 0 || ply >= line.size()) break;
        Node* n = line[ply];
        if (n->eval.valid) continue;
        const int v = numbers[k];
        // beyond ±30,000: an announced mate (32,767 − number of moves, judging by the values seen)
        if (v >= 30000 || v <= -30000) n->eval = {true, true, v > 0 ? std::max(1, 32767 - v) : -std::max(1, 32767 + v), 0};
        else n->eval = {true, false, v, 0};
        ++applied;
    }
    return applied;
}

std::vector<std::string> medalNames(unsigned medals)
{
    static const char* const names[16] = {"meilleure partie", "partie décisive", "partie modèle", "nouveauté", "structure de pions", "stratégie", "tactique", "attaque",
                                          "sacrifice", "défense", "matériel", "jeu des pièces", "finale", "gaffe tactique", "erreur stratégique", "personnelle"};
    std::vector<std::string> out;
    for (int i = 0; i < 16; ++i)
        if (medals & (1u << i)) out.push_back(names[i]);
    return out;
}

namespace {
// "[#]" (diagram) and "[%mdl n]" (medals) of a ChessBase comment: removed from the text.
void extractMarkers(Node* node)
{
    std::string& c = node->comment;
    for (size_t at; (at = c.find("[#]")) != std::string::npos;) {
        c.erase(at, 3);
        if (std::find(node->nags.begin(), node->nags.end(), DiagramNag) == node->nags.end()) node->nags.push_back(DiagramNag);
    }
    for (size_t b; (b = c.find("[%mdl")) != std::string::npos;) {
        const size_t e = c.find(']', b);
        if (e == std::string::npos) break;
        node->medals = uint16_t(node->medals | std::atoi(c.substr(b + 5, e - b - 5).c_str()));
        c.erase(b, e - b + 1);
    }
    while (!c.empty() && c.front() == ' ') c.erase(0, 1);
    while (!c.empty() && c.back() == ' ') c.pop_back();
    for (size_t p; (p = c.find("  ")) != std::string::npos;) c.erase(p, 1);
}

// Training questions: [%tqu "language","wording","hint 1","hint 2","move","feedback",points], this
// seven-field block repeated per language (ChessBase syntax). Removes the blocks from `c` and returns
// their arguments (without "[%tqu" or "]").
std::vector<std::string> takeQuestions(std::string& c)
{
    std::vector<std::string> found;
    for (size_t b; (b = c.find("[%tqu")) != std::string::npos;) {
        size_t e = b + 5;
        bool quoted = false;
        for (; e < c.size() && (quoted || c[e] != ']'); ++e)
            if (c[e] == '"') quoted = !quoted;
        if (e >= c.size()) break;
        size_t from = b + 5;
        while (from < e && c[from] == ' ') ++from;
        found.push_back(c.substr(from, e - from));
        c.erase(b, e - b + 1);
    }
    return found;
}

// Quoted fields of a question's arguments.
std::vector<std::string> questionFields(const std::string& args)
{
    std::vector<std::string> fields;
    bool quoted = false;
    for (char ch : args) {
        if (ch == '"') {
            quoted = !quoted;
            if (quoted) fields.emplace_back();
        } else if (quoted) {
            fields.back() += ch;
        }
    }
    return fields;
}

void applyQuestion(Node* node, const std::string& args)
{
    const std::vector<std::string> fields = questionFields(args);
    node->question = true;
    if (fields.size() >= 2) node->questionText = fields[1];
    // Only a question richer than what Hydra64 writes itself is kept whole
    const bool plain = fields.size() == 6 && fields[0].empty() && fields[2].empty() && fields[3].empty() && fields[5].empty();
    node->questionData = plain ? std::string() : args;
}

std::string formatQuestion(const Node* node)
{
    if (!node->questionData.empty()) return "[%tqu " + node->questionData + "]";
    std::string text = node->questionText;
    for (char& ch : text) // the wording must close neither the string, nor the marker, nor the comment
        ch = ch == '"' ? '\'' : ch == ']' ? ')' : ch == '}' ? ')' : ch == '\n' ? ' ' : ch;
    const Move m = node->move;
    std::string uci = squareName(m.from) + squareName(m.to);
    return "[%tqu \"\",\"" + text + "\",\"\",\"\",\"" + uci + "\",\"\",10]";
}
} // namespace

bool parseGame(std::string_view text, Game& game, std::string* error) { return parseGame(text, game, error, ParseOptions()); }

bool parseGame(std::string_view text, Game& game, std::string* error, const ParseOptions& options)
{
    game.reset();
    game.headers().clear();
    const size_t n = text.size();
    size_t i = text.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0; // UTF-8 BOM (ChessBase exports)

    // Tag section
    while (i < n) {
        while (i < n && isSpace(text[i])) ++i;
        if (i < n && text[i] == '[') {
            std::string name, value;
            if (readTag(text, i, name, value)) game.setHeader(name, value);
        } else {
            break;
        }
    }

    const std::string fen = game.header("FEN");
    if (!fen.empty()) {
        Position start;
        if (!start.setFen(fen)) {
            if (error) *error = "FEN invalide : " + fen;
            return false;
        }
        auto headers = game.headers();
        game.reset(start);
        game.headers() = std::move(headers);
    }

    Node* cur = game.root();
    if (options.hasMainline) { // moves in binary, already validated by their source
        static constexpr PieceType promos[] = {NoType, Knight, Bishop, Rook, Queen};
        for (size_t k = 0; k < options.mainlineSize; ++k) {
            const uint16_t w = options.mainline[k];
            Move m;
            if (w != 0xFFFF) {
                const Position& pos = cur->pos;
                m.from = Square(w & 63);
                m.to = Square((w >> 6) & 63);
                m.promo = (w >> 12) <= 4 ? promos[w >> 12] : NoType;
                const Piece king = pos.at(m.from);
                if (typeOf(king) == King && pos.at(m.to) == makePiece(colorOf(king), Rook)) m = pos.castlingMove(fileOf(m.to) > fileOf(m.from));
                // Irregular moves kept (see Position::parseSanLenient): a king left in check
                // already passes isPseudoLegal; what remains is the pawn capture onto an empty square.
                const Piece mover = m.isNull() ? NoPiece : pos.at(m.from);
                const bool playable = !m.isNull() && mover == makePiece(pos.sideToMove(), Pawn) && pos.at(m.to) == NoPiece && std::abs(fileOf(m.to) - fileOf(m.from)) == 1
                                      && rankOf(m.to) - rankOf(m.from) == (pos.sideToMove() == White ? 1 : -1);
                if (m.isNull() || (!pos.isPseudoLegal(m) && !playable)) {
                    if (error) *error = "Coup illisible : demi-coup " + std::to_string(k + 1);
                    game.setCurrent(game.root());
                    return false;
                }
            }
            cur = game.addMove(cur, m, true, options.withSan);
        }
        game.setCurrent(game.root());
        return true;
    }
    std::vector<Node*> stack;
    // "R" = king in French but rook in English: we switch to French reading
    // as soon as a move only reads in French (Cf3, Fb5, Te1…), then prefer it.
    bool french = false;
    // First move of a variation: it always opens a branch, even when identical to the move it
    // replaces (otherwise the rest of the variation would extend the main line) or to the first move
    // of a neighbouring variation (ChessBase writes "({1)} 4... Qg8 …) ({2)} 4... Qg8 …)": merging
    // them into one would lose their pre-move comments and change the tree).
    bool variationStart = false;

    auto normalized = [](std::string_view c) {
        std::string t;
        for (char ch : trim(c)) {
            if (ch == '\x04') { // ChessBase diagram, which its PGN export sometimes writes in this raw form
                t += "[#]";
                continue;
            }
            if (isSpace(ch)) {
                if (!t.empty() && t.back() != ' ') t += ' ';
            } else {
                t += ch;
            }
        }
        return t;
    };
    auto appendComment = [&](Node* node, std::string_view c) {
        const std::string t = normalized(c);
        if (t.empty()) return;
        if (!node->comment.empty()) node->comment += ' ';
        node->comment += t;
    };
    // Pre-move comment (Node::preComment): a comment that does not directly follow a
    // move — at the head of a variation, after the move's comment, after a variation — waits for the next
    // move. If none comes (end of line), it joins the comment of the last move.
    // A comment made only of commands ([%clk …]) always stays with the move already played.
    bool afterMove = false; // the last element read is a move (or its symbols)
    std::string pending;
    // Training question read in front of the move to find (ChessBase writes it there): it waits for that move.
    std::string pendingQuestion;
    auto flushPending = [&] {
        if (!pendingQuestion.empty() && cur->parent) applyQuestion(cur, pendingQuestion); // no move followed
        pendingQuestion.clear();
        if (pending.empty()) return;
        appendComment(cur, pending);
        pending.clear();
    };
    auto moveAdded = [&] {
        if (!pending.empty()) cur->preComment += (cur->preComment.empty() ? "" : " ") + pending; // never overwritten if the move already existed
        pending.clear();
        if (!pendingQuestion.empty()) applyQuestion(cur, pendingQuestion);
        pendingQuestion.clear();
        afterMove = true;
    };

    while (i < n) {
        char c = text[i];
        if (isSpace(c)) {
            ++i;
        } else if (c == '{') {
            size_t e = text.find('}', i + 1);
            if (e == std::string_view::npos) e = n;
            std::string_view body = text.substr(i + 1, e - i - 1);
            // Training questions: one whose move is the one just read belongs
            // to it (Hydra64's former writing, after the move); otherwise it bears on
            // the following move (ChessBase's writing, in front of the move).
            std::string withoutQuestions;
            if (body.find("[%tqu") != std::string_view::npos) {
                withoutQuestions = normalized(body); // the file's line breaks are not part of the question
                for (const std::string& args : takeQuestions(withoutQuestions)) {
                    const std::vector<std::string> fields = questionFields(args);
                    const bool own = afterMove && cur->parent && fields.size() >= 5 && fields[4].size() >= 4
                                     && fields[4].compare(0, 4, squareName(cur->move.from) + squareName(cur->move.to)) == 0;
                    if (own) applyQuestion(cur, args);
                    else pendingQuestion = args;
                }
                body = withoutQuestions;
                if (normalized(body).empty()) { // nothing else in this comment
                    i = e + 1;
                    continue;
                }
            }
            if (!afterMove && (variationStart || cur->parent)) {
                const std::string t = normalized(body);
                if (!withoutCommands(t).empty()) {
                    pending += (pending.empty() ? "" : " ") + t;
                    i = e + 1;
                    continue;
                }
            }
            afterMove = false;
            appendComment(cur, body);
            if (const int clk = extractClock(cur->comment); clk >= 0) cur->clockMs = clk;
            if (cur->comment.find("[%c") != std::string::npos || cur->comment.find("[%draw") != std::string::npos) extractGraphics(cur->comment, cur->graphics);
            if (cur->comment.find("[#]") != std::string::npos || cur->comment.find("[%mdl") != std::string::npos) extractMarkers(cur);
            i = e + 1;
        } else if (c == ';') {
            size_t e = text.find('\n', i);
            if (e == std::string_view::npos) e = n;
            appendComment(cur, text.substr(i + 1, e - i - 1));
            i = e;
        } else if (c == '%' && (i == 0 || text[i - 1] == '\n')) {
            size_t e = text.find('\n', i);
            i = e == std::string_view::npos ? n : e;
        } else if (c == '(') {
            flushPending();
            afterMove = false;
            stack.push_back(cur);
            if (cur->parent) cur = cur->parent;
            variationStart = true;
            ++i;
        } else if (c == ')') {
            flushPending();
            afterMove = false;
            if (!stack.empty()) {
                cur = stack.back();
                stack.pop_back();
            }
            variationStart = false;
            ++i;
        } else if (c == '$') {
            size_t b = ++i;
            while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
            if (i > b) cur->nags.push_back(std::stoi(std::string(text.substr(b, i - b))));
        } else if (c == '}') {
            ++i; // closing brace with no open comment: ignored (it used to block reading)
        } else if (c == '[') {
            break; // start of the next game
        } else {
            size_t b = i;
            while (i < n && !isSpace(text[i]) && text[i] != '{' && text[i] != '}' && text[i] != '('
                   && text[i] != ')' && text[i] != ';' && text[i] != '$')
                ++i;
            std::string_view tok = text.substr(b, i - b);

            if (isResult(tok)) {
                if (game.header("Result").empty()) game.setHeader("Result", std::string(tok));
                continue;
            }
            // Strip the move number: "12." "12..." "12.e4"
            size_t k = 0;
            while (k < tok.size() && std::isdigit(static_cast<unsigned char>(tok[k]))) ++k;
            if (k > 0 && k < tok.size() && tok[k] == '.') {
                while (k < tok.size() && tok[k] == '.') ++k;
                tok = tok.substr(k);
            } else if (k == tok.size()) {
                continue; // bare number
            }
            while (!tok.empty() && tok.front() == '.') tok.remove_prefix(1);
            if (tok.empty()) continue;

            // Annotations stuck to the move
            size_t s = tok.size();
            while (s > 0 && (tok[s - 1] == '!' || tok[s - 1] == '?')) --s;
            int suffixNag = nagFromSuffix(tok.substr(s));
            std::string_view san = tok.substr(0, s);
            if (san == "--" || san == "Z0") { // null move: the turn passes (analyses, ChessBase input)
                cur = game.addMove(cur, Move(), variationStart && !stack.empty(), options.withSan);
                moveAdded();
                variationStart = false;
                continue;
            }

            Move m = french ? cur->pos.parseSan(frenchToEnglish(san)) : cur->pos.parseSan(san);
            if (m.isNull()) {
                m = french ? cur->pos.parseSan(san) : cur->pos.parseSan(frenchToEnglish(san));
                if (!m.isNull() && !french) french = true;
            }
            // Irregular but playable move (kept by ChessBase: king left in check, pawn
            // capture onto an empty square): accepted as a last resort.
            if (m.isNull()) m = cur->pos.parseSanLenient(french ? frenchToEnglish(san) : std::string(san));
            if (m.isNull()) {
                if (error) *error = "Coup illisible : " + std::string(tok);
                game.setCurrent(game.root());
                return false;
            }
            cur = game.addMove(cur, m, variationStart && !stack.empty(), options.withSan);
            moveAdded();
            variationStart = false;
            if (suffixNag) cur->nags.push_back(suffixNag);
        }
    }
    flushPending();

    game.setCurrent(game.root());
    return true;
}

namespace {
// Tag value: "\" and '"' preceded by a backslash (PGN standard), otherwise
// they vanish or cut the value when read back.
std::string escapeTag(const std::string& v)
{
    if (v.find_first_of("\\\"") == std::string::npos) return v;
    std::string out;
    for (char c : v) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}
} // namespace

bool keepsPreComment(const Node* n)
{
    const Node* parent = n ? n->parent : nullptr;
    if (!parent) return false;
    if (parent->mainChild() != n) return true; // first move of a variation
    if (!parent->parent) return false;         // first move of the game: pre-game comment
    const bool written = !parent->comment.empty() || parent->clockMs >= 0 || parent->medals || parent->question || !parent->graphics.empty();
    const bool afterVariations = parent->parent->mainChild() == parent && parent->parent->children.size() > 1;
    return written || afterVariations;
}

std::string writeGame(const Game& game)
{
    std::string out;
    static const char* roster[] = {"Event", "Site", "Date", "Round", "White", "Black", "Result"};
    for (const char* key : roster) {
        std::string v = game.header(key);
        if (v.empty()) v = std::string(key) == "Result" ? "*" : (std::string(key) == "Date" ? "????.??.??" : "?");
        out += "[" + std::string(key) + " \"" + escapeTag(v) + "\"]\n";
    }
    for (const auto& [k, v] : game.headers()) {
        bool inRoster = false;
        for (const char* key : roster) inRoster |= (k == key);
        if (!inRoster) out += "[" + k + " \"" + escapeTag(v) + "\"]\n";
    }
    out += '\n';

    std::string text;
    bool noSpace = true;
    auto put = [&](const std::string& t) {
        if (!noSpace) text += ' ';
        text += t;
        noSpace = false;
    };

    auto emitMove = [&](const Node* node, bool forceNumber) -> bool {
        const Position& before = node->parent->pos;
        if (!node->preComment.empty()) { // pre-move comment, then the move number
            // Nothing written between the previous move and this comment (neither comment nor variation):
            // an empty comment separates them, otherwise it would read back as the comment of the previous
            // move ("Re3 $220 {} {24...} 24... Bf8").
            if (node->parent->parent && !keepsPreComment(node)) put("{}");
            put("{" + node->preComment + "}");
            forceNumber = true;
        }
        if (node->question) { // training question: in front of the move to find, like ChessBase
            put("{" + formatQuestion(node) + "}");
            forceNumber = true;
        }
        std::string tok;
        if (before.sideToMove() == White) tok = std::to_string(before.fullmoveNumber()) + ". ";
        else if (forceNumber) tok = std::to_string(before.fullmoveNumber()) + "... ";
        tok += node->san.empty() ? before.toSan(node->move) : node->san; // notation not computed when reading
        put(tok);
        for (int nag : node->nags) put("$" + std::to_string(nag));
        std::string comment = node->comment;

        if (node->medals) comment = "[%mdl " + std::to_string(node->medals) + "]" + (comment.empty() ? "" : " " + comment);
        if (const std::string graphics = formatGraphics(node->graphics); !graphics.empty()) comment = graphics + (comment.empty() ? "" : " " + comment);
        if (node->clockMs >= 0) comment = "[%clk " + formatClock(node->clockMs) + "]" + (comment.empty() ? "" : " " + comment);
        if (!comment.empty()) {
            put("{" + comment + "}");
            return true;
        }
        return false;
    };

    std::function<void(const Node*, bool)> emitLine = [&](const Node* n, bool needNumber) {
        while (const Node* main = n->mainChild()) {
            needNumber = emitMove(main, needNumber);
            for (size_t v = 1; v < n->children.size(); ++v) {
                const Node* var = n->children[v].get();
                put("(");
                noSpace = true;
                emitMove(var, true);
                emitLine(var, false);
                text += ')';
                needNumber = true;
            }
            n = main;
        }
    };

    {
        const std::string graphics = formatGraphics(game.root()->graphics) + (game.root()->medals ? "[%mdl " + std::to_string(game.root()->medals) + "]" : "");
        // Diagram of the starting position: no move to put "$220" on, so it is written "[#]"
        // in the pre-game comment (this is how ChessBase writes it, and how it reads back).
        const auto& rootNags = game.root()->nags;
        const std::string text = game.root()->comment
                                 + (std::find(rootNags.begin(), rootNags.end(), DiagramNag) != rootNags.end() ? std::string(game.root()->comment.empty() ? "" : " ") + "[#]" : "");
        if (!graphics.empty() || !text.empty()) put("{" + graphics + (graphics.empty() || text.empty() ? "" : " ") + text + "}");
    }
    emitLine(game.root(), true);
    std::string result = game.header("Result");
    put(result.empty() ? "*" : result);

    // Line wrap at 80 columns
    size_t lineStart = 0, lastSpace = std::string::npos;
    for (size_t k = 0; k < text.size(); ++k) {
        // Never break before "[": a comment line starting that way is taken for
        // a tag — hence for the start of a game — by many PGN readers.
        if (text[k] == ' ' && !(k + 1 < text.size() && text[k + 1] == '[')) lastSpace = k;
        if (k - lineStart >= 80 && lastSpace != std::string::npos && lastSpace > lineStart) {
            text[lastSpace] = '\n';
            lineStart = lastSpace + 1;
            lastSpace = std::string::npos;
        }
    }
    out += text + "\n\n";
    return out;
}

std::vector<GameEntry> indexDatabase(std::string_view data)
{
    std::vector<GameEntry> games;
    const size_t n = data.size();
    // UTF-8 BOM at the start of the file (ChessBase exports): skipped, otherwise the first line does not
    // start with "[" and the first tag of the first game is lost.
    size_t i = data.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
    bool inMoves = true; // so that a tag at the start of the file opens a game
    bool inComment = false; // inside a { … } comment opened on a previous line
    GameEntry* cur = nullptr;
    // Tracks the braces of a move line: a comment line starting with "["
    // is not a tag and does not open a game.
    auto trackBraces = [&](size_t from, size_t to) {
        while (from < to) {
            const char* hit = static_cast<const char*>(std::memchr(data.data() + from, inComment ? '}' : '{', to - from));
            if (!hit) return;
            inComment = !inComment;
            from = size_t(hit - data.data()) + 1;
        }
    };

    while (i < n) {
        size_t lineEnd = data.find('\n', i);
        if (lineEnd == std::string_view::npos) lineEnd = n;
        size_t j = i;
        while (j < lineEnd && (data[j] == ' ' || data[j] == '\t' || data[j] == '\r')) ++j;

        if (inComment) {
            trackBraces(i, lineEnd);
            inMoves = true;
        } else if (j < lineEnd && data[j] == '[') {
            if (inMoves) {
                if (cur) cur->length = i - cur->offset;
                games.emplace_back();
                cur = &games.back();
                cur->offset = i;
                inMoves = false;
            }
            std::string name, value;
            size_t k = j;
            if (readTag(data, k, name, value)) {
                if (name == "White") cur->white = value;
                else if (name == "Black") cur->black = value;
                else if (name == "WhiteElo") cur->whiteElo = value;
                else if (name == "BlackElo") cur->blackElo = value;
                else if (name == "Result") cur->result = value;
                else if (name == "Date") cur->date = value;
                else if (name == "Event") cur->event = value;
                else if (name == "Site") cur->site = value;
                else if (name == "Round") cur->round = value;
                else if (name == "ECO") cur->eco = value;
            }
        } else if (j < lineEnd) {
            inMoves = true;
            trackBraces(j, lineEnd);
        }
        i = lineEnd + 1;
    }
    if (cur) cur->length = n - cur->offset;
    return games;
}

} // namespace chess::pgn
