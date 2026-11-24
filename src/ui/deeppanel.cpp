#include "deeppanel.h"
#include "userdirs.h"
#include "display.h"

#include "chess/polyglot.h"
#include "deepdialogs.h"
#include "engine/engineregistry.h"
#include "gamesession.h"
#include "theme.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>

using namespace chess;

namespace {
enum TreeColumn { ColMove, ColScore, ColEval, ColPositions, ColNote, TreeColumns };
enum NoteColumn { NoteLine, NoteAdded, NoteInitial, NoteCurrent, NoteText, NoteColumns };

const QColor Green(0x6a, 0xbf, 0x69);
const QColor Red(0xe0, 0x5c, 0x4f);
const QColor Yellow(0xe6, 0xc2, 0x4f);

QString noteListName(deep::NoteList l)
{
    switch (l) {
    case deep::NoteList::Good: return DeepPanel::tr("Bonne");
    case deep::NoteList::Interesting: return DeepPanel::tr("Intéressante");
    case deep::NoteList::Critical: return DeepPanel::tr("Critique");
    case deep::NoteList::Dubious: return DeepPanel::tr("Douteuse");
    default: return {};
    }
}

QString activityName(DeepAnalysis::Activity a)
{
    switch (a) {
    case DeepAnalysis::Activity::Analysing: return DeepPanel::tr("en analyse");
    case DeepAnalysis::Activity::Generating: return DeepPanel::tr("création des tâches");
    case DeepAnalysis::Activity::Minimizing: return DeepPanel::tr("minimax");
    case DeepAnalysis::Activity::Waiting: return DeepPanel::tr("en attente de tâches");
    default: return DeepPanel::tr("au repos");
    }
}

QString durationText(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s < 60) return DeepPanel::tr("%1 s").arg(s);
    if (s < 3600) return DeepPanel::tr("%1 min %2 s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    return DeepPanel::tr("%1 h %2 min").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0'));
}

QString sanLine(Position pos, const std::vector<Move>& moves, int max = 40)
{
    QStringList out;
    for (size_t i = 0; i < moves.size() && int(i) < max; ++i) {
        if (!pos.isLegal(moves[i])) break;
        QString san = display::san(pos.toSan(moves[i]));
        if (pos.sideToMove() == White) san = QStringLiteral("%1.%2").arg(pos.fullmoveNumber()).arg(san);
        else if (i == 0) san = QStringLiteral("%1…%2").arg(pos.fullmoveNumber()).arg(san);
        out << san;
        pos.makeMove(moves[i]);
    }
    return out.join(QLatin1Char(' '));
}

QToolButton* toolButton(const char* icon, const QString& tip)
{
    auto* b = new QToolButton;
    b->setIcon(theme::icon(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}
} // namespace

// ---------------------------------------------------------------- task squares

DeepTaskSquares::DeepTaskSquares(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMinimumHeight(24);
}

int DeepTaskSquares::columns() const { return std::max(1, (width() - 4) / 12); }

int DeepTaskSquares::heightForWidth(int w) const
{
    const int cols = std::max(1, (w - 4) / 12);
    const int rows = std::max(1, (int(m_squares.size()) + cols - 1) / cols);
    return std::min(rows, 30) * 12 + 4;
}

void DeepTaskSquares::setSquares(const QList<Square>& squares)
{
    m_squares = squares;
    updateGeometry();
    update();
}

void DeepTaskSquares::select(uint64_t id)
{
    if (m_selected == id) return;
    m_selected = id;
    update();
    emit selectedChanged(id);
}

void DeepTaskSquares::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const int cols = columns();
    for (int i = 0; i < m_squares.size(); ++i) {
        const Square& s = m_squares[i];
        const QRect r(2 + (i % cols) * 12, 2 + (i / cols) * 12, 10, 10);
        if (r.top() > height()) break;
        QColor c = theme::TextFaint;
        if (s.state == Running) c = Green;
        else if (s.state == Waiting) c = Yellow;
        else if (s.state == Manual) c = Red;
        p.fillRect(r, c);
        if (s.id && s.id == m_selected) {
            p.setPen(QPen(theme::Text, 2));
            p.drawRect(r.adjusted(-1, -1, 0, 0));
        }
    }
}

void DeepTaskSquares::mousePressEvent(QMouseEvent* e)
{
    const int cols = columns();
    const int col = (int(e->position().x()) - 2) / 12, row = (int(e->position().y()) - 2) / 12;
    const int i = row * cols + col;
    if (col >= 0 && col < cols && i >= 0 && i < m_squares.size() && m_squares[i].id) select(m_squares[i].id);
}

// ---------------------------------------------------------------- panel

DeepPanel::DeepPanel(DeepAnalysis* deep, GameSession* session, QWidget* parent)
    : QWidget(parent)
    , m_deep(deep)
    , m_session(session)
    , m_refresh(new QTimer(this))
{
    setObjectName("PanelBody");
    // Toolbar: default project, start / stop, engines, options, menu
    m_projectBox = new QComboBox;
    m_projectBox->setToolTip(tr("Projet affiché (projet par défaut : il reçoit les positions envoyées)"));
    m_run = toolButton("play", tr("Démarrer l'analyse approfondie"));
    auto* engines = toolButton("engine", tr("Moteurs…"));
    auto* options = toolButton("edit", tr("Options du projet…"));
    m_menu = toolButton("new", tr("Projet, exports…"));
    m_menu->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_menu);
    menu->addAction(theme::icon("new"), tr("Nouveau projet…"), this, &DeepPanel::newProject);
    menu->addAction(tr("Racines du projet…"), this, &DeepPanel::editRoots);
    menu->addSeparator();
    menu->addAction(tr("Ajouter des tâches (EPD)…"), this, [this] { addEpd(false); });
    menu->addAction(tr("Ajouter des résultats (EPD)…"), this, [this] { addEpd(true); });
    menu->addAction(tr("Minimax maintenant"), this, [this] { m_deep->minimaxNow(project()); });
    menu->addAction(tr("Fusionner dans l'arbre maître"), this, [this] {
        QString err;
        if (m_deep->mergeIntoMaster(project(), &err)) emit statusMessage(tr("Résultats ajoutés à l'arbre maître"));
        else QMessageBox::warning(this, tr("Arbre maître"), err);
    });
    menu->addSeparator();
    menu->addAction(theme::icon("save"), tr("Exporter en PGN…"), this, [this] { exportProject(0); });
    menu->addAction(tr("Exporter en arbre d'ouverture Hydra64…"), this, [this] { exportProject(1); });
    menu->addAction(tr("Exporter en livre Polyglot…"), this, [this] { exportProject(2); });
    m_menu->setMenu(menu);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 2);
    bar->setSpacing(2);
    bar->addWidget(m_projectBox, 1);
    bar->addWidget(m_run);
    bar->addWidget(engines);
    bar->addWidget(options);
    bar->addWidget(m_menu);

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildTreeTab(), tr("Arbre"));
    m_tabs->addTab(buildTasksTab(), tr("Tâches"));
    m_tabs->addTab(buildProjectsTab(), tr("Projets"));
    m_tabs->addTab(buildNotesTab(), tr("Notes"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bar);
    layout->addWidget(m_tabs, 1);

    connect(m_run, &QToolButton::clicked, this, &DeepPanel::toggleRunning);
    connect(engines, &QToolButton::clicked, this, &DeepPanel::editEngines);
    connect(options, &QToolButton::clicked, this, &DeepPanel::editProject);
    connect(m_projectBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_filling || i < 0) return;
        m_deep->setDefaultProject(m_projectBox->itemData(i).toInt());
        m_treeDirty = m_tasksDirty = true;
        scheduleRefresh();
    });
    m_refresh->setSingleShot(true);
    m_refresh->setInterval(250);
    connect(m_refresh, &QTimer::timeout, this, [this] {
        if (m_treeDirty) refreshTree();
        if (m_tasksDirty) refreshTasks();
        if (m_projectsDirty) {
            refreshProjects();
            refreshNotes();
        }
        m_treeDirty = m_tasksDirty = m_projectsDirty = false;
    });
    connect(m_deep, &DeepAnalysis::treeChanged, this, [this](int p) {
        if (p != project()) return;
        m_treeDirty = true;
        scheduleRefresh();
    });
    connect(m_deep, &DeepAnalysis::queueChanged, this, [this](int p) {
        if (p != project()) return;
        m_tasksDirty = true;
        scheduleRefresh();
    });
    connect(m_deep, &DeepAnalysis::taskProgress, this, [this](uint64_t id) {
        if (id == m_squares->selected() && m_tabs->currentIndex() == 1) refreshTaskDetails();
    });
    connect(m_deep, &DeepAnalysis::projectsChanged, this, [this] {
        m_projectsDirty = m_treeDirty = m_tasksDirty = true;
        refreshToolbar();
        scheduleRefresh();
    });
    connect(m_deep, &DeepAnalysis::runningChanged, this, [this] { refreshToolbar(); });
    connect(m_session, &GameSession::currentChanged, this, [this] { refreshTree(); });
    connect(m_session, &GameSession::gameReplaced, this, [this] { refreshTree(); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        refreshTasks();
        refreshProjects();
        refreshNotes();
    });
    refreshToolbar();
    refreshProjects();
    refreshTree();
    refreshTasks();
    refreshNotes();
}

