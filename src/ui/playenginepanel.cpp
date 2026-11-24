#include "playenginepanel.h"
#include "enginegame.h"
#include "enginepanel.h"
#include "evalbar.h"
#include "modulewidgets.h"
#include "playdialog.h"
#include "tabengine.h"
#include "preferences.h"
#include "theme.h"

#include <QSettings>
#include <QSignalBlocker>
#include <QWidgetAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QLocale>
#include <QThread>
#include <QToolButton>
#include <QMouseEvent>
#include <QVBoxLayout>

using namespace chess;


PlayEnginePanel::PlayEnginePanel(EngineGame* game, QWidget* parent)
    : QWidget(parent)
    , m_game(game)
{
    auto* bar = new QWidget;
    bar->setObjectName("PanelBody");
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 6, 8, 2);
    bl->setSpacing(4);
    m_name = module::nameButton(tr("Moteur adversaire — clic : changer d'adversaire sans recommencer la partie"));
    m_name->setPopupMode(QToolButton::InstantPopup);
    m_name->setMenu(new QMenu(m_name));
    m_name->setStyleSheet(QStringLiteral("QToolButton { padding-right: 22px; }"
                                         "QToolButton::menu-indicator { image: url(:/style/chevron-down.svg); subcontrol-origin: padding;"
                                         " subcontrol-position: center right; right: 5px; width: 12px; height: 12px; }"));
    m_play = module::button("play", {}, {});
    m_cpus = module::button({}, {}, tr("Processeurs utilisés par le moteur — clic : paramètres du moteur"));
    bl->addWidget(m_play);
    bl->addSpacing(4);
    bl->addWidget(m_name);
    bl->addSpacing(4);
    m_pv = new module::VariantsStepper;
    bl->addWidget(m_pv);
    bl->addSpacing(4);
    bl->addWidget(m_cpus);
    bl->addSpacing(4);
    m_strength = module::button({}, {}, tr("Force de l'adversaire — clic : la changer, même en cours de partie"));
    m_strength->setObjectName("PlayStrength");
    m_strength->setPopupMode(QToolButton::InstantPopup);
    {
        // The slider of the "Play against the engine" dialog, in a menu; applied on closing
        // (a single change recorded in the notation)
        auto* menu = new QMenu(m_strength);
        auto* box = new QWidget;
        auto* vl = new QVBoxLayout(box);
        vl->setContentsMargins(12, 10, 12, 10);
        auto* slider = new StrengthSlider;
        vl->addWidget(slider);
        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(box);
        menu->addAction(action);
        m_strength->setMenu(menu);
        connect(menu, &QMenu::aboutToShow, this, [this, slider] {
            const PlaySettings ps = m_game->isActive() ? m_game->settings() : PlayDialog::savedSettings();
            slider->setStrength(ps.fullStrength, ps.elo, ps.beginnerLevel, ps.tablebases);
            m_strengthStep = slider->step();
        });
        // Nothing if the slider has not moved: an off-level Elo (1493) is not rounded by merely opening it
        connect(menu, &QMenu::aboutToHide, this, [this, slider] {
            if (slider->step() != m_strengthStep) setStrength(slider->fullStrength(), slider->elo(), slider->beginnerLevel(), slider->tablebases());
        });
    }
    bl->addWidget(m_strength);
    bl->addSpacing(4);
    m_book = module::button("book", {}, {});
    m_book->setCheckable(true);
    bl->addWidget(m_book);
    bl->addStretch(1);
    // Spectators of the Play page: engines that analyse the game beside the opponent (panel under this one)
    m_spectator = module::button("new", {}, tr("Inviter un moteur spectateur : il analyse la partie dans son propre panneau, sous l'adversaire (Ctrl+Maj+K)"));
    m_spectator->setObjectName("PlayInviteSpectator");
    m_spectator->setPopupMode(QToolButton::InstantPopup);
    m_spectator->setMenu(new QMenu(m_spectator));
    m_spectator->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    module::engineChoiceMenu(m_spectator->menu(), [this](const QString& id) { emit spectatorRequested(id); });
    bl->addWidget(m_spectator);

    auto* stats = new QWidget;
    stats->setObjectName("PanelBody");
    auto* sl = new QHBoxLayout(stats);
    sl->setContentsMargins(8, 2, 8, 6);
    sl->setSpacing(4);
    m_eval = module::stat();
    m_depth = module::stat();
    m_depth->setObjectName("PlayDepth");
    m_speed = module::stat();
    sl->addWidget(m_eval);
    sl->addWidget(m_depth, 1);
    sl->addWidget(m_speed);
    // Tip: a click on this line during the search makes the engine play right away
    m_stats = stats;
    m_stats->setObjectName("PlayStats");
    for (QWidget* w : {static_cast<QWidget*>(m_stats), static_cast<QWidget*>(m_eval), static_cast<QWidget*>(m_depth), static_cast<QWidget*>(m_speed)}) w->installEventFilter(this);

    m_view = new EngineLinesView;

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(stats);
    layout->addWidget(m_view, 1);

    // List of engines: choosing one changes the opponent of the game in progress (or of the
    // next one); the other settings stay in "Play against the engine" (Ctrl+G).
    connect(m_name->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* menu = m_name->menu();
        menu->clear();
        const QString current = engine().id;
        for (const EngineConfig& e : EngineRegistry::instance().engines()) {
            QAction* a = menu->addAction(e.name, this, [this, id = e.id] { emit engineSelected(id); });
            a->setCheckable(true);
            a->setChecked(e.id == current);
        }
        menu->addSeparator();
        menu->addAction(theme::icon("engine"), tr("Nouvelle partie contre le moteur… (Ctrl+G)"), this, &PlayEnginePanel::chooseEngineRequested);
    });
    connect(m_play, &QToolButton::clicked, this, &PlayEnginePanel::togglePlay);
    connect(m_pv, &module::VariantsStepper::changed, this, [this](int n) {
        m_game->setMultiPv(n);
        refresh();
    });
    connect(m_cpus, &QToolButton::clicked, this, [this] { emit engineSettingsRequested(engine().id); });
    connect(m_book, &QToolButton::toggled, this, [this](bool on) { m_game->setUseBook(on); });
    connect(m_game, &EngineGame::bookMovePlayed, this, [this] {
        m_lines.clear();
        refreshRows();
        m_eval->setText(tr("Arbre d'ouverture"));
        m_depth->setText(tr("Coup de l'arbre"));
        m_speed->setText(QStringLiteral("–"));
    });
    connect(m_game, &EngineGame::engineInfo, this, &PlayEnginePanel::onInfo);
    connect(m_game, &EngineGame::searchStarted, this, [this] { m_lines.clear(); });
    connect(m_game, &EngineGame::stateChanged, this, &PlayEnginePanel::refresh);
    connect(&EngineRegistry::instance(), &EngineRegistry::changed, this, &PlayEnginePanel::refresh);
    connect(&tabengine::Notifier::instance(), &tabengine::Notifier::changed, this, &PlayEnginePanel::refresh);
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, &PlayEnginePanel::refreshRows);
    refresh();
}

