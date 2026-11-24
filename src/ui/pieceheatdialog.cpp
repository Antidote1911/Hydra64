#include "pieceheatdialog.h"

#include "chess/position.h"
#include "piecetracker.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

using namespace chess;

namespace {
QChar glyph(Piece p)
{
    static const char16_t white[] = u" ♙♘♗♖♕♔", black[] = u" ♟♞♝♜♛♚";
    return QChar(colorOf(p) == White ? white[typeOf(p)] : black[typeOf(p)]);
}
QString kindName(PieceType t, bool plural)
{
    switch (t) {
    case King: return PieceHeatDialog::tr("Roi");
    case Queen: return plural ? PieceHeatDialog::tr("Dames") : PieceHeatDialog::tr("Dame");
    case Rook: return plural ? PieceHeatDialog::tr("Tours") : PieceHeatDialog::tr("Tour");
    case Bishop: return plural ? PieceHeatDialog::tr("Fous") : PieceHeatDialog::tr("Fou");
    case Knight: return plural ? PieceHeatDialog::tr("Cavaliers") : PieceHeatDialog::tr("Cavalier");
    case Pawn: return plural ? PieceHeatDialog::tr("Pions") : PieceHeatDialog::tr("Pion");
    default: return {};
    }
}
} // namespace

// What the board shows: a square under half a percent (it would read "0 %") stays bare.
static std::array<int, 64> visible(std::array<int, 64> shares)
{
    for (int& v : shares)
        if (v < 5000) v = 0;
    return shares;
}

