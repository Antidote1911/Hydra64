#include "enginepanel.h"
#include "tabengine.h"
#include "display.h"
#include "preferences.h"
#include "evalbar.h"
#include "modulewidgets.h"
#include "theme.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QSettings>
#include <QContextMenuEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace chess;

// ---------------------------------------------------------------- EngineLinesView

EngineLinesView::EngineLinesView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(RowHeight * 2);
}

void EngineLinesView::setRows(const QList<Row>& rows)
{
    m_rows = rows;
    update();
}

void EngineLinesView::setPlaceholder(const QString& text)
{
    m_placeholder = text;
    update();
}

void EngineLinesView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::Panel);

    if (m_rows.isEmpty()) {
        p.setPen(theme::TextFaint);
        p.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
        return;
    }

    QFont badgeFont = font();
    badgeFont.setWeight(QFont::Bold);
    badgeFont.setPixelSize(11);
    QFont lineFont = font();

    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        const QRectF r(0, i * RowHeight, width(), RowHeight);
        if (i % 2) p.fillRect(r, theme::PanelAlt);
        if (i == m_hover) p.fillRect(r, QColor(255, 255, 255, 12));

        // Score badge: light if White is better, dark otherwise
        const QRectF badge(10, r.top() + 5, 54, RowHeight - 10);
        const bool whiteBetter = EvalBar::whiteShare(row.eval) >= 0.5;
        QPainterPath bp;
        bp.addRoundedRect(badge, 3, 3);
        p.fillPath(bp, whiteBetter ? QColor(0xe8, 0xe6, 0xe0) : QColor(0x0f, 0x0f, 0x11));
        if (!whiteBetter) {
            p.setPen(QPen(theme::BorderSoft, 1));
            p.drawPath(bp);
        }
        p.setFont(badgeFont);
        p.setPen(whiteBetter ? QColor(0x1a, 0x1a, 0x1c) : theme::Text);
        p.drawText(badge, Qt::AlignCenter, EvalBar::format(row.eval));

        p.setFont(lineFont);
        p.setPen(i == 0 ? theme::Text : theme::TextDim);
        const QRectF tr = r.adjusted(76, 0, -10, 0);
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(row.text, Qt::ElideRight, int(tr.width())));
    }
}

