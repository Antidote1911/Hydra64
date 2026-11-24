#pragma once

#include "chess/game.h"
#include "engine/engineregistry.h"

#include <QDialog>

class Annotator;
class BoardWidget;
class QLabel;
class QPushButton;

// Replaying a game while guessing the moves of one side (ChessBase: "Visionner la partie"): each
// move of that side is to be found on the board; the opponent's replies are played by themselves.
// 3 points for the move of the game. Another move is judged by the engine against it: 2 points
// if it is as good (0.05 of winning chances at most below), 1 if it is close (0.1 at most), 0
// otherwise. A hint leaves 1 point at most; the move shown gives none.
class GuessDialog : public QDialog {
    Q_OBJECT
public:
    // `fromMove`: number of the first move to find (the ones before are played).
    GuessDialog(const chess::Game& game, chess::Color side, int fromMove, QWidget* parent = nullptr);

    // Asks the side and the first move; false if cancelled. Proposes the winner's side.
    static bool ask(QWidget* parent, const chess::Game& game, chess::Color* side, int* fromMove);
    // Engine judging the moves that are not the game's (invalid: none, such a move gives 0).
    void setEngine(const EngineConfig& engine) { m_engine = engine; }

    enum class Result { Exact, Judging, Missed, Ignored };
    Result answer(chess::Move move); // move played on the board
    void showHint();
    void reveal();  // shows the move of the game (no point)
    void proceed(); // after a miss: on to the next move

    int questionCount() const { return int(m_questions.size()); }
    int currentQuestion() const { return m_index; }
    bool finished() const { return m_index >= questionCount(); }
    bool waiting() const { return m_state == State::Judging; }
    int score() const { return m_score; }
    int maxScore() const { return 3 * questionCount(); }
    int exactCount() const { return m_exact; }
    const chess::Node* expected() const { return finished() ? nullptr : m_questions[size_t(m_index)]; }
    // Points for a move that is not the game's, from the winning chances of the player after each.
    static int pointsFor(double mine, double games);

signals:
    void judged(int points); // the engine has given its verdict on the last answer

private:
    enum class State { Asking, Judging, Shown, Done };
    void present();
    void settle(int points, const QString& verdict); // a move that was not the game's: verdict shown, game's move played
    void updateScore();

    chess::Game m_game; // copy: the displayed game may change meanwhile
    std::vector<const chess::Node*> m_questions;
    chess::Color m_side;
    int m_index = 0, m_score = 0, m_exact = 0, m_good = 0;
    bool m_hinted = false;
    State m_state = State::Asking;
    chess::Move m_tried;
    EngineConfig m_engine;
    Annotator* m_annotator;
    BoardWidget* m_board;
    QLabel *m_title, *m_text, *m_feedback, *m_scoreLabel;
    QPushButton *m_hint, *m_reveal, *m_next;
};
