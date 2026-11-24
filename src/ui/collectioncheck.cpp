#include "collectioncheck.h"
#include "display.h"

#include "autoevaluator.h"
#include "chess/position.h"
#include "engine/uciengine.h"
#include "theme.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

using namespace chess;

namespace collectioncheck {

int value(const Line& l)
{
    if (!l.mate) return l.score;
    return l.score > 0 ? 30000 - l.score : -30000 - l.score;
}

QString scoreText(const Line& l)
{
    if (l.mate) return l.score > 0 ? QObject::tr("mat en %1").arg(l.score) : QObject::tr("mat subi en %1").arg(-l.score);
    return QStringLiteral("%1%2").arg(l.score >= 0 ? QStringLiteral("+") : QString()).arg(l.score / 100.0, 0, 'f', 2);
}

namespace {
QString san(const TestPosition& p, const QString& uci)
{
    Position pos;
    if (!pos.setFen(p.fen.toStdString())) return uci;
    const Move m = pos.parseUci(uci.toStdString());
    return m.isNull() || !pos.isLegal(m) ? uci : display::san(pos.toSan(m));
}
} // namespace

bool needsSolutionSearch(const TestPosition& p, const QList<Line>& top)
{
    return p.mateIn == 0 && !p.best.isEmpty() && !top.isEmpty() && !p.best.contains(top.first().move);
}

Result judge(const TestPosition& p, const QList<Line>& top, const std::optional<Line>& solution)
{
    Result r;
    if (top.isEmpty()) {
        r.verdict = Verdict::Doubtful;
        r.detail = QObject::tr("le moteur n'a rien renvoyé");
        return r;
    }
    const Line& first = top.first();
    // Announced mate: it must be forced, and no longer than announced
    if (p.mateIn > 0) {
        if (!first.mate || first.score <= 0) {
            r.verdict = Verdict::MateNotForced;
            r.detail = QObject::tr("mat en %1 annoncé, le moteur ne trouve pas de mat (%2)").arg(p.mateIn).arg(scoreText(first));
        } else if (first.score > p.mateIn) {
            r.verdict = Verdict::MateNotForced;
            r.detail = QObject::tr("mat en %1 annoncé, le moteur trouve seulement un mat en %2").arg(p.mateIn).arg(first.score);
        } else if (first.score < p.mateIn) {
            r.verdict = Verdict::MateShorter;
            r.detail = QObject::tr("mat en %1 annoncé, il y a un mat en %2 (%3)").arg(p.mateIn).arg(first.score).arg(san(p, first.move));
        } else {
            r.detail = QObject::tr("mat en %1 confirmé").arg(p.mateIn);
        }
        return r;
    }
    if (p.avoid.contains(first.move)) {
        r.verdict = Verdict::Avoided;
        r.detail = QObject::tr("le moteur joue %1 (%2), donné comme coup à éviter").arg(san(p, first.move), scoreText(first));
        return r;
    }
    if (p.best.isEmpty()) {
        r.detail = QObject::tr("le moteur évite les coups à éviter (%1 %2)").arg(san(p, first.move), scoreText(first));
        return r;
    }
    const int v1 = value(first);
    if (p.best.contains(first.move)) {
        // The solution is the engine's choice: is another move worth almost as much?
        for (int i = 1; i < top.size(); ++i) {
            if (p.best.contains(top[i].move)) continue;
            const int v2 = value(top[i]);
            if (v2 >= v1 - AmbiguityMarginCp || (v1 >= WinningCp && v2 >= WinningCp)) {
                r.verdict = Verdict::Ambiguous;
                r.detail = QObject::tr("%1 (%2) vaut presque la solution %3 (%4)").arg(san(p, top[i].move), scoreText(top[i]), san(p, first.move), scoreText(first));
                return r;
            }
            break;
        }
        r.detail = QObject::tr("%1 confirmé (%2)").arg(san(p, first.move), scoreText(first));
        return r;
    }
    // The engine prefers another move: is the solution at least as good?
    if (solution) {
        const int vs = value(*solution);
        if (vs >= v1 - AmbiguityMarginCp || (v1 >= WinningCp && vs >= WinningCp)) {
            r.verdict = Verdict::Ambiguous;
            r.detail = QObject::tr("%1 (%2) vaut autant que la solution %3 (%4)").arg(san(p, first.move), scoreText(first), san(p, solution->move), scoreText(*solution));
        } else {
            r.verdict = Verdict::Doubtful;
            r.detail = QObject::tr("le moteur préfère %1 (%2) à la solution %3 (%4)").arg(san(p, first.move), scoreText(first), san(p, solution->move), scoreText(*solution));
        }
        return r;
    }
    r.verdict = Verdict::Doubtful;
    r.detail = QObject::tr("le moteur préfère %1 (%2)").arg(san(p, first.move), scoreText(first));
    return r;
}

QString verdictText(Verdict v)
{
    switch (v) {
    case Verdict::Ok: return QObject::tr("Correct");
    case Verdict::Ambiguous: return QObject::tr("Ambigu");
    case Verdict::Doubtful: return QObject::tr("Solution douteuse");
    case Verdict::Avoided: return QObject::tr("Coup à éviter joué");
    case Verdict::MateNotForced: return QObject::tr("Mat non forcé");
    case Verdict::MateShorter: return QObject::tr("Mat plus court");
    }
    return {};
}

} // namespace collectioncheck

