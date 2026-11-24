#include "tournamenthtml.h"

#include "boardtheme.h"
#include "display.h"
#include "chess/pgn.h"
#include "piecerenderer.h"
#include "tournament.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <functional>

using namespace chess;

namespace tournamentHtml {

namespace {

QString esc(const QString& s) { return s.toHtmlEscaped(); }

// Piece placement of a position (first field of a FEN).
QString placement(const Position& pos)
{
    const std::string fen = pos.fen();
    return QString::fromStdString(fen.substr(0, fen.find(' ')));
}

// Arrows and marked squares of a position: [from, to (-1: marked square), colour].
QJsonArray graphicsArray(const std::vector<Graphic>& graphics)
{
    QJsonArray out;
    for (const Graphic& g : graphics) out.append(QJsonArray{int(g.from), g.isArrow() ? int(g.to) : -1, QString(QChar(g.color))});
    return out;
}

// A game for the page: starting position and "ready to play" moves — the page's script
// needs to know no rule of the game, it moves the pieces it is told to.
// Moves are in depth-first order, the main continuation first:
//   [English notation, from, to, promotion, special, {extras}]
//   special: [square] = piece captured en passant; [from, to] = castling rook
//   extras (absent if empty): p = previous move when it is not the one before
//     in the list (variations), e = evaluation (centipawns from White's point of view, or
//     "#n"), d = depth, t = time in seconds, k = clock in ms, c = comment, g = NAG,
//     h = arrows and marked squares
// "k" of the game: number of book half-moves at the head of the main line.
QJsonObject gameObject(const Game& game, int libraryPlies)
{
    QJsonObject o;
    const Position& start = game.startPosition();
    o["f"] = placement(start);
    o["s"] = QJsonArray{start.fullmoveNumber(), start.sideToMove() == Black ? 1 : 0};
    static const QRegularExpression note(QStringLiteral(R"((?:([+-]\d+\.\d\d)|#(-?\d+))/(\d+) (\d+(?:\.\d+)?)s)"));
    static const QRegularExpression evalTag(QStringLiteral(R"(\[%eval (?:(#-?\d+)|(-?\d+(?:\.\d+)?))[^\]]*\])"));
    static const QRegularExpression anyTag(QStringLiteral(R"(\[%[^\]]*\])"));
    static const QStringList libraryMarks = {QStringLiteral("livre"), QStringLiteral("book"), QCoreApplication::translate("Tournament", "livre")};
    static const QStringList libraryEnds = {QStringLiteral("Fin des coups de la base"), QStringLiteral("End of book moves"),
                                            QStringLiteral("Position de départ de la base (sans coups) : fin des coups de la base"),
                                            QStringLiteral("Start position from the book (no moves): end of book moves")};
    QJsonArray moves;
    int library = libraryPlies;
    std::function<void(const Node*, int, int, bool)> emitNode = [&](const Node* n, int parentIndex, int depth, bool mainline) {
        const int index = int(moves.size());
        const Position& before = n->parent->pos;
        const Move m = n->move;
        QJsonArray a;
        a.append(QString::fromStdString(n->san));
        if (m.isNull()) {
            a.append(-1), a.append(-1), a.append(QString()), a.append(0);
        } else {
            const Piece moved = before.at(m.from);
            const Color us = colorOf(moved);
            QString promo;
            QJsonValue special = 0;
            int to = m.to;
            if (before.isCastling(m)) { // king to g or c, rook to f or d (Chess960 included)
                const bool kingside = before.isKingsideCastling(m);
                const int rank = rankOf(m.from);
                to = makeSquare(kingside ? 6 : 2, rank);
                special = QJsonArray{int(before.castlingRook(us, kingside)), int(makeSquare(kingside ? 5 : 3, rank))};
            } else if (typeOf(moved) == Pawn && fileOf(m.from) != fileOf(m.to) && before.at(m.to) == NoPiece) {
                special = QJsonArray{int(makeSquare(fileOf(m.to), rankOf(m.from)))};
            }
            if (m.promo != NoType) {
                const char letters[] = {' ', 'p', 'n', 'b', 'r', 'q', 'k'};
                const char c = letters[m.promo];
                promo = QChar(us == White ? char(std::toupper(c)) : c);
            }
            a.append(int(m.from)), a.append(to), a.append(promo), a.append(special);
        }
        QJsonObject x;
        if (parentIndex != index - 1) x["p"] = parentIndex;
        // Comment: engine note ("+0.23/31 2.6s"), [%eval …] and [%clk …] tags, text
        QString comment = QString::fromStdString(n->comment);
        if (const auto match = note.match(comment); match.hasMatch()) {
            if (!match.captured(1).isEmpty()) x["e"] = int(std::lround(match.captured(1).toDouble() * 100));
            else x["e"] = QStringLiteral("#") + match.captured(2);
            x["d"] = match.captured(3).toInt();
            x["t"] = match.captured(4).toDouble();
            comment.remove(match.capturedStart(), match.capturedLength());
        } else if (const auto tag = evalTag.match(comment); tag.hasMatch()) {
            if (!tag.captured(1).isEmpty()) x["e"] = tag.captured(1);
            else x["e"] = int(std::lround(tag.captured(2).toDouble() * 100));
        } else if (n->eval.valid) {
            if (n->eval.mate) x["e"] = QStringLiteral("#%1").arg(n->eval.value);
            else x["e"] = n->eval.value;
            if (n->eval.depth > 0) x["d"] = n->eval.depth;
        }
        comment.remove(anyTag);
        comment = comment.simplified();
        // ChessBase private-use characters (figurines, signs): translated here, pieces as English letters like the moves;
        // the page's script then rewrites the quoted moves in the displayed language
        comment = display::comment(comment, prefs::Notation::English);
        if (libraryMarks.contains(comment)) {
            if (mainline) library = std::max(library, depth);
            comment.clear();
        } else if (libraryEnds.contains(comment)) {
            if (mainline) library = std::max(library, depth);
            comment.clear();
        }
        if (!comment.isEmpty()) x["c"] = comment;
        // Pre-move comment ("b": before), written in front of the move by the script
        if (QString pre = display::comment(QString::fromStdString(n->preComment).remove(anyTag).simplified(), prefs::Notation::English); !pre.isEmpty()) x["b"] = pre;
        if (n->clockMs >= 0) x["k"] = n->clockMs;
        if (!n->graphics.empty()) x["h"] = graphicsArray(n->graphics);
        if (!n->nags.empty()) {
            QJsonArray nags;
            for (int nag : n->nags) nags.append(nag);
            x["g"] = nags;
        }
        if (!x.isEmpty()) a.append(x);
        moves.append(a);
        for (size_t i = 0; i < n->children.size(); ++i) emitNode(n->children[i].get(), index, depth + 1, mainline && i == 0);
    };
    const Node* root = game.root();
    for (size_t i = 0; i < root->children.size(); ++i) emitNode(root->children[i].get(), -1, 1, i == 0);
    o["m"] = moves;
    const auto mainline = game.mainline();
    if (library > 0) o["k"] = std::min<int>(library, int(mainline.size()));
    QString rootComment = QString::fromStdString(root->comment);
    rootComment.remove(anyTag);
    rootComment = display::comment(rootComment.simplified(), prefs::Notation::English);
    if (!rootComment.isEmpty() && !libraryEnds.contains(rootComment)) o["c"] = rootComment;
    if (!root->graphics.empty()) o["h"] = graphicsArray(root->graphics);
    o["e"] = placement(mainline.empty() ? start : mainline.back()->pos); // end of the main line: used to check the replay
    return o;
}

// Game of a tournament: names, result, reason, moves imposed by the starting positions file.
QJsonObject tournamentGame(int number, const Tournament::GameRecord& rec)
{
    Game game;
    int library = 0;
    if (const qsizetype at = rec.opening.indexOf(QLatin1String(" moves ")); at >= 0) library = int(rec.opening.mid(at + 7).split(QLatin1Char(' '), Qt::SkipEmptyParts).size());
    QJsonObject o;
    if (pgn::parseGame(rec.pgn.toStdString(), game)) o = gameObject(game, library);
    o["n"] = number;
    o["w"] = rec.whiteName;
    o["b"] = rec.blackName;
    o["r"] = rec.result;
    o["y"] = rec.reason;
    if (rec.round) o["o"] = rec.round;
    return o;
}

// PNG image of a piece of the current set, as "data:".
QString pieceData(PieceRenderer& renderer, Piece piece)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    renderer.pixmap(piece, 120, 1.0).toImage().save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

QString points(double p)
{
    const int whole = int(p);
    return p - whole > 0.25 ? (whole ? QString::number(whole) + QStringLiteral("½") : QStringLiteral("½")) : QString::number(whole);
}

// Hydra64's logo (resources/hydra64.svg), written as it is in the page.
QString logoSvg()
{
    QFile f(QStringLiteral(":/style/hydra64.svg"));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

// The page's script. The functions at the beginning do not touch the document: the tests
// run them on their own.
const char* const Script = R"JS(
function startBoard(placement) {
  const b = new Array(64).fill('');
  let rank = 7, file = 0;
  for (const c of placement) {
    if (c === '/') { rank--; file = 0; }
    else if (c >= '1' && c <= '8') file += +c;
    else b[rank * 8 + file++] = c;
  }
  return b;
}
function applyMove(b, m) {
  const n = b.slice(), from = m[1], to = m[2], special = m[4];
  if (from < 0) return n;                       // coup nul
  if (special && special.length === 2) {        // roque : roi et tour
    const king = n[from], rook = n[special[0]];
    n[from] = ''; n[special[0]] = '';
    n[to] = king; n[special[1]] = rook;
    return n;
  }
  if (special && special.length === 1) n[special[0]] = '';  // prise en passant
  n[to] = m[3] || n[from];
  n[from] = '';
  return n;
}
// Arbre d'une partie : pour chaque coup son précédent (-1 : position de départ), ses suites
// (la principale d'abord), sa profondeur et la position qu'il donne ; « main » : ligne principale.
function gameTree(game) {
  const moves = game.m || [], n = moves.length;
  const par = new Array(n), depth = new Array(n), kids = Array.from({ length: n + 1 }, () => []);
  const boards = [startBoard(game.f || 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR')];
  for (let i = 0; i < n; i++) {
    const x = moves[i][5] || {}, p = x.p !== undefined ? x.p : i - 1;
    par[i] = p; depth[i] = p < 0 ? 1 : depth[p] + 1;
    kids[p + 1].push(i);
    boards[i + 1] = applyMove(boards[p + 1], moves[i]);
  }
  const main = [];
  for (let c = kids[0][0]; c !== undefined; c = kids[c + 1][0]) main.push(c);
  return { par, depth, kids, boards, main };
}
function boardPlacement(b) {
  let out = '';
  for (let rank = 7; rank >= 0; rank--) {
    let empty = 0;
    for (let file = 0; file < 8; file++) {
      const c = b[rank * 8 + file];
      if (!c) { empty++; continue; }
      if (empty) { out += empty; empty = 0; }
      out += c;
    }
    if (empty) out += empty;
    if (rank) out += '/';
  }
  return out;
}
function finalPlacement(game) { const t = gameTree(game); return boardPlacement(t.boards[t.main.length ? t.main[t.main.length - 1] + 1 : 0]); }
function evalValue(e) {                          // en pions, borné, pour la courbe
  if (e === null || e === undefined) return null;
  if (typeof e === 'string') return e.indexOf('-') >= 0 ? -6 : 6;
  return Math.max(-6, Math.min(6, e / 100));
}
function evalText(e) {
  if (e === null || e === undefined) return '';
  if (typeof e === 'string') return e;
  return (e > 0 ? '+' : e < 0 ? '−' : '') + (Math.abs(e) / 100).toFixed(2);
}
// Notation : les données sont en lettres anglaises (K Q R B N) ; en français R D T F C.
function sanText(san, lang) {
  if (lang !== 'fr') return san;
  const fr = { K: 'R', Q: 'D', R: 'T', B: 'F', N: 'C' };
  return san.replace(/[KQRBN]/g, c => fr[c]);
}
// Coups cités dans un commentaire (« Qh5 », « Nbd7 », « exd8=Q ») : réécrits comme ceux de la partie.
// Seuls les mots qui ont la forme exacte d'un coup sont touchés.
function cmtText(text, lang) {
  if (lang !== 'fr') return text;
  return text.replace(/(^|[^A-Za-z0-9])((?:[KQRBN][a-h]?[1-8]?[x-]?[a-h][1-8]|(?:[a-h]x)?[a-h][18]=[QRBN]))(?![A-Za-z0-9=])/g, (all, before, move) => before + sanText(move, lang));
}
function nagText(nags) {
  const sym = { 1: '!', 2: '?', 3: '!!', 4: '??', 5: '!?', 6: '?!', 10: ' =', 13: ' ∞', 14: ' ⩲', 15: ' ⩱', 16: ' ±', 17: ' ∓', 18: ' +−', 19: ' −+' };
  return (nags || []).map(n => sym[n] || ' $' + n).join('');
}
function clockText(ms) {
  const s = Math.floor(ms / 1000), h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, pad = v => (v < 10 ? '0' : '') + v;
  return (h ? h + ':' + pad(m) : m) + ':' + pad(s % 60);
}
// Date sans dépendre du navigateur : « 20 novembre 2026 » / « November 20, 2026 » ; mois ou jour
// inconnus (0) omis.
var MONTHS = {
  fr: ['janvier', 'février', 'mars', 'avril', 'mai', 'juin', 'juillet', 'août', 'septembre', 'octobre', 'novembre', 'décembre'],
  en: ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'],
};
function dateText(y, m, d, lang) {
  if (!y) return '';
  if (!m || m < 1 || m > 12) return String(y);
  const month = MONTHS[lang === 'fr' ? 'fr' : 'en'][m - 1];
  if (lang === 'fr') return (d ? (d === 1 ? '1er' : d) + ' ' : '') + month + ' ' + y;
  return month + (d ? ' ' + d + ',' : '') + ' ' + y;
}
function pgnDateText(text, lang) {               // « 2026.11.20 », « 1851.??.?? »
  const p = String(text || '').split('.').map(v => parseInt(v, 10) || 0);
  return dateText(p[0], p[1], p[2], lang);
}
// Motif de fin de partie : enregistré dans la langue de l'application au moment de la partie.
var REASONS = [
  ['Échec et mat', 'Checkmate'], ['Pat', 'Stalemate'], ['Matériel insuffisant', 'Insufficient material'], ['Règle des 50 coups', '50-move rule'],
  ['Triple répétition', 'Threefold repetition'], ['Temps écoulé', 'Time out'], ['Temps écoulé — matériel insuffisant pour mater', 'Time out — insufficient mating material'],
  ['Le moteur ne répond plus', 'The engine no longer responds'], ["Le moteur s'est arrêté", 'The engine stopped'], ['Moteur indisponible', 'Engine unavailable'],
  ['Impossible de lancer le moteur', 'Could not start the engine'], ['Abandon', 'Resignation'],
  ['Arbitrage : tables de finales (gain des Blancs)', 'Adjudication: endgame tables (White wins)'], ['Arbitrage : tables de finales (gain des Noirs)', 'Adjudication: endgame tables (Black wins)'],
  ['Arbitrage : tables de finales (nulle)', 'Adjudication: endgame tables (draw)'],
  ['Arbitrage : tables de finales (nulle par la règle des 50 coups)', 'Adjudication: endgame tables (draw by the 50-move rule)'],
];
var REASON_RULES = [
  [/^Arbitrage : nulle \((.*)\)$/, 'Adjudication: draw', /^Adjudication: draw \((.*)\)$/, 'Arbitrage : nulle'],
  [/^Arbitrage : abandon des Noirs \((.*)\)$/, 'Adjudication: Black resigns', /^Adjudication: Black resigns \((.*)\)$/, 'Arbitrage : abandon des Noirs'],
  [/^Arbitrage : abandon des Blancs \((.*)\)$/, 'Adjudication: White resigns', /^Adjudication: White resigns \((.*)\)$/, 'Arbitrage : abandon des Blancs'],
  [/^Coup illégal \((.*)\)$/, 'Illegal move ($1)', /^Illegal move \((.*)\)$/, 'Coup illégal ($1)'],
  [/^Limite de (\d+) demi-coups$/, 'Limit of $1 plies', /^Limit of (\d+) plies$/, 'Limite de $1 demi-coups'],
];
function reasonText(reason, lang) {
  if (!reason) return '';
  for (const [fr, en] of REASONS) if (reason === fr || reason === en) return lang === 'fr' ? fr : en;
  for (const [frRe, en, enRe, fr] of REASON_RULES) {
    if (lang === 'en' && frRe.test(reason)) return reason.replace(frRe, en);
    if (lang === 'fr' && enRe.test(reason)) return reason.replace(enRe, fr);
  }
  return reason;
}
var TEXTS = {
  fr: { brand: 'Hydra64 · Tournoi de moteurs', gameBrand: 'Hydra64 · Partie', standings: 'Classement', cross: 'Tableau croisé', games: 'Parties', oneGame: 'Partie',
        engine: 'Moteur', points: 'Points', played: 'Parties', elo: 'Elo ±', num: 'N°', white: 'Blancs', black: 'Noirs', result: 'Résultat', moves: 'Coups', reason: 'Motif',
        eval: 'évaluation', depth: 'profondeur', clock: 'pendule', game: 'Partie', round: 'ronde', prevGame: 'Partie précédente', nextGame: 'Partie suivante',
        pgn: 'Télécharger le PGN', first: 'Début', prev: 'Coup précédent', next: 'Coup suivant', last: 'Fin', flip: "Retourner l'échiquier",
        libraryEnd: 'fin de la bibliothèque', libraryMove: 'coup de bibliothèque', rr: 'toutes rondes', gauntlet: 'gauntlet', swiss: 'système suisse',
        clockLimit: '{0} min + {1} s', movetime: '{0} s par coup', depthLimit: 'profondeur {0}', participants: '{0} participants', playedOf: '{0} parties jouées sur {1}',
        start: 'position de départ', roundN: 'ronde {0}', keysTitle: 'Clavier', kMoves: 'coup précédent / suivant', kGames: 'partie précédente / suivante',
        kEnds: 'début / fin de la partie', kFlip: "retourner l'échiquier", kHome: 'Début', kEnd: 'Fin', creditsTitle: 'Crédits', pieces: 'Pièces : ',
        made: 'Page autonome produite par Hydra64 : tout est dans ce fichier, rien n’est chargé d’Internet.', exported: 'Exportée le {0}', aboutTitle: 'À propos' },
  en: { brand: 'Hydra64 · Engine tournament', gameBrand: 'Hydra64 · Game', standings: 'Standings', cross: 'Cross table', games: 'Games', oneGame: 'Game',
        engine: 'Engine', points: 'Points', played: 'Games', elo: 'Elo ±', num: 'No.', white: 'White', black: 'Black', result: 'Result', moves: 'Moves', reason: 'Ending',
        eval: 'evaluation', depth: 'depth', clock: 'clock', game: 'Game', round: 'round', prevGame: 'Previous game', nextGame: 'Next game',
        pgn: 'Download the PGN', first: 'Start', prev: 'Previous move', next: 'Next move', last: 'End', flip: 'Flip the board',
        libraryEnd: 'end of book', libraryMove: 'book move', rr: 'round robin', gauntlet: 'gauntlet', swiss: 'Swiss system',
        clockLimit: '{0} min + {1} s', movetime: '{0} s per move', depthLimit: 'depth {0}', participants: '{0} participants', playedOf: '{0} games played out of {1}',
        start: 'start position', roundN: 'round {0}', keysTitle: 'Keyboard', kMoves: 'previous / next move', kGames: 'previous / next game',
        kEnds: 'start / end of the game', kFlip: 'flip the board', kHome: 'Home', kEnd: 'End', creditsTitle: 'Credits', pieces: 'Pieces: ',
        made: 'Standalone page produced by Hydra64: everything is in this file, nothing is loaded from the Internet.', exported: 'Exported on {0}', aboutTitle: 'About' },
};
// Flèches et cases marquées d'une position, en SVG par-dessus l'échiquier (repère : 8 × 8 cases).
function graphicsSvg(list, flipped) {
  if (!list || !list.length) return '';
  const colors = { G: '#5cb87e', R: '#e05a50', Y: '#e6be3c', B: '#6fa3d8' };
  const x = sq => (flipped ? 7 - sq % 8 : sq % 8) + 0.5, y = sq => (flipped ? Math.floor(sq / 8) : 7 - Math.floor(sq / 8)) + 0.5;
  let out = '';
  for (const [from, to, color] of list) {
    const c = colors[color] || colors.G;
    if (to < 0) { out += '<circle cx="' + x(from) + '" cy="' + y(from) + '" r="0.43" fill="none" stroke="' + c + '" stroke-width="0.07" opacity="0.85"/>'; continue; }
    const dx = x(to) - x(from), dy = y(to) - y(from), len = Math.hypot(dx, dy);
    if (len < 0.1) continue;
    const ux = dx / len, uy = dy / len, head = 0.38, tip = len - 0.12, base = tip - head;
    const px = k => (x(from) + ux * k).toFixed(3), py = k => (y(from) + uy * k).toFixed(3);
    const side = (k, w) => (x(from) + ux * k - uy * w).toFixed(3) + ',' + (y(from) + uy * k + ux * w).toFixed(3);
    out += '<g fill="' + c + '" stroke="' + c + '" opacity="0.8"><line x1="' + px(0) + '" y1="' + py(0) + '" x2="' + px(base) + '" y2="' + py(base) + '" stroke-width="0.16" stroke-linecap="round"/>'
         + '<polygon stroke="none" points="' + side(base, 0.21) + ' ' + px(tip) + ',' + py(tip) + ' ' + side(base, -0.21) + '"/></g>';
  }
  return '<svg class="gfx" viewBox="0 0 8 8">' + out + '</svg>';
}
function fill(text, ...args) { return text.replace(/\{(\d)\}/g, (_, i) => args[+i]); }
if (typeof document !== 'undefined') (function () {
  const D = JSON.parse(document.getElementById('data').textContent), G = D.games, I = D.info, single = D.mode === 'game';
  const $ = id => document.getElementById(id);
  let lang = 'en';
  try { const saved = localStorage.getItem('hydra64-lang'); if (saved === 'en' || saved === 'fr') lang = saved; } catch (e) {}
  let L = TEXTS[lang];
  let game = null, T = null, cur = -1, flipped = false, current = -1;
  const number = (v, digits) => { const t = (Math.round(v * Math.pow(10, digits)) / Math.pow(10, digits)).toString(); return lang === 'fr' ? t.replace('.', ',') : t; };
  function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;'); }
  const result = r => (r || '*').replace('1/2-1/2', '½-½');
  const extra = i => (i >= 0 && game.m[i][5]) || {};
  const depthOf = i => (i < 0 ? 0 : T.depth[i]);
  const onMain = i => i < 0 || T.main[T.depth[i] - 1] === i;

  function drawBoard() {
    const b = T.boards[cur + 1] || [], last = cur >= 0 ? game.m[cur] : null;
    let html = '';
    for (let row = 0; row < 8; row++) for (let col = 0; col < 8; col++) {
      const rank = flipped ? row : 7 - row, file = flipped ? 7 - col : col, sq = rank * 8 + file;
      const dark = (rank + file) % 2 === 0;
      const mark = last && last[1] >= 0 && (sq === last[1] || sq === last[2]) ? ' last' : '';
      const p = b[sq] ? '<i class="p ' + (b[sq] === b[sq].toUpperCase() ? 'w' : 'b') + b[sq].toUpperCase() + '"></i>' : '';
      const coordFile = row === 7 ? '<u class="cf">' + 'abcdefgh'[file] + '</u>' : '';
      const coordRank = col === 0 ? '<u class="cr">' + (rank + 1) + '</u>' : '';
      html += '<div class="sq ' + (dark ? 'dk' : 'lt') + mark + '">' + coordRank + coordFile + p + '</div>';
    }
    $('board').innerHTML = html + graphicsSvg(cur >= 0 ? extra(cur).h : game.h, flipped);
    $('top').textContent = flipped ? game.w : game.b;
    $('bottom').textContent = flipped ? game.b : game.w;
    const x = extra(cur), library = cur >= 0 && onMain(cur) && T.depth[cur] <= (game.k || 0);
    const parts = [];
    if (cur < 0) parts.push(L.start);
    else {
      if (x.e !== undefined) parts.push(L.eval + ' ' + evalText(x.e) + (x.d ? ' · ' + L.depth + ' ' + x.d : '') + (x.t ? ' · ' + number(x.t, 1) + ' s' : ''));
      else if (library) parts.push(L.libraryMove);
      if (x.k !== undefined) parts.push(L.clock + ' ' + clockText(x.k));
    }
    $('info').textContent = parts.join(' · ') || ' ';
    $('info').className = library ? 'lib' : '';
    $('pos').textContent = depthOf(cur) + (onMain(cur) ? ' / ' + T.main.length : '');
    document.querySelectorAll('#moves a.cur').forEach(a => a.classList.remove('cur'));
    const a = document.querySelector('#moves a[data-p="' + cur + '"]');
    if (a) { a.classList.add('cur'); a.scrollIntoView({ block: 'nearest' }); }
    drawChart();
  }
  function drawChart() {
    const c = $('chart'), ctx = c.getContext('2d'), w = c.width = c.clientWidth * 2, h = c.height = c.clientHeight * 2;
    ctx.clearRect(0, 0, w, h);
    const n = T.main.length, css = name => getComputedStyle(document.body).getPropertyValue(name);
    const vals = T.main.map(i => evalValue(extra(i).e));
    if (vals.filter(v => v !== null).length < 2) { c.style.display = 'none'; return; }
    c.style.display = 'block';
    const x = i => (i / n) * w, y = v => h / 2 - (v / 6) * (h / 2 - 4);
    if (game.k) { ctx.fillStyle = css('--raised'); ctx.fillRect(0, 0, x(game.k), h); }   // coups de bibliothèque
    ctx.strokeStyle = css('--text-faint'); ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2); ctx.stroke();
    ctx.strokeStyle = css('--accent'); ctx.lineWidth = 3;
    ctx.beginPath();
    let started = false;
    vals.forEach((v, i) => { if (v === null) return; if (started) ctx.lineTo(x(i + 1), y(v)); else { ctx.moveTo(x(i + 1), y(v)); started = true; } });
    ctx.stroke();
    ctx.strokeStyle = css('--text'); ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(x(depthOf(cur)), 0); ctx.lineTo(x(depthOf(cur)), h); ctx.stroke();
  }
  function go(i) { if (i < -1 || i >= game.m.length) return; cur = i; drawBoard(); }
  const goNext = () => { const k = T.kids[cur + 1][0]; if (k !== undefined) go(k); };
  const goPrev = () => { if (cur >= 0) go(T.par[cur]); };
  const goEnd = () => { let i = cur; for (let k = T.kids[i + 1][0]; k !== undefined; k = T.kids[i + 1][0]) i = k; go(i); };

  // Texte des coups : ligne principale, variantes entre parenthèses, commentaires
  function drawMoves() {
    const s0 = game.s ? game.s[0] : 1, b0 = game.s ? game.s[1] : 0, library = game.k || 0, out = [];
    const moveHtml = (i, force, main) => {
      const d = T.depth[i], black = (b0 + d - 1) % 2 === 1, no = s0 + Math.floor((b0 + d - 1) / 2), x = extra(i);
      let h = '';
      if (x.b) h += '<span class="cmt">' + esc(cmtText(x.b, lang)) + '</span> ';
      if (!black) h += '<span class="no">' + no + '.</span>';
      else if (force || x.b) h += '<span class="no">' + no + '…</span>';
      h += '<a data-p="' + i + '"' + (main && d <= library ? ' class="lib"' : '') + '>' + esc(sanText(game.m[i][0], lang)) + esc(nagText(x.g)) + '</a> ';
      if (x.c) h += '<span class="cmt">' + esc(cmtText(x.c, lang)) + '</span> ';
      return h;
    };
    const line = (parent, force, main) => {
      let p = parent, need = force;
      for (;;) {
        const ks = T.kids[p + 1];
        if (!ks.length) break;
        const m0 = ks[0];
        out.push(moveHtml(m0, need, main));
        need = !!extra(m0).c;
        if (main && T.depth[m0] === library) { out.push('<span class="libend">' + L.libraryEnd + '</span> '); need = true; }
        for (let v = 1; v < ks.length; v++) {
          out.push('<span class="var">(' + moveHtml(ks[v], true, false));
          line(ks[v], !!extra(ks[v]).c, false);
          out.push(')</span> ');
          need = true;
        }
        p = m0;
      }
    };
    if (game.c) out.push('<span class="cmt">' + esc(cmtText(game.c, lang)) + '</span> ');
    line(-1, true, true);
    $('moves').innerHTML = out.join('').replace(/ \)<\/span>/g, ')</span>') + '<b class="res">' + result(game.r) + '</b>';
  }
  function drawGame() {
    drawMoves();
    if (single) $('title').textContent = result(game.r) + (game.y ? ' · ' + reasonText(game.y, lang) : '');
    else {
      $('title').textContent = L.game + ' ' + game.n + (game.o ? ' · ' + L.round + ' ' + game.o : '') + ' — ' + game.w + ' – ' + game.b;
      $('why').textContent = reasonText(game.y, lang);
      document.querySelectorAll('#games tr.sel').forEach(r => r.classList.remove('sel'));
      const row = document.querySelector('#games tr[data-g="' + current + '"]');
      if (row) { row.classList.add('sel'); row.scrollIntoView({ block: 'nearest' }); }
    }
    drawBoard();
  }
  function show(index) {
    if (index < 0 || index >= G.length) return;
    current = index; game = G[index]; game.m = game.m || [];
    T = gameTree(game); cur = -1;
    drawGame();
  }
  // Tout le texte de la page, dans la langue choisie
  function translate() {
    L = TEXTS[lang];
    document.documentElement.lang = lang;
    document.querySelectorAll('[data-i]').forEach(e => { e.innerHTML = L[e.dataset.i]; });
    document.querySelectorAll('[data-t]').forEach(e => { e.title = L[e.dataset.t]; });
    document.querySelectorAll('#lang button').forEach(b => b.classList.toggle('on', b.dataset.lang === lang));
    const day = I.date.split('-').map(v => +v);
    if (single) {
      const H = D.head;
      $('lead').textContent = [H.event, H.site, H.round ? fill(L.roundN, H.round) : '', pgnDateText(H.date, lang), H.eco, result(G[0].r)].filter(v => v).join(' · ');
    } else {
      const limit = I.limit[0] === 'clock' ? fill(L.clockLimit, number(I.limit[1], 2), number(I.limit[2], 2))
                  : I.limit[0] === 'movetime' ? fill(L.movetime, number(I.limit[1], 2)) : fill(L.depthLimit, I.limit[1]);
      $('lead').textContent = [dateText(day[0], day[1], day[2], lang), L[I.schedule], limit, fill(L.participants, I.players), fill(L.playedOf, I.played, I.total)].join(' · ');
      let rows = '';
      G.forEach((g, i) => {
        rows += '<tr data-g="' + i + '"><td class="num">' + g.n + '</td><td>' + esc(g.w) + '</td><td>' + esc(g.b) + '</td><td class="ctr">' + (g.r ? result(g.r) : '')
          + '</td><td class="num">' + Math.ceil(gameTree(g).main.length / 2) + '</td><td class="dim">' + esc(reasonText(g.y, lang)) + '</td></tr>';
      });
      $('games').querySelector('tbody').innerHTML = rows;
    }
    $('credit').textContent = L.pieces + I.pieces;
    $('stamp').textContent = fill(L.exported, dateText(day[0], day[1], day[2], lang));
    if (game) drawGame();
  }
  $('lang').addEventListener('click', e => {
    const b = e.target.closest('button[data-lang]');
    if (!b) return;
    lang = b.dataset.lang;
    try { localStorage.setItem('hydra64-lang', lang); } catch (e) {}
    translate();
  });
  if (!single) $('games').addEventListener('click', e => { const r = e.target.closest('tr[data-g]'); if (r) show(+r.dataset.g); });
  $('moves').addEventListener('click', e => { const a = e.target.closest('a[data-p]'); if (a) go(+a.dataset.p); });
  $('chart').addEventListener('click', e => { const r = e.target.getBoundingClientRect(), d = Math.round((e.clientX - r.left) / r.width * T.main.length); go(d > 0 ? T.main[d - 1] : -1); });
  $('first').onclick = () => go(-1); $('prev').onclick = goPrev;
  $('next').onclick = goNext; $('last').onclick = goEnd;
  $('flip').onclick = () => { flipped = !flipped; drawBoard(); };
  if (!single) { $('prevg').onclick = () => show(current - 1); $('nextg').onclick = () => show(current + 1); }
  $('pgn').onclick = () => {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([D.pgn], { type: 'application/x-chess-pgn' }));
    a.download = D.file + '.pgn'; a.click();
  };
  document.addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT') return;
    const keys = { ArrowLeft: goPrev, ArrowRight: goNext, Home: () => go(-1), End: goEnd,
                   ArrowUp: () => { if (!single) show(current - 1); }, ArrowDown: () => { if (!single) show(current + 1); }, f: () => $('flip').onclick() };
    if (keys[e.key]) { keys[e.key](); e.preventDefault(); }
  });
  window.addEventListener('resize', () => { if (game) drawChart(); });
  translate();
  if (G.length) show(0);
})();
)JS";

