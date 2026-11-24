#include "ecobrowser.h"

#include "openings.h"
#include "theme.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {
enum Role { RangeFrom = Qt::UserRole, RangeTo, Moves };
int code(char letter, int n) { return (letter - 'A') * 100 + n + 1; } // parseEco
} // namespace

EcoBrowser::EcoBrowser(std::vector<int> counts, QWidget* parent)
    : QDialog(parent)
    , m_counts(std::move(counts))
{
    setWindowTitle(tr("Navigateur ECO"));
    resize(760, 620);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Chercher un nom ou un code : « sicilienne », « najdorf », « B90 »…"));
    m_search->setClearButtonEnabled(true);
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("ECO"), tr("Ouverture"), tr("Parties")});
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setColumnWidth(0, 120);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setObjectName("Faint");
    auto* filter = new QPushButton(theme::icon("search"), tr("Filtrer la base"));
    auto* play = new QPushButton(theme::icon("analysis"), tr("Jouer la ligne"));
    filter->setEnabled(false);
    play->setEnabled(false);
    filter->setVisible(!m_counts.empty());
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    buttons->addButton(filter, QDialogButtonBox::ActionRole);
    buttons->addButton(play, QDialogButtonBox::ActionRole);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_info);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_search, &QLineEdit::textChanged, this, &EcoBrowser::applySearch);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this, filter, play](QTreeWidgetItem* it) {
        const QString moves = it ? it->data(0, Moves).toString() : QString();
        filter->setEnabled(it && rangeOf(it).first > 0);
        play->setEnabled(!moves.isEmpty());
        m_info->setText(moves);
    });
    connect(filter, &QPushButton::clicked, this, [this] {
        const auto r = rangeOf(m_tree->currentItem());
        if (r.first > 0) emit filterRequested(r.first, r.second);
    });
    connect(play, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem* it = m_tree->currentItem(); it && !it->data(0, Moves).toString().isEmpty()) emit lineRequested(it->data(0, Moves).toString());
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        if (!it->data(0, Moves).toString().isEmpty()) emit lineRequested(it->data(0, Moves).toString());
    });
    build();
}

std::pair<int, int> EcoBrowser::rangeOf(const QTreeWidgetItem* item)
{
    if (!item) return {0, 0};
    return {item->data(0, RangeFrom).toInt(), item->data(0, RangeTo).toInt()};
}

void EcoBrowser::build()
{
    const auto& lines = openingsBook().lines();
    const QString volumes[5] = {tr("Ouvertures de flanc"), tr("Défenses semi-ouvertes (sauf française)"), tr("Jeux ouverts et défense française"),
                                tr("Jeux fermés et semi-fermés"), tr("Défenses indiennes")};
    auto count = [this](int from, int to) {
        int n = 0;
        for (int c = from; c <= to && c < int(m_counts.size()); ++c) n += m_counts[size_t(c)];
        return n;
    };
    auto setRange = [&](QTreeWidgetItem* it, int from, int to) {
        it->setData(0, RangeFrom, from);
        it->setData(0, RangeTo, to);
        if (!m_counts.empty()) {
            it->setText(2, QString::number(count(from, to)));
            it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        }
    };
    QTreeWidgetItem* groups[5][10] = {};
    QHash<QString, QTreeWidgetItem*> codes;
    for (int v = 0; v < 5; ++v) {
        const char letter = char('A' + v);
        auto* vol = new QTreeWidgetItem(m_tree, {QString(QChar(letter)), volumes[v]});
        setRange(vol, code(letter, 0), code(letter, 99));
        for (int g = 0; g < 10; ++g) {
            auto* grp = new QTreeWidgetItem(vol, {QStringLiteral("%1%2").arg(QChar(letter)).arg(g)});
            setRange(grp, code(letter, g * 10), code(letter, g * 10 + 9));
            groups[v][g] = grp;
        }
    }
    for (const auto& l : lines) {
        const QString eco = QString::fromStdString(l.eco);
        const int v = eco[0].unicode() - 'A', n = eco.mid(1).toInt();
        if (v < 0 || v > 4 || n < 0 || n > 99) continue;
        QTreeWidgetItem*& c = codes[eco];
        if (!c) {
            c = new QTreeWidgetItem(groups[v][n / 10], {eco, openingName(openingsBook().nameOf(l.eco))});
            setRange(c, code(char('A' + v), n), code(char('A' + v), n));
            c->setData(0, Moves, QString::fromStdString(l.moves)); // shortest line: the first one read
        }
        auto* line = new QTreeWidgetItem(c, {QString(), openingName(l.name)});
        line->setData(0, Moves, QString::fromStdString(l.moves));
        line->setToolTip(1, QString::fromStdString(l.moves));
    }
    // Group names: first named code
    for (int v = 0; v < 5; ++v)
        for (int g = 0; g < 10; ++g)
            if (QTreeWidgetItem* grp = groups[v][g]; grp->childCount())
                grp->setText(1, grp->child(0)->text(1) + (grp->childCount() > 1 ? QStringLiteral(" …") : QString()));
}

void EcoBrowser::applySearch(const QString& text)
{
    const QString t = text.trimmed();
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* it) {
        bool any = t.isEmpty() || it->text(0).startsWith(t, Qt::CaseInsensitive) || it->text(1).contains(t, Qt::CaseInsensitive);
        const bool self = any;
        for (int i = 0; i < it->childCount(); ++i) any = visit(it->child(i)) || any;
        // a parent found shows all its children
        if (self && !t.isEmpty())
            for (int i = 0; i < it->childCount(); ++i) it->child(i)->setHidden(false);
        it->setHidden(!any);
        if (!t.isEmpty()) it->setExpanded(any && !self);
        return any;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) visit(m_tree->topLevelItem(i));
    if (t.isEmpty()) m_tree->collapseAll();
}
