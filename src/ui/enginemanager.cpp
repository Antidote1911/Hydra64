#include "enginemanager.h"
#include "tabengine.h"
#include "theme.h"
#include "userdirs.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int IdRole = Qt::UserRole;
constexpr int SubRole = Qt::UserRole + 1;
constexpr int OkRole = Qt::UserRole + 2;

// Engine card in the list: status dot, name, subtitle.
class EngineItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {200, 52}; }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = opt.rect.adjusted(6, 3, -6, -3);
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        if (selected || hover) {
            p->setPen(selected ? QPen(theme::Accent, 1) : Qt::NoPen);
            p->setBrush(selected ? theme::Raised : QColor(255, 255, 255, 10));
            p->drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        }
        // Status dot
        p->setPen(Qt::NoPen);
        p->setBrush(idx.data(OkRole).toBool() ? theme::Good : theme::Bad);
        p->drawEllipse(QPointF(r.left() + 14, r.center().y()), 4, 4);

        QFont f = opt.font;
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(theme::Text);
        const QRect text = r.adjusted(28, 7, -30, -7);
        p->drawText(text, Qt::AlignLeft | Qt::AlignTop, p->fontMetrics().elidedText(idx.data().toString(), Qt::ElideRight, text.width()));
        f.setWeight(QFont::Normal);
        f.setPixelSize(11);
        p->setFont(f);
        p->setPen(theme::TextFaint);
        p->drawText(text, Qt::AlignLeft | Qt::AlignBottom, p->fontMetrics().elidedText(idx.data(SubRole).toString(), Qt::ElideMiddle, text.width()));
        p->restore();
    }
};

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

bool looksLikePath(const UciOption& o)
{
    return o.type == QLatin1String("string")
        && (o.name.contains(QLatin1String("Path"), Qt::CaseInsensitive) || o.name.contains(QLatin1String("File"), Qt::CaseInsensitive)
            || o.name.contains(QLatin1String("Dir"), Qt::CaseInsensitive));
}

} // namespace

QWidget* engineui::optionEditor(const UciOption& o, const QString& value, QWidget* dialogParent, std::function<void(const QString&)> changed)
{
    if (o.type == QLatin1String("spin")) {
        auto* sb = new QSpinBox;
        sb->setRange(int(std::max<qint64>(o.min, INT_MIN)), int(std::min<qint64>(o.max, INT_MAX)));
        sb->setValue(value.toInt());
        sb->setKeyboardTracking(false);
        sb->setToolTip(QObject::tr("De %1 à %2").arg(o.min).arg(o.max));
        QObject::connect(sb, &QSpinBox::valueChanged, sb, [changed](int v) { changed(QString::number(v)); });
        return sb;
    }
    if (o.type == QLatin1String("check")) {
        auto* cb = new QCheckBox;
        cb->setChecked(value == QLatin1String("true"));
        QObject::connect(cb, &QCheckBox::toggled, cb, [changed](bool on) { changed(on ? QStringLiteral("true") : QStringLiteral("false")); });
        return cb;
    }
    if (o.type == QLatin1String("combo")) {
        auto* cb = new QComboBox;
        cb->addItems(o.choices);
        cb->setCurrentText(value);
        QObject::connect(cb, &QComboBox::currentTextChanged, cb, [changed](const QString& v) { changed(v); });
        return cb;
    }
    auto* le = new QLineEdit(value);
    le->setPlaceholderText(QObject::tr("vide"));
    // Endgame tablebases, engine by engine: empty = those of the Preferences; a folder = its own; "<empty>" = none
    if (o.name.compare(QLatin1String("SyzygyPath"), Qt::CaseInsensitive) == 0) {
        le->setObjectName(QStringLiteral("SyzygyPathEdit"));
        le->setPlaceholderText(QObject::tr("tables des Préférences"));
        le->setToolTip(QObject::tr("Vide : les tables de finales des Préférences, selon l'usage (analyse, partie, tournoi).\nUn dossier : ce moteur utilise toujours celui-ci.\n« <empty> » : ce moteur ne reçoit jamais de tables."));
    }
    QObject::connect(le, &QLineEdit::editingFinished, le, [changed, le] { changed(le->text()); });
    if (!looksLikePath(o)) return le;
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    auto* b = new QToolButton;
    b->setIcon(theme::icon("open"));
    b->setToolTip(QObject::tr("Parcourir…"));
    const QString name = o.name;
    QObject::connect(b, &QToolButton::clicked, b, [dialogParent, le, name, changed] {
        const QString p = name.contains(QLatin1String("Path"), Qt::CaseInsensitive) || name.contains(QLatin1String("Dir"), Qt::CaseInsensitive)
                              ? QFileDialog::getExistingDirectory(dialogParent, name, le->text())
                              : QFileDialog::getOpenFileName(dialogParent, name, le->text());
        if (!p.isEmpty()) {
            le->setText(p);
            changed(p);
        }
    });
    h->addWidget(le, 1);
    h->addWidget(b);
    return w;
}

