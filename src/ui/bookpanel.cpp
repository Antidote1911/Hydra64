#include "bookpanel.h"
#include "userdirs.h"
#include "conversion.h"
#include "display.h"
#include "gamesession.h"
#include "modulewidgets.h"
#include "preferences.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRandomGenerator>
#include <QTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <atomic>
#include <climits>
#include <thread>

using namespace chess;

namespace {

constexpr int MaxRecentBooks = 8;
const QString RecentKey = QStringLiteral("book/recent");  // last books opened (the first is the current one)
const QString CurrentKey = QStringLiteral("book/current"); // empty: no book open

const QColor RecommendedColor{0x5c, 0xb8, 0x7e}; // ChessBase "green" move
const QColor AvoidColor{0xe0, 0x5a, 0x50};       // "red" move


bool isTreeBookFile(const QString& path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("hy64book"), Qt::CaseInsensitive) == 0;
}


QString percent(double v, int decimals = 0)
{
    return QLocale().toString(v, 'f', decimals) + QStringLiteral(" %");
}

} // namespace

double BookMovesView::Row::score() const
{
    const int n = wins + draws + losses;
    return n > 0 ? 100.0 * (wins + 0.5 * draws) / n : -1.0;
}

// ---------------------------------------------------------------- BookMovesView

BookMovesView::BookMovesView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(RowHeight * 2);
}

void BookMovesView::setRows(const QList<Row>& rows, bool withStats)
{
    m_rows = rows;
    m_withStats = withStats;
    m_hover = -1;
    setCursor(Qt::ArrowCursor);
    setToolTip({});
    setMinimumHeight(RowHeight * std::max<int>(2, int(rows.size()))); // scrolling in the panel
    update();
}

void BookMovesView::setPlaceholder(const QString& text)
{
    m_placeholder = text;
    update();
}

void BookMovesView::paintEvent(QPaintEvent* e)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::Panel);

    if (m_rows.isEmpty()) {
        p.setPen(theme::TextFaint);
        p.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
        return;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        const QRectF r(0, i * RowHeight, width(), RowHeight);
        if (!r.intersects(e->rect())) continue;
        if (i % 2) p.fillRect(r, theme::PanelAlt);
        if (i == m_hover) p.fillRect(r, QColor(255, 255, 255, 12));
        if (m_withStats) paintStatsRow(p, m_rows[i], r, i == 0);
        else paintWeightRow(p, m_rows[i], r, i == 0);
    }
}

// Polyglot: move, bar of its share (weight) and percentage.
void BookMovesView::paintWeightRow(QPainter& p, const Row& row, const QRectF& r, bool first)
{
    QFont moveFont = font();
    moveFont.setWeight(QFont::DemiBold);
    p.setFont(moveFont);
    p.setPen(first ? theme::Text : theme::TextDim);
    p.drawText(QRectF(12, r.top(), 70, RowHeight), Qt::AlignVCenter | Qt::AlignLeft, row.san);

    QString pct;
    if (row.share < 0.001) pct = QStringLiteral("< ") + percent(0.1, 1);
    else pct = percent(row.share * 100, row.share >= 0.995 ? 0 : 1);
    QFont numFont = font();
    numFont.setPixelSize(11);
    p.setFont(numFont);
    const qreal textW = 52;
    const QRectF track(86, r.top() + RowHeight / 2.0 - 4, std::max<qreal>(20, width() - 86 - textW - 16), 8);
    QPainterPath tp;
    tp.addRoundedRect(track, 3, 3);
    p.fillPath(tp, theme::Window);
    if (row.share > 0) {
        QPainterPath fp;
        fp.addRoundedRect(QRectF(track.topLeft(), QSizeF(std::max<qreal>(3, track.width() * row.share), track.height())), 3, 3);
        p.fillPath(fp, first ? theme::Accent : theme::Accent.darker(160));
    }
    p.setPen(first ? theme::Text : theme::TextDim);
    p.drawText(QRectF(track.right() + 6, r.top(), textW + 4, RowHeight), Qt::AlignVCenter | Qt::AlignRight, pct);
}

