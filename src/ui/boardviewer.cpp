#include "boardviewer.h"
#include "boardwidget.h"
#include "chessclock.h"
#include "display.h"
#include "preferences.h"
#include "evalbar.h"
#include "gamesession.h"
#include "openings.h"
#include "theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

using namespace chess;

namespace {

int materialBalance(const Position& pos) // positive = White's advantage, in pawns
{
    static const int values[] = {0, 1, 3, 3, 5, 9, 0};
    int score = 0;
    for (int s = 0; s < 64; ++s) {
        const Piece p = pos.at(Square(s));
        if (p == NoPiece) continue;
        score += (colorOf(p) == White ? 1 : -1) * values[typeOf(p)];
    }
    return score;
}

QToolButton* transportButton(const QString& icon, const QString& tip)
{
    auto* b = new QToolButton;
    b->setObjectName("TransportButton");
    b->setIcon(theme::icon(icon));
    b->setIconSize(QSize(18, 18));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

} // namespace

// ---------------------------------------------------------------- PlayerStrip

PlayerStrip::PlayerStrip(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(30);
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(24, 0, 4, 0);
    l->setSpacing(8);
    m_name = new QLabel;
    m_name->setObjectName("PlayerName");
    m_elo = new QLabel;
    m_elo->setObjectName("PlayerElo");
    m_material = new QLabel;
    m_material->setObjectName("Dim");
    m_status = new QLabel;
    m_status->setObjectName("PlayerStatus");
    l->addWidget(m_name);
    l->addWidget(m_elo);
    l->addWidget(m_status);
    l->addStretch(1);
    l->addWidget(m_material);
    m_clock = new QLabel;
    m_clock->setObjectName("Clock");
    m_clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_clock->setMinimumWidth(72);
    m_clock->hide();
    l->addWidget(m_clock);
}

void PlayerStrip::set(bool white, const QString& name, const QString& elo, int materialAdvantage, bool toMove)
{
    m_white = white;
    m_toMove = toMove;
    Q_UNUSED(white);
    m_name->setText(name.isEmpty() ? QStringLiteral("?") : name);
    m_elo->setText(elo);
    m_material->setText(materialAdvantage > 0 ? QStringLiteral("+%1").arg(materialAdvantage) : QString());
    update();
}

void PlayerStrip::setStatus(const QString& text) { m_status->setText(text); }

void PlayerStrip::setClock(const QString& text, bool active, bool low)
{
    m_clock->setVisible(!text.isEmpty());
    m_clock->setText(text);
    if (m_clock->property("active").toBool() != active || m_clock->property("low").toBool() != low) {
        m_clock->setProperty("active", active);
        m_clock->setProperty("low", low);
        m_clock->style()->unpolish(m_clock); // reapplies the style sheet
        m_clock->style()->polish(m_clock);
    }
}

void PlayerStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(10, height() / 2.0);
    // Accent halo when it is the player's turn
    if (m_toMove) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme::AccentSoft);
        p.drawEllipse(c, 9, 9);
    }
    p.setPen(QPen(m_toMove ? theme::Accent : theme::BorderSoft, 1.4));
    p.setBrush(m_white ? QColor(0xee, 0xec, 0xe6) : QColor(0x14, 0x14, 0x16));
    p.drawEllipse(c, 5.5, 5.5);
}

// ---------------------------------------------------------------- Banner

