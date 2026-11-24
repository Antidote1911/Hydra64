#pragma once

#include "chess/position.h"

#include <QDialog>
#include <QList>

#include <functional>

class BoardWidget;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class UciEngine;
struct EngineLine;

// Calculation of variations (after the Stoyko exercise of SCID vs PC, "calvar"): from a
// position, you play your variations on the board without seeing them played elsewhere and give
// each one your evaluation; Stockfish then judges the end of each variation and gives its own
// analysis of the position (three best lines).
class CalcVarDialog : public QDialog {
    Q_OBJECT
public:
    struct Variation {
        std::vector<chess::Move> moves;
        int userEval = 0;     // in centipawns, from White's point of view
        bool judged = false;
        bool engineMate = false;
        int engineEval = 0;   // centipawns (or moves to mate), from White's point of view
    };
    CalcVarDialog(const chess::Position& start, QWidget* parent = nullptr);
    ~CalcVarDialog() override;

    // For the tests and the dialog
    void playMove(chess::Move m);
    void finishVariation(int userEvalCp); // the current variation is recorded
    void judge(int msPerPosition = 800);  // Stockfish evaluates the variations and the position
    bool isJudging() const { return m_judging; }
    const QList<Variation>& variations() const { return m_vars; }
    QString startLines() const { return m_startLines; } // engine analysis, text

    // Evaluations offered (symbols) and their value in centipawns
    static QList<QPair<QString, int>> evalChoices();
    static QString evalText(int cp, bool mate = false); // « +1.20 », « #3 »

private:
    void showBoard(chess::Move last = {});
    void refreshTable();
    void nextJudgement();
    UciEngine* engine();

    chess::Position m_start, m_pos;
    std::vector<chess::Move> m_current;
    QList<Variation> m_vars;
    BoardWidget* m_board;
    QLabel* m_line;
    QComboBox* m_eval;
    QPushButton* m_save;
    QPushButton* m_reset;
    QPushButton* m_judge;
    QTableWidget* m_table;
    QLabel* m_result;
    UciEngine* m_engine = nullptr;
    bool m_engineReady = false;
    std::function<void()> m_pending;
    bool m_judging = false;
    int m_judgeIndex = -1; // -1: starting position (MultiPV 3)
    int m_ms = 800;
    QString m_fen;
    QList<EngineLine>* m_lines;
    QString m_startLines;
};
