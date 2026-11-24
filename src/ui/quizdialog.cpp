#include "quizdialog.h"

#include "boardwidget.h"
#include "chess/pgn.h"
#include "display.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>

using namespace chess;

QuizDialog::QuizDialog(const Game& game, QWidget* parent)
    : QDialog(parent)
{
    pgn::parseGame(pgn::writeGame(game), m_game);
    std::function<void(const Node*)> collect = [&](const Node* n) { // in reading order: main line, then variations
        if (n->parent && n->question) m_questions.push_back(n);
        for (const auto& c : n->children) collect(c.get());
    };
    collect(m_game.root());

    setWindowTitle(tr("Entraînement : %1 – %2").arg(QString::fromStdString(m_game.header("White")), QString::fromStdString(m_game.header("Black"))));
    m_board = new BoardWidget;
    m_board->setMinimumSize(440, 440);
    m_title = new QLabel;
    m_title->setStyleSheet(QStringLiteral("font-size:16px;font-weight:600;"));
    m_text = new QLabel;
    m_text->setWordWrap(true);
    m_feedback = new QLabel;
    m_feedback->setWordWrap(true);
    m_feedback->setTextFormat(Qt::RichText);
    m_scoreLabel = new QLabel;
    m_hint = new QPushButton(tr("Indice"));
    m_solution = new QPushButton(tr("Solution"));
    m_next = new QPushButton(tr("Question suivante"));
    auto* side = new QVBoxLayout;
    side->addWidget(m_title);
    side->addWidget(m_text);
    side->addSpacing(8);
    side->addWidget(m_feedback, 1, Qt::AlignTop);
    side->addWidget(m_scoreLabel);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_hint);
    buttons->addWidget(m_solution);
    buttons->addWidget(m_next);
    side->addLayout(buttons);
    auto* layout = new QHBoxLayout(this);
    layout->addWidget(m_board, 3);
    layout->addLayout(side, 2);
    resize(860, 500);
    connect(m_board, &BoardWidget::moveRequested, this, [this](Move m) { answer(m); });
    connect(m_hint, &QPushButton::clicked, this, [this] { showHint(); });
    connect(m_solution, &QPushButton::clicked, this, [this] { showSolution(); });
    connect(m_next, &QPushButton::clicked, this, [this] { next(); });
    present();
}

void QuizDialog::present()
{
    m_tries = 0;
    m_answered = m_solutionShown = false;
    m_scoreLabel->setText(tr("Score : %1 / %2").arg(m_score).arg(maxScore()));
    if (finished()) {
        m_title->setText(questionCount() ? tr("Entraînement terminé") : tr("Aucune question dans cette partie"));
        m_text->setText(questionCount() ? tr("%1 points sur %2.").arg(m_score).arg(maxScore())
                                        : tr("Pour en poser une : clic droit sur un coup de la notation → Question d'entraînement."));
        m_feedback->clear();
        m_board->setMovableSides(false, false);
        m_hint->setEnabled(false);
        m_solution->setEnabled(false);
        m_next->setText(tr("Fermer"));
        return;
    }
    const Node* n = expected();
    const Position& pos = n->parent->pos;
    const bool white = pos.sideToMove() == White;
    m_title->setText(tr("Question %1 sur %2").arg(m_index + 1).arg(questionCount()));
    m_text->setText(n->questionText.empty() ? (white ? tr("Trouvez le coup des Blancs.") : tr("Trouvez le coup des Noirs.")) : QString::fromStdString(n->questionText));
    m_feedback->clear();
    m_board->setFlipped(!white);
    m_board->setGraphics({});
    m_board->setPosition(pos, n->parent->parent ? n->parent->move : Move(), false);
    m_board->setMovableSides(white, !white);
    m_hint->setEnabled(true);
    m_solution->setEnabled(true);
    m_next->setEnabled(false);
    m_next->setText(m_index + 1 < questionCount() ? tr("Question suivante") : tr("Terminer"));
}

bool QuizDialog::answer(Move move)
{
    if (finished() || m_answered) return false;
    const Node* n = expected();
    ++m_tries;
    if (!(move == n->move)) {
        m_feedback->setText(QStringLiteral("<span style='color:#e08a00;'>%1</span>").arg(tr("Ce n'est pas le coup attendu. Essayez encore.")));
        m_board->setPosition(n->parent->pos, n->parent->parent ? n->parent->move : Move(), false); // the piece goes back
        return false;
    }
    const int points = m_solutionShown ? 0 : m_tries == 1 ? 10 : m_tries == 2 ? 5 : 2;
    m_score += points;
    m_answered = true;
    QString text = QStringLiteral("<span style='color:%1;font-weight:600;'>%2</span>").arg(theme::Accent.name(), tr("Exact : %1 (+%2)").arg(display::san(n->san)).arg(points));
    if (!n->comment.empty()) text += QStringLiteral("<br><br>") + QString::fromStdString(n->comment).toHtmlEscaped();
    m_feedback->setText(text);
    m_board->setPosition(n->pos, n->move, true);
    m_board->setGraphics(n->graphics);
    m_board->setMovableSides(false, false);
    m_hint->setEnabled(false);
    m_solution->setEnabled(false);
    m_next->setEnabled(true);
    m_scoreLabel->setText(tr("Score : %1 / %2").arg(m_score).arg(maxScore()));
    return true;
}

void QuizDialog::showHint()
{
    if (finished() || m_answered) return;
    const Node* n = expected();
    m_tries = std::max(m_tries, 1); // a hint costs one attempt
    m_board->setGraphics({Graphic{n->move.from, NoSquare, 'Y'}});
    m_feedback->setText(tr("La pièce à jouer est sur la case marquée."));
}

void QuizDialog::showSolution()
{
    if (finished() || m_answered) return;
    m_solutionShown = true;
    answer(expected()->move);
}

void QuizDialog::next()
{
    if (finished()) {
        accept();
        return;
    }
    if (!m_answered) return;
    ++m_index;
    present();
}