// ChessBase: recommendation dot, move and annotation, games, bar
// White / draws / Black, score of the side playing.
void BookMovesView::paintStatsRow(QPainter& p, const Row& row, const QRectF& r, bool first)
{
    const qreal cy = r.center().y();
    if (row.recommendation == ctg::Recommended || row.recommendation == ctg::Avoid) {
        p.setPen(Qt::NoPen);
        p.setBrush(row.recommendation == ctg::Recommended ? RecommendedColor : AvoidColor);
        p.drawEllipse(QPointF(9, cy), 3, 3);
        p.setBrush(Qt::NoBrush);
    }

    QFont moveFont = font();
    moveFont.setWeight(QFont::DemiBold);
    p.setFont(moveFont);
    QColor moveColor = first ? theme::Text : theme::TextDim;
    if (row.recommendation == ctg::Avoid) moveColor = AvoidColor;
    p.setPen(moveColor);
    p.drawText(QRectF(18, r.top(), 76, RowHeight), Qt::AlignVCenter | Qt::AlignLeft, row.san + row.annotation);

    QFont numFont = font();
    numFont.setPixelSize(11);
    p.setFont(numFont);
    const QLocale locale;
    p.setPen(theme::TextDim);
    const QRectF gamesRect(94, r.top(), 64, RowHeight);
    if (row.hasEval && !row.hasStats) {
        // Deep analysis tree: evaluation (White's point of view) and depth
        p.setPen(first ? theme::Text : theme::TextDim);
        p.drawText(gamesRect, Qt::AlignVCenter | Qt::AlignRight, evalText(row));
        p.setPen(theme::TextFaint);
        p.drawText(QRectF(gamesRect.right() + 10, r.top(), 80, RowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                   row.evalDepth > 0 ? tr("prof. %1").arg(row.evalDepth) : QString());
        return;
    }
    p.drawText(gamesRect, Qt::AlignVCenter | Qt::AlignRight, row.games > 0 ? locale.toString(row.games) : QStringLiteral("—"));

    const qreal scoreW = 46;
    const QRectF bar(gamesRect.right() + 10, cy - 6, std::max<qreal>(24, width() - gamesRect.right() - 10 - scoreW - 14), 12);
    const int n = row.wins + row.draws + row.losses;
    QPainterPath clip;
    clip.addRoundedRect(bar, 3, 3);
    if (n <= 0) {
        p.fillPath(clip, theme::Window);
    } else {
        // Always White on the left, Black on the right (like ChessBase and Lichess)
        const int white = row.whiteMoves ? row.wins : row.losses, black = row.whiteMoves ? row.losses : row.wins;
        const qreal wW = bar.width() * white / n, dW = bar.width() * row.draws / n, bW = bar.width() - wW - dW;
        p.save();
        p.setClipPath(clip);
        const QRectF wr(bar.left(), bar.top(), wW, bar.height()), dr(wr.right(), bar.top(), dW, bar.height()),
            br(dr.right(), bar.top(), bW, bar.height());
        p.fillRect(wr, QColor(0xe8, 0xe6, 0xe0));
        p.fillRect(dr, QColor(0x80, 0x80, 0x88));
        p.fillRect(br, QColor(0x0f, 0x0f, 0x11));
        QFont barFont = numFont;
        barFont.setPixelSize(9);
        p.setFont(barFont);
        const QFontMetrics fm(barFont);
        auto label = [&](const QRectF& seg, int count, const QColor& c) {
            const QString t = percent(100.0 * count / n);
            if (fm.horizontalAdvance(t) + 4 > seg.width()) return;
            p.setPen(c);
            p.drawText(seg, Qt::AlignCenter, t);
        };
        label(wr, white, QColor(0x1a, 0x1a, 0x1c));
        label(dr, row.draws, QColor(0xf0, 0xf0, 0xf0));
        label(br, black, theme::Text);
        p.restore();
        p.setPen(QPen(theme::BorderSoft, 1));
        p.drawPath(clip);
        p.setFont(numFont);
    }
    p.setPen(first ? theme::Text : theme::TextDim);
    const double s = row.score();
    p.drawText(QRectF(bar.right() + 4, r.top(), scoreW + 6, RowHeight), Qt::AlignVCenter | Qt::AlignRight, s < 0 ? QStringLiteral("—") : percent(s));
}

QString BookMovesView::evalText(const Row& row)
{
    const int v = row.whiteMoves ? row.eval : -row.eval; // White's point of view
    if (std::abs(v) > 15000) {
        const int moves = (30000 - std::abs(v) + 1) / 2;
        return v > 0 ? QStringLiteral("#%1").arg(moves) : QStringLiteral("#-%1").arg(moves);
    }
    return QString::asprintf("%+.2f", v / 100.0);
}

QString BookMovesView::toolTipFor(const Row& row) const
{
    if (!m_withStats)
        return tr("%1 — poids %2\nDouble-clic : jouer le coup").arg(row.san).arg(row.weight);
    const QLocale locale;
    QStringList lines;
    lines << row.san + row.annotation;
    if (row.recommendation == ctg::Recommended) lines << tr("Coup recommandé");
    else if (row.recommendation == ctg::Avoid) lines << tr("Coup à éviter");
    if (row.hasEval) lines << tr("Évaluation : %1 (profondeur %2)").arg(evalText(row)).arg(row.evalDepth);
    if (row.hasEval && !row.hasStats) {
        // analysis tree: no games
    } else if (row.hasStats) {
        lines << tr("%1 parties : +%2 =%3 −%4").arg(locale.toString(row.games)).arg(row.wins).arg(row.draws).arg(row.losses);
        if (row.score() >= 0) lines << tr("Score : %1").arg(percent(row.score(), 1));
        if (row.averageElo > 0) lines << tr("Elo moyen : %1").arg(row.averageElo);
        if (row.performance > 0) lines << tr("Performance : %1").arg(row.performance);
        if (row.maxElo > 0) lines << tr("Elo maximum : %1").arg(row.maxElo);
        if (row.year > 0) lines << tr("Dernière partie : %1").arg(row.year);
    } else {
        lines << tr("Position suivante absente de l'arbre");
    }
    lines << tr("Double-clic : jouer le coup");
    return lines.join('\n');
}

void BookMovesView::mouseMoveEvent(QMouseEvent* e)
{
    int h = int(e->position().y()) / RowHeight;
    if (h >= m_rows.size()) h = -1;
    if (h != m_hover) {
        m_hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(h >= 0 ? toolTipFor(m_rows[h]) : QString());
        update();
    }
}

void BookMovesView::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int i = int(e->position().y()) / RowHeight;
    if (i >= 0 && i < m_rows.size()) emit movePicked(m_rows[i].move);
}