// Pieces of the current set (styles) and their credit.
QString pieceStyles(QString* credit)
{
    PieceRenderer renderer;
    renderer.setFixedShadow(0);
    QString css;
    const std::pair<Piece, const char*> pieces[] = {
        {makePiece(White, King), "wK"}, {makePiece(White, Queen), "wQ"}, {makePiece(White, Rook), "wR"}, {makePiece(White, Bishop), "wB"},
        {makePiece(White, Knight), "wN"}, {makePiece(White, Pawn), "wP"}, {makePiece(Black, King), "bK"}, {makePiece(Black, Queen), "bQ"},
        {makePiece(Black, Rook), "bR"}, {makePiece(Black, Bishop), "bB"}, {makePiece(Black, Knight), "bN"}, {makePiece(Black, Pawn), "bP"},
    };
    for (const auto& [piece, name] : pieces) css += QStringLiteral(".%1{background-image:url(%2)}").arg(QLatin1String(name), pieceData(renderer, piece));
    for (const PieceRenderer::PieceSet& set : PieceRenderer::availableSets())
        if (set.id == PieceRenderer::currentSet()) *credit = set.credit.isEmpty() ? set.name : QStringLiteral("%1 — %2").arg(set.name, set.credit);
    return css;
}

// Full page: header (logo, title, language), content, footer, data and script.
// `brand`: key of the text of the line above the title; `keysGames`: the page has several games.
QString shell(const QString& title, const char* brand, const QString& content, QJsonObject data, QJsonObject info, bool keysGames)
{
    QString credit;
    const QString pieces = pieceStyles(&credit);
    info["pieces"] = credit;
    info["date"] = QDate::currentDate().toString(Qt::ISODate);
    data["info"] = info;
    QString json = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
    json.replace(QStringLiteral("</"), QStringLiteral("<\\/")); // never "</script>" in the data

    QString h = QStringLiteral("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>%1</title>\n<style>\n").arg(esc(title));
    h += QStringLiteral(
             ":root{--window:#18181a;--panel:#212124;--panel-alt:#262629;--header:#2a2a2e;--raised:#333338;--border:#0e0e10;--border-soft:#36363b;--text:#d6d6da;--text-dim:#8e8e96;"
             "--text-faint:#5c5c64;--accent:#e68a35;--accent-soft:rgba(230,138,53,.16);--good:#5cb87e;--bad:#e05a50;--comment:#86b095;--info:#6fa3d8;--sq-light:%1;--sq-dark:%2;"
             "--mono:\"JetBrains Mono\",\"DejaVu Sans Mono\",ui-monospace,monospace}"
             "*{box-sizing:border-box}html{background:var(--window)}body{margin:0;color:var(--text);background:var(--window);font:15px/1.6 \"Inter\",\"Segoe UI\",\"Noto Sans\",\"DejaVu Sans\",system-ui,sans-serif}"
             ".wrap{max-width:1180px;margin:0 auto;padding:0 20px}"
             "header{background:linear-gradient(180deg,var(--header),var(--window));border-bottom:1px solid var(--border)}"
             "header .wrap{display:flex;align-items:center;gap:22px;padding-top:36px;padding-bottom:30px}header svg{width:84px;height:84px;flex:none;filter:drop-shadow(0 4px 10px rgba(0,0,0,.45))}"
             ".brand{font-size:11px;font-weight:600;letter-spacing:.22em;color:var(--text-faint);text-transform:uppercase}h1{margin:2px 0 6px;font-size:30px;line-height:1.2;font-weight:700;letter-spacing:-.01em;color:#ececef}"
             ".lead{margin:0;color:var(--text-dim)}.head-text{flex:1;min-width:0}"
             "#lang{display:flex;flex:none;align-self:flex-start;border:1px solid var(--border-soft);border-radius:6px;overflow:hidden}"
             "#lang button{border:0;border-radius:0;background:var(--panel);color:var(--text-dim);padding:4px 11px;font-size:12px;font-weight:600;letter-spacing:.08em}#lang button.on{background:var(--accent);color:#18181a}"
             "main{padding-bottom:56px;width:100%}section{margin-top:40px}"
             "h2{margin:0 0 16px;font-size:22px;font-weight:700;letter-spacing:-.01em;color:#ececef;display:flex;align-items:center;gap:12px}"
             "h2 .glyph{color:var(--accent);font-size:24px;width:28px;text-align:center}h2::after{content:\"\";flex:1;height:1px;background:var(--border-soft);margin-left:6px}"
             ".tables{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,420px),1fr));gap:28px}"
             ".table-wrap{overflow-x:auto;border-radius:8px;border:1px solid var(--border);background:var(--panel)}"
             "table{width:100%;border-collapse:collapse;font-size:14px}th,td{text-align:left;padding:8px 12px;white-space:nowrap}"
             "th{font-size:10px;font-weight:600;letter-spacing:.14em;text-transform:uppercase;color:var(--text-faint);border-bottom:1px solid var(--border-soft);background:var(--panel-alt)}"
             "tbody tr+tr td{border-top:1px solid var(--border-soft)}tbody tr:hover{background:var(--panel-alt)}"
             ".num{text-align:right;font-variant-numeric:tabular-nums}.ctr{text-align:center;font-variant-numeric:tabular-nums}.dim{color:var(--text-dim)}.who{color:#ececef;font-weight:600}.pts{color:var(--accent);font-weight:700}.self{background:var(--header)}"
             ".viewer{display:grid;grid-template-columns:minmax(280px,520px) 1fr;gap:28px;align-items:start}.viewer>div{min-width:0}@media(max-width:820px){.viewer{grid-template-columns:minmax(0,1fr)}header svg{display:none}}"
             ".name{color:var(--text-dim);padding:5px 2px;font-weight:600}#board{display:grid;grid-template-columns:repeat(8,1fr);aspect-ratio:1;border-radius:6px;overflow:hidden;border:1px solid var(--border);box-shadow:0 8px 28px rgba(0,0,0,.5)}"
             ".sq{position:relative}.lt{background:var(--sq-light)}.dk{background:var(--sq-dark)}.sq.last::after{content:'';position:absolute;inset:0;background:rgba(230,138,53,.36)}"
             ".p{position:absolute;inset:0;background-size:contain;background-repeat:no-repeat;z-index:1}u{position:absolute;font-size:10px;text-decoration:none;opacity:.55;color:#000;z-index:2}.cr{left:3px;top:1px}.cf{right:3px;bottom:1px}"
             ".bar{display:flex;gap:6px;margin-top:12px;align-items:center;flex-wrap:wrap}"
             "button{background:var(--panel-alt);color:var(--text);border:1px solid var(--border-soft);border-bottom-width:2px;border-radius:6px;padding:6px 12px;font:inherit;cursor:pointer}button:hover{border-color:var(--accent);color:#ececef}"
             "#pos{margin-left:auto;color:var(--text-dim);font-family:var(--mono);font-size:13px}#info{color:var(--comment);margin-top:10px;min-height:1.6em;font-family:var(--mono);font-size:13.5px}#info.lib{color:var(--info)}"
             "#chart{width:100%;height:72px;margin-top:6px;cursor:pointer;background:var(--panel);border:1px solid var(--border);border-radius:6px}"
             "#title{font-weight:600;color:#ececef}#why{color:var(--text-dim);margin-bottom:10px}"
             "#moves{font-family:var(--mono);font-size:13.5px;line-height:1.95;background:var(--panel-alt);border:1px solid var(--border);border-radius:6px;padding:10px 14px;max-height:420px;overflow-y:auto}"
             "#moves a{cursor:pointer;padding:1px 4px;border-radius:3px;font-weight:600;color:#ececef}#moves a.lib{color:var(--text-dim);font-weight:400}#moves a:hover{background:var(--raised)}#moves a.cur{background:var(--accent);color:#18181a}"
             ".no{color:var(--text-faint);margin-right:2px}.res{margin-left:6px;color:var(--accent)}"
             ".libend{display:inline-block;font-family:inherit;font-size:11px;line-height:1.5;padding:0 8px;margin:0 4px;border-radius:999px;background:rgba(111,163,216,.15);color:var(--info);white-space:nowrap}"
             ".list{max-height:340px;overflow:auto;margin-top:16px}#games tbody tr{cursor:pointer}#games tr.sel td{color:var(--accent)}#games thead th{position:sticky;top:0}"
                          "kbd{font-family:var(--mono);font-size:12px;padding:1px 7px;border-radius:4px;white-space:nowrap;background:var(--panel-alt);border:1px solid var(--border-soft);border-bottom-width:2px;color:#ececef}"
             ".gfx{position:absolute;inset:0;width:100%;height:100%;pointer-events:none;z-index:3}#board{position:relative}"
             ".var{color:var(--text-dim)}#moves .var a{font-weight:400;color:var(--text-dim)}#moves .var a.cur{color:#18181a;font-weight:600}.cmt{color:var(--comment);font-family:\"Inter\",\"Segoe UI\",system-ui,sans-serif;font-size:13.5px}"
             "body{display:flex;flex-direction:column;min-height:100vh}main{flex:1}"
             "footer{margin-top:48px;padding:0;border-top:1px solid var(--border);background:linear-gradient(0deg,var(--header),var(--window));color:var(--text-dim);font-size:13px}"
             ".foot{display:grid;grid-template-columns:1.3fr 1fr 1fr;gap:32px;padding-top:28px;padding-bottom:30px}@media(max-width:820px){.foot{grid-template-columns:1fr}}"
             ".foot-brand{display:flex;gap:14px;align-items:flex-start}.foot-brand svg{width:44px;height:44px;flex:none}.foot-brand b{display:block;color:#ececef;font-size:15px;letter-spacing:.02em}"
             ".foot-title{font-size:10px;font-weight:600;letter-spacing:.16em;text-transform:uppercase;color:var(--text-faint);margin-bottom:8px}"
             ".keys{display:grid;grid-template-columns:auto 1fr;gap:5px 12px;align-items:center}.keys span:nth-child(odd){white-space:nowrap}#stamp{display:block;margin-top:6px;color:var(--text-faint)}")
             .arg(boardtheme::lightColor().name(), boardtheme::darkColor().name());
    h += pieces;
    h += QStringLiteral("\n</style></head><body>\n<header><div class=wrap>%1<div class=head-text><div class=brand data-i=%2></div><h1>%3</h1><p class=lead id=lead></p></div>"
                        "<div id=lang><button data-lang=en>EN</button><button data-lang=fr>FR</button></div></div></header>\n<main class=wrap>\n")
             .arg(logoSvg(), QLatin1String(brand), esc(title));
    h += content;
    h += QStringLiteral("</main>\n<footer><div class=\"wrap foot\">"
                        "<div class=foot-brand>%1<div><b>Hydra64</b><span data-i=made></span><span id=stamp></span></div></div>"
                        "<div><div class=foot-title data-i=keysTitle></div><div class=keys>"
                        "<span><kbd>←</kbd> <kbd>→</kbd></span><span data-i=kMoves></span>%2"
                        "<span><kbd data-i=kHome></kbd> <kbd data-i=kEnd></kbd></span><span data-i=kEnds></span>"
                        "<span><kbd>F</kbd></span><span data-i=kFlip></span></div></div>"
                        "<div><div class=foot-title data-i=creditsTitle></div><span id=credit></span></div>"
                        "</div></footer>\n")
             .arg(logoSvg(), keysGames ? QStringLiteral("<span><kbd>↑</kbd> <kbd>↓</kbd></span><span data-i=kGames></span>") : QString());
    h += QStringLiteral("<script type=\"application/json\" id=\"data\">%1</script>\n<script>%2</script>\n</body></html>\n").arg(json, QString::fromUtf8(Script));
    return h;
}

