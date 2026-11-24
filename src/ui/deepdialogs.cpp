#include "deepdialogs.h"

#include "chess/pgn.h"
#include "engine/engineregistry.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace chess;

QString deepScoreText(int cp, Color toMove)
{
    if (cp == deep::Unknown) return QStringLiteral("–");
    const int v = toMove == White ? cp : -cp;
    if (std::abs(v) > deep::MateValue / 2) {
        const int moves = (deep::MateValue - std::abs(v) + 1) / 2;
        return v > 0 ? QStringLiteral("#%1").arg(moves) : QStringLiteral("#-%1").arg(moves);
    }
    return QString::asprintf("%+.2f", v / 100.0);
}

namespace {
QDialogButtonBox* okCancel(QDialog* d, const QString& ok)
{
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    b->button(QDialogButtonBox::Ok)->setText(ok);
    b->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    b->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("DeepDialogs", "Annuler"));
    QObject::connect(b, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return b;
}
QDoubleSpinBox* seconds(double value, double max = 86400)
{
    auto* s = new QDoubleSpinBox;
    s->setRange(0, max);
    s->setDecimals(1);
    s->setSuffix(QCoreApplication::translate("DeepDialogs", " s"));
    s->setValue(value);
    return s;
}
QSpinBox* spin(int lo, int hi, int value, const QString& suffix = {})
{
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(value);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}
} // namespace

// ---------------------------------------------------------------- project options