void EngineLinesView::mouseMoveEvent(QMouseEvent* e)
{
    int h = int(e->position().y()) / RowHeight;
    if (h >= m_rows.size()) h = -1;
    if (h != m_hover) {
        m_hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EngineLinesView::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int i = int(e->position().y()) / RowHeight;
    if (i >= 0 && i < m_rows.size() && !m_rows[i].firstMove.isNull()) emit movePicked(m_rows[i].firstMove);
}

void EngineLinesView::contextMenuEvent(QContextMenuEvent* e)
{
    const int i = e->pos().y() / RowHeight;
    emit rowMenuRequested(i >= 0 && i < m_rows.size() ? i : -1, e->globalPos());
}

void EngineLinesView::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- EnginePanel

namespace {

// Dot of the analysed side: white, black, or half white half black (both sides).
QIcon sidesIcon(EnginePanel::Sides s)
{
    QIcon icon;
    for (qreal dpr : {1.0, 2.0}) {
        QPixmap pm(QSize(16, 16) * dpr);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(dpr);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r(2, 2, 12, 12);
        const QColor white(0xf2, 0xf2, 0xf2), black(0x16, 0x16, 0x18);
        p.setPen(Qt::NoPen);
        if (s == EnginePanel::BothSides) {
            p.setBrush(white);
            p.drawPie(r, 90 * 16, 180 * 16);
            p.setBrush(black);
            p.drawPie(r, -90 * 16, 180 * 16);
        } else {
            p.setBrush(s == EnginePanel::WhiteOnly ? white : black);
            p.drawEllipse(r);
        }
        p.setPen(QPen(theme::Text, 1.2)); // light outline: the black dot stays visible
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QString sidesLabel(EnginePanel::Sides s)
{
    switch (s) {
    case EnginePanel::WhiteOnly: return EnginePanel::tr("Coups des Blancs seulement");
    case EnginePanel::BlackOnly: return EnginePanel::tr("Coups des Noirs seulement");
    default: return EnginePanel::tr("Coups des deux camps");
    }
}

} // namespace

EnginePanel::EnginePanel(QWidget* parent)
    : QWidget(parent)
    , m_engine(new UciEngine(this))
{
    auto* bar = new QWidget;
    bar->setObjectName("PanelBody");
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 6, 8, 2);
    barLayout->setSpacing(4);

    m_name = module::nameButton(tr("Moteur d'analyse — clic : changer de moteur"));
    m_name->setPopupMode(QToolButton::InstantPopup);
    m_name->setMenu(new QMenu(m_name));
    m_toggle = module::button("play", {}, {});
    setArrowColor(theme::Accent);
    m_pv = new module::VariantsStepper;
    m_cpus = module::button({}, {}, tr("Processeurs utilisés par le moteur — clic : paramètres du moteur"));
    // Analysed side: menu of the three choices
    m_sidesButton = module::button({}, {}, {});
    m_sidesButton->setPopupMode(QToolButton::InstantPopup);
    m_sidesButton->setToolButtonStyle(Qt::ToolButtonIconOnly); // dot only
    m_sidesButton->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }"));
    auto* sidesMenu = new QMenu(m_sidesButton);
    for (Sides s : {BothSides, WhiteOnly, BlackOnly}) {
        QAction* a = sidesMenu->addAction(sidesIcon(s), sidesLabel(s));
        a->setData(int(s));
        a->setCheckable(true);
    }
    m_sidesButton->setMenu(sidesMenu);
    connect(sidesMenu, &QMenu::triggered, this, [this](QAction* a) { setSides(Sides(a->data().toInt())); });

    barLayout->addWidget(m_toggle);
    barLayout->addSpacing(4);
    barLayout->addWidget(m_name);
    barLayout->addSpacing(4);
    barLayout->addWidget(m_pv);
    barLayout->addSpacing(4);
    barLayout->addWidget(m_cpus);
    barLayout->addSpacing(4);
    barLayout->addWidget(m_sidesButton);
    m_lock = module::button("lock", {}, tr("Verrouiller le moteur sur cette position (il ne suit plus l'échiquier)"));
    m_lock->setCheckable(true);
    barLayout->addSpacing(4);
    barLayout->addWidget(m_lock);
    barLayout->addStretch(1);

    auto* stats = new QWidget;
    stats->setObjectName("PanelBody");
    auto* sl = new QHBoxLayout(stats);
    sl->setContentsMargins(8, 2, 8, 6);
    sl->setSpacing(4);
    m_eval = module::stat();
    m_depth = module::stat();
    m_speed = module::stat();
    sl->addWidget(m_eval);
    sl->addWidget(m_depth, 1);
    sl->addWidget(m_speed);

    m_view = new EngineLinesView;
    m_view->setPlaceholder(tr("Play pour lancer l'analyse."));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(stats);
    layout->addWidget(m_view, 1);

    connect(m_toggle, &QToolButton::clicked, this, [this] { setAnalysing(!m_on); });
    connect(m_lock, &QToolButton::toggled, this, &EnginePanel::setLocked);
    connect(m_view, &EngineLinesView::rowMenuRequested, this, &EnginePanel::rowMenu);
    connect(m_cpus, &QToolButton::clicked, this, [this] { emit engineSettingsRequested(m_config.id); });
    connect(m_pv, &module::VariantsStepper::changed, this, [this](int v) {
        if (m_remembersChoice) QSettings().setValue("analysisVariants", v);
        if (module::supportsMultiPv(m_config)) m_engine->setOption(QStringLiteral("MultiPV"), QString::number(v));
        if (isAnalysing()) analyseCurrent();
    });
    connect(m_name->menu(), &QMenu::triggered, this, [this](QAction* a) {
        const EngineConfig cfg = EngineRegistry::instance().engine(a->data().toString());
        if (!cfg.isValid()) return;
        if (m_remembersChoice) QSettings().setValue("analysisEngine", cfg.id);
        setEngine(cfg);
    });
    connect(m_view, &EngineLinesView::movePicked, this, &EnginePanel::movePicked);
    m_refresh = new QTimer(this);
    m_refresh->setSingleShot(true);
    m_refresh->setInterval(40);
    connect(m_refresh, &QTimer::timeout, this, &EnginePanel::flushInfo);
    connect(m_engine, &UciEngine::infoReceived, this, &EnginePanel::onInfo);
    connect(m_engine, &UciEngine::started, this, [this](const QString& name) {
        emit statusMessage(tr("Moteur prêt : %1").arg(name));
    });
    // Options or list changed in the engine manager: taken into account at once.
    connect(&EngineRegistry::instance(), &EngineRegistry::changed, this, [this] {
        refreshEngineList();
        if (m_config.id.isEmpty()) return; // engine outside the registry
        const EngineConfig cfg = EngineRegistry::instance().engine(m_config.id);
        const EngineConfig mine = tabengine::effective(m_tabKey, cfg);
        if (!cfg.isValid()) setEngine(EngineRegistry::instance().fallbackEngine());
        else if (mine.path != m_config.path || mine.values != m_config.values) setEngine(cfg);
        else m_config = mine; // plain renaming
        emit engineChanged(m_config);
    });
    // Settings of this tab changed (CPU button): the engine is restarted with them
    connect(&tabengine::Notifier::instance(), &tabengine::Notifier::changed, this, [this](const QString& tab) {
        if (tab != m_tabKey || m_tabKey.isEmpty() || m_config.id.isEmpty()) return;
        const EngineConfig cfg = EngineRegistry::instance().engine(m_config.id);
        if (cfg.isValid() && tabengine::effective(m_tabKey, cfg).values != m_config.values) setEngine(cfg);
    });
    m_pv->setValue(QSettings().value("analysisVariants", 4).toInt());
    setSides(BothSides);
    refreshEngineList();
    refreshStatus();
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, &EnginePanel::refreshRows);
    connect(m_engine, &UciEngine::errorOccurred, this, [this](const QString& msg) {
        emit statusMessage(msg);
        setAnalysing(false);
        m_view->setPlaceholder(msg + '\n' + tr("Choisissez un moteur dans le menu Moteur."));
    });
    connect(m_engine, &UciEngine::terminated, this, [this] {
        if (m_on) setAnalysing(false);
    });
}

