#include "guessdialog.h"

#include "annotator.h"
#include "boardwidget.h"
#include "chess/pgn.h"
#include "display.h"
#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

using namespace chess;

bool GuessDialog::ask(QWidget* parent, const Game& game, Color* side, int* fromMove)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Deviner les coups de la partie"));
    auto* who = new QComboBox;
    who->addItem(tr("Blancs : %1").arg(QString::fromStdString(game.header("White"))));
    who->addItem(tr("Noirs : %1").arg(QString::fromStdString(game.header("Black"))));
    who->setCurrentIndex(game.header("Result") == "0-1" ? 1 : 0); // the winner's moves are the ones worth finding
    const int moves = (int(game.mainline().size()) + 1) / 2;
    auto* from = new QSpinBox;
    from->setRange(1, std::max(1, moves));
    from->setValue(std::min(10, std::max(1, moves / 2)));
    from->setToolTip(tr("Les coups d'avant sont joués d'office (la théorie n'est pas à deviner)"));
    auto* form = new QFormLayout;
    form->addRow(tr("Vous jouez les"), who);
    form->addRow(tr("À partir du coup"), from);
    auto* note = new QLabel(tr("Trouvez sur l'échiquier chaque coup de ce camp ; les réponses adverses se jouent seules. "
                               "Un autre coup que celui de la partie est jugé par le moteur."));
    note->setWordWrap(true);
    note->setObjectName("Faint");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Commencer"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return false;
    *side = who->currentIndex() == 0 ? White : Black;
    *fromMove = from->value();
    return true;
}

int GuessDialog::pointsFor(double mine, double games)
{
    const double behind = games - mine;
    return behind <= 0.05 ? 2 : behind <= 0.1 ? 1 : 0;
}

GuessDialog::GuessDialog(const Game& game, Color side, int fromMove, QWidget* parent)
    : QDialog(parent)
    , m_side(side)
    , m_annotator(new Annotator(this))
{
    pgn::parseGame(pgn::writeGame(game), m_game);
    const int firstPly = (std::max(1, fromMove) - 1) * 2 + (side == White ? 1 : 2);
    for (const Node* n : m_game.mainline())
        if (n->parent->pos.sideToMove() == side && n->ply() >= firstPly && !n->move.isNull()) m_questions.push_back(n);

    const QString white = QString::fromStdString(m_game.header("White")), black = QString::fromStdString(m_game.header("Black"));
    setWindowTitle(tr("Deviner les coups : %1 – %2").arg(white, black));
    m_board = new BoardWidget;
    m_board->setMinimumSize(440, 440);
    m_board->setFlipped(side == Black);
    m_title = new QLabel;
    m_title->setStyleSheet(QStringLiteral("font-size:16px;font-weight:600;"));
    m_text = new QLabel;
    m_text->setWordWrap(true);
    m_feedback = new QLabel;
    m_feedback->setWordWrap(true);
    m_feedback->setTextFormat(Qt::RichText);
    m_scoreLabel = new QLabel;
    m_hint = new QPushButton(tr("Indice"));
    m_hint->setToolTip(tr("Marque la pièce à jouer (1 point au plus)"));
    m_reveal = new QPushButton(tr("Montrer le coup"));
    m_next = new QPushButton(tr("Continuer"));
    m_next->setObjectName("Primary");
    auto* sideBox = new QVBoxLayout;
    sideBox->addWidget(m_title);
    sideBox->addWidget(m_text);
    sideBox->addSpacing(8);
    sideBox->addWidget(m_feedback, 1, Qt::AlignTop);
    sideBox->addWidget(m_scoreLabel);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_hint);
    buttons->addWidget(m_reveal);
    buttons->addWidget(m_next);
    sideBox->addLayout(buttons);
    auto* layout = new QHBoxLayout(this);
    layout->addWidget(m_board, 3);
    layout->addLayout(sideBox, 2);
    resize(880, 500);
    connect(m_board, &BoardWidget::moveRequested, this, [this](Move m) { answer(m); });
    connect(m_hint, &QPushButton::clicked, this, [this] { showHint(); });
    connect(m_reveal, &QPushButton::clicked, this, [this] { reveal(); });
    connect(m_next, &QPushButton::clicked, this, [this] { proceed(); });
    // Verdict of the engine: the position after my move, then after the game's (opponent to move in both)
    connect(m_annotator, &Annotator::finished, this, [this](bool completed) {
        if (m_state != State::Judging) return;
        const auto& evals = m_annotator->evaluations();
        if (!completed || evals.size() != 2 || !evals[0].valid || !evals[1].valid) {
            settle(0, tr("Le moteur n'a pas pu juger votre coup."));
            emit judged(0);
            return;
        }
        const double mine = -annotation::winningChances(evals[0]), games = -annotation::winningChances(evals[1]);
        const int points = std::min(pointsFor(mine, games), m_hinted ? 1 : 2);
        const QString tried = display::san(expected()->parent->pos.toSan(m_tried));
        settle(points, mine >= games - 0.05 ? tr("%1 est aussi bon d'après le moteur.").arg(tried)
                       : mine >= games - 0.1 ? tr("%1 est jouable, un peu moins bon d'après le moteur.").arg(tried)
                                             : tr("%1 est nettement moins bon d'après le moteur.").arg(tried));
        emit judged(points);
    });
    present();
}