void BookMovesView::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- BookPanel

BookPanel::BookPanel(GameSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    auto* bar = new QWidget;
    bar->setObjectName("PanelBody");
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 6, 8, 6);
    barLayout->setSpacing(4);

    m_name = module::nameButton(tr("Arbre d'ouverture — clic : changer d'arbre"));
    m_name->setPopupMode(QToolButton::InstantPopup);
    m_name->setMenu(new QMenu(m_name));
    connect(m_name->menu(), &QMenu::aboutToShow, this, &BookPanel::fillMenu);
    m_open = module::button("open", {}, tr("Ouvrir un arbre d'ouverture (Hydra64 .hy64book, Polyglot .bin)…"));
    m_info = module::stat();

    barLayout->addWidget(m_name, 1);
    barLayout->addWidget(m_info);
    barLayout->addWidget(m_open);

    m_view = new BookMovesView;
    auto* scroll = new QScrollArea;
    scroll->setWidget(m_view);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(scroll, 1);

    connect(m_open, &QToolButton::clicked, this, &BookPanel::chooseBook);
    connect(m_view, &BookMovesView::movePicked, this, &BookPanel::movePicked);
    connect(m_session, &GameSession::currentChanged, this, &BookPanel::refresh);
    connect(m_session, &GameSession::gameReplaced, this, &BookPanel::refresh);
    connect(m_session, &GameSession::slotSwitched, this, &BookPanel::refresh);
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, &BookPanel::refresh); // move notation

    // Last book opened
    const QString last = QSettings().value(CurrentKey).toString();
    if (!last.isEmpty() && !openBook(last)) QSettings().setValue(CurrentKey, QString());
    updateHeader();
    refresh();
}