void EnginePanel::refreshEngineList()
{
    QMenu* menu = m_name->menu();
    menu->clear();
    for (const EngineConfig& e : EngineRegistry::instance().engines()) {
        QAction* a = menu->addAction(e.name);
        a->setData(e.id);
        a->setCheckable(true);
        a->setChecked(e.id == m_config.id);
    }
    if (menu->isEmpty()) menu->addAction(tr("Aucun moteur"))->setEnabled(false);
    m_name->setText(m_config.isValid() ? m_config.name : tr("Aucun moteur"));
    m_cpus->setText(module::cpuText(m_config));
    m_cpus->setEnabled(m_config.isValid() && !m_config.id.isEmpty());
    // Single-line engine (Fruit…): no choice of the number of lines.
    m_pv->setVisible(module::supportsMultiPv(m_config));
}

void EnginePanel::refreshStatus()
{
    const bool on = isAnalysing();
    m_toggle->setIcon(theme::icon(on ? "stop" : "play"));
    m_toggle->setToolTip(on ? tr("Arrêter l'analyse de ce moteur") : tr("Lancer l'analyse de ce moteur"));
    if (!on) {
        m_eval->setText(QStringLiteral("–"));
        m_depth->setText(tr("Profondeur –"));
        m_speed->setText(tr("– kN/s"));
    }
}