void DeepPanel::scheduleRefresh()
{
    if (!m_refresh->isActive()) m_refresh->start();
}

void DeepPanel::refreshToolbar()
{
    m_filling = true;
    m_projectBox->clear();
    for (int i = 0; i < m_deep->count(); ++i) m_projectBox->addItem(m_deep->info(i).name, i);
    if (m_deep->count() == 0) m_projectBox->addItem(tr("Aucun projet : + Nouveau projet…"), -1);
    m_projectBox->setCurrentIndex(std::max(0, m_projectBox->findData(project())));
    m_filling = false;
    const bool running = m_deep->isRunning();
    m_run->setIcon(theme::icon(running ? "close" : "play"));
    m_run->setToolTip(running ? tr("Arrêter l'analyse approfondie") : tr("Démarrer l'analyse approfondie"));
}

// ---------------------------------------------------------------- Tree tab

QWidget* DeepPanel::buildTreeTab()
{
    auto* w = new QWidget;
    m_treeHeader = new QLabel;
    m_treeHeader->setObjectName("Faint");
    m_treeHeader->setWordWrap(true);
    m_tree = new QTableWidget(0, TreeColumns);
    m_tree->setHorizontalHeaderLabels({tr("Coup"), tr("IDeA"), tr("Éval"), tr("Positions"), tr("Note")});
    m_tree->verticalHeader()->hide();
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->horizontalHeader()->setStretchLastSection(true);
    m_tree->setColumnWidth(ColMove, 70);
    m_tree->setColumnWidth(ColScore, 62);
    m_tree->setColumnWidth(ColEval, 80);
    m_tree->setColumnWidth(ColPositions, 70);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setToolTip(tr("Double-clic : jouer le coup. Clic droit : marquer en vert (seul coup analysé) ou en rouge (exclu), notes."));
    connect(m_tree, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_treeMoves.size()) emit movePicked(m_treeMoves[row]);
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, &DeepPanel::treeMenu);

    // The seven task buttons of IDeA
    auto* current = new QPushButton(tr("Position"));
    current->setToolTip(tr("Analyser la position de l'échiquier (tâche prioritaire)"));
    auto* autoPlay = new QPushButton(tr("Auto-play"));
    autoPlay->setToolTip(tr("Le moteur joue la suite depuis la position, chaque coup gardé avec son évaluation"));
    m_autoPlies = new QSpinBox;
    m_autoPlies->setRange(1, 60);
    m_autoPlies->setValue(QSettings().value("deep/autoPlies", 10).toInt());
    m_autoPlies->setSuffix(tr(" coups"));
    auto* alternative = new QPushButton(tr("Alternative"));
    alternative->setToolTip(tr("Chercher un nouveau coup dans cette position"));
    auto* all = new QPushButton(tr("Toutes"));
    all->setToolTip(tr("Envoyer toutes les positions de la notation (ligne principale et variantes)"));
    auto* custom = new QPushButton(tr("Personnalisée…"));
    auto* infinite = new QPushButton(tr("Infinie"));
    infinite->setToolTip(tr("Analyse sans limite de la position (onglet Tâches : Finir)"));
    auto* root = new QPushButton(tr("Racine"));
    root->setToolTip(tr("Faire de cette position une racine du projet"));
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(4);
    for (QWidget* b : {static_cast<QWidget*>(current), static_cast<QWidget*>(alternative), static_cast<QWidget*>(all), static_cast<QWidget*>(root)})
        row1->addWidget(b);
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(4);
    row2->addWidget(autoPlay);
    row2->addWidget(m_autoPlies);
    row2->addWidget(custom);
    row2->addWidget(infinite);
    connect(current, &QPushButton::clicked, this, &DeepPanel::sendCurrentPosition);
    connect(alternative, &QPushButton::clicked, this, &DeepPanel::sendAlternative);
    connect(all, &QPushButton::clicked, this, [this] {
        const int n = sendAllPositions();
        emit statusMessage(tr("%n position(s) envoyée(s) à l'analyse approfondie", nullptr, n));
    });
    connect(root, &QPushButton::clicked, this, &DeepPanel::makeRoot);
    connect(autoPlay, &QPushButton::clicked, this, [this] {
        QSettings().setValue("deep/autoPlies", m_autoPlies->value());
        sendAutoPlay(m_autoPlies->value());
    });
    connect(custom, &QPushButton::clicked, this, [this] {
        DeepCustomTaskDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return;
        deep::Task t;
        if (!currentTask(t)) return;
        t.kind = dlg.alternative() ? deep::TaskKind::Alternative : deep::TaskKind::Evaluate;
        t.timeMs = std::max(1, dlg.timeMs());
        t.depth = dlg.depth();
        send(t);
    });
    connect(infinite, &QPushButton::clicked, this, [this, infinite] {
        const QList<DeepAnalysis::EngineSlot> engines = m_deep->engines();
        if (engines.size() == 1) {
            sendInfinite(0);
            return;
        }
        QMenu m(this);
        for (int s = 0; s < engines.size(); ++s) {
            const EngineConfig cfg = EngineRegistry::instance().engine(engines[s].engineId);
            m.addAction(cfg.name.isEmpty() ? cfg.idName : cfg.name, this, [this, s] { sendInfinite(s); });
        }
        m.exec(infinite->mapToGlobal(QPoint(0, infinite->height())));
    });

    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(m_treeHeader);
    layout->addWidget(m_tree, 1);
    layout->addLayout(row1);
    layout->addLayout(row2);
    return w;
}