QStringList BookPanel::recentBooks()
{
    // The old proprietary books (ChessBase, ChessOK) no longer open directly.
    QStringList list = QSettings().value(RecentKey).toStringList();
    list.removeIf([](const QString& p) { return conversion::isProprietary(p); });
    return list;
}

bool BookPanel::openBook(const QString& path, QString* error)
{
    const QString fileName = QFileInfo(path).fileName();
    auto fail = [&](const QString& reason) {
        const QString msg = tr("Arbre d'ouverture « %1 » non ouvert : %2").arg(fileName, reason);
        if (error) *error = msg;
        emit statusMessage(msg);
        return false;
    };
    // The previous tree stays open if this one is refused.
    if (conversion::isProprietary(path) || treebook::looksLikeAquariumExport(QFile::encodeName(path).toStdString()))
        return fail(tr("format %1 : à convertir d'abord (Fichier → Convertir…)").arg(conversion::formatName(path)));
    if (isTreeBookFile(path)) {
        treebook::Book book;
        if (!book.open(QFile::encodeName(path).toStdString()))
            return fail(QFileInfo(path).isReadable() ? tr("ce n'est pas un arbre Hydra64 (.hy64book)") : tr("fichier illisible"));
        m_tree = std::move(book);
        m_polyglot.close();
        m_format = Tree;
        m_path = QFileInfo(path).absoluteFilePath();
    } else {
        polyglot::Book book;
        if (!book.open(QFile::encodeName(path).toStdString()))
            return fail(QFileInfo(path).isReadable() ? tr("ce n'est pas un arbre Polyglot (.bin)") : tr("fichier illisible"));
        m_polyglot = std::move(book);
        m_tree.close();
        m_format = Polyglot;
        m_path = QFileInfo(path).absoluteFilePath();
    }
    remember(m_path);
    updateHeader();
    refresh();
    return true;
}

void BookPanel::remember(const QString& path)
{
    QSettings s;
    s.setValue(CurrentKey, path);
    QStringList recent = s.value(RecentKey).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > MaxRecentBooks) recent.removeLast();
    s.setValue(RecentKey, recent);
}

void BookPanel::setDatabaseTree(const QString& name, TreeProvider provider)
{
    m_dbName = name;
    m_dbTree = std::move(provider);
    if (m_format == Database) {
        if (!m_dbTree) closeBook();
        else {
            updateHeader();
            refresh();
        }
    }
}

void BookPanel::showDatabaseTree()
{
    if (!m_dbTree) return;
    m_polyglot.close();
    m_tree.close();
    m_format = Database;
    m_path.clear();
    updateHeader();
    refresh();
}

void BookPanel::closeBook()
{
    m_polyglot.close();
    m_tree.close();
    m_format = NoBook;
    m_path.clear();
    QSettings().setValue(CurrentKey, QString());
    updateHeader();
    refresh();
}

void BookPanel::chooseBook()
{
    // The user's opening book folder first, then wherever they went last time
    const QString path = QFileDialog::getOpenFileName(this, tr("Ouvrir un arbre d'ouverture"), userdirs::startDir(userdirs::Kind::OpeningBook),
                                                      tr("Arbres d'ouverture (*.hy64book *.bin);;Hydra64 (*.hy64book);;Polyglot (*.bin);;Tous les fichiers (*)"));
    if (path.isEmpty()) return;
    userdirs::remember(userdirs::Kind::OpeningBook, path);
    // ChessBase, ChessOK / Aquarium (or their export): offered for conversion
    if (conversion::isProprietary(path) || treebook::looksLikeAquariumExport(QFile::encodeName(path).toStdString())) {
        emit convertRequested(path);
        return;
    }
    openBook(path);
}