void EnginePanel::setEngine(const EngineConfig& cfg)
{
    const bool wasOn = isAnalysing();
    if (wasOn) setAnalysing(false);
    m_engine->quit();
    m_config = tabengine::effective(m_tabKey, cfg); // `cfg`: engine settings; the tab adds its own
    refreshEngineList();
    emit engineChanged(m_config);
    if (wasOn) setAnalysing(true); // restart with the new engine / the new options
}

void EnginePanel::setEnginePath(const QString& path)
{
    EngineConfig cfg;
    cfg.name = QFileInfo(path).baseName();
    cfg.path = path;
    setEngine(cfg);
}

void EnginePanel::ensureStarted()
{
    if (m_engine->isRunning()) return;
    if (!m_config.isValid()) {
        emit statusMessage(tr("Aucun moteur configuré (menu Moteur → Gestion des moteurs…)"));
        m_view->setPlaceholder(tr("Aucun moteur configuré.\nInstallez Stockfish (sudo pacman -S stockfish)\npuis ajoutez-le dans Moteur → Gestion des moteurs…"));
        return;
    }
    if (!m_engine->start(m_config.path)) {
        emit statusMessage(tr("Impossible de lancer %1").arg(m_config.path));
        m_view->setPlaceholder(tr("Impossible de lancer le moteur :\n%1").arg(m_config.path));
        return;
    }
    if (m_config.id.isEmpty()) { // engine outside the registry: reasonable default settings
        m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
        m_engine->setOption(QStringLiteral("Hash"), QStringLiteral("256"));
    }
    // Options set in the engine manager; MultiPV is driven by this panel.
    for (auto it = m_config.values.cbegin(); it != m_config.values.cend(); ++it)
        if (it.key().compare(QLatin1String("MultiPV"), Qt::CaseInsensitive) != 0) m_engine->setOption(it.key(), it.value());
    if (module::supportsMultiPv(m_config)) m_engine->setOption(QStringLiteral("MultiPV"), QString::number(multiPv()));
}

void EnginePanel::setArrowColor(const QColor& color)
{
    m_arrowColor = color;
    // Menu arrow on the right, without overlapping the name; edging in the arrow's colour.
    m_name->setStyleSheet(QStringLiteral("QToolButton { padding-right: 22px; border-bottom: 3px solid %1; }"
                                         "QToolButton::menu-indicator { image: url(:/style/chevron-down.svg); subcontrol-origin: padding;"
                                         " subcontrol-position: center right; right: 5px; width: 12px; height: 12px; }")
                              .arg(color.name()));
    m_name->setToolTip(tr("Moteur — clic : changer de moteur. Sa flèche sur l'échiquier a la couleur du liseré."));
}

int EnginePanel::multiPv() const { return m_pv->value(); }

void EnginePanel::setMultiPv(int n)
{
    m_pv->setValue(n);
    if (module::supportsMultiPv(m_config)) m_engine->setOption(QStringLiteral("MultiPV"), QString::number(multiPv()));
    if (isAnalysing()) analyseCurrent();
}

bool EnginePanel::isAnalysing() const { return m_on; }

void EnginePanel::setSides(Sides s)
{
    const bool changed = s != m_sides;
    m_sides = s;
    m_sidesButton->setIcon(sidesIcon(s));
    m_sidesButton->setToolTip(tr("Camp analysé : %1 — clic : changer").arg(sidesLabel(s).toLower()));
    for (QAction* a : m_sidesButton->menu()->actions()) a->setChecked(a->data().toInt() == int(s));
    if (!changed) return;
    if (isAnalysing()) analyseCurrent();
    emit sidesChanged(s);
}

bool EnginePanel::analysesSideToMove() const
{
    if (m_sides == BothSides) return true;
    return (m_pos.sideToMove() == White) == (m_sides == WhiteOnly);
}