EngineManagerDialog::EngineManagerDialog(QWidget* parent, const QString& selectId)
    : QDialog(parent)
{
    setWindowTitle(tr("Gestion des moteurs"));
    resize(1060, 680);

    // ---------------- Left column: list
    auto* left = new QWidget;
    left->setObjectName("ManagerSide");
    left->setAttribute(Qt::WA_StyledBackground);
    left->setFixedWidth(300);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(12, 14, 12, 12);
    ll->setSpacing(8);
    ll->addWidget(section(tr("Moteurs")));
    m_list = new QListWidget;
    m_list->setObjectName("EngineList");
    m_list->setItemDelegate(new EngineItemDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setFrameShape(QFrame::NoFrame);
    ll->addWidget(m_list, 1);
    auto* addBtn = new QPushButton(theme::icon("new"), tr("Ajouter…"));
    m_remove = new QPushButton(theme::icon("trash"), tr("Retirer"));
    auto* lb = new QHBoxLayout;
    lb->addWidget(addBtn, 1);
    lb->addWidget(m_remove, 1);
    ll->addLayout(lb);

    // ---------------- Right part: engine sheet
    auto* detail = new QWidget;
    auto* dl = new QVBoxLayout(detail);
    dl->setContentsMargins(22, 16, 22, 8);
    dl->setSpacing(8);

    m_name = new QLineEdit;
    m_name->setObjectName("TitleEdit");
    m_name->setPlaceholderText(tr("Nom du moteur"));
    m_identity = new QLabel;
    m_identity->setObjectName("Faint");

    auto* pathRow = new QHBoxLayout;
    m_path = new QLineEdit;
    m_path->setReadOnly(true);
    auto* browse = new QPushButton(theme::icon("open"), tr("Changer…"));
    m_browse = browse;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);

    auto* actions = new QHBoxLayout;
    auto* verify = new QPushButton(theme::icon("search"), tr("Vérifier"));
    verify->setToolTip(tr("Relance le moteur et relit ses options"));
    m_status = new QLabel;
    actions->addWidget(verify);
    actions->addSpacing(8);
    actions->addWidget(m_status, 1);

    auto* optHeader = new QHBoxLayout;
    optHeader->addWidget(section(tr("Options UCI")));
    optHeader->addStretch(1);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filtrer les options…"));
    m_filter->addAction(theme::icon("search"), QLineEdit::LeadingPosition);
    m_filter->setClearButtonEnabled(true);
    m_filter->setFixedWidth(220);
    auto* reset = new QPushButton(tr("Tout réinitialiser"));
    optHeader->addWidget(m_filter);
    optHeader->addWidget(reset);

    m_options = new QTableWidget(0, 3);
    m_options->setObjectName("OptionTable");
    m_options->setHorizontalHeaderLabels({tr("Option"), tr("Valeur"), tr("Défaut")});
    m_options->verticalHeader()->hide();
    m_options->verticalHeader()->setDefaultSectionSize(32);
    m_options->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_options->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_options->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_options->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_options->setColumnWidth(0, 210);
    m_options->setColumnWidth(2, 150);
    m_options->setSelectionMode(QAbstractItemView::NoSelection);
    m_options->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_options->setShowGrid(false);
    m_options->setAlternatingRowColors(true);
    m_options->setFocusPolicy(Qt::NoFocus);

    dl->addWidget(m_name);
    dl->addWidget(m_identity);
    m_about = new QLabel;
    m_about->setObjectName("EngineAbout");
    m_about->setWordWrap(true);
    m_about->setTextFormat(Qt::RichText);
    m_about->setStyleSheet(QStringLiteral("QLabel { color:%1; background:%2; border:1px solid %3; border-radius:6px; padding:8px 10px; }").arg(theme::TextDim.name(), theme::Raised.name(), theme::Border.name()));
    dl->addWidget(m_about);
    dl->addSpacing(6);
    dl->addWidget(section(tr("Exécutable")));
    dl->addLayout(pathRow);
    dl->addLayout(actions);
    dl->addSpacing(8);
    dl->addLayout(optHeader);
    dl->addWidget(m_options, 1);
    // What these settings are (the engine's, inherited by the tabs) and what they do not set
    m_strengthNote = new QLabel(tr("Ces réglages sont ceux du moteur : chaque onglet (Adversaire, Spectateur…) en part, et peut en changer pour lui seul avec son bouton CPU. "
                                   "La force de l'adversaire de la page Jouer se règle avec son bouton « Elo », pas avec « UCI_LimitStrength » ni « UCI_Elo »."));
    m_strengthNote->setWordWrap(true);
    m_strengthNote->setStyleSheet(QStringLiteral("color:%1;").arg(theme::TextFaint.name()));
    dl->addWidget(m_strengthNote);

    auto* empty = new QLabel(tr("Aucun moteur.\nCliquez sur « Ajouter… » et choisissez un exécutable UCI\n(Stockfish, Lc0, Berserk, Komodo…)."));
    empty->setObjectName("Faint");
    empty->setAlignment(Qt::AlignCenter);

    m_stack = new QStackedWidget;
    m_stack->addWidget(empty);
    m_stack->addWidget(detail);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));

    auto* right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 16, 14);
    right->addWidget(m_stack, 1);
    right->addWidget(buttons);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(left);
    layout->addLayout(right, 1);

    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        showEngine(item ? item->data(IdRole).toString() : QString());
    });
    connect(addBtn, &QPushButton::clicked, this, &EngineManagerDialog::addEngine);
    connect(m_remove, &QPushButton::clicked, this, &EngineManagerDialog::removeEngine);
    connect(verify, &QPushButton::clicked, this, &EngineManagerDialog::verifyEngine);
    connect(browse, &QPushButton::clicked, this, &EngineManagerDialog::changePath);
    connect(reset, &QPushButton::clicked, this, &EngineManagerDialog::resetOptions);
    connect(m_filter, &QLineEdit::textChanged, this, &EngineManagerDialog::applyFilter);
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const QString n = m_name->text().trimmed();
        if (m_current.isValid() && !n.isEmpty() && n != m_current.name) {
            m_current.name = n;
            commit();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    rebuildList(selectId);
}