EngineConfig PlayEnginePanel::engine() const
{
    if (m_game->isActive() && m_game->engineConfig().isValid()) {
        // Up-to-date values (the options may have changed in the engine manager)
        const EngineConfig e = EngineRegistry::instance().engine(m_game->engineConfig().id);
        return e.isValid() ? tabengine::effective(QLatin1String(tabengine::PlayTab), e) : m_game->engineConfig();
    }
    EngineConfig e = EngineRegistry::instance().engine(PlayDialog::savedSettings().engineId);
    return tabengine::effective(QLatin1String(tabengine::PlayTab), e.isValid() ? e : EngineRegistry::instance().fallbackEngine());
}

void PlayEnginePanel::refresh()
{
    const EngineConfig e = engine();
    const QString name = e.isValid() ? e.name : tr("Aucun moteur");
    m_name->setText(name);
    if (name != m_title) {
        m_title = name;
        emit titleChanged(name);
    }
    m_cpus->setText(module::cpuText(e));
    m_cpus->setEnabled(e.isValid() && !e.id.isEmpty());
    {
        const PlaySettings ps = m_game->isActive() ? m_game->settings() : PlayDialog::savedSettings();
        m_strength->setText(ps.beginnerLevel > 0 ? tr("Débutant %1").arg(ps.beginnerLevel) : !ps.fullStrength ? tr("Elo %1").arg(ps.elo) : ps.tablebases ? tr("Max. + tables") : tr("Max."));
        m_strength->setVisible(e.isValid() && (e.options.isEmpty() || e.option(QStringLiteral("UCI_Elo"))));
    }

    const bool active = m_game->isActive();
    // Stop while the engine is thinking, Play as soon as it waits (after its move, or stopped).
    const bool running = active && m_game->isThinking();
    if (running != m_running) {
        m_running = running;
        emit runningChanged(running);
    }
    m_play->setEnabled(e.isValid());
    m_play->setIcon(theme::icon(running ? "stop" : "play"));
    const bool canForce = running && !m_game->isPlayingBookMove(); // only during its search
    m_stats->setCursor(canForce ? Qt::PointingHandCursor : Qt::ArrowCursor);
    m_stats->setToolTip(canForce ? tr("Le moteur réfléchit — clic : il joue tout de suite son meilleur coup (Ctrl+Espace)") : QString());
    m_play->setToolTip(running ? tr("Arrêter le moteur : il ne joue plus jusqu'à Play (Espace)")
                               : tr("Faire jouer le moteur : il joue le coup du camp au trait, vous prenez l'autre (Espace)"));
    {
        QSignalBlocker blocker(m_book);
        m_book->setChecked(m_game->useBook());
    }
    m_book->setToolTip(m_game->useBook() ? tr("Arbre d'ouverture : le moteur joue les coups de l'arbre ouvert tant qu'il y en a — clic : désactiver")
                                         : tr("Arbre d'ouverture désactivé : le moteur réfléchit dès le premier coup — clic : activer"));
    m_pv->setValue(m_game->multiPv());
    m_pv->setVisible(module::supportsMultiPv(e)); // single-line engine (Fruit…): no choice

    QString placeholder;
    if (!e.isValid()) placeholder = tr("Aucun moteur configuré (Moteur → Gestion des moteurs…).");
    else if (!active) placeholder = tr("Jouez un coup pour commencer une partie contre %1,\nou Play pour qu'il joue le camp au trait.").arg(e.name);
    else if (m_game->isHalted()) placeholder = tr("Moteur arrêté : coups libres. Play pour qu'il reprenne.");
    else if (m_game->isPaused()) placeholder = tr("Partie reprise, en pause : les pendules attendent.\nJouez votre coup pour continuer.");
    else if (m_game->isAwaiting()) placeholder = tr("Coup supprimé : le moteur attend.\nPlay pour qu'il joue, ou jouez un coup de son camp pour échanger les camps.");
    else if (m_game->isPlayingBookMove()) placeholder = tr("Coup de l'arbre…");
    else if (m_game->isThinking()) placeholder = tr("Réflexion…");
    else placeholder = tr("À vous de jouer.");
    m_view->setPlaceholder(placeholder);
    if (!active && m_lines.isEmpty()) {
        m_eval->setText(QStringLiteral("–"));
        m_depth->setText(tr("Profondeur –"));
        m_speed->setText(tr("– kN/s"));
    }
    while (m_lines.size() > m_game->multiPv()) m_lines.erase(std::prev(m_lines.end()));
    refreshRows();
}

