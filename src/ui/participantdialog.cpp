#include "participantdialog.h"
#include "enginemanager.h"
#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

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

QString shown(const UciOption& o, QString v)
{
    if (o.type == QLatin1String("check")) return v == QLatin1String("true") ? QObject::tr("oui") : QObject::tr("non");
    return v.isEmpty() ? QStringLiteral("—") : v;
}

} // namespace

ParticipantDialog::ParticipantDialog(const Tournament::Participant& initial, QWidget* parent)
    : QDialog(parent)
    , m_participant(initial)
{
    setWindowTitle(tr("Moteur du tournoi"));
    resize(760, 620);

    m_engineBox = new QComboBox;
    for (const EngineConfig& e : EngineRegistry::instance().engines()) m_engineBox->addItem(e.name, e.id);

    m_name = new QLineEdit;
    m_name->setObjectName("TitleEdit");
    m_name->setPlaceholderText(tr("Nom dans le tournoi"));
    m_name->setToolTip(tr("Nom affiché dans le classement, les parties et le PGN"));
    m_identity = new QLabel;
    m_identity->setObjectName("Faint");

    auto* note = new QLabel(tr("Ces réglages ne valent que pour ce tournoi : la gestion des moteurs n'est pas modifiée."));
    note->setWordWrap(true);
    note->setObjectName("Faint");

    auto* engineRow = new QHBoxLayout;
    engineRow->addWidget(new QLabel(tr("Moteur")));
    engineRow->addWidget(m_engineBox, 1);

    auto* optHeader = new QHBoxLayout;
    optHeader->addWidget(section(tr("Options UCI")));
    optHeader->addStretch(1);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filtrer les options…"));
    m_filter->addAction(theme::icon("search"), QLineEdit::LeadingPosition);
    m_filter->setClearButtonEnabled(true);
    m_filter->setFixedWidth(200);
    auto* general = new QPushButton(tr("Réglages généraux"));
    general->setToolTip(tr("Reprendre les réglages du moteur dans la gestion des moteurs"));
    optHeader->addWidget(m_filter);
    optHeader->addWidget(general);

    m_options = new QTableWidget(0, 3);
    m_options->setObjectName("OptionTable");
    m_options->setHorizontalHeaderLabels({tr("Option"), tr("Valeur pour le tournoi"), tr("Réglage général")});
    m_options->verticalHeader()->hide();
    m_options->verticalHeader()->setDefaultSectionSize(32);
    m_options->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_options->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_options->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_options->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_options->setColumnWidth(0, 190);
    m_options->setColumnWidth(2, 140);
    m_options->setSelectionMode(QAbstractItemView::NoSelection);
    m_options->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_options->setShowGrid(false);
    m_options->setAlternatingRowColors(true);
    m_options->setFocusPolicy(Qt::NoFocus);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(initial.engineId.isEmpty() ? tr("Ajouter") : tr("Enregistrer"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 16, 22, 14);
    layout->setSpacing(8);
    layout->addLayout(engineRow);
    layout->addSpacing(4);
    layout->addWidget(m_name);
    layout->addWidget(m_identity);
    layout->addWidget(note);
    layout->addSpacing(6);
    layout->addLayout(optHeader);
    layout->addWidget(m_options, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textEdited, this, [this] { m_nameEdited = true; });
    connect(m_engineBox, &QComboBox::currentIndexChanged, this, [this] {
        // Another engine: we start from its general settings.
        m_engine = EngineRegistry::instance().engine(m_engineBox->currentData().toString());
        m_participant.engineId = m_engine.id;
        m_participant.values = m_engine.values;
        if (!m_nameEdited) m_name->setText(m_engine.name);
        rebuildOptions();
    });
    connect(general, &QPushButton::clicked, this, [this] {
        m_participant.values = m_engine.values;
        rebuildOptions();
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& t) {
        for (int r = 0; r < m_options->rowCount(); ++r)
            m_options->setRowHidden(r, !t.trimmed().isEmpty() && !m_options->item(r, 0)->text().contains(t.trimmed(), Qt::CaseInsensitive));
    });

    // Initial state: the given participant, or the first engine.
    const int index = m_engineBox->findData(initial.engineId);
    if (index >= 0) {
        QSignalBlocker block(m_engineBox);
        m_engineBox->setCurrentIndex(index);
        m_engine = EngineRegistry::instance().engine(initial.engineId);
        m_name->setText(initial.name.isEmpty() ? m_engine.name : initial.name);
        m_nameEdited = !initial.name.isEmpty() && initial.name != m_engine.name;
        rebuildOptions();
    } else if (m_engineBox->count() > 0) {
        m_engineBox->setCurrentIndex(-1);
        m_engineBox->setCurrentIndex(0); // → general settings of the first engine
    }
    buttons->button(QDialogButtonBox::Ok)->setEnabled(m_engineBox->count() > 0);
}

Tournament::Participant ParticipantDialog::participant() const
{
    Tournament::Participant p = m_participant;
    p.name = m_name->text().trimmed().isEmpty() ? m_engine.name : m_name->text().trimmed();
    return p;
}

void ParticipantDialog::selectEngine(const QString& engineId)
{
    const int i = m_engineBox->findData(engineId);
    if (i >= 0) m_engineBox->setCurrentIndex(i);
}

void ParticipantDialog::setOptionValue(const QString& option, const QString& value)
{
    const UciOption* o = m_engine.option(option);
    if (!o) return;
    if (value == o->defaultValue) m_participant.values.remove(option);
    else m_participant.values[option] = value;
    rebuildOptions();
}

QString ParticipantDialog::generalValue(const QString& option) const { return m_engine.value(option); }

void ParticipantDialog::rebuildOptions()
{
    QStringList ident;
    if (!m_engine.idName.isEmpty()) ident << m_engine.idName;
    if (!m_engine.author.isEmpty()) ident << tr("par %1").arg(m_engine.author);
    m_identity->setText(ident.join(QStringLiteral(" · ")));

    m_options->setRowCount(0);
    for (const UciOption& o : m_engine.options) {
        if (o.type == QLatin1String("button")) continue;
        // MultiPV makes no sense in a game: the tournament does not send it.
        if (o.name.compare(QLatin1String("MultiPV"), Qt::CaseInsensitive) == 0) continue;
        const int row = m_options->rowCount();
        m_options->insertRow(row);
        m_options->setItem(row, 0, new QTableWidgetItem(o.name));
        auto* gen = new QTableWidgetItem(shown(o, generalValue(o.name)));
        gen->setForeground(theme::TextFaint);
        m_options->setItem(row, 2, gen);

        const QString name = o.name;
        const QString def = o.defaultValue;
        const QString current = m_participant.values.value(o.name, o.defaultValue);
        QWidget* editor = engineui::optionEditor(o, current, this, [this, name, def, row](const QString& v) {
            if (v == def) m_participant.values.remove(name);
            else m_participant.values[name] = v;
            restyleRow(row);
        });
        auto* cell = new QWidget;
        auto* cl = new QHBoxLayout(cell);
        cl->setContentsMargins(4, 2, 8, 2);
        cl->addWidget(editor, editor->inherits("QCheckBox") ? 0 : 1);
        if (editor->inherits("QCheckBox")) cl->addStretch(1);
        m_options->setCellWidget(row, 1, cell);
        restyleRow(row);
    }
    emit m_filter->textChanged(m_filter->text());
}

void ParticipantDialog::restyleRow(int row)
{
    // In colour: the options set differently from the engine manager.
    QTableWidgetItem* it = m_options->item(row, 0);
    if (!it) return;
    const UciOption* o = m_engine.option(it->text());
    const bool differs = o && m_participant.values.value(o->name, o->defaultValue) != generalValue(o->name);
    QFont f = m_options->font();
    f.setWeight(differs ? QFont::DemiBold : QFont::Normal);
    it->setFont(f);
    it->setForeground(differs ? theme::Accent : theme::Text);
    it->setToolTip(differs ? tr("Réglé autrement que dans la gestion des moteurs") : QString());
}