void DeepPanel::refreshTree()
{
    m_treeMoves.clear();
    m_tree->setRowCount(0);
    const int p = project();
    if (p < 0) {
        m_treeHeader->setText(tr("Créez un projet (bouton du menu : Nouveau projet…) pour lancer une analyse approfondie de la position."));
        emit bestMoveChanged({});
        return;
    }
    const deep::Tree& tree = m_deep->project(p).tree;
    const Position pos = m_session->current()->pos;
    const uint32_t n = tree.find(pos);
    if (n == deep::NoNode) {
        m_treeHeader->setText(tr("Position absente de l'arbre de « %1 » (%2 positions). Bouton Position : l'analyser.")
                                  .arg(m_deep->info(p).name, QLocale().toString(qulonglong(tree.size()))));
        emit bestMoveChanged({});
        return;
    }
    const deep::Node& node = tree.node(n);
    QString head = tr("IDeA %1").arg(deepScoreText(node.score, pos.sideToMove()));
    if (node.eval.valid) head += tr(" · moteur %1 (prof. %2)").arg(deepScoreText(deep::toCp(node.eval), pos.sideToMove())).arg(node.eval.depth);
    head += tr(" · %1 positions").arg(QLocale().toString(qulonglong(node.positions)));
    if (node.alternatives) head += tr(" · %n alternative(s) cherchée(s)", nullptr, node.alternatives);
    if (node.note != deep::NoteList::None) head += QStringLiteral(" · ") + noteListName(node.note);
    if (!node.comment.empty()) head += QStringLiteral(" · « ") + QString::fromStdString(node.comment) + QStringLiteral(" »");
    m_treeHeader->setText(head);

    std::vector<const deep::Edge*> edges;
    for (const deep::Edge& e : node.edges) edges.push_back(&e);
    std::stable_sort(edges.begin(), edges.end(), [&](const deep::Edge* a, const deep::Edge* b) {
        const bool ra = a->mark == deep::Mark::Exclude, rb = b->mark == deep::Mark::Exclude;
        if (ra != rb) return rb;
        return tree.moveScore(*a) > tree.moveScore(*b);
    });
    const deep::Edge* best = tree.bestEdge(n);
    m_tree->setRowCount(int(edges.size()));
    for (int r = 0; r < int(edges.size()); ++r) {
        const deep::Edge& e = *edges[size_t(r)];
        const deep::Node& child = tree.node(e.child);
        m_treeMoves << e.move;
        auto set = [&](int col, const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* it = new QTableWidgetItem(text);
            it->setTextAlignment(align);
            if (e.mark == deep::Mark::Focus) it->setForeground(Green);
            else if (e.mark == deep::Mark::Exclude) it->setForeground(Red);
            m_tree->setItem(r, col, it);
        };
        QString move = display::san(pos.toSan(e.move));
        if (&e == best) move = QStringLiteral("▸ ") + move;
        set(ColMove, move);
        set(ColScore, deepScoreText(tree.moveScore(e), pos.sideToMove()), Qt::AlignRight | Qt::AlignVCenter);
        const Color after = pos.sideToMove() == White ? Black : White;
        set(ColEval, child.eval.valid ? QStringLiteral("%1 /%2").arg(deepScoreText(deep::toCp(child.eval), after)).arg(child.eval.depth) : QString(),
            Qt::AlignRight | Qt::AlignVCenter);
        set(ColPositions, QLocale().toString(qulonglong(child.positions)), Qt::AlignRight | Qt::AlignVCenter);
        QString note = noteListName(child.note);
        if (!child.comment.empty()) note += (note.isEmpty() ? QString() : QStringLiteral(" · ")) + QString::fromStdString(child.comment);
        if (e.mark == deep::Mark::Focus) note = tr("vert") + (note.isEmpty() ? QString() : QStringLiteral(" · ") + note);
        if (e.mark == deep::Mark::Exclude) note = tr("rouge (exclu)") + (note.isEmpty() ? QString() : QStringLiteral(" · ") + note);
        set(ColNote, note);
    }
    emit bestMoveChanged(best ? best->move : Move{});
}

