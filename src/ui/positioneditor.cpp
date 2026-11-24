#include "positioneditor.h"
#include "boardtheme.h"
#include "preferences.h"
#include "theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

using namespace chess;

namespace {

Piece toPiece(char c)
{
    const Color col = std::islower(static_cast<unsigned char>(c)) ? Black : White;
    switch (std::toupper(static_cast<unsigned char>(c))) {
    case 'P': return makePiece(col, Pawn);
    case 'N': return makePiece(col, Knight);
    case 'B': return makePiece(col, Bishop);
    case 'R': return makePiece(col, Rook);
    case 'Q': return makePiece(col, Queen);
    case 'K': return makePiece(col, King);
    default: return NoPiece;
    }
}

bool isPieceChar(char c) { return toPiece(c) != NoPiece; }

QLabel* section(const QString& text)
{
    auto* l = new QLabel(text.toUpper());
    l->setObjectName("Faint");
    QFont f = l->font();
    f.setPixelSize(10);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    l->setFont(f);
    return l;
}

const char* kStartBoard = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR";

} // namespace

// ================================================================= EditorBoard

EditorBoard::EditorBoard(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, qOverload<>(&QWidget::update)); // piece set
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 320);
    m_squares.fill(' ');
}

void EditorBoard::setSquares(const Squares& s)
{
    m_squares = s;
    update();
}

QPixmap EditorBoard::pieceIcon(char piece, int size) { return m_pieces.pixmap(toPiece(piece), size, devicePixelRatioF()); }

QRectF EditorBoard::boardRect() const
{
    int side = std::min(width(), height()) - 16;
    side = std::max(8, side - side % 8);
    return QRectF((width() - side) / 2, (height() - side) / 2, side, side);
}

QRectF EditorBoard::squareRect(int sq) const
{
    const QRectF b = boardRect();
    const qreal s = b.width() / 8;
    int f = sq & 7, r = sq >> 3;
    if (m_flipped) f = 7 - f;
    else r = 7 - r;
    return QRectF(b.left() + f * s, b.top() + r * s, s, s);
}

int EditorBoard::squareAt(QPointF p) const
{
    const QRectF b = boardRect();
    if (!b.contains(p)) return -1;
    const qreal s = b.width() / 8;
    int f = std::clamp(int((p.x() - b.left()) / s), 0, 7), r = std::clamp(int((p.y() - b.top()) / s), 0, 7);
    if (m_flipped) f = 7 - f;
    else r = 7 - r;
    return r * 8 + f;
}

void EditorBoard::put(int sq, char piece)
{
    if (piece == 'K' || piece == 'k') // a single king per side: it is moved
        for (char& c : m_squares)
            if (c == piece) c = ' ';
    m_squares[size_t(sq)] = piece;
}

