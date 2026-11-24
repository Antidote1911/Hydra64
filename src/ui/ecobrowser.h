#pragma once

#include <QDialog>

#include <vector>

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// ECO browser (after the "ECO browser" of SCID vs PC): volumes A–E, groups A0–A9, codes
// A00–A99 and their lines (name, moves), with the number of games of the open database per code.
// "Filter the database" searches the code (or the range); "Play the line" opens it in the Analysis
// page.
class EcoBrowser : public QDialog {
    Q_OBJECT
public:
    // counts[eco] (1 = A00 … 500 = E99): games of the database; empty if no database
    EcoBrowser(std::vector<int> counts, QWidget* parent = nullptr);

    QTreeWidget* tree() const { return m_tree; }
    // ECO range of the item (codes 1…500), {0, 0} if none
    static std::pair<int, int> rangeOf(const QTreeWidgetItem* item);

signals:
    void filterRequested(int ecoFrom, int ecoTo);
    void lineRequested(const QString& sanMoves);

private:
    void build();
    void applySearch(const QString& text);
    std::vector<int> m_counts;
    QTreeWidget* m_tree;
    QLineEdit* m_search;
    QLabel* m_info;
};
