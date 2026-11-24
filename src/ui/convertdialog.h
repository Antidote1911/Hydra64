#pragma once

#include "conversion.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// File → Convert…: choice of the source (any readable format), of the target format
// (Hydra64 or open) and of the destination. The conversion itself is started by the main
// window (progress, opening of the result).
class ConvertDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConvertDialog(const QString& source = {}, QWidget* parent = nullptr);

    conversion::Request request() const;
    bool openResult() const;
    void setSource(const QString& path);
    void setTarget(conversion::Target target); // if the source allows it

private:
    void chooseSource();
    void chooseDestination();
    void update();          // detected format, possible target formats
    void suggestDestination();

    QLineEdit* m_source;
    QLabel* m_detected;
    QComboBox* m_target;
    QLineEdit* m_destination;
    QSpinBox* m_depth;
    QLabel* m_depthLabel;
    // Opening tree made from a game database
    QSpinBox* m_treePlies;
    QLabel* m_treePliesLabel;
    QSpinBox* m_treeMinGames;
    QLabel* m_treeMinGamesLabel;
    QCheckBox* m_open;
    QPushButton* m_convert;
    bool m_destinationEdited = false;
};