PieceHeatDialog::PieceHeatDialog(const hy64db::PieceHeat& heat, const QString& what, QWidget* parent)
    : QDialog(parent)
    , m_heat(heat)
{
    setWindowTitle(tr("Pièces sur les parties affichées — %1").arg(what));
    auto* modes = new QButtonGroup(this);
    for (int k = 0; k < 2; ++k) {
        m_modeButton[k] = new QToolButton;
        m_modeButton[k]->setObjectName("Segment");
        m_modeButton[k]->setCheckable(true);
        m_modeButton[k]->setText(k == 0 ? tr("Où se tiennent les pièces") : tr("Premier coup d'une pièce"));
        modes->addButton(m_modeButton[k], k);
    }
    m_modeButton[0]->setChecked(true);
    m_piece = new QComboBox;
    m_slice = new QComboBox;
    m_slice->addItem(tr("Toute la partie"), -1);
    m_slice->addItem(tr("Coups 1 à 10"), 0);
    m_slice->addItem(tr("Coups 11 à 20"), 1);
    m_slice->addItem(tr("Coups 21 à 30"), 2);
    m_slice->addItem(tr("Coups 31 à 40"), 3);
    m_slice->addItem(tr("Après le 40ᵉ coup"), 4);
    m_sliceLabel = new QLabel(tr("Moment"));
    m_sliceLabel->setObjectName("Dim");
    m_info = new QLabel;
    m_info->setObjectName("Faint");
    m_info->setWordWrap(true);
    m_board = new HeatBoard;
    auto* top = new QHBoxLayout;
    top->addWidget(m_modeButton[0]);
    top->addWidget(m_modeButton[1]);
    top->addStretch(1);
    auto* choice = new QHBoxLayout;
    auto* pieceLabel = new QLabel(tr("Pièce"));
    pieceLabel->setObjectName("Dim");
    choice->addWidget(pieceLabel);
    choice->addWidget(m_piece, 1);
    choice->addSpacing(12);
    choice->addWidget(m_sliceLabel);
    choice->addWidget(m_slice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(choice);
    layout->addWidget(m_board, 1);
    layout->addWidget(m_info);
    layout->addWidget(buttons);
    resize(520, 680);
    connect(modes, &QButtonGroup::idClicked, this, [this](int id) { setMode(Mode(id)); });
    connect(m_piece, &QComboBox::currentIndexChanged, this, &PieceHeatDialog::refresh);
    connect(m_slice, &QComboBox::currentIndexChanged, this, &PieceHeatDialog::refresh);
    fill();
}

// The pieces offered: the twelve kinds (presence), or the thirty-two pieces of the starting position (first move).
void PieceHeatDialog::fill()
{
    const QSignalBlocker quiet(m_piece);
    m_piece->clear();
    if (m_mode == Presence) {
        for (Color c : {White, Black})
            for (PieceType t : {King, Queen, Rook, Bishop, Knight, Pawn})
                m_piece->addItem(QStringLiteral("%1 %2 (%3)").arg(glyph(makePiece(c, t))).arg(kindName(t, true), c == White ? tr("Blancs") : tr("Noirs")), int(makePiece(c, t)));
        m_piece->setCurrentIndex(m_piece->findData(int(makePiece(White, Knight))));
    } else {
        const Position start = Position::startPos();
        for (Color c : {White, Black})
            for (int r = 0; r < 2; ++r)
                for (int f = 0; f < 8; ++f) {
                    const Square s = makeSquare(f, c == White ? r : 7 - r);
                    const Piece p = start.at(s);
                    m_piece->addItem(QStringLiteral("%1 %2 %3").arg(glyph(p)).arg(kindName(typeOf(p), false), QString::fromStdString(squareName(s))), int(s));
                }
        m_piece->setCurrentIndex(m_piece->findData(int(makeSquare(6, 0)))); // the g1 knight
    }
    m_slice->setVisible(m_mode == Presence);
    m_sliceLabel->setVisible(m_mode == Presence);
    refresh();
}

void PieceHeatDialog::setMode(Mode mode)
{
    m_mode = mode;
    m_modeButton[mode == Presence ? 0 : 1]->setChecked(true);
    fill();
}

void PieceHeatDialog::setPiece(Piece piece)
{
    if (m_mode != Presence) setMode(Presence);
    m_piece->setCurrentIndex(m_piece->findData(int(piece)));
}

void PieceHeatDialog::setOrigin(Square origin)
{
    if (m_mode != FirstMove) setMode(FirstMove);
    m_piece->setCurrentIndex(m_piece->findData(int(origin)));
}

void PieceHeatDialog::setSlice(int slice) { m_slice->setCurrentIndex(m_slice->findData(slice)); }

QString PieceHeatDialog::infoText() const { return m_info->text(); }

void PieceHeatDialog::refresh()
{
    const QLocale loc;
    const QString games = loc.toString(m_heat.games);
    m_shown.fill(0);
    int busiest = -1;
    if (m_piece->currentIndex() < 0 || m_heat.games == 0) {
        m_board->setData(m_shown, 0, Position());
        m_info->setText(tr("Aucune partie à compter (les parties qui ne partent pas de la position initiale sont écartées)."));
        return;
    }
    if (m_mode == Presence) {
        const Piece piece = Piece(m_piece->currentData().toInt());
        const int slice = m_slice->currentData().toInt();
        m_shown = m_heat.shares(piece, slice);
        for (int s = 0; s < 64; ++s)
            if (m_shown[size_t(s)] && (busiest < 0 || m_shown[size_t(s)] > m_shown[size_t(busiest)])) busiest = s;
        m_board->setData(visible(m_shown), 1000000, Position());
        const QString positions = loc.toString(qulonglong(m_heat.positionCount(slice)));
        m_info->setText(busiest < 0 ? tr("%1 parties, %2 positions : cette pièce n'y est jamais.").arg(games, positions)
                                    : tr("%1 parties, %2 positions. Chaque case : part des positions où une pièce de ce genre s'y trouve ; le plus souvent en %3 (%4 %).")
                                          .arg(games, positions, QString::fromStdString(squareName(Square(busiest))), loc.toString(m_shown[size_t(busiest)] / 10000.0, 'f', 1)));
    } else {
        const Square origin = Square(m_piece->currentData().toInt());
        const uint32_t moved = m_heat.moved(origin);
        for (int s = 0; s < 64; ++s) {
            const uint32_t n = m_heat.firstTo[size_t(origin)][size_t(s)];
            m_shown[size_t(s)] = moved ? int((uint64_t(n) * 1000000 + moved / 2) / moved) : 0;
            if (n && (busiest < 0 || n > m_heat.firstTo[size_t(origin)][size_t(busiest)])) busiest = s;
        }
        m_board->setData(visible(m_shown), 1000000, Position::startPos());
        m_info->setText(busiest < 0 ? tr("%1 parties : cette pièce n'a jamais bougé.").arg(games)
                                    : tr("%1 parties. La pièce bouge dans %2 % d'entre elles, pour la première fois au %3ᵉ coup en moyenne ; le plus souvent vers %4 (%5 % de ses premiers coups).")
                                          .arg(games, loc.toString(100.0 * moved / m_heat.games, 'f', 1))
                                          .arg((m_heat.firstPly[size_t(origin)] / moved + 1) / 2)
                                          .arg(QString::fromStdString(squareName(Square(busiest))), loc.toString(m_shown[size_t(busiest)] / 10000.0, 'f', 1)));
    }
}