void GuessDialog::updateScore()
{
    m_scoreLabel->setText(tr("Score : %1 / %2 · %3 coups de la partie trouvés").arg(m_score).arg(3 * std::min(m_index + (m_state == State::Shown ? 1 : 0), questionCount())).arg(m_exact));
}

void GuessDialog::present()
{
    m_hinted = false;
    m_tried = {};
    if (finished()) {
        m_state = State::Done;
        const Node* last = m_game.mainline().empty() ? m_game.root() : m_game.mainline().back();
        m_board->setPosition(last->pos, last->move, false);
        m_board->setGraphics({});
        m_board->setMovableSides(false, false);
        m_title->setText(questionCount() ? tr("Partie terminée") : tr("Aucun coup à deviner"));
        m_text->setText(questionCount() ? tr("%1 points sur %2 : %3 coups de la partie trouvés sur %4, %5 autres aussi bons.")
                                              .arg(m_score).arg(maxScore()).arg(m_exact).arg(questionCount()).arg(m_good)
                                        : tr("La partie n'a pas de coup de ce camp à partir du coup demandé."));
        m_feedback->clear();
        m_scoreLabel->clear();
        m_hint->setEnabled(false);
        m_reveal->setEnabled(false);
        m_next->setEnabled(true);
        m_next->setText(tr("Fermer"));
        return;
    }
    m_state = State::Asking;
    const Node* n = expected();
    const Node* before = n->parent;
    m_board->setGraphics({});
    m_board->setPosition(before->pos, before->parent ? before->move : Move(), m_index > 0); // the opponent's reply slides in
    m_board->setMovableSides(m_side == White, m_side == Black);
    m_title->setText(tr("Coup %1 — %2 sur %3").arg((n->ply() + 1) / 2).arg(m_index + 1).arg(questionCount()));
    QString text = m_side == White ? tr("Quel coup les Blancs ont-ils joué ?") : tr("Quel coup les Noirs ont-ils joué ?");
    if (before->parent) text = tr("Dernier coup : %1. ").arg(display::san(before->san)) + text;
    m_text->setText(text);
    m_hint->setEnabled(true);
    m_reveal->setEnabled(true);
    m_next->setEnabled(false);
    m_next->setText(tr("Continuer"));
    updateScore();
}

GuessDialog::Result GuessDialog::answer(Move move)
{
    if (finished() || m_state != State::Asking) return Result::Ignored;
    const Node* n = expected();
    const Position& pos = n->parent->pos;
    if (!pos.isLegal(move)) return Result::Ignored;
    if (move == n->move) {
        const int points = m_hinted ? 1 : 3;
        m_score += points;
        ++m_exact;
        m_feedback->setText(QStringLiteral("<span style='color:%1;font-weight:600;'>%2</span>").arg(theme::Accent.name(), tr("Exact : %1 (+%2)").arg(display::san(n->san)).arg(points)));
        ++m_index; // found: on to the next one at once
        present();
        return Result::Exact;
    }
    m_tried = move;
    m_board->setMovableSides(false, false);
    m_hint->setEnabled(false);
    m_reveal->setEnabled(false);
    const QStringList fens{QString::fromStdString(pos.after(move).fen()), QString::fromStdString(n->pos.fen())};
    if (m_engine.isValid() && m_annotator->start(m_engine, fens, 300, nullptr)) {
        m_state = State::Judging;
        m_board->setPosition(pos.after(move), move, false);
        m_feedback->setText(tr("Ce n'est pas le coup de la partie. Le moteur compare…"));
        return Result::Judging;
    }
    settle(0, QString());
    return Result::Missed;
}

void GuessDialog::settle(int points, const QString& verdict)
{
    const Node* n = expected();
    m_score += points;
    if (points == 2) ++m_good;
    m_state = State::Shown;
    QString text = tr("Le coup de la partie : <b>%1</b>").arg(display::san(n->san).toHtmlEscaped());
    if (!verdict.isEmpty()) text += QStringLiteral("<br>%1 (+%2)").arg(verdict.toHtmlEscaped()).arg(points);
    if (!n->comment.empty()) text += QStringLiteral("<br><br>") + QString::fromStdString(n->comment).toHtmlEscaped();
    m_feedback->setText(text);
    m_board->setPosition(n->pos, n->move, false);
    m_board->setGraphics(n->graphics);
    m_board->setMovableSides(false, false);
    m_hint->setEnabled(false);
    m_reveal->setEnabled(false);
    m_next->setEnabled(true);
    m_next->setFocus();
    updateScore();
}

void GuessDialog::showHint()
{
    if (finished() || m_state != State::Asking) return;
    m_hinted = true;
    m_board->setGraphics({Graphic{expected()->move.from, NoSquare, 'Y'}});
    m_feedback->setText(tr("La pièce à jouer est sur la case marquée."));
}

void GuessDialog::reveal()
{
    if (finished() || m_state != State::Asking) return;
    settle(0, QString());
}

void GuessDialog::proceed()
{
    if (m_state == State::Done) {
        accept();
        return;
    }
    if (m_state != State::Shown) return;
    m_feedback->clear();
    ++m_index;
    present();
}