using namespace collectioncheck;

// ---------------------------------------------------------------- dialog

CollectionCheckDialog::CollectionCheckDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Vérifier un recueil"));
    resize(860, 560);
    m_file = new QLineEdit;
    m_file->setPlaceholderText(tr("Recueil : EPD, PGN ou base Hydra64"));
    auto* browse = new QPushButton(theme::icon("open"), QString());
    m_time = new QSpinBox;
    m_time->setRange(100, 60000);
    m_time->setSingleStep(500);
    m_time->setValue(1000);
    m_time->setSuffix(tr(" ms par position"));
    m_startButton = new QPushButton(theme::icon("play"), tr("Vérifier"));
    m_startButton->setObjectName("Primary");
    m_summary = new QLabel(tr("Stockfish cherche chaque position et signale les solutions fausses, les exercices ambigus et les mats non forcés."));
    m_summary->setWordWrap(true);
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({tr("N°"), tr("Exercice"), tr("Résultat"), tr("Détail")});
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setToolTip(tr("Double-clic : ouvrir la position dans la page Analyse"));

    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(m_file, 1);
    fileRow->addWidget(browse);
    auto* form = new QFormLayout;
    form->addRow(tr("Recueil"), fileRow);
    form->addRow(tr("Temps"), m_time);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    buttons->addButton(m_startButton, QDialogButtonBox::ActionRole);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_summary);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Recueil"), QFileInfo(m_file->text()).absolutePath(),
                                                       tr("Recueils (*.hy64 *.hy64i *.epd *.pgn);;Tous les fichiers (*)"));
        if (!p.isEmpty()) m_file->setText(p);
    });
    connect(m_startButton, &QPushButton::clicked, this, [this] {
        if (m_running) {
            stop();
            return;
        }
        QString error;
        if (!start(m_file->text().trimmed(), m_time->value(), &error)) m_summary->setText(error);
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const int i = m_table->item(row, 0)->data(Qt::UserRole).toInt();
        if (i >= 0 && i < m_positions.size()) emit analyseRequested(m_positions[i].fen);
    });
}

CollectionCheckDialog::~CollectionCheckDialog()
{
    if (m_engine) m_engine->quit();
}

void CollectionCheckDialog::setCollection(const QString& path) { m_file->setText(path); }

