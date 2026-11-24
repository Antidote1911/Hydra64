#include "training.h"

#include "bundledbases.h"
#include "display.h"

#include "addexercise.h"
#include "autoevaluator.h"
#include "boardwidget.h"
#include "engine/uciengine.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <random>

using namespace chess;

namespace {
enum Phase { Idle, Measure, Defend };
int g_phase = Idle; // a single active exercise at a time (single window)
QString sanList(Position pos, const std::vector<Move>& moves)
{
    QStringList out;
    for (const Move& m : moves) {
        if (!pos.isLegal(m)) break;
        out << display::san(pos.toSan(m));
        pos.makeMove(m);
    }
    return out.join(QStringLiteral(", "));
}
} // namespace

TrainingDialog::TrainingDialog(Book book, QWidget* parent)
    : QDialog(parent)
    , m_book(std::move(book))
    , m_line(new EngineLine)
    , m_progress(std::make_unique<TrainingProgress>())
{
    setWindowTitle(tr("Entraînement"));
    resize(900, 620);
    QSettings s;
    m_board = new BoardWidget;
    m_board->setMinimumSize(420, 420);
    m_modeBox = new QComboBox;
    m_modeBox->addItem(tr("Ouverture avec l'arbre d'ouverture"), int(Opening));
    m_modeBox->addItem(tr("Trouver le meilleur coup (recueil)"), int(BestMove));
    m_modeBox->addItem(tr("Mats en N (recueil, avec Stockfish)"), int(Mate));
    m_modeBox->setCurrentIndex(std::clamp(s.value("training/mode", 0).toInt(), 0, 2));
    m_sideBox = new QComboBox;
    m_sideBox->addItem(tr("Blancs"), int(White));
    m_sideBox->addItem(tr("Noirs"), int(Black));
    m_collection = new QComboBox;
    m_file = new QLineEdit(s.value("training/file").toString());
    m_file->setPlaceholderText(tr("Recueil : EPD (bm / am), PGN ou base Hydra64"));
    m_browse = new QPushButton(theme::icon("open"), QString());
    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(m_file, 1);
    fileRow->addWidget(m_browse);
    m_start = new QPushButton(theme::icon("play"), tr("Commencer"));
    m_start->setObjectName("Primary");
    m_next = new QPushButton(tr("Suivante"));
    m_solution = new QPushButton(tr("Solution"));
    m_analyse = new QPushButton(theme::icon("analysis"), tr("Analyser"));
    m_analyse->setToolTip(tr("Ouvrir la position de l'exercice dans la page Analyse"));
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setMinimumWidth(320);
    m_score = new QLabel;
    m_score->setObjectName("Faint");

    auto* form = new QFormLayout;
    form->addRow(tr("Exercice"), m_modeBox);
    form->addRow(tr("Votre camp"), m_sideBox);
    form->addRow(tr("Recueil"), m_collection);
    form->addRow(QString(), fileRow);
    m_spaced = new QCheckBox(tr("Révision espacée (les positions ratées reviennent plus souvent)"));
    m_spaced->setChecked(s.value("training/spaced", true).toBool());
    m_spaced->setToolTip(tr("Chaque position réussie est revue après 1, 3, 7, 14 puis 30 jours ; une position ratée revient aussitôt, "
                            "et encore quelques positions plus loin. Votre progression est gardée pour chaque recueil."));
    m_resetProgress = new QPushButton(tr("Remettre à zéro"));
    m_resetProgress->setToolTip(tr("Oublier la progression enregistrée pour ce recueil"));
    m_check = new QPushButton(tr("Vérifier…"));
    m_check->setToolTip(tr("Faire vérifier ce recueil par Stockfish : solutions fausses, exercices ambigus, mats non forcés"));
    auto* spacedRow = new QHBoxLayout;
    spacedRow->addWidget(m_spaced, 1);
    spacedRow->addWidget(m_resetProgress);
    spacedRow->addWidget(m_check);
    form->addRow(QString(), spacedRow);
    auto* actions = new QHBoxLayout;
    for (QPushButton* b : {m_start, m_next, m_solution, m_analyse}) actions->addWidget(b);
    actions->addStretch(1);
    auto* right = new QVBoxLayout;
    right->addLayout(form);
    right->addLayout(actions);
    right->addSpacing(8);
    right->addWidget(m_status);
    right->addWidget(m_score);
    right->addStretch(1);
    auto* body = new QHBoxLayout;
    body->addWidget(m_board, 3);
    body->addLayout(right, 2);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);

    connect(m_modeBox, &QComboBox::currentIndexChanged, this, &TrainingDialog::refreshCollections);
    connect(m_collection, &QComboBox::currentIndexChanged, this, &TrainingDialog::updateFileRow);
    refreshCollections();
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_board, &BoardWidget::moveRequested, this, &TrainingDialog::playMove);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Recueil"), QFileInfo(m_file->text()).absolutePath(),
                                                       tr("Recueils (*.hy64 *.hy64i *.epd *.pgn);;Tous les fichiers (*)"));
        if (!p.isEmpty()) m_file->setText(p);
    });
    connect(m_start, &QPushButton::clicked, this, [this] {
        QSettings st;
        st.setValue("training/mode", m_modeBox->currentIndex());
        st.setValue("training/file", m_file->text());
        st.setValue("training/spaced", m_spaced->isChecked());
        const Mode mode = Mode(m_modeBox->currentData().toInt());
        if (mode == Opening) {
            startOpening(Color(m_sideBox->currentData().toInt()));
            return;
        }
        const QString chosen = m_collection->currentData().toString();
        st.setValue(QStringLiteral("training/collection%1").arg(int(mode)), chosen);
        QString error;
        // Built-in databases (one category): order drawn at random; your collections: file order
        QString path, category;
        splitCollectionId(chosen, path, category);
        const bool ok = chosen.isEmpty() ? startPositions(mode, m_file->text().trimmed(), &error)
                                         : startPositions(mode, path, &error, !category.isEmpty(), category);
        if (!ok) setStatus(error);
    });
    connect(m_next, &QPushButton::clicked, this, &TrainingDialog::nextPosition);
    connect(m_solution, &QPushButton::clicked, this, [this] {
        if (m_mode == Opening) {
            const std::vector<Move> known = m_book.known ? m_book.known(m_pos) : std::vector<Move>{};
            setStatus(known.empty() ? tr("Hors de l'arbre.") : tr("Coups de l'arbre : %1").arg(sanList(m_pos, known)));
            return;
        }
        if (m_index < 0 || m_index >= m_positions.size()) return;
        const TestPosition& t = m_positions[m_index];
        const QString sol = !t.line.isEmpty() ? t.line : t.solution;
        setStatus(tr("Solution : %1").arg(sol.isEmpty() ? tr("(non donnée par le recueil)") : sol));
        if (!m_done) finishExercise(false, tr("Solution : %1").arg(sol.isEmpty() ? tr("(non donnée par le recueil)") : sol));
    });
    connect(m_check, &QPushButton::clicked, this, [this] {
        QString path, category;
        splitCollectionId(m_collection->currentData().toString(), path, category);
        emit checkRequested(path.isEmpty() ? m_file->text().trimmed() : path);
    });
    connect(m_resetProgress, &QPushButton::clicked, this, [this] {
        QString path, category;
        splitCollectionId(m_collection->currentData().toString(), path, category);
        if (path.isEmpty()) path = m_file->text().trimmed();
        if (path.isEmpty()) return;
        m_progress->reset(path);
        m_progress->save();
        setStatus(tr("Progression oubliée pour ce recueil."));
    });
    connect(m_analyse, &QPushButton::clicked, this, [this] { emit analyseRequested(QString::fromStdString(m_startPos.fen())); });
    m_pos = Position::startPos();
    m_startPos = m_pos;
    showPosition();
    setStatus(tr("Choisissez un exercice puis « Commencer »."));
}

