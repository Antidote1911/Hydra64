#pragma once

#include "solutiontest.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Engine → Solution test…: collection of tactical positions (Tactics Marathon, WAC…), time per
// position, results as they come (move found, solving time) and final score.
class SolutionTestDialog : public QDialog {
    Q_OBJECT
public:
    explicit SolutionTestDialog(QWidget* parent = nullptr);

    SolutionTest* test() const { return m_test; }
    void setFile(const QString& path);
    void setMovetime(double seconds);
    bool startTest(); // with the dialog's settings; false if impossible (message displayed)

signals:
    void analyseRequested(const QString& fen); // double-click on a position

private:
    void chooseFile();
    void loadFile();
    void toggle();
    void refreshEngines();
    void updateRow(int index);
    void updateSummary();
    void setRunning(bool running);

    SolutionTest* m_test;
    QList<TestPosition> m_positions;
    QComboBox* m_engine;
    QLineEdit* m_file;
    QDoubleSpinBox* m_time;
    QPushButton* m_run;
    QTableWidget* m_table;
    QLabel* m_summary;
};
