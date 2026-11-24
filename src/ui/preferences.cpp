#include "preferences.h"
#include "tablebases.h"
#include "display.h"
#include "defaultsets.h"
#include "boardtheme.h"
#include "piecerenderer.h"
#include "theme.h"
#include "playerencyclopedia.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSettings>
#include <QSpinBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QHash>
#include <QThread>
#include <QVBoxLayout>

#include <functional>

namespace prefs {

namespace {
// Values read are kept in memory: creating a QSettings on each read costs about 7 µs, and the
// notation is requested for every displayed move (move list, engine lines).
QHash<QByteArray, QVariant>& cache()
{
    static QHash<QByteArray, QVariant> values;
    return values;
}
QVariant value(const char* key, const QVariant& def)
{
    auto& values = cache();
    const QByteArray k(key);
    if (const auto it = values.constFind(k); it != values.constEnd()) return it->isValid() ? *it : def;
    const QVariant v = QSettings().value(QStringLiteral("prefs/") + QLatin1String(key));
    values.insert(k, v); // invalid = absent: the caller's default value applies
    return v.isValid() ? v : def;
}
void store(const char* key, const QVariant& v)
{
    QSettings().setValue(QStringLiteral("prefs/") + QLatin1String(key), v);
    cache().insert(QByteArray(key), v);
}
} // namespace

void invalidateCache() { cache().clear(); }

Notifier& Notifier::instance()
{
    static Notifier n;
    return n;
}

QString systemLanguage() { return QLocale::system().language() == QLocale::French ? QStringLiteral("fr") : QStringLiteral("en"); }

QString language()
{
    const QString l = value("language", systemLanguage()).toString();
    return l == QLatin1String("en") ? l : QStringLiteral("fr");
}

void setLanguage(const QString& lang) { store("language", lang); }

Notation notation()
{
    // By default: the notation of the chosen language.
    const QString def = language() == QLatin1String("fr") ? QStringLiteral("fr") : QStringLiteral("en");
    const QString n = value("notation", def).toString();
    if (n == QLatin1String("en")) return Notation::English;
    if (n == QLatin1String("figurine")) return Notation::Figurine;
    return Notation::French;
}

void setNotation(Notation n)
{
    store("notation", n == Notation::English ? "en" : n == Notation::Figurine ? "figurine" : "fr");
    emit Notifier::instance().changed();
}

bool wheelNavigation() { return value("wheelNavigation", true).toBool(); }
void setWheelNavigation(bool on) { store("wheelNavigation", on); }
bool wheelInverted() { return value("wheelInverted", false).toBool(); }
bool restoreSession() { return value("restoreSession", true).toBool(); }
void setRestoreSession(bool on) { store("restoreSession", on); }
void setWheelInverted(bool on) { store("wheelInverted", on); }

bool showEvalBar() { return value("showEvalBar", true).toBool(); }
void setShowEvalBar(bool on)
{
    if (on == showEvalBar()) return;
    store("showEvalBar", on);
    emit Notifier::instance().changed();
}

bool showEvalBarOnBases() { return value("showEvalBarOnBases", false).toBool(); }
void setShowEvalBarOnBases(bool on)
{
    if (on == showEvalBarOnBases()) return;
    store("showEvalBarOnBases", on);
    emit Notifier::instance().changed();
}

bool assistedAnalysis() { return value("assistedAnalysis", false).toBool(); }
void setAssistedAnalysis(bool on)
{
    if (on == assistedAnalysis()) return;
    store("assistedAnalysis", on);
    emit Notifier::instance().changed();
}

bool threatArrow() { return value("threatArrow", false).toBool(); }
void setThreatArrow(bool on)
{
    if (on == threatArrow()) return;
    store("threatArrow", on);
    emit Notifier::instance().changed();
}

QString boardTheme() { return value("boardTheme", QString::fromLatin1(boardtheme::DefaultTheme)).toString(); }
void setBoardTheme(const QString& id)
{
    if (id == boardTheme()) return;
    store("boardTheme", id);
    boardtheme::setCurrent(id);
    emit Notifier::instance().changed();
}

namespace {
QColor storedColor(const char* key, const char* def)
{
    const QColor c(value(key, QString::fromLatin1(def)).toString());
    return c.isValid() ? c : QColor(QLatin1String(def));
}
} // namespace
QColor boardLightColor() { return storedColor("boardLightColor", "#f0d9b5"); }
QColor boardDarkColor() { return storedColor("boardDarkColor", "#b58863"); }
void setBoardColors(const QColor& light, const QColor& dark)
{
    if (!light.isValid() || !dark.isValid() || (light == boardLightColor() && dark == boardDarkColor())) return;
    store("boardLightColor", light.name());
    store("boardDarkColor", dark.name());
    emit Notifier::instance().changed();
}

QString pieceSet() { return value("pieceSet", QString::fromLatin1(PieceRenderer::DefaultSet)).toString(); }
void setPieceSet(const QString& id)
{
    if (id == pieceSet()) return;
    store("pieceSet", id);
    PieceRenderer::setCurrentSet(id);
    emit Notifier::instance().changed();
}

bool pieceShadow() { return value("pieceShadow", true).toBool(); }
void setPieceShadow(bool on)
{
    if (on == pieceShadow()) return;
    store("pieceShadow", on);
    PieceRenderer::setShadow(on);
    emit Notifier::instance().changed();
}

bool hideMenuBar() { return value("hideMenuBar", false).toBool(); }
void setHideMenuBar(bool on)
{
    if (on == hideMenuBar()) return;
    store("hideMenuBar", on);
    emit Notifier::instance().changed();
}

int evalThreads() { return std::clamp(value("evalThreads", 2).toInt(), 1, 1024); }
int evalHashMb() { return std::clamp(value("evalHashMb", 64).toInt(), 1, 65536); }
int evalDepth() { return std::clamp(value("evalDepth", 16).toInt(), 1, 99); }
int evalTimeMs() { return std::clamp(value("evalTimeMs", 300).toInt(), 50, 600000); }

void setEvaluation(int threads, int hashMb, int depth, int timeMs)
{
    if (threads == evalThreads() && hashMb == evalHashMb() && depth == evalDepth() && timeMs == evalTimeMs()) return;
    store("evalThreads", threads);
    store("evalHashMb", hashMb);
    store("evalDepth", depth);
    store("evalTimeMs", timeMs);
    emit Notifier::instance().changed();
}

int notationFontSize() { return std::clamp(value("notationFontSize", 13).toInt(), MinNotationFontSize, MaxNotationFontSize); }
bool showEngineComments() { return value("showEngineComments", true).toBool(); }
void setShowEngineComments(bool on)
{
    if (on == showEngineComments()) return;
    store("showEngineComments", on);
    emit Notifier::instance().changed();
}

void setNotationFontSize(int px)
{
    px = std::clamp(px, MinNotationFontSize, MaxNotationFontSize);
    if (px == notationFontSize()) return;
    store("notationFontSize", px);
    emit Notifier::instance().changed();
}

} // namespace prefs