TrainingDialog::~TrainingDialog()
{
    if (m_engine) m_engine->quit();
    delete m_line;
    g_phase = Idle;
}

void TrainingDialog::setStatus(const QString& text)
{
    m_status->setText(text);
    QStringList lines;
    if (m_tried) lines << tr("Réussis : %1 / %2").arg(m_solved).arg(m_tried);
    if (m_mode != Opening && m_spacedActive && !m_collectionPath.isEmpty()) {
        const TrainingProgress::Stats st = progressStats();
        lines << tr("Recueil : %1 à revoir, %2 nouvelle(s), %3 acquise(s) sur %4").arg(st.due).arg(st.fresh).arg(st.learned).arg(st.total);
        if (st.tries) lines << tr("Réussite sur tous vos essais : %1 %").arg(qRound(100.0 * st.successes / st.tries));
    }
    m_score->setText(lines.join(QLatin1Char('\n')));
}

QString TrainingDialog::positionKey(const TestPosition& t) { return t.fen; }

TrainingProgress::Stats TrainingDialog::progressStats() const
{
    QStringList keys;
    QSet<QString> seen;
    for (const TestPosition& t : m_positions)
        if (!seen.contains(positionKey(t))) {
            seen.insert(positionKey(t));
            keys << positionKey(t);
        }
    return m_progress->stats(m_collectionPath, keys);
}

