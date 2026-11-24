#pragma once

#include "chess/game.h"

#include <QDialog>

class BoardWidget;
class QLabel;
class QPushButton;

// Training on the questions of a game: each move marked "question" is to be found
// on the board. 10 points on the first attempt, 5 on the second, 2 afterwards, 0 if the solution is requested.
class QuizDialog : public QDialog {
    Q_OBJECT
public:
    explicit QuizDialog(const chess::Game& game, QWidget* parent = nullptr);

    int questionCount() const { return int(m_questions.size()); }
    int currentQuestion() const { return m_index; } // questionCount(): finished
    bool finished() const { return m_index >= questionCount(); }
    int score() const { return m_score; }
    int maxScore() const { return 10 * questionCount(); }
    const chess::Node* expected() const { return finished() ? nullptr : m_questions[size_t(m_index)]; }
    bool answer(chess::Move move); // move played on the board; true if it is right
    void showHint();
    void showSolution();
    void next();

private:
    void present();
    chess::Game m_game; // copy: the displayed game may change during the training
    std::vector<const chess::Node*> m_questions;
    int m_index = 0, m_score = 0, m_tries = 0;
    bool m_answered = false, m_solutionShown = false;
    BoardWidget* m_board;
    QLabel *m_title, *m_text, *m_feedback, *m_scoreLabel;
    QPushButton *m_hint, *m_solution, *m_next;
};