// ================================================================= PreferencesDialog

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

} // namespace

int PreferencesDialog::s_lastPage = 0;

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Préférences"));
    setMinimumWidth(700);

    auto* title = new QLabel(tr("Préférences"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);

    // Language (names displayed in their own language)
    m_language = new QComboBox;
    m_language->addItem(QStringLiteral("Français"), QStringLiteral("fr"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->setCurrentIndex(std::max(0, m_language->findData(prefs::language())));
    m_restartNote = new QLabel(tr("Le changement de langue prend effet au redémarrage de Hydra64."));
    m_restartNote->setObjectName("Faint");
    m_restartNote->setVisible(false);

    // Move notation: drop-down list + preview
    m_notation = new QComboBox;
    m_notation->addItem(tr("Française"), int(prefs::Notation::French));
    m_notation->addItem(tr("Anglaise"), int(prefs::Notation::English));
    m_notation->addItem(tr("Figurines"), int(prefs::Notation::Figurine));
    m_notation->setCurrentIndex(std::max(0, m_notation->findData(int(prefs::notation()))));
    m_preview = new QLabel;
    m_preview->setObjectName("NotationPreview");
    QFont pf = m_preview->font();
    pf.setPixelSize(14);
    m_preview->setFont(pf);

    // Wheel navigation
    m_wheel = new QCheckBox(tr("Avancer et reculer dans la partie avec la molette sur l'échiquier"));
    m_wheel->setChecked(prefs::wheelNavigation());
    m_wheelInvert = new QCheckBox(tr("Inverser le sens (molette vers le haut = coup suivant)"));
    m_wheelInvert->setChecked(prefs::wheelInverted());
    m_wheelInvert->setEnabled(m_wheel->isChecked());

    m_evalBar = new QCheckBox(tr("Afficher la barre d'évaluation à côté de l'échiquier"));
    m_evalBar->setChecked(prefs::showEvalBar());
    m_hideMenu = new QCheckBox(tr("Masquer la barre de menu"));
    m_hideMenu->setToolTip(tr("Les menus restent accessibles d'un clic sur la tête d'hydre, en bas à gauche (et Ctrl+Maj+M)"));
    m_hideMenu->setChecked(prefs::hideMenuBar());

    // Folders of the personal sets: created here so that the user knows where to drop their own
    PieceRenderer::userDirectory(true);
    boardtheme::userDirectory(true);
    // Piece set: list (icon: white king and black knight) + preview of the twelve pieces
    m_pieceSet = new QComboBox;
    m_pieceSet->setIconSize(QSize(44, 22));
    m_piecePreview = new QLabel;
    m_pieceCredit = new QLabel;
    m_pieceCredit->setObjectName("Faint");
    m_pieceCredit->setWordWrap(true);

    // Board theme: list (icon: a light square and a dark square)
    m_boardTheme = new QComboBox;
    m_boardTheme->setIconSize(QSize(44, 22));
    m_boardLight = prefs::boardLightColor();
    m_boardDark = prefs::boardDarkColor();
    fillSetLists(PieceRenderer::currentSet(), boardtheme::current());
    // Colours of the squares, for the "plain colours" board
    m_boardColorsLabel = new QLabel(tr("Couleurs des cases"));
    m_boardColors = new QWidget;
    m_boardLightButton = new QPushButton(tr("Cases claires…"));
    m_boardDarkButton = new QPushButton(tr("Cases foncées…"));
    auto* colorRow = new QHBoxLayout(m_boardColors);
    colorRow->setContentsMargins(0, 0, 0, 0);
    colorRow->addWidget(m_boardLightButton);
    colorRow->addWidget(m_boardDarkButton);
    colorRow->addStretch(1);
    connect(m_boardLightButton, &QPushButton::clicked, this, [this] { pickBoardColor(true); });
    connect(m_boardDarkButton, &QPushButton::clicked, this, [this] { pickBoardColor(false); });

    auto* pieceRow = new QGridLayout;
    pieceRow->setHorizontalSpacing(12);
    pieceRow->addWidget(new QLabel(tr("Jeu de pièces")), 0, 0);
    pieceRow->addWidget(m_pieceSet, 0, 1);
    pieceRow->addWidget(new QLabel(tr("Échiquier")), 1, 0);
    pieceRow->addWidget(m_boardTheme, 1, 1);
    pieceRow->addWidget(m_boardColorsLabel, 2, 0);
    pieceRow->addWidget(m_boardColors, 2, 1);
    pieceRow->setColumnStretch(1, 1);
    // Personal sets: read from ~/Documents/hydra64/themes, each time this dialog opens
    auto* userFolders = new QPushButton(theme::icon("open"), QString());
    userFolders->setObjectName("UserSetsFolder");
    userFolders->setToolTip(tr("Ouvrir le dossier des jeux de pièces et des échiquiers personnels"));
    pieceRow->addWidget(userFolders, 0, 2, 2, 1, Qt::AlignVCenter);
    connect(userFolders, &QPushButton::clicked, this, [] { PieceRenderer::userDirectory(true);
        QDesktopServices::openUrl(QUrl::fromLocalFile(defaultsets::themesDirectory())); });
    m_userSetsHint = new QLabel(tr("Les jeux sont des dossiers de %1 : Pieces/<jeu> (pièces en SVG ou PNG) et Boards/<échiquier> (image ou SVG). "
                                   "Ajoutez, renommez ou supprimez-en : les listes sont relues à chaque ouverture des Préférences. "
                                   "Les formats acceptés sont décrits dans le fichier format.html de ce dossier.")
                                    .arg(QDir::toNativeSeparators(defaultsets::themesDirectory())));
    m_userSetsHint->setObjectName("Faint");
    m_userSetsHint->setWordWrap(true);
    // Sets shipped with Hydra64 deleted or renamed: offer to put them back (button hidden if none is missing)
    m_restoreSets = new QPushButton;
    m_restoreSets->setObjectName("RestoreDefaultSets");
    m_restoreSets->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto showRestore = [this] {
        const int n = defaultsets::missing();
        m_restoreSets->setVisible(n > 0);
        m_restoreSets->setText(tr("Restaurer les jeux par défaut (%n manquant(s))", nullptr, n));
    };
    showRestore();
    connect(m_restoreSets, &QPushButton::clicked, this, [this, showRestore] {
        defaultsets::restore();
        fillSetLists(m_pieceSet->currentData().toString(), m_boardTheme->currentData().toString());
        showRestore();
        updatePiecePreview();
    });
    m_pieceShadow = new QCheckBox(tr("Légère ombre portée sous les pièces"));
    m_pieceShadow->setChecked(prefs::pieceShadow());

    // Automatic evaluation (evaluation bar and graph)
    auto spin = [](int min, int max, int v, const QString& suffix) {
        auto* s = new QSpinBox;
        s->setRange(min, max);
        s->setValue(v);
        s->setSuffix(suffix);
        s->setMinimumWidth(90);
        s->setKeyboardTracking(false); // applied when the input is validated, not at each digit typed
        return s;
    };
    m_evalThreads = spin(1, std::max(1, QThread::idealThreadCount()), prefs::evalThreads(), QString());
    m_evalHash = new QComboBox;
    for (int mb : {16, 32, 64, 128, 256, 512, 1024, 2048}) m_evalHash->addItem(tr("%1 Mo").arg(mb), mb);
    m_evalHash->setCurrentIndex(std::max(0, m_evalHash->findData(prefs::evalHashMb())));
    m_evalDepth = spin(4, 40, prefs::evalDepth(), QString());
    m_evalTime = spin(50, 10000, prefs::evalTimeMs(), tr(" ms"));
    m_evalTime->setSingleStep(50);
    auto* evalGrid = new QGridLayout;
    evalGrid->setHorizontalSpacing(12);
    evalGrid->setVerticalSpacing(6);
    m_evalThreads->setToolTip(tr("Nombre de processeurs (threads) utilisés par Stockfish pour l'évaluation"));
    m_evalHash->setToolTip(tr("Mémoire de la table de hachage de Stockfish"));
    m_evalDepth->setToolTip(tr("Profondeur visée pour chaque position"));
    m_evalTime->setToolTip(tr("Temps maximum de réflexion pour chaque position"));
    evalGrid->addWidget(new QLabel(tr("Processeurs")), 0, 0);
    evalGrid->addWidget(m_evalThreads, 0, 1);
    evalGrid->addWidget(new QLabel(tr("Mémoire")), 0, 2);
    evalGrid->addWidget(m_evalHash, 0, 3);
    evalGrid->addWidget(new QLabel(tr("Profondeur")), 1, 0);
    evalGrid->addWidget(m_evalDepth, 1, 1);
    evalGrid->addWidget(new QLabel(tr("Temps max.")), 1, 2);
    evalGrid->addWidget(m_evalTime, 1, 3);
    evalGrid->setColumnStretch(4, 1);
    auto* evalNote = new QLabel(tr("Stockfish embarqué évalue en arrière-plan chaque position (barre et courbe d'évaluation), "
                                   "tant que le panneau Évaluation est ouvert sur la page."));
    evalNote->setObjectName("Faint");
    evalNote->setWordWrap(true);

    m_restoreSession = new QCheckBox(tr("Restaurer la partie de chaque page au démarrage"));
    m_restoreSession->setToolTip(tr("Une partie contre le moteur en cours reprend là où elle en était, pendules comprises, moteur arrêté (Espace pour le relancer)"));
    m_restoreSession->setChecked(prefs::restoreSession());

    // ChessBase players encyclopedia: folder chosen here, applied on validation
    m_encyclopediaDir = new QLineEdit(playerEncyclopedia::directory());
    m_encyclopediaDir->setObjectName("EncyclopediaDir");
    m_encyclopediaDir->setReadOnly(true);
    m_encyclopediaDir->setPlaceholderText(tr("Aucun dossier"));
    auto* encChoose = new QPushButton(theme::icon("folder"), tr("Choisir…"));
    auto* encClear = new QPushButton(tr("Retirer"));
    m_encyclopediaStatus = new QLabel;
    m_encyclopediaStatus->setWordWrap(true);
    auto* encRow = new QHBoxLayout;
    encRow->addWidget(m_encyclopediaDir, 1);
    encRow->addWidget(encChoose);
    encRow->addWidget(encClear);
    auto* encNote = new QLabel(tr("Dossier « Playbase » de l'encyclopédie des joueurs de ChessBase. La fiche d'un joueur affiche alors sa photo, son titre, sa fédération et ses Elo FIDE."));
    encNote->setWordWrap(true);
    connect(encChoose, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Dossier de l'encyclopédie des joueurs (Playbase)"), m_encyclopediaDir->text());
        if (dir.isEmpty()) return;
        m_encyclopediaDir->setText(dir);
        checkEncyclopedia();
    });
    connect(encClear, &QPushButton::clicked, this, [this] {
        m_encyclopediaDir->clear();
        checkEncyclopedia();
    });
    checkEncyclopedia();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Valider"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));

    // One page per category; each page stacks its sections.
    auto makePage = [](std::initializer_list<std::function<void(QVBoxLayout*)>> sections) {
        auto* page = new QWidget;
        auto* l = new QVBoxLayout(page);
        l->setContentsMargins(0, 4, 0, 0);
        l->setSpacing(8);
        bool first = true;
        for (const auto& add : sections) {
            if (!first) l->addSpacing(12);
            first = false;
            add(l);
        }
        l->addStretch(1);
        return page;
    };
    QWidget* general = makePage({
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Langue")));
            l->addWidget(m_language);
            l->addWidget(m_restartNote);
        },
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Notation des coups")));
            l->addWidget(m_notation);
            l->addWidget(m_preview);
        },
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Interface")));
            l->addWidget(m_hideMenu);
        },
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Démarrage")));
            l->addWidget(m_restoreSession);
        },
    });
    QWidget* board = makePage({
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Apparence")));
            l->addLayout(pieceRow);
            l->addWidget(m_pieceShadow);
            l->addWidget(m_piecePreview);
            l->addWidget(m_pieceCredit);
            l->addWidget(m_userSetsHint);
            l->addWidget(m_restoreSets);
        },
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Affichage")));
            l->addWidget(m_evalBar);
        },
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Navigation")));
            l->addWidget(m_wheel);
            l->addWidget(m_wheelInvert);
        },
    });
    QWidget* bases = makePage({
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Encyclopédie des joueurs")));
            l->addWidget(encNote);
            l->addLayout(encRow);
            l->addWidget(m_encyclopediaStatus);
        },
    });
    QWidget* evaluation = makePage({
        [&](QVBoxLayout* l) {
            l->addWidget(section(tr("Évaluation automatique")));
            l->addWidget(evalNote);
            l->addLayout(evalGrid);
        },
    });

    // Endgame tablebases: separate page (sources, engines, download), scrollable
    m_tablebasePage = new TablebasePage;
    auto* tableScroll = new QScrollArea;
    tableScroll->setWidgetResizable(true);
    tableScroll->setFrameShape(QFrame::NoFrame);
    tableScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tableScroll->setWidget(m_tablebasePage);

    // Categories: list on the side, the chosen page on the right
    m_pages = new QStackedWidget;
    auto* nav = new QListWidget;
    nav->setObjectName("PrefsNav");
    nav->setIconSize(QSize(18, 18));
    nav->setFocusPolicy(Qt::NoFocus);
    nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const std::tuple<QWidget*, QString, const char*> pages[] = {
        {general, tr("Général"), "layout"},
        {board, tr("Échiquier"), "board"},
        {evaluation, tr("Évaluation"), "analysis"},
        {bases, tr("Bases"), "database"},
        {tableScroll, tr("Finales"), "board"},
    };
    for (const auto& [page, label, icon] : pages) {
        m_pages->addWidget(page);
        new QListWidgetItem(theme::icon(QLatin1String(icon)), label, nav);
    }
    nav->setFixedWidth(160);
    connect(nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        m_pages->setCurrentIndex(row);
        s_lastPage = row; // the category is found again at the next opening
    });
    nav->setCurrentRow(s_lastPage);

    auto* body = new QHBoxLayout;
    body->setSpacing(20);
    body->addWidget(nav);
    body->addWidget(m_pages, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 16);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addSpacing(10);
    layout->addLayout(body, 1);
    layout->addSpacing(16);
    layout->addWidget(buttons);

    connect(m_language, &QComboBox::currentIndexChanged, this, [this] {
        m_restartNote->setVisible(m_language->currentData().toString() != prefs::language());
    });
    connect(m_notation, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updatePreview);
    connect(m_pieceSet, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updatePiecePreview);
    connect(m_boardTheme, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updatePiecePreview);
    connect(m_boardTheme, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updateBoardColors);
    connect(m_wheel, &QCheckBox::toggled, m_wheelInvert, &QWidget::setEnabled);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Immediate application: each change is taken into account without waiting for "OK".
    // "Cancel" restores the settings present on opening (except the language, applied on validation).
    m_initial = current();
    for (QCheckBox* c : {m_wheel, m_wheelInvert, m_restoreSession, m_evalBar, m_hideMenu, m_pieceShadow})
        connect(c, &QCheckBox::toggled, this, [this] { apply(current()); });
    connect(m_pieceShadow, &QCheckBox::toggled, this, &PreferencesDialog::updatePiecePreview);
    for (QComboBox* c : {m_notation, m_pieceSet, m_boardTheme, m_evalHash})
        connect(c, &QComboBox::currentIndexChanged, this, [this] { apply(current()); });
    for (QSpinBox* s : {m_evalThreads, m_evalDepth, m_evalTime})
        connect(s, &QSpinBox::valueChanged, this, [this] { apply(current()); });

    updatePreview();
    updatePiecePreview();
    updateBoardColors();
}