// Board, controls, graph; on the right the text of the moves and what the page adds (`side`).
QString viewer(const QString& side)
{
    return QStringLiteral("<div class=viewer><div>"
                          "<div class=name id=top></div><div id=board></div><div class=name id=bottom></div>"
                          "<div class=bar><button id=first data-t=first>⏮</button><button id=prev data-t=prev>◀</button><button id=next data-t=next>▶</button><button id=last data-t=last>⏭</button>"
                          "<button id=flip data-t=flip>⇅</button><span id=pos></span></div><div id=info></div><canvas id=chart></canvas></div>\n"
                          "<div><div id=title></div><div id=why></div><div id=moves></div>%1</div></div>\n")
        .arg(side);
}

} // namespace

QString page(const Tournament& t, const QString& title)
{
    const QList<Tournament::Standing> standings = t.standings();
    const QList<Tournament::GameRecord>& games = t.games();
    const Tournament::Settings& s = t.settings();

    // ---- Data. The texts are in the script, in English and in French: the page reads
    // in both languages whatever the application's.
    QJsonArray gameArray;
    for (int i = 0; i < games.size(); ++i)
        if (!games[i].pgn.isEmpty()) gameArray.append(tournamentGame(i + 1, games[i]));
    QJsonArray limit;
    if (s.limit == Tournament::Settings::Limit::Clock) limit = QJsonArray{"clock", s.baseMs / 60000.0, s.incMs / 1000.0};
    else if (s.limit == Tournament::Settings::Limit::MoveTime) limit = QJsonArray{"movetime", s.moveTimeMs / 1000.0};
    else limit = QJsonArray{"depth", s.depth};
    QJsonObject info{{"schedule", s.schedule == Tournament::Settings::Schedule::Swiss ? "swiss" : s.schedule == Tournament::Settings::Schedule::Gauntlet ? "gauntlet" : "rr"},
                     {"limit", limit}, {"players", int(standings.size())}, {"played", t.playedCount()}, {"total", int(games.size())}};
    QJsonObject data{{"mode", "tournament"}, {"games", gameArray}, {"pgn", t.exportPgn()}, {"file", title}};

    // ---- Standings
    QString table = QStringLiteral("<table><thead><tr><th class=num>#</th><th data-i=engine></th><th class=num data-i=points></th><th class=num data-i=played></th><th class=num>+</th><th class=num>=</th>"
                                   "<th class=num>−</th><th class=num>%</th><th class=num data-i=elo></th></tr></thead><tbody>");
    for (int i = 0; i < standings.size(); ++i) {
        const Tournament::Standing& st = standings[i];
        const QString elo = st.hasElo ? QStringLiteral("%1%2 ± %3").arg(st.elo >= 0 ? QStringLiteral("+") : QStringLiteral("−")).arg(int(std::lround(std::abs(st.elo)))).arg(int(std::lround(st.margin)))
                                      : QStringLiteral("–");
        table += QStringLiteral("<tr><td class=num>%1</td><td class=who>%2</td><td class=\"num pts\">%3</td><td class=num>%4</td><td class=num>%5</td><td class=num>%6</td><td class=num>%7</td><td class=num>%8</td><td class=num>%9</td></tr>")
                     .arg(i + 1).arg(esc(st.name), points(st.points)).arg(st.played).arg(st.wins).arg(st.draws).arg(st.losses)
                     .arg(st.played ? QString::number(int(std::lround(100.0 * st.points / st.played))) : QStringLiteral("–"), elo);
    }
    table += QStringLiteral("</tbody></table>");

    // ---- Crosstable: points of the row's player against the column's
    QString cross = QStringLiteral("<table><thead><tr><th class=num>#</th><th data-i=engine></th>");
    for (int j = 0; j < standings.size(); ++j) cross += QStringLiteral("<th class=ctr>%1</th>").arg(j + 1);
    cross += QStringLiteral("</tr></thead><tbody>");
    for (int i = 0; i < standings.size(); ++i) {
        cross += QStringLiteral("<tr><td class=num>%1</td><td class=who>%2</td>").arg(i + 1).arg(esc(standings[i].name));
        for (int j = 0; j < standings.size(); ++j) {
            if (i == j) {
                cross += QStringLiteral("<td class=self></td>");
                continue;
            }
            double pts = 0;
            int played = 0;
            for (const Tournament::GameRecord& g : games) {
                if (g.result.isEmpty() || g.blackId.isEmpty()) continue;
                const bool asWhite = g.whiteId == standings[i].id && g.blackId == standings[j].id;
                const bool asBlack = g.blackId == standings[i].id && g.whiteId == standings[j].id;
                if (!asWhite && !asBlack) continue;
                ++played;
                if (g.result == QLatin1String("1/2-1/2")) pts += 0.5;
                else if ((g.result == QLatin1String("1-0")) == asWhite) pts += 1;
            }
            cross += QStringLiteral("<td class=ctr>%1</td>").arg(played ? QStringLiteral("%1/%2").arg(points(pts)).arg(played) : QString());
        }
        cross += QStringLiteral("</tr>");
    }
    cross += QStringLiteral("</tbody></table>");

    QString content = QStringLiteral("<section class=tables><div><h2><span class=glyph>♛</span><span data-i=standings></span></h2><div class=table-wrap>%1</div></div>"
                                     "<div><h2><span class=glyph>⊞</span><span data-i=cross></span></h2><div class=table-wrap>%2</div></div></section>\n").arg(table, cross);
    content += QStringLiteral("<section><h2><span class=glyph>♞</span><span data-i=games></span></h2>\n")
               + viewer(QStringLiteral("<div class=bar><button id=prevg>▲ <span data-i=prevGame></span></button><button id=nextg>▼ <span data-i=nextGame></span></button>"
                                       "<button id=pgn style=\"margin-left:auto\" data-i=pgn></button></div>"
                                       "<div class=\"table-wrap list\"><table id=games><thead><tr><th class=num data-i=num></th><th data-i=white></th><th data-i=black></th><th class=ctr data-i=result></th>"
                                       "<th class=num data-i=moves></th><th data-i=reason></th></tr></thead><tbody></tbody></table></div>"))
               + QStringLiteral("</section>\n");
    return shell(title, "brand", content, data, info, true);
}

