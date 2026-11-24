#include "piecetracker.h"

#include "theme.h"

#include "chess/pgn.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace chess;

namespace {
QString pieceName(PieceType t)
{
    switch (t) {
    case King: return PieceTracker::tr("Roi");
    case Queen: return PieceTracker::tr("Dame");
    case Rook: return PieceTracker::tr("Tour");
    case Bishop: return PieceTracker::tr("Fou");
    case Knight: return PieceTracker::tr("Cavalier");
    case Pawn: return PieceTracker::tr("Pion");
    default: return {};
    }
}
QString piecesName(PieceType t)
{
    switch (t) {
    case King: return PieceTracker::tr("Roi");
    case Queen: return PieceTracker::tr("Dames");
    case Rook: return PieceTracker::tr("Tours");
    case Bishop: return PieceTracker::tr("Fous");
    case Knight: return PieceTracker::tr("Cavaliers");
    case Pawn: return PieceTracker::tr("Pions");
    default: return {};
    }
}
QChar glyph(Piece p)
{
    static const char16_t white[] = u" ♙♘♗♖♕♔", black[] = u" ♟♞♝♜♛♚";
    return QChar(colorOf(p) == White ? white[typeOf(p)] : black[typeOf(p)]);
}
} // namespace

std::array<int, 64> PieceTracker::track(const Game& game, const Spec& spec, int plyFrom, int plyTo, int* positions)
{
    std::array<int, 64> out{};
    int counted = 0;
    Square at = spec.origin;
    Position pos = game.root()->pos;
    if (at != NoSquare && pos.at(at) == NoPiece) at = NoSquare;
    auto count = [&](int ply) {
        if (ply < plyFrom || (plyTo >= 0 && ply > plyTo)) return;
        ++counted;
        if (spec.origin != NoSquare) {
            if (at != NoSquare) ++out[size_t(at)];
            return;
        }
        for (int s = 0; s < 64; ++s)
            if (pos.at(Square(s)) == spec.piece) ++out[size_t(s)];
    };
    count(0);
    int ply = 0;
    for (const Node* n : game.mainline()) {
        const Move m = n->move;
        const Piece mover = pos.at(m.from);
        if (at != NoSquare) {
            const bool castling = pos.isCastling(m);
            const bool kingside = castling && pos.isKingsideCastling(m);
            if (m.from == at) {
                at = castling ? makeSquare(kingside ? 6 : 2, rankOf(m.from)) : m.to; // the king of a castling move lands on g or c
            } else if (m.to == at && !castling) {
                at = NoSquare; // capture
            } else if (castling && at == pos.castlingRook(colorOf(mover), kingside)) {
                at = makeSquare(kingside ? 5 : 3, rankOf(m.from)); // the castling rook
            } else if (typeOf(mover) == Pawn && fileOf(m.to) != fileOf(m.from) && pos.at(m.to) == NoPiece
                       && at == makeSquare(fileOf(m.to), rankOf(m.from))) {
                at = NoSquare; // en passant
            }
        }
        pos = n->pos;
        if (at != NoSquare && pos.at(at) == NoPiece) at = NoSquare; // safeguard
        count(++ply);
    }
    if (positions) *positions = counted;
    return out;
}

PieceTracker::PieceTracker(const Game& game, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Suivi des pièces"));
    chess::pgn::parseGame(chess::pgn::writeGame(game), m_game); // independent copy
    m_piece = new QComboBox;
    const Position& start = m_game.root()->pos;
    // Specific pieces (by starting square), then types
    for (Color c : {White, Black})
        for (int r = 0; r < 8; ++r)
            for (int f = 0; f < 8; ++f) {
                const Square s = makeSquare(f, c == White ? r : 7 - r);
                const Piece p = start.at(s);
                if (p == NoPiece || colorOf(p) != c) continue;
                m_piece->addItem(QStringLiteral("%1 %2 %3").arg(glyph(p)).arg(pieceName(typeOf(p)), QString::fromStdString(squareName(s))),
                                 QVariantList{int(s), 0});
            }
    m_piece->insertSeparator(m_piece->count());
    for (Color c : {White, Black})
        for (PieceType t : {King, Queen, Rook, Bishop, Knight, Pawn})
            m_piece->addItem(QStringLiteral("%1 %2 (%3)").arg(glyph(makePiece(c, t))).arg(piecesName(t), c == White ? tr("Blancs") : tr("Noirs")),
                             QVariantList{-1, int(makePiece(c, t))});
    const int plies = int(m_game.mainline().size());
    m_from = new QSpinBox;
    m_to = new QSpinBox;
    m_from->setRange(0, plies);
    m_to->setRange(0, plies);
    m_to->setValue(plies);
    m_info = new QLabel;
    m_info->setObjectName("Faint");
    m_info->setWordWrap(true);
    m_board = new HeatBoard;
    auto* range = new QHBoxLayout;
    range->addWidget(m_from);
    range->addWidget(new QLabel(tr("à")));
    range->addWidget(m_to);
    range->addStretch(1);
    auto* form = new QFormLayout;
    form->addRow(tr("Pièce"), m_piece);
    form->addRow(tr("Demi-coups"), range);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_board, 1);
    layout->addWidget(m_info);
    layout->addWidget(buttons);
    connect(m_piece, &QComboBox::currentIndexChanged, this, &PieceTracker::refresh);
    connect(m_from, &QSpinBox::valueChanged, this, &PieceTracker::refresh);
    connect(m_to, &QSpinBox::valueChanged, this, &PieceTracker::refresh);
    // By default: the g1 knight (or the first piece)
    const int g1 = m_piece->findText(QStringLiteral("g1"), Qt::MatchEndsWith);
    m_piece->setCurrentIndex(std::max(0, g1));
    refresh();
}