void EnginePanel::analyseCurrent()
{
    m_lines.clear();
    m_view->setRows({});
    if (m_pos.legalMoves().empty()) {
        m_engine->stop();
        m_view->setPlaceholder(m_pos.inCheck() ? tr("Échec et mat.") : tr("Pat."));
        emit bestMoveChanged({});
        return;
    }
    if (!analysesSideToMove()) {
        // Not the analysed side: the engine waits for the next move of its side.
        m_engine->stop();
        m_view->setPlaceholder(m_sides == WhiteOnly ? tr("En attente : ce moteur n'analyse que les coups des Blancs.")
                                                    : tr("En attente : ce moteur n'analyse que les coups des Noirs."));
        m_eval->setText(QStringLiteral("–"));
        m_depth->setText(tr("Profondeur –"));
        m_speed->setText(tr("– kN/s"));
        emit bestMoveChanged({});
        return;
    }
    m_view->setPlaceholder(tr("Analyse en cours…"));
    if (m_excluded.isEmpty()) {
        m_engine->analyse(QString::fromStdString(m_pos.fen()));
        return;
    }
    // Excluded moves: the search bears on the other legal moves
    QString go = QStringLiteral("infinite searchmoves");
    int n = 0;
    for (const Move& m : m_pos.legalMoves())
        if (!m_excluded.contains(m)) {
            go += QLatin1Char(' ') + QString::fromStdString(Position::toUci(m));
            ++n;
        }
    if (n == 0) {
        m_engine->stop();
        m_view->setPlaceholder(tr("Tous les coups sont exclus."));
        emit bestMoveChanged({});
        return;
    }
    m_engine->search(QString::fromStdString(m_pos.fen()), go);
}

void EnginePanel::setLocked(bool on)
{
    if (m_locked == on) return;
    m_locked = on;
    {
        const QSignalBlocker block(m_lock);
        m_lock->setChecked(on);
    }
    m_lock->setToolTip(on ? tr("Moteur verrouillé sur sa position — clic : suivre de nouveau l'échiquier")
                          : tr("Verrouiller le moteur sur cette position (il ne suit plus l'échiquier)"));
    if (!on) setPosition(m_boardPos);
    else emit bestMoveChanged({}); // the arrow no longer makes sense on the board
    emit statusMessage(on ? tr("Moteur verrouillé sur la position analysée") : tr("Moteur déverrouillé : il suit l'échiquier"));
}

void EnginePanel::excludeMove(Move m)
{
    if (m.isNull() || m_excluded.contains(m)) return;
    m_excluded << m;
    if (isAnalysing()) analyseCurrent();
}

void EnginePanel::clearExcluded()
{
    if (m_excluded.isEmpty()) return;
    m_excluded.clear();
    if (isAnalysing()) analyseCurrent();
}

QList<std::vector<Move>> EnginePanel::lineMoves() const
{
    QList<std::vector<Move>> out;
    for (const EngineLinesView::Row& r : m_view->rows()) out << r.moves;
    return out;
}

void EnginePanel::rowMenu(int row, const QPoint& at)
{
    QMenu menu(this);
    const QList<EngineLinesView::Row>& rows = m_view->rows();
    if (row >= 0 && row < rows.size() && !rows[row].moves.empty()) {
        const EngineLinesView::Row r = rows[row];
        menu.addAction(theme::icon("new"), tr("Ajouter la variante à la notation"), this, [this, r] { emit linesInsertRequested(m_pos, {r.moves}); });
        menu.addAction(tr("Ajouter toutes les variantes à la notation"), this, [this] { emit linesInsertRequested(m_pos, lineMoves()); });
        menu.addSeparator();
        menu.addAction(theme::icon("minus"), tr("Exclure %1 de la recherche").arg(display::san(m_pos.toSan(r.firstMove))), this,
                       [this, r] { excludeMove(r.firstMove); });
    }
    if (!m_excluded.isEmpty()) {
        QStringList names;
        for (const Move& m : m_excluded) names << display::san(m_pos.toSan(m));
        menu.addAction(tr("Inclure de nouveau tous les coups (exclus : %1)").arg(names.join(QStringLiteral(", "))), this, &EnginePanel::clearExcluded);
    }
    menu.addSeparator();
    QAction* lock = menu.addAction(theme::icon("lock"), tr("Verrouiller sur cette position"));
    lock->setCheckable(true);
    lock->setChecked(m_locked);
    connect(lock, &QAction::toggled, this, &EnginePanel::setLocked);
    menu.exec(at);
}