bool CollectionCheckDialog::start(const QString& path, int moveTimeMs, QString* error)
{
    m_positions = solutiontest::load(path, error);
    if (m_positions.isEmpty()) return false;
    m_file->setText(path);
    m_moveTime = moveTimeMs;
    m_results.clear();
    m_table->setRowCount(0);
    m_index = -1;
    m_running = true;
    m_startButton->setText(tr("Arrêter"));
    m_startButton->setIcon(theme::icon("stop"));
    if (!m_engine) {
        const EngineConfig sf = AutoEvaluator::findStockfish();
        m_engine = new UciEngine(this);
        connect(m_engine, &UciEngine::started, this, [this] {
            m_engineReady = true;
            if (m_running && m_index < 0) searchNext();
        });
        connect(m_engine, &UciEngine::infoReceived, this, [this](const EngineLine& l) {
            if (l.pv.isEmpty() || l.multipv < 1 || l.multipv > 2) return;
            while (m_lines.size() < l.multipv) m_lines << Line{};
            m_lines[l.multipv - 1] = Line{l.pv.first(), l.score, l.mate};
        });
        connect(m_engine, &UciEngine::bestMoveReceived, this, [this](const QString&, const QString& fen) { onBestMove(fen); });
        if (!sf.isValid() || !m_engine->start(sf.path)) {
            if (error) *error = tr("Stockfish introuvable : la vérification demande le moteur embarqué.");
            m_running = false;
            return false;
        }
        m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
        m_engine->setOption(QStringLiteral("MultiPV"), QStringLiteral("2"));
        return true; // the search starts when the engine is ready
    }
    if (m_engineReady) searchNext();
    return true;
}

void CollectionCheckDialog::stop()
{
    if (!m_running) return;
    m_running = false;
    if (m_engine) m_engine->stop();
    finish(tr("Vérification arrêtée."));
}

void CollectionCheckDialog::searchNext()
{
    if (!m_running) return;
    if (++m_index >= m_positions.size()) {
        m_running = false;
        finish(QString());
        return;
    }
    m_restricted = false;
    m_lines.clear();
    m_top.clear();
    m_fen = m_positions[m_index].fen;
    m_summary->setText(tr("Position %1 / %2…").arg(m_index + 1).arg(m_positions.size()));
    m_engine->search(m_fen, QStringLiteral("movetime %1").arg(m_moveTime));
}

void CollectionCheckDialog::onBestMove(const QString& fen)
{
    if (!m_running || fen != m_fen || m_index < 0 || m_index >= m_positions.size()) return;
    const TestPosition& p = m_positions[m_index];
    if (!m_restricted) {
        m_top = m_lines;
        m_lines.clear();
        if (needsSolutionSearch(p, m_top)) {
            // The engine prefers another move: the best of the solutions is measured
            m_restricted = true;
            QStringList moves = p.best;
            m_engine->search(m_fen, QStringLiteral("movetime %1 searchmoves %2").arg(m_moveTime).arg(moves.join(QLatin1Char(' '))));
            return;
        }
        const Result r = judge(p, m_top, std::nullopt);
        m_results << r;
        addRow(m_index, r);
    } else {
        std::optional<Line> sol;
        if (!m_lines.isEmpty() && !m_lines.first().move.isEmpty()) sol = m_lines.first();
        const Result r = judge(p, m_top, sol);
        m_results << r;
        addRow(m_index, r);
    }
    searchNext();
}

void CollectionCheckDialog::addRow(int index, const Result& r)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto* number = new QTableWidgetItem(QString::number(index + 1));
    number->setData(Qt::UserRole, index);
    m_table->setItem(row, 0, number);
    m_table->setItem(row, 1, new QTableWidgetItem(m_positions[index].id));
    auto* verdict = new QTableWidgetItem(verdictText(r.verdict));
    const QColor good(92, 184, 126), warn(230, 138, 53), bad(224, 90, 80);
    verdict->setForeground(r.verdict == Verdict::Ok ? good : (r.verdict == Verdict::Ambiguous || r.verdict == Verdict::MateShorter) ? warn : bad);
    m_table->setItem(row, 2, verdict);
    m_table->setItem(row, 3, new QTableWidgetItem(r.detail));
    m_table->scrollToBottom();
}

void CollectionCheckDialog::finish(const QString& text)
{
    m_startButton->setText(tr("Vérifier"));
    m_startButton->setIcon(theme::icon("play"));
    int ok = 0;
    for (const Result& r : std::as_const(m_results)) ok += r.verdict == Verdict::Ok;
    const QString counts = tr("%1 position(s) vérifiée(s) : %2 correcte(s), %3 à revoir.").arg(m_results.size()).arg(ok).arg(m_results.size() - ok);
    m_summary->setText(text.isEmpty() ? counts : text + QLatin1Char(' ') + counts);
    emit finished();
}
