// Tests of the chess core: perft (move generation) and PGN round trip.
#include "chess/aquarium.h"
#include "chess/ctg.h"
#include "chess/eco.h"
#include "chess/tablebase.h"
#include "db/heatmap.h"
#include "db/preparation.h"
#include "db/materialquery.h"
using hy64db::MaterialMatcher;
#include "chess/game.h"
#include "chess/pgn.h"
#include "chess/repertoire.h"
#include "chess/epd.h"
#include <iterator>
#include "chess/polyglot.h"
#include "chess/treebook.h"
#include "chess/position.h"
#include "db/beauty.h"
#include "db/convert.h"
#include "db/database.h"
#include "db/gamecodec.h"
#include "db/nameindex.h"
#include "db/patternsearch.h"
#include "db/playerbase.h"
#include "db/positionindex.h"
#include "db/names.h"
#include "deep/deepplanner.h"
#include <random>

#include <algorithm>
#include <deque>
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace chess;

static uint64_t perft(const Position& p, int depth)
{
    if (depth == 0) return 1;
    auto moves = p.legalMoves();
    if (depth == 1) return moves.size();
    uint64_t n = 0;
    for (const Move& m : moves) n += perft(p.after(m), depth - 1);
    return n;
}

static int failures = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

// Encodes then decodes a game with the Hydra64 database codec; true if the PGN is identical.
static bool codecRoundTrip(const Game& g, std::string* why = nullptr)
{
    std::vector<uint8_t> blob;
    hy64db::GameInfo info;
    std::string err;
    if (!hy64db::encodeGame(g, blob, info, &err)) {
        if (why) *why = "codage : " + err;
        return false;
    }
    hy64db::Names names;
    hy64db::Record r = info.record;
    auto id = [&](hy64db::NameType t, const std::string& v) { return names.intern(t, v, nullptr); };
    r.white = id(hy64db::Player, info.white);
    r.black = id(hy64db::Player, info.black);
    r.event = id(hy64db::Event, info.event);
    r.site = id(hy64db::Site, info.site);
    r.round = id(hy64db::Round, info.round);
    if (!info.annotator.empty()) r.annotator = id(hy64db::Annotator, info.annotator);
    if (!info.source.empty()) r.source = id(hy64db::Source, info.source);
    uint8_t raw[hy64db::RecordSize];
    r.write(raw);
    const hy64db::Record back = hy64db::Record::read(raw);
    Game out;
    if (!hy64db::decodeGame(blob.data(), blob.size(), back, names, out, &err)) {
        if (why) *why = "décodage : " + err;
        return false;
    }
    const std::string a = pgn::writeGame(g), b = pgn::writeGame(out);
    if (a != b && why) {
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
        *why = "différence vers « " + a.substr(i > 30 ? i - 30 : 0, 80) + " » / « " + b.substr(i > 30 ? i - 30 : 0, 80) + " »";
    }
    return a == b;
}

// "Targeted" notation and legality compared with the reference by generation of all the legal moves.
static std::string referenceSan(const Position& pos, Move m)
{
    static const char letters[] = " PNBRQK";
    const Piece p = pos.at(m.from);
    const PieceType t = typeOf(p);
    std::string san;
    if (t == King && std::abs(fileOf(m.to) - fileOf(m.from)) == 2) san = fileOf(m.to) == 6 ? "O-O" : "O-O-O";
    else {
        const bool capture = pos.at(m.to) != NoPiece || (t == Pawn && m.to == pos.epSquare());
        if (t == Pawn) {
            if (capture) san += char('a' + fileOf(m.from));
        } else {
            san += letters[t];
            bool ambiguous = false, sameFile = false, sameRank = false;
            for (const Move& o : pos.legalMoves()) {
                if (o.to != m.to || o.from == m.from || pos.at(o.from) != p) continue;
                ambiguous = true;
                if (fileOf(o.from) == fileOf(m.from)) sameFile = true;
                if (rankOf(o.from) == rankOf(m.from)) sameRank = true;
            }
            if (ambiguous) {
                if (!sameFile) san += char('a' + fileOf(m.from));
                else if (!sameRank) san += char('1' + rankOf(m.from));
                else san += squareName(m.from);
            }
        }
        if (capture) san += 'x';
        san += squareName(m.to);
        if (m.promo != NoType) san += '=', san += letters[m.promo];
    }
    const Position next = pos.after(m);
    if (next.inCheck()) san += next.legalMoves().empty() ? '#' : '+';
    return san;
}

static void testTargetedSan()
{
    std::mt19937 rng(20261120);
    const char* starts[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                            "8/P1k5/8/8/3N1N2/8/1p4K1/N7 w - - 0 1", "R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1",
                            "4k3/8/8/2pP4/8/8/8/R3K2R w KQ c6 0 2", "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1"};
    long positions = 0, moves = 0, bad = 0;
    for (const char* fen : starts)
        for (int game = 0; game < 300; ++game) {
            Position pos;
            pos.setFen(fen);
            for (int ply = 0; ply < 120; ++ply) {
                const std::vector<Move> legal = pos.legalMoves();
                if (legal.empty() != !pos.hasLegalMove()) ++bad;
                if (legal.empty()) break;
                ++positions;
                for (const Move& m : legal) {
                    ++moves;
                    const std::string ref = referenceSan(pos, m), san = pos.toSan(m);
                    if (san != ref || !(pos.parseSan(san) == m) || !pos.isLegal(m)) ++bad;
                    // Over-disambiguation ("Rdxd5", "Ng1f3") accepted as before
                    if (typeOf(pos.at(m.from)) != Pawn && typeOf(pos.at(m.from)) != King) {
                        std::string full = ref.substr(0, 1) + squareName(m.from) + squareName(m.to);
                        if (!(pos.parseSan(full) == m)) ++bad;
                    }
                }
                // Arbitrary moves: isLegal must say exactly "in the list"
                for (int k = 0; k < 40; ++k) {
                    Move m{Square(rng() % 64), Square(rng() % 64), k % 7 == 0 ? Queen : k % 11 == 0 ? Knight : NoType};
                    const bool inList = std::find(legal.begin(), legal.end(), m) != legal.end();
                    if (pos.isLegal(m) != inList) ++bad;
                }
                pos.makeMove(legal[rng() % legal.size()]);
            }
        }
    check(bad == 0 && positions > 50000, "notation et légalité ciblées identiques à la référence (" + std::to_string(positions) + " positions, " + std::to_string(moves)
                                             + " coups, " + std::to_string(bad) + " écarts)");
}

// Chess960: castling rules, FEN (X-FEN and Shredder-FEN), notation, UCI, encoding in a database.
static void testChess960()
{
    struct Case {
        const char* fen;
        uint64_t d1, d2, d3;
    };
    static const Case cases[] = { // references: python-chess
        {"bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9", 21, 528, 12189},
        {"2nnrbkr/p1qppppp/8/1ppb4/6PP/3PP3/PPP2P2/BQNNRBKR w HEhe - 1 9", 21, 807, 18002},
        {"b1q1rrkb/pppppppp/3nn3/8/P7/1PPP4/4PPPP/BQNNRKRB w GE - 1 9", 20, 479, 10471},
        {"qbbnnrkr/2pp2pp/p7/1p2pp2/8/P3PP2/1PPP1KPP/QBBNNR1R w hf - 0 9", 22, 593, 13440},
        {"r1k1r2q/p1ppp1pp/8/8/8/8/P1PPP1PP/R1K1R2Q w KQkq - 0 1", 23, 522, 12333},
        {"r1k2r1q/p1ppp1pp/8/8/8/8/P1PPP1PP/R1K2R1Q w KQkq - 0 1", 28, 738, 20218},
        {"8/8/8/4B2b/6nN/8/5P2/2R1K2k w Q - 0 1", 34, 318, 9002},
        {"2r5/8/8/8/8/8/6PP/k2KR3 w K - 0 1", 17, 242, 3931},
        {"4r3/3k4/8/8/8/8/6PP/qR1K1R2 w KQ - 0 1", 19, 628, 12858},
    };
    for (const Case& c : cases) {
        Position p;
        const bool ok = p.setFen(c.fen) && perft(p, 1) == c.d1 && perft(p, 2) == c.d2 && perft(p, 3) == c.d3;
        check(ok, std::string("Chess960 perft 3  ") + c.fen + (ok ? "" : "  → " + std::to_string(perft(p, 1)) + " " + std::to_string(perft(p, 2)) + " " + std::to_string(perft(p, 3))));
    }
    // Numbered starting positions
    auto backRank = [](int n) { return Position::chess960Start(n).fen().substr(0, 8); };
    check(Position::chess960Start(518).fen() == Position::startPos().fen() && !Position::chess960Start(518).isChess960() && backRank(0) == "bbqnnrkr"
              && backRank(1) == "bqnbnrkr" && backRank(959) == "rkrnnqbb" && backRank(300) == "qbnrkrbn" && Position::chess960Start(300).isChess960(),
          "Chess960 : positions de départ n° 0, 1, 300, 518 (classique), 959");
    // FEN: X-FEN and Shredder-FEN give the same position; round trip
    Position x, sh;
    const bool both = x.setFen("bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9") && sh.setFen(x.fen()) && sh.fen() == x.fen()
                      && x.castlingRook(White, true) == parseSquare("h1") && x.castlingRook(White, false) == parseSquare("f1");
    check(both && x.isChess960(), "Chess960 : FEN à colonnes de tours relue à l'identique — " + x.fen());
    // Castling: notation, UCI ("king captures its rook" and king onto its target square), position after the move
    Position p;
    p.setFen("r1k2r1q/p1pppppp/8/8/8/8/P1PPPPPP/R1K2R1Q w KQkq - 0 1"); // f-file closed: the king can cross f1
    const Move oo = p.parseSan("O-O"), ooo = p.parseSan("O-O-O");
    const bool notation = !oo.isNull() && !ooo.isNull() && p.isCastling(oo) && p.toSan(oo) == "O-O" && p.toSan(ooo) == "O-O-O" && Position::toUci(oo) == "c1f1"
                          && Position::toUci(ooo) == "c1a1" && p.parseUci("c1f1") == oo && p.parseUci("c1g1") == oo && p.parseUci("c1a1") == ooo;
    const Position afterOO = p.after(oo), afterOOO = p.after(ooo);
    check(notation && afterOO.fen() == "r1k2r1q/p1pppppp/8/8/8/8/P1PPPPPP/R4RKQ b kq - 1 1" && afterOOO.fen() == "r1k2r1q/p1pppppp/8/8/8/8/P1PPPPPP/2KR1R1Q b kq - 1 1",
          "Chess960 : O-O et O-O-O (notation, UCI, position d'arrivée) — " + afterOO.fen() + " | " + afterOOO.fen());
    // Chess960 PGN game: reading, writing, then encoding in a database and rereading
    const std::string pgnText = "[Event \"960\"]\n[Variant \"Chess960\"]\n[SetUp \"1\"]\n[FEN \"rqknnbbr/pppppppp/8/8/8/8/PPPPPPPP/RQKNNBBR w KQkq - 0 1\"]\n\n"
                                "1. f4 f5 2. Bf2 g6 3. Nf3 c6 4. e3 Ne6 5. Be2 d6 6. c4 Qc7 7. Nc3 Bg7 8. d4 O-O-O 9. h3 Nf6 10. O-O h6 11. b4 Bf7 1-0\n";
    Game g;
    std::string err;
    const bool parsed = pgn::parseGame(pgnText, g, &err) && g.mainline().size() == 22;
    Game again;
    const bool rewritten = parsed && pgn::parseGame(pgn::writeGame(g), again) && again.mainline().size() == 22 && again.mainline().back()->pos.fen() == g.mainline().back()->pos.fen();
    std::vector<uint8_t> blob;
    hy64db::GameInfo info;
    hy64db::Names names;
    Game decoded;
    const bool coded = parsed && hy64db::encodeGame(g, blob, info, &err) && hy64db::decodeGame(blob.data(), blob.size(), info.record, names, decoded, &err)
                       && decoded.mainline().size() == 22 && decoded.mainline().back()->pos.fen() == g.mainline().back()->pos.fen()
                       && decoded.mainline()[15]->san == "O-O-O" && decoded.mainline()[18]->san == "O-O";
    check(parsed && rewritten && coded, "Chess960 : partie PGN lue, réécrite, codée dans une base et relue" + (err.empty() ? std::string() : " — " + err));
}