void BookPanel::fillMenu()
{
    QMenu* menu = m_name->menu();
    menu->clear();
    for (const QString& path : recentBooks()) {
        QAction* a = menu->addAction(QFileInfo(path).fileName());
        a->setToolTip(path);
        a->setCheckable(true);
        a->setChecked(path == m_path);
        connect(a, &QAction::triggered, this, [this, path] {
            if (path != m_path) openBook(path);
        });
    }
    if (m_dbTree) {
        if (!menu->isEmpty()) menu->addSeparator();
        QAction* a = menu->addAction(theme::icon("database"), tr("Arbre de la base « %1 »").arg(m_dbName));
        a->setCheckable(true);
        a->setChecked(m_format == Database);
        connect(a, &QAction::triggered, this, &BookPanel::showDatabaseTree);
    }
    if (!menu->isEmpty()) menu->addSeparator();
    connect(menu->addAction(theme::icon("open"), tr("Ouvrir un arbre d'ouverture…")), &QAction::triggered, this, &BookPanel::chooseBook);
    QAction* close = menu->addAction(tr("Fermer l'arbre d'ouverture"));
    close->setEnabled(m_format != NoBook);
    connect(close, &QAction::triggered, this, &BookPanel::closeBook);
    if (!recentBooks().isEmpty()) {
        connect(menu->addAction(tr("Vider la liste")), &QAction::triggered, this, [] {
            QSettings s;
            const QString current = s.value(CurrentKey).toString();
            s.setValue(RecentKey, current.isEmpty() ? QStringList() : QStringList{current});
        });
    }
}

void BookPanel::updateHeader()
{
    const QString path = QDir::toNativeSeparators(m_path);
    switch (m_format) {
    case Polyglot:
        m_name->setText(QFileInfo(m_path).completeBaseName());
        m_name->setToolTip(tr("%1\nPolyglot, %2 entrées — clic : changer d'arbre")
                               .arg(path, QLocale().toString(qulonglong(m_polyglot.entryCount()))));
        break;
    case Tree:
        m_name->setText(QFileInfo(m_path).completeBaseName());
        m_name->setToolTip(tr("%1\nArbre Aquarium, %2 coups — clic : changer d'arbre")
                               .arg(path, QLocale().toString(qulonglong(m_tree.entryCount()))));
        break;
    case Database:
        m_name->setText(tr("Base : %1").arg(m_dbName));
        m_name->setToolTip(tr("Arbre de la base « %1 » (parties de la page Bases) — clic : changer d'arbre").arg(m_dbName));
        break;
    case NoBook:
        m_name->setText(tr("Aucun arbre d'ouverture"));
        m_name->setToolTip(tr("Arbre d'ouverture — clic : changer d'arbre"));
        break;
    }
}

