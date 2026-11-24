#include "annotator.h"
#include "userdirs.h"
#include <QLineEdit>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QDir>

#include "engine/uciengine.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

using namespace chess;

namespace annotation {

namespace {
QString tr(const char* text, int n = -1) { return QCoreApplication::translate("Annotation", text, nullptr, n); }

enum Nag { NagMistake = 2, NagBlunder = 4, NagDubious = 6 };

PositionEval negated(PositionEval e)
{
    e.score = -e.score;
    if (e.mate && e.score == 0) e.score = 1; // the side to move was mated: won for the other one
    return e;
}
} // namespace

double winningChances(const PositionEval& e)
{
    if (e.mate) return e.score > 0 ? 1.0 : -1.0; // mate 0: the side to move is mated
    const double cp = std::clamp(e.score, -1000, 1000);
    return 2.0 / (1.0 + std::exp(-0.00368208 * cp)) - 1.0; // Lichess curve
}

QString formatWhite(const PositionEval& e, Color toMove)
{
    const int v = toMove == White ? e.score : -e.score;
    if (e.mate) {
        if (e.score == 0) return QStringLiteral("#");
        return v > 0 ? QStringLiteral("#%1").arg(v) : QStringLiteral("#-%1").arg(-v);
    }
    return QString::asprintf("%+.2f", v / 100.0);
}

QString Summary::text() const
{
    QStringList parts;
    if (blunders) parts << tr("%n gaffe(s) (?\?)", blunders);
    if (mistakes) parts << tr("%n erreur(s) (?)", mistakes);
    if (inaccuracies) parts << tr("%n imprécision(s) (?!)", inaccuracies);
    if (missedMates) parts << tr("%n mat(s) manqué(s)", missedMates);
    if (!exercises.isEmpty()) parts << tr("%n exercice(s) tactique(s)", int(exercises.size()));
    return parts.isEmpty() ? tr("aucune erreur relevée") : parts.join(QStringLiteral(", "));
}

Summary apply(Game& game, const QList<PositionEval>& evals, const Options& o, const QString& annotator)
{
    Summary sum;
    const std::vector<Node*> line = game.mainline();
    if (evals.size() != int(line.size()) + 1) return sum;
    auto store = [](Node* n, const PositionEval& e) {
        if (!e.valid || e.depth < n->eval.depth) return;
        const int sign = n->pos.sideToMove() == White ? 1 : -1;
        n->eval = {true, e.mate, e.mate && e.score == 0 ? 0 : sign * e.score, e.depth};
    };
    store(game.root(), evals[0]);
    for (int i = 1; i <= int(line.size()); ++i) {
        Node* node = line[size_t(i - 1)];
        Node* parent = node->parent;
        const PositionEval& before = evals[i - 1];
        const PositionEval& after = evals[i];
        store(node, after);
        const Color mover = parent->pos.sideToMove();
        if ((o.side == Options::WhiteOnly && mover != White) || (o.side == Options::BlackOnly && mover != Black)) continue;
        if ((i + 1) / 2 <= o.skipMoves) continue;
        if (i <= o.bookPlies) continue; // book move: not judged
        if (!before.valid || !after.valid || before.pv.isEmpty()) continue;
        const QString played = QString::fromStdString(Position::toUci(node->move));
        if (before.pv.first() == played) continue; // the engine's move
        const PositionEval result = negated(after); // from the point of view of the one who played
        const double drop = winningChances(before) - winningChances(result);
        const bool missedMate = o.missedMates && before.mate && before.score > 0 && !(result.mate && result.score > 0);
        int nag = 0;
        if (drop >= 0.3) nag = NagBlunder;
        else if (drop >= 0.2) nag = NagMistake;
        else if (drop >= 0.1 && o.inaccuracies) nag = NagDubious;
        if (missedMate && nag == 0) nag = NagMistake;
        if (nag == 0) continue;
        switch (nag) {
        case NagBlunder: ++sum.blunders; break;
        case NagMistake: ++sum.mistakes; break;
        default: ++sum.inaccuracies; break;
        }
        if (missedMate) ++sum.missedMates;
        // Exercise: after the blunder, the opponent has a clear win to find
        if (o.markExercises && nag == NagBlunder && !after.pv.isEmpty() && winningChances(after) >= 0.4) {
            const Move bm = node->pos.parseUci(after.pv.first().toStdString());
            if (!bm.isNull() && node->pos.isLegal(bm)) {
                std::string fen = node->pos.fen();
                for (int cut = 0; cut < 2; ++cut) fen = fen.substr(0, fen.rfind(' ')); // EPD: 4 fields
                const QString id = QStringLiteral("%1 - %2, %3%4").arg(QString::fromStdString(game.header("White")), QString::fromStdString(game.header("Black")))
                                       .arg((i + 2) / 2)
                                       .arg(node->pos.sideToMove() == White ? QStringLiteral(".") : QStringLiteral("..."));
                sum.exercises << QStringLiteral("%1 bm %2; id \"%3\";")
                                     .arg(QString::fromStdString(fen), QString::fromStdString(node->pos.toSan(bm)), QString(id).replace('"', '\''));
                const std::string mark = tr("Exercice : trouvez le coup").toStdString();
                if (node->comment.find(mark) == std::string::npos) node->comment = node->comment.empty() ? mark : node->comment + ' ' + mark;
            }
        }
        // User annotation (!, ?…) kept
        if (std::none_of(node->nags.begin(), node->nags.end(), [](int x) { return x >= 1 && x <= 6; })) node->nags.insert(node->nags.begin(), nag);
        QString note = formatWhite(after, node->pos.sideToMove());
        if (missedMate) note = tr("Mat en %1 manqué").arg(before.score) + QStringLiteral(" ; ") + note;
        if (!node->comment.empty()) note = QString::fromStdString(node->comment) + QLatin1Char(' ') + note;
        node->comment = note.toStdString();
        if (!o.variations) continue;
        // Engine's best continuation as a variation, its evaluation at the end
        Node* cur = parent;
        Node* first = nullptr;
        for (int k = 0; k < before.pv.size() && k < std::max(1, o.variationPlies); ++k) {
            const Move m = cur->pos.parseUci(before.pv[k].toStdString());
            if (m.isNull()) break;
            cur = game.addMove(cur, m);
            if (!first) first = cur;
        }
        if (first && cur != parent) cur->comment = formatWhite(before, mover).toStdString();
    }
    // Out-of-book marker (like SCID vs PC: "Last book move")
    if (o.bookMarker && o.bookPlies > 0 && o.bookPlies < int(line.size())) {
        Node* last = line[size_t(o.bookPlies - 1)];
        const std::string mark =
            (o.bookName.isEmpty() ? tr("Dernier coup du livre") : tr("Dernier coup du livre (%1)").arg(o.bookName)).toStdString();
        if (last->comment.find(mark) == std::string::npos) last->comment = last->comment.empty() ? mark : last->comment + ' ' + mark;
    }
    if (!annotator.isEmpty()) game.setHeader("Annotator", annotator.toStdString());
    return sum;
}

QString defaultExercisesFile()
{
    return userdirs::exercises() + QStringLiteral("/exercises.epd");
}

bool appendExercises(const QString& path, const QStringList& epd, QString* error)
{
    if (epd.isEmpty() || path.isEmpty()) return true;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write((epd.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    return true;
}

} // namespace annotation

// ---------------------------------------------------------------- Annotator

Annotator::Annotator(QObject* parent)
    : QObject(parent)
{
}

Annotator::~Annotator() { shutdown(); }

bool Annotator::start(const EngineConfig& cfg, const QStringList& fens, int movetimeMs, QString* error)
{
    shutdown();
    if (!cfg.isValid() || fens.isEmpty()) {
        if (error) *error = cfg.isValid() ? tr("partie vide") : tr("aucun moteur");
        return false;
    }
    m_fens = fens;
    m_evals = QList<annotation::PositionEval>(fens.size());
    m_movetime = std::max(50, movetimeMs);
    m_current = -1;
    m_name = cfg.name.isEmpty() ? cfg.idName : cfg.name;
    m_engine = new UciEngine(this);
    connect(m_engine, &UciEngine::started, this, [this] { next(); });
    connect(m_engine, &UciEngine::infoReceived, this, &Annotator::onInfo);
    connect(m_engine, &UciEngine::bestMoveReceived, this, &Annotator::onBestMove);
    connect(m_engine, &UciEngine::terminated, this, [this] {
        if (!m_engine) return;
        m_engine->deleteLater();
        m_engine = nullptr;
        emit errorOccurred(tr("Le moteur s'est arrêté"));
        emit finished(false);
    });
    if (!m_engine->start(cfg.path)) {
        delete m_engine;
        m_engine = nullptr;
        if (error) *error = tr("Impossible de lancer le moteur");
        return false;
    }
    if (cfg.id.isEmpty()) m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
    for (auto it = cfg.values.cbegin(); it != cfg.values.cend(); ++it) m_engine->setOption(it.key(), it.value());
    // At full strength, a single line
    if (cfg.option(QStringLiteral("MultiPV"))) m_engine->setOption(QStringLiteral("MultiPV"), QStringLiteral("1"));
    if (cfg.option(QStringLiteral("UCI_LimitStrength"))) m_engine->setOption(QStringLiteral("UCI_LimitStrength"), QStringLiteral("false"));
    return true;
}

void Annotator::cancel()
{
    if (!m_engine) return;
    shutdown();
    emit finished(false);
}

void Annotator::shutdown()
{
    if (!m_engine) return;
    UciEngine* e = m_engine;
    m_engine = nullptr;
    e->disconnect(this);
    e->quit();
    e->deleteLater();
}

void Annotator::next()
{
    while (m_engine) {
        if (++m_current >= m_fens.size()) {
            shutdown();
            emit progress(int(m_fens.size()), int(m_fens.size()));
            emit finished(true);
            return;
        }
        emit progress(m_current, int(m_fens.size()));
        Position p;
        if (!p.setFen(m_fens[m_current].toStdString())) continue;
        if (p.legalMoves().empty()) { // mate or stalemate: nothing to search
            m_evals[m_current] = {true, p.inCheck(), 0, 99, {}};
            continue;
        }
        m_engine->search(m_fens[m_current], QStringLiteral("movetime %1").arg(m_movetime));
        return;
    }
}

void Annotator::onInfo(const EngineLine& line)
{
    if (m_current < 0 || m_current >= m_fens.size() || line.multipv != 1 || line.pv.isEmpty()) return;
    m_evals[m_current] = {true, line.mate, line.score, line.depth, line.pv};
}

void Annotator::onBestMove(const QString& uci, const QString& fen)
{
    if (m_current < 0 || m_current >= m_fens.size() || (!fen.isEmpty() && fen != m_fens[m_current])) return;
    annotation::PositionEval& e = m_evals[m_current];
    if (e.pv.isEmpty() && !uci.isEmpty() && uci != QLatin1String("0000")) e.pv = {uci};
    else if (!e.pv.isEmpty() && !uci.isEmpty() && e.pv.first() != uci) e.pv = {uci}; // move played ≠ start of the last line
    QTimer::singleShot(0, this, &Annotator::next);
}

// ---------------------------------------------------------------- AnnotateDialog

AnnotateDialog::AnnotateDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Annoter la partie"));
    QSettings s;
    m_engine = new QComboBox;
    for (const EngineConfig& e : EngineRegistry::instance().engines()) m_engine->addItem(e.name, e.id);
    const QString last = s.value("annotate/engine", EngineRegistry::instance().fallbackEngine().id).toString();
    if (const int i = m_engine->findData(last); i >= 0) m_engine->setCurrentIndex(i);
    m_time = new QDoubleSpinBox;
    m_time->setRange(0.1, 600);
    m_time->setDecimals(1);
    m_time->setSuffix(tr(" s"));
    m_time->setValue(s.value("annotate/seconds", 1.0).toDouble());
    m_side = new QComboBox;
    m_side->addItem(tr("Les deux camps"), int(annotation::Options::Both));
    m_side->addItem(tr("Blancs seulement"), int(annotation::Options::WhiteOnly));
    m_side->addItem(tr("Noirs seulement"), int(annotation::Options::BlackOnly));
    m_side->setCurrentIndex(std::clamp(s.value("annotate/side", 0).toInt(), 0, 2));
    m_inaccuracies = new QCheckBox(tr("Imprécisions (?!) en plus des erreurs (?) et des gaffes (?\?)"));
    m_inaccuracies->setChecked(s.value("annotate/inaccuracies", true).toBool());
    m_missedMates = new QCheckBox(tr("Mats manqués"));
    m_missedMates->setChecked(s.value("annotate/missedMates", true).toBool());
    m_variations = new QCheckBox(tr("Meilleure suite du moteur en variante"));
    m_variations->setChecked(s.value("annotate/variations", true).toBool());
    m_plies = new QSpinBox;
    m_plies->setRange(1, 40);
    m_plies->setSuffix(tr(" demi-coups"));
    m_plies->setValue(s.value("annotate/plies", 8).toInt());
    m_skip = new QSpinBox;
    m_skip->setRange(0, 60);
    m_skip->setSpecialValueText(tr("aucun"));
    m_skip->setSuffix(tr(" coups"));
    m_skip->setValue(s.value("annotate/skip", 0).toInt());
    m_book = new QCheckBox(tr("Repère de sortie du livre (arbre d'ouverture ouvert) ; coups du livre non jugés"));
    m_book->setChecked(s.value("annotate/book", true).toBool());
    m_exercises = new QCheckBox(tr("Marquer les exercices tactiques (après une gaffe, le coup gagnant à trouver)"));
    m_exercises->setChecked(s.value("annotate/exercises", false).toBool());
    m_exercisesFile = new QLineEdit(s.value("annotate/exercisesFile", annotation::defaultExercisesFile()).toString());
    m_exercisesFile->setToolTip(tr("Recueil EPD complété ; il se rejoue dans Partie → Entraînement… (meilleur coup)"));
    m_exercisesFile->setEnabled(m_exercises->isChecked());
    connect(m_exercises, &QCheckBox::toggled, m_exercisesFile, &QWidget::setEnabled);
    connect(m_variations, &QCheckBox::toggled, m_plies, &QWidget::setEnabled);
    m_plies->setEnabled(m_variations->isChecked());

    auto* note = new QLabel(tr("Le moteur analyse chaque position de la ligne principale. Un coup qui fait perdre des chances de gain est "
                               "marqué ?!, ? ou ??, avec l'évaluation et la meilleure suite. Les annotations déjà présentes sont gardées."));
    note->setWordWrap(true);
    note->setObjectName("Faint");

    auto* form = new QFormLayout;
    form->addRow(tr("Moteur"), m_engine);
    form->addRow(tr("Temps par position"), m_time);
    form->addRow(tr("Coups annotés"), m_side);
    form->addRow(tr("Ignorer les premiers"), m_skip);
    form->addRow(QString(), m_book);
    form->addRow(QString(), m_inaccuracies);
    form->addRow(QString(), m_missedMates);
    form->addRow(QString(), m_variations);
    form->addRow(tr("Longueur des variantes"), m_plies);
    form->addRow(QString(), m_exercises);
    form->addRow(tr("Recueil d'exercices"), m_exercisesFile);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Annoter"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Ok)->setEnabled(m_engine->count() > 0);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings st;
        st.setValue("annotate/engine", m_engine->currentData());
        st.setValue("annotate/seconds", m_time->value());
        st.setValue("annotate/side", m_side->currentIndex());
        st.setValue("annotate/inaccuracies", m_inaccuracies->isChecked());
        st.setValue("annotate/missedMates", m_missedMates->isChecked());
        st.setValue("annotate/variations", m_variations->isChecked());
        st.setValue("annotate/plies", m_plies->value());
        st.setValue("annotate/skip", m_skip->value());
        st.setValue("annotate/book", m_book->isChecked());
        st.setValue("annotate/exercises", m_exercises->isChecked());
        st.setValue("annotate/exercisesFile", m_exercisesFile->text().trimmed());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

annotation::Options AnnotateDialog::options() const
{
    annotation::Options o;
    o.movetimeMs = int(m_time->value() * 1000);
    o.side = annotation::Options::Side(m_side->currentData().toInt());
    o.inaccuracies = m_inaccuracies->isChecked();
    o.missedMates = m_missedMates->isChecked();
    o.variations = m_variations->isChecked();
    o.variationPlies = m_plies->value();
    o.skipMoves = m_skip->value();
    o.bookMarker = m_book->isChecked();
    o.markExercises = m_exercises->isChecked();
    o.exercisesFile = o.markExercises ? m_exercisesFile->text().trimmed() : QString();
    return o;
}

EngineConfig AnnotateDialog::engine() const { return EngineRegistry::instance().engine(m_engine->currentData().toString()); }
