#include "namefixdialog.h"

#include "db/playerbase.h"
#include "playerencyclopedia.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace namefix {

namespace {
// Folded words of a name, sorted; a federation in parentheses at the end is dropped.
std::vector<std::string> words(const std::string& name)
{
    std::string text = name;
    if (const size_t open = text.rfind('('); open != std::string::npos && text.find(')', open) != std::string::npos) text.erase(open);
    const std::string folded = hy64db::PlayerBase::foldName(text);
    std::vector<std::string> out;
    std::string word;
    for (unsigned char c : folded) {
        if (c == ' ' || c == ',' || c == '.' || c == '-' || c == '\'') {
            if (!word.empty()) out.push_back(std::move(word));
            word.clear();
        } else {
            word += char(c);
        }
    }
    if (!word.empty()) out.push_back(std::move(word));
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

bool sameWords(const std::string& a, const std::string& b)
{
    const auto wa = words(a);
    return !wa.empty() && wa == words(b);
}

std::vector<Proposal> proposals(const hy64db::Database& db, const std::function<std::string(const std::string&)>& canonical,
                                const std::function<bool(uint64_t, uint64_t)>& progress)
{
    std::vector<Proposal> out;
    const std::vector<uint32_t> usage = db.nameUsage(hy64db::Player);
    for (uint32_t id = 0; id < usage.size(); ++id) {
        if (progress && id % 512 == 0 && !progress(id, usage.size())) return {};
        if (usage[id] == 0) continue;
        const std::string from = db.names().name(hy64db::Player, id);
        const std::string to = canonical(from);
        if (to.empty() || to == from) continue;
        out.push_back({id, from, to, usage[id], sameWords(from, to)});
    }
    std::stable_sort(out.begin(), out.end(), [](const Proposal& a, const Proposal& b) { return a.games > b.games; });
    return out;
}

std::string encyclopediaName(const std::string& name)
{
    const hy64db::PlayerBase* base = playerEncyclopedia::get();
    if (!base) return {};
    if (words(name).size() < 2) return {}; // a family name alone says too little
    const auto found = base->find(name);
    return found.size() == 1 ? found.front().name() : std::string();
}

} // namespace namefix

NameFixDialog::NameFixDialog(std::vector<namefix::Proposal> proposals, QWidget* parent)
    : QDialog(parent)
    , m_proposals(std::move(proposals))
{
    setWindowTitle(tr("Améliorer les noms des joueurs"));
    resize(760, 560);
    auto* note = new QLabel(tr("L'encyclopédie des joueurs reconnaît ces noms sans hésitation et propose son écriture. Les lignes cochées d'office ne changent que "
                               "l'ordre des mots, la casse, les accents ou la ponctuation ; les autres complètent un prénom abrégé — vérifiez-les avant de les cocher. "
                               "Un nom déjà présent sous sa bonne écriture est fusionné avec elle."));
    note->setWordWrap(true);
    note->setObjectName("Faint");
    m_table = new QTableWidget(int(m_proposals.size()), 3);
    m_table->setHorizontalHeaderLabels({tr("Nom actuel"), tr("Écriture proposée"), tr("Parties")});
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    for (int row = 0; row < int(m_proposals.size()); ++row) {
        const namefix::Proposal& p = m_proposals[size_t(row)];
        auto* from = new QTableWidgetItem(QString::fromStdString(p.from));
        from->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        from->setCheckState(p.sure ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, 0, from);
        m_table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(p.to)));
        auto* games = new QTableWidgetItem(QLocale().toString(p.games));
        games->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 2, games);
    }
    m_count = new QLabel;
    m_count->setObjectName("Dim");
    auto* all = new QPushButton(tr("Tout cocher"));
    auto* none = new QPushButton(tr("Tout décocher"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Appliquer"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(all);
    bottom->addWidget(none);
    bottom->addWidget(m_count, 1);
    bottom->addWidget(buttons);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addWidget(m_table, 1);
    layout->addLayout(bottom);
    connect(all, &QPushButton::clicked, this, [this] { setAllChecked(true); });
    connect(none, &QPushButton::clicked, this, [this] { setAllChecked(false); });
    connect(m_table, &QTableWidget::itemChanged, this, [this] { updateCount(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateCount();
}

int NameFixDialog::rowCount() const { return m_table->rowCount(); }

void NameFixDialog::setAllChecked(bool on)
{
    const QSignalBlocker quiet(m_table);
    for (int row = 0; row < m_table->rowCount(); ++row) m_table->item(row, 0)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    updateCount();
}

std::vector<std::pair<uint32_t, std::string>> NameFixDialog::chosen() const
{
    std::vector<std::pair<uint32_t, std::string>> out;
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (m_table->item(row, 0)->checkState() == Qt::Checked) out.push_back({m_proposals[size_t(row)].id, m_proposals[size_t(row)].to});
    return out;
}

void NameFixDialog::updateCount() { m_count->setText(tr("%1 noms cochés sur %2").arg(chosen().size()).arg(m_table->rowCount())); }