// PGN: a comment with a line starting with "[" does not cut the game at indexing, an
// orphan closing brace does not block reading, and the "for encoding" reading (notation
// not computed, main line in binary) gives the same game as the normal reading.
static void testPgnRobustness()
{
    const std::string two = "[Event \"A\"]\n[Result \"1-0\"]\n\n1. e4 e5 {un commentaire assez long pour être coupé\n[source: Gazette 1840]} 2. Nf3 Nc6 1-0\n\n"
                            "[Event \"B\"]\n[Result \"*\"]\n\n1. d4 *\n";
    const auto index = pgn::indexDatabase(two);
    Game a;
    const bool split = index.size() == 2 && index[0].event == "A" && index[1].event == "B"
                       && pgn::parseGame(std::string_view(two).substr(index[0].offset, index[0].length), a) && a.mainline().size() == 4;
    check(split, "PGN : « [ » en début de ligne dans un commentaire — " + std::to_string(index.size()) + " parties indexées");
    // UTF-8 BOM at the start (ChessBase PGN export): the first tag of the first game is kept
    const std::string withBom = "\xEF\xBB\xBF" + two;
    const auto bomIndex = pgn::indexDatabase(withBom);
    Game bomGame;
    check(bomIndex.size() == 2 && bomIndex[0].event == "A" && pgn::parseGame(withBom, bomGame) && bomGame.header("Event") == "A",
          "PGN : BOM UTF-8 en tête de fichier — premier tag gardé");
    Game stray;
    check(pgn::parseGame("[Event \"C\"]\n\n} 1. e4 } e5 *\n", stray) && stray.mainline().size() == 2, "PGN : accolade fermante orpheline ignorée");
    // Writing never breaks a line before "["
    Game w;
    Node* n = w.addMove(w.root(), w.root()->pos.parseSan("e4"));
    n->comment = std::string(70, 'x') + " [source: y] " + std::string(70, 'z') + " [autre] fin";
    const std::string written = pgn::writeGame(w);
    check(written.find("\n[source") == std::string::npos && written.find("\n[autre") == std::string::npos && pgn::indexDatabase(written + written).size() == 2,
          "PGN : pas de retour à la ligne devant « [ » à l'écriture");
    // Tags: "\" and '"' survive writing then rereading
    Game tagged, reread;
    tagged.setHeader("White", "Staunton\\Owen");
    tagged.setHeader("Event", "Tournoi \"A\"");
    check(pgn::parseGame(pgn::writeGame(tagged), reread) && reread.header("White") == "Staunton\\Owen" && reread.header("Event") == "Tournoi \"A\"",
          "PGN : barre oblique inverse et guillemet dans un tag — " + reread.header("White") + " / " + reread.header("Event"));
    // Arrows and marked squares: read from the PGN ([%cal], [%csl]), kept out of the comment, rewritten,
    // encoded in a database and reread
    {
        const std::string drawn = "[Event \"G\"]\n\n{[%csl Gd4] départ} 1. e4 {[%clk 0:05:00] [%csl Rd5,Ye5][%cal Ge2e4,Bd1h5] le centre} e5 {[%cal Rg8f6]} 2. Nf3 *\n";
        Game g, again, decoded;
        const bool read = pgn::parseGame(drawn, g) && g.mainline().size() == 3;
        const Node* e4 = read ? g.mainline()[0] : nullptr;
        const bool parsed = read && e4->comment == "le centre" && e4->clockMs == 300000 && e4->graphics.size() == 4
                            && e4->graphics[0] == Graphic{parseSquare("d5"), NoSquare, 'R'} && e4->graphics[1] == Graphic{parseSquare("e5"), NoSquare, 'Y'}
                            && e4->graphics[2] == Graphic{parseSquare("e2"), parseSquare("e4"), 'G'} && e4->graphics[3] == Graphic{parseSquare("d1"), parseSquare("h5"), 'B'}
                            && g.mainline()[1]->comment.empty() && g.mainline()[1]->graphics.size() == 1 && g.root()->graphics.size() == 1 && g.root()->comment == "départ";
        const std::string out = parsed ? pgn::writeGame(g) : std::string();
        const bool written = out.find("{[%clk 0:05:00] [%csl Rd5,Ye5][%cal Ge2e4,Bd1h5] le") != std::string::npos && out.find("{[%cal Rg8f6]}") != std::string::npos
                             && out.find("{[%csl Gd4] départ}") != std::string::npos && pgn::parseGame(out, again) && pgn::writeGame(again) == out;
        std::vector<uint8_t> blob;
        hy64db::GameInfo info;
        hy64db::Names names;
        std::string cerr2;
        const bool coded = written && hy64db::encodeGame(g, blob, info, &cerr2) && hy64db::decodeGame(blob.data(), blob.size(), info.record, names, decoded, &cerr2)
                           && decoded.mainline()[0]->graphics == e4->graphics && decoded.root()->graphics == g.root()->graphics && decoded.mainline()[0]->comment == "le centre";
        check(parsed && written && coded, "PGN et base : flèches et cases marquées ([%cal], [%csl]) lues, réécrites, codées et relues" + (cerr2.empty() ? "" : " — " + cerr2));
    }
    // "[%draw …]" (Scid): converted into arrows and squares; an unknown form stays in the comment
    {
        Game g;
        const bool parsed = pgn::parseGame("[Event \"D\"]\n\n1. e4 {[%draw arrow,e2,e4,red] [%draw full,d5,yellow] [%draw arrow,g1,f3] [%draw text,a1,x] idée} e5 *\n", g);
        const Node* e4 = parsed ? g.mainline()[0] : nullptr;
        const std::vector<Graphic> want{{parseSquare("e2"), parseSquare("e4"), 'R'}, {parseSquare("d5"), NoSquare, 'Y'}, {parseSquare("g1"), parseSquare("f3"), 'G'}};
        check(e4 && e4->graphics == want && e4->comment == "[%draw text,a1,x] idée", "PGN : [%draw] converti en flèches et cases — " + (e4 ? e4->comment : std::string("?")));
    }
    check(pgn::elapsedSeconds("[%emt 0:02:10] bien") == 130 && pgn::elapsedSeconds("x [%emt 1:00:05]") == 3605 && pgn::elapsedSeconds("[%emt 0:12]") == 12
              && pgn::elapsedSeconds("[%emt 7]") == 7 && pgn::elapsedSeconds("[%clk 0:05:00]") == -1 && pgn::elapsedSeconds("[%emt ?]") == -1,
          "PGN : temps passé sur le coup lu de [%emt]");
    // "With comments" search in a PGN: a text to read is needed, not just commands
    {
        using hy64db::hasTextComment;
        check(!hasTextComment("1. e4 {[%clk 0:05:00]} e5 {[%eval 12,20] [%emt 0:00:03]} *") && !hasTextComment("1. e4 e5 { } *") && !hasTextComment("1. e4 e5 *")
                  && hasTextComment("1. e4 {[%clk 0:05:00] le centre} e5 *") && hasTextComment("1. e4 e5 {bien} *"),
              "recherche PGN « avec commentaires » : les commandes seules ne comptent pas");
    }
    // Pre-move comment (ChessBase: "({or} 2. Bc4)"): at the head of a variation, after the
    // previous move's comment, after a variation; at the end of a line it joins the last move.
    {
        Game g, again, decoded;
        const bool read = pgn::parseGame("1. e4 e5 {après e5} {avant Cf3} 2. Nf3 ({ou} 2. Bc4 {après Fc4}) {après la variante} 2... Nc6 {fin} {dernier mot} *", g);
        const auto line = g.mainline();
        const Node* bc4 = read && line.size() == 4 && line[1]->children.size() == 2 ? line[1]->children[1].get() : nullptr;
        const bool parsed = bc4 && line[1]->comment == "après e5" && line[2]->preComment == "avant Cf3" && line[2]->comment.empty() && bc4->preComment == "ou"
                            && bc4->comment == "après Fc4" && line[3]->preComment == "après la variante" && line[3]->comment == "fin dernier mot" && line[0]->preComment.empty()
                            && pgn::keepsPreComment(line[2]) && pgn::keepsPreComment(bc4) && pgn::keepsPreComment(line[3]) && !pgn::keepsPreComment(line[1])
                            && !pgn::keepsPreComment(line[0]);
        const std::string out = pgn::writeGame(g);
        std::vector<uint8_t> blob;
        hy64db::GameInfo info;
        hy64db::Names names;
        std::string flat = out; // writing wraps lines at 80 columns
        std::replace(flat.begin(), flat.end(), '\n', ' ');
        const bool written = flat.find("1. e4 e5 {après e5} {avant Cf3} 2. Nf3 ({ou} 2. Bc4 {après Fc4}) {après la variante} 2... Nc6 {fin dernier mot} *") != std::string::npos
                             && pgn::parseGame(out, again) && pgn::writeGame(again) == out;
        const bool coded = hy64db::encodeGame(g, blob, info) && hy64db::decodeGame(blob.data(), blob.size(), info.record, names, decoded) && decoded.mainline().size() == 4
                           && decoded.mainline()[2]->preComment == "avant Cf3" && decoded.mainline()[1]->children[1]->preComment == "ou"
                           && decoded.mainline()[1]->children[1]->comment == "après Fc4" && decoded.mainline()[3]->preComment == "après la variante"
                           && decoded.mainline()[3]->comment == "fin dernier mot" && (info.record.flags & hy64db::FlagComments);
        // A pre-move comment alone is enough for the "commented" flag; commands alone stay with the move played
        Game only, clocks;
        std::vector<uint8_t> blob2;
        hy64db::GameInfo info2;
        const bool flagged = pgn::parseGame("1. e4 ({ou} 1. d4) *", only) && hy64db::encodeGame(only, blob2, info2) && (info2.record.flags & hy64db::FlagComments);
        const bool commands = pgn::parseGame("1. e4 {[%eval 12,20]} {[%clk 0:05:00]} e5 *", clocks) && clocks.mainline()[0]->clockMs == 300000 && clocks.mainline()[1]->preComment.empty();
        check(parsed && written && coded && flagged && commands,
              std::string("PGN et base : commentaire d'avant-coup lu, réécrit, codé") + (parsed ? "" : " [lecture]") + (written ? "" : " [écriture : " + out + "]") + (coded ? "" : " [base]")
                  + (flagged ? "" : " [drapeau]") + (commands ? "" : " [commandes]"));
    }
    // ChessBase markers in comments: "[#]" (diagram) and "[%mdl n]" (medals)
    {
        Game g, again, decoded;
        const bool read = pgn::parseGame("\n1. e4 {[#] position de base} e5 {[%mdl 4160] bien joué} 2. Nf3 $220 *\n", g);
        const Node* e4 = g.mainline()[0];
        const Node* e5 = g.mainline()[1];
        const bool parsed = read && e4->comment == "position de base" && e4->nags == std::vector<int>{pgn::DiagramNag} && e5->medals == 4160 && e5->comment == "bien joué"
                            && pgn::medalNames(e5->medals) == std::vector<std::string>{"tactique", "finale"} && pgn::nagSymbol(pgn::DiagramNag) == "▦";
        const std::string out = pgn::writeGame(g);
        std::vector<uint8_t> blob;
        hy64db::GameInfo info;
        hy64db::Names names;
        const bool kept = parsed && out.find("1. e4 $220 {position de base} 1... e5 {[%mdl 4160] bien joué}") != std::string::npos && pgn::parseGame(out, again) && pgn::writeGame(again) == out
                          && hy64db::encodeGame(g, blob, info) && hy64db::decodeGame(blob.data(), blob.size(), info.record, names, decoded) && decoded.mainline()[1]->medals == 4160
                          && decoded.mainline()[0]->nags == e4->nags && ((info.record.flags >> hy64db::UserFlagShift) & 0x3F) == ((1 << 2) | (1 << 3)); // Endgame + Tactics
        check(parsed && kept, "PGN et base : diagramme « [#] » et médailles « [%mdl] » lus, réécrits, codés ; drapeaux Finale et Tactique posés");
    }
    // ChessBase evaluation profile "[%evp]": kept in the comment, hidden on display,
    // carried over to the moves (White's side), mates included
    {
        Game g;
        const bool read = pgn::parseGame("\n{[%evp 0,4,21,30,-49,\n61,32760] Conditions de jeu} 1. e4 e5 2. Nf3 Nc6 3. Bb5 {[%emt 0:02:10] [%eval -35,18] bien} *\n", g);
        const std::string before = pgn::writeGame(g);
        const int applied = pgn::applyEvalProfile(g) - 1; // (a move "[%eval]" in addition to the profile)
        const auto line = g.mainline();
        check(read && applied == 5 && g.root()->eval.valid && g.root()->eval.value == 21 && line[0]->eval.value == 30 && line[1]->eval.value == -49 && line[2]->eval.value == 61
                  && line[3]->eval.mate && line[3]->eval.value == 7 && line[4]->eval.value == -35 && line[4]->eval.depth == 18 && pgn::withoutCommands(line[4]->comment) == "bien" && pgn::withoutCommands(g.root()->comment) == "Conditions de jeu"
                  && pgn::writeGame(g) == before && pgn::applyEvalProfile(g) == 0 && pgn::withoutCommands("texte seul") == "texte seul",
              "profil d'évaluation « [%evp] » : " + std::to_string(applied) + " coups évalués, texte gardé dans la partie et masqué à l'affichage");
    }
    // "Commented game" flag: not for a game that only carries hidden commands
    {
        auto flagged = [](const char* text) {
            Game g;
            pgn::parseGame(text, g);
            std::vector<uint8_t> blob;
            hy64db::GameInfo info;
            hy64db::encodeGame(g, blob, info);
            hy64db::Names names;
            Game back;
            const bool kept = hy64db::decodeGame(blob.data(), blob.size(), info.record, names, back) && back.root()->comment == g.root()->comment;
            return std::pair<bool, bool>(info.record.flags & hy64db::FlagComments, kept);
        };
        const auto onlyCommands = flagged("\n{[%evp 0,2,21,30,-49]} 1. e4 {[%emt 0:00:12]} e5 {[%eval 20,18]} *\n");
        const auto realText = flagged("\n{[%evp 0,2,21,30,-49]} 1. e4 {[%emt 0:00:12] le meilleur} e5 *\n");
        const auto plain = flagged("\n1. e4 {bon} e5 *\n");
        check(!onlyCommands.first && onlyCommands.second && realText.first && plain.first, "drapeau « commentée » : absent quand il n'y a que des commandes [%…], qui restent pourtant dans la partie");
    }
    // Several "[%clk]" in a comment: only one kept, and the round trip through PGN is stable
    {
        Game g, again;
        const bool read = pgn::parseGame("\n1. e4 {[%clk 1:30:00] solide [%clk 1:29:10] [%clk x]} e5 {[%clk 1:28:00][%clk 1:27:00]} *\n", g);
        const std::string once = pgn::writeGame(g);
        const bool stable = read && pgn::parseGame(once, again) && pgn::writeGame(again) == once;
        check(stable && g.mainline()[0]->clockMs == 5400000 && g.mainline()[0]->comment == "solide" && g.mainline()[1]->clockMs == 5280000 && g.mainline()[1]->comment.empty(),
              "PGN : plusieurs « [%clk] » dans un commentaire, le premier gardé, aller-retour stable");
    }
    // Training questions: "[%tqu …]" read, rewritten, encoded in a database. ChessBase writes it in front of
    // the move to find, with hints, feedback and points, one block per language: everything is kept. A
    // question written after its own move (Hydra64's former writing) stays attached to it.
    {
        Game g, again, decoded;
        const std::string full = "\"En\",\"Comment défendre e5 ?\",\"Un cavalier\",\"\",\"b8c6\",\"Le plus naturel.\",6,\"De\",\"Wie deckt man e5?\",\"\",\"\",\"b8c6\",\"\",6";
        const bool read = pgn::parseGame("\n1. e4 e5 2. Nf3 {le plus naturel} {[%tqu " + full + "]} Nc6 3. Bb5 {[%tqu \"\",\"\",\"\",\"\",\"f1b5\",\"\",10]} a6 {[%tqu \"En\",\"Et maintenant ?\",\"\",\"\",\"b5a4\",\"\",10]} 4. Ba4 *\n", g);
        const auto line = g.mainline();
        const bool parsed = read && line.size() == 7 && !line[2]->question && line[2]->comment == "le plus naturel" && line[3]->question && line[3]->questionText == "Comment défendre e5 ?"
                            && line[3]->questionData == full && line[3]->preComment.empty() && line[4]->question && line[4]->questionText.empty() && line[4]->questionData.empty()
                            && line[4]->comment.empty() && !line[5]->question && line[5]->comment.empty() && line[6]->question && line[6]->questionText == "Et maintenant ?";
        std::string out = pgn::writeGame(g);
        std::string flat = out;
        std::replace(flat.begin(), flat.end(), '\n', ' ');
        std::vector<uint8_t> blob;
        hy64db::GameInfo info;
        hy64db::Names names;
        const bool written = flat.find("2. Nf3 {le plus naturel} {[%tqu " + full + "]} 2... Nc6 {[%tqu \"\",\"\",\"\",\"\",\"f1b5\",\"\",10]} 3. Bb5") != std::string::npos;
        const bool kept = parsed && pgn::parseGame(out, again) && pgn::writeGame(again) == out && again.mainline()[3]->questionData == full && again.mainline()[4]->question
                          && hy64db::encodeGame(g, blob, info) && hy64db::decodeGame(blob.data(), blob.size(), info.record, names, decoded) && decoded.mainline()[3]->question
                          && decoded.mainline()[3]->questionText == line[3]->questionText && decoded.mainline()[3]->questionData == full && decoded.mainline()[4]->question
                          && decoded.mainline()[4]->questionData.empty() && !decoded.mainline()[2]->question;
        int replayed = 0;
        hy64db::replayMainline(blob.data(), blob.size(), [&](const Position&, int) { return ++replayed, true; });
        check(parsed && written && kept && replayed == 8,
              std::string("PGN et base : questions d'entraînement « [%tqu] » lues, réécrites, codées") + (parsed ? "" : " [lecture]") + (written ? "" : " [écriture : " + flat + "]") + (kept ? "" : " [gardé]")
                  + (replayed == 8 ? "" : " [rejeu]"));
    }
    // Merge of games: common trunk shared, deviations as variations, comments and marker taken over
    {
        Game trunk, other, third, elsewhere;
        pgn::parseGame("[White \"A\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 *\n", trunk);
        pgn::parseGame("[White \"B\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 {italienne} Bc5 $1 4. c3 *\n", other);
        pgn::parseGame("[White \"C\"]\n\n1. e4 c5 {sicilienne} 2. Nf3 (2. c3 d5) 2... d6 *\n", third);
        pgn::parseGame("[FEN \"8/8/8/8/8/5k2/8/5K1R w - - 0 1\"]\n[SetUp \"1\"]\n\n1. Rh3+ *\n", elsewhere);
        const int a = trunk.merge(other, "B – X, Paris 1900");
        const int b = trunk.merge(third, "C – Y");
        const int again = trunk.merge(other, "B – X, Paris 1900"); // already merged: nothing more
        const int refused = trunk.merge(elsewhere);
        const std::string out = pgn::writeGame(trunk);
        Game reread;
        check(a == 3 && b == 5 && again == 0 && refused == -1 && trunk.mainline().size() == 6 && pgn::parseGame(out, reread) && pgn::writeGame(reread) == out
                  && out.find("3. Bb5 (3. Bc4 {italienne} Bc5 $1 4. c3 {B – X, Paris 1900}) 3... a6") != std::string::npos
                  && out.find("e5 (1... c5 {sicilienne} 2. Nf3 (2. c3 d5) 2... d6 {C – Y})") != std::string::npos,
              "fusion de parties : " + std::to_string(a) + " + " + std::to_string(b) + " coups ajoutés en variantes, refus d'une autre position de départ");
    }
    // Repertoire: lines added, then report of what games add to it
    {
        Game rep, line, full;
        pgn::parseGame("\n1. e4 e5 2. Nf3 Nc6 3. Bb5 {espagnole} a6 4. Ba4 *\n", full);
        const int first = repertoire::addLine(rep, full);
        pgn::parseGame("\n1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 *\n", line);
        const int second = repertoire::addLine(rep, line, line.mainline()[4]); // up to 3. d4 only
        repertoire::Report report(rep, White);
        auto play = [&](const char* moves, const char* result, int id) {
            Game g;
            pgn::parseGame(std::string("[Result \"") + result + "\"]\n\n" + moves + " " + result + "\n", g);
            return report.add(g, id);
        };
        play("1. e4 e6 2. d4 d5", "1-0", 1);                       // French: unforeseen opponent move
        play("1. e4 e6 2. d4", "0-1", 2);
        play("1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4", "1/2-1/2", 3); // the repertoire stops after 3. d4
        play("1. e4 e5 2. Bc4", "1-0", 4);                         // I played something other than 2. Nf3
        play("1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4", "1-0", 5);    // entirely in the repertoire
        const auto gaps = report.gaps();
        const bool ok = first == 7 && second == 4 && rep.mainline().size() == 7 && report.gamesSeen() == 5 && report.gamesInside() == 1 && gaps.size() == 3
                        && gaps[0].kind == repertoire::Gap::Uncovered && gaps[0].line == "1. e4" && gaps[0].move == "1... e6" && gaps[0].games == 2 && gaps[0].whiteWins == 1
                        && gaps[0].blackWins == 1 && gaps[0].known == "e5, c5" && gaps[0].ids == std::vector<int>{1, 2};
        bool ends = false, deviates = false;
        for (const auto& g : gaps) {
            ends = ends || (g.kind == repertoire::Gap::LineEnds && g.line == "1. e4 c5 2. Nf3 d6 3. d4" && g.move == "3... cxd4" && g.known.empty());
            deviates = deviates || (g.kind == repertoire::Gap::Deviation && g.line == "1. e4 e5" && g.move == "2. Bc4" && g.known == "Nf3");
        }
        check(ok && ends && deviates, "répertoire : lignes ajoutées (" + std::to_string(first) + " + " + std::to_string(second) + " coups), écarts relevés (" + std::to_string(gaps.size()) + ")");
    }
    // Reading for encoding: same encoded block as the normal reading
    const std::string text = "[Event \"D\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 (5... b5 6. Bb3 $1 {idée}) 6. Re1 1-0\n";
    Game full, bare, binary;
    pgn::ParseOptions noSan;
    noSan.withSan = false;
    const bool parsed = pgn::parseGame(text, full) && pgn::parseGame(text, bare, nullptr, noSan);
    std::vector<uint8_t> b1, b2, b3;
    hy64db::GameInfo i1, i2, i3;
    const bool same = parsed && hy64db::encodeGame(full, b1, i1) && hy64db::encodeGame(bare, b2, i2) && b1 == b2 && bare.mainline()[0]->san.empty()
                      && pgn::writeGame(bare) == pgn::writeGame(full);
    // Main line in binary (castling = the king onto its rook's square: e1h1)
    auto word = [](const char* uci) { return uint16_t(parseSquare(std::string(uci, 2)) | parseSquare(std::string(uci + 2, 2)) << 6); };
    const uint16_t words[] = {word("e2e4"), word("e7e5"), word("g1f3"), word("b8c6"), word("f1b5"), word("a7a6"), word("b5a4"), word("g8f6"), word("e1h1"), word("f8e7"), word("f1e1")};
    pgn::ParseOptions bin = noSan;
    bin.hasMainline = true;
    bin.mainline = words;
    bin.mainlineSize = std::size(words);
    const std::string plain = "[Event \"D\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 1-0\n";
    Game plainGame;
    const bool binOk = pgn::parseGame(plain, plainGame) && pgn::parseGame(plain, binary, nullptr, bin) && hy64db::encodeGame(plainGame, b1, i1)
                       && hy64db::encodeGame(binary, b3, i3) && b1 == b3;
    const uint16_t bad[] = {word("e2e5")};
    bin.mainline = bad;
    bin.mainlineSize = 1;
    Game rejected;
    check(same && binOk && !pgn::parseGame(plain, rejected, nullptr, bin), "PGN : lecture pour codage (sans notation, coups en binaire) identique à la lecture normale");
    // Irregular moves that ChessBase keeps: king moving into check (Dereviagin – Strenzwilk, US op
    // 1993: 39...Kg6 under fire from the h5 pawn), pawn capture onto an empty square in a
    // transposition note. Read as a last resort, rewritten as they are, encoded in the database; an
    // impossible move (jumping pawn) is still refused.
    {
        Game king, pawn, decoded, wrong;
        const bool readKing = pgn::parseGame("[FEN \"8/6k1/1p3P2/4B1PP/p3N3/2b1K3/8/1r6 b - - 0 39\"]\n\n39... Kg6 40. h6 Rb2 1-0", king) && king.mainline().size() == 3
                              && king.mainline()[0]->san == "Kg6";
        const bool readPawn = pgn::parseGame("1. d4 d5 2. c4 e6 3. c5 b5 4. Nf3 Nf6 (4... a6 5. cxb6 {note}) 5. cxb6 *", pawn) && pawn.mainline().size() == 9;
        const Node* note = readPawn ? pawn.mainline()[6]->children[1]->mainChild() : nullptr; // 5.cxb6 after 4...a6: no en passant any more
        std::string flat = readPawn ? pgn::writeGame(pawn) : std::string();
        std::replace(flat.begin(), flat.end(), '\n', ' ');
        std::vector<uint8_t> blob;
        hy64db::GameInfo info;
        hy64db::Names names;
        const bool kept = note && note->san == "cxb6" && note->pos.at(parseSquare("b6")) == makePiece(White, Pawn) && note->pos.at(parseSquare("b5")) == makePiece(Black, Pawn)
                          && flat.find("(4... a6 5. cxb6 {note})") != std::string::npos && hy64db::encodeGame(pawn, blob, info)
                          && hy64db::decodeGame(blob.data(), blob.size(), info.record, names, decoded) && pgn::writeGame(decoded).find("5. cxb6 {note}") != std::string::npos;
        check(readKing && readPawn && kept && !pgn::parseGame("1. e5 *", wrong) && !pgn::parseGame("1. e4 e5 2. Ke3 *", wrong),
              std::string("PGN et base : coups irréguliers de ChessBase gardés (roi en échec, prise de pion sur case vide)") + (readKing ? "" : " [roi]") + (readPawn ? "" : " [pion]")
                  + (kept ? "" : " [gardé : " + flat + "]"));
    }
}