void DeepPanel::treeMenu(const QPoint& at)
{
    const int row = m_tree->rowAt(at.y());
    QMenu m(this);
    if (row >= 0 && row < m_treeMoves.size()) {
        const Move mv = m_treeMoves[row];
        m.addAction(tr("Vert : seul coup analysé ici"), this, [this, mv] { setMark(mv, deep::Mark::Focus); });
        m.addAction(tr("Rouge : exclure de l'analyse"), this, [this, mv] { setMark(mv, deep::Mark::Exclude); });
        m.addAction(tr("Sans marque"), this, [this, mv] { setMark(mv, deep::Mark::None); });
        m.addSeparator();
        m.addAction(tr("Jouer le coup"), this, [this, mv] { emit movePicked(mv); });
    }
    auto* notes = m.addMenu(tr("Ajouter la position à la liste"));
    for (deep::NoteList l : {deep::NoteList::Good, deep::NoteList::Interesting, deep::NoteList::Critical, deep::NoteList::Dubious})
        notes->addAction(noteListName(l), this, [this, l] { addNote(l); });
    m.addAction(tr("Commentaire de la position…"), this, [this] {
        const int p = project();
        if (p < 0) return;
        deep::Tree& tree = m_deep->project(p).tree;
        const uint32_t n = tree.find(m_session->current()->pos);
        if (n == deep::NoNode) return;
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Commentaire"), tr("Commentaire de la position :"), QLineEdit::Normal,
                                                   QString::fromStdString(tree.node(n).comment), &ok);
        if (!ok) return;
        tree.node(n).comment = text.toStdString();
        m_deep->markDirty(p);
        refreshTree();
    });
    m.addSeparator();
    m.addAction(tr("Meilleure ligne dans la notation (F4)"), this, &DeepPanel::copyBestLine);
    m.exec(m_tree->viewport()->mapToGlobal(at));
}

void DeepPanel::setMark(Move move, deep::Mark mark)
{
    const int p = project();
    if (p < 0) return;
    deep::Tree& tree = m_deep->project(p).tree;
    const uint32_t n = tree.find(m_session->current()->pos);
    if (n == deep::NoNode) return;
    deep::Edge* e = tree.edge(n, move);
    if (!e) return;
    e->mark = mark;
    tree.propagate(n);
    m_deep->markDirty(p);
    refreshTree();
}

bool DeepPanel::currentTask(deep::Task& t)
{
    const int p = project();
    if (p < 0) {
        emit statusMessage(tr("Aucun projet d'analyse approfondie : créez-en un (Nouveau projet…)"));
        return false;
    }
    t = {};
    t.manual = true;
    return m_deep->pathTo(p, m_session->game(), m_session->current(), t.root, t.path);
}

uint64_t DeepPanel::send(deep::Task t)
{
    const uint64_t id = m_deep->addTask(project(), std::move(t));
    if (id && !m_deep->isRunning()) emit statusMessage(tr("Tâche ajoutée : elle sera analysée au démarrage de l'analyse approfondie"));
    return id;
}

bool DeepPanel::sendCurrentPosition()
{
    deep::Task t;
    if (!currentTask(t)) return false;
    return send(t) != 0;
}

bool DeepPanel::sendAlternative()
{
    deep::Task t;
    if (!currentTask(t)) return false;
    t.kind = deep::TaskKind::Alternative;
    return send(t) != 0;
}

bool DeepPanel::sendAutoPlay(int plies)
{
    deep::Task t;
    if (!currentTask(t)) return false;
    t.kind = deep::TaskKind::AutoPlay;
    t.plies = std::max(1, plies);
    return send(t) != 0;
}

bool DeepPanel::sendInfinite(int slot)
{
    deep::Task t;
    if (!currentTask(t)) return false;
    t.kind = deep::TaskKind::Infinite;
    t.engine = slot;
    return send(t) != 0;
}

int DeepPanel::sendAllPositions()
{
    const int p = project();
    if (p < 0) return 0;
    int count = 0;
    std::unordered_set<uint64_t> seen;
    std::function<void(const Node*)> visit = [&](const Node* n) {
        if (seen.insert(polyglot::key(n->pos)).second) {
            deep::Task t;
            t.manual = true;
            if (m_deep->pathTo(p, m_session->game(), n, t.root, t.path) && m_deep->addTask(p, t)) ++count;
        }
        for (const auto& c : n->children) visit(c.get());
    };
    visit(m_session->game().root());
    return count;
}

bool DeepPanel::makeRoot()
{
    const int p = project();
    if (p < 0) return false;
    deep::Tree& tree = m_deep->project(p).tree;
    const int r = tree.addRoot(m_session->current()->pos);
    tree.roots()[size_t(r)].enabled = true;
    m_deep->markDirty(p);
    emit statusMessage(tr("Racine ajoutée au projet « %1 »").arg(m_deep->info(p).name));
    return true;
}

void DeepPanel::copyBestLine()
{
    const int p = project();
    if (p < 0) return;
    const deep::Tree& tree = m_deep->project(p).tree;
    const uint32_t n = tree.find(m_session->current()->pos);
    if (n == deep::NoNode) return;
    const std::vector<Move> line = tree.bestLine(n);
    if (line.empty()) return;
    Node* from = m_session->current();
    m_session->editGame([&](Game& g) {
        Node* cur = from;
        for (const Move& m : line) {
            if (!cur->pos.isLegal(m)) break;
            cur = g.addMove(cur, m);
        }
    });
    emit statusMessage(tr("Meilleure ligne de l'arbre ajoutée à la notation (%n demi-coup(s))", nullptr, int(line.size())));
}

// ---------------------------------------------------------------- Tasks tab

