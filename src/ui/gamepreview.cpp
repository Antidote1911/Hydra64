#include "gamepreview.h"
#include "display.h"

#include "boardwidget.h"
#include "theme.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

using namespace chess;

GamePreviewDialog::GamePreviewDialog(Game game, const QString& title, QWidget* parent)
    : QDialog(parent)
    , m_game(std::move(game))
    , m_line(m_game.mainline())
{
    setWindowTitle(tr("Aperçu : %1").arg(title));
    resize(820, 520);
    m_board = new BoardWidget;
    m_board->setMovableSides(false, false);
    m_board->setMinimumSize(360, 360);
    m_header = new QLabel;
    m_header->setWordWrap(true);
    m_moves = new QTextBrowser;
    m_moves->setOpenLinks(false);
    auto button = [this](const char* icon, const QString& tip, auto slot) {
        auto* b = new QToolButton;
        b->setIcon(theme::icon(icon));
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, slot);
        return b;
    };
    auto* nav = new QHBoxLayout;
    nav->addStretch(1);
    nav->addWidget(button("first", tr("Début"), [this] { goTo(0); }));
    nav->addWidget(button("prev", tr("Coup précédent"), [this] { goTo(m_ply - 1); }));
    nav->addWidget(button("next", tr("Coup suivant"), [this] { goTo(m_ply + 1); }));
    nav->addWidget(button("last", tr("Fin"), [this] { goTo(plies()); }));
    nav->addStretch(1);
    auto* left = new QVBoxLayout;
    left->addWidget(m_board, 1);
    left->addLayout(nav);
    auto* right = new QVBoxLayout;
    right->addWidget(m_header);
    right->addWidget(m_moves, 1);
    auto* body = new QHBoxLayout;
    body->addLayout(left, 3);
    body->addLayout(right, 2);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    auto* open = buttons->addButton(tr("Ouvrir la partie"), QDialogButtonBox::AcceptRole);
    open->setObjectName("Primary");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(open, &QPushButton::clicked, this, [this] {
        emit openRequested();
        accept();
    });
    connect(m_moves, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) { goTo(url.fragment().toInt()); });
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);
    auto h = [&](const char* k) { return QString::fromStdString(m_game.header(k)).toHtmlEscaped(); };
    m_header->setText(QStringLiteral("<b>%1 – %2</b> &nbsp; %3<br><span style='color:gray'>%4 · %5 · %6 · %7</span>")
                          .arg(h("White"), h("Black"), h("Result"), h("Event"), h("Site"), h("Date"), h("ECO")));
    goTo(0);
}

void GamePreviewDialog::goTo(int ply)
{
    m_ply = std::clamp(ply, 0, plies());
    refresh();
}

void GamePreviewDialog::refresh()
{
    const Node* n = m_ply == 0 ? m_game.root() : m_line[size_t(m_ply - 1)];
    m_board->setPosition(n->pos, n == m_game.root() ? Move{} : n->move, true);
    m_board->setGraphics(n->graphics);
    // Clickable moves, the displayed move in bold
    QString html;
    for (int i = 0; i < plies(); ++i) {
        const Node* m = m_line[size_t(i)];
        const Position& before = m->parent->pos;
        if (before.sideToMove() == White) html += QStringLiteral("%1. ").arg(before.fullmoveNumber());
        else if (i == 0) html += QStringLiteral("%1… ").arg(before.fullmoveNumber());
        const QString san = display::san(before.toSan(m->move)).toHtmlEscaped();
        html += i + 1 == m_ply ? QStringLiteral("<b style='color:#e68a35'>%1</b> ").arg(san)
                               : QStringLiteral("<a href='#%1' style='color:%3;text-decoration:none'>%2</a> ").arg(i + 1).arg(san, QColor(theme::Text).name());
    }
    m_moves->setHtml(html);
}

void GamePreviewDialog::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Left: goTo(m_ply - 1); return;
    case Qt::Key_Right: goTo(m_ply + 1); return;
    case Qt::Key_Home: goTo(0); return;
    case Qt::Key_End: goTo(plies()); return;
    default: QDialog::keyPressEvent(e);
    }
}

void GamePreviewDialog::wheelEvent(QWheelEvent* e)
{
    goTo(m_ply + (e->angleDelta().y() < 0 ? 1 : -1));
}