Banner::Banner(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("Banner");
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::PointingHandCursor);
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(28, 18, 28, 18);
    l->setSpacing(4);
    m_title = new QLabel;
    m_title->setObjectName("BannerTitle");
    m_title->setAlignment(Qt::AlignCenter);
    m_text = new QLabel;
    m_text->setObjectName("BannerText");
    m_text->setAlignment(Qt::AlignCenter);
    // End-of-game choices, on two rows: save / analyse, rematch / new game.
    m_actions = new QWidget;
    auto* grid = new QGridLayout(m_actions);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);
    auto button = [this, grid](const char* icon, const QString& label, int row, int col, bool primary, auto signal) {
        auto* b = new QPushButton(theme::icon(QLatin1String(icon)), label);
        if (primary) b->setObjectName("Primary");
        b->setCursor(Qt::PointingHandCursor);
        grid->addWidget(b, row, col);
        connect(b, &QPushButton::clicked, this, [this, signal] {
            hide();
            emit (this->*signal)();
        });
        return b;
    };
    button("save", tr("Enregistrer dans la base"), 0, 0, true, &Banner::saveRequested);
    button("analysis", tr("Analyser la partie"), 0, 1, false, &Banner::analyseRequested);
    button("flip", tr("Revanche"), 1, 0, false, &Banner::rematchRequested);
    button("engine", tr("Nouvelle partie…"), 1, 1, false, &Banner::newGameRequested);
    auto* close = new QPushButton(tr("Fermer"));
    close->setFlat(true);
    close->setObjectName("BannerClose");
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    grid->addWidget(close, 2, 0, 1, 2, Qt::AlignHCenter);

    l->addWidget(m_title);
    l->addWidget(m_text);
    l->addSpacing(10);
    l->addWidget(m_actions);
    hide();
}

void Banner::setText(const QString& title, const QString& text, bool offerActions)
{
    m_title->setText(title);
    m_text->setText(text);
    m_actions->setVisible(offerActions);
    adjustSize();
}

// ---------------------------------------------------------------- BoardArea

BoardArea::BoardArea(EvalBar* bar, BoardWidget* board, PlayerStrip* top, PlayerStrip* bottom, Banner* banner, QWidget* parent)
    : QWidget(parent)
    , m_bar(bar)
    , m_board(board)
    , m_top(top)
    , m_bottom(bottom)
    , m_banner(banner)
{
    for (QWidget* w : std::initializer_list<QWidget*>{bar, board, top, bottom, banner}) w->setParent(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(260, 300);
}

void BoardArea::resizeEvent(QResizeEvent*) { relayout(); }

void BoardArea::relayout()
{
    const int stripH = m_top->height();
    const int barW = m_bar->isVisible() ? m_bar->width() + 2 : 0;
    const int side = std::max(100, std::min(height() - 2 * stripH, width() - barW - 16));
    const int x = (width() - side - barW) / 2 + barW;
    const int y = (height() - side) / 2;
    m_board->setGeometry(x, y, side, side);
    // The bar covers the board's margin so as to sit against it
    m_bar->setGeometry(x - barW + 8, y, m_bar->width(), side);
    m_bar->raise();
    // Strips aligned on the inner edge of the board (10 px margin)
    m_top->setGeometry(x + 6, y - stripH + 8, side - 12, stripH);
    m_bottom->setGeometry(x + 6, y + side - 8, side - 12, stripH);
    // Banner centred on the board
    const QSize b = m_banner->sizeHint().expandedTo(QSize(std::min(320, side - 40), 0));
    m_banner->setGeometry(x + (side - b.width()) / 2, y + (side - b.height()) / 2, b.width(), b.height());
    m_banner->raise();
}

// ---------------------------------------------------------------- BoardViewer

BoardViewer::BoardViewer(GameSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    setObjectName("Viewer");
    setAttribute(Qt::WA_StyledBackground);

    m_board = new BoardWidget;
    m_evalBar = new EvalBar;
    m_top = new PlayerStrip;
    m_bottom = new PlayerStrip;

    m_banner = new Banner;
    m_area = new BoardArea(m_evalBar, m_board, m_top, m_bottom, m_banner);
    connect(m_banner, &Banner::saveRequested, this, &BoardViewer::saveRequested);
    connect(m_banner, &Banner::analyseRequested, this, &BoardViewer::analyseRequested);
    connect(m_banner, &Banner::rematchRequested, this, &BoardViewer::rematchRequested);
    connect(m_banner, &Banner::newGameRequested, this, &BoardViewer::newGameRequested);

    // Transport bar, like under Resolve's viewer
    auto* transport = new QWidget;
    transport->setObjectName("Transport");
    transport->setFixedHeight(40);
    auto* t = new QHBoxLayout(transport);
    t->setContentsMargins(14, 0, 10, 0);
    t->setSpacing(4);
    m_moveInfo = new ElidedLabel;
    m_moveInfo->setObjectName("Faint");
    m_moveInfo->setMinimumWidth(100);
    m_moveInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // long name: cut (…)
    auto* first = transportButton("first", tr("Début (Origine)"));
    auto* prev = transportButton("prev", tr("Coup précédent (←)"));
    auto* next = transportButton("next", tr("Coup suivant (→)"));
    auto* last = transportButton("last", tr("Fin (Fin)"));
    auto* flip = transportButton("flip", tr("Retourner l'échiquier (F)"));
    auto* edit = transportButton("edit", tr("Éditer la position (Ctrl+E)"));
    m_sendButton = transportButton("analysis", tr("Envoyer dans l'analyse : copier cette partie dans la page Analyse (Ctrl+Maj+A)"));
    m_sendButton->hide();
    auto* spacer = new QWidget;
    spacer->setMinimumWidth(0);
    t->addWidget(m_moveInfo, 1);
    t->addWidget(first);
    t->addWidget(prev);
    t->addWidget(next);
    t->addWidget(last);
    t->addStretch(1);
    t->addWidget(spacer);
    t->addWidget(m_sendButton);
    t->addWidget(edit);
    t->addWidget(flip);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_area, 1);
    layout->addWidget(transport);

    connect(first, &QToolButton::clicked, session, &GameSession::toStart);
    connect(prev, &QToolButton::clicked, session, &GameSession::back);
    connect(next, &QToolButton::clicked, session, &GameSession::forward);
    connect(last, &QToolButton::clicked, session, &GameSession::toEnd);
    connect(flip, &QToolButton::clicked, this, &BoardViewer::flipRequested);
    connect(m_sendButton, &QToolButton::clicked, this, &BoardViewer::sendToAnalysisRequested);
    connect(edit, &QToolButton::clicked, this, &BoardViewer::editRequested);
    // The banner disappears as soon as the position changes (connected before EngineGame,
    // which may show it again on the final move).
    connect(session, &GameSession::currentChanged, m_banner, &QWidget::hide);
    connect(session, &GameSession::currentChanged, this, &BoardViewer::refresh);
    connect(session, &GameSession::headersChanged, this, &BoardViewer::refresh);
    connect(session, &GameSession::treeChanged, this, &BoardViewer::refresh); // moves taken back, variations
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, &BoardViewer::refresh);
    refresh();
}