namespace {
// Icon of a board in the lists: a light square and a dark square.
QIcon boardIcon(const QString& id, qreal dpr)
{
    QPixmap icon(QSize(44, 22) * dpr);
    icon.setDevicePixelRatio(dpr);
    QPainter p(&icon);
    p.drawPixmap(0, 0, boardtheme::tile(0, 0, 22, dpr, id));
    p.drawPixmap(22, 0, boardtheme::tile(1, 0, 22, dpr, id));
    p.end();
    return QIcon(icon);
}

QIcon swatch(const QColor& color, qreal dpr)
{
    QPixmap pm(QSize(18, 18) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(color);
    QPainter p(&pm);
    p.setPen(QColor(0, 0, 0, 90));
    p.drawRect(QRectF(0.5, 0.5, 17, 17));
    p.end();
    return QIcon(pm);
}
} // namespace

// The two colour buttons: shown for the "plain colours" board only, each with its colour.
void PreferencesDialog::updateBoardColors()
{
    const bool colors = m_boardTheme->currentData().toString() == QLatin1String(boardtheme::ColorTheme);
    m_boardColorsLabel->setVisible(colors);
    m_boardColors->setVisible(colors);
    const qreal dpr = devicePixelRatioF();
    m_boardLightButton->setIcon(swatch(m_boardLight, dpr));
    m_boardDarkButton->setIcon(swatch(m_boardDark, dpr));
}

// Colour of the light or dark squares, chosen in the system's colour dialog and applied at once.
void PreferencesDialog::pickBoardColor(bool light)
{
    QColor& target = light ? m_boardLight : m_boardDark;
    const QColor chosen = QColorDialog::getColor(target, this, light ? tr("Couleur des cases claires") : tr("Couleur des cases foncées"));
    if (!chosen.isValid() || chosen == target) return;
    target = chosen;
    apply(current());
    const int row = m_boardTheme->findData(QLatin1String(boardtheme::ColorTheme));
    if (row >= 0) m_boardTheme->setItemIcon(row, boardIcon(QLatin1String(boardtheme::ColorTheme), devicePixelRatioF()));
    updateBoardColors();
    updatePiecePreview();
}

// Lists of the piece sets and boards, reread from disk (default set, then the user's folders).
void PreferencesDialog::fillSetLists(const QString& pieceSet, const QString& boardTheme)
{
    const QSignalBlocker blockPieces(m_pieceSet), blockBoards(m_boardTheme);
    const qreal dpr = devicePixelRatioF();
    m_pieceSet->clear();
    for (const PieceRenderer::PieceSet& set : PieceRenderer::availableSets()) {
        PieceRenderer r;
        r.setFixedSet(set.id);
        QPixmap icon(QSize(44, 22) * dpr);
        icon.fill(Qt::transparent);
        icon.setDevicePixelRatio(dpr);
        QPainter p(&icon); // icon: white king and black knight
        p.drawPixmap(0, 0, r.pixmap(chess::makePiece(chess::White, chess::King), 22, dpr));
        p.drawPixmap(22, 0, r.pixmap(chess::makePiece(chess::Black, chess::Knight), 22, dpr));
        p.end();
        m_pieceSet->addItem(QIcon(icon), set.name, set.id);
        m_pieceSet->setItemData(m_pieceSet->count() - 1, set.credit, Qt::UserRole + 1);
    }
    m_pieceSet->setCurrentIndex(std::max(0, m_pieceSet->findData(pieceSet)));
    m_boardTheme->clear();
    for (const boardtheme::Theme& t : boardtheme::available()) {
        m_boardTheme->addItem(boardIcon(t.id, dpr), t.name, t.id);
        m_boardTheme->setItemData(m_boardTheme->count() - 1, t.credit, Qt::UserRole + 1);
    }
    m_boardTheme->setCurrentIndex(std::max(0, m_boardTheme->findData(boardTheme)));
}

void PreferencesDialog::updatePiecePreview()
{
    PieceRenderer r;
    r.setFixedSet(m_pieceSet->currentData().toString());
    r.setFixedShadow(m_pieceShadow->isChecked() ? 1 : 0);
    const int sz = 40;
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(QSize(sz * 12, sz) * dpr);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    const QString board = m_boardTheme->currentData().toString();
    int i = 0;
    for (chess::Color c : {chess::White, chess::Black})
        for (chess::PieceType t : {chess::King, chess::Queen, chess::Rook, chess::Bishop, chess::Knight, chess::Pawn}) {
            // Squares of the chosen board, taken from its ranks 8 then 6 (the alternation continues).
            p.drawPixmap(i * sz, 0, boardtheme::tile(i % 8, (i / 8) * 2, sz, dpr, board));
            p.drawPixmap(i * sz, 0, r.pixmap(chess::makePiece(c, t), sz, dpr));
            ++i;
        }
    p.end();
    m_piecePreview->setPixmap(pm);
    const QString boardCredit = m_boardTheme->currentData(Qt::UserRole + 1).toString();
    QString credit = tr("Pièces : %1").arg(m_pieceSet->currentData(Qt::UserRole + 1).toString());
    if (!boardCredit.isEmpty()) credit += tr(" — échiquier : %1").arg(boardCredit);
    m_pieceCredit->setText(credit);
}

void PreferencesDialog::updatePreview()
{
    const auto n = prefs::Notation(m_notation->currentData().toInt());
    QStringList moves;
    int number = 1;
    const char* line[] = {"e4", "e5", "Nf3", "Nc6", "Bb5", "a6", "O-O", "Nf6", "Re1", "Qe7"};
    for (int i = 0; i < 10; ++i) {
        if (i % 2 == 0) moves << QStringLiteral("%1.").arg(number++);
        moves << display::san(line[i], n);
    }
    m_preview->setText(tr("Aperçu : %1").arg(moves.join(' ')));
}

PreferencesDialog::Settings PreferencesDialog::current() const
{
    return {
        m_wheel->isChecked(),
        m_wheelInvert->isChecked(),
        m_restoreSession->isChecked(),
        m_evalBar->isChecked(),
        m_hideMenu->isChecked(),
        m_pieceShadow->isChecked(),
        m_pieceSet->currentData().toString(),
        m_boardTheme->currentData().toString(),
        m_boardLight,
        m_boardDark,
        prefs::Notation(m_notation->currentData().toInt()),
        m_evalThreads->value(),
        m_evalHash->currentData().toInt(),
        m_evalDepth->value(),
        m_evalTime->value(),
    };
}

void PreferencesDialog::apply(const Settings& s)
{
    prefs::setWheelNavigation(s.wheel);
    prefs::setWheelInverted(s.wheelInverted);
    prefs::setRestoreSession(s.restoreSession);
    prefs::setShowEvalBar(s.evalBar);
    prefs::setHideMenuBar(s.hideMenu);
    prefs::setPieceShadow(s.pieceShadow);
    prefs::setPieceSet(s.pieceSet);
    prefs::setBoardColors(s.boardLight, s.boardDark);
    prefs::setBoardTheme(s.boardTheme);
    prefs::setEvaluation(s.evalThreads, s.evalHashMb, s.evalDepth, s.evalTimeMs);
    if (s.notation != prefs::notation()) prefs::setNotation(s.notation); // emits changed(): displays updated
}

void PreferencesDialog::checkEncyclopedia()
{
    const QString dir = m_encyclopediaDir->text();
    if (dir.isEmpty()) {
        m_encyclopediaStatus->setText(tr("Aucune encyclopédie."));
        return;
    }
    hy64db::PlayerBase base;
    std::string error;
    if (base.open(dir.toStdString(), &error))
        m_encyclopediaStatus->setText(tr("Encyclopédie lisible : %1 fiches.").arg(QLocale().toString(qulonglong(base.count()))));
    else
        m_encyclopediaStatus->setText(QStringLiteral("<font color=\"#e68a35\">%1</font>").arg(tr("Ce dossier ne contient pas l'encyclopédie des joueurs de ChessBase (%1).").arg(QString::fromStdString(error).toHtmlEscaped())));
}

void PreferencesDialog::accept()
{
    const QString lang = m_language->currentData().toString();
    m_languageChanged = lang != prefs::language();
    prefs::setLanguage(lang);
    apply(current());
    if (m_encyclopediaDir->text() != playerEncyclopedia::directory()) {
        // Unreadable folder: it is not kept (the displayed state has already reported it).
        if (!playerEncyclopedia::setDirectory(m_encyclopediaDir->text(), nullptr)) playerEncyclopedia::setDirectory({}, nullptr);
    }
    m_tablebasePage->commit();
    QDialog::accept();
}

void PreferencesDialog::openOnTablebases() { s_lastPage = 4; }

void PreferencesDialog::reject()
{
    apply(m_initial);
    QDialog::reject();
}