QWidget* DeepPanel::buildTasksTab()
{
    auto* w = new QWidget;
    m_stage = new QLabel;
    m_stage->setWordWrap(true);
    m_squares = new DeepTaskSquares;
    m_taskDetails = new QLabel;
    m_taskDetails->setWordWrap(true);
    m_taskDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_taskDetails->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_insert = new QPushButton(tr("Insérer"));
    m_insert->setToolTip(tr("Ajouter la ligne du moteur à la notation"));
    m_finish = new QPushButton(tr("Finir"));
    m_finish->setToolTip(tr("Terminer tout de suite l'analyse de la tâche (son résultat est gardé)"));
    m_delete = new QPushButton(tr("Supprimer"));
    m_infinite = new QPushButton(tr("Infinie"));
    m_infinite->setToolTip(tr("Analyser cette tâche sans limite de temps"));
    auto* row = new QHBoxLayout;
    row->addWidget(m_insert);
    row->addWidget(m_finish);
    row->addWidget(m_delete);
    row->addWidget(m_infinite);
    row->addStretch(1);
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->addWidget(m_stage);
    layout->addWidget(m_squares);
    layout->addWidget(m_taskDetails, 1);
    layout->addLayout(row);
    connect(m_squares, &DeepTaskSquares::selectedChanged, this, &DeepPanel::refreshTaskDetails);
    connect(m_finish, &QPushButton::clicked, this, [this] { m_deep->finishTask(m_squares->selected()); });
    connect(m_delete, &QPushButton::clicked, this, [this] { m_deep->deleteTask(project(), m_squares->selected()); });
    connect(m_infinite, &QPushButton::clicked, this, [this] { m_deep->makeInfinite(project(), m_squares->selected()); });
    connect(m_insert, &QPushButton::clicked, this, [this] {
        // Task position and engine line added to the notation (from the root)
        const int p = project();
        for (const DeepAnalysis::Running& r : m_deep->running()) {
            if (r.task.id != m_squares->selected() || r.project != p) continue;
            const deep::Project& pr = m_deep->project(p);
            Position start;
            if (!start.setFen(pr.tree.roots()[size_t(r.task.root)].fen)) return;
            std::vector<Move> moves = r.task.path;
            Position end;
            pr.positionOf(r.task, end);
            for (const QString& u : r.line.pv) {
                const Move m = end.parseUci(u.toStdString());
                if (m.isNull() || !end.isLegal(m)) break;
                moves.push_back(m);
                end.makeMove(m);
            }
            Game g(start);
            Node* cur = g.root();
            for (const Move& m : moves) cur = g.addMove(cur, m);
            emit gameRequested(g);
            return;
        }
    });
    return w;
}

void DeepPanel::refreshTasks()
{
    const int p = project();
    if (p < 0) {
        m_stage->clear();
        m_squares->setSquares({});
        refreshTaskDetails();
        return;
    }
    const deep::Project& pr = m_deep->project(p);
    const DeepAnalysis::ProjectInfo& in = m_deep->info(p);
    QList<DeepTaskSquares::Square> squares;
    for (int i = 0; i < in.stageDone; ++i) squares << DeepTaskSquares::Square{DeepTaskSquares::Done, 0};
    int running = 0;
    for (const DeepAnalysis::Running& r : m_deep->running())
        if (r.project == p) {
            squares << DeepTaskSquares::Square{DeepTaskSquares::Running, r.task.id};
            ++running;
        }
    std::vector<const deep::Task*> queued;
    for (const deep::Task& t : pr.queue()) queued.push_back(&t);
    std::stable_sort(queued.begin(), queued.end(), [](const deep::Task* a, const deep::Task* b) { return a->priority < b->priority; });
    for (const deep::Task* t : queued)
        squares << DeepTaskSquares::Square{t->manual ? DeepTaskSquares::Manual : DeepTaskSquares::Waiting, t->id};
    m_squares->setSquares(squares);
    const QLocale loc;
    QString text = tr("<b>Étape %1</b> — %2 · <span style='color:%3'>%4 en cours</span> + %5 en attente")
                       .arg(pr.stage())
                       .arg(activityName(in.activity))
                       .arg(Green.name())
                       .arg(running)
                       .arg(loc.toString(qulonglong(pr.queue().size())));
    if (const int manual = pr.manualCount()) text += tr(" (<span style='color:%1'>%2 manuelle(s)</span>)").arg(Red.name()).arg(manual);
    text += tr(" · %1 faites").arg(in.stageDone);
    m_stage->setText(text);
    refreshTaskDetails();
}

void DeepPanel::refreshTaskDetails()
{
    const uint64_t id = m_squares->selected();
    const int p = project();
    bool isRunning = false, isQueued = false;
    QString text;
    if (p >= 0 && id) {
        const deep::Project& pr = m_deep->project(p);
        auto describe = [&](const deep::Task& t) {
            Position start;
            start.setFen(pr.tree.roots()[size_t(std::clamp(t.root, 0, int(pr.tree.roots().size()) - 1))].fen);
            QString kind;
            switch (t.kind) {
            case deep::TaskKind::Alternative: kind = tr("alternative"); break;
            case deep::TaskKind::AutoPlay: kind = tr("auto-play (%n coup(s))", nullptr, t.plies); break;
            case deep::TaskKind::Infinite: kind = tr("analyse infinie"); break;
            default: kind = tr("évaluation"); break;
            }
            QString line = sanLine(start, t.path);
            if (line.isEmpty()) line = tr("racine %1").arg(t.root + 1);
            return tr("<b>%1</b>%2 — %3").arg(kind, t.manual ? tr(" (manuelle)") : QString(), line.toHtmlEscaped());
        };
        for (const DeepAnalysis::Running& r : m_deep->running()) {
            if (r.task.id != id || r.project != p) continue;
            isRunning = true;
            text = describe(r.task);
            Position pos;
            if (pr.positionOf(r.task, pos)) {
                std::vector<Move> pv;
                Position walk = pos;
                for (const QString& u : r.line.pv) {
                    const Move m = walk.parseUci(u.toStdString());
                    if (m.isNull() || !walk.isLegal(m)) break;
                    pv.push_back(m);
                    walk.makeMove(m);
                }
                const int cp = r.line.mate ? (r.line.score > 0 ? deep::MateValue - (2 * r.line.score - 1) : -deep::MateValue + 2 * -r.line.score)
                                           : r.line.score;
                text += tr("<br>Analyse : %1 · [%2 / %3] %4<br><span style='color:gray'>%5</span>")
                            .arg(durationText(r.elapsedMs))
                            .arg(r.line.depth > 0 ? deepScoreText(cp, pos.sideToMove()) : QStringLiteral("…"))
                            .arg(r.line.depth)
                            .arg(sanLine(pos, pv, 16).toHtmlEscaped())
                            .arg(r.engineName.toHtmlEscaped());
            }
        }
        if (!isRunning)
            for (const deep::Task& t : pr.queue())
                if (t.id == id) {
                    isQueued = true;
                    text = describe(t) + tr("<br>En attente");
                }
    }
    if (text.isEmpty()) text = tr("Cliquez sur un carré pour voir sa tâche (vert : en cours, jaune : en attente, rouge : manuelle, gris : faite).");
    m_taskDetails->setText(text);
    m_insert->setEnabled(isRunning);
    m_finish->setEnabled(isRunning);
    m_delete->setEnabled(isRunning || isQueued);
    m_infinite->setEnabled(isRunning || isQueued);
}

// ---------------------------------------------------------------- Projects tab

