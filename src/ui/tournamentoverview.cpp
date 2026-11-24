#include "tournamentoverview.h"

#include "boardwidget.h"
#include "chessclock.h"
#include "tournament.h"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

using namespace chess;

TournamentOverview::TournamentOverview(Tournament* tournament, QWidget* parent)
    : QWidget(parent)
    , m_tournament(tournament)
    , m_grid(new QGridLayout)
    , m_empty(new QLabel(tr("Aucune partie en cours.")))
    , m_timer(new QTimer(this))
{
    m_empty->setAlignment(Qt::AlignCenter);
    m_grid->setSpacing(10);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->addWidget(m_empty);
    layout->addLayout(m_grid, 1);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &TournamentOverview::refreshClocks);
    connect(m_tournament, &Tournament::changed, this, [this] {
        if (isVisible()) rebuild();
    });
    connect(m_tournament, &Tournament::boardChanged, this, [this](int index) {
        if (isVisible()) refresh(index);
    });
}

void TournamentOverview::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    rebuild();
    m_timer->start();
}

void TournamentOverview::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

void TournamentOverview::setFlipped(bool flipped)
{
    m_flipped = flipped;
    for (Tile& t : m_tiles) t.board->setFlipped(flipped);
}

void TournamentOverview::rebuild()
{
    const QList<int> running = m_tournament->runningGames();
    if (running != m_order) {
        for (const Tile& t : std::as_const(m_tiles)) t.frame->deleteLater();
        m_tiles.clear();
        m_order = running;
        // Grid as square as possible
        const int columns = std::max(1, int(std::ceil(std::sqrt(double(running.size())))));
        for (int k = 0; k < running.size(); ++k) {
            Tile t;
            auto* frame = new QFrame;
            frame->setObjectName("TournamentTile");
            frame->setFrameShape(QFrame::StyledPanel);
            frame->setCursor(Qt::PointingHandCursor);
            frame->setToolTip(tr("Clic : afficher cette partie dans la notation. Double-clic : la suivre en grand."));
            frame->setProperty("game", running[k]);
            t.frame = frame;
            t.title = new QLabel;
            t.title->setAlignment(Qt::AlignCenter);
            t.board = new BoardWidget;
            t.board->setMinimumSize(96, 96);
            t.board->setMovableSides(false, false);
            t.board->setFlipped(m_flipped);
            t.clocks = new QLabel;
            t.clocks->setAlignment(Qt::AlignCenter);
            auto* l = new QVBoxLayout(frame);
            l->setContentsMargins(6, 6, 6, 6);
            l->setSpacing(4);
            l->addWidget(t.title);
            l->addWidget(t.board, 1);
            l->addWidget(t.clocks);
            // The click is taken on the whole tile, board included
            for (QWidget* w : {static_cast<QWidget*>(frame), static_cast<QWidget*>(t.title), static_cast<QWidget*>(t.board), static_cast<QWidget*>(t.clocks)}) {
                w->setProperty("game", running[k]);
                w->installEventFilter(this);
            }
            m_grid->addWidget(frame, k / columns, k % columns);
            m_tiles.insert(running[k], t);
        }
    }
    m_empty->setVisible(running.isEmpty());
    for (int index : running) refresh(index);
}

void TournamentOverview::refresh(int index)
{
    auto it = m_tiles.find(index);
    if (it == m_tiles.end()) return;
    const Game* game = m_tournament->runningGame(index);
    if (!game) return;
    const Tournament::GameRecord& rec = m_tournament->games()[index];
    const QString shown = index == m_tournament->currentIndex() ? QStringLiteral(" ●") : QString();
    it->title->setText(tr("n° %1 · %2 – %3").arg(index + 1).arg(rec.whiteName, rec.blackName) + shown);
    const Node* last = game->root();
    int plies = 0;
    while (last->mainChild()) last = last->mainChild(), ++plies;
    if (plies != it->plies) {
        it->board->setPosition(last->pos, last->move, false);
        it->plies = plies;
    }
    refreshClocks();
}

void TournamentOverview::refreshClocks()
{
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        const ChessClock* clock = m_tournament->runningClock(it.key());
        const Game* game = m_tournament->runningGame(it.key());
        if (!game) continue;
        const Node* last = game->root();
        while (last->mainChild()) last = last->mainChild();
        QString text = tr("coup %1").arg(last->pos.fullmoveNumber());
        if (clock && m_tournament->settings().limit == Tournament::Settings::Limit::Clock)
            text = QStringLiteral("%1  ·  %2  ·  %3").arg(ChessClock::format(clock->remaining(White)), text, ChessClock::format(clock->remaining(Black)));
        it->clocks->setText(text);
    }
}

bool TournamentOverview::eventFilter(QObject* watched, QEvent* event)
{
    const bool left = (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick) && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton;
    if (left) {
        const QVariant game = watched->property("game");
        if (game.isValid()) {
            if (event->type() == QEvent::MouseButtonDblClick) emit gameChosen(game.toInt()); // full size
            else if (isVisible()) emit gameSelected(game.toInt());                           // in the notation
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress && watched->property("game").isValid() && qobject_cast<BoardWidget*>(watched)) return true; // no square selection
    return QWidget::eventFilter(watched, event);
}
