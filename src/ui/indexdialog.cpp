#include "indexdialog.h"

#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

IndexModel::IndexModel(hy64db::NameType type, QObject* parent)
    : QAbstractTableModel(parent)
    , m_type(type)
{
}

void IndexModel::setEntries(std::vector<hy64db::IndexEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    rebuild();
    endResetModel();
}

void IndexModel::setFilter(const QString& text)
{
    if (text == m_filter) return;
    beginResetModel();
    m_filter = text;
    rebuild();
    endResetModel();
}

void IndexModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    m_sortColumn = column;
    m_sortOrder = order;
    rebuild();
    endResetModel();
}

void IndexModel::rebuild()
{
    m_rows.clear();
    const QString needle = m_filter.trimmed();
    for (uint32_t i = 0; i < m_entries.size(); ++i)
        if (needle.isEmpty() || QString::fromStdString(m_entries[i].name).contains(needle, Qt::CaseInsensitive)) m_rows.push_back(i);
    auto key = [&](uint32_t i) -> double {
        const hy64db::IndexEntry& e = m_entries[i];
        switch (m_sortColumn) {
        case Games: return e.games;
        case Years: return e.firstYear ? e.firstYear : 1e9; // without a date: at the end
        case BestElo: return e.bestElo;
        case Score: return e.score();
        default: return i; // already alphabetical
        }
    };
    const bool up = m_sortOrder == Qt::AscendingOrder;
    std::stable_sort(m_rows.begin(), m_rows.end(), [&](uint32_t a, uint32_t b) { return up ? key(a) < key(b) : key(a) > key(b); });
}

QVariant IndexModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size())) return {};
    const hy64db::IndexEntry& e = entry(index.row());
    if (role == Qt::TextAlignmentRole) return index.column() == Name ? QVariant(int(Qt::AlignLeft | Qt::AlignVCenter)) : QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
    case Name: return QString::fromStdString(e.name);
    case Games: return QLocale().toString(e.games);
    case Years: return !e.firstYear ? QString() : e.firstYear == e.lastYear ? QString::number(e.firstYear) : QStringLiteral("%1–%2").arg(e.firstYear).arg(e.lastYear);
    case BestElo: return e.bestElo ? QString::number(e.bestElo) : QString();
    case Score: return e.wins + e.draws + e.losses ? QStringLiteral("%1 %").arg(QLocale().toString(e.score(), 'f', 1)) : QString();
    }
    return {};
}

QVariant IndexModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case Name: return tr("Nom");
    case Games: return tr("Parties");
    case Years: return tr("Années");
    case BestElo: return tr("Meilleur Elo");
    case Score: return tr("Score");
    }
    return {};
}

IndexDialog::IndexDialog(const hy64db::Database& db, const QString& baseName, QWidget* parent)
    : QDialog(parent)
    , m_db(db)
{
    setWindowTitle(tr("Index de « %1 »").arg(baseName));
    resize(760, 620);
    auto* layout = new QVBoxLayout(this);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filtrer les noms…"));
    m_filter->setClearButtonEnabled(true);
    layout->addWidget(m_filter);
    m_tabs = new QTabWidget;
    layout->addWidget(m_tabs, 1);
    m_count = new QLabel;
    layout->addWidget(m_count);
    const std::pair<hy64db::NameType, QString> tabs[] = {{hy64db::Player, tr("Joueurs")}, {hy64db::Event, tr("Tournois")}, {hy64db::Site, tr("Lieux")}, {hy64db::Annotator, tr("Annotateurs")}, {hy64db::Source, tr("Sources")}};
    for (const auto& [type, title] : tabs) {
        auto* model = new IndexModel(type, this);
        auto* view = new QTableView;
        view->setModel(model);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setSortingEnabled(true);
        view->horizontalHeader()->setSortIndicator(IndexModel::Name, Qt::AscendingOrder);
        view->horizontalHeader()->setSectionResizeMode(IndexModel::Name, QHeaderView::Stretch);
        view->verticalHeader()->hide();
        view->verticalHeader()->setDefaultSectionSize(view->fontMetrics().height() + 8);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        const int nameType = int(type);
        connect(view, &QTableView::doubleClicked, this, [this, model, nameType](const QModelIndex& i) {
            const auto& e = model->entry(i.row());
            emit gamesRequested(nameType, e.id, QString::fromStdString(e.name));
        });
        connect(view, &QWidget::customContextMenuRequested, this, [this, view, model, nameType](const QPoint& pos) {
            const QModelIndex i = view->indexAt(pos);
            if (!i.isValid()) return;
            const hy64db::IndexEntry e = model->entry(i.row());
            QMenu menu(this);
            connect(menu.addAction(tr("Afficher ses parties")), &QAction::triggered, this, [this, nameType, e] { emit gamesRequested(nameType, e.id, QString::fromStdString(e.name)); });
            if (nameType == int(hy64db::Player))
                connect(menu.addAction(tr("Fiche du joueur…")), &QAction::triggered, this, [this, e] { emit playerCardRequested(QString::fromStdString(e.name)); });
            menu.exec(view->viewport()->mapToGlobal(pos));
        });
        m_tabs->addTab(view, title);
        m_types.push_back(type);
        m_models.push_back(model);
        m_views.push_back(view);
        m_loaded.push_back(false);
    }
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { tabChanged(); });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_models[size_t(m_tabs->currentIndex())]->setFilter(text);
        updateCount();
    });
    tabChanged();
}

IndexModel* IndexDialog::model(hy64db::NameType type)
{
    for (size_t i = 0; i < m_types.size(); ++i) {
        if (m_types[i] != type) continue;
        if (!m_loaded[i]) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            m_models[i]->setEntries(hy64db::nameIndex(m_db, type));
            QApplication::restoreOverrideCursor();
            m_loaded[i] = true;
        }
        return m_models[i];
    }
    return nullptr;
}

void IndexDialog::showType(hy64db::NameType type)
{
    for (size_t i = 0; i < m_types.size(); ++i)
        if (m_types[i] == type) m_tabs->setCurrentIndex(int(i));
}

void IndexDialog::tabChanged()
{
    const size_t i = size_t(m_tabs->currentIndex());
    model(m_types[i])->setFilter(m_filter->text());
    updateCount();
}

void IndexDialog::updateCount()
{
    const IndexModel* m = m_models[size_t(m_tabs->currentIndex())];
    m_count->setText(m->rowCount() == m->total() ? tr("%1 entrées — double-clic : les parties dans la liste").arg(QLocale().toString(m->total()))
                                                 : tr("%1 entrées sur %2").arg(QLocale().toString(m->rowCount()), QLocale().toString(m->total())));
}