// ChessBase players encyclopedia (real folder: HYDRA64_PLAYBASE).
static void testPlayerBase()
{
    check(hy64db::PlayerBase::foldName("Vachier-Lagrave") == "vachier lagrave" && hy64db::PlayerBase::foldName("  Ståhlberg ") == "stahlberg"
              && hy64db::PlayerBase::foldName("O'Kelly de Galway") == "o kelly de galway" && hy64db::PlayerBase::nationCode(108) == "NOR" && hy64db::PlayerBase::nationCode(0).empty(),
          "encyclopédie des joueurs : noms repliés, codes de fédération");
    const char* dir = std::getenv("HYDRA64_PLAYBASE");
    if (!dir) {
        std::printf("[SKIP] encyclopédie des joueurs réelle : définir HYDRA64_PLAYBASE pour la tester\n");
        return;
    }
    hy64db::PlayerBase pb;
    std::string err;
    if (!pb.open(dir, &err)) {
        check(false, "encyclopédie des joueurs : ouverture — " + err);
        return;
    }
    const auto carlsen = pb.find("Carlsen, Magnus");
    const bool found = !carlsen.empty() && carlsen[0].title == "GM" && carlsen[0].nation == "NOR" && carlsen[0].birth.year == 1990 && carlsen[0].reigningChampion;
    const auto ratings = found ? pb.ratings(carlsen[0].id) : std::vector<hy64db::PlayerBase::Rating>();
    int peak = 0, peakYear = 0;
    for (const auto& r : ratings)
        if (r.elo > peak) peak = r.elo, peakYear = r.date.year;
    const auto photos = found ? pb.photos(carlsen[0]) : std::vector<hy64db::PlayerBase::Photo>();
    size_t present = 0;
    for (const auto& ph : photos) present += !ph.path.empty();
    check(found && peak == 2882 && peakYear == 2014 && ratings.size() > 80 && ratings.front().date.year == 2001 && ratings.back().rank == 1 && photos.size() > 40 && present == photos.size(),
          "encyclopédie des joueurs : Carlsen — " + std::to_string(pb.count()) + " fiches, " + std::to_string(ratings.size()) + " notes, maximum " + std::to_string(peak) + " en "
              + std::to_string(peakYear) + ", " + std::to_string(photos.size()) + " photos");
    const auto fischer = pb.find("Fischer, Robert James");
    const auto initial = pb.find("Kasparov, G");
    const auto polgar = pb.find("polgar, judit");
    check(!fischer.empty() && fischer[0].birth.year == 1943 && fischer[0].birth.month == 3 && fischer[0].birth.day == 9 && fischer[0].death.year == 2008 && fischer[0].worldChampion
              && fischer[0].nation == "USA" && !initial.empty() && initial[0].first == "Garry" && initial[0].formerNation == "URS" && !polgar.empty() && polgar[0].womenTitle == "WGM"
              && !polgar[0].male && pb.find("Zzzz, Personne").empty() && !pb.find("Anderssen").empty() && pb.find("Anderssen")[0].first == "Adolf"
              && pb.find("Anderssen")[0].birth.year == 1818 && pb.photos(pb.find("Anderssen")[0]).size() == 4 && !pb.find("Adolf Anderssen").empty()
              && pb.find("Adolf Anderssen")[0].birth.year == 1818 && !pb.find("Van Wely").empty() && pb.find("Van Wely")[0].first == "Loek"
              && !pb.find("Kasparov Garry (RUS)").empty() && pb.find("Kasparov Garry (RUS)")[0].first == "Garry" && pb.find("Kasparov Garry (RUS)")[0].title == "GM"
              && !pb.find("Anand Viswanathan (IND)").empty() && pb.find("Anand Viswanathan (IND)")[0].nation == "IND" && !pb.find("Van Wely Loek").empty()
              && pb.find("Van Wely Loek")[0].last == "Van Wely" && !pb.find("Magnus Carlsen").empty() && pb.find("Magnus Carlsen")[0].nation == "NOR",
          "encyclopédie des joueurs : Fischer (dates, champion du monde), Kasparov par son initiale, Polgar, Anderssen sans prénom, formes « Nom Prénom (FED) » et « Prénom Nom »");
}