void EditorBoard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF b = boardRect();
    const qreal s = b.width() / 8;
    const qreal dpr = devicePixelRatioF();

    p.setPen(Qt::NoPen);
    for (int i = 1; i <= 6; ++i) {
        p.setBrush(QColor(0, 0, 0, 12));
        p.drawRoundedRect(b.adjusted(-i, -i + 3, i, i + 3), 4 + i, 4 + i);
    }
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(b, 3, 3);
    p.setClipPath(clip);
    boardtheme::paint(p, b, dpr); // board theme (Preferences)
    // Hover: square targeted by the current tool
    if (m_hover >= 0 && m_dragFrom < 0) {
        const QRectF r = squareRect(m_hover);
        if (m_tool == EraseTool || (m_paint == Paint::Erase)) p.fillRect(r, QColor(224, 90, 80, 90));
        else p.fillRect(r, QColor(theme::Accent.red(), theme::Accent.green(), theme::Accent.blue(), 70));
    }
    // Coordinates
    QFont cf = font();
    cf.setPixelSize(std::max(9, int(s * 0.15)));
    cf.setWeight(QFont::DemiBold);
    p.setFont(cf);
    for (int i = 0; i < 8; ++i) {
        const int fileSq = m_flipped ? 7 - i : i;             // bottom rank
        const int rankSq = (m_flipped ? i : 7 - i) * 8 + (m_flipped ? 7 : 0);
        const int bottomSq = (m_flipped ? 56 : 0) + fileSq;
        auto other = [](int sq) { return (((sq & 7) + (sq >> 3)) & 1) ? boardtheme::darkColor() : boardtheme::lightColor(); };
        p.setPen(other(bottomSq));
        p.drawText(squareRect(bottomSq).adjusted(0, 0, -s * 0.06, -s * 0.03), Qt::AlignRight | Qt::AlignBottom, QString(QChar('a' + fileSq)));
        p.setPen(other(rankSq));
        p.drawText(squareRect(rankSq).adjusted(s * 0.06, s * 0.03, 0, 0), Qt::AlignLeft | Qt::AlignTop, QString(QChar('1' + (rankSq >> 3))));
    }
    // Pieces
    for (int sq = 0; sq < 64; ++sq) {
        if (m_squares[size_t(sq)] == ' ' || sq == m_dragFrom) continue;
        p.drawPixmap(squareRect(sq).topLeft(), m_pieces.pixmap(toPiece(m_squares[size_t(sq)]), int(s), dpr));
    }
    // Translucent preview of the piece to place
    if (m_hover >= 0 && m_dragFrom < 0 && isPieceChar(m_tool) && m_squares[size_t(m_hover)] != m_tool && m_paint != Paint::Erase) {
        p.setOpacity(0.45);
        p.drawPixmap(squareRect(m_hover).topLeft(), m_pieces.pixmap(toPiece(m_tool), int(s), dpr));
        p.setOpacity(1.0);
    }
    p.restore();
    if (m_dragFrom >= 0) {
        const int big = int(s * 1.1);
        p.drawPixmap(m_dragPos - QPointF(big / 2.0, big / 2.0), m_pieces.pixmap(toPiece(m_squares[size_t(m_dragFrom)]), big, dpr));
    }
}

void EditorBoard::mousePressEvent(QMouseEvent* e)
{
    const int sq = squareAt(e->position());
    if (sq < 0) return;
    char& c = m_squares[size_t(sq)];
    if (e->button() == Qt::RightButton) {
        m_paint = Paint::Erase;
        c = ' ';
    } else if (e->button() == Qt::LeftButton) {
        if (m_tool == MoveTool) {
            if (c != ' ') {
                m_dragFrom = sq;
                m_dragPos = e->position();
            }
        } else if (m_tool == EraseTool) {
            m_paint = Paint::Erase;
            c = ' ';
        } else if (c == m_tool) { // same piece: click = remove
            m_paint = Paint::Erase;
            c = ' ';
        } else {
            m_paint = Paint::Place;
            put(sq, m_tool);
        }
    }
    update();
    emit changed();
}

void EditorBoard::mouseMoveEvent(QMouseEvent* e)
{
    const int sq = squareAt(e->position());
    const bool moved = sq != m_hover;
    m_hover = sq;
    setCursor(m_tool == MoveTool ? (m_dragFrom >= 0 ? Qt::ClosedHandCursor : Qt::OpenHandCursor) : Qt::CrossCursor);
    if (m_dragFrom >= 0) {
        m_dragPos = e->position();
    } else if (moved && sq >= 0 && m_paint != Paint::None) { // paint by dragging
        if (m_paint == Paint::Erase) m_squares[size_t(sq)] = ' ';
        else if (m_tool != 'K' && m_tool != 'k') put(sq, m_tool);
        emit changed();
    }
    update();
}

void EditorBoard::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_dragFrom >= 0) {
        const int to = squareAt(e->position());
        const char piece = m_squares[size_t(m_dragFrom)];
        m_squares[size_t(m_dragFrom)] = ' ';
        if (to >= 0) put(to, piece); // dropped off the board: the piece is removed
        m_dragFrom = -1;
        emit changed();
    }
    m_paint = Paint::None;
    update();
}