void EnginePanel::setAnalysing(bool on)
{
    m_on = on;
    if (on) {
        ensureStarted();
        if (!m_engine->isRunning()) {
            m_on = false;
            refreshStatus();
            emit analysingChanged(false);
            return;
        }
        analyseCurrent();
    } else {
        m_engine->stop();
        m_lines.clear();
        m_view->setRows({});
        m_view->setPlaceholder(tr("Analyse arrêtée."));
        emit bestMoveChanged({});
    }
    refreshStatus();
    emit analysingChanged(on);
}

void EnginePanel::setPosition(const Position& pos)
{
    m_boardPos = pos;
    if (m_locked) return; // locked: it keeps its position
    if (pos.fen() != m_pos.fen()) m_excluded.clear(); // exclusions apply to one position
    m_pos = pos;
    m_lines.clear();
    m_view->setRows({});
    if (isAnalysing()) analyseCurrent();
}

void EnginePanel::onInfo(const EngineLine& line)
{
    if (line.multipv < 1 || line.multipv > multiPv()) return;
    if (!m_on || !analysesSideToMove()) return; // last messages of a stopped search
    // An engine sends dozens of lines per second: the display (notation of each
    // line) is redone at most 25 times per second.
    m_lines[line.multipv] = line;
    if (!m_refresh->isActive()) m_refresh->start();
}

void EnginePanel::flushInfo()
{
    if (!m_on || m_lines.isEmpty()) return;
    refreshRows();
    const auto first = m_lines.constFind(1);
    if (first == m_lines.constEnd()) return;
    const EngineLine& line = *first;
    const QLocale loc;
    // Endgame tablebases: the number of probes shows that the engine uses them
    m_depth->setText(line.tbHits > 0 ? tr("Profondeur %1/%2 · tables : %3").arg(line.depth).arg(line.seldepth).arg(loc.toString(line.tbHits))
                                     : tr("Profondeur %1/%2").arg(line.depth).arg(line.seldepth));
    m_depth->setToolTip(line.tbHits > 0 ? tr("Le moteur a consulté %1 fois les tables de finales pendant cette recherche. « +Tables » : gain lu dans les tables.").arg(loc.toString(line.tbHits)) : QString());
    m_speed->setText(tr("%1 kN/s").arg(loc.toString(line.nps / 1000)));
    m_speed->setToolTip(tr("%1 nœuds").arg(loc.toString(line.nodes)));
}

EngineLinesView::Row EngineLinesView::rowFor(const Position& pos, const EngineLine& l)
{
    Row row;
    const int sign = pos.sideToMove() == White ? 1 : -1;
    row.eval = {true, l.mate, sign * l.score, l.depth};
    Position p = pos;
    QStringList parts;
    for (int i = 0; i < l.pv.size() && i < 24; ++i) {
        const Move m = p.parseUci(l.pv[i].toStdString());
        if (m.isNull()) break;
        if (i == 0) row.firstMove = m;
        row.moves.push_back(m);
        if (p.sideToMove() == White) parts << QStringLiteral("%1.").arg(p.fullmoveNumber());
        else if (i == 0) parts << QStringLiteral("%1…").arg(p.fullmoveNumber());
        parts << display::san(p.toSan(m));
        p.makeMove(m);
    }
    row.text = parts.join(' ');
    return row;
}

void EnginePanel::refreshRows()
{
    QList<EngineLinesView::Row> rows;
    for (auto it = m_lines.cbegin(); it != m_lines.cend(); ++it) {
        const EngineLinesView::Row row = EngineLinesView::rowFor(m_pos, it.value());
        rows << row;
        if (it.key() == 1) {
            m_eval->setText(EvalBar::format(row.eval));
            emit evaluationChanged(row.eval);
            emit bestMoveChanged(m_locked ? Move{} : row.firstMove);
        }
    }
    m_view->setRows(rows);
    m_view->setMinimumHeight(28 * std::max(2, int(rows.size())));
}