void BoardViewer::setFlipped(bool flipped)
{
    m_board->setFlipped(flipped);
    m_evalBar->setFlipped(flipped);
    refresh();
}

bool BoardViewer::isFlipped() const { return m_board->isFlipped(); }

void BoardViewer::setPlayerStatus(bool white, const QString& text)
{
    PlayerStrip* strip = (white != isFlipped()) ? m_bottom : m_top;
    PlayerStrip* other = strip == m_bottom ? m_top : m_bottom;
    strip->setStatus(text);
    other->setStatus(QString());
}

void BoardViewer::setLiveClocks(bool live, qint64 whiteMs, qint64 blackMs, int runningSide)
{
    m_liveClocks = live;
    m_liveMs[0] = whiteMs;
    m_liveMs[1] = blackMs;
    m_running = runningSide;
    updateClocks();
}

void BoardViewer::setPreviewClocks(qint64 whiteMs, qint64 blackMs)
{
    m_previewMs[0] = whiteMs;
    m_previewMs[1] = blackMs;
    updateClocks();
}

void BoardViewer::updateClocks()
{
    qint64 ms[2] = {-1, -1};
    if (m_liveClocks) {
        ms[0] = m_liveMs[0];
        ms[1] = m_liveMs[1];
    } else {
        // Replay: last recorded time of each side up to the displayed move
        for (const Node* n = m_session->current(); n && n->parent && (ms[0] < 0 || ms[1] < 0); n = n->parent) {
            const int mover = n->parent->pos.sideToMove() == White ? 0 : 1;
            if (ms[mover] < 0 && n->clockMs >= 0) ms[mover] = n->clockMs;
        }
        // No recorded times in the game: the time control of the next game (Play page).
        if (ms[0] < 0 && ms[1] < 0) {
            ms[0] = m_previewMs[0];
            ms[1] = m_previewMs[1];
        }
    }
    const bool show = ms[0] >= 0 || ms[1] >= 0;
    emit clocksChanged(ms[0], ms[1], m_liveClocks ? m_running : -1, m_liveClocks);
    for (int side = 0; side < 2; ++side) {
        PlayerStrip* strip = (side == 0) != isFlipped() ? m_bottom : m_top;
        if (!show) {
            strip->setClock(QString(), false, false);
            continue;
        }
        const qint64 v = std::max<qint64>(0, ms[side]);
        strip->setClock(ms[side] < 0 ? QStringLiteral("–") : ChessClock::format(v), m_liveClocks && m_running == side,
                        m_liveClocks && v < 10000);
    }
}

