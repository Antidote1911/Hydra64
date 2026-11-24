#include "calcvar.h"
#include "display.h"

#include "autoevaluator.h"
#include "boardwidget.h"
#include "engine/uciengine.h"
#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

using namespace chess;

namespace {
QString sanLine(Position pos, const std::vector<Move>& moves)
{
    QStringList out;
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i == 0 || pos.sideToMove() == White)
            out << (pos.sideToMove() == White ? QStringLiteral("%1.").arg(pos.fullmoveNumber()) : QStringLiteral("%1...").arg(pos.fullmoveNumber()));
        out << display::san(pos.toSan(moves[i]));
        pos.makeMove(moves[i]);
    }
    return out.join(QLatin1Char(' '));
}
Position after(Position pos, const std::vector<Move>& moves)
{
    for (const Move& m : moves) pos.makeMove(m);
    return pos;
}
} // namespace

QList<QPair<QString, int>> CalcVarDialog::evalChoices()
{
    return {{tr("+− (les Blancs gagnent)"), 300}, {tr("± (net avantage blanc)"), 150}, {tr("⩲ (léger avantage blanc)"), 50},
            {tr("= (égalité)"), 0},              {tr("⩱ (léger avantage noir)"), -50}, {tr("∓ (net avantage noir)"), -150},
            {tr("−+ (les Noirs gagnent)"), -300}};
}

QString CalcVarDialog::evalText(int cp, bool mate)
{
    if (mate) return cp >= 0 ? QStringLiteral("#%1").arg(cp) : QStringLiteral("#−%1").arg(-cp);
    return (cp >= 0 ? QStringLiteral("+") : QStringLiteral("−")) + QString::number(std::abs(cp) / 100.0, 'f', 2);
}

CalcVarDialog::CalcVarDialog(const Position& start, QWidget* parent)
    : QDialog(parent)
    , m_start(start)
    , m_pos(start)
    , m_lines(new QList<EngineLine>)
{
    setWindowTitle(tr("Calcul de variantes"));
    resize(980, 640);
    m_board = new BoardWidget;
    m_board->setMinimumSize(400, 400);
    m_line = new QLabel;
    m_line->setWordWrap(true);
    m_eval = new QComboBox;
    for (const auto& [text, cp] : evalChoices()) m_eval->addItem(text, cp);
    m_eval->setCurrentIndex(3);
    m_save = new QPushButton(theme::icon("plus"), tr("Enregistrer la variante"));
    m_reset = new QPushButton(tr("Recommencer la variante"));
    m_judge = new QPushButton(theme::icon("analysis"), tr("Terminer : le moteur juge"));
    m_judge->setObjectName("Primary");
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({tr("Variante"), tr("Votre éval."), tr("Moteur"), tr("Écart")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_result = new QLabel;
    m_result->setWordWrap(true);
    auto* help = new QLabel(tr("Calculez sans bouger les pièces dans votre tête… puis jouez chaque variante sur l'échiquier, "
                               "évaluez sa position finale et enregistrez-la. Après la dernière, le moteur juge."));
    help->setWordWrap(true);
    help->setObjectName("Faint");
    auto* evalRow = new QHBoxLayout;
    evalRow->addWidget(new QLabel(tr("Évaluation finale")));
    evalRow->addWidget(m_eval, 1);
    auto* actions = new QHBoxLayout;
    actions->addWidget(m_save);
    actions->addWidget(m_reset);
    actions->addStretch(1);
    auto* right = new QVBoxLayout;
    right->addWidget(help);
    right->addWidget(m_line);
    right->addLayout(evalRow);
    right->addLayout(actions);
    right->addWidget(m_table, 1);
    right->addWidget(m_judge);
    right->addWidget(m_result);
    auto* body = new QHBoxLayout;
    body->addWidget(m_board, 3);
    body->addLayout(right, 2);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);
    connect(m_board, &BoardWidget::moveRequested, this, &CalcVarDialog::playMove);
    connect(m_save, &QPushButton::clicked, this, [this] { finishVariation(m_eval->currentData().toInt()); });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        m_current.clear();
        m_pos = m_start;
        showBoard();
    });
    connect(m_judge, &QPushButton::clicked, this, [this] { judge(); });
    m_board->setFlipped(m_start.sideToMove() == Black);
    showBoard();
    refreshTable();
}