void EngineManagerDialog::rebuildList(const QString& selectId)
{
    const EngineRegistry& reg = EngineRegistry::instance();
    QSignalBlocker block(m_list);
    m_list->clear();
    QListWidgetItem* toSelect = nullptr;
    for (const EngineConfig& e : reg.engines()) {
        auto* item = new QListWidgetItem(e.name, m_list);
        item->setData(IdRole, e.id);
        item->setData(SubRole, e.author.isEmpty() ? e.path : e.author);
        item->setData(OkRole, e.verified && QFileInfo::exists(e.path));
        item->setToolTip(e.path);
        if (e.id == selectId) toSelect = item;
    }
    if (!toSelect && m_list->count()) toSelect = m_list->item(0);
    m_list->setCurrentItem(toSelect);
    m_remove->setEnabled(toSelect);
    showEngine(toSelect ? toSelect->data(IdRole).toString() : QString());
}

// Engine shipped with Hydra64: a word on what it is, then what it can do according to its UCI options
// (this second line therefore only says what the engine itself declares).
QString EngineManagerDialog::aboutText(const EngineConfig& e)
{
    const QString kind = EngineRegistry::bundledKind(e);
    QString what;
    if (kind == QLatin1String("stockfish"))
        what = tr("<b>Le moteur de référence</b>, le plus fort : recherche alpha-bêta et réseau de neurones (NNUE). Hydra64 s'en sert pour l'évaluation "
                  "automatique et comme moteur par défaut partout : il ne peut pas être retiré. Sa force se règle : "
                  "l'adversaire à choisir pour jouer à votre niveau.");
    else if (kind == QLatin1String("shuffle"))
        what = tr("<b>Le moteur maison</b>, écrit en Rust : il joue presque au hasard, attiré par les prises, les échecs et le mat en un. "
                  "Pour débuter, se sentir très fort, ou essayer un tournoi sans attendre. Option « Style » : Silly (par défaut) ou Random "
                  "(tous les coups à chances égales) ; « Seed » rend les parties reproductibles ; « Move Delay » règle sa fausse réflexion. "
                  "Intégré à Hydra64 : il ne peut pas être retiré.");
    else
        return {};
    auto has = [&](const char* option) { return e.option(QLatin1String(option)) != nullptr; };
    auto item = [&](const QString& label, bool yes) { return QStringLiteral("%1 : %2").arg(label, yes ? tr("oui") : tr("non")); };
    QStringList can{item(tr("Force réglable (Elo)"), has("UCI_Elo")), item(tr("Tables de finales"), has("SyzygyPath")), item(tr("Chess960"), has("UCI_Chess960")),
                    item(tr("Plusieurs variantes"), has("MultiPV")), item(tr("Réflexion permanente"), has("Ponder"))};
    if (e.options.isEmpty()) can.clear(); // unknown options (engine not checked)
    return can.isEmpty() ? what : what + QStringLiteral("<br><span style='color:%1;'>%2</span>").arg(theme::TextFaint.name(), can.join(QStringLiteral("  ·  ")));
}

