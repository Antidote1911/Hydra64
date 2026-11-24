#pragma once

#include "deepanalysis.h"

#include <QWidget>

class GameSession;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTimer;
class QToolButton;

// Squares of the step's tasks (grey: done, green: running, yellow: pending, red:
// manual), like IDeA's "Stage status" window.
class DeepTaskSquares : public QWidget {
    Q_OBJECT
public:
    enum State { Done, Running, Waiting, Manual };
    struct Square {
        State state;
        uint64_t id = 0;
    };
    explicit DeepTaskSquares(QWidget* parent = nullptr);
    void setSquares(const QList<Square>& squares);
    uint64_t selected() const { return m_selected; }
    void select(uint64_t id);
    QSize sizeHint() const override { return {300, 80}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

signals:
    void selectedChanged(uint64_t id);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    int columns() const;
    QList<Square> m_squares;
    uint64_t m_selected = 0;
};

// "Deep analysis" panel (Analysis page): tree of the board position, tasks
// of the step, projects (control centre) and note lists.
class DeepPanel : public QWidget {
    Q_OBJECT
public:
    DeepPanel(DeepAnalysis* deep, GameSession* session, QWidget* parent = nullptr);

    int project() const { return m_deep->defaultProject(); }
    // Tasks sent from the displayed position (buttons of the Tree tab)
    bool sendCurrentPosition();
    bool sendAlternative();
    bool sendAutoPlay(int plies);
    int sendAllPositions();
    bool sendInfinite(int slot);
    bool makeRoot();
    void copyBestLine(); // F4: best line of the tree into the notation
    void setMark(chess::Move move, deep::Mark mark);
    void addNote(deep::NoteList list, const QString& text = {});
    void newProject();
    void editProject();
    void toggleRunning();
    QTableWidget* treeTable() const { return m_tree; }

signals:
    void movePicked(chess::Move m);
    void bestMoveChanged(chess::Move m); // best move of the tree in the position (arrow)
    void gameRequested(const chess::Game& game); // go to a position of the tree (notes)
    void statusMessage(const QString& msg);

private:
    QWidget* buildTreeTab();
    QWidget* buildTasksTab();
    QWidget* buildProjectsTab();
    QWidget* buildNotesTab();
    void refreshTree();
    void refreshTasks();
    void refreshTaskDetails();
    void refreshProjects();
    void refreshNotes();
    void refreshToolbar();
    void scheduleRefresh();
    bool currentTask(deep::Task& t); // root and path of the displayed position
    uint64_t send(deep::Task t);
    void treeMenu(const QPoint& pos);
    void showNode(uint32_t node);
    bool lineTo(uint32_t node, std::string& fen, std::vector<chess::Move>& moves) const;
    void exportProject(int kind);
    void addEpd(bool results);
    void editEngines();
    void editRoots();

    DeepAnalysis* m_deep;
    GameSession* m_session;
    QTabWidget* m_tabs;
    QComboBox* m_projectBox;
    QToolButton* m_run;
    QToolButton* m_menu;
    // Tree
    QLabel* m_treeHeader;
    QTableWidget* m_tree;
    QSpinBox* m_autoPlies;
    QList<chess::Move> m_treeMoves;
    // Tasks
    QLabel* m_stage;
    DeepTaskSquares* m_squares;
    QLabel* m_taskDetails;
    QPushButton* m_insert;
    QPushButton* m_finish;
    QPushButton* m_delete;
    QPushButton* m_infinite;
    // Projects
    QTableWidget* m_projects;
    QLabel* m_projectStatus;
    // Notes
    QComboBox* m_noteList;
    QTableWidget* m_notes;
    QLabel* m_noteNav;
    QList<uint32_t> m_noteNodes;
    int m_noteIndex = -1;

    QTimer* m_refresh;
    bool m_treeDirty = false, m_tasksDirty = false, m_projectsDirty = false;
    bool m_filling = false;
};