int main(int argc, char** argv)
{
    testPlayerBase();
    testPgnRobustness();
    testTargetedSan();
    testChess960();
    struct Case { const char* fen; int depth; uint64_t nodes; };
    const Case cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
    };
    for (const Case& c : cases) {
        Position p;
        p.setFen(c.fen);
        uint64_t n = perft(p, c.depth);
        check(n == c.nodes, std::string("perft ") + std::to_string(c.depth) + " = " + std::to_string(n) + "  " + c.fen);
    }

    // PGN: reading of the sample database, then write/read round trip.
    const char* path = argc > 1 ? argv[1] : "tests/data/classics.pgn";
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string data = ss.str();
    auto entries = pgn::indexDatabase(data);
    check(entries.size() == 3, "index PGN : " + std::to_string(entries.size()) + " parties");

    for (const auto& e : entries) {
        Game g;
        std::string err;
        bool ok = pgn::parseGame(std::string_view(data).substr(e.offset, e.length), g, &err);
        g.toEnd();
        check(ok && g.current()->pos.isCheckmate(),
              "lecture " + e.white + " – " + e.black + (err.empty() ? "" : " (" + err + ")"));

        std::string out = pgn::writeGame(g);
        Game g2;
        bool ok2 = pgn::parseGame(out, g2, &err);
        check(ok2 && pgn::writeGame(g2) == out, "aller-retour PGN " + e.white);
    }

    // Variation repeating the move it replaces (ChessBase exports): it stays a
    // variation, the main line is not extended.
    {
        Game g;
        std::string err;
        const bool ok = pgn::parseGame("1. e4 e5 2. Nf3 Nc6 (2... Nc6 3. Bb5 a6 4. Ba4) 3. Bc4 *", g, &err);
        const auto main = g.mainline();
        check(ok && main.size() == 5 && main.back()->san == "Bc4", "variante identique au coup remplacé : ligne principale intacte");
        check(main[3]->parent->children.size() == 2 && main[3]->parent->children[1]->mainChild()
                  && main[3]->parent->children[1]->mainChild()->san == "Bb5",
              "variante identique au coup remplacé : gardée comme variante");
        Game g2;
        const std::string out = pgn::writeGame(g);
        check(pgn::parseGame(out, g2, &err) && pgn::writeGame(g2) == out && g2.mainline().size() == 5,
              "variante identique au coup remplacé : aller-retour PGN");
        Game g3; // at the end of the game, as in the Mega Database (Cramling – Kortschnoj 1996)
        check(pgn::parseGame("1. e4 e5 2. Nf3 (2. Nf3 Nc6 3. Bb5) *", g3, &err) && g3.mainline().size() == 3,
              "variante identique au dernier coup : la partie n'est pas prolongée");
    }

    // Null moves ("--", "Z0"): the turn passes, in the main line as in a variation
    {
        Game g;
        std::string err;
        const bool ok = pgn::parseGame("1. e4 -- 2. d4 e5 (2... -- 3. c4) 3. dxe5 Z0 4. Nf3 *", g, &err);
        const auto main = g.mainline();
        check(ok && main.size() == 7 && main[1]->san == "--" && main[5]->san == "--" && main.back()->san == "Nf3",
              "coups nuls : partie lue en entier (" + err + ")");
        check(main[1]->pos.sideToMove() == White && main[1]->pos.fullmoveNumber() == 2, "coup nul : le trait passe, numéro de coup suivant");
        const std::string out = pgn::writeGame(g);
        Game g2;
        check(out.find("1. e4 -- 2. d4 e5 (2... -- 3. c4) 3. dxe5 -- 4. Nf3") != std::string::npos && pgn::parseGame(out, g2, &err)
                  && pgn::writeGame(g2) == out,
              "coups nuls : écriture et aller-retour PGN");
    }

    // ---- Hydra64 database codec (src/db): exact round trip
    {
        // Classic games
        bool all = true;
        std::string why;
        for (const auto& e : entries) {
            Game g;
            pgn::parseGame(std::string_view(data).substr(e.offset, e.length), g);
            if (!codecRoundTrip(g, &why)) all = false;
        }
        check(all, "codec : parties classiques identiques après codage" + (all ? std::string() : " (" + why + ")"));

        // Edge cases: nested variations and ones identical to the replaced move, null moves, all the
        // promotions (advancing, capturing on both sides), castling, en passant, UTF-8
        // comments, NAGs, clocks, unusual headers, partial dates.
        const char* tricky[] = {
            "[Event \"Tournoi d'été\"]\n[Site \"Zürich\"]\n[Date \"1996.??.??\"]\n[Round \"7.2\"]\n[White \"Ünzicker, Wolfgang\"]\n"
            "[Black \"Kortschnoj, Viktor\"]\n[Result \"1-0\"]\n[ECO \"C42\"]\n[WhiteElo \"2525\"]\n[BlackElo \"?\"]\n[Annotator \"Nunn\"]\n"
            "[TimeControl \"40/7200\"]\n[PlyCount \"9\"]\n\n"
            "{Commentaire initial — « test »} 1. e4 $1 {[%clk 1:59:58] bon} e5 (1... c5 2. Nf3 (2. Nc3 Nc6 (2... d6) 3. g3) 2... d6) "
            "(1... e5 2. Nc3) 2. Nf3 Nf6 $2 $14 3. Nxe5 -- 4. d4 {coup nul ci-dessus} d6 5. Nf3 1-0\n",
            "[Event \"?\"]\n[Site \"?\"]\n[Date \"????.??.??\"]\n[Round \"?\"]\n[White \"?\"]\n[Black \"?\"]\n[Result \"*\"]\n"
            "[SetUp \"1\"]\n[FEN \"r3k2r/1P6/8/3pP3/8/8/6p1/R3K2R w KQkq d6 0 1\"]\n\n"
            "1. exd6 (1. b8=Q+ Rxb8) (1. bxa8=R+ Ke7) 1... O-O 2. O-O-O gxh1=Q 3. bxa8=N Qxd1+ 4. Kxd1 *\n",
            "[Event \"Q\"]\n[Site \"?\"]\n[Date \"2020.02.30\"]\n[Round \"?\"]\n[White \"A\"]\n[Black \"B\"]\n[Result \"1/2-1/2\"]\n[ECO \"B90a\"]\n\n"
            "1. d4 e5 2. Qd3 Qg5 3. Qb5 Qxg2 4. Qxb7 Qxh1 5. Qxa8 Qxg1 6. Qxb8 Qxf1+ 7. Kd2 Qxf2 1/2-1/2\n",
        };
        for (const char* t : tricky) {
            Game g;
            std::string err;
            const bool parsed = pgn::parseGame(t, g, &err);
            check(parsed && codecRoundTrip(g, &why), "codec : cas limite identique après codage" + (parsed ? why.empty() ? std::string() : " (" + why + ")" : " (PGN illisible : " + err + ")"));
            why.clear();
        }

        // Random games: random legal moves, variations, promotions, null moves
        std::mt19937 rng(64);
        int bad = 0, total = 0, plies = 0;
        for (int gi = 0; gi < 3000; ++gi) {
            Game g;
            g.setHeader("Event", "Aléatoire " + std::to_string(gi));
            std::vector<Node*> frontier{g.root()};
            const int len = 20 + int(rng() % 180);
            Node* cur = g.root();
            for (int i = 0; i < len; ++i) {
                auto moves = cur->pos.legalMoves();
                if (moves.empty()) break;
                // prefers promotions and captures to cover the rare cases
                Move m = moves[rng() % moves.size()];
                for (const Move& c : moves) {
                    const Piece moving = cur->pos.at(c.from);
                    const bool castle = typeOf(moving) == King && std::abs(fileOf(c.to) - fileOf(c.from)) == 2;
                    const bool enPassant = typeOf(moving) == Pawn && c.to == cur->pos.epSquare();
                    if ((c.promo != NoType || castle || enPassant) && rng() % 2) m = c;
                }
                if (rng() % 97 == 0 && !cur->pos.inCheck()) m = Move(); // null move
                Node* n = g.addMove(cur, m);
                if (rng() % 7 == 0) n->comment = "c" + std::to_string(i);
                if (rng() % 11 == 0) n->nags.push_back(int(rng() % 20));
                if (rng() % 5 == 0) n->clockMs = int(rng() % 7200000);
                if (rng() % 9 == 0 && moves.size() > 1) { // a variation of a few moves
                    Node* v = g.addMove(cur, moves[(rng() % (moves.size() - 1) + 1 + 0) % moves.size()], true);
                    for (int k = 0; k < 4; ++k) {
                        auto vm = v->pos.legalMoves();
                        if (vm.empty()) break;
                        v = g.addMove(v, vm[rng() % vm.size()]);
                    }
                }
                cur = n;
            }
            ++total;
            plies += int(g.mainline().size());
            if (!codecRoundTrip(g, &why)) {
                if (bad++ < 3) std::printf("       partie aléatoire %d : %s\n", gi, why.c_str());
            }
        }
        check(bad == 0, "codec : " + std::to_string(total) + " parties aléatoires (" + std::to_string(plies)
                            + " demi-coups, variantes, promotions, coups nuls) identiques après codage");

        // Fast replay of the main line and signatures (never a wrong exclusion)
        int replayBad = 0, sigBad = 0;
        for (const auto& e : entries) {
            Game g;
            pgn::parseGame(std::string_view(data).substr(e.offset, e.length), g);
            std::vector<uint8_t> blob;
            hy64db::GameInfo info;
            hy64db::encodeGame(g, blob, info);
            std::vector<uint64_t> keys;
            hy64db::replayMainline(blob.data(), blob.size(), [&](const Position& p, int) {
                keys.push_back(polyglot::key(p));
                return true;
            });
            std::vector<uint64_t> expect{polyglot::key(g.startPosition())};
            for (const Node* n : g.mainline()) expect.push_back(polyglot::key(n->pos));
            if (keys != expect) ++replayBad;
            for (const Node* n : g.mainline())
                if (!hy64db::PawnSignature::compatible(info.record.pawns, hy64db::PawnSignature::missing(n->pos))) ++sigBad;
        }
        check(replayBad == 0, "codec : rejeu de la ligne principale (sans variantes) = mêmes positions");
        check(sigBad == 0, "codec : signature des pions compatible avec chaque position de la partie");
    }

    // ---- Hydra64 database: conversion, reading, writing, search, sort, positions
    {
        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / ("hy64db-test-" + std::to_string(std::rand()))).string();
        fs::create_directories(dir);
        // Source: the classic games, plus 2,000 random games (for volume and positions)
        std::string pgnText = data;
        std::mt19937 rng(7);
        for (int gi = 0; gi < 2000; ++gi) {
            Game g;
            g.setHeader("Event", gi % 3 ? "Open de Zürich" : "Mémorial Capablanca");
            g.setHeader("White", gi % 5 ? "Kasparov, Garry" : "Ünzicker, Wolfgang");
            g.setHeader("Black", "Joueur " + std::to_string(gi % 97));
            g.setHeader("WhiteElo", std::to_string(2000 + int(rng() % 850)));
            g.setHeader("Date", std::to_string(1950 + int(rng() % 70)) + ".??.??");
            const char* results[] = {"1-0", "0-1", "1/2-1/2", "*"};
            g.setHeader("Result", results[rng() % 4]);
            Node* cur = g.root();
            const int len = 10 + int(rng() % 60);
            for (int i = 0; i < len; ++i) {
                auto moves = cur->pos.legalMoves();
                if (moves.empty()) break;
                // little variety in the openings: positions shared by many games
                cur = g.addMove(cur, moves[i < 6 ? rng() % std::min<size_t>(3, moves.size()) : rng() % moves.size()]);
            }
            pgnText += "\n" + pgn::writeGame(g);
        }
        const std::string pgnPath = dir + "/source.pgn";
        { std::ofstream(pgnPath, std::ios::binary) << pgnText; }

        hy64db::PgnSource src;
        std::string err;
        hy64db::ConvertResult cr;
        hy64db::ConvertOptions opt;
        opt.positionDepth = 12;
        const bool converted = src.open(pgnPath, &err) && hy64db::convert(src, dir + "/base", opt, &cr, &err);
        check(converted && cr.games == src.count() && cr.failed == 0, "base : conversion PGN → Hydra64 (" + std::to_string(cr.games) + " parties" + (err.empty() ? "" : ", " + err) + ")");

        hy64db::Database db;
        check(db.open(dir + "/base.hy64i", &err) && db.count() == src.count() && db.positions(), "base : ouverture, nombre de parties, index des positions");
        int same = 0;
        for (uint32_t id = 0; id < db.count(); ++id) {
            Game a, b;
            src.read(id, a, nullptr);
            if (db.game(id, b) && pgn::writeGame(a) == pgn::writeGame(b)) ++same;
        }
        check(same == int(db.count()), "base : chaque partie relue est identique à sa source (" + std::to_string(same) + "/" + std::to_string(db.count()) + ")");

        // Single-file database (.hy64): packed, opened as it is, same games and same
        // position search; unpacked again, files identical to the originals
        {
            std::string perr;
            const bool packed = hy64db::packDatabase(dir + "/base.hy64i", dir + "/base-un.hy64", &perr);
            hy64db::Database one;
            const bool opened = packed && one.open(dir + "/base-un.hy64", &perr) && one.count() == db.count() && one.positions() && one.isPacked() && !db.isPacked();
            int identical = 0;
            for (uint32_t id = 0; opened && id < db.count(); ++id) {
                Game a, b;
                if (db.game(id, a) && one.game(id, b) && pgn::writeGame(a) == pgn::writeGame(b)) ++identical;
            }
            const Position after = Position::startPos().after(Position::startPos().parseSan("e4"));
            const bool sameSearch = opened && hy64db::searchPosition(one, after) == hy64db::searchPosition(db, after) && !hy64db::searchPosition(db, after).empty();
            one.close();
            const bool unpacked = hy64db::unpackDatabase(dir + "/base-un.hy64", dir + "/base-deux.hy64i", &perr);
            auto bytes = [](const std::string& path) {
                std::ifstream f(path, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            };
            bool sameFiles = unpacked;
            for (const char* ext : {".hy64i", ".hy64n", ".hy64g", ".hy64p"}) sameFiles = sameFiles && bytes(dir + "/base" + ext) == bytes(dir + "/base-deux" + ext) && !bytes(dir + "/base" + ext).empty();
            std::string bad;
            hy64db::Database notOne;
            const bool rejects = !notOne.open(pgnPath.substr(0, pgnPath.size() - 4) + ".hy64", &bad) && !hy64db::unpackDatabase(pgnPath, dir + "/non.hy64i", &bad);
            check(opened && identical == int(db.count()) && sameSearch && sameFiles && rejects,
                  "base en un seul fichier : réunie, ouverte, lue (" + std::to_string(identical) + " parties), recherche de position, séparée à l'identique" + (perr.empty() ? "" : " — " + perr));

            // Editable: the same operations on the single-file database and on an ordinary database
            // give the same games, including after closing, reopening and unpacking.
            Game added;
            pgn::parseGame("[Event \"Journal\"]\n[White \"Nouveau, Joueur\"]\n[Black \"Autre, Joueur\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 {ajoutée} Nc6 1-0\n", added);
            Game changed;
            pgn::parseGame("[Event \"Remplacée\"]\n[White \"Remplaçant, A\"]\n[Black \"B\"]\n[Result \"1/2-1/2\"]\n\n1. d4 d5 (1... Nf6) 2. c4 1/2-1/2\n", changed);
            auto operate = [&](hy64db::Database& d, std::string* e) {
                uint32_t id1 = 0, id2 = 0;
                return d.append(added, &id1, e) && d.append(changed, &id2, e) && id2 == id1 + 1 && d.replace(3, changed, e) && d.replace(id1, changed, e) && d.setDeleted(5, true, e)
                       && d.setDeleted(id2, true, e) && d.setUserFlags(7, 0x21, e) && d.setUserFlags(id1, 0x05, e) && d.renameName(hy64db::Player, d.names().find(hy64db::Player, "Remplaçant, A"), "Renommé, A", e) >= 1;
            };
            auto snapshot = [](const hy64db::Database& d) {
                std::string out;
                for (uint32_t id = 0; id < d.count(); ++id) {
                    Game g;
                    d.game(id, g);
                    const hy64db::Record r = d.record(id);
                    out += std::to_string(id) + ":" + std::to_string(r.flags) + ":" + pgn::writeGame(g);
                }
                return out;
            };
            std::string e1, e2;
            hy64db::Database plain, single;
            const bool both = plain.open(dir + "/base-deux.hy64i", &e1) && single.open(dir + "/base-un.hy64", &e2) && operate(plain, &e1) && operate(single, &e2);
            const std::string expected = both ? snapshot(plain) : std::string();
            const bool live = both && snapshot(single) == expected && single.count() == db.count() + 2;
            single.close();
            const bool reopened = live && single.open(dir + "/base-un.hy64", &e2) && snapshot(single) == expected && single.isPacked();
            // one more addition after reopening, on both sides
            uint32_t extraId = 0;
            const bool again = reopened && plain.append(added, &extraId, &e1) && single.append(added, &extraId, &e2) && snapshot(single) == snapshot(plain);
            const std::string expected2 = again ? snapshot(plain) : std::string();
            single.close();
            hy64db::Database merged;
            const bool split = again && hy64db::unpackDatabase(dir + "/base-un.hy64", dir + "/base-trois.hy64i", &e2) && merged.open(dir + "/base-trois.hy64i", &e2) && snapshot(merged) == expected2;
            merged.close();
            // Journal tidying: same games, deleted games kept, nothing left in the journal
            hy64db::Database tidy;
            const bool hadJournal = split && tidy.open(dir + "/base-un.hy64", &e2) && tidy.journalBytes() > 0 && tidy.journalGames() == 3;
            const bool tidied = hadJournal && tidy.tidy(&e2) && tidy.isPacked() && tidy.journalBytes() == 0 && tidy.journalGames() == 0 && snapshot(tidy) == expected2;
            tidy.close();
            check(tidied, "base en un seul fichier : journal rangé (3 ajouts), parties et suppressions conservées" + (e2.empty() ? "" : " — " + e2));
            // Thousands of additions in the journal: reread after closing, then tidied identically
            {
                std::string e3;
                hy64db::Database many;
                const bool opened = hy64db::packDatabase(dir + "/base-deux.hy64i", dir + "/base-mille.hy64", &e3) && many.open(dir + "/base-mille.hy64", &e3);
                const uint32_t before = opened ? many.count() : 0;
                bool appended = opened;
                const int Added = 6000;
                for (int k = 0; appended && k < Added; ++k) {
                    Game g;
                    db.game(uint32_t(k) % db.count(), g);
                    g.setHeader("Round", std::to_string(k)); // new names in the names journal
                    g.setHeader("White", "Ajouté " + std::to_string(k % 700));
                    uint32_t id = 0;
                    appended = many.append(g, &id, &e3) && id == before + uint32_t(k);
                    if (k % 1000 == 999) many.sync();
                }
                const uint64_t journal = appended ? many.journalBytes() : 0;
                auto digest = [](const hy64db::Database& d, uint32_t from) {
                    uint64_t h = 1469598103934665603ull;
                    for (uint32_t id = from; id < d.count(); ++id) {
                        Game g;
                        d.game(id, g);
                        for (unsigned char ch : pgn::writeGame(g)) h = (h ^ ch) * 1099511628211ull;
                    }
                    return h;
                };
                const uint64_t live = appended ? digest(many, before) : 0;
                many.close();
                const bool reopened = appended && many.open(dir + "/base-mille.hy64", &e3) && many.count() == before + Added && many.journalGames() == Added && digest(many, before) == live;
                const bool tidied = reopened && many.tidy(&e3) && many.journalBytes() == 0 && many.count() == before + Added && digest(many, before) == live;
                many.close();
                const bool afterTidy = tidied && many.open(dir + "/base-mille.hy64", &e3) && digest(many, before) == live && many.names().find(hy64db::Player, "Ajouté 699") != hy64db::NoName;
                many.close();
                check(afterTidy, "base en un seul fichier : " + std::to_string(Added) + " ajouts (journal de " + std::to_string(journal / 1024) + " Ko), relus après réouverture, rangés à l'identique"
                                     + (e3.empty() ? "" : " — " + e3));
            }
            // Position index rebuilt, then compaction: the journal is tidied, the deleted games removed
            const bool reindexed = split && single.open(dir + "/base-un.hy64", &e2) && hy64db::buildPositionIndex(single, 12, &e2) && single.isPacked() && single.reloadPositions()
                                   && single.positions() && snapshot(single) == expected2 && !hy64db::searchPosition(single, after).empty();
            const uint32_t beforeCompact = single.count();
            const bool compacted = reindexed && single.compact(&e2) && single.isPacked() && single.count() == beforeCompact - 2 && plain.compact(&e1) && snapshot(single) == snapshot(plain);
            const bool noLeftovers = !std::filesystem::exists(dir + "/base-un.hy64.work.hy64i") && !std::filesystem::exists(dir + "/base-un.hy64.part");
            check(both && live && reopened && again && split && reindexed && compacted && noLeftovers,
                  "base en un seul fichier : modifiable (ajout, remplacement, suppression, drapeaux, nom corrigé), rouverte, séparée, réindexée, compactée" + (e1.empty() ? "" : " — " + e1)
                      + (e2.empty() ? "" : " — " + e2) + " [" + std::to_string(both) + std::to_string(live) + std::to_string(reopened) + std::to_string(again) + std::to_string(split) + std::to_string(reindexed) + std::to_string(compacted) + std::to_string(noLeftovers) + "]");
        }

        // Browsable index: players and tournaments of the database, counted; games of an entry
        {
            const auto players = hy64db::nameIndex(db, hy64db::Player);
            const auto events = hy64db::nameIndex(db, hy64db::Event);
            uint64_t playerGames = 0, eventGames = 0;
            bool ordered = true, scores = true;
            for (size_t i = 0; i < players.size(); ++i) {
                playerGames += players[i].games;
                scores = scores && players[i].wins + players[i].draws + players[i].losses <= players[i].games && players[i].firstYear <= players[i].lastYear;
            }
            for (const auto& e : events) eventGames += e.games;
            const auto ranks = db.names().ranks(hy64db::Player);
            for (size_t i = 1; i < players.size(); ++i) ordered = ordered && (*ranks)[players[i - 1].id] < (*ranks)[players[i].id];
            const auto& busiest = *std::max_element(players.begin(), players.end(), [](const auto& a, const auto& b) { return a.games < b.games; });
            const auto theirs = hy64db::gamesOf(db, hy64db::Player, busiest.id);
            bool allTheirs = theirs.size() == busiest.games;
            for (uint32_t id : theirs) allTheirs = allTheirs && (db.record(id).white == busiest.id || db.record(id).black == busiest.id);
            check(!players.empty() && !events.empty() && playerGames <= 2 * uint64_t(db.count()) && playerGames >= db.count() && eventGames <= db.count() && ordered && scores && allTheirs,
                  "index : " + std::to_string(players.size()) + " joueurs, " + std::to_string(events.size()) + " tournois ; " + busiest.name + " : " + std::to_string(busiest.games) + " parties");
        }

        // Searches that replay the games: manoeuvre, pawn structure, material — compared with a
        // direct walk of the games
        {
            hy64db::ManeuverQuery q;
            q.piece = Knight, q.color = 0, q.from = parseSquare("f3"), q.to = parseSquare("f5"), q.maxMoves = 3;
            const auto fast = hy64db::searchManeuver(db, q);
            hy64db::ManeuverQuery viaQ = q;
            viaQ.via = parseSquare("h4");
            const auto withVia = hy64db::searchManeuver(db, viaQ);
            // reference: tracking of the knight move by move in the decoded games
            std::vector<uint32_t> slow, slowVia;
            Position target;
            uint32_t targetGame = 0;
            for (uint32_t id = 0; id < db.count(); ++id) {
                Game g;
                db.game(id, g);
                const auto line = g.mainline();
                bool hit = false, hitVia = false;
                for (size_t i = 0; i < line.size() && !(hit && hitVia); ++i) {
                    if (line[i]->move.from != q.from || line[i]->parent->pos.at(q.from) != makePiece(White, Knight)) continue;
                    Square at = line[i]->move.to;
                    bool via = at == viaQ.via;
                    int moves = 1;
                    if (at == q.to) hit = true;
                    for (size_t j = i + 1; j < line.size() && moves < q.maxMoves; ++j) {
                        if (line[j]->move.to == at && line[j]->move.from != at) break; // knight captured
                        if (line[j]->move.from != at || line[j]->parent->pos.sideToMove() != White) continue;
                        at = line[j]->move.to;
                        ++moves;
                        via = via || at == viaQ.via;
                        if (at == q.to) {
                            hit = true;
                            hitVia = hitVia || via;
                            break;
                        }
                    }
                }
                if (hit) slow.push_back(id);
                if (hitVia) slowVia.push_back(id);
                if (id == 7 && line.size() > 30) target = line[30]->pos, targetGame = id;
            }
            const auto structure = hy64db::searchPawnStructure(db, target);
            const auto material = hy64db::searchSameMaterial(db, target);
            const bool inStructure = std::find(structure.begin(), structure.end(), targetGame) != structure.end();
            const bool inMaterial = std::find(material.begin(), material.end(), targetGame) != material.end();
            hy64db::ManeuverTracker tracker(q);
            Game demo;
            pgn::parseGame("\n1. Nf3 d5 2. Nh4 e5 3. Nf5 *\n", demo);
            bool demoFound = tracker.feed(demo.startPosition());
            for (const Node* n : demo.mainline()) demoFound = tracker.feed(n->pos) || demoFound;
            check(fast == slow && withVia == slowVia && !fast.empty() && withVia.size() <= fast.size() && demoFound && inStructure && inMaterial && structure.size() <= material.size(),
                  "recherches par rejeu : manœuvre Cf3→f5 en 3 coups (" + std::to_string(fast.size()) + " parties, " + std::to_string(withVia.size()) + " par h4), structure de pions ("
                      + std::to_string(structure.size()) + "), même matériel (" + std::to_string(material.size()) + ")");
        }

        // Search by headers: Hydra64 database and PGN file, same answers
        {
            using hy64db::HeaderQuery;
            const std::string text = [&] {
                std::ifstream in(pgnPath);
                std::stringstream ss;
                ss << in.rdbuf();
                return ss.str();
            }();
            const auto entries = pgn::indexDatabase(text);
            auto both = [&](const HeaderQuery& q) {
                std::vector<uint32_t> pgnIds;
                for (uint32_t i = 0; i < entries.size(); ++i)
                    if (hy64db::matches(q, entries[i], std::string_view(text).substr(entries[i].offset, entries[i].length))) pgnIds.push_back(i);
                const std::vector<uint32_t> dbIds = db.searchHeaders(q);
                return pgnIds == dbIds ? dbIds : std::vector<uint32_t>{9999};
            };
            HeaderQuery anderssen;
            anderssen.white = "anders";
            HeaderQuery reversed;
            reversed.white = "kieser";
            reversed.black = "adolf";
            reversed.ignoreColors = true;
            HeaderQuery years;
            years.yearFrom = 1852;
            years.yearTo = 1860;
            HeaderQuery eco;
            eco.ecoFrom = hy64db::parseEco("C40");
            eco.ecoTo = hy64db::parseEco("C49");
            HeaderQuery draws;
            draws.results = HeaderQuery::DrawResult;
            HeaderQuery site;
            site.site = "londres";
            HeaderQuery longGames;
            longGames.movesMin = 22;
            const std::vector<uint32_t> within{0, 1};
            // Test database: the three classic games first, then generated games
            bool same = true;
            for (const HeaderQuery* q : {&anderssen, &reversed, &years, &eco, &draws, &site, &longGames}) same = same && both(*q) != std::vector<uint32_t>{9999};
            const auto y = both(years);
            const auto e = both(eco);
            check(same && both(anderssen) == std::vector<uint32_t>{1, 2} && both(reversed) == std::vector<uint32_t>{1} && y.size() >= 2 && y[0] == 0 &&
                      !e.empty() && e[0] == 0 && !both(draws).empty() && both(site) == std::vector<uint32_t>{1} &&
                      db.searchHeaders(anderssen, &within) == std::vector<uint32_t>{1} && hy64db::countMoves(text.substr(entries[0].offset, entries[0].length)) == 17,
                  "recherche par en-têtes : joueurs (couleurs ignorées), années, ECO, résultat, lieu, coups — PGN et Hydra64 identiques");
            (void)longGames;
            // Material and patterns: PGN and Hydra64 identical, and a few typical positions
            {
                using MQ = hy64db::MaterialQuery;
                HeaderQuery queens; // no queens left on the board, for 4 half-moves
                queens.material.maxCount[0][chess::Queen] = 0;
                queens.material.maxCount[1][chess::Queen] = 0;
                queens.material.minPlies = 4;
                HeaderQuery up; // White with at least 3 points more
                up.material.useDiff = true;
                up.material.diffMin = 3;
                up.material.diffMax = 99;
                HeaderQuery fianchetto;
                fianchetto.material.patterns = MQ::WhiteKingFianchetto;
                HeaderQuery pawns; // same pawn structure as the starting position after 1.e4 e5
                Position pe = Position::startPos();
                pe = pe.after(pe.parseSan("e4"));
                pe = pe.after(pe.parseSan("e5"));
                pawns.material.reference = MQ::Reference::SamePawns;
                pawns.material.refPos = pe;
                pawns.material.plyFrom = 6;
                const auto nq = both(queens), u = both(up), f = both(fianchetto), pw = both(pawns);
                const bool agree = nq != std::vector<uint32_t>{9999} && u != std::vector<uint32_t>{9999} && f != std::vector<uint32_t>{9999}
                                   && pw != std::vector<uint32_t>{9999};
                auto pos = [](const char* fen) {
                    Position p;
                    p.setFen(fen);
                    return p;
                };
                MQ iqp;
                iqp.patterns = MQ::WhiteIsolatedQueenPawn | MQ::OppositeCastling;
                MQ hang;
                hang.patterns = MQ::BlackHangingPawns;
                MQ maroczy;
                maroczy.patterns = MQ::WhiteMaroczy;
                MQ opp;
                opp.bishops = MQ::Bishops::OppositeColor;
                MQ files;
                files.reference = MQ::Reference::SamePawnFiles;
                files.refPos = pos("4k3/pp6/8/8/8/8/1P6/P3K3 w - - 0 1");
                MQ passed;
                passed.patterns = MQ::WhitePassedPawn;
                MQ sac;
                sac.patterns = MQ::RookSacC3;
                const Position before = pos("2r3k1/8/8/8/8/2N5/8/2K5 b - - 0 1");
                const Position after = before.after(before.parseSan("Rxc3+"));
                const std::vector<bool> tv = {MaterialMatcher::positionMatches(iqp, pos("2kr4/ppp5/8/8/3P4/8/PP3PPP/6K1 w - - 0 1"), nullptr)
                                   , !MaterialMatcher::positionMatches(iqp, pos("2kr4/ppp5/8/8/3P4/8/PPP2PPP/6K1 w - - 0 1"), nullptr)
                                   , MaterialMatcher::positionMatches(hang, pos("4k3/p4ppp/8/2pp4/8/8/8/4K3 w - - 0 1"), nullptr)
                                   , MaterialMatcher::positionMatches(maroczy, pos("4k3/8/3p4/8/2P1P3/8/8/4K3 w - - 0 1"), nullptr)
                                   , MaterialMatcher::positionMatches(opp, pos("4k1b1/8/8/8/8/8/8/2B1K3 w - - 0 1"), nullptr)
                                   , !MaterialMatcher::positionMatches(opp, pos("4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1"), nullptr)
                                   , MaterialMatcher::positionMatches(files, pos("4k3/1p6/p7/8/8/8/P7/1P2K3 w - - 0 1"), nullptr)
                                   , !MaterialMatcher::positionMatches(files, pos("4k3/2p5/p7/8/8/8/P7/1P2K3 w - - 0 1"), nullptr)
                                   , MaterialMatcher::positionMatches(passed, pos("4k3/p7/8/8/8/8/7P/4K3 w - - 0 1"), nullptr)
                                   , !MaterialMatcher::positionMatches(passed, pos("4k3/6p1/8/8/8/8/7P/4K3 w - - 0 1"), nullptr)
                                   , MaterialMatcher::positionMatches(sac, after, &before) && !MaterialMatcher::positionMatches(sac, before, nullptr)};
                bool types = true;
                std::string tvs;
                for (bool b : tv) types = types && b, tvs += b ? '1' : '0';
                check(agree && types && !nq.empty() && !pw.empty(),
                      "recherche par matériel et motifs : PGN et Hydra64 identiques (" + std::to_string(nq.size()) + " sans dames, " + std::to_string(u.size()) +
                          " Blancs +3, " + std::to_string(f.size()) + " fianchettos, " + std::to_string(pw.size()) + " mêmes pions) ; motifs sur des positions types " + tvs);
            }
            std::vector<uint32_t> byMoves = db.allGames();
            db.sort(byMoves, hy64db::ColMoves, true);
            bool ordered = true;
            for (size_t i = 1; i < byMoves.size(); ++i) ordered = ordered && db.record(byMoves[i - 1]).plies <= db.record(byMoves[i]).plies;
            check(ordered && byMoves.size() == db.count(), "tri par nombre de coups (colonne Coups)");
        }

        // Duplicates and name correction (small separate database)
        {
            const std::string twinPgn = dir + "/doublons.pgn";
            {
                std::ofstream f(twinPgn, std::ios::binary);
                f << "[Event \"A\"]\n[White \"Anderssen\"]\n[Black \"Kieseritzky\"]\n[Date \"1851.06.21\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. f4 exf4 1-0\n\n"
                  << "[Event \"A\"]\n[White \"Anderssen\"]\n[Black \"Kieseritzky\"]\n[Date \"1851.??.??\"]\n[Result \"1-0\"]\n\n1. e4 {joli} e5 2. f4 exf4 1-0\n\n"
                  << "[Event \"A\"]\n[White \"Anderssen\"]\n[Black \"Kieseritzky\"]\n[Date \"1851.06.21\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. f4 d5 1-0\n\n"
                  << "[Event \"B\"]\n[White \"Andersen\"]\n[Black \"Morphy\"]\n[Result \"0-1\"]\n\n1. d4 d5 0-1\n";
            }
            hy64db::PgnSource ts;
            hy64db::ConvertResult tr;
            std::string terr;
            hy64db::Database tdb;
            const bool ok = ts.open(twinPgn, &terr) && hy64db::convert(ts, dir + "/doublons", {}, &tr, &terr) && tdb.open(dir + "/doublons.hy64i", &terr);
            const auto groups = ok ? tdb.findTwins({}) : std::vector<std::vector<uint32_t>>{};
            hy64db::TwinCriteria loose;
            loose.sameMoves = false;
            const auto looseGroups = ok ? tdb.findTwins(loose) : std::vector<std::vector<uint32_t>>{};
            check(ok && groups.size() == 1 && groups[0] == std::vector<uint32_t>{1, 0} && looseGroups.size() == 1 && looseGroups[0].size() == 3,
                  "doublons : mêmes coups (la partie commentée gardée en tête) ; sans les coups, trois parties");
            // "Andersen" (misspelling) → "Anderssen": merge of the two names
            const uint32_t wrong = tdb.names().find(hy64db::Player, "Andersen");
            const int64_t changed = wrong == hy64db::NoName ? -1 : tdb.renameName(hy64db::Player, wrong, "Anderssen", &terr);
            const uint32_t right = tdb.names().find(hy64db::Player, "Anderssen");
            const auto use = tdb.nameUsage(hy64db::Player);
            hy64db::HeaderQuery q;
            q.white = "anderssen";
            check(changed == 1 && tdb.record(3).white == right && use[right] == 4 && use[wrong] == 0 && tdb.searchHeaders(q).size() == 4,
                  "correction des noms : « Andersen » fusionné dans « Anderssen » (4 parties)");
            hy64db::Database again;
            check(again.open(dir + "/doublons.hy64i", &terr) && again.record(3).white == right, "correction des noms : enregistrée dans la base");
            const hy64db::PlayerStats ps = tdb.playerStats(right);
            const uint32_t kies = tdb.names().find(hy64db::Player, "Kieseritzky");
            check(ps.games.size() == 4 && ps.asWhite.wins == 3 && ps.asWhite.losses == 1 && ps.asBlack.games() == 0 && !ps.opponents.empty() &&
                      ps.opponents[0].id == kies && ps.opponents[0].score.wins == 3 && ps.firstYear == 1851 && ps.years.size() == 1 && ps.years[0].games == 3,
                  "fiche joueur : 4 parties (+3 −1 avec les Blancs), adversaire le plus fréquent, années");
            {
                // Preparation against a player: tree of his moves with one colour, weak and strong lines
                const std::string prepPgn = dir + "/preparation.pgn";
                {
                    std::ofstream f(prepPgn, std::ios::binary);
                    auto game = [&](const char* white, const char* black, const char* date, const char* result, const char* moves, const char* fen = nullptr) {
                        f << "[Event \"P\"]\n[White \"" << white << "\"]\n[Black \"" << black << "\"]\n[Date \"" << date << "\"]\n[Result \"" << result << "\"]\n";
                        if (fen) f << "[SetUp \"1\"]\n[FEN \"" << fen << "\"]\n";
                        f << "\n" << moves << " " << result << "\n\n";
                    };
                    game("Cible", "A", "2020.01.01", "1-0", "1. e4 c5 2. Nf3 d6 3. d4");
                    game("Cible", "B", "2021.01.01", "1-0", "1. e4 c5 2. Nf3 d6 3. Bb5+");
                    game("Cible", "C", "2022.01.01", "0-1", "1. e4 c5 2. Nf3 Nc6");
                    game("Cible", "D", "2023.01.01", "0-1", "1. e4 c5 2. Nf3 Nc6");
                    game("Cible", "E", "2023.02.01", "1/2-1/2", "1. e4 c5 2. Nf3 Nc6");
                    game("Cible", "F", "2019.01.01", "1-0", "1. e4 e5 2. Nf3");
                    game("Cible", "G", "2018.01.01", "1-0", "1. d4 d5");
                    game("H", "Cible", "2023.03.01", "0-1", "1. e4 e5");
                    game("Cible", "I", "2023.04.01", "1-0", "1. Kd2", "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
                }
                hy64db::PgnSource src;
                hy64db::ConvertResult res;
                std::string perr;
                hy64db::Database pdb;
                const bool ready = src.open(prepPgn, &perr) && hy64db::convert(src, dir + "/preparation", {}, &res, &perr) && pdb.open(dir + "/preparation.hy64i", &perr);
                const uint32_t target = ready ? pdb.names().find(hy64db::Player, "Cible") : hy64db::NoName;
                const hy64db::Preparation w = hy64db::prepareAgainst(pdb, target, chess::White);
                const hy64db::PrepNode* e4 = w.root.children.size() == 1 ? &w.root.children[0] : nullptr;
                const hy64db::PrepNode* c5 = e4 && e4->children.size() == 1 ? &e4->children[0] : nullptr;
                const hy64db::PrepNode* nf3 = c5 && c5->children.size() == 1 ? &c5->children[0] : nullptr;
                check(ready && w.root.games == 7 && w.otherStarts == 1 && w.root.score.wins == 4 && w.root.score.draws == 1 && w.root.score.losses == 2
                          && w.root.dropped == 1 && e4 && e4->san == "e4" && e4->byPlayer && e4->games == 6 && e4->dropped == 1 && c5 && c5->san == "c5"
                          && !c5->byPlayer && c5->games == 5 && c5->lastYear == 2023 && nf3 && nf3->children.size() == 2 && nf3->children[0].san == "Nc6"
                          && nf3->children[0].games == 3 && nf3->children[0].score.losses == 2 && nf3->children[1].san == "d6" && nf3->children[1].score.wins == 2
                          && nf3->children[1].ids == std::vector<uint32_t>{0, 1} && w.firstYear == 2018 && w.lastYear == 2023,
                      "préparation contre un joueur : arbre de ses coups avec les Blancs (parties, score, dernière année ; coups isolés écartés)");
                const auto weak = hy64db::rankedLines(w, true);
                const auto strong = hy64db::rankedLines(w, false, 2);
                check(weak.size() == 1 && weak[0].size() == 4 && weak[0].back()->san == "Nc6" && strong.size() == 1 && strong[0].back()->san == "d6"
                          && w.lossesShort == 2 && w.lossesMiddle == 0 && w.quickLosses == std::vector<uint32_t>{2, 3} && w.averageLossMoves == 2,
                      "préparation contre un joueur : ligne où il marque le moins, ligne où il marque le plus, défaites courtes");
                hy64db::PrepOptions recent;
                recent.fromYear = 2021;
                recent.maxPlies = 2;
                recent.minGames = 1;
                const hy64db::Preparation r = hy64db::prepareAgainst(pdb, target, chess::White, recent);
                const hy64db::Preparation bl = hy64db::prepareAgainst(pdb, target, chess::Black);
                check(r.root.games == 4 && r.root.children.size() == 1 && r.root.children[0].children.size() == 1 && r.root.children[0].children[0].children.empty()
                          && bl.root.games == 1 && bl.root.score.wins == 1 && bl.root.children.empty() && bl.root.dropped == 1,
                      "préparation contre un joueur : depuis une année, profondeur limitée, avec les Noirs");
                {
                    // Integrity test: a sound database, then the same with the block of a game overwritten
                    const hy64db::IntegrityReport sound = pdb.checkIntegrity();
                    const hy64db::Record first = pdb.record(0);
                    const std::string from = dir + "/preparation", to = dir + "/abimee";
                    for (const auto& path : {hy64db::indexPath, hy64db::namesPath, hy64db::gamesPath}) {
                        std::ifstream in(path(from), std::ios::binary);
                        std::ofstream out(path(to), std::ios::binary);
                        out << in.rdbuf();
                    }
                    {
                        std::fstream g(hy64db::gamesPath(to), std::ios::binary | std::ios::in | std::ios::out);
                        g.seekp(std::streamoff(first.offset));
                        const std::string junk(first.size, char(0xFF));
                        g.write(junk.data(), std::streamsize(junk.size()));
                    }
                    hy64db::Database damaged;
                    const bool reopened = damaged.open(hy64db::indexPath(to), &perr);
                    const hy64db::IntegrityReport bad = reopened ? damaged.checkIntegrity() : hy64db::IntegrityReport{};
                    if (!(bad.problems() == 1)) std::printf("       %u problèmes, %u illisibles, %u longueurs\n", bad.problems(), bad.unreadable, bad.badLength);
                    check(sound.ok() && sound.games == 9 && sound.problems() == 0 && reopened && bad.games == 9 && !bad.ok() && bad.problems() == 1 && bad.first.size() == 1
                              && bad.first[0].id == 0,
                          "test d'intégrité : base saine sans problème ; bloc d'une partie écrasé, cette partie signalée");
                }
                {
                    // Pieces over a set of games: share of the positions per square, first move of a piece
                    const hy64db::PieceHeat heat = hy64db::pieceHeat(pdb, pdb.allGames());
                    const chess::Piece wn = chess::makePiece(chess::White, chess::Knight);
                    const auto knights = heat.shares(wn);
                    const chess::Square g1 = chess::makeSquare(6, 0), f3 = chess::makeSquare(5, 2), b1 = chess::makeSquare(1, 0), e2 = chess::makeSquare(4, 1),
                                        e4 = chess::makeSquare(4, 3), d2 = chess::makeSquare(3, 1), d4 = chess::makeSquare(3, 3);
                    check(heat.games == 8 && heat.skipped == 1 && heat.positionCount() == 29 && heat.positionCount(0) == 29 && heat.positionCount(1) == 0
                              && heat.occupancy[0][size_t(wn)][size_t(g1)] == 22 && heat.occupancy[0][size_t(wn)][size_t(f3)] == 7 && knights[size_t(b1)] == 1000000
                              && knights[size_t(g1)] == 758621 && heat.firstTo[size_t(g1)][size_t(f3)] == 6 && heat.moved(g1) == 6 && heat.firstPly[size_t(g1)] == 18
                              && heat.firstTo[size_t(e2)][size_t(e4)] == 7 && heat.firstTo[size_t(d2)][size_t(d4)] == 2 && heat.moved(b1) == 0
                              && hy64db::PieceHeat::sliceOfPly(20) == 0 && hy64db::PieceHeat::sliceOfPly(21) == 1 && hy64db::PieceHeat::sliceOfPly(300) == 4,
                          "pièces sur un ensemble de parties : présence par case, premier coup de chaque pièce, partie à position spéciale écartée");
                }
                // Transpositions merged, and the tree compared with one's own repertoire
                const std::string transPgn = dir + "/transpositions.pgn";
                {
                    std::ofstream f(transPgn, std::ios::binary);
                    auto game = [&](const char* result, const char* moves) {
                        f << "[Event \"T\"]\n[White \"Cible\"]\n[Black \"X\"]\n[Date \"2024.01.01\"]\n[Result \"" << result << "\"]\n\n" << moves << " " << result << "\n\n";
                    };
                    game("0-1", "1. Nf3 d5 2. d4 Nf6");
                    game("0-1", "1. d4 d5 2. Nf3 Nf6");
                    game("0-1", "1. d4 d5 2. Nf3 Nf6");
                    game("1-0", "1. d4 d5 2. c4 e6");
                    game("1-0", "1. d4 d5 2. c4 e6 3. Nc3");
                }
                hy64db::PgnSource tsrc;
                hy64db::Database tdb2;
                const bool tready = tsrc.open(transPgn, &perr) && hy64db::convert(tsrc, dir + "/transpositions", {}, &res, &perr) && tdb2.open(dir + "/transpositions.hy64i", &perr);
                hy64db::PrepOptions every;
                every.minGames = 1;
                const hy64db::Preparation t = hy64db::prepareAgainst(tdb2, tready ? tdb2.names().find(hy64db::Player, "Cible") : hy64db::NoName, chess::White, every);
                auto child = [](const hy64db::PrepNode* n, const char* san) -> const hy64db::PrepNode* {
                    if (!n) return nullptr;
                    for (const auto& c : n->children)
                        if (c.san == san) return &c;
                    return nullptr;
                };
                const auto* d4 = child(&t.root, "d4");
                const auto* d4nf3 = child(child(d4, "d5"), "Nf3");
                const auto* nf6 = child(d4nf3, "Nf6");
                const auto* other = child(child(child(&t.root, "Nf3"), "d5"), "d4");
                check(tready && t.root.games == 5 && t.root.children.size() == 2 && d4 && d4->games == 4 && d4nf3 && d4nf3->games == 2 && !d4nf3->transposition
                          && nf6 && nf6->games == 3 && nf6->score.losses == 3 && nf6->ids == std::vector<uint32_t>{0, 1, 2} && other && other->transposition
                          && other->children.empty() && other->key == d4nf3->key && other->games == 1,
                      "préparation contre un joueur : transpositions réunies (1.Cf3 d5 2.d4 et 1.d4 d5 2.Cf3), position développée une seule fois");
                chess::Game line, rep;
                for (const char* san : {"d4", "d5", "c4", "e6"}) line.playMove(line.current()->pos.parseSan(san));
                const int added = chess::repertoire::addLine(rep, line);
                const hy64db::PrepCoverage cov = hy64db::coverage(t, rep);
                const auto* c4 = child(child(d4, "d5"), "c4");
                const auto* nc3 = child(child(c4, "e6"), "Nc3");
                check(added == 4 && cov.of(d4) == hy64db::PrepCover::Inside && cov.of(child(c4, "e6")) == hy64db::PrepCover::Inside && cov.of(d4nf3) == hy64db::PrepCover::Uncovered
                          && cov.of(nf6) == hy64db::PrepCover::Outside && nc3 && cov.of(nc3) == hy64db::PrepCover::Ends && cov.of(child(&t.root, "Nf3")) == hy64db::PrepCover::Uncovered
                          && cov.gaps.size() == 3 && cov.gaps[0].path.back() == d4nf3 && cov.gaps[0].known == std::vector<std::string>{"c4"}
                          && cov.gaps[0].kind == hy64db::PrepCover::Uncovered,
                      "préparation contre un joueur : face au répertoire (coup non prévu, répertoire qui s'arrête, lignes au-delà)");
            }
            {
                // ECO classification (lichess-org/chess-openings data, resources/eco-opening)
                chess::eco::Book book;
                int lines = 0;
                for (const char* f : {"a", "b", "c", "d", "e"}) {
                    std::ifstream in(std::string(HYDRA64_SOURCE_DIR "/resources/eco-opening/") + f + ".tsv");
                    std::stringstream ss;
                    ss << in.rdbuf();
                    lines += book.load(ss.str());
                }
                chess::Game kg;
                for (const char* san : {"e4", "e5", "f4", "exf4"}) kg.playMove(kg.current()->pos.parseSan(san));
                int at = 0;
                const chess::eco::Opening* o = book.classify(kg, &at);
                const int64_t changed = tdb.classifyEco(book, &terr);
                check(lines > 3000 && o && o->eco == "C33" && at == 4 && book.nameOf("C33").find("King's Gambit") != std::string::npos && changed >= 1 &&
                          hy64db::formatEco(tdb.record(0).eco) == "C33" && hy64db::formatEco(tdb.record(3).eco) == "D00",
                      "classement ECO : " + std::to_string(lines) + " ouvertures, gambit du roi accepté C33, base classée (" + std::to_string(changed) + " codes changés)");
            }
            const hy64db::EventTable et = tdb.eventTable(tdb.names().find(hy64db::Event, "A"));
            check(et.players.size() == 2 && et.players[0].id == right && et.players[0].points == 3 && et.points[0][1] == 3 && et.games[0][1] == 3 &&
                      et.pairGames[0][1].size() == 3 && et.ids.size() == 3,
                  "tableau croisé d'un tournoi de la base : classement et points entre joueurs");
        }

        // Search (words, ignoring case and accents) and sort
        const auto kasp = db.search("kasp"), unz = db.search("unzick"), memo = db.search("memorial");
        int kExpected = 0, uExpected = 0, mExpected = 0;
        for (uint32_t id = 0; id < db.count(); ++id) {
            Game g;
            db.game(id, g);
            const std::string w = g.header("White"), e = g.header("Event");
            kExpected += w.rfind("Kasparov", 0) == 0 || g.header("Black").rfind("Kasparov", 0) == 0;
            uExpected += w == "Ünzicker, Wolfgang";
            mExpected += e == "Mémorial Capablanca";
        }
        check(int(kasp.size()) == kExpected && int(unz.size()) == uExpected && int(memo.size()) == mExpected && kExpected > 0 && uExpected > 0,
              "base : recherche joueur et tournoi (sans casse ni accents)");
        auto ids = db.allGames();
        bool sortedOk = db.sort(ids, hy64db::ColWhiteElo, false);
        for (size_t i = 1; i < ids.size() && sortedOk; ++i) {
            const auto a = db.record(ids[i - 1]), b = db.record(ids[i]);
            if (b.whiteElo && a.whiteElo < b.whiteElo) sortedOk = false;
            if (!a.whiteElo && b.whiteElo) sortedOk = false; // unknown ones at the end
        }
        check(sortedOk, "base : tri par Elo des Blancs (décroissant, inconnus en fin)");
        ids = db.allGames();
        sortedOk = db.sort(ids, hy64db::ColWhite, true);
        for (size_t i = 1; i < ids.size() && sortedOk; ++i)
            if (hy64db::fold(db.names().name(hy64db::Player, db.record(ids[i - 1]).white)) > hy64db::fold(db.names().name(hy64db::Player, db.record(ids[i]).white)))
                sortedOk = false;
        check(sortedOk, "base : tri par Blancs (alphabétique, accents repliés)");

        // Position search: index + replays = brute force
        std::mt19937 pick(3);
        int posOk = 0, posTotal = 0;
        for (int k = 0; k < 40; ++k) {
            Game g;
            db.game(uint32_t(pick() % db.count()), g);
            const auto main = g.mainline();
            if (main.empty()) continue;
            const Position target = main[pick() % main.size()]->pos;
            const uint64_t key = polyglot::key(target);
            std::vector<uint32_t> brute;
            for (uint32_t id = 0; id < db.count(); ++id) {
                Game h;
                db.game(id, h);
                bool hit = polyglot::key(h.startPosition()) == key;
                for (const Node* n : h.mainline()) hit = hit || polyglot::key(n->pos) == key;
                if (hit) brute.push_back(id);
            }
            ++posTotal;
            posOk += hy64db::searchPosition(db, target) == brute;
        }
        check(posOk == posTotal && posTotal > 30, "base : recherche de position identique à la force brute (" + std::to_string(posOk) + "/" + std::to_string(posTotal) + ")");
        const auto tree = hy64db::positionTree(db, Position::startPos());
        uint64_t treeGames = 0;
        for (const auto& m : tree) treeGames += m.games();
        uint64_t fromStart = 0;
        for (uint32_t id = 0; id < db.count(); ++id)
            if (!(db.record(id).flags & hy64db::FlagStartFen) && db.record(id).plies > 0) ++fromStart;
        check(!tree.empty() && treeGames == fromStart, "base : arbre de la position initiale (" + std::to_string(treeGames) + " parties)");

        // Writing: addition, replacement, deletion, compaction, reopening
        Game added;
        pgn::parseGame("[Event \"Ajoutée\"]\n[White \"Nouveau, Joueur\"]\n[Black \"B\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0\n", added);
        uint32_t newId = 0;
        const uint32_t before = db.count();
        check(db.append(added, &newId, &err) && newId == before && db.count() == before + 1 && db.search("nouveau").size() == 1,
              "base : partie ajoutée (" + err + ")");
        Game back;
        check(db.game(newId, back) && pgn::writeGame(back) == pgn::writeGame(added), "base : partie ajoutée relue à l'identique");
        added.setHeader("Event", "Remplacée");
        check(db.replace(0, added, &err) && db.game(0, back) && back.header("Event") == "Remplacée", "base : partie remplacée");
        check(db.setDeleted(1, true, &err) && db.record(1).deleted() && db.allGames().size() == db.count() - 1, "base : partie marquée supprimée");
        // position search sees the game added after the index was built
        Position mate = added.mainline().back()->pos;
        const auto mateHits = hy64db::searchPosition(db, mate);
        check(std::find(mateHits.begin(), mateHits.end(), newId) != mateHits.end(), "base : position d'une partie ajoutée après l'index trouvée");
        db.close();
        check(db.open(dir + "/base", &err) && db.count() == before + 1 && db.game(newId, back) && back.header("Event") == "Ajoutée",
              "base : réouverture après écriture");
        // (game 0 was replaced by a copy of the added game: two "Nouveau, Joueur")
        const bool compacted = db.compact(&err);
        check(compacted && db.count() == before && db.allGames().size() == before && db.search("nouveau").size() == 2,
              "base : compactage (partie supprimée retirée) — " + std::to_string(db.count()) + " parties, "
                  + std::to_string(db.search("nouveau").size()) + " « Nouveau » " + err);
        fs::remove_all(dir);
    }

    // Game endings
    {
        Game g;
        GameEnd end;
        for (const char* san : {"Nf3", "Nf6", "Ng1", "Ng8", "Nf3", "Nf6", "Ng1"}) g.playMove(g.current()->pos.parseSan(san));
        check(!detectGameEnd(g.current(), end), "pas de répétition après 7 demi-coups");
        g.playMove(g.current()->pos.parseSan("Ng8"));
        check(detectGameEnd(g.current(), end) && end.result == "1/2-1/2", "triple répétition détectée");

        Game m;
        for (const char* san : {"f3", "e5", "g4", "Qh4#"}) m.playMove(m.current()->pos.parseSan(san));
        const bool mated = detectGameEnd(m.current(), end);
        check(mated && end.result == "0-1" && end.reason == "Échec et mat", "mat du lion : " + end.reason);

        Position p;
        p.setFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        Game s(p);
        check(detectGameEnd(s.root(), end) && end.reason == "Pat", "pat détecté");
    }

    // PGN in French notation
    {
        Game fr;
        std::string err;
        const bool ok = pgn::parseGame("1. e4 e5 2. Cf3 Cc6 3. Fb5 a6 4. Fa4 Cf6 5. O-O Fe7 6. Te1 b5 7. Fb3 d6 "
                                       "8. c3 O-O 9. h3 Ca5 10. Fc2 c5 11. d4 Dc7 12. Cbd2 Fd7 13. Rh2 Tfe8 *", fr, &err);
        std::string sans;
        for (const Node* n : fr.mainline()) sans += n->san + ' ';
        check(ok && fr.mainline().size() == 26, "lecture d'un PGN en français (" + std::to_string(fr.mainline().size()) + " demi-coups) " + err);
        check(sans.find("Re1 ") != std::string::npos && sans.find("Kh2 ") != std::string::npos && sans.find("Rfe8 ") != std::string::npos
                  && sans.find("Qc7 ") != std::string::npos,
              "Te1 → Re1, Rh2 (roi) → Kh2, Tfe8 → Rfe8, Dc7 → Qc7");
        check(pgn::writeGame(fr).find("Re1") != std::string::npos, "réécrit en anglais (standard PGN)");
    }

    // Clock times [%clk]
    {
        Game g;
        std::string err;
        pgn::parseGame("[TimeControl \"180+2\"]\n1. e4 {[%clk 0:03:01.5]} e5 {Bien joué [%clk 0:02:59]} 2. Nf3 *", g, &err);
        const auto line = g.mainline();
        check(line[0]->clockMs == 181500 && line[0]->comment.empty(), "lecture [%clk] (181,5 s, commentaire vidé)");
        check(line[1]->clockMs == 179000 && line[1]->comment == "Bien joué" && line[2]->clockMs == -1, "commentaire conservé autour de [%clk]");
        const std::string out = pgn::writeGame(g);
        check(out.find("{[%clk 0:03:01.5]}") != std::string::npos && out.find("{[%clk 0:02:59] Bien joué}") != std::string::npos,
              "écriture [%clk]");
        Game g2;
        pgn::parseGame(out, g2, &err);
        check(g2.mainline()[1]->clockMs == 179000 && g2.header("TimeControl") == "180+2", "aller-retour des temps et de la cadence");
    }


    // Polyglot books: reference keys of the format
    {
        struct KeyCase { const char* moves; uint64_t key; };
        const KeyCase keys[] = {
            {"", 0x463b96181691fc9cULL},
            {"e4", 0x823c9b50fd114196ULL},
            {"e4 d5", 0x0756b94461c50fb0ULL},
            {"e4 d5 e5", 0x662fafb965db29d4ULL},
            {"e4 d5 e5 f5", 0x22a48b5a8e47ff78ULL},
            {"e4 d5 e5 f5 Ke2", 0x652a607ca3f242c1ULL},
            {"e4 d5 e5 f5 Ke2 Kf7", 0x00fdd303c946bdd9ULL},
            {"a4 b5 h4 b4 c4", 0x3c8123ea7b067637ULL},
            {"a4 b5 h4 b4 c4 bxc3 Ra3", 0x5c3f9b829b279560ULL},
        };
        bool allKeys = true;
        for (const KeyCase& kc : keys) {
            Position p = Position::startPos();
            std::istringstream in(kc.moves);
            std::string san;
            while (in >> san) p.makeMove(p.parseSan(san));
            if (polyglot::key(p) != kc.key) {
                allKeys = false;
                std::printf("       clé Polyglot fausse après « %s »\n", kc.moves);
            }
        }
        check(allKeys, "clés Polyglot (positions de référence, prise en passant comprise)");

        // Small hand-written book: start → e4 (weight 3), d4 (weight 1);
        // a position with castling encoded e1h1.
        const auto file = std::filesystem::temp_directory_path() / "hydra64_test_book.bin";
        struct Raw { uint64_t key; uint16_t move, weight; };
        auto enc = [](const char* from, const char* to) {
            const Square f = parseSquare(from), t = parseSquare(to);
            return uint16_t(fileOf(t) | rankOf(t) << 3 | fileOf(f) << 6 | rankOf(f) << 9);
        };
        Position castle;
        castle.setFen("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
        std::vector<Raw> raws{{polyglot::key(Position::startPos()), enc("d2", "d4"), 1},
                              {polyglot::key(Position::startPos()), enc("e2", "e4"), 3},
                              {polyglot::key(Position::startPos()), enc("e2", "e5"), 9}, // illegal: ignored
                              {polyglot::key(castle), enc("e1", "h1"), 5}};
        std::sort(raws.begin(), raws.end(), [](const Raw& a, const Raw& b) { return a.key < b.key; });
        {
            std::ofstream out(file, std::ios::binary);
            for (const Raw& r : raws) {
                unsigned char b[16] = {};
                for (int i = 0; i < 8; ++i) b[i] = uint8_t(r.key >> (56 - 8 * i));
                b[8] = uint8_t(r.move >> 8), b[9] = uint8_t(r.move);
                b[10] = uint8_t(r.weight >> 8), b[11] = uint8_t(r.weight);
                out.write(reinterpret_cast<const char*>(b), 16);
            }
        }
        polyglot::Book book;
        std::string err;
        check(book.open(file.string(), &err) && book.entryCount() == 4, "ouverture d'une bibliothèque Polyglot");
        const auto start = book.moves(Position::startPos());
        check(start.size() == 2 && Position::toUci(start[0].move) == "e2e4" && start[0].weight == 3
                  && Position::toUci(start[1].move) == "d2d4",
              "coups de bibliothèque triés par poids, coup illégal écarté");
        const auto castles = book.moves(castle);
        check(castles.size() == 1 && Position::toUci(castles[0].move) == "e1g1", "roque Polyglot (e1h1) → e1g1");
        Position other = Position::startPos();
        other.makeMove(other.parseSan("a4"));
        check(book.moves(other).empty(), "position absente de la bibliothèque");
        book.close();
        std::filesystem::remove(file);
        check(!book.open(file.string()), "fichier absent refusé");
    }


    // ChessBase books (CTG): signatures and hashes checked on a real
    // book (Fritz 16) — start, Black to move, mirrored board (king on c1 with no
    // castling possible), playable en passant.
    {
        struct SigCase { const char* fen; uint32_t hash; const char* hex; };
        const SigCase sigs[] = {
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0xeb7ed3d7u, "56b60f7960f3a60f58b078e0c1e1a60f5960f3b60f7f"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", 0x8c978593u, "56b60f7960f3a60f58b078e0c721a60f5960f3b60f7f"},
            {"r2q1rk1/1p1nbppp/p2pbn2/4p3/4P3/1NN1BP2/PPPQ2PP/2KR1B1R w - - 5 11", 0x5259808fu, "16b60e60f0d189fb94deb55a21e71c1a40690e61d700"},
            {"rnbqkb1r/pp1p1ppp/5n2/2pPp3/2P5/8/PP2PPPP/RNBQKBNR w KQkq e6 0 4", 0xb6ce2c24u, "77b60f7960f3a1b9588678e0c721a613f5960eb60f704f"},
        };
        bool allSigs = true;
        for (const SigCase& sc : sigs) {
            Position p;
            p.setFen(sc.fen);
            const ctg::Signature sig = ctg::signature(p);
            std::string hex;
            char buf[3];
            for (int i = 0; i < sig.length; ++i) {
                std::snprintf(buf, sizeof buf, "%02x", sig.bytes[size_t(i)]);
                hex += buf;
            }
            if (hex != sc.hex || ctg::hash(sig) != sc.hash) {
                allSigs = false;
                std::printf("       signature CTG fausse : %s\n", sc.fen);
            }
        }
        check(allSigs, "signatures et hachages CTG (trait aux Noirs, symétrie, prise en passant)");

        // Each legal move has a code that gives the same move back (varied positions).
        bool roundTrip = true;
        for (const char* fen : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                                "r3k2r/pppq1ppp/2npbn2/4p3/2B1P3/2NPBN2/PPPQ1PPP/R3K2R b KQkq - 0 8",
                                "r2q1rk1/1p1nbppp/p2pbn2/4p3/4P3/1NN1BP2/PPPQ2PP/2KR1B1R w - - 5 11",
                                "rnbqkb1r/pp1p1ppp/5n2/2pPp3/2P5/8/PP2PPPP/RNBQKBNR w KQkq e6 0 4"}) {
            Position p;
            p.setFen(fen);
            for (const Move& m : p.legalMoves()) {
                if (m.promo != NoType && m.promo != Queen) continue;
                const int code = ctg::encodeMove(p, m);
                if (code < 0 || ctg::decodeMove(p, uint8_t(code)) != m) {
                    roundTrip = false;
                    std::printf("       coup sans code CTG : %s dans %s\n", Position::toUci(m).c_str(), fen);
                }
            }
        }
        check(roundTrip, "codage CTG des coups (roques, Noirs, échiquier symétrisé)");

        // Small hand-written book: start (e4 !, d4) and positions after e4 and d4.
        const auto dir = std::filesystem::temp_directory_path();
        const std::string base = (dir / "hydra64_test_ctg").string();
        const Position start = Position::startPos();
        const Move e4 = start.parseSan("e4"), d4 = start.parseSan("d4");
        auto be = [](std::string& out, uint32_t v, int bytes) {
            for (int i = bytes - 1; i >= 0; --i) out += char((v >> (8 * i)) & 0xff);
        };
        auto entry = [&](const Position& pos, std::vector<std::pair<Move, uint8_t>> moves, int w, int d, int l, uint8_t rec) {
            const ctg::Signature sig = ctg::signature(pos);
            std::string e(reinterpret_cast<const char*>(sig.bytes.data()), size_t(sig.length));
            e += char(1 + 2 * moves.size());
            for (auto& [m, ann] : moves) {
                e += char(ctg::encodeMove(pos, m));
                e += char(ann);
            }
            be(e, uint32_t(w + d + l), 3);
            be(e, uint32_t(l), 3);
            be(e, uint32_t(w), 3);
            be(e, uint32_t(d), 3);
            be(e, 0, 4);
            be(e, uint32_t(w + d + l), 3); // Average Elo: 2500
            be(e, uint32_t(2500 * (w + d + l)), 4);
            be(e, 0, 3);
            be(e, 0, 4);
            e += char(rec);
            e += '\0';
            e += '\0';
            return e;
        };
        std::string page;
        be(page, 3, 2);
        be(page, 0, 2);
        page += entry(start, {{e4, ctg::Good}, {d4, ctg::NoAnnotation}}, 0, 0, 0, 0);
        page += entry(start.after(e4), {}, 30, 50, 20, ctg::Recommended);
        page += entry(start.after(d4), {}, 5, 3, 2, ctg::Avoid);
        page.resize(4096, '\0');
        {
            std::ofstream ctg(base + ".ctg", std::ios::binary);
            ctg << std::string(4096, '\0') << page;
            // Index: all the slots (0 to 2) point to page 0.
            std::string cto(16, '\0');
            for (int k = 0; k < 3; ++k) be(cto, 0, 4);
            std::ofstream(base + ".cto", std::ios::binary) << cto;
            std::string ctb;
            be(ctb, 0, 4);
            be(ctb, 0, 4); // first slot
            be(ctb, 2, 4); // last slot
            std::ofstream(base + ".ctb", std::ios::binary) << ctb;
        }
        ctg::Book book;
        std::string err;
        check(book.open(base + ".cto", &err) && book.path() == base + ".ctg", "ouverture d'une bibliothèque CTG (par n'importe lequel des trois fichiers)");
        const auto moves = book.moves(start);
        check(moves.size() == 2 && moves[0].move == e4 && moves[0].annotation == ctg::Good && moves[0].stats.games == 100
                  && moves[0].stats.wins == 30 && moves[0].stats.draws == 50 && moves[0].stats.averageElo == 2500
                  && moves[0].stats.recommendation == ctg::Recommended && moves[1].move == d4 && moves[1].stats.recommendation == ctg::Avoid,
              "coups CTG : annotation, statistiques, Elo, recommandation, tri par parties");
        check(std::abs(moves[0].stats.score() - 55.0) < 1e-9 && ctg::annotationSymbol(moves[0].annotation) == "!", "score et symbole d'annotation");
        check(book.moves(start.after(e4)).empty(), "position sans coups");
        Position other = start;
        other.makeMove(other.parseSan("a4"));
        ctg::Stats st;
        check(!book.positionStats(other, st) && book.moves(other).empty(), "position absente de la bibliothèque CTG");

        {
            // Conversion to a Hydra64 book (.hy64book): everything is taken over identically
            const std::string out = base + ".hy64book";
            treebook::ConvertResult cr;
            std::string cerr;
            const bool converted = treebook::convertCtg(base + ".ctg", out, &cr, &cerr);
            check(converted && cr.positions == 3 && cr.inFile == 3 && cr.moves == 2,
                  "CTG → .hy64book : 3 positions parcourues sur 3 dans le fichier, 2 coups (" + cerr + ")");
            treebook::Book tb;
            const auto tm = tb.open(out) ? tb.moves(start) : std::vector<treebook::BookMove>();
            bool same = tm.size() == moves.size();
            for (size_t i = 0; same && i < tm.size(); ++i) {
                const auto& a = tm[i];
                const auto& c = moves[i];
                same = a.move == c.move && a.annotation == c.annotation && a.recommendation == c.stats.recommendation && a.hasStats == c.hasStats
                    && a.games() == uint64_t(c.stats.games) && a.wins == uint32_t(c.stats.wins) && a.draws == uint32_t(c.stats.draws)
                    && a.losses == uint32_t(c.stats.losses) && a.averageElo == c.stats.averageElo && a.performance == c.stats.performance;
            }
            check(same, "CTG → .hy64book : coups, annotations, marques, parties, résultats, Elo moyen et performance identiques");
            tb.close();
            std::filesystem::remove(out);
            // CTG → Polyglot (open format): weight = games, 0 for a move "to avoid"
            const std::string bin = base + ".bin";
            check(treebook::convertTree(base + ".ctg", bin, treebook::TreeFormat::Polyglot, &cr, &cerr) && cr.moves == 2,
                  "CTG → Polyglot : 2 coups écrits (" + cerr + ")");
            polyglot::Book pb;
            const auto pm = pb.open(bin) ? pb.moves(start) : std::vector<polyglot::BookMove>();
            auto weightOf = [&](Move m) {
                for (const auto& x : pm)
                    if (x.move == m) return int(x.weight);
                return -1;
            };
            check(pm.size() == 2 && weightOf(e4) == 100 && weightOf(d4) == 0, "CTG → Polyglot : poids e4 = 100 parties, d4 « à éviter » = 0");
            pb.close();
            // Polyglot → .hy64book → Polyglot: same moves, same weights
            const std::string back = base + "-back.hy64book", bin2 = base + "-2.bin";
            const bool round = treebook::convertTree(bin, back, treebook::TreeFormat::Hy64Book, &cr, &cerr)
                            && treebook::convertTree(back, bin2, treebook::TreeFormat::Polyglot, &cr, &cerr);
            auto fileBytes = [](const std::string& p) {
                std::ifstream f(p, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            };
            check(round && fileBytes(bin) == fileBytes(bin2), "Polyglot → .hy64book → Polyglot : fichier identique");
            for (const std::string& f : {bin, back, bin2}) std::filesystem::remove(f);
        }
        book.close();
        std::filesystem::remove(base + ".cto");
        check(!book.open(base + ".ctg", &err), "fichier .cto manquant : refusé");
        std::filesystem::remove(base + ".ctg");
        std::filesystem::remove(base + ".ctb");
    }


    // Castling: a tree converted to Polyglot writes castling "king onto its rook's square"
    {
        const auto dir = std::filesystem::temp_directory_path();
        const std::string txt = (dir / "hydra64_test_castle.txt").string(), bin = (dir / "hydra64_test_castle.bin").string();
        Game g;
        pgn::parseGame("1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 4. O-O *", g);
        // Export: one line per position, from the initial position up to castling.
        auto epd4 = [](const Position& p) {
            std::string f = p.fen();
            return f.substr(0, f.rfind(' ', f.rfind(' ') - 1));
        };
        {
            std::ofstream out(txt);
            const Node* prev = g.root();
            for (const Node* n : g.mainline()) {
                out << epd4(prev->pos) << '\t' << Position::toUci(n->move) << '|' << n->san << "|10|5|5|2700|2020|0\n";
                prev = n;
            }
        }
        treebook::ConvertResult cr;
        std::string cerr;
        polyglot::Book pb;
        const Position before = g.mainline()[5]->pos, after = g.mainline()[6]->pos;
        const bool ok = treebook::convertTree(txt, bin, treebook::TreeFormat::Polyglot, &cr, &cerr) && pb.open(bin);
        const auto pm = ok ? pb.moves(before) : std::vector<polyglot::BookMove>();
        check(ok && pm.size() == 1 && pm[0].move == before.parseSan("O-O") && before.after(pm[0].move).fen() == after.fen(),
              "arbre → Polyglot : le roque est écrit e1h1 et relu comme le roque (" + cerr + ")");
        check(polyglot::encodeMove(before, before.parseSan("O-O")) == uint16_t(7 | 0 << 3 | 4 << 6 | 0 << 9), "Polyglot : roque codé e1h1");
        pb.close();
        std::filesystem::remove(txt);
        std::filesystem::remove(bin);
    }

    // Aquarium / Chess Assistant trees exported as text → .hy64book book
    {
        const auto dir = std::filesystem::temp_directory_path();
        const std::string txt = (dir / "hydra64_test_tree.txt").string(), book = (dir / "hydra64_test_tree.hy64book").string();
        {
            std::ofstream out(txt, std::ios::binary);
            // Start (with the "number of moves" column of the diagnostics), position with castling and promotion
            out << "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -\t2\td2d4|d4|30|20|30|2800|2021|0\te2e4|e4|50|30|20|3115|2023|0\r\n";
            out << "r3k2r/1P6/8/8/8/8/8/R3K2R w KQkq -\te1g1|O-O|1|1|1|2500|2000|0\tb7b8|b8=Q|5|0|0|2400|1999|0\tz9z9|??|1|1|1|0|0|0\r\n";
            out << "pas une position\r\n";
            // Same position a 2nd time (less played): ignored
            out << "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -\te2e4|e4|1|1|1|0|0|0\r\n";
        }
        check(treebook::looksLikeAquariumExport(txt), "export d'arbre Aquarium reconnu");
        treebook::ConvertResult res;
        std::string err;
        int calls = 0;
        const bool ok = treebook::convertAquariumExport(txt, book, &res, &err, [&](uint64_t, uint64_t) { ++calls; return true; });
        check(ok && res.positions == 3 && res.moves == 4 && res.skipped == 2 && calls > 0,
              "conversion : positions, coups, lignes et coups illisibles écartés, doublon ignoré");
        treebook::Book b;
        check(b.open(book, &err) && b.entryCount() == 4, "ouverture de la bibliothèque .hy64book");
        const auto start = b.moves(Position::startPos());
        check(start.size() == 2 && Position::toUci(start[0].move) == "e2e4" && start[0].wins == 50 && start[0].draws == 30
                  && start[0].losses == 20 && start[0].maxElo == 3115 && start[0].year == 2023 && Position::toUci(start[1].move) == "d2d4",
              "coups de l'arbre triés par parties, statistiques, Elo maximum et année");
        Position special;
        special.setFen("r3k2r/1P6/8/8/8/8/8/R3K2R w KQkq - 0 1");
        const auto sm = b.moves(special);
        check(sm.size() == 2 && Position::toUci(sm[0].move) == "b7b8q" && Position::toUci(sm[1].move) == "e1g1",
              "promotion (lue en SAN faute de suffixe) et roque");
        Position other = Position::startPos();
        other.makeMove(other.parseSan("a4"));
        check(b.moves(other).empty(), "position absente de l'arbre");
        b.close();
        check(!treebook::convertAquariumExport(txt, book, nullptr, &err, [](uint64_t, uint64_t) { return false; }) && err == "cancelled"
                  && b.open(book),
              "conversion annulée : la bibliothèque existante reste intacte");
        b.close();
        std::filesystem::remove(txt);
        std::filesystem::remove(book);
        check(!b.open(txt), "fichier .hy64book absent ou invalide refusé");
    }


    // Aquarium / Chess Assistant trees (.hsh + .elm) read directly
    {
        // CPS keys (embedded tables): identical to those computed with Aquarium's addonce1.dll
        struct KeyCase { const char* fen; const char* bits; };
        const KeyCase keys[] = {
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
             "1011001010101001101000110011000110001001000010011101010100010101011011100000"},
            {"r2q1rk1/pp2bpp1/3p3p/2pPn3/5B2/3P1N2/PPP3PP/R2Q1RK1 w - - 0 1",
             "10000000000000000000100000100010011001010111011010101100100110010000"},
            {"rnbqkb1r/pp1p1ppp/5n2/2pPp3/2P5/8/PP2PPPP/RNBQKBNR w KQkq e6 0 4",
             "1011001010101001101000000000110001000000100111010101000101010110011110010"},
        };
        aquarium::Book book;
        bool allKeys = true;
        for (const KeyCase& kc : keys) {
            Position p;
            p.setFen(kc.fen);
            std::string bits;
            for (uint8_t bit : book.keyBits(p)) bits += char('0' + bit);
            if (bits != kc.bits) {
                allKeys = false;
                std::printf("       clé CPS fausse : %s\n", kc.fen);
            }
        }
        check(allKeys, "arbre Aquarium : clés CPS (tables intégrées, prise en passant)");
        Position threeKnights;
        threeKnights.setFen("r1bqkb1r/pppppppp/n1n2n2/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        check(book.keyBits(threeKnights).empty(), "arbre Aquarium : position non représentable (trois cavaliers)");
        std::string err;
        check(!book.open("/nonexistent/tree.hsh", &err) && err == "missing", "arbre Aquarium : fichiers absents refusés");
        // Check on a real tree (not distributable): HYDRA64_AQUARIUM_TREE=…/d2m.hsh
        if (const char* tree = std::getenv("HYDRA64_AQUARIUM_TREE")) {
            check(book.open(tree, &err), "arbre Aquarium ouvert");
            const auto moves = book.moves(Position::startPos());
            bool e4 = false;
            for (const auto& m : moves) e4 |= Position::toUci(m.move) == "e2e4" && m.games() > 0;
            check(moves.size() >= 10 && e4, "arbre Aquarium : coups et statistiques de la position de départ");
            Position benoni;
            benoni.setFen("rnbqkb1r/pp1p1ppp/5n2/2pPp3/2P5/8/PP2PPPP/RNBQKBNR w KQkq e6 0 4");
            bool ep = false;
            for (const auto& m : book.moves(benoni)) ep |= Position::toUci(m.move) == "d5e6";
            check(ep, "arbre Aquarium : prise en passant (clé et coup)");
        } else {
            std::printf("[SKIP] arbre Aquarium réel : définir HYDRA64_AQUARIUM_TREE pour le tester\n");
        }
    }

    // --- Deep analysis (tree, minimax, steps) with a simulated engine
    {
        using namespace deep;
        // Simulated engine: material + fixed noise per position; move = best at one half-move.
        auto material = [](const Position& p) {
            static const int value[] = {0, 100, 300, 310, 500, 900, 0};
            int v = 0;
            for (int sq = 0; sq < 64; ++sq) {
                const Piece pc = p.at(Square(sq));
                if (pc == NoPiece) continue;
                v += (pc & 8 ? -1 : 1) * value[typeOf(pc)];
            }
            return p.sideToMove() == White ? v : -v;
        };
        auto noise = [](const Position& p) { return int(polyglot::key(p) % 41) - 20; };
        auto staticEval = [&](const Position& p) { return material(p) + noise(p); };
        auto engine = [&](const Position& p, const std::vector<Move>& allowed) {
            Eval e;
            const std::vector<Move> moves = allowed.empty() ? p.legalMoves() : allowed;
            if (moves.empty()) return e;
            int best = -1000000;
            Move bm;
            for (const Move& m : moves) {
                const Position q = p.after(m);
                const int s = q.legalMoves().empty() ? (q.inCheck() ? 100000 : 0) : -staticEval(q);
                if (s > best) {
                    best = s;
                    bm = m;
                }
            }
            e.valid = true;
            e.score = std::clamp(best, -5000, 5000);
            e.depth = 10;
            e.pv = {bm};
            const Position q = p.after(bm);
            if (!q.legalMoves().empty()) e.pv.push_back(q.legalMoves().front());
            return e;
        };
        auto run = [&](Project& pr, int stages) {
            int tasks = 0;
            for (int s = 0; s < stages; ++s) {
                if (pr.generateStage() == 0 && pr.queue().empty()) break;
                while (auto t = pr.takeNext()) {
                    Position pos;
                    if (!pr.positionOf(*t, pos)) continue;
                    std::vector<Move> allowed;
                    if (t->kind == TaskKind::Alternative) {
                        allowed = pr.searchMoves(*t);
                        if (allowed.empty()) {
                            pr.applyResult(*t, Eval{});
                            continue;
                        }
                    }
                    pr.applyResult(*t, engine(pos, allowed));
                    ++tasks;
                }
            }
            return tasks;
        };

        // Minimax: transposition (1.Nf3 Nf6 2.Nc3 and 1.Nc3 Nf6 2.Nf3) and repetition (knights going back and forth)
        {
            Tree t;
            const Position start = Position::startPos();
            t.addRoot(start);
            auto mv = [](const Position& p, const char* san) { return p.parseSan(san); };
            Position a = start;
            const uint32_t viaNf3 = t.ensurePath(start, {mv(a, "Nf3"), mv(a.after(mv(a, "Nf3")), "Nf6")});
            (void)viaNf3;
            Position p1 = start.after(mv(start, "Nf3"));
            p1 = p1.after(mv(p1, "Nf6"));
            const uint32_t x = t.ensurePath(start, {mv(start, "Nf3"), mv(start.after(mv(start, "Nf3")), "Nf6"), mv(p1, "Nc3")});
            Position p2 = start.after(mv(start, "Nc3"));
            p2 = p2.after(mv(p2, "Nf6"));
            const uint32_t y = t.ensurePath(start, {mv(start, "Nc3"), mv(start.after(mv(start, "Nc3")), "Nf6"), mv(p2, "Nf3")});
            Eval e;
            e.valid = true;
            e.score = -40; // Black to move: +0.40 for White
            e.depth = 20;
            t.setEval(x, e);
            const uint32_t root = t.roots()[0].node;
            check(x == y && t.node(root).score == 40 && t.size() == 6, "analyse approfondie : transposition fusionnée, minimax remonté (+0.40)");
            // Repetition: 1.Nf3 Nf6 2.Ng1 Ng8 comes back to the start
            Position r = start;
            std::vector<Move> back;
            for (const char* san : {"Nf3", "Nf6", "Ng1", "Ng8"}) {
                back.push_back(r.parseSan(san));
                r.makeMove(back.back());
            }
            t.ensurePath(start, back);
            t.minimaxAll();
            check(t.node(root).score == 40, "analyse approfondie : la répétition ne boucle pas (minimax inchangé)");
            // Red move: excluded from the minimax
            t.edge(root, start.parseSan("Nf3"))->mark = Mark::Exclude;
            t.edge(root, start.parseSan("Nc3"))->mark = Mark::Exclude;
            t.minimaxAll();
            check(t.node(root).score == Unknown || t.node(root).score == toCp(t.node(root).eval), "analyse approfondie : coups rouges exclus du minimax");
            Eval m1;
            m1.valid = true;
            m1.mate = true;
            m1.score = 1;
            Eval m0;
            m0.valid = true;
            m0.mate = true;
            check(toCp(m1) == MateValue - 1 && toCp(m0) == -MateValue && childEval([&] {
                      Eval z = m1;
                      z.pv = {start.legalMoves().front()};
                      return z;
                  }()).score == 0,
                  "analyse approfondie : scores de mat (mat en 1, mat subi, enfant mat)");
        }

        // Steps on the starting position
        Project pr;
        pr.tree.addRoot(Position::startPos());
        pr.options.limitWhite = pr.options.limitBlack = 3;
        pr.options.maxPlies = 6;
        pr.options.widthCp = 30;
        const int tasks = run(pr, 12);
        const uint32_t root = pr.tree.roots()[0].node;
        const deep::Node& rn = pr.tree.node(root);
        // depth = shortest path from the root (transpositions shorten it)
        int deepest = 0;
        {
            std::vector<int> dist(pr.tree.size(), -1);
            std::deque<uint32_t> bfs{root};
            dist[root] = 0;
            while (!bfs.empty()) {
                const uint32_t n = bfs.front();
                bfs.pop_front();
                deepest = std::max(deepest, dist[n]);
                for (const deep::Edge& e : pr.tree.node(n).edges)
                    if (dist[e.child] < 0) {
                        dist[e.child] = dist[n] + 1;
                        bfs.push_back(e.child);
                    }
            }
        }
        bool limits = true;
        for (uint32_t i = 0; i < pr.tree.size(); ++i) limits = limits && pr.tree.node(i).edges.size() <= 3;
        check(tasks > 20 && rn.edges.size() >= 2 && rn.edges.size() <= 3 && deepest >= 4 && deepest <= 6 && limits,
              "analyse approfondie : l'arbre grandit (" + std::to_string(tasks) + " tâches, " + std::to_string(pr.tree.size()) + " positions, profondeur " +
                  std::to_string(deepest) + "), limites d'alternatives et de longueur respectées");

        // Red move: no longer extended; green move: the only one followed
        {
            Project q;
            q.tree.addRoot(Position::startPos());
            q.options.maxPlies = 4;
            run(q, 3);
            const uint32_t r0 = q.tree.roots()[0].node;
            Edge* first = q.tree.node(r0).edges.empty() ? nullptr : &q.tree.node(r0).edges.front();
            const uint32_t excludedChild = first ? first->child : NoNode;
            if (first) first->mark = Mark::Exclude;
            const size_t before = excludedChild == NoNode ? 0 : q.tree.node(excludedChild).edges.size();
            run(q, 6);
            const bool frozen = excludedChild != NoNode && q.tree.node(excludedChild).edges.size() == before;
            // green on the last move of the root: the others no longer grow
            Edge& green = q.tree.node(r0).edges.back();
            green.mark = Mark::Focus;
            std::vector<size_t> sizes;
            for (const Edge& e : q.tree.node(r0).edges) sizes.push_back(q.tree.node(e.child).edges.size());
            const uint32_t greenChild = green.child;
            const size_t greenBefore = q.tree.node(greenChild).edges.size();
            run(q, 6);
            bool othersFrozen = true;
            for (size_t i = 0; i < q.tree.node(r0).edges.size() && i < sizes.size(); ++i)
                if (q.tree.node(r0).edges[i].child != greenChild && q.tree.node(q.tree.node(r0).edges[i].child).edges.size() != sizes[i]) othersFrozen = false;
            check(frozen && othersFrozen && q.tree.node(greenChild).edges.size() >= greenBefore,
                  "analyse approfondie : coup rouge plus prolongé, coup vert seul suivi");
        }

        // Preferred side (White): less analysis in the white moves that are not the best
        {
            auto sideBranches = [&](Side side) {
                Project q;
                q.tree.addRoot(Position::startPos());
                q.options.preferred = side;
                q.options.maxPlies = 5;
                run(q, 10);
                q.tree.minimaxAll();
                const uint32_t r0 = q.tree.roots()[0].node;
                const deep::Edge* best = q.tree.bestEdge(r0);
                uint32_t others = 0;
                for (const deep::Edge& e : q.tree.node(r0).edges)
                    if (&e != best) others += q.tree.node(e.child).positions;
                return std::pair<uint32_t, size_t>(others, q.tree.node(r0).edges.size());
            };
            const auto [preferred, rootMoves] = sideBranches(Side::White);
            const auto [neutral, rootMovesNeutral] = sideBranches(Side::None);
            check(preferred < neutral && rootMoves >= 2,
                  "analyse approfondie : camp préféré, moins d'analyse des coups blancs secondaires (" + std::to_string(preferred) + " positions contre " +
                      std::to_string(neutral) + ")");
        }

        // False alarm: an alternative better than the best move is checked right away
        {
            Project q;
            q.tree.addRoot(Position::startPos());
            const uint32_t r0 = q.tree.roots()[0].node;
            Eval e;
            e.valid = true;
            e.score = 20;
            e.depth = 10;
            e.pv = {Position::startPos().parseSan("e4")};
            Task t;
            q.applyResult(t, e); // root evaluated
            Task ext;
            ext.path = {Position::startPos().parseSan("e4")};
            Eval child;
            child.valid = true;
            child.score = -20;
            child.depth = 10;
            q.applyResult(ext, child); // 1.e4 : +0.20
            Task alt;
            alt.kind = TaskKind::Alternative;
            Eval better;
            better.valid = true;
            better.score = 80;
            better.depth = 10;
            better.pv = {Position::startPos().parseSan("d4")};
            const auto follow = q.applyResult(alt, better);
            check(q.tree.node(r0).edges.size() == 2 && follow.size() == 1 && follow[0].kind == TaskKind::AutoPlay && q.takeNext()->kind == TaskKind::AutoPlay,
                  "analyse approfondie : fausse alerte vérifiée en priorité (auto-play)");
            // Manual task before everything else
            Task manual;
            manual.manual = true;
            manual.path = {Position::startPos().parseSan("c4")};
            q.generateStage();
            q.addTask(manual);
            check(q.takeNext()->manual, "analyse approfondie : tâche manuelle traitée en premier");
        }

        // EPD mode: positions evaluated, no expansion; results given (ce, acd, pv)
        {
            Project q;
            q.options.autoExpand = false;
            const int n = q.addEpd("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - id \"e4\";\n"
                                   "8/8/8/8/8/8/5k2/7K b - - 0 1\n",
                                   false);
            run(q, 3);
            bool noExpansion = true;
            for (uint32_t i = 0; i < q.tree.size(); ++i) noExpansion = noExpansion && q.tree.node(i).edges.empty();
            Project r;
            r.addEpd("4k3/8/8/8/8/8/8/4K2R w K - ce 520; acd 30; pv Rh8+;\n", true);
            const deep::Node& rr = r.tree.node(r.tree.roots()[0].node);
            check(n == 2 && noExpansion && q.tree.node(q.tree.roots()[0].node).eval.valid && q.tree.roots()[0].comment == "e4" && rr.eval.valid &&
                      rr.eval.score == 520 && rr.eval.depth == 30 && rr.eval.pv.size() == 1,
                  "analyse approfondie : mode EPD (sans expansion) et résultats EPD (ce, acd, pv)");
        }

        // .hy64deep file: round trip
        {
            const std::string path = (std::filesystem::temp_directory_path() / "hydra64-test.hy64deep").string();
            pr.meta = "{\"name\":\"essai\"}";
            pr.options.thematic = {"Bxh7"};
            pr.tree.node(root).comment = "départ";
            pr.tree.node(root).note = NoteList::Critical;
            Task pending;
            pending.manual = true;
            pending.path = {Position::startPos().parseSan("g4")};
            pr.addTask(pending);
            std::string err;
            Project back;
            const bool saved = pr.save(path, &err) && back.load(path, &err);
            bool same = saved && back.tree.size() == pr.tree.size() && back.queue().size() == pr.queue().size() && back.meta == pr.meta &&
                        back.options.thematic == pr.options.thematic && back.options.maxPlies == 6;
            for (uint32_t i = 0; same && i < pr.tree.size(); ++i) {
                const deep::Node& a = pr.tree.node(i);
                const deep::Node& b = back.tree.node(i);
                same = a.key == b.key && a.score == b.score && a.edges.size() == b.edges.size() && a.eval.pv == b.eval.pv && a.comment == b.comment &&
                       a.note == b.note;
            }
            check(same, "analyse approfondie : projet .hy64deep enregistré et relu à l'identique (" + err + ")");
            std::filesystem::remove(path);
        }
    }

    // --- Syzygy endgame tablebases (Fathom): 3-piece tables of the tests
    {
        const int n = chess::tb::init(HYDRA64_SOURCE_DIR "/tests/data/syzygy");
        Position kq, kqb, stale;
        kq.setFen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
        kqb.setFen("4k3/8/8/8/8/8/8/3QK3 b - - 0 1");
        stale.setFen("4k3/4P3/4K3/8/8/8/8/8 b - - 0 1");
        const chess::tb::Probe w = chess::tb::probe(kq), b = chess::tb::probe(kqb), st = chess::tb::probe(stale);
        bool sorted = w.ok && !w.moves.empty() && w.moves.front().wdl == 2;
        for (size_t i = 1; sorted && i < w.moves.size(); ++i) sorted = w.moves[i - 1].wdl >= w.moves[i].wdl;
        check(n == 3 && w.ok && w.wdl == 2 && w.dtz > 0 && sorted && b.ok && b.wdl == -2 && st.ok && st.wdl == 0 && st.moves.empty(),
              "tables de finales : roi et dame gagnent (DTZ " + std::to_string(w.dtz) + "), perte vue des Noirs, pat");
        chess::tb::init("");
    }

    std::printf("\n%s\n", failures ? "ÉCHECS DÉTECTÉS" : "Tous les tests passent.");
    {
        // EPD (open format) → PGN: solution as main line, bm / am as variations, ops as a comment
        const std::string text =
            "# commentaire\n"
            "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - bm Rd8#; id \"mat; 1\"; dm 1;\n"
            "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - bm Bb5 Bc4; am g4; ce 30; pv \"Bb5 a6 Ba4\"; fmvn 3;\n"
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1\n"
            "ceci n'est pas une position\n";
        int positions = 0, skipped = 0;
        const std::string pgn = chess::epd::toPgn(text, "essai.epd", &positions, &skipped);
        const auto entries = chess::pgn::indexDatabase(pgn);
        chess::Game g1, g2, g3;
        const bool parsed = entries.size() == 3 && chess::pgn::parseGame(std::string_view(pgn).substr(entries[0].offset, entries[0].length), g1)
                            && chess::pgn::parseGame(std::string_view(pgn).substr(entries[1].offset, entries[1].length), g2)
                            && chess::pgn::parseGame(std::string_view(pgn).substr(entries[2].offset, entries[2].length), g3);
        check(positions == 3 && skipped == 1 && parsed, "EPD : 3 positions lues, 1 ligne refusée, PGN relu");
        if (parsed) {
            const auto m1 = g1.mainline();
            check(g1.header("Event") == "mat; 1" && m1.size() == 1 && m1[0]->san == "Rd8#" && m1[0]->pos.isCheckmate(),
                  "EPD : id (avec « ; » entre guillemets) en Event, bm en ligne principale");
            const auto m2 = g2.mainline();
            const chess::Node* root = g2.root();
            bool avoidMarked = false, otherBest = false;
            for (size_t i = 1; i < root->children.size(); ++i) {
                if (root->children[i]->san == "g4") avoidMarked = !root->children[i]->nags.empty() && root->children[i]->nags[0] == 2;
                if (root->children[i]->san == "Bc4") otherBest = true;
            }
            check(m2.size() == 3 && m2[0]->san == "Bb5" && m2[2]->san == "Ba4" && otherBest && avoidMarked && g2.startPosition().fullmoveNumber() == 3
                      && root->comment.find("ce 30") != std::string::npos,
                  "EPD : pv en ligne principale, autre bm et am « ? » en variantes, fmvn, ce en commentaire");
            check(g3.mainline().empty() && g3.startPosition().sideToMove() == chess::Black && g3.header("Event") == "Position 3",
                  "EPD : ligne FEN complète sans opération");
        }
        // Built-in training databases (databases/training, Hydra64 format): each exercise
        // is a readable game (position, solution); a mate in N ends with the mate on the right move.
        std::map<std::string, std::string> epdBySet; // EPD rebuilt per category (round trip below)
        int all = 0, bad = 0;
        std::map<std::string, int> perCategory;
        for (const char* name : {"mates/mate_in_1", "mates/mate_in_2", "mates/mate_in_3", "mates/mate_in_4", "mates/mate_in_5", "best_moves/best_move_easy",
                                 "best_moves/best_move_medium", "best_moves/best_move_hard"}) {
            hy64db::Database db;
            std::string err;
            if (!db.open(std::string(HYDRA64_SOURCE_DIR) + "/databases/training/" + name + ".hy64", &err)) {
                ++bad;
                continue;
            }
            for (uint32_t id : db.allGames()) {
                chess::Game g;
                ++all;
                if (!db.game(id, g) || g.mainline().empty() || std::atoi(g.header("WhiteElo").c_str()) <= 0) {
                    ++bad;
                    continue;
                }
                const std::string category = g.header("Event");
                ++perCategory[category];
                epdBySet[category] += chess::epd::fromGame(g) + "\n";
                if (category.rfind("Mat en ", 0) == 0) {
                    const int n = std::atoi(category.c_str() + 7);
                    const auto line = g.mainline();
                    const bool dm = g.root()->comment == "dm " + std::to_string(n);
                    if (!dm || int(line.size()) != 2 * n - 1 || !line.back()->pos.isCheckmate()) ++bad;
                }
            }
        }
        check(all == 3565 && bad == 0 && perCategory["Mat en 2"] == 500 && perCategory["Mat en 5"] == 165 && perCategory["Meilleur coup, difficile"] == 600,
              "bases d'entraînement : " + std::to_string(all) + " exercices, " + std::to_string(perCategory.size()) + " catégories, chaque mat vérifié, "
                  + std::to_string(bad) + " défaut(s)");
        // Round trip EPD → games → EPD: stable, and the useful operations are kept
        {
            int lines = 0, lost = 0;
            bool stable = true;
            for (const char* name : {"Mat en 3", "Meilleur coup, moyen"}) {
                const std::string original = epdBySet[name];
                const std::string once = chess::epd::fromPgn(chess::epd::toPgn(original, name));
                const std::string twice = chess::epd::fromPgn(chess::epd::toPgn(once, name));
                stable = stable && once == twice;
                std::string_view a(original), b(once);
                while (!a.empty() && !b.empty()) {
                    const std::string_view la = a.substr(0, a.find('\n')), lb = b.substr(0, b.find('\n'));
                    a.remove_prefix(std::min(a.size(), la.size() + 1));
                    b.remove_prefix(std::min(b.size(), lb.size() + 1));
                    chess::epd::Record ra, rb;
                    if (!chess::epd::parseLine(la, ra) || !chess::epd::parseLine(lb, rb)) {
                        ++lost;
                        continue;
                    }
                    ++lines;
                    for (const char* code : {"bm", "id", "pv", "dm"}) {
                        const std::string* x = ra.op(code);
                        const std::string* y = rb.op(code);
                        if ((x == nullptr) != (y == nullptr) || (x && *x != *y)) ++lost;
                    }
                    if (ra.fen != rb.fen) ++lost;
                }
            }
            check(stable && lines == 1100 && lost == 0, "EPD : aller-retour parties ↔ EPD stable sur " + std::to_string(lines) + " positions, " + std::to_string(lost) + " écart(s)");
        }
        {
            // Edited game → EPD: bm / am of the moves, pv, id, free comment as c0, counters
            chess::Game g(chess::Position::startPos());
            g.setHeader("Event", "Ouverture; test");
            chess::Node* e4 = g.addMove(g.root(), g.startPosition().parseSan("e4"));
            g.addMove(e4, e4->pos.parseSan("e5"));
            chess::Node* f3 = g.addMove(g.root(), g.startPosition().parseSan("f3"));
            f3->nags.push_back(2);
            g.root()->comment = "Le roi des coups";
            const std::string line = chess::epd::fromGame(g);
            chess::epd::Record r;
            const bool ok = chess::epd::parseLine(line, r) && r.op("bm") && *r.op("bm") == "e4" && r.op("am") && *r.op("am") == "f3" && r.op("pv")
                            && *r.op("pv") == "e4 e5" && r.op("id") && *r.op("id") == "Ouverture; test" && r.op("c0") && *r.op("c0") == "Le roi des coups";
            check(ok, "EPD : partie → ligne EPD (bm, am « ? », pv, id avec « ; », commentaire en c0) : " + line);
        }
    }
    // Beauty mark of a game (db/beauty.h): sacrifices, mate, from the moves alone
    {
        namespace fs = std::filesystem;
        const std::string dir = (fs::temp_directory_path() / ("hy64db-beauty-" + std::to_string(std::rand()))).string();
        fs::create_directories(dir);
        const std::string opera = "1. e4 e5 2. Nf3 d6 3. d4 Bg4 4. dxe5 Bxf3 5. Qxf3 dxe5 6. Bc4 Nf6 7. Qb3 Qe7 8. Nc3 c6 9. Bg5 b5 10. Nxb5 cxb5 "
                                  "11. Bxb5+ Nbd7 12. O-O-O Rd8 13. Rxd7 Rxd7 14. Rd1 Qe6 15. Bxd7+ Nxd7 16. Qb8+ Nxb8 17. Rd8# ";
        const std::string quiet = "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Bxc6 dxc6 5. O-O f6 6. d4 exd4 7. Nxd4 c5 8. Nb3 Qxd1 9. Rxd1 Bg4 10. f3 Be6 11. Nc3 Bd6 12. Be3 b6 ";
        {
            std::ofstream f(dir + "/beaute.pgn", std::ios::binary);
            f << "[White \"Morphy\"]\n[Black \"Duc\"]\n[Result \"1-0\"]\n\n" << opera << "1-0\n\n"
              << "[White \"Calme\"]\n[Black \"Plat\"]\n[Result \"1-0\"]\n\n" << quiet << "1-0\n\n"
              << "[White \"Morphy\"]\n[Black \"Nulle\"]\n[Result \"1/2-1/2\"]\n\n" << opera << "1/2-1/2\n\n"
              << "[White \"Morphy\"]\n[Black \"Sans\"]\n[Result \"*\"]\n\n" << opera << "*\n";
        }
        hy64db::PgnSource src;
        hy64db::ConvertResult res;
        std::string err;
        hy64db::Database db;
        const bool ok = src.open(dir + "/beaute.pgn", &err) && hy64db::convert(src, dir + "/beaute", {}, &res, &err) && db.open(dir + "/beaute.hy64i", &err) && db.count() == 4;
        int marks[4] = {-2, -2, -2, -2};
        for (uint32_t id = 0; ok && id < 4; ++id) marks[id] = hy64db::beautyOf(db.record(id).flags);
        check(ok && marks[0] == 3 && marks[1] == 0 && marks[2] == 0 && marks[3] == 3,
              "beauté : la partie de l'Opéra reçoit 3 étoiles dès la conversion, une partie calme ou une nulle aucune ; sans résultat, le mat désigne le vainqueur (" +
                  std::to_string(marks[0]) + " " + std::to_string(marks[1]) + " " + std::to_string(marks[2]) + " " + std::to_string(marks[3]) + ")");
        if (ok) {
            const uint16_t before = db.record(1).flags;
            const int64_t again = db.rateBeauty(&err, {}, true);
            std::vector<uint32_t> ids = db.allGames();
            db.sort(ids, hy64db::ColBeauty, false);
            const bool sorted = ids.size() == 4 && ids[0] == 0 && ids[1] == 3;
            check(again == 0 && db.unratedBeauty() == 0 && db.record(1).flags == before && sorted,
                  "beauté : réévaluer une base déjà notée ne change rien ; tri par beauté, les plus belles d'abord");
            hy64db::BeautyRater rater;
            rater.start();
            chess::Game og;
            db.game(0, og);
            const chess::Position* at = &og.startPosition();
            for (const chess::Node* n : og.mainline()) {
                rater.move(*at, n->move);
                at = &n->pos;
            }
            check(rater.points(hy64db::WhiteWins) >= 9 && rater.points(hy64db::BlackWins) == 0 && rater.points(hy64db::Draw) == 0 && hy64db::BeautyRater::medals(2) == 0 &&
                      hy64db::BeautyRater::medals(3) == 1 && hy64db::BeautyRater::medals(5) == 2 && hy64db::BeautyRater::medals(9) == 3,
                  "beauté : points de la partie de l'Opéra (" + std::to_string(rater.points(hy64db::WhiteWins)) + "), rien pour le camp qui perd, seuils des étoiles 3 / 5 / 9");
        }
        db.close();
        fs::remove_all(dir);
    }
    return failures ? 1 : 0;
}