void PieceTracker::setSpec(const Spec& spec)
{
    for (int i = 0; i < m_piece->count(); ++i) {
        const QVariantList d = m_piece->itemData(i).toList();
        if (d.size() == 2 && d[0].toInt() == (spec.origin == NoSquare ? -1 : int(spec.origin)) && (spec.origin != NoSquare || d[1].toInt() == int(spec.piece))) {
            m_piece->setCurrentIndex(i);
            break;
        }
    }
    refresh();
}

void PieceTracker::refresh()
{
    const QVariantList d = m_piece->currentData().toList();
    if (d.size() != 2) return;
    Spec spec;
    spec.origin = Square(d[0].toInt());
    if (d[0].toInt() < 0) spec.origin = NoSquare;
    spec.piece = Piece(d[1].toInt());
    int positions = 0;
    m_counts = track(m_game, spec, std::min(m_from->value(), m_to->value()), std::max(m_from->value(), m_to->value()), &positions);
    // Position shown: the last of the range
    Position shown = m_game.root()->pos;
    int k = 0;
    for (const Node* n : m_game.mainline()) {
        if (++k > std::max(m_from->value(), m_to->value())) break;
        shown = n->pos;
    }
    m_board->setData(m_counts, positions, shown);
    int busiest = -1;
    for (int s = 0; s < 64; ++s)
        if (m_counts[size_t(s)] && (busiest < 0 || m_counts[size_t(s)] > m_counts[size_t(busiest)])) busiest = s;
    int squares = 0;
    for (int c : m_counts) squares += c > 0;
    m_info->setText(busiest < 0 ? tr("La pièce n'est pas sur l'échiquier dans cette plage.")
                                : tr("%n case(s) visitée(s) ; le plus souvent en %1 (%2 % des %3 positions).", nullptr, squares)
                                      .arg(QString::fromStdString(squareName(Square(busiest))))
                                      .arg(100.0 * m_counts[size_t(busiest)] / std::max(1, positions), 0, 'f', 0)
                                      .arg(positions));
}

// ---------------------------------------------------------------- map

HeatBoard::HeatBoard(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(280, 280);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void HeatBoard::setData(const std::array<int, 64>& counts, int positions, const Position& shown)
{
    m_counts = counts;
    m_positions = positions;
    m_pos = shown;
    update();
}

void HeatBoard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::Panel);
    const int side = std::min(width(), height()) - 20;
    const double sq = side / 8.0;
    const QPointF o((width() - side) / 2.0, (height() - side) / 2.0);
    int top = 1;
    for (int c : m_counts) top = std::max(top, c);
    QFont pieceFont = font();
    pieceFont.setPixelSize(int(sq * 0.5));
    QFont small = font();
    small.setPixelSize(std::max(9, int(sq * 0.2)));
    for (int r = 0; r < 8; ++r)
        for (int f = 0; f < 8; ++f) {
            const Square s = makeSquare(f, r);
            const QRectF cell(o.x() + f * sq, o.y() + (7 - r) * sq, sq, sq);
            p.fillRect(cell, (f + r) % 2 ? theme::SquareLight : theme::SquareDark);
            const int n = m_counts[size_t(s)];
            if (n) {
                QColor heat = theme::Accent;
                heat.setAlphaF(0.25 + 0.65 * double(n) / top);
                p.fillRect(cell, heat);
            }
            const Piece pc = m_pos.at(s);
            if (pc != NoPiece) {
                static const char16_t glyphs[] = u" ♟♞♝♜♛♚";
                p.setFont(pieceFont);
                p.setPen(colorOf(pc) == White ? QColor(0xfa, 0xfa, 0xfa) : QColor(0x10, 0x10, 0x12));
                p.drawText(cell, Qt::AlignCenter, QString(QChar(glyphs[typeOf(pc)])));
            }
            if (n && m_positions) {
                p.setFont(small);
                p.setPen(QColor(0x10, 0x10, 0x12));
                p.drawText(cell.adjusted(2, 0, -3, -1), Qt::AlignRight | Qt::AlignBottom, QStringLiteral("%1 %").arg(qRound(100.0 * n / m_positions)));
            }
        }
}
