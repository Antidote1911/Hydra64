#pragma once

#include "deepanalysis.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

// Options of a deep analysis project (General and IDeA tabs, as in Aquarium).
class DeepProjectDialog : public QDialog {
    Q_OBJECT
public:
    // `index`: project being edited (-1: new project, root = `root`).
    DeepProjectDialog(DeepAnalysis* deep, int index, const chess::Position& root, QWidget* parent = nullptr);
    QString name() const;
    deep::Options options() const;
    DeepAnalysis::Status status() const;
    int share() const;
    QString master() const;

private:
    QLineEdit* m_name;
    QComboBox* m_status;
    QSpinBox* m_share;
    QComboBox* m_master;
    QDoubleSpinBox* m_minTime;
    QSpinBox* m_minDepth;
    QDoubleSpinBox* m_maxTime;
    QCheckBox* m_waitDepth;
    QCheckBox* m_spectators;
    QDoubleSpinBox* m_specTime;
    QSpinBox* m_specDepth;
    QCheckBox* m_autoExpand;
    QSpinBox* m_limitWhite;
    QSpinBox* m_limitBlack;
    QSpinBox* m_width;
    QSpinBox* m_falseAlarm;
    QComboBox* m_preferred;
    QLineEdit* m_thematic;
    QSpinBox* m_maxPlies;
    QCheckBox* m_bounds;
    QSpinBox* m_low;
    QSpinBox* m_high;
};

// Roots of a project: enabled or not, eval, position, comment.
class DeepRootsDialog : public QDialog {
    Q_OBJECT
public:
    DeepRootsDialog(DeepAnalysis* deep, int project, const chess::Position& board, QWidget* parent = nullptr);

private:
    void fill();
    void apply();
    DeepAnalysis* m_deep;
    int m_project;
    chess::Position m_board;
    QTableWidget* m_table;
};

// Deep analysis engines: engine, instances, threads and memory per instance.
class DeepEnginesDialog : public QDialog {
    Q_OBJECT
public:
    explicit DeepEnginesDialog(const QList<DeepAnalysis::EngineSlot>& engineSlots, QWidget* parent = nullptr);
    QList<DeepAnalysis::EngineSlot> engineSlots() const;

private:
    void addRow(const DeepAnalysis::EngineSlot& s);
    QTableWidget* m_table;
};

// Custom task: its own time and depth.
class DeepCustomTaskDialog : public QDialog {
    Q_OBJECT
public:
    explicit DeepCustomTaskDialog(QWidget* parent = nullptr);
    int timeMs() const;
    int depth() const;
    bool alternative() const;

private:
    QDoubleSpinBox* m_time;
    QSpinBox* m_depth;
    QCheckBox* m_alternative;
};

// Text of a score (centipawns, point of view of the side to move) seen from White: "+0.35", "#3".
QString deepScoreText(int cp, chess::Color toMove);