QString TrainingDialog::status() const { return m_status->text(); }

void TrainingDialog::showPosition(Move last)
{
    m_board->setPosition(m_pos, last, true);
    m_board->setFlipped(m_side == Black);
    const bool userTurn = !m_done && !m_waiting && m_pos.sideToMove() == m_side;
    m_board->setMovableSides(userTurn && m_side == White, userTurn && m_side == Black);
}

// ---------------------------------------------------------------- opening

void TrainingDialog::startOpening(Color side)
{
    m_mode = Opening;
    m_side = side;
    m_pos = Position::startPos();
    m_startPos = m_pos;
    m_done = false;
    m_movesPlayed = 0;
    if (!m_book.known || !m_book.pick || m_book.known(m_pos).empty()) {
        m_done = true;
        showPosition();
        setStatus(tr("Ouvrez d'abord un arbre d'ouverture (panneau Arbre d'ouverture) qui contient la position de départ."));
        return;
    }
    auto reply = [this] {
        m_waiting = true;
        showPosition();
        QTimer::singleShot(250, this, [this] {
            m_waiting = false;
            const Move m = m_book.pick(m_pos);
            if (m.isNull()) {
                m_done = true;
                showPosition();
                setStatus(tr("Fin de l'arbre « %1 » : vous l'avez suivi sur %n coup(s).", nullptr, m_movesPlayed).arg(bookName()));
                return;
            }
            const Position before = m_pos;
            m_pos.makeMove(m);
            showPosition(m);
            setStatus(tr("L'arbre joue %1. À vous.").arg(display::san(before.toSan(m))));
            if (m_book.known(m_pos).empty()) {
                m_done = true;
                showPosition(m);
                setStatus(tr("Fin de l'arbre « %1 » : vous l'avez suivi sur %n coup(s).", nullptr, m_movesPlayed).arg(bookName()));
            }
        });
    };
    showPosition();
    if (side == Black) reply();
    else setStatus(tr("Jouez un coup de l'arbre « %1 ».").arg(bookName()));
}

// ---------------------------------------------------------------- collections

void TrainingDialog::updateFileRow()
{
    const bool own = m_modeBox->currentData().toInt() != Opening && m_collection->currentData().toString().isEmpty();
    m_file->setVisible(own);
    m_browse->setVisible(own);
}

void TrainingDialog::refreshCollections()
{
    const int mode = m_modeBox->currentData().toInt();
    const bool opening = mode == Opening;
    m_sideBox->setEnabled(opening);
    m_collection->setEnabled(!opening);
    m_next->setEnabled(!opening);
    m_spaced->setEnabled(!opening);
    m_resetProgress->setEnabled(!opening);
    m_check->setEnabled(!opening);
    {
        const QSignalBlocker block(m_collection);
        const QString current = m_collection->currentData().toString();
        m_collection->clear();
        if (!opening) {
            for (const Collection& c : builtInCollections(Mode(mode))) m_collection->addItem(c.name, collectionId(c));
            for (const Collection& c : userCollections()) m_collection->addItem(c.name, collectionId(c));
            m_collection->addItem(tr("Autre fichier…"), QString());
            const QString saved = QSettings().value(QStringLiteral("training/collection%1").arg(mode), QStringLiteral(":")).toString();
            int i = m_collection->findData(sender() == m_modeBox || current.isNull() ? saved : current);
            if (i < 0) i = m_collection->findData(saved);
            m_collection->setCurrentIndex(i >= 0 ? i : 0);
        }
    }
    updateFileRow();
}