DeepProjectDialog::DeepProjectDialog(DeepAnalysis* deep, int index, const Position& root, QWidget* parent)
    : QDialog(parent)
{
    const bool editing = index >= 0;
    setWindowTitle(editing ? tr("Options du projet") : tr("Nouveau projet d'analyse approfondie"));
    const deep::Options o = editing ? deep->project(index).options : deep::Options{};
    const DeepAnalysis::ProjectInfo in = editing ? deep->info(index) : DeepAnalysis::ProjectInfo{};

    // General
    m_name = new QLineEdit(editing ? in.name : tr("Analyse"));
    m_status = new QComboBox;
    m_status->addItem(tr("Inactif"), int(DeepAnalysis::Status::Inactive));
    m_status->addItem(tr("Actif"), int(DeepAnalysis::Status::Active));
    m_status->addItem(tr("Part du CPU"), int(DeepAnalysis::Status::Share));
    m_status->setCurrentIndex(m_status->findData(int(editing ? in.status : DeepAnalysis::Status::Active)));
    m_share = spin(1, 100, in.share, tr(" %"));
    m_share->setToolTip(tr("Part du temps des moteurs donnée à ce projet quand plusieurs projets sont actifs"));
    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_share);
    m_master = new QComboBox;
    m_master->addItem(tr("aucun"), QString());
    for (int i = 0; i < deep->count(); ++i)
        if (i != index) m_master->addItem(deep->info(i).name, deep->info(i).path);
    if (const int m = m_master->findData(in.master); m >= 0) m_master->setCurrentIndex(m);
    m_master->setToolTip(tr("Arbre maître : ses résultats sont repris à la création ; « Fusionner dans l'arbre maître » y ajoute ceux du projet"));
    m_minTime = seconds(o.quality.minTimeMs / 1000.0);
    m_minDepth = spin(0, 200, o.quality.minDepth, tr(" demi-coups"));
    m_maxTime = seconds(o.quality.maxTimeMs / 1000.0);
    m_maxTime->setSpecialValueText(tr("sans limite"));
    m_waitDepth = new QCheckBox(tr("Attendre la fin de la profondeur commencée"));
    m_waitDepth->setChecked(o.quality.waitNextDepth);
    m_spectators = new QCheckBox(tr("Envoyer à l'arbre les lignes des moteurs spectateurs"));
    m_spectators->setChecked(o.insertSpectators);
    m_specTime = seconds(o.spectatorMinTimeMs / 1000.0);
    m_specDepth = spin(0, 200, o.spectatorMinDepth, tr(" demi-coups"));
    m_specDepth->setToolTip(tr("0 : seulement le premier coup de la ligne"));

    auto* general = new QWidget;
    auto* gf = new QFormLayout(general);
    gf->addRow(tr("Nom"), m_name);
    gf->addRow(tr("État"), statusRow);
    gf->addRow(tr("Arbre maître"), m_master);
    if (!editing) {
        auto* rootLabel = new QLabel(QString::fromStdString(root.fen()));
        rootLabel->setObjectName("Faint");
        rootLabel->setWordWrap(true);
        gf->addRow(tr("Racine"), rootLabel);
    }
    gf->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Qualité de l'analyse d'une position"))));
    gf->addRow(tr("Au moins"), m_minTime);
    gf->addRow(tr("et une profondeur d'au moins"), m_minDepth);
    gf->addRow(tr("Au plus"), m_maxTime);
    gf->addRow(QString(), m_waitDepth);
    gf->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Moteurs spectateurs"))));
    gf->addRow(QString(), m_spectators);
    gf->addRow(tr("Après au moins"), m_specTime);
    gf->addRow(tr("et une profondeur d'au moins"), m_specDepth);

    // IDeA: shape of the tree
    m_autoExpand = new QCheckBox(tr("Expansion automatique de l'arbre (sinon : seulement les positions données, mode EPD)"));
    m_autoExpand->setChecked(o.autoExpand);
    m_limitWhite = spin(1, 64, o.limitWhite);
    m_limitBlack = spin(1, 64, o.limitBlack);
    m_width = spin(1, 2000, o.widthCp, tr(" cp"));
    m_width->setToolTip(tr("Écart au meilleur coup jusqu'auquel un coup est encore analysé : plus grand = arbre plus large"));
    m_falseAlarm = spin(0, 40, o.falseAlarmPlies, tr(" demi-coups"));
    m_falseAlarm->setSpecialValueText(tr("non"));
    m_falseAlarm->setToolTip(tr("Une alternative qui paraît meilleure que le meilleur coup est aussitôt prolongée de ce nombre de demi-coups"));
    m_preferred = new QComboBox;
    m_preferred->addItem(tr("Aucun"), int(deep::Side::None));
    m_preferred->addItem(tr("Blancs"), int(deep::Side::White));
    m_preferred->addItem(tr("Noirs"), int(deep::Side::Black));
    m_preferred->addItem(tr("Les deux"), int(deep::Side::Both));
    m_preferred->setCurrentIndex(m_preferred->findData(int(o.preferred)));
    m_preferred->setToolTip(tr("Préparation d'un répertoire : pour ce camp, seul son meilleur coup est approfondi"));
    QStringList thematic;
    for (const std::string& t : o.thematic) thematic << QString::fromStdString(t);
    m_thematic = new QLineEdit(thematic.join(QStringLiteral(", ")));
    m_thematic->setPlaceholderText(tr("Bxh7, Ng5…"));
    m_maxPlies = spin(0, 500, o.maxPlies, tr(" demi-coups"));
    m_maxPlies->setSpecialValueText(tr("sans limite"));
    m_bounds = new QCheckBox(tr("Bornes de score (point de vue des Blancs)"));
    m_bounds->setChecked(o.useBounds);
    m_low = spin(-30000, 30000, o.lowCp, tr(" cp"));
    m_high = spin(-30000, 30000, o.highCp, tr(" cp"));
    auto* boundsRow = new QHBoxLayout;
    boundsRow->addWidget(m_low);
    boundsRow->addWidget(new QLabel(tr("à")));
    boundsRow->addWidget(m_high);

    auto* idea = new QWidget;
    auto* ff = new QFormLayout(idea);
    ff->addRow(QString(), m_autoExpand);
    ff->addRow(tr("Alternatives des Blancs au plus"), m_limitWhite);
    ff->addRow(tr("Alternatives des Noirs au plus"), m_limitBlack);
    ff->addRow(tr("Largeur de l'arbre"), m_width);
    ff->addRow(tr("Vérification des fausses alertes"), m_falseAlarm);
    ff->addRow(tr("Camp préféré"), m_preferred);
    ff->addRow(tr("Coups thématiques"), m_thematic);
    ff->addRow(tr("Longueur des variantes"), m_maxPlies);
    ff->addRow(QString(), m_bounds);
    ff->addRow(tr("Bornes"), boundsRow);

    auto* tabs = new QTabWidget;
    tabs->addTab(general, tr("Général"));
    tabs->addTab(idea, tr("IDeA"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(okCancel(this, editing ? tr("Enregistrer") : tr("Créer")));
    auto sync = [this] {
        m_share->setEnabled(m_status->currentData().toInt() == int(DeepAnalysis::Status::Share));
        m_low->setEnabled(m_bounds->isChecked());
        m_high->setEnabled(m_bounds->isChecked());
        m_specTime->setEnabled(m_spectators->isChecked());
        m_specDepth->setEnabled(m_spectators->isChecked());
    };
    connect(m_status, &QComboBox::currentIndexChanged, this, sync);
    connect(m_bounds, &QCheckBox::toggled, this, sync);
    connect(m_spectators, &QCheckBox::toggled, this, sync);
    sync();
}

QString DeepProjectDialog::name() const { return m_name->text().trimmed(); }
DeepAnalysis::Status DeepProjectDialog::status() const { return DeepAnalysis::Status(m_status->currentData().toInt()); }
int DeepProjectDialog::share() const { return m_share->value(); }
QString DeepProjectDialog::master() const { return m_master->currentData().toString(); }

deep::Options DeepProjectDialog::options() const
{
    deep::Options o;
    o.quality.minTimeMs = int(m_minTime->value() * 1000);
    o.quality.minDepth = m_minDepth->value();
    o.quality.maxTimeMs = int(m_maxTime->value() * 1000);
    o.quality.waitNextDepth = m_waitDepth->isChecked();
    o.insertSpectators = m_spectators->isChecked();
    o.spectatorMinTimeMs = int(m_specTime->value() * 1000);
    o.spectatorMinDepth = m_specDepth->value();
    o.autoExpand = m_autoExpand->isChecked();
    o.limitWhite = m_limitWhite->value();
    o.limitBlack = m_limitBlack->value();
    o.widthCp = m_width->value();
    o.falseAlarmPlies = m_falseAlarm->value();
    o.preferred = deep::Side(m_preferred->currentData().toInt());
    for (const QString& t : m_thematic->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (!t.trimmed().isEmpty()) o.thematic.push_back(t.trimmed().toStdString());
    o.maxPlies = m_maxPlies->value();
    o.useBounds = m_bounds->isChecked();
    o.lowCp = std::min(m_low->value(), m_high->value());
    o.highCp = std::max(m_low->value(), m_high->value());
    return o;
}

// ---------------------------------------------------------------- roots

DeepRootsDialog::DeepRootsDialog(DeepAnalysis* deep, int project, const Position& board, QWidget* parent)
    : QDialog(parent)
    , m_deep(deep)
    , m_project(project)
    , m_board(board)
{
    setWindowTitle(tr("Racines du projet"));
    resize(760, 360);
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({tr("Active"), tr("Éval"), tr("Position"), tr("Commentaire")});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 60);
    m_table->setColumnWidth(1, 70);
    m_table->setColumnWidth(2, 380);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    auto* addBoard = new QPushButton(theme::icon("board"), tr("Position de l'échiquier"));
    auto* paste = new QPushButton(theme::icon("paste"), tr("Coller (FEN ou PGN)"));
    auto* remove = new QPushButton(theme::icon("trash"), tr("Retirer"));
    auto* row = new QHBoxLayout;
    row->addWidget(addBoard);
    row->addWidget(paste);
    row->addWidget(remove);
    row->addStretch(1);
    auto* layout = new QVBoxLayout(this);
    auto* note = new QLabel(tr("Chaque racine active est un point de départ de l'analyse ; plusieurs racines actives se partagent le temps du projet."));
    note->setObjectName("Faint");
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addWidget(m_table, 1);
    layout->addLayout(row);
    layout->addWidget(okCancel(this, tr("Enregistrer")));
    auto addPosition = [this](const Position& p) {
        apply();
        m_deep->project(m_project).tree.addRoot(p);
        m_deep->markDirty(m_project);
        fill();
    };
    connect(addBoard, &QPushButton::clicked, this, [this, addPosition] { addPosition(m_board); });
    connect(paste, &QPushButton::clicked, this, [addPosition] {
        const std::string text = QApplication::clipboard()->text().trimmed().toStdString();
        Position p;
        if (p.setFen(text)) {
            addPosition(p);
            return;
        }
        Game g; // game fragment: its final position
        if (pgn::parseGame(text, g)) {
            g.toEnd();
            addPosition(g.current()->pos);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int r = m_table->currentRow();
        auto& roots = m_deep->project(m_project).tree.roots();
        if (r < 0 || r >= int(roots.size()) || roots.size() <= 1) return;
        apply();
        roots.erase(roots.begin() + r);
        // Tasks of this root removed, numbers of the following ones shifted
        auto& q = m_deep->project(m_project).queue();
        std::erase_if(q, [r](const deep::Task& t) { return t.root == r; });
        for (deep::Task& t : q)
            if (t.root > r) --t.root;
        m_deep->markDirty(m_project);
        fill();
    });
    connect(this, &QDialog::accepted, this, &DeepRootsDialog::apply);
    fill();
}

void DeepRootsDialog::fill()
{
    const deep::Tree& tree = m_deep->project(m_project).tree;
    m_table->setRowCount(int(tree.roots().size()));
    for (int i = 0; i < int(tree.roots().size()); ++i) {
        const deep::Root& r = tree.roots()[size_t(i)];
        auto* en = new QTableWidgetItem;
        en->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, 0, en);
        Position p;
        p.setFen(r.fen);
        auto* ev = new QTableWidgetItem(r.node == deep::NoNode ? QString() : deepScoreText(tree.node(r.node).score, p.sideToMove()));
        ev->setFlags(ev->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, ev);
        auto* fen = new QTableWidgetItem(QString::fromStdString(r.fen));
        fen->setFlags(fen->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 2, fen);
        m_table->setItem(i, 3, new QTableWidgetItem(QString::fromStdString(r.comment)));
    }
}

void DeepRootsDialog::apply()
{
    auto& roots = m_deep->project(m_project).tree.roots();
    for (int i = 0; i < m_table->rowCount() && i < int(roots.size()); ++i) {
        roots[size_t(i)].enabled = m_table->item(i, 0)->checkState() == Qt::Checked;
        roots[size_t(i)].comment = m_table->item(i, 3)->text().toStdString();
    }
    m_deep->markDirty(m_project);
}

// ---------------------------------------------------------------- engines

DeepEnginesDialog::DeepEnginesDialog(const QList<DeepAnalysis::EngineSlot>& engineSlots, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Moteurs de l'analyse approfondie"));
    resize(640, 300);
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({tr("Actif"), tr("Moteur"), tr("Instances"), tr("Fils"), tr("Mémoire")});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (const auto& s : engineSlots) addRow(s);
    auto* add = new QPushButton(theme::icon("new"), tr("Ajouter un moteur"));
    auto* remove = new QPushButton(theme::icon("trash"), tr("Retirer"));
    auto* row = new QHBoxLayout;
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    auto* note = new QLabel(tr("Chaque instance analyse une tâche à la fois. Le plus efficace : beaucoup d'instances à un fil, "
                               "par exemple autant que de cœurs moins un. Pris en compte au prochain démarrage."));
    note->setObjectName("Faint");
    note->setWordWrap(true);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addWidget(m_table, 1);
    layout->addLayout(row);
    layout->addWidget(okCancel(this, tr("Enregistrer")));
    connect(add, &QPushButton::clicked, this, [this] {
        DeepAnalysis::EngineSlot s;
        s.engineId = EngineRegistry::instance().fallbackEngine().id;
        addRow(s);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (m_table->rowCount() > 1 && m_table->currentRow() >= 0) m_table->removeRow(m_table->currentRow());
    });
}

void DeepEnginesDialog::addRow(const DeepAnalysis::EngineSlot& s)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    auto* en = new QTableWidgetItem;
    en->setCheckState(s.enabled ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(r, 0, en);
    auto* engine = new QComboBox;
    for (const EngineConfig& e : EngineRegistry::instance().engines()) engine->addItem(e.name, e.id);
    if (const int i = engine->findData(s.engineId); i >= 0) engine->setCurrentIndex(i);
    m_table->setCellWidget(r, 1, engine);
    m_table->setCellWidget(r, 2, spin(1, 256, s.count));
    m_table->setCellWidget(r, 3, spin(1, 512, s.threads));
    m_table->setCellWidget(r, 4, spin(1, 1 << 20, s.hashMb, tr(" Mo")));
}

QList<DeepAnalysis::EngineSlot> DeepEnginesDialog::engineSlots() const
{
    QList<DeepAnalysis::EngineSlot> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        DeepAnalysis::EngineSlot s;
        s.enabled = m_table->item(r, 0)->checkState() == Qt::Checked;
        s.engineId = static_cast<QComboBox*>(m_table->cellWidget(r, 1))->currentData().toString();
        s.count = static_cast<QSpinBox*>(m_table->cellWidget(r, 2))->value();
        s.threads = static_cast<QSpinBox*>(m_table->cellWidget(r, 3))->value();
        s.hashMb = static_cast<QSpinBox*>(m_table->cellWidget(r, 4))->value();
        out << s;
    }
    return out;
}

// ---------------------------------------------------------------- custom task

DeepCustomTaskDialog::DeepCustomTaskDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tâche personnalisée"));
    m_time = seconds(30);
    m_depth = spin(0, 200, 0, tr(" demi-coups"));
    m_depth->setSpecialValueText(tr("indifférente"));
    m_alternative = new QCheckBox(tr("Chercher une alternative (coup pas encore dans l'arbre)"));
    auto* form = new QFormLayout;
    form->addRow(tr("Analyser au moins"), m_time);
    form->addRow(tr("et jusqu'à la profondeur"), m_depth);
    form->addRow(QString(), m_alternative);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(okCancel(this, tr("Envoyer")));
}

int DeepCustomTaskDialog::timeMs() const { return int(m_time->value() * 1000); }
int DeepCustomTaskDialog::depth() const { return m_depth->value(); }
bool DeepCustomTaskDialog::alternative() const { return m_alternative->isChecked(); }
