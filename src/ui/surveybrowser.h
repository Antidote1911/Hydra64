#pragma once

#include "surveyindex.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QTreeWidget;

// Opening surveys of a database (surveyindex): one row per survey, sorted by ECO code, with a
// search on the code, the title or the author. Double-click or "Open" opens the survey as a
// database.
class SurveyBrowser : public QDialog {
    Q_OBJECT
public:
    SurveyBrowser(QList<surveyindex::Survey> surveys, const QString& databaseName, QWidget* parent = nullptr);

    QTreeWidget* tree() const { return m_tree; }
    void setSearch(const QString& text);
    int visibleCount() const;

signals:
    void openRequested(const QString& path);

private:
    void applySearch(const QString& text);
    QList<surveyindex::Survey> m_surveys;
    QTreeWidget* m_tree;
    QLineEdit* m_search;
    QLabel* m_info;
};