void TrainingDialog::showEvent(QShowEvent* e)
{
    refreshCollections(); // a collection may have been created since (Game → Add to collection…)
    QDialog::showEvent(e);
}

QList<TrainingDialog::Collection> TrainingDialog::userCollections()
{
    QList<Collection> out;
    for (const QString& p : exercise::collections()) out.append({p, tr("Mes recueils : %1").arg(QFileInfo(p).completeBaseName())});
    return out;
}

QString TrainingDialog::builtInDir()
{
    return bundled::databasesDir() + QStringLiteral("/training");
}

QString TrainingDialog::collectionId(const Collection& c) { return c.category.isEmpty() ? c.path : c.path + QLatin1Char('\n') + c.category; }

void TrainingDialog::splitCollectionId(const QString& id, QString& path, QString& category)
{
    const int cut = id.indexOf(QLatin1Char('\n'));
    path = cut < 0 ? id : id.left(cut);
    category = cut < 0 ? QString() : id.mid(cut + 1);
}

QList<TrainingDialog::Collection> TrainingDialog::builtInCollections(Mode mode)
{
    // One Hydra64 database per category (databases/training/mates, …/best_moves); the category
    // (Event field) also marks the built-in databases, drawn in random order.
    QList<Collection> out;
    const QString dir = builtInDir();
    if (mode == Mate) {
        for (int n = 1; n <= 5; ++n)
            out.append({dir + QStringLiteral("/mates/mate_in_%1.hy64").arg(n), tr("Intégré : mats en %n (exercices Lichess)", nullptr, n),
                        QStringLiteral("Mat en %1").arg(n)});
    } else if (mode == BestMove) {
        out.append({dir + QStringLiteral("/best_moves/best_move_easy.hy64"), tr("Intégré : facile, classement < 1500 (exercices Lichess)"),
                    QStringLiteral("Meilleur coup, facile")});
        out.append({dir + QStringLiteral("/best_moves/best_move_medium.hy64"), tr("Intégré : moyen, 1500 à 2000 (exercices Lichess)"),
                    QStringLiteral("Meilleur coup, moyen")});
        out.append({dir + QStringLiteral("/best_moves/best_move_hard.hy64"), tr("Intégré : difficile, > 2000 (exercices Lichess)"),
                    QStringLiteral("Meilleur coup, difficile")});
    }
    return out;
}

bool TrainingDialog::startPositions(Mode mode, const QString& path, QString* error, bool shuffle, const QString& category)
{
    m_positions = solutiontest::load(path, error);
    if (!category.isEmpty()) {
        m_positions.erase(std::remove_if(m_positions.begin(), m_positions.end(), [&](const TestPosition& t) { return t.id != category; }),
                          m_positions.end());
        if (m_positions.isEmpty() && error) *error = tr("Aucune position « %1 » dans %2.").arg(category, QFileInfo(path).fileName());
    }
    if (m_positions.isEmpty()) return false;
    if (shuffle) std::shuffle(m_positions.begin(), m_positions.end(), std::mt19937(QRandomGenerator::global()->generate()));
    m_collectionPath = path;
    m_spacedActive = m_spaced->isChecked();
    m_requeued.clear();
    if (m_spacedActive) {
        // To review first, then the new ones (in the order above), then the others
        QStringList keys;
        for (const TestPosition& t : std::as_const(m_positions)) keys << positionKey(t);
        QList<TestPosition> ordered;
        for (int i : m_progress->order(path, keys)) ordered << m_positions[i];
        m_positions = ordered;
    }
    m_mode = mode;
    m_index = -1;
    m_solved = m_tried = 0;
    nextPosition();
    return true;
}