void EngineManagerDialog::showEngine(const QString& id)
{
    m_current = EngineRegistry::instance().engine(id);
    // The built-in engines (Stockfish, Shuffle) are always there: no removal, no change of executable.
    const bool builtin = EngineRegistry::isBuiltin(m_current);
    m_remove->setEnabled(m_current.isValid() && !builtin);
    m_remove->setToolTip(builtin ? tr("Un moteur intégré ne peut pas être retiré") : QString());
    m_browse->setEnabled(!builtin);
    m_browse->setToolTip(builtin ? tr("Exécutable intégré à Hydra64") : QString());
    if (!m_current.isValid()) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);
    m_name->setText(m_current.name);
    QStringList ident;
    if (!m_current.idName.isEmpty()) ident << m_current.idName;
    if (!m_current.author.isEmpty()) ident << tr("par %1").arg(m_current.author);
    ident << tr("%n option(s) UCI", nullptr, int(m_current.options.size()));
    m_identity->setText(ident.join(QStringLiteral("  ·  ")));
    m_about->setText(aboutText(m_current));
    m_about->setVisible(!m_about->text().isEmpty());
    m_path->setText(m_current.path);
    if (!QFileInfo::exists(m_current.path)) setStatus(false, tr("Exécutable introuvable"));
    else if (m_current.verified) setStatus(true, tr("Moteur opérationnel"));
    else setStatus(false, tr("Non vérifié"));
    rebuildOptions();
}

void EngineManagerDialog::setStatus(bool ok, const QString& text)
{
    m_status->setText(QStringLiteral("<span style='color:%1;'>●</span>&nbsp; %2").arg((ok ? theme::Good : theme::Bad).name(), text.toHtmlEscaped()));
}

