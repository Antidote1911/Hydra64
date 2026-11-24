#pragma once

#include "tournament.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

// Choice of an engine for the tournament, with its name and its UCI settings specific to the tournament.
// Nothing is written to the engine manager: the settings stay in the participant.
class ParticipantDialog : public QDialog {
    Q_OBJECT
public:
    // `initial`: participant to edit; without an engine, the first available engine.
    explicit ParticipantDialog(const Tournament::Participant& initial, QWidget* parent = nullptr);

    Tournament::Participant participant() const;

    // For the tests: chosen engine and value of an option.
    void selectEngine(const QString& engineId);
    void setOptionValue(const QString& option, const QString& value);

private:
    void rebuildOptions();
    void restyleRow(int row);
    QString generalValue(const QString& option) const; // value in the engine manager

    Tournament::Participant m_participant;
    EngineConfig m_engine; // engine of the registry (options, general settings)
    bool m_nameEdited = false;

    QComboBox* m_engineBox;
    QLineEdit* m_name;
    QLabel* m_identity;
    QLineEdit* m_filter;
    QTableWidget* m_options;
};