QWidget* DeepPanel::buildProjectsTab()
{
    auto* w = new QWidget;
    m_projects = new QTableWidget(0, 5);
    m_projects->setHorizontalHeaderLabels({tr("Projet"), tr("État"), tr("Activité"), tr("Créé"), tr("Modifié")});
    m_projects->verticalHeader()->hide();
    m_projects->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_projects->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_projects->setSelectionMode(QAbstractItemView::SingleSelection);
    m_projects->horizontalHeader()->setStretchLastSection(true);
    m_projects->setColumnWidth(0, 150);
    auto* create = new QPushButton(theme::icon("new"), tr("Nouveau…"));
    auto* edit = new QPushButton(theme::icon("edit"), tr("Modifier…"));
    auto* remove = new QPushButton(theme::icon("trash"), tr("Supprimer"));
    auto* makeDefault = new QPushButton(tr("Par défaut"));
    auto* activate = new QPushButton(tr("Activer / désactiver"));
    auto* row = new QHBoxLayout;
    for (QPushButton* b : {create, edit, remove, makeDefault, activate}) row->addWidget(b);
    row->addStretch(1);
    m_projectStatus = new QLabel;
    m_projectStatus->setWordWrap(true);
    m_projectStatus->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->addWidget(m_projects, 1);
    layout->addLayout(row);
    layout->addWidget(m_projectStatus);
    auto selected = [this] {
        const int r = m_projects->currentRow();
        return r >= 0 && r < m_deep->count() ? r : -1;
    };
    connect(create, &QPushButton::clicked, this, &DeepPanel::newProject);
    connect(edit, &QPushButton::clicked, this, [this, selected] {
        if (const int r = selected(); r >= 0) {
            m_deep->setDefaultProject(r);
            editProject();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this, selected] {
        const int r = selected();
        if (r < 0) return;
        if (QMessageBox::question(this, tr("Supprimer le projet"), tr("Supprimer définitivement le projet « %1 » et son arbre ?").arg(m_deep->info(r).name))
            != QMessageBox::Yes)
            return;
        QString err;
        if (!m_deep->deleteProject(r, &err)) QMessageBox::warning(this, tr("Supprimer le projet"), err);
    });
    connect(makeDefault, &QPushButton::clicked, this, [this, selected] {
        if (const int r = selected(); r >= 0) m_deep->setDefaultProject(r);
    });
    connect(activate, &QPushButton::clicked, this, [this, selected] {
        const int r = selected();
        if (r < 0) return;
        const auto st = m_deep->info(r).status == DeepAnalysis::Status::Inactive ? DeepAnalysis::Status::Active : DeepAnalysis::Status::Inactive;
        m_deep->setStatus(r, st, m_deep->info(r).share);
    });
    connect(m_projects, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        m_deep->setDefaultProject(r);
        m_tabs->setCurrentIndex(0);
    });
    connect(m_projects, &QTableWidget::currentCellChanged, this, [this] { refreshProjects(); });
    return w;
}

void DeepPanel::refreshProjects()
{
    const QLocale loc;
    const int keep = m_projects->currentRow();
    {
        const QSignalBlocker block(m_projects);
        m_projects->setRowCount(m_deep->count());
        for (int i = 0; i < m_deep->count(); ++i) {
            const DeepAnalysis::ProjectInfo& in = m_deep->info(i);
            QString status = in.status == DeepAnalysis::Status::Inactive ? tr("inactif")
                             : in.status == DeepAnalysis::Status::Share ? tr("%1 % du CPU").arg(in.share)
                                                                        : tr("actif");
            const QStringList cells = {in.name + (i == m_deep->defaultProject() ? tr(" (par défaut)") : QString()), status, activityName(in.activity),
                                       loc.toString(in.created, QLocale::ShortFormat), loc.toString(in.modified, QLocale::ShortFormat)};
            for (int c = 0; c < cells.size(); ++c) m_projects->setItem(i, c, new QTableWidgetItem(cells[c]));
        }
        if (keep >= 0 && keep < m_deep->count()) m_projects->setCurrentCell(keep, 0);
        else if (m_deep->defaultProject() >= 0) m_projects->setCurrentCell(m_deep->defaultProject(), 0);
    }
    const int r = m_projects->currentRow();
    if (r < 0 || r >= m_deep->count()) {
        m_projectStatus->setText(tr("Aucun projet. « Nouveau… » crée un projet sur la position de l'échiquier."));
        return;
    }
    const DeepAnalysis::ProjectInfo& in = m_deep->info(r);
    const deep::Project& pr = m_deep->project(r);
    const int running = m_deep->runningCount(r);
    // Estimated remaining time: average time of a task × pending tasks / instances
    QString left;
    if (in.sessionTasks > 0 && running > 0) {
        const qint64 avg = in.sessionCpuMs / in.sessionTasks;
        left = tr(" · reste environ %1").arg(durationText(avg * qint64(pr.queue().size()) / std::max(1, running)));
    }
    QString text = tr("<b>%1</b> — %2 positions, %3 racine(s), étape %4<br>").arg(in.name.toHtmlEscaped())
                       .arg(loc.toString(qulonglong(pr.tree.size())))
                       .arg(pr.tree.roots().size())
                       .arg(pr.stage());
    text += tr("Tâches : <span style='color:%1'>%2 en cours</span> + %3 en attente").arg(Green.name()).arg(running).arg(loc.toString(qulonglong(pr.queue().size())));
    if (m_deep->isRunning()) text += tr(" · étape commencée depuis %1").arg(durationText(m_deep->clockMs() - in.stageStartMs)) + left;
    text += tr("<br>Session : %1 tâches, %2 de calcul · Total : %3 tâches, %4 de calcul")
                .arg(loc.toString(in.sessionTasks))
                .arg(durationText(in.sessionCpuMs))
                .arg(loc.toString(in.totalTasks))
                .arg(durationText(in.totalCpuMs));
    if (!in.master.isEmpty()) text += tr("<br>Arbre maître : %1").arg(QFileInfo(in.master).completeBaseName().toHtmlEscaped());
    text += QStringLiteral("<br><span style='color:gray'>%1</span>").arg(QDir::toNativeSeparators(in.path).toHtmlEscaped());
    m_projectStatus->setText(text);
}