void EngineManagerDialog::rebuildOptions()
{
    m_options->setRowCount(0);
    for (const UciOption& o : m_current.options) {
        if (o.type == QLatin1String("button")) continue; // one-off actions, with no value to remember
        const int row = m_options->rowCount();
        m_options->insertRow(row);
        const bool modified = m_current.values.contains(o.name);
        const bool byGame = false;
        const QString gameValue;

        auto* nameItem = new QTableWidgetItem(o.name);
        QFont f = m_options->font();
        f.setWeight(modified ? QFont::DemiBold : QFont::Normal);
        nameItem->setFont(f);
        nameItem->setForeground(byGame ? theme::TextFaint : modified ? theme::Accent : theme::Text);
        nameItem->setToolTip(byGame ? (gameValue.isNull() ? tr("Sans effet pendant la partie en cours : la force est réglée par l'Elo de la partie") : tr("Réglé par la partie en cours : la valeur affichée est celle de la partie"))
                                    : modified ? tr("Valeur modifiée") : QString());
        m_options->setItem(row, 0, nameItem);

        QString def = o.defaultValue;
        if (o.type == QLatin1String("check")) def = def == QLatin1String("true") ? tr("oui") : tr("non");
        if (def.isEmpty()) def = QStringLiteral("—");
        if (byGame) def = gameValue.isNull() ? tr("sans effet (partie)") : tr("réglé par la partie");
        auto* defItem = new QTableWidgetItem(def);
        defItem->setForeground(theme::TextFaint);
        m_options->setItem(row, 2, defItem);

        const QString name = o.name;
        QWidget* editor = byGame ? engineui::optionEditor(o, gameValue.isNull() ? m_current.value(o.name) : gameValue, this, [](const QString&) {})
                                 : engineui::optionEditor(o, m_current.value(o.name), this, [this, name](const QString& v) { setValue(name, v); });
        editor->setEnabled(!byGame);
        editor->setObjectName(QStringLiteral("Option_") + o.name);
        if (byGame) editor->setToolTip(nameItem->toolTip());
        // Margin around the editor in the cell
        auto* cell = new QWidget;
        auto* cl = new QHBoxLayout(cell);
        cl->setContentsMargins(4, 2, 8, 2);
        cl->addWidget(editor, editor->inherits("QCheckBox") ? 0 : 1);
        if (editor->inherits("QCheckBox")) cl->addStretch(1);
        m_options->setCellWidget(row, 1, cell);
    }
    applyFilter();
}

void EngineManagerDialog::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (int r = 0; r < m_options->rowCount(); ++r)
        m_options->setRowHidden(r, !f.isEmpty() && !m_options->item(r, 0)->text().contains(f, Qt::CaseInsensitive));
}

void EngineManagerDialog::setValue(const QString& option, const QString& value)
{
    const UciOption* o = m_current.option(option);
    if (!o) return;
    const bool wasModified = m_current.values.contains(option);
    if (value == o->defaultValue) m_current.values.remove(option);
    else m_current.values[option] = value;
    commit();
    // Update of the "modified" style without rebuilding the editors
    if (wasModified != m_current.values.contains(option)) {
        for (int r = 0; r < m_options->rowCount(); ++r) {
            QTableWidgetItem* it = m_options->item(r, 0);
            if (it->text() != option) continue;
            QFont f = it->font();
            f.setWeight(m_current.values.contains(option) ? QFont::DemiBold : QFont::Normal);
            it->setFont(f);
            it->setForeground(m_current.values.contains(option) ? theme::Accent : theme::Text);
        }
    }
}

void EngineManagerDialog::commit()
{
    EngineRegistry::instance().update(m_current);
    if (QListWidgetItem* item = m_list->currentItem()) item->setText(m_current.name);
}

void EngineManagerDialog::addEngine()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Ajouter un moteur UCI"), userdirs::uciEngines()); // the user's engines folder
    if (path.isEmpty()) return;
    for (const EngineConfig& e : EngineRegistry::instance().engines())
        if (QFileInfo(e.path) == QFileInfo(path)) {
            QMessageBox::information(this, tr("Ajouter un moteur"), tr("Ce moteur est déjà enregistré sous le nom « %1 ».").arg(e.name));
            rebuildList(e.id);
            return;
        }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    EngineConfig cfg;
    QString error;
    const bool ok = EngineRegistry::create(path, cfg, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Ajouter un moteur"), error);
        return;
    }
    EngineRegistry::instance().add(cfg);
    rebuildList(cfg.id);
}