void TrainingDialog::nextPosition()
{
    if (m_mode == Opening) return;
    if (++m_index >= m_positions.size()) {
        m_done = true;
        setStatus(tr("Recueil terminé : %1 réussi(s) sur %2.").arg(m_solved).arg(m_tried));
        return;
    }
    m_pos.setFen(m_positions[m_index].fen.toStdString());
    m_startPos = m_pos;
    m_side = m_pos.sideToMove();
    m_done = false;
    m_movesPlayed = 0;
    m_mateIn = 0;
    const QString who = m_side == White ? tr("les Blancs") : tr("les Noirs");
    if (m_mode == BestMove) {
        showPosition();
        const TestPosition& t = m_positions[m_index];
        const QString label = t.rating > 0 ? tr("%1, classement %2").arg(t.id).arg(t.rating) : t.id;
        setStatus(tr("Position %1 / %2 (%3) : trouvez le meilleur coup pour %4.").arg(m_index + 1).arg(m_positions.size()).arg(label, who));
        return;
    }
    // Mates: distance announced by the collection (dm), otherwise Stockfish measures it first
    if (m_positions[m_index].mateIn > 0 && m_positions[m_index].mateIn <= 8) {
        m_mateIn = m_positions[m_index].mateIn;
        showPosition();
        setStatus(tr("Position %1 / %2 : %3 matent en %n coup(s).", nullptr, m_mateIn).arg(m_index + 1).arg(m_positions.size()).arg(who));
        return;
    }
    m_waiting = true;
    showPosition();
    setStatus(tr("Position %1 / %2 : recherche du mat…").arg(m_index + 1).arg(m_positions.size()));
    g_phase = Measure;
    engineSearch(m_pos, 1500);
}

void TrainingDialog::finishExercise(bool success, const QString& text)
{
    m_done = true;
    ++m_tried;
    if (success) ++m_solved;
    if (m_mode != Opening && m_spacedActive && m_index >= 0 && m_index < m_positions.size()) {
        const TestPosition t = m_positions[m_index];
        m_progress->record(m_collectionPath, positionKey(t), success);
        m_progress->save();
        // Missed: it comes back a few positions later (twice at most per session)
        if (!success && m_requeued.value(positionKey(t)) < 2) {
            m_requeued[positionKey(t)] += 1;
            m_positions.insert(std::min(int(m_positions.size()), m_index + 1 + RequeueGap), t);
        }
    }
    showPosition();
    setStatus(text + (m_mode == Opening ? QString() : tr(" — « Suivante » pour continuer.")));
}

void TrainingDialog::playMove(Move m)
{
    if (m_done || m_waiting || m_pos.sideToMove() != m_side || !m_pos.isLegal(m)) return;
    const Position before = m_pos;
    const QString san = display::san(before.toSan(m));
    if (m_mode == Opening) {
        const std::vector<Move> known = m_book.known(m_pos);
        if (std::find(known.begin(), known.end(), m) == known.end()) {
            m_done = true;
            showPosition();
            setStatus(tr("%1 sort de l'arbre. Coups de l'arbre : %2").arg(san, sanList(before, known)));
            return;
        }
        m_pos.makeMove(m);
        ++m_movesPlayed;
        showPosition(m);
        if (m_book.known(m_pos).empty()) {
            m_done = true;
            showPosition(m);
            setStatus(tr("Fin de l'arbre « %1 » : vous l'avez suivi sur %n coup(s).", nullptr, m_movesPlayed).arg(bookName()));
            return;
        }
        // the tree replies
        m_waiting = true;
        showPosition(m);
        QTimer::singleShot(250, this, [this] {
            m_waiting = false;
            const Move r = m_book.pick(m_pos);
            if (r.isNull()) {
                m_done = true;
                showPosition();
                setStatus(tr("Fin de l'arbre « %1 » : vous l'avez suivi sur %n coup(s).", nullptr, m_movesPlayed).arg(bookName()));
                return;
            }
            const Position b = m_pos;
            m_pos.makeMove(r);
            showPosition(r);
            setStatus(tr("L'arbre joue %1. À vous.").arg(display::san(b.toSan(r))));
            if (m_book.known(m_pos).empty()) {
                m_done = true;
                showPosition(r);
                setStatus(tr("Fin de l'arbre « %1 » : vous l'avez suivi sur %n coup(s).", nullptr, m_movesPlayed).arg(bookName()));
            }
        });
        return;
    }
    if (m_mode == BestMove) {
        const TestPosition& t = m_positions[m_index];
        const bool ok = solutiontest::isCorrect(t, QString::fromStdString(Position::toUci(m)));
        m_pos.makeMove(m);
        finishExercise(ok, ok ? tr("Bravo : %1 est la solution.").arg(san) : tr("%1 n'est pas la solution (%2).").arg(san, t.solution));
        return;
    }
    // Mates in N
    m_pos.makeMove(m);
    ++m_movesPlayed;
    showPosition(m);
    if (m_pos.isCheckmate()) {
        finishExercise(true, tr("Mat ! Réussi en %n coup(s).", nullptr, m_movesPlayed));
        return;
    }
    if (m_pos.legalMoves().empty()) {
        finishExercise(false, tr("Pat : le mat est manqué."));
        return;
    }
    if (m_movesPlayed >= m_mateIn) {
        finishExercise(false, tr("Pas de mat en %n coup(s).", nullptr, m_mateIn));
        return;
    }
    // Stockfish defends and checks that the mate is still forced
    m_waiting = true;
    showPosition(m);
    setStatus(tr("%1 : l'adversaire cherche sa défense…").arg(san));
    g_phase = Defend;
    engineSearch(m_pos, 600);
}