void BookPanel::refresh()
{
    m_rows.clear();
    m_info->setToolTip({});
    if (m_format == NoBook) {
        m_info->setText(QStringLiteral("—"));
        m_view->setPlaceholder(tr("Aucun arbre d'ouverture ouvert.\nOuvrez un arbre Hydra64 (.hy64book) ou Polyglot (.bin) ;\nles livres ChessBase et ChessOK se convertissent (Fichier → Convertir…)."));
        m_view->setRows(m_rows);
        return;
    }
    const Position& pos = m_session->current()->pos;
    const bool whiteMoves = pos.sideToMove() == White;

    if (m_format == Polyglot) {
        const auto moves = m_polyglot.moves(pos);
        double total = 0;
        for (const auto& bm : moves) total += bm.weight;
        for (const auto& bm : moves) {
            BookMovesView::Row row;
            row.move = bm.move;
            row.san = display::san(pos.toSan(bm.move));
            row.weight = bm.weight;
            row.whiteMoves = whiteMoves;
            row.share = total > 0 ? bm.weight / total : 1.0 / double(moves.size());
            m_rows << row;
        }
        m_info->setText(moves.empty() ? tr("hors de l'arbre") : moves.size() == 1 ? tr("1 coup") : tr("%1 coups").arg(moves.size()));
    } else if (m_format == Tree || m_format == Database) {
        const auto moves = m_format == Tree ? m_tree.moves(pos) : m_dbTree ? m_dbTree(pos) : std::vector<chess::treebook::BookMove>();
        double total = 0;
        for (const auto& bm : moves) total += double(bm.games());
        for (const auto& bm : moves) {
            BookMovesView::Row row;
            row.move = bm.move;
            row.san = display::san(pos.toSan(bm.move));
            row.whiteMoves = whiteMoves;
            row.hasStats = bm.hasStats;
            row.games = int(std::min<uint64_t>(bm.games(), INT_MAX));
            row.wins = int(bm.wins);
            row.draws = int(bm.draws);
            row.losses = int(bm.losses);
            row.maxElo = bm.maxElo;
            row.year = bm.year;
            // Coming from a ChessBase book: annotation, mark, average Elo, performance
            row.annotation = QString::fromStdString(ctg::annotationSymbol(bm.annotation));
            row.recommendation = bm.recommendation;
            if (row.recommendation == ctg::Recommended
                && (bm.annotation == ctg::Mistake || bm.annotation == ctg::Blunder || bm.annotation == ctg::Dubious || bm.annotation == ctg::Interesting))
                row.recommendation = ctg::Neutral; // like ChessBase
            row.averageElo = bm.averageElo;
            row.performance = bm.performance;
            row.hasEval = bm.hasEval;
            row.eval = bm.eval;
            row.evalDepth = bm.evalDepth;
            row.share = total > 0 ? double(bm.games()) / total : 0;
            m_rows << row;
        }
        const bool analysis = !moves.empty() && std::all_of(moves.begin(), moves.end(), [](const auto& m) { return m.hasEval && !m.hasStats; });
        if (moves.empty()) m_info->setText(tr("hors de l'arbre"));
        else if (analysis) m_info->setText(tr("%n coup(s) analysé(s)", nullptr, int(moves.size())));
        else m_info->setText(tr("%1 parties").arg(QLocale().toString(qulonglong(total))));
    }
    m_view->setPlaceholder(tr("Position hors de l'arbre."));
    m_view->setRows(m_rows, m_format == Tree || m_format == Database);
}

std::vector<Move> BookPanel::movesAt(const Position& pos)
{
    std::vector<Move> out;
    switch (m_format) {
    case Polyglot:
        for (const auto& bm : m_polyglot.moves(pos)) out.push_back(bm.move);
        break;
    case Tree:
        for (const auto& bm : m_tree.moves(pos)) out.push_back(bm.move);
        break;
    case Database:
        if (m_dbTree)
            for (const auto& bm : m_dbTree(pos)) out.push_back(bm.move);
        break;
    case NoBook:
        break;
    }
    return out;
}

QString BookPanel::bookName() const
{
    if (m_format == NoBook) return {};
    if (m_format == Database) return tr("arbre de la base");
    return QFileInfo(m_path).completeBaseName();
}

Move BookPanel::pickMove(const Position& pos)
{
    std::vector<std::pair<Move, double>> choices;
    switch (m_format) {
    case Polyglot:
        for (const auto& bm : m_polyglot.moves(pos)) choices.push_back({bm.move, double(bm.weight)});
        break;
    case Tree:
        for (const auto& bm : m_tree.moves(pos)) {
            if (bm.recommendation == ctg::Avoid || bm.annotation == ctg::Mistake || bm.annotation == ctg::Blunder) continue;
            choices.push_back({bm.move, double(bm.games())});
        }
        break;
    case Database:
        if (m_dbTree)
            for (const auto& bm : m_dbTree(pos)) choices.push_back({bm.move, double(bm.games())});
        break;
    case NoBook:
        break;
    }
    double total = 0;
    for (const auto& c : choices) total += c.second;
    if (choices.empty()) return {};
    if (total <= 0) return choices[size_t(QRandomGenerator::global()->bounded(int(choices.size())))].first;
    double r = QRandomGenerator::global()->generateDouble() * total;
    for (const auto& c : choices) {
        r -= c.second;
        if (r < 0) return c.first;
    }
    return choices.back().first;
}