void EngineManagerDialog::removeEngine()
{
    if (!m_current.isValid() || EngineRegistry::isBuiltin(m_current)) return;
    if (QMessageBox::question(this, tr("Retirer le moteur"),
                              tr("Retirer « %1 » de la liste ?\nL'exécutable n'est pas supprimé.").arg(m_current.name))
        != QMessageBox::Yes)
        return;
    EngineRegistry::instance().remove(m_current.id);
    rebuildList();
}

void EngineManagerDialog::verifyEngine()
{
    if (!m_current.isValid()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = EngineRegistry::probe(m_current.path, m_current, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) m_current.verified = false;
    commit();
    showEngine(m_current.id);
    if (ok) setStatus(true, tr("Moteur opérationnel : %n option(s) lue(s)", nullptr, int(m_current.options.size())));
    else setStatus(false, error);
    rebuildList(m_current.id);
}

void EngineManagerDialog::changePath()
{
    if (!m_current.isValid() || EngineRegistry::isBuiltin(m_current)) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Exécutable du moteur"), QFileInfo(m_current.path).absolutePath());
    if (path.isEmpty()) return;
    m_current.path = path;
    verifyEngine();
}

void EngineManagerDialog::resetOptions()
{
    if (!m_current.isValid() || m_current.values.isEmpty()) return;
    m_current.values.clear();
    commit();
    rebuildOptions();
}

// ---------------------------------------------------------------- Settings of a tab

EngineTabDialog::EngineTabDialog(const QString& tab, const QString& tabTitle, const QString& engineId, QWidget* parent)
    : QDialog(parent)
    , m_tab(tab)
    , m_engine(EngineRegistry::instance().engine(engineId))
{
    setWindowTitle(tr("%1 — réglages de %2").arg(tabTitle, m_engine.name));
    resize(720, 620);
    auto* title = new QLabel(tr("%1 dans l'onglet « %2 »").arg(m_engine.name, tabTitle));
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    f.setWeight(QFont::DemiBold);
    title->setFont(f);
    auto* lead = new QLabel(tr("Ces réglages ne valent que pour cet onglet. Les autres onglets qui utilisent le même moteur gardent les leurs ; "
                               "une option non modifiée ici suit celle du moteur (Gestion des moteurs)."));
    lead->setWordWrap(true);
    lead->setStyleSheet(QStringLiteral("color:%1;").arg(theme::TextDim.name()));

    m_options = new QTableWidget(0, 3);
    m_options->setObjectName("OptionTable");
    m_options->setHorizontalHeaderLabels({tr("Option"), tr("Valeur dans cet onglet"), tr("Valeur du moteur")});
    m_options->verticalHeader()->hide();
    m_options->verticalHeader()->setDefaultSectionSize(32);
    m_options->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_options->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_options->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_options->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_options->setColumnWidth(0, 210);
    m_options->setColumnWidth(2, 170);
    m_options->setSelectionMode(QAbstractItemView::NoSelection);
    m_options->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_options->setShowGrid(false);
    m_options->setAlternatingRowColors(true);
    m_options->setFocusPolicy(Qt::NoFocus);

    m_reset = new QPushButton(tr("Reprendre les réglages du moteur"));
    m_reset->setObjectName("TabEngineReset");
    m_reset->setToolTip(tr("Efface les réglages propres à cet onglet"));
    auto* manage = new QPushButton(theme::icon("engine"), tr("Gestion des moteurs…"));
    manage->setToolTip(tr("Réglages du moteur lui-même, hérités par tous les onglets"));
    auto* close = new QPushButton(tr("Fermer"));
    close->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_reset);
    buttons->addWidget(manage);
    buttons->addStretch(1);
    buttons->addWidget(close);

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(20, 18, 20, 16);
    l->addWidget(title);
    l->addWidget(lead);
    l->addSpacing(6);
    l->addWidget(m_options, 1);
    l->addLayout(buttons);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(manage, &QPushButton::clicked, this, [this] {
        m_manage = true;
        accept();
    });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        tabengine::clear(m_tab, m_engine.id);
        rebuild();
    });
    rebuild();
}