void DeepPanel::newProject()
{
    const Position root = m_session->current()->pos;
    DeepProjectDialog dlg(m_deep, -1, root, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    const int i = m_deep->createProject(dlg.name(), root, dlg.options(), dlg.master(), &err);
    if (i < 0) {
        QMessageBox::warning(this, tr("Nouveau projet"), err);
        return;
    }
    m_deep->setStatus(i, dlg.status(), dlg.share());
    m_deep->setDefaultProject(i);
    emit statusMessage(tr("Projet « %1 » créé : Démarrer lance l'analyse").arg(m_deep->info(i).name));
}

void DeepPanel::editProject()
{
    const int p = project();
    if (p < 0) {
        newProject();
        return;
    }
    DeepProjectDialog dlg(m_deep, p, m_session->current()->pos, this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_deep->project(p).options = dlg.options();
    m_deep->info(p).name = dlg.name().isEmpty() ? m_deep->info(p).name : dlg.name();
    m_deep->info(p).master = dlg.master();
    m_deep->setStatus(p, dlg.status(), dlg.share());
    m_deep->saveProject(p);
    m_treeDirty = m_tasksDirty = true;
    scheduleRefresh();
}

void DeepPanel::editRoots()
{
    const int p = project();
    if (p < 0) return;
    DeepRootsDialog dlg(m_deep, p, m_session->current()->pos, this);
    dlg.exec();
    m_deep->saveProject(p);
    m_projectsDirty = m_treeDirty = true;
    scheduleRefresh();
}

void DeepPanel::editEngines()
{
    DeepEnginesDialog dlg(m_deep->engines(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_deep->setEngines(dlg.engineSlots());
    if (m_deep->isRunning()) emit statusMessage(tr("Moteurs enregistrés : pris en compte au prochain démarrage"));
}

void DeepPanel::toggleRunning()
{
    if (m_deep->isRunning()) {
        m_deep->stop();
        emit statusMessage(tr("Analyse approfondie arrêtée (tâches en attente gardées)"));
        return;
    }
    if (project() < 0) {
        newProject();
        if (project() < 0) return;
    }
    QString err;
    if (!m_deep->start(&err)) QMessageBox::warning(this, tr("Analyse approfondie"), err);
}

void DeepPanel::addEpd(bool results)
{
    const int p = project();
    if (p < 0) return;
    const QString path = QFileDialog::getOpenFileName(this, results ? tr("Résultats EPD") : tr("Positions EPD"), QSettings().value("lastDir").toString(),
                                                      tr("EPD (*.epd *.txt);;Tous les fichiers (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const int n = m_deep->project(p).addEpd(f.readAll().toStdString(), results);
    m_deep->markDirty(p);
    emit m_deep->queueChanged(p);
    emit m_deep->treeChanged(p);
    emit statusMessage(tr("%n position(s) EPD ajoutée(s)", nullptr, n));
}

void DeepPanel::exportProject(int kind)
{
    const int p = project();
    if (p < 0) return;
    static const char* filters[] = {QT_TR_NOOP("Parties PGN (*.pgn)"), QT_TR_NOOP("Arbre Hydra64 (*.hy64book)"), QT_TR_NOOP("Livre Polyglot (*.bin)")};
    static const char* exts[] = {".pgn", ".hy64book", ".bin"};
    const QString dir = kind == 0 ? userdirs::databases() : userdirs::openingBooks();
    QDir().mkpath(dir);
    QString path = QFileDialog::getSaveFileName(this, tr("Exporter le projet"), dir + QLatin1Char('/') + m_deep->info(p).name + QLatin1String(exts[kind]),
                                                tr(filters[kind]));
    if (path.isEmpty()) return;
    if (!path.endsWith(QLatin1String(exts[kind]))) path += QLatin1String(exts[kind]);
    QString err;
    const bool ok = kind == 0 ? m_deep->exportPgn(p, path, &err)
                              : m_deep->exportBook(p, path, kind == 1 ? treebook::TreeFormat::Hy64Book : treebook::TreeFormat::Polyglot, &err);
    if (ok) emit statusMessage(tr("Projet exporté : %1").arg(QDir::toNativeSeparators(path)));
    else QMessageBox::warning(this, tr("Exporter le projet"), err);
}

// ---------------------------------------------------------------- Notes tab

QWidget* DeepPanel::buildNotesTab()
{
    auto* w = new QWidget;
    m_noteList = new QComboBox;
    for (deep::NoteList l : {deep::NoteList::Good, deep::NoteList::Interesting, deep::NoteList::Critical, deep::NoteList::Dubious})
        m_noteList->addItem(noteListName(l), int(l));
    m_noteList->setCurrentIndex(2); // Critical
    m_noteNav = new QLabel;
    auto* prev = toolButton("prev", tr("Position précédente de la liste"));
    auto* next = toolButton("next", tr("Position suivante de la liste"));
    auto* top = new QHBoxLayout;
    top->addWidget(m_noteList, 1);
    top->addWidget(prev);
    top->addWidget(m_noteNav);
    top->addWidget(next);
    m_notes = new QTableWidget(0, NoteColumns);
    m_notes->setHorizontalHeaderLabels({tr("Position"), tr("Ajoutée"), tr("Score initial"), tr("Score actuel"), tr("Note")});
    m_notes->verticalHeader()->hide();
    m_notes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_notes->horizontalHeader()->setStretchLastSection(true);
    m_notes->setColumnWidth(NoteLine, 200);
    m_notes->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* addBoard = new QPushButton(theme::icon("board"), tr("Position de l'échiquier"));
    auto* remove = new QPushButton(theme::icon("trash"), tr("Retirer"));
    auto* removeAll = new QPushButton(tr("Tout retirer"));
    auto* row = new QHBoxLayout;
    row->addWidget(addBoard);
    row->addWidget(remove);
    row->addWidget(removeAll);
    row->addStretch(1);
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->addLayout(top);
    layout->addWidget(m_notes, 1);
    layout->addLayout(row);

    auto list = [this] { return deep::NoteList(m_noteList->currentData().toInt()); };
    connect(m_noteList, &QComboBox::currentIndexChanged, this, [this] {
        m_noteIndex = -1;
        refreshNotes();
    });
    auto step = [this](int d) {
        if (m_noteNodes.isEmpty()) return;
        m_noteIndex = (m_noteIndex + d + int(m_noteNodes.size())) % int(m_noteNodes.size());
        showNode(m_noteNodes[m_noteIndex]);
        refreshNotes();
    };
    connect(prev, &QToolButton::clicked, this, [step] { step(-1); });
    connect(next, &QToolButton::clicked, this, [step] { step(1); });
    connect(addBoard, &QPushButton::clicked, this, [this, list] { addNote(list()); });
    auto removeRows = [this](bool all) {
        const int p = project();
        if (p < 0) return;
        deep::Tree& tree = m_deep->project(p).tree;
        for (int i = 0; i < m_noteNodes.size(); ++i)
            if (all || m_notes->item(i, 0)->isSelected()) tree.node(m_noteNodes[i]).note = deep::NoteList::None;
        m_deep->markDirty(p);
        refreshNotes();
        refreshTree();
    };
    connect(remove, &QPushButton::clicked, this, [removeRows] { removeRows(false); });
    connect(removeAll, &QPushButton::clicked, this, [removeRows] { removeRows(true); });
    connect(m_notes, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) {
        if (c == NoteText || r < 0 || r >= m_noteNodes.size()) return;
        m_noteIndex = r;
        showNode(m_noteNodes[r]);
    });
    connect(m_notes, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (m_filling || it->column() != NoteText) return;
        const int p = project();
        if (p < 0 || it->row() >= m_noteNodes.size()) return;
        m_deep->project(p).tree.node(m_noteNodes[it->row()]).comment = it->text().toStdString();
        m_deep->markDirty(p);
    });
    connect(m_notes, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const int r = m_notes->rowAt(at.y());
        const int p = project();
        if (r < 0 || r >= m_noteNodes.size() || p < 0) return;
        QMenu m(this);
        auto* move = m.addMenu(tr("Déplacer vers"));
        for (deep::NoteList l : {deep::NoteList::Good, deep::NoteList::Interesting, deep::NoteList::Critical, deep::NoteList::Dubious})
            move->addAction(noteListName(l), this, [this, p, r, l] {
                m_deep->project(p).tree.node(m_noteNodes[r]).note = l;
                m_deep->markDirty(p);
                refreshNotes();
            });
        m.addAction(tr("Afficher la position"), this, [this, r] { showNode(m_noteNodes[r]); });
        m.exec(m_notes->viewport()->mapToGlobal(at));
    });
    return w;
}

void DeepPanel::addNote(deep::NoteList list, const QString& text)
{
    const int p = project();
    if (p < 0) return;
    deep::Tree& tree = m_deep->project(p).tree;
    deep::Task t;
    if (!currentTask(t)) return;
    Position start;
    start.setFen(tree.roots()[size_t(t.root)].fen);
    const uint32_t n = tree.ensurePath(start, t.path);
    deep::Node& node = tree.node(n);
    node.note = list;
    node.noteDate = QDateTime::currentSecsSinceEpoch();
    node.noteInitialScore = node.score;
    if (!text.isEmpty()) node.comment = text.toStdString();
    m_deep->markDirty(p);
    refreshNotes();
    refreshTree();
    emit statusMessage(tr("Position ajoutée à la liste « %1 »").arg(noteListName(list)));
}

bool DeepPanel::lineTo(uint32_t target, std::string& fen, std::vector<Move>& moves) const
{
    const int p = project();
    if (p < 0) return false;
    const deep::Tree& tree = m_deep->project(p).tree;
    // Walk up through the parents (breadth-first) to a root
    std::unordered_map<uint32_t, uint32_t> from{{target, deep::NoNode}};
    std::deque<uint32_t> queue{target};
    while (!queue.empty()) {
        const uint32_t n = queue.front();
        queue.pop_front();
        for (const deep::Root& r : tree.roots()) {
            if (r.node != n) continue;
            fen = r.fen;
            moves.clear();
            for (uint32_t cur = n; from[cur] != deep::NoNode; cur = from[cur]) {
                const uint32_t child = from[cur];
                for (const deep::Edge& e : tree.node(cur).edges)
                    if (e.child == child) {
                        moves.push_back(e.move);
                        break;
                    }
            }
            return true;
        }
        for (uint32_t parent : tree.node(n).parents)
            if (from.emplace(parent, n).second) queue.push_back(parent);
    }
    return false;
}

void DeepPanel::showNode(uint32_t node)
{
    std::string fen;
    std::vector<Move> moves;
    if (!lineTo(node, fen, moves)) return;
    Position start;
    if (!start.setFen(fen)) return;
    Game g(start);
    Node* cur = g.root();
    for (const Move& m : moves) {
        if (!cur->pos.isLegal(m)) break;
        cur = g.addMove(cur, m);
    }
    emit gameRequested(g);
}

void DeepPanel::refreshNotes()
{
    m_filling = true;
    m_noteNodes.clear();
    m_notes->setRowCount(0);
    const int p = project();
    if (p >= 0) {
        const deep::Tree& tree = m_deep->project(p).tree;
        const deep::NoteList list = deep::NoteList(m_noteList->currentData().toInt());
        for (uint32_t i = 0; i < tree.size(); ++i)
            if (tree.node(i).note == list) m_noteNodes << i;
        const QLocale loc;
        m_notes->setRowCount(int(m_noteNodes.size()));
        for (int r = 0; r < m_noteNodes.size(); ++r) {
            const deep::Node& n = tree.node(m_noteNodes[r]);
            std::string fen;
            std::vector<Move> moves;
            QString where;
            Color toMove = White;
            if (lineTo(m_noteNodes[r], fen, moves)) {
                Position s;
                s.setFen(fen);
                where = sanLine(s, moves, 30);
                Position e = s;
                for (const Move& m : moves) e.makeMove(m);
                toMove = e.sideToMove();
                if (where.isEmpty()) where = tr("racine");
            }
            auto ro = [](const QString& t) {
                auto* it = new QTableWidgetItem(t);
                it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                return it;
            };
            m_notes->setItem(r, NoteLine, ro(where));
            m_notes->setItem(r, NoteAdded, ro(loc.toString(QDateTime::fromSecsSinceEpoch(n.noteDate), QLocale::ShortFormat)));
            m_notes->setItem(r, NoteInitial, ro(deepScoreText(n.noteInitialScore, toMove)));
            m_notes->setItem(r, NoteCurrent, ro(deepScoreText(n.score, toMove)));
            m_notes->setItem(r, NoteText, new QTableWidgetItem(QString::fromStdString(n.comment)));
        }
        // Board position in the list: highlighted
        const uint32_t cur = tree.find(m_session->current()->pos);
        for (int r = 0; r < m_noteNodes.size(); ++r)
            if (m_noteNodes[r] == cur) m_notes->selectRow(r);
    }
    m_noteNav->setText(m_noteNodes.isEmpty() ? QStringLiteral("0/0") : QStringLiteral("%1/%2").arg(std::max(0, m_noteIndex) + 1).arg(m_noteNodes.size()));
    m_filling = false;
}