CalcVarDialog::~CalcVarDialog()
{
    if (m_engine) {
        m_engine->disconnect(this);
        m_engine->quit();
    }
    delete m_lines;
}

void CalcVarDialog::showBoard(Move last)
{
    m_board->setPosition(m_pos, last, true);
    const bool open = !m_judging;
    m_board->setMovableSides(open && m_pos.sideToMove() == White, open && m_pos.sideToMove() == Black);
    m_line->setText(m_current.empty() ? tr("Variante %1 : jouez ses coups.").arg(m_vars.size() + 1)
                                      : tr("Variante %1 : %2").arg(m_vars.size() + 1).arg(sanLine(m_start, m_current)));
    m_save->setEnabled(!m_current.empty() && open);
    m_reset->setEnabled(!m_current.empty() && open);
    m_judge->setEnabled(!m_vars.isEmpty() && open);
}

void CalcVarDialog::playMove(Move m)
{
    if (m_judging || !m_pos.isLegal(m)) return;
    m_pos.makeMove(m);
    m_current.push_back(m);
    showBoard(m);
}

void CalcVarDialog::finishVariation(int userEvalCp)
{
    if (m_current.empty() || m_judging) return;
    Variation v;
    v.moves = m_current;
    v.userEval = userEvalCp;
    m_vars << v;
    m_current.clear();
    m_pos = m_start;
    showBoard();
    refreshTable();
}

void CalcVarDialog::refreshTable()
{
    m_table->setRowCount(int(m_vars.size()));
    for (int i = 0; i < m_vars.size(); ++i) {
        const Variation& v = m_vars[i];
        m_table->setItem(i, 0, new QTableWidgetItem(sanLine(m_start, v.moves)));
        m_table->setItem(i, 1, new QTableWidgetItem(evalText(v.userEval)));
        m_table->setItem(i, 2, new QTableWidgetItem(v.judged ? evalText(v.engineEval, v.engineMate) : QString()));
        QString gap;
        if (v.judged) {
            const int engineCp = v.engineMate ? (v.engineEval > 0 ? 1000 : -1000) : v.engineEval;
            const int d = std::abs(std::clamp(engineCp, -300, 300) - std::clamp(v.userEval, -300, 300));
            gap = d <= 50 ? tr("juste") : d <= 150 ? tr("proche") : tr("à revoir (%1)").arg(evalText(d).mid(1));
            auto* it = new QTableWidgetItem(gap);
            it->setForeground(d <= 50 ? theme::Good : d <= 150 ? theme::Accent : theme::Bad);
            m_table->setItem(i, 3, it);
        } else {
            m_table->setItem(i, 3, new QTableWidgetItem());
        }
    }
}