void EngineTabDialog::setGameOptions(const QMap<QString, QString>& values)
{
    m_gameOptions = values;
    rebuild();
}

void EngineTabDialog::rebuild()
{
    const QMap<QString, QString> own = tabengine::overrides(m_tab, m_engine.id);
    const EngineConfig mine = tabengine::effective(m_tab, m_engine);
    m_reset->setEnabled(!own.isEmpty());
    m_options->setRowCount(0);
    for (const UciOption& o : m_engine.options) {
        if (o.type == QLatin1String("button")) continue;
        const int row = m_options->rowCount();
        m_options->insertRow(row);
        const bool changed = own.contains(o.name);
        // Set by the game in progress: the game's value, not editable here
        bool byGame = false;
        QString gameValue;
        for (auto it = m_gameOptions.cbegin(); it != m_gameOptions.cend(); ++it)
            if (it.key().compare(o.name, Qt::CaseInsensitive) == 0) byGame = true, gameValue = it.value();

        auto* nameItem = new QTableWidgetItem(o.name);
        QFont f = m_options->font();
        f.setWeight(changed && !byGame ? QFont::DemiBold : QFont::Normal);
        nameItem->setFont(f);
        nameItem->setForeground(byGame ? theme::TextFaint : changed ? theme::Accent : theme::Text);
        nameItem->setToolTip(byGame ? (gameValue.isNull() ? tr("Sans effet pendant la partie en cours : la force est réglée par l'Elo de la partie") : tr("Réglé par la partie en cours : la valeur affichée est celle de la partie"))
                                    : changed ? tr("Réglage propre à cet onglet") : QString());
        m_options->setItem(row, 0, nameItem);

        QString ref = m_engine.value(o.name);
        if (o.type == QLatin1String("check")) ref = ref == QLatin1String("true") ? tr("oui") : tr("non");
        if (ref.isEmpty()) ref = QStringLiteral("—");
        if (byGame) ref = gameValue.isNull() ? tr("sans effet (partie)") : tr("réglé par la partie");
        auto* refItem = new QTableWidgetItem(ref);
        refItem->setForeground(theme::TextFaint);
        m_options->setItem(row, 2, refItem);

        const QString name = o.name;
        QWidget* editor = byGame ? engineui::optionEditor(o, gameValue.isNull() ? mine.value(name) : gameValue, this, [](const QString&) {})
                                 : engineui::optionEditor(o, mine.value(name), this, [this, name](const QString& v) {
                                       tabengine::setValue(m_tab, m_engine, name, v);
                                       // "Modified" style without rebuilding the editors (the current editor keeps the focus)
                                       const bool own = tabengine::overrides(m_tab, m_engine.id).contains(name);
                                       m_reset->setEnabled(!tabengine::overrides(m_tab, m_engine.id).isEmpty());
                                       for (int r = 0; r < m_options->rowCount(); ++r) {
                                           QTableWidgetItem* it = m_options->item(r, 0);
                                           if (it->text() != name) continue;
                                           QFont f = it->font();
                                           f.setWeight(own ? QFont::DemiBold : QFont::Normal);
                                           it->setFont(f);
                                           it->setForeground(own ? theme::Accent : theme::Text);
                                           it->setToolTip(own ? tr("Réglage propre à cet onglet") : QString());
                                       }
                                   });
        editor->setEnabled(!byGame);
        editor->setObjectName(QStringLiteral("Option_") + o.name);
        if (byGame) editor->setToolTip(nameItem->toolTip());
        auto* cell = new QWidget;
        auto* cl = new QHBoxLayout(cell);
        cl->setContentsMargins(4, 2, 8, 2);
        cl->addWidget(editor, editor->inherits("QCheckBox") ? 0 : 1);
        if (editor->inherits("QCheckBox")) cl->addStretch(1);
        m_options->setCellWidget(row, 1, cell);
    }
}