// ---------------------------------------------------------------- engine

void TrainingDialog::engineSearch(const Position& pos, int ms)
{
    if (!m_engine) {
        const EngineConfig sf = AutoEvaluator::findStockfish();
        m_engine = new UciEngine(this);
        connect(m_engine, &UciEngine::started, this, [this] {
            m_engineReady = true;
            if (m_pendingSearch) {
                auto f = std::move(m_pendingSearch);
                m_pendingSearch = nullptr;
                f();
            }
        });
        connect(m_engine, &UciEngine::infoReceived, this, [this](const EngineLine& l) {
            if (l.multipv == 1 && !l.pv.isEmpty()) *m_line = l;
        });
        connect(m_engine, &UciEngine::bestMoveReceived, this, [this](const QString& uci, const QString& fen) {
            if (fen == m_engineFen) onEngineResult(*m_line, uci);
        });
        if (!sf.isValid() || !m_engine->start(sf.path)) {
            m_waiting = false;
            setStatus(tr("Stockfish introuvable : les mats demandent le moteur embarqué."));
            return;
        }
        m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 4)));
    }
    auto go = [this, pos, ms] {
        *m_line = {};
        m_engineFen = QString::fromStdString(pos.fen());
        m_engine->search(m_engineFen, QStringLiteral("movetime %1").arg(ms));
    };
    if (m_engineReady) go();
    else m_pendingSearch = std::move(go);
}

void TrainingDialog::onEngineResult(const EngineLine& line, const QString& best)
{
    m_waiting = false;
    if (g_phase == Measure) {
        g_phase = Idle;
        if (!line.mate || line.score <= 0 || line.score > 8) { // no forced mate (for the side to move): next exercise
            setStatus(tr("Position %1 : pas de mat forcé trouvé, exercice suivant.").arg(m_index + 1));
            QTimer::singleShot(600, this, &TrainingDialog::nextPosition);
            return;
        }
        m_mateIn = line.score;
        showPosition();
        setStatus(tr("Position %1 / %2 : %3 matent en %n coup(s).", nullptr, m_mateIn)
                      .arg(m_index + 1)
                      .arg(m_positions.size())
                      .arg(m_side == White ? tr("les Blancs") : tr("les Noirs")));
        return;
    }
    if (g_phase == Defend) {
        g_phase = Idle;
        // The defender is to move: a mate against it (negative score) within the remaining moves
        const int left = m_mateIn - m_movesPlayed;
        if (!line.mate || line.score >= 0 || -line.score > left) {
            finishExercise(false, tr("Le mat est perdu : la défense tient. Solution : %1").arg(m_positions[m_index].line.isEmpty() ? m_positions[m_index].solution : m_positions[m_index].line));
            return;
        }
        const Move d = m_pos.parseUci(best.toStdString());
        if (d.isNull() || !m_pos.isLegal(d)) return;
        const QString san = display::san(m_pos.toSan(d));
        m_pos.makeMove(d);
        showPosition(d);
        setStatus(tr("L'adversaire joue %1. Mat en %n coup(s) : à vous.", nullptr, left).arg(san));
    }
}

void TrainingDialog::setSpacedRepetition(bool on) { m_spaced->setChecked(on); }