void EditorBoard::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

// ================================================================= PositionEditorDialog

PositionEditorDialog::PositionEditorDialog(const Position& start, bool flipped, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Éditeur de position"));
    resize(1000, 700);

    m_board = new EditorBoard;
    m_board->setFlipped(flipped);

    // ---------------- Palette: black + move, white + eraser
    auto* palette = new QGridLayout;
    palette->setSpacing(3);
    m_tools = new QButtonGroup(this);
    const char rows[2][7] = {{'k', 'q', 'r', 'b', 'n', 'p', EditorBoard::MoveTool}, {'K', 'Q', 'R', 'B', 'N', 'P', EditorBoard::EraseTool}};
    static const QHash<char, QString> names = {{'k', tr("Roi")},      {'q', tr("Dame")},    {'r', tr("Tour")},
                                               {'b', tr("Fou")},      {'n', tr("Cavalier")}, {'p', tr("Pion")}};
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 7; ++c) {
            const char t = rows[r][c];
            auto* b = new QToolButton;
            b->setObjectName("PaletteButton");
            b->setCheckable(true);
            b->setFixedSize(40, 40);
            b->setIconSize(QSize(32, 32));
            if (t == EditorBoard::MoveTool) {
                b->setIcon(theme::icon("cursor"));
                b->setIconSize(QSize(20, 20));
                b->setToolTip(tr("Déplacer les pièces (glisser ; hors de l'échiquier = retirer)"));
            } else if (t == EditorBoard::EraseTool) {
                b->setIcon(theme::icon("eraser"));
                b->setIconSize(QSize(20, 20));
                b->setToolTip(tr("Gomme"));
            } else {
                b->setIcon(QIcon(m_board->pieceIcon(t, 64)));
                b->setToolTip(QStringLiteral("%1 %2").arg(names.value(char(std::tolower(t))), std::isupper(static_cast<unsigned char>(t)) ? tr("blanc") : tr("noir")));
            }
            m_tools->addButton(b, int(t));
            palette->addWidget(b, r, c);
        }
    m_tools->button('P')->setChecked(true);
    m_board->setTool('P');
    connect(m_tools, &QButtonGroup::idClicked, this, [this](int id) { m_board->setTool(char(id)); });

    auto* hint = new QLabel(tr("Clic : poser · re-clic ou clic droit : retirer · glisser : peindre"));
    hint->setObjectName("Faint");
    hint->setWordWrap(true);

    // ---------------- Quick actions
    auto* clear = new QPushButton(theme::icon("trash"), tr("Vider"));
    auto* initial = new QPushButton(theme::icon("board"), tr("Position initiale"));
    auto* paste = new QPushButton(theme::icon("paste"), tr("Coller FEN"));
    auto* copy = new QPushButton(theme::icon("copy"), tr("Copier FEN"));
    auto* quick = new QGridLayout;
    quick->setSpacing(6);
    quick->addWidget(clear, 0, 0);
    quick->addWidget(initial, 0, 1);
    quick->addWidget(paste, 1, 0);
    quick->addWidget(copy, 1, 1);

    // ---------------- Side to move
    auto* sideRow = new QHBoxLayout;
    sideRow->setSpacing(0);
    m_side = new QButtonGroup(this);
    for (int i = 0; i < 2; ++i) {
        auto* b = new QPushButton(i == 0 ? tr("Blancs") : tr("Noirs"));
        b->setObjectName("Segment");
        b->setCheckable(true);
        b->setProperty("position", i == 0 ? "first" : "last");
        m_side->addButton(b, i);
        sideRow->addWidget(b);
    }

    // ---------------- Castling
    auto* castles = new QGridLayout;
    castles->setHorizontalSpacing(16);
    const QString labels[4] = {tr("Blancs  O-O"), tr("Blancs  O-O-O"), tr("Noirs  O-O"), tr("Noirs  O-O-O")};
    for (int i = 0; i < 4; ++i) {
        m_castle[i] = new QCheckBox(labels[i]);
        castles->addWidget(m_castle[i], i / 2, i % 2);
        connect(m_castle[i], &QCheckBox::toggled, this, &PositionEditorDialog::refresh);
    }

    // ---------------- En passant, counters
    m_ep = new QComboBox;
    m_fullmove = new QSpinBox;
    m_fullmove->setRange(1, 9999);
    m_halfmove = new QSpinBox;
    m_halfmove->setRange(0, 150);
    m_halfmove->setToolTip(tr("Demi-coups depuis la dernière prise ou le dernier coup de pion (règle des 50 coups)"));
    auto* counters = new QGridLayout;
    counters->setHorizontalSpacing(10);
    counters->setVerticalSpacing(4);
    counters->addWidget(section(tr("En passant")), 0, 0);
    counters->addWidget(section(tr("Coup n°")), 0, 1);
    counters->addWidget(section(tr("Règle des 50")), 0, 2);
    counters->addWidget(m_ep, 1, 0);
    counters->addWidget(m_fullmove, 1, 1);
    counters->addWidget(m_halfmove, 1, 2);

    // ---------------- Transformations
    auto* mirror = new QPushButton(theme::icon("mirror"), tr("Miroir"));
    mirror->setToolTip(tr("Symétrie gauche-droite (colonnes a↔h)"));
    auto* invert = new QPushButton(theme::icon("invert"), tr("Inverser"));
    invert->setToolTip(tr("Échange les couleurs : la position vue de l'autre camp"));
    auto* flip = new QPushButton(theme::icon("flip"), tr("Retourner"));
    flip->setToolTip(tr("Retourne la vue de l'échiquier"));
    auto* transforms = new QHBoxLayout;
    transforms->addWidget(mirror);
    transforms->addWidget(invert);
    transforms->addWidget(flip);

    // ---------------- Right column
    auto* side = new QVBoxLayout;
    side->setSpacing(8);
    side->addWidget(section(tr("Pièces")));
    side->addLayout(palette);
    side->addWidget(hint);
    side->addSpacing(6);
    side->addLayout(quick);
    side->addSpacing(10);
    side->addWidget(section(tr("Trait")));
    side->addLayout(sideRow);
    side->addSpacing(10);
    side->addWidget(section(tr("Droits de roque")));
    side->addLayout(castles);
    side->addSpacing(10);
    side->addLayout(counters);
    side->addSpacing(10);
    side->addWidget(section(tr("Transformer")));
    side->addLayout(transforms);
    side->addStretch(1);

    auto* sideWidget = new QWidget;
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(330);

    // ---------------- Bottom: FEN, state, buttons
    m_fen = new QLineEdit;
    m_fen->setObjectName("FenEdit");
    QFont mono(QStringLiteral("JetBrains Mono"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(12);
    m_fen->setFont(mono);
    m_fen->setToolTip(tr("Modifiable : appuyez sur Entrée pour appliquer"));
    m_status = new QLabel;
    m_status->setObjectName("EditorStatus");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("Valider"));
    m_ok->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));

    auto* fenRow = new QHBoxLayout;
    auto* fenLabel = section(QStringLiteral("FEN"));
    fenRow->addWidget(fenLabel);
    fenRow->addWidget(m_fen, 1);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(buttons);

    auto* top = new QHBoxLayout;
    top->setSpacing(18);
    top->addWidget(m_board, 1);
    top->addWidget(sideWidget);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    layout->addLayout(top, 1);
    layout->addLayout(fenRow);
    layout->addLayout(bottom);

    // ---------------- Connections
    connect(m_board, &EditorBoard::changed, this, &PositionEditorDialog::refresh);
    connect(m_side, &QButtonGroup::idClicked, this, &PositionEditorDialog::refresh);
    connect(m_ep, &QComboBox::activated, this, &PositionEditorDialog::refresh);
    connect(m_fullmove, &QSpinBox::valueChanged, this, &PositionEditorDialog::refresh);
    connect(m_halfmove, &QSpinBox::valueChanged, this, &PositionEditorDialog::refresh);
    connect(m_fen, &QLineEdit::editingFinished, this, [this] {
        if (m_fen->text().trimmed() != buildFen()) loadFen(m_fen->text());
    });
    connect(clear, &QPushButton::clicked, this, [this] { loadFen(QStringLiteral("8/8/8/8/8/8/8/8 w - - 0 1")); });
    connect(initial, &QPushButton::clicked, this, [this] { loadFen(QString::fromLatin1(kStartBoard) + QStringLiteral(" w KQkq - 0 1")); });
    connect(paste, &QPushButton::clicked, this, [this] { loadFen(QApplication::clipboard()->text()); });
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(buildFen()); });
    connect(mirror, &QPushButton::clicked, this, &PositionEditorDialog::mirrorFiles);
    connect(invert, &QPushButton::clicked, this, &PositionEditorDialog::invertColors);
    connect(flip, &QPushButton::clicked, this, [this] { m_board->setFlipped(!m_board->isFlipped()); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (validate(&m_result).isEmpty()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadFen(QString::fromStdString(start.fen()));
}

void PositionEditorDialog::loadFen(const QString& fenText)
{
    const QStringList f = fenText.trimmed().split(' ', Qt::SkipEmptyParts);
    if (f.isEmpty()) return;
    EditorBoard::Squares sq;
    sq.fill(' ');
    int rank = 7, file = 0;
    for (QChar qc : f[0]) {
        const char c = qc.toLatin1();
        if (c == '/') {
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else if (isPieceChar(c) && rank >= 0 && file < 8) {
            sq[size_t(rank * 8 + file++)] = c;
        } else {
            m_status->setText(QStringLiteral("<span style='color:%1;'>●</span>&nbsp; %2").arg(theme::Bad.name(), tr("FEN illisible")));
            return;
        }
    }
    m_updating = true;
    m_board->setSquares(sq);
    m_side->button(f.value(1) == QLatin1String("b") ? 1 : 0)->setChecked(true);
    // Castling rights: reread by Position, which also understands rook files ("HAha", Chess960).
    const QString castling = f.value(2);
    const char flags[4] = {'K', 'Q', 'k', 'q'};
    chess::Position parsed;
    const bool known = parsed.setFen(fenText.trimmed().toStdString());
    for (int i = 0; i < 4; ++i) {
        m_castle[i]->setEnabled(true); // refresh() then removes the impossible rights
        m_castle[i]->setChecked(known ? (parsed.castlingRights() >> i) & 1 : castling.contains(QChar(flags[i])));
        // The rook of the right, to find it again on output even if another rook is further out
        const bool has = known && ((parsed.castlingRights() >> i) & 1);
        m_castleRookFile[i] = has ? int(parsed.castlingRook(i < 2 ? chess::White : chess::Black, i % 2 == 0)) & 7 : -1;
    }
    m_halfmove->setValue(f.value(4, QStringLiteral("0")).toInt());
    m_fullmove->setValue(std::max(1, f.value(5, QStringLiteral("1")).toInt()));
    m_ep->clear();
    m_ep->addItem(QStringLiteral("—"), QString());
    const QString ep = f.value(3);
    if (ep.size() == 2) m_ep->addItem(ep, ep); // kept if valid, see refresh()
    m_ep->setCurrentIndex(m_ep->count() - 1);
    m_updating = false;
    refresh();
}

QString PositionEditorDialog::buildFen() const
{
    const auto& sq = m_board->squares();
    QString board;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            const char c = sq[size_t(r * 8 + f)];
            if (c == ' ') {
                ++empty;
                continue;
            }
            if (empty) board += QString::number(empty);
            empty = 0;
            board += QChar(c);
        }
        if (empty) board += QString::number(empty);
        if (r) board += '/';
    }
    QString castling;
    const char flags[4] = {'K', 'Q', 'k', 'q'};
    for (int i = 0; i < 4; ++i) {
        if (!m_castle[i]->isChecked()) continue;
        // Rook of the right still in place, on the right side of the king, but not the outermost: the right is written
        // with the rook's file ("B", "g"), otherwise "K" or "Q" would designate the other rook.
        const int base = i < 2 ? 0 : 56, file = m_castleRookFile[i];
        const char king = i < 2 ? 'K' : 'k', rook = i < 2 ? 'R' : 'r';
        const bool kingside = i % 2 == 0;
        int kingFile = -1, outer = -1;
        for (int f = 0; f < 8; ++f)
            if (sq[size_t(base + f)] == king) kingFile = f;
        for (int f = 0; f < 8 && kingFile >= 0; ++f)
            if (sq[size_t(base + f)] == rook && (kingside ? f > kingFile : f < kingFile) && (outer < 0 || kingside)) outer = f;
        const bool inner = file >= 0 && kingFile >= 0 && sq[size_t(base + file)] == rook && (kingside ? file > kingFile : file < kingFile) && file != outer;
        castling += inner ? QChar((i < 2 ? 'A' : 'a') + file) : QChar(flags[i]);
    }
    const QString ep = m_ep->currentData().toString();
    return QStringLiteral("%1 %2 %3 %4 %5 %6")
        .arg(board, m_side->checkedId() == 1 ? QStringLiteral("b") : QStringLiteral("w"), castling.isEmpty() ? QStringLiteral("-") : castling,
             ep.isEmpty() ? QStringLiteral("-") : ep)
        .arg(m_halfmove->value())
        .arg(m_fullmove->value());
}

void PositionEditorDialog::refresh()
{
    if (m_updating) return;
    m_updating = true;
    const auto& sq = m_board->squares();

    // Possible castling right: king on its first rank and a rook on that side (Chess960 included;
    // in a classical position: king on e, rook on a or h). The right bears on the rook read from the position, otherwise
    // on the outermost rook (see buildFen).
    bool possible[4] = {false, false, false, false};
    for (int color = 0; color < 2; ++color) {
        const int base = color == 0 ? 0 : 56;
        const char king = color == 0 ? 'K' : 'k', rook = color == 0 ? 'R' : 'r';
        int kingFile = -1;
        for (int f = 0; f < 8; ++f)
            if (sq[size_t(base + f)] == king) kingFile = f;
        for (int f = 0; f < 8 && kingFile >= 0; ++f)
            if (sq[size_t(base + f)] == rook) possible[color * 2 + (f > kingFile ? 0 : 1)] = true;
    }
    for (int i = 0; i < 4; ++i) {
        m_castle[i]->setEnabled(possible[i]);
        if (!possible[i]) m_castle[i]->setChecked(false);
    }

    // Plausible en passant squares: opponent pawn having just advanced two squares,
    // squares crossed empty, and a pawn of the side to move ready to capture.
    const bool white = m_side->checkedId() != 1;
    const QString keep = m_ep->currentData().toString();
    m_ep->clear();
    m_ep->addItem(QStringLiteral("—"), QString());
    const int pawnRank = white ? 4 : 3, behind = white ? 5 : 2, origin = white ? 6 : 1;
    const char enemyPawn = white ? 'p' : 'P', ownPawn = white ? 'P' : 'p';
    for (int f = 0; f < 8; ++f) {
        if (sq[size_t(pawnRank * 8 + f)] != enemyPawn || sq[size_t(behind * 8 + f)] != ' ' || sq[size_t(origin * 8 + f)] != ' ') continue;
        const bool attacker = (f > 0 && sq[size_t(pawnRank * 8 + f - 1)] == ownPawn) || (f < 7 && sq[size_t(pawnRank * 8 + f + 1)] == ownPawn);
        if (!attacker) continue;
        const QString name = QString(QChar('a' + f)) + QChar('1' + behind);
        m_ep->addItem(name, name);
    }
    m_ep->setCurrentIndex(std::max(0, m_ep->findData(keep)));
    m_ep->setEnabled(m_ep->count() > 1);

    if (!m_fen->hasFocus()) m_fen->setText(buildFen());
    m_updating = false;

    Position p;
    const QString error = validate(&p);
    QString text;
    if (error.isEmpty()) {
        text = tr("Position valide");
        if (p.isCheckmate()) text += tr(" — échec et mat");
        else if (p.isStalemate()) text += tr(" — pat");
        else if (p.inCheck()) text += tr(" — échec");
    }
    m_status->setText(QStringLiteral("<span style='color:%1;'>●</span>&nbsp; %2").arg((error.isEmpty() ? theme::Good : theme::Bad).name(), (error.isEmpty() ? text : error).toHtmlEscaped()));
    m_ok->setEnabled(error.isEmpty());
}

QString PositionEditorDialog::validate(Position* out) const
{
    const auto& sq = m_board->squares();
    int count[128] = {};
    for (char c : sq) ++count[static_cast<unsigned char>(c)];
    if (count['K'] != 1) return tr("Il faut exactement un roi blanc");
    if (count['k'] != 1) return tr("Il faut exactement un roi noir");
    for (int f = 0; f < 8; ++f)
        if (std::toupper(static_cast<unsigned char>(sq[size_t(f)])) == 'P' || std::toupper(static_cast<unsigned char>(sq[size_t(56 + f)])) == 'P')
            return tr("Un pion ne peut pas être sur la 1re ou la 8e rangée");
    if (count['P'] > 8 || count['p'] > 8) return tr("Plus de 8 pions dans un camp");
    int white = 0, black = 0;
    for (char c : sq) {
        if (c == ' ') continue;
        (std::isupper(static_cast<unsigned char>(c)) ? white : black)++;
    }
    if (white > 16 || black > 16) return tr("Plus de 16 pièces dans un camp");

    Position p;
    if (!p.setFen(buildFen().toStdString())) return tr("Position invalide");
    const Color stm = p.sideToMove();
    if (p.isAttacked(p.kingSquare(~stm), stm))
        return stm == White ? tr("Le roi noir est en échec alors que les Blancs ont le trait") : tr("Le roi blanc est en échec alors que les Noirs ont le trait");
    if (out) *out = p;
    return {};
}

void PositionEditorDialog::mirrorFiles()
{
    EditorBoard::Squares s = m_board->squares(), m;
    for (int sq = 0; sq < 64; ++sq) m[size_t((sq & ~7) | (7 - (sq & 7)))] = s[size_t(sq)];
    m_board->setSquares(m);
    for (QCheckBox* c : m_castle) c->setChecked(false); // castling no longer makes sense
    for (int& f : m_castleRookFile) f = -1;
    refresh();
}

void PositionEditorDialog::invertColors()
{
    // Top-bottom mirror + colour swap + side to move reversed: same position, sides swapped.
    EditorBoard::Squares s = m_board->squares(), m;
    for (int sq = 0; sq < 64; ++sq) {
        char c = s[size_t(sq)];
        if (c != ' ') c = std::isupper(static_cast<unsigned char>(c)) ? char(std::tolower(c)) : char(std::toupper(c));
        m[size_t(((7 - (sq >> 3)) << 3) | (sq & 7))] = c;
    }
    const bool rights[4] = {m_castle[0]->isChecked(), m_castle[1]->isChecked(), m_castle[2]->isChecked(), m_castle[3]->isChecked()};
    std::swap(m_castleRookFile[0], m_castleRookFile[2]); // the rooks keep their file, the sides are swapped
    std::swap(m_castleRookFile[1], m_castleRookFile[3]);
    m_updating = true;
    m_board->setSquares(m);
    m_side->button(m_side->checkedId() == 1 ? 0 : 1)->setChecked(true);
    m_castle[0]->setChecked(rights[2]);
    m_castle[1]->setChecked(rights[3]);
    m_castle[2]->setChecked(rights[0]);
    m_castle[3]->setChecked(rights[1]);
    m_updating = false;
    refresh();
}