bool BoardViewer::isBannerVisible() const { return m_banner->isVisible(); }
void BoardViewer::hideBanner() { m_banner->hide(); }

void BoardViewer::showBanner(const QString& title, const QString& text, bool offerActions)
{
    m_banner->setText(title, text, offerActions);
    m_banner->show();
    m_area->relayout();
}

void BoardViewer::setSendToAnalysisVisible(bool visible) { m_sendButton->setVisible(visible); }

void BoardViewer::setEvalBarVisible(bool visible)
{
    m_evalBar->setVisible(visible);
    m_area->relayout();
}

void ElidedLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setPen(palette().color(foregroundRole()));
    const QRect r = contentsRect();
    p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(text(), Qt::ElideRight, r.width()));
}

QString BoardViewer::moveInfoText() const { return m_moveInfo->text(); }

QString BoardViewer::openingAt(const chess::Node* node)
{
    const chess::eco::Book& book = openingsBook();
    for (const chess::Node* n = node; n; n = n->parent) {
        if (!n->parent) break; // starting position: not an opening
        if (const chess::eco::Opening* o = book.lookup(n->pos))
            return QStringLiteral("%1 %2").arg(QString::fromStdString(o->eco), openingName(o->name));
    }
    return {};
}

void BoardViewer::refresh()
{
    const Game& g = m_session->game();
    const Node* cur = m_session->current();
    const Position& pos = cur->pos;
    const int balance = materialBalance(pos);
    const bool whiteToMove = pos.sideToMove() == White;

    auto q = [](const std::string& s) { return QString::fromStdString(s); };
    PlayerStrip* whiteStrip = isFlipped() ? m_top : m_bottom;
    PlayerStrip* blackStrip = isFlipped() ? m_bottom : m_top;
    whiteStrip->set(true, q(g.header("White")), q(g.header("WhiteElo")), balance, whiteToMove);
    blackStrip->set(false, q(g.header("Black")), q(g.header("BlackElo")), -balance, !whiteToMove);

    QString lastMove;
    if (cur->parent) {
        const Position& before = cur->parent->pos;
        lastMove = tr("Coup %1%2 %3")
                       .arg(before.fullmoveNumber())
                       .arg(before.sideToMove() == White ? QStringLiteral(".") : QStringLiteral("…"))
                       .arg(display::san(cur->san));
    } else {
        lastMove = tr("Position initiale");
    }
    // Opening: the deepest one recognised between the displayed position and the start of the game
    // (whatever the move order); unknown: the last move.
    const QString opening = openingAt(cur);
    QString info = opening.isEmpty() ? lastMove : opening;
    if (pos.isCheckmate()) info += tr("  ·  mat");
    else if (pos.isStalemate()) info += tr("  ·  pat");
    m_moveInfo->setText(info);
    m_moveInfo->setToolTip(opening.isEmpty() ? QString() : opening + QStringLiteral("\n") + lastMove);
    updateClocks();
}
