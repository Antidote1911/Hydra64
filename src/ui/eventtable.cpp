#include "eventtable.h"

#include "theme.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString pointsText(double p)
{
    const int whole = int(p);
    const bool half = p - whole > 0.25;
    if (!half) return QString::number(whole);
    return whole ? QStringLiteral("%1½").arg(whole) : QStringLiteral("½");
}
} // namespace

EventTableDialog::EventTableDialog(const hy64db::Database& db, uint32_t event, QWidget* parent)
    : QDialog(parent)
    , m_db(db)
    , m_table(db.eventTable(event))
    , m_event(QString::fromStdString(db.names().name(hy64db::Event, event)))
{
    setWindowTitle(tr("Tableau croisé : %1").arg(m_event));
    resize(960, 560);
    const int n = int(m_table.players.size());
    // Columns: rank, player, Elo, one result per opponent, points, games, performance
    m_view = new QTableWidget(n, n + 6);
    QStringList headers{tr("#"), tr("Joueur"), tr("Elo")};
    for (int j = 0; j < n; ++j) headers << QString::number(j + 1);
    headers << tr("Points") << tr("Parties") << tr("Perf.");
    m_view->setHorizontalHeaderLabels(headers);
    m_view->verticalHeader()->hide();
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_view->setToolTip(tr("Double-clic : les parties (une case : les parties entre les deux joueurs ; un nom : celles du joueur)"));
    auto cell = [&](int r, int c, const QString& text, Qt::Alignment align = Qt::AlignCenter) {
        auto* it = new QTableWidgetItem(text);
        it->setTextAlignment(align);
        m_view->setItem(r, c, it);
        return it;
    };
    for (int i = 0; i < n; ++i) {
        const hy64db::EventTable::Player& p = m_table.players[size_t(i)];
        cell(i, 0, QString::number(i + 1));
        cell(i, 1, QString::fromStdString(db.names().name(hy64db::Player, p.id)), Qt::AlignLeft | Qt::AlignVCenter);
        cell(i, 2, p.elo ? QString::number(p.elo) : QString());
        for (int j = 0; j < n; ++j) {
            if (i == j) {
                cell(i, 3 + j, QStringLiteral("×"))->setForeground(theme::TextFaint);
                continue;
            }
            const uint32_t g = m_table.games[size_t(i)][size_t(j)];
            QString text;
            if (g == 1) text = pointsText(m_table.points[size_t(i)][size_t(j)]);
            else if (g > 1) text = QStringLiteral("%1/%2").arg(pointsText(m_table.points[size_t(i)][size_t(j)])).arg(g);
            cell(i, 3 + j, text);
        }
        cell(i, n + 3, pointsText(p.points))->setFont([&] {
            QFont f = font();
            f.setBold(true);
            return f;
        }());
        cell(i, n + 4, QString::number(p.games));
        cell(i, n + 5, p.performance ? QString::number(p.performance) : QString());
    }
    auto* note = new QLabel(tr("%n partie(s), %1 joueur(s).", nullptr, int(m_table.ids.size())).arg(n));
    note->setObjectName("Faint");
    auto* all = new QPushButton(theme::icon("database"), tr("Parties du tournoi"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    buttons->addButton(all, QDialogButtonBox::ActionRole);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addWidget(m_view, 1);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(all, &QPushButton::clicked, this, [this] { emit gamesRequested(m_table.ids, m_event); });
    connect(m_view, &QTableWidget::cellDoubleClicked, this, [this, n](int r, int c) {
        if (r < 0 || r >= n) return;
        const QString name = QString::fromStdString(m_db.names().name(hy64db::Player, m_table.players[size_t(r)].id));
        if (c >= 3 && c < 3 + n && c - 3 != r) {
            const QString other = QString::fromStdString(m_db.names().name(hy64db::Player, m_table.players[size_t(c - 3)].id));
            emit gamesRequested(m_table.pairGames[size_t(r)][size_t(c - 3)], tr("%1 – %2").arg(name, other));
            return;
        }
        std::vector<uint32_t> ids; // games of the player in the tournament
        for (const auto& row : m_table.pairGames[size_t(r)]) ids.insert(ids.end(), row.begin(), row.end());
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        emit gamesRequested(ids, name);
    });
}