bool PlayEnginePanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton
        && (watched == m_stats || watched == m_eval || watched == m_depth || watched == m_speed)) {
        forceMove(); // no effect if the engine is not thinking
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void PlayEnginePanel::togglePlay()
{
    if (!m_game->isActive()) emit startRequested();
    else if (m_game->isThinking()) m_game->halt();
    else m_game->moveNow(); // it plays the side to move (you take the other)
}

void PlayEnginePanel::setStrength(bool fullStrength, int elo, int beginnerLevel, bool tablebases)
{
    PlaySettings wanted;
    wanted.fullStrength = fullStrength && beginnerLevel == 0;
    wanted.elo = elo;
    wanted.beginnerLevel = beginnerLevel;
    wanted.tablebases = tablebases;
    const PlaySettings ps = m_game->isActive() ? m_game->settings() : PlayDialog::savedSettings();
    if (EngineGame::strengthText(ps) == EngineGame::strengthText(wanted)) return; // nothing changed
    if (m_game->isActive() && !m_game->setStrength(fullStrength, elo, beginnerLevel, tablebases)) return;
    // Remembered for the next games (same settings as the "Play against the engine" dialog)
    QSettings s;
    s.setValue("play/full", wanted.fullStrength);
    s.setValue("play/elo", elo);
    s.setValue("play/beginner", beginnerLevel);
    if (wanted.fullStrength) s.setValue("play/tables", tablebases);
    refresh();
    emit strengthChanged();
}

void PlayEnginePanel::forceMove()
{
    if (m_game->isActive() && m_game->isThinking()) m_game->moveNow();
}

void PlayEnginePanel::onInfo(const EngineLine& line)
{
    if (line.multipv < 1 || line.multipv > m_game->multiPv()) return;
    m_lines[line.multipv] = line;
    refreshRows();
    if (line.multipv != 1) return;
    const QLocale loc;
    // Endgame tablebases: the number of probes shows that the engine uses them (as in analysis)
    m_depth->setText(line.tbHits > 0 ? tr("Profondeur %1/%2 · tables : %3").arg(line.depth).arg(line.seldepth).arg(loc.toString(line.tbHits))
                                     : tr("Profondeur %1/%2").arg(line.depth).arg(line.seldepth));
    m_depth->setToolTip(line.tbHits > 0 ? tr("Le moteur a consulté %1 fois les tables de finales pendant cette recherche. « +Tables » : gain lu dans les tables.").arg(loc.toString(line.tbHits)) : QString());
    m_speed->setText(tr("%1 kN/s").arg(loc.toString(line.nps / 1000)));
}

void PlayEnginePanel::refreshRows()
{
    QList<EngineLinesView::Row> rows;
    for (const EngineLine& l : std::as_const(m_lines)) rows << EngineLinesView::rowFor(m_game->searchPosition(), l);
    m_view->setRows(rows);
    m_view->setMinimumHeight(28 * std::max(2, int(rows.size())));
    if (!rows.isEmpty()) m_eval->setText(EvalBar::format(rows.first().eval));
}
