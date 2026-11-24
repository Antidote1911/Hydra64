#include "surveybrowser.h"

#include "theme.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum Column { ColEco, ColTitle, ColAuthor, ColDate, ColGames };
enum Role { Path = Qt::UserRole, SortKey };

// Sorts on the key of the cell when it has one (ECO: surveys without code last; games: number).
class Item : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int c = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(c, SortKey), b = other.data(c, SortKey);
        if (a.typeId() == QMetaType::Int) return a.toInt() < b.toInt();
        if (a.isValid()) return a.toString() < b.toString();
        return text(c).compare(other.text(c), Qt::CaseInsensitive) < 0;
    }
};
} // namespace

SurveyBrowser::SurveyBrowser(QList<surveyindex::Survey> surveys, const QString& databaseName, QWidget* parent)
    : QDialog(parent)
    , m_surveys(std::move(surveys))
{
    setWindowTitle(tr("Synthèses d'ouverture — %1").arg(databaseName));
    resize(900, 640);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Chercher un code, un titre ou un auteur : « C11 », « najdorf », « stohl »…"));
    m_search->setClearButtonEnabled(true);
    m_tree = new QTreeWidget;
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setHeaderLabels({tr("ECO"), tr("Synthèse"), tr("Auteur"), tr("Parution"), tr("Parties")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(ColTitle, QHeaderView::Stretch);
    m_tree->setColumnWidth(ColEco, 90);
    m_tree->setColumnWidth(ColAuthor, 190);
    m_tree->setColumnWidth(ColDate, 80);
    m_tree->setColumnWidth(ColGames, 70);
    m_info = new QLabel;
    m_info->setObjectName("Faint");
    auto* open = new QPushButton(theme::icon("open"), tr("Ouvrir la synthèse"));
    open->setEnabled(false);
    open->setDefault(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
    buttons->addButton(open, QDialogButtonBox::ActionRole);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_info);
    layout->addWidget(buttons);

    int row = 0;
    for (const auto& s : m_surveys) { // already sorted by ECO then date
        auto* it = new Item(m_tree, {s.eco.isEmpty() ? QStringLiteral("—") : s.ecoLabel(), s.title, s.author, s.date, QLocale().toString(s.games)});
        it->setData(ColEco, SortKey, row++);
        it->setData(ColGames, SortKey, s.games);
        it->setData(0, Path, s.file);
        it->setTextAlignment(ColGames, Qt::AlignRight | Qt::AlignVCenter);
        it->setToolTip(ColTitle, QDir::toNativeSeparators(s.file));
    }
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(ColEco, Qt::AscendingOrder);

    auto request = [this](QTreeWidgetItem* it) {
        if (it) emit openRequested(it->data(0, Path).toString());
    };
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_search, &QLineEdit::textChanged, this, &SurveyBrowser::applySearch);
    connect(m_search, &QLineEdit::returnPressed, this, [this, request] { request(m_tree->currentItem()); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [open](QTreeWidgetItem* it) { open->setEnabled(it && !it->isHidden()); });
    connect(m_tree, &QTreeWidget::itemActivated, this, request);
    connect(open, &QPushButton::clicked, this, [this, request] { request(m_tree->currentItem()); });
    applySearch(QString());
}

void SurveyBrowser::setSearch(const QString& text)
{
    m_search->setText(text);
}

int SurveyBrowser::visibleCount() const
{
    int n = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) n += !m_tree->topLevelItem(i)->isHidden();
    return n;
}

void SurveyBrowser::applySearch(const QString& text)
{
    // Every word must be found: at the start of the ECO code, or anywhere in the title or the author.
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QTreeWidgetItem* first = nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = m_tree->topLevelItem(i);
        bool all = true;
        for (const QString& w : words)
            all = all && (it->text(ColEco).startsWith(w, Qt::CaseInsensitive) || it->text(ColTitle).contains(w, Qt::CaseInsensitive)
                          || it->text(ColAuthor).contains(w, Qt::CaseInsensitive) || it->text(ColDate).startsWith(w));
        it->setHidden(!all);
        if (all && !first) first = it;
    }
    if (!m_tree->currentItem() || m_tree->currentItem()->isHidden()) m_tree->setCurrentItem(first);
    const int shown = visibleCount();
    m_info->setText(shown == m_surveys.size() ? tr("%n synthèse(s)", nullptr, shown) : tr("%1 synthèse(s) sur %2").arg(shown).arg(m_surveys.size()));
}
