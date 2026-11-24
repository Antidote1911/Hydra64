#include "convertdialog.h"
#include "userdirs.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

ConvertDialog::ConvertDialog(const QString& source, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Convertir une base ou un arbre d'ouverture"));
    setMinimumWidth(620);

    auto browse = [](const char* tip) {
        auto* b = new QPushButton(theme::icon("open"), QString());
        b->setToolTip(ConvertDialog::tr(tip));
        return b;
    };
    m_source = new QLineEdit;
    m_source->setPlaceholderText(tr("Base de parties ou arbre d'ouverture à convertir"));
    auto* sourceBrowse = browse(QT_TR_NOOP("Choisir le fichier à convertir…"));
    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(m_source, 1);
    sourceRow->addWidget(sourceBrowse);

    m_detected = new QLabel;
    m_detected->setObjectName("Faint");
    m_detected->setWordWrap(true);

    m_target = new QComboBox;

    m_destination = new QLineEdit;
    auto* destBrowse = browse(QT_TR_NOOP("Choisir le fichier à créer…"));
    auto* destRow = new QHBoxLayout;
    destRow->addWidget(m_destination, 1);
    destRow->addWidget(destBrowse);

    m_depth = new QSpinBox;
    m_depth->setRange(0, 80);
    m_depth->setValue(QSettings().value("convert/positionDepth", 24).toInt());
    m_depth->setSuffix(tr(" demi-coups"));
    m_depth->setSpecialValueText(tr("sans index des positions"));
    m_depth->setToolTip(tr("Recherche de position et arbre de la base sur ces premiers demi-coups (24 : les 12 premiers coups)"));
    m_depthLabel = new QLabel(tr("Index des positions"));

    m_treePlies = new QSpinBox;
    m_treePlies->setRange(2, 200);
    m_treePlies->setValue(QSettings().value("convert/treePlies", 50).toInt());
    m_treePlies->setSuffix(tr(" demi-coups"));
    m_treePlies->setToolTip(tr("Chaque partie entre dans l'arbre sur ses premiers demi-coups, au plus (50 : les 25 premiers coups)"));
    m_treePliesLabel = new QLabel(tr("Profondeur de l'arbre"));
    m_treeMinGames = new QSpinBox;
    m_treeMinGames->setRange(1, 1000);
    m_treeMinGames->setValue(QSettings().value("convert/treeMinGames", 2).toInt());
    m_treeMinGames->setSpecialValueText(tr("toutes les parties jusqu'au bout"));
    m_treeMinGames->setSuffix(tr(" parties"));
    m_treeMinGames->setToolTip(tr("Chaque partie est suivie jusqu'à sa dernière position connue d'au moins ce nombre de parties, plus un coup : "
                                  "la suite, qu'elle est seule à jouer, est laissée de côté. 2 parties et 50 demi-coups donnent les arbres livrés par ChessOK."));
    m_treeMinGamesLabel = new QLabel(tr("Suivre les parties tant que la position est connue de"));

    m_open = new QCheckBox(tr("Ouvrir le résultat après la conversion"));
    m_open->setChecked(true);

    auto* note = new QLabel(tr("Les formats propriétaires (ChessBase, ChessOK / Aquarium) ne s'ouvrent pas directement : "
                               "on les convertit ici au format Hydra64 ou dans un format libre (PGN, Polyglot)."));
    note->setWordWrap(true);
    note->setObjectName("Faint");

    auto* form = new QFormLayout;
    form->addRow(tr("Source"), sourceRow);
    form->addRow(QString(), m_detected);
    form->addRow(tr("Convertir en"), m_target);
    form->addRow(tr("Destination"), destRow);
    form->addRow(m_depthLabel, m_depth);
    form->addRow(m_treePliesLabel, m_treePlies);
    form->addRow(m_treeMinGamesLabel, m_treeMinGames);
    form->addRow(QString(), m_open);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_convert = buttons->button(QDialogButtonBox::Ok);
    m_convert->setText(tr("Convertir"));
    m_convert->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addSpacing(6);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(sourceBrowse, &QPushButton::clicked, this, &ConvertDialog::chooseSource);
    connect(destBrowse, &QPushButton::clicked, this, &ConvertDialog::chooseDestination);
    connect(m_source, &QLineEdit::textChanged, this, [this] {
        m_destinationEdited = false;
        update();
    });
    connect(m_target, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_destinationEdited) suggestDestination();
        update();
    });
    connect(m_destination, &QLineEdit::textEdited, this, [this] { m_destinationEdited = true; });
    connect(m_destination, &QLineEdit::textChanged, this, [this] { update(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings().setValue("convert/positionDepth", m_depth->value());
        QSettings().setValue("convert/treePlies", m_treePlies->value());
        QSettings().setValue("convert/treeMinGames", m_treeMinGames->value());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setSource(source);
    update();
}

void ConvertDialog::setSource(const QString& path)
{
    m_source->setText(QDir::toNativeSeparators(path.isEmpty() ? path : conversion::databaseFile(path))); // .cbg… → the database's .cbh
}

conversion::Request ConvertDialog::request() const
{
    conversion::Request r;
    r.source = QDir::fromNativeSeparators(m_source->text().trimmed());
    r.destination = QDir::fromNativeSeparators(m_destination->text().trimmed());
    r.target = conversion::Target(m_target->currentData().toInt());
    r.positionDepth = m_depth->value();
    r.treePlies = m_treePlies->value();
    r.treeMinGames = m_treeMinGames->value();
    // the extension of the chosen format, if missing
    const QString ext = conversion::targetExtension(r.target);
    if (!r.destination.isEmpty() && !r.destination.endsWith(ext, Qt::CaseInsensitive)) r.destination += ext;
    return r;
}

bool ConvertDialog::openResult() const { return m_open->isChecked(); }

void ConvertDialog::setTarget(conversion::Target target)
{
    if (const int i = m_target->findData(int(target)); i >= 0) m_target->setCurrentIndex(i);
}

void ConvertDialog::chooseSource()
{
    QSettings s;
    const QString path = QFileDialog::getOpenFileName(this, tr("Fichier à convertir"), userdirs::startDir(userdirs::Kind::Database), conversion::sourceFilter());
    if (path.isEmpty()) return;
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    userdirs::remember(userdirs::Kind::Database, path);
    setSource(path);
}

void ConvertDialog::chooseDestination()
{
    const conversion::Target t = conversion::Target(m_target->currentData().toInt());
    const QString ext = conversion::targetExtension(t);
    const QString path = QFileDialog::getSaveFileName(this, tr("Fichier à créer"), m_destination->text(),
                                                      conversion::targetName(t) + QStringLiteral(" (*") + ext + QLatin1Char(')'));
    if (path.isEmpty()) return;
    m_destinationEdited = true;
    m_destination->setText(QDir::toNativeSeparators(path.endsWith(ext, Qt::CaseInsensitive) ? path : path + ext));
}

void ConvertDialog::suggestDestination()
{
    const QString src = QDir::fromNativeSeparators(m_source->text().trimmed());
    if (src.isEmpty() || m_target->count() == 0) return;
    const conversion::Target t = conversion::Target(m_target->currentData().toInt());
    const bool tree = conversion::isTree(t);
    const QString dir = tree ? userdirs::openingBooks() : userdirs::databases();
    QString dest = dir + QLatin1Char('/') + QFileInfo(src).completeBaseName() + conversion::targetExtension(t);
    if (QFileInfo(dest) == QFileInfo(src)) // same format as the source: another name
        dest = dir + QLatin1Char('/') + QFileInfo(src).completeBaseName() + tr(" (converti)") + conversion::targetExtension(t);
    m_destination->setText(QDir::toNativeSeparators(dest));
}

void ConvertDialog::update()
{
    const QString src = QDir::fromNativeSeparators(m_source->text().trimmed());
    const conversion::Kind kind = src.isEmpty() ? conversion::Kind::Unknown : conversion::kindOf(src);
    const bool exists = !src.isEmpty() && QFileInfo::exists(src);
    // possible target formats depending on the nature of the source (database or tree)
    const QList<conversion::Target> targets = conversion::targetsFor(kind);
    QList<int> current;
    for (int i = 0; i < m_target->count(); ++i) current << m_target->itemData(i).toInt();
    QList<int> wanted;
    for (conversion::Target t : targets) wanted << int(t);
    if (current != wanted) {
        QSignalBlocker block(m_target);
        m_target->clear();
        for (conversion::Target t : targets) m_target->addItem(conversion::targetName(t), int(t));
        if (!m_destinationEdited) suggestDestination();
    }
    if (src.isEmpty()) m_detected->setText(tr("Formats lus : PGN, Hydra64, ChessBase (.cbh, .2cbh, .cbv, .2cbv, .ctg), ChessOK / Aquarium (.hsh, export texte), Polyglot (.bin)."));
    else if (!exists) m_detected->setText(tr("Fichier introuvable."));
    else if (kind == conversion::Kind::Unknown) m_detected->setText(tr("Format non reconnu."));
    else m_detected->setText(conversion::formatName(src) + (conversion::isArchive(src) ? tr(" — sera extraite avant la conversion") : QString()));
    const conversion::Target t = conversion::Target(m_target->currentData().toInt());
    const bool depth = kind == conversion::Kind::Database && (t == conversion::Target::Hydra64Database || t == conversion::Target::Hydra64Single);
    m_depth->setVisible(depth);
    m_depthLabel->setVisible(depth);
    const bool gameTree = kind == conversion::Kind::Database && conversion::isTree(t);
    for (QWidget* w : {static_cast<QWidget*>(m_treePlies), static_cast<QWidget*>(m_treePliesLabel), static_cast<QWidget*>(m_treeMinGames), static_cast<QWidget*>(m_treeMinGamesLabel)})
        w->setVisible(gameTree);
    m_convert->setEnabled(exists && kind != conversion::Kind::Unknown && !m_destination->text().trimmed().isEmpty());
}