QString gamePage(const Game& game)
{
    auto header = [&](const char* key) {
        const QString v = QString::fromStdString(game.header(key)).trimmed();
        return v == QLatin1String("?") || v == QLatin1String("-") ? QString() : v;
    };
    const QString white = header("White").isEmpty() ? QStringLiteral("?") : header("White");
    const QString black = header("Black").isEmpty() ? QStringLiteral("?") : header("Black");
    QJsonObject g = gameObject(game, 0);
    auto withElo = [&](const QString& name, const char* key) { return header(key).isEmpty() || header(key) == QLatin1String("0") ? name : QStringLiteral("%1 (%2)").arg(name, header(key)); };
    g["n"] = 1;
    g["w"] = withElo(white, "WhiteElo");
    g["b"] = withElo(black, "BlackElo");
    g["r"] = header("Result");
    g["y"] = header("Termination");
    const QString title = QStringLiteral("%1 – %2").arg(white, black);
    QJsonObject head{{"event", header("Event")}, {"site", header("Site")}, {"round", header("Round")}, {"date", header("Date")}, {"eco", header("ECO")}};
    QString file = title;
    file.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    QJsonObject data{{"mode", "game"}, {"games", QJsonArray{g}}, {"head", head}, {"pgn", QString::fromStdString(pgn::writeGame(game))}, {"file", file}};
    const QString content = QStringLiteral("<section><h2><span class=glyph>♞</span><span data-i=oneGame></span></h2>\n")
                            + viewer(QStringLiteral("<div class=bar><button id=pgn style=\"margin-left:auto\" data-i=pgn></button></div>")) + QStringLiteral("</section>\n");
    return shell(title, "gameBrand", content, data, QJsonObject(), false);
}

} // namespace tournamentHtml