UciEngine* CalcVarDialog::engine()
{
    if (m_engine) return m_engine;
    const EngineConfig sf = AutoEvaluator::findStockfish();
    m_engine = new UciEngine(this);
    connect(m_engine, &UciEngine::started, this, [this] {
        m_engineReady = true;
        if (m_pending) {
            auto f = std::move(m_pending);
            m_pending = nullptr;
            f();
        }
    });
    connect(m_engine, &UciEngine::infoReceived, this, [this](const EngineLine& l) {
        if (l.pv.isEmpty()) return;
        while (m_lines->size() < l.multipv) m_lines->append(EngineLine{});
        (*m_lines)[l.multipv - 1] = l;
    });
    connect(m_engine, &UciEngine::bestMoveReceived, this, [this](const QString&, const QString& fen) {
        if (fen != m_fen) return;
        Position pos;
        pos.setFen(fen.toStdString());
        const int sign = pos.sideToMove() == White ? 1 : -1;
        if (m_judgeIndex < 0) {
            QStringList out;
            for (const EngineLine& l : std::as_const(*m_lines)) {
                if (l.pv.isEmpty()) continue;
                std::vector<Move> moves;
                Position p = pos;
                for (const QString& u : l.pv.mid(0, 8)) {
                    const Move m = p.parseUci(u.toStdString());
                    if (m.isNull() || !p.isLegal(m)) break;
                    moves.push_back(m);
                    p.makeMove(m);
                }
                out << QStringLiteral("%1  %2").arg(evalText(sign * l.score, l.mate), sanLine(pos, moves));
            }
            m_startLines = out.join(QLatin1Char('\n'));
        } else if (m_judgeIndex < m_vars.size() && !m_lines->isEmpty()) {
            Variation& v = m_vars[m_judgeIndex];
            const EngineLine& l = m_lines->first();
            v.judged = true;
            v.engineMate = l.mate;
            v.engineEval = sign * l.score;
            refreshTable();
        }
        ++m_judgeIndex;
        nextJudgement();
    });
    if (!sf.isValid() || !m_engine->start(sf.path)) {
        m_result->setText(tr("Stockfish introuvable : le jugement demande le moteur embarqué."));
        return nullptr;
    }
    m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
    return m_engine;
}

void CalcVarDialog::judge(int msPerPosition)
{
    if (m_vars.isEmpty() || m_judging) return;
    if (!engine()) return;
    m_ms = std::max(50, msPerPosition);
    m_judging = true;
    m_judgeIndex = -1;
    showBoard();
    m_result->setText(tr("Le moteur analyse la position…"));
    nextJudgement();
}

void CalcVarDialog::nextJudgement()
{
    if (m_judgeIndex >= m_vars.size()) {
        m_judging = false;
        int good = 0;
        for (const Variation& v : std::as_const(m_vars)) {
            const int engineCp = v.engineMate ? (v.engineEval > 0 ? 1000 : -1000) : v.engineEval;
            good += std::abs(std::clamp(engineCp, -300, 300) - std::clamp(v.userEval, -300, 300)) <= 150;
        }
        m_result->setText(tr("%1 variante(s) bien évaluée(s) sur %2.\nLignes du moteur :\n%3").arg(good).arg(m_vars.size()).arg(m_startLines));
        showBoard();
        return;
    }
    // Position (MultiPV 3), then the end of each variation (MultiPV 1)
    const Position pos = m_judgeIndex < 0 ? m_start : after(m_start, m_vars[m_judgeIndex].moves);
    if (m_judgeIndex >= 0 && (pos.isCheckmate() || pos.legalMoves().empty())) {
        Variation& v = m_vars[m_judgeIndex];
        v.judged = true;
        v.engineMate = pos.isCheckmate();
        v.engineEval = pos.isCheckmate() ? (pos.sideToMove() == White ? -1 : 1) : 0; // mate on the board, or stalemate
        refreshTable();
        ++m_judgeIndex;
        nextJudgement();
        return;
    }
    if (m_judgeIndex >= 0) m_result->setText(tr("Le moteur juge la variante %1 / %2…").arg(m_judgeIndex + 1).arg(m_vars.size()));
    auto go = [this, pos] {
        m_lines->clear();
        m_engine->setOption(QStringLiteral("MultiPV"), m_judgeIndex < 0 ? QStringLiteral("3") : QStringLiteral("1"));
        m_fen = QString::fromStdString(pos.fen());
        m_engine->search(m_fen, QStringLiteral("movetime %1").arg(m_judgeIndex < 0 ? m_ms * 2 : m_ms));
    };
    if (m_engineReady) go();
    else m_pending = std::move(go);
}
